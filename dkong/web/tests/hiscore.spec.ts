import { test, expect } from "@playwright/test";
import { readFileSync } from "fs";
import { HWTEST_FILES } from "./cabinet";

function userSet() {
  return HWTEST_FILES.map((path) => {
    const name = path.split("/").pop()!;
    let buffer = readFileSync(path);
    if (name === "program.bin") {
      // Flip a byte in the unused padding, not byte 0 (the JP at reset is
      // flag-dependent on JP NZ if its low bit flips, which corrupts the CPU).
      buffer = Buffer.from(buffer);
      buffer[0x300] ^= 1;
    }
    return { name, mimeType: "application/octet-stream" as const, buffer };
  });
}

// readHiscore() returns HI at $60B8 (3) then the $6100 rank table ($AA).
// Factory HI is ROM $01C0 ($50,$76,$00); the self-test leaves $3565 blank.
const table = (hi: number[], rank0 = 0) => [...hi, rank0, ...new Array(0xA9).fill(0)];
// Cold start LDIRs the factory scores; write ours after it.
const booted = (page: import("@playwright/test").Page) =>
  page.waitForFunction(() => (window as any).__test.machine.state().nmiEnable);
const SAMPLE = table([0x00, 0x76, 0x00], 0x07);
const FACTORY = table([0x50, 0x76, 0x00]);

test.describe("HIGH SCORE persist", () => {
  test.describe.configure({ mode: "serial" });
  test.beforeEach(async ({ page }) => {
    await page.goto("/?test=1");
    await page.waitForFunction(() => (window as any).__test);
  });

  test("Reset is present and disabled on the self-test ROM", async ({ page }) => {
    const btn = page.locator("#resetHiscore");
    await expect(btn).toBeVisible();
    await expect(btn).toBeDisabled();
    await expect(page.locator("p.legal").filter({ hasText: "HIGH SCORE is kept" })).toBeVisible();
  });

  test("HI-SCORE at $60B8 is saved and poked back after a reload", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(userSet());
    await expect(page.locator("#romStatus")).toHaveText("Loaded ROM set");
    await booted(page);
    await expect(page.locator("#resetHiscore")).toBeEnabled();

    await page.evaluate(async (bytes) => {
      const t = (window as any).__test;
      t.writeHiscore(bytes);
      await t.saveHiscoreNow(t.readHiscore());
    }, SAMPLE);

    await page.reload();
    await expect(page.locator("#romStatus")).toHaveText("Loaded stored ROM set");

    await expect.poll(() => page.evaluate(() => (window as any).__test.hiscoreRestored)).toBe(true);
    expect(await page.evaluate(() => (window as any).__test.readHiscore())).toEqual(SAMPLE);
  });

  test("the restore waits until factory HI-SCORE is in RAM", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(userSet());
    await expect(page.locator("#romStatus")).toHaveText("Loaded ROM set");
    await booted(page);
    await page.evaluate(async (bytes) => {
      const t = (window as any).__test;
      t.writeHiscore(bytes);
      await t.saveHiscoreNow(t.readHiscore());
    }, SAMPLE);

    // Hold a non-factory HI after every slice so ready() stays false.
    await page.addInitScript(() => {
      const w = window as any;
      let t: any;
      w.__holdHiscore = true;
      Object.defineProperty(w, "__test", {
        configurable: true,
        get: () => t,
        set: (v) => {
          t = v;
          const run = v.machine.runCycles.bind(v.machine);
          v.machine.runCycles = (n: number) => { run(n); if (w.__holdHiscore) v.writeHiscore([0x00, 0x11, 0x00, ...new Array(0xAA).fill(0)]); };
        },
      });
    });
    await page.reload();
    await expect(page.locator("#romStatus")).toHaveText("Loaded stored ROM set");
    await page.waitForFunction(() => { const m = (window as any).__test.machine.state(); return m.frames > 120 && m.nmiEnable; });
    expect(await page.evaluate(() => (window as any).__test.hiscoreRestored)).toBe(false);

    await page.evaluate((factory) => {
      (window as any).__holdHiscore = false;
      (window as any).__test.writeHiscore(factory);
    }, FACTORY);
    await expect.poll(() => page.evaluate(() => (window as any).__test.hiscoreRestored)).toBe(true);
    expect(await page.evaluate(() => (window as any).__test.readHiscore())).toEqual(SAMPLE);
  });

  test("Remove ROMs disables Reset again", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(userSet());
    await expect(page.locator("#resetHiscore")).toBeEnabled();
    await page.locator("#removeRomBtn").click();
    await expect(page.locator("#romStatus")).toHaveText("Running test ROM");
    await page.waitForFunction(() => !!(window as any).__test?.machine);
    await expect(page.locator("#resetHiscore")).toBeDisabled();
  });

  test("Reset Cancel leaves the saved table in place", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(userSet());
    await expect(page.locator("#romStatus")).toHaveText("Loaded ROM set");
    await booted(page);
    await page.evaluate(async (bytes) => {
      const t = (window as any).__test;
      t.writeHiscore(bytes);
      await t.saveHiscoreNow(t.readHiscore());
    }, SAMPLE);

    await page.locator("#resetHiscore").click();
    await expect(page.locator("#hiscoreResetHint")).toBeVisible();
    await page.locator("#hiscoreResetCancel").click();
    await expect(page.locator("#hiscoreResetHint")).toBeHidden();

    expect(await page.evaluate(() => (window as any).__test.readHiscore())).toEqual(SAMPLE);
  });

  test("Reset confirm clears the saved table", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(userSet());
    await expect(page.locator("#romStatus")).toHaveText("Loaded ROM set");
    await booted(page);
    await page.evaluate(async (bytes) => {
      const t = (window as any).__test;
      t.writeHiscore(bytes);
      await t.saveHiscoreNow(t.readHiscore());
    }, SAMPLE);

    expect(Object.keys(await page.evaluate(() => (window as any).__test.savedHiscores()))).toHaveLength(1);

    await page.locator("#resetHiscore").click();
    await page.locator("#hiscoreResetOk").click();
    await expect(page.locator("#hiscoreResetHint")).toBeHidden();
    await expect.poll(async () => Object.keys(await page.evaluate(() => (window as any).__test.savedHiscores()))).toHaveLength(0);

    await expect.poll(() => page.evaluate(() => (window as any).__test.hiscoreRestored)).toBe(true);
    expect(await page.evaluate(() => (window as any).__test.readHiscore())).toEqual(FACTORY);
  });
});
