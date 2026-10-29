import { test, expect } from "./fixtures";
import { boot, insertFloppy, typeStr, waitForScreen, setPowerSwitch, emuWait, rafTicks, ejectFloppy } from "./helpers";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";

type Log = { motorOn: number; motorOff: number; access: number[]; seeks: { src: number; steps: number }[]; frames: number; errors: number };
const log = (page): Promise<Log> => page.evaluate(() => JSON.parse(JSON.stringify((window as any).__test.driveNoiseLog)));

// A 360KB FAT12 disk with FAR.TXT in cluster 347, the first sector of cylinder 39.
function farFileImage(): string {
  const img = Buffer.alloc(368640, 0);
  const b = Buffer.alloc(512, 0);
  b.set([0xeb, 0x3c, 0x90], 0);
  b.write("MSDOS5.0", 3, "ascii");
  b.writeUInt16LE(512, 11);
  b[13] = 2;
  b.writeUInt16LE(1, 14);
  b[16] = 2;
  b.writeUInt16LE(112, 17);
  b.writeUInt16LE(720, 19);
  b[21] = 0xfd;
  b.writeUInt16LE(2, 22);
  b.writeUInt16LE(9, 24);
  b.writeUInt16LE(2, 26);
  b[510] = 0x55;
  b[511] = 0xaa;
  b.copy(img, 0);
  const cluster = 347;
  for (const fat of [1, 3]) {
    const base = fat * 512;
    img.set([0xfd, 0xff, 0xff], base);
    const off = base + Math.floor(cluster * 1.5);
    // odd cluster: high 12 bits of the pair
    img[off] = (img[off] & 0x0f) | 0xf0;
    img[off + 1] = 0xff;
  }
  const dir = Buffer.alloc(32, 0);
  dir.write("FAR     TXT", 0, "ascii");
  dir[11] = 0x20;
  dir.writeUInt16LE(cluster, 26);
  dir.writeUInt32LE(9, 28);
  dir.copy(img, 5 * 512);
  img.write("FAR AWAY\n", (12 + (cluster - 2) * 2) * 512, "ascii");
  const p = path.join(os.tmpdir(), `ibmpcat-far-${Date.now()}-${Math.random().toString(36).slice(2)}.img`);
  fs.writeFileSync(p, img);
  return p;
}

test.describe("drive noise", () => {
  test("is unchecked on every page load", async ({ page }) => {
    await boot(page);
    const box = page.locator("#driveNoiseEnabled");
    await expect(box).not.toBeChecked();
    await expect(page.locator("label", { has: box })).toHaveAttribute("title", /Synthesized/);
    await box.check();
    await page.reload();
    await page.waitForFunction(() => !!(window as any).__test?.machine, null, { timeout: 15000 });
    await expect(box).not.toBeChecked();
  });

  test("muted, the controllers log nothing", async ({ page }) => {
    await boot(page);
    await insertFloppy(page, 1, farFileImage());
    await typeStr(page, "TYPE B:FAR.TXT");
    await waitForScreen(page, /FAR AWAY/, 60_000);
    expect(await page.evaluate(() => (window as any).__test.machine.driveSoundEvents().length)).toBe(0);
    const l = await log(page);
    expect(l.seeks).toEqual([]);
    expect(l.frames).toBe(0);
  });

  test("a read on cylinder 39 steps the B: head 39 times, then unchecking stops it", async ({ page }) => {
    await boot(page);
    await insertFloppy(page, 1, farFileImage());
    await page.locator("#driveNoiseEnabled").check();
    await typeStr(page, "TYPE B:FAR.TXT");
    await waitForScreen(page, /FAR AWAY/, 60_000);
    await expect.poll(async () => (await log(page)).seeks).toContainEqual({ src: 1, steps: 39 });
    const l = await log(page);
    expect(l.motorOn).toBeGreaterThan(0);
    expect(l.access[1]).toBeGreaterThan(0);
    expect(l.seeks.every((s) => s.src === 1)).toBe(true);
    expect(l.errors).toBe(0);
    expect(l.frames).toBeGreaterThan(0);

    await page.locator("#driveNoiseEnabled").uncheck();
    await typeStr(page, "CLS");
    await typeStr(page, "TYPE B:FAR.TXT");
    await waitForScreen(page, /FAR AWAY/, 60_000);
    await emuWait(page, 3000);
    await rafTicks(page);
    expect((await log(page)).seeks.length).toBe(l.seeks.length);
  });

  test("swapping the disk leaves B:'s head on cylinder 39, so the next access grinds it home", async ({ page }) => {
    await boot(page);
    const img = farFileImage();
    await insertFloppy(page, 1, img);
    await page.locator("#driveNoiseEnabled").check();
    await typeStr(page, "TYPE B:FAR.TXT");
    await waitForScreen(page, /FAR AWAY/, 60_000);
    await ejectFloppy(page, 1);
    await insertFloppy(page, 1, img);
    await page.evaluate(() => { (window as any).__test.driveNoiseLog.seeks.length = 0; });
    await typeStr(page, "DIR B:");
    await waitForScreen(page, /FAR\s+TXT/, 60_000);
    expect((await log(page)).seeks[0]).toEqual({ src: 1, steps: 39 });
  });

  test("C: sounds for every sector it reads, seek or not, while its light is on", async ({ page }) => {
    await boot(page);
    await page.locator("#driveNoiseEnabled").check();
    await typeStr(page, "DIR");
    await waitForScreen(page, /bytes free/, 60_000);
    await expect.poll(async () => (await log(page)).access[2]).toBeGreaterThan(0);
    expect((await log(page)).access.slice(0, 2)).toEqual([0, 0]);
  });

  test("a power-on with it checked logs the POST's floppy motors and C:'s seeks", async ({ page }) => {
    await boot(page);
    await page.locator("#driveNoiseEnabled").check();
    await setPowerSwitch(page, false);
    await setPowerSwitch(page, true);
    await page.waitForFunction(() => !!(window as any).__test?.machine);
    await waitForScreen(page, /C:\\>/);
    const l = await log(page);
    expect(l.motorOn).toBeGreaterThan(0);
    expect(l.seeks.filter((s) => s.src === 2 && s.steps === 1).length).toBeGreaterThan(5);
    expect(l.errors).toBe(0);
  });
});
