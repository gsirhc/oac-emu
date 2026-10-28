import { test, expect } from "./fixtures";
import { boot, typeStr, insertFloppy } from "./helpers";
import path from "path";
import fs from "fs";
import os from "os";
import { execFileSync } from "child_process";

// Serial mouse capture is off for a first visit. Preference lives in retro8080.ibmpcatMouse.
test.describe("serial mouse", () => {
  test("capture checkbox is unchecked by default", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await page.evaluate(() => localStorage.removeItem("retro8080.ibmpcatMouse"));
    await page.reload();
    await page.waitForFunction(() => !!(window as any).__test?.machine, null, { timeout: 15000 });
    await expect(page.locator("#mouseCaptureEnabled")).not.toBeChecked();
  });

  test("Enable Mouse preference persists across reload", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await page.evaluate(() => localStorage.removeItem("retro8080.ibmpcatMouse"));
    await page.reload();
    await page.waitForFunction(() => !!(window as any).__test?.machine, null, { timeout: 15000 });

    await page.locator("#mouseCaptureEnabled").check();
    await expect
      .poll(() => page.evaluate(() => localStorage.getItem("retro8080.ibmpcatMouse")))
      .toBe("1");

    await page.reload();
    await page.waitForFunction(() => !!(window as any).__test?.machine, null, { timeout: 15000 });
    await expect(page.locator("#mouseCaptureEnabled")).toBeChecked();
    await page.evaluate(() => localStorage.removeItem("retro8080.ibmpcatMouse"));
  });

  test("checking/unchecking it doesn't affect the running machine", async ({ page }) => {
    await boot(page, { expectScreen: null });
    const cycles1 = await page.evaluate(
      () => (window as any).__test.machine.totalCycles()
    );

    await page.locator("#mouseCaptureEnabled").check();

    await expect
      .poll(() => page.evaluate(() => (window as any).__test.machine.totalCycles()))
      .toBeGreaterThan(cycles1);
  });

  test("injectSerialMouse is callable via the embind API", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await page.evaluate(() => {
      const m = (window as any).__test.machine;
      m.injectSerialMouse(5, -3, 0);
      m.injectSerialMouse(0, 0, 0x01);
      m.injectSerialMouse(0, 0, 0x00);
    });
  });

  test("bezel button mirrors the checkbox", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await page.evaluate(() => localStorage.removeItem("retro8080.ibmpcatMouse"));
    await page.reload();
    await page.waitForFunction(() => !!(window as any).__test?.machine, null, { timeout: 15000 });
    await expect(page.locator("#mouseCaptureBtn")).toHaveAttribute("aria-pressed", "false");
    await page.locator("#mouseCaptureBtn").click();
    await expect(page.locator("#mouseCaptureEnabled")).toBeChecked();
    await expect(page.locator("#mouseCaptureBtn")).toHaveAttribute("aria-pressed", "true");
  });

  test("CuteMouse INT 33h sees mickeys after injectSerialMouse", async ({ page }) => {
    test.setTimeout(180_000);
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mousmic-"));
    const com = path.join(dir, "MOUSMIC.COM");
    const img = path.join(dir, "mousmic.img");
    // INT 33 AX=000Bh → store CX/DX at 0000:0500, exit.
    fs.writeFileSync(
      com,
      Buffer.from([
        0xb8, 0x0b, 0x00, 0xcd, 0x33, 0x1e, 0x31, 0xc0, 0x8e, 0xd8, 0x89, 0x0e, 0x00, 0x05, 0x89, 0x16, 0x02, 0x05, 0x1f,
        0xb8, 0x00, 0x4c, 0xcd, 0x21,
      ]),
    );
    execFileSync("dd", ["if=/dev/zero", `of=${img}`, "bs=512", "count=720"], { stdio: "ignore" });
    execFileSync("mformat", ["-i", img, "-f", "360", "::"]);
    execFileSync("mcopy", ["-i", img, com, "::MOUSMIC.COM"]);

    await boot(page);
    // FDAUTO already loads CTMOUSE; this floppy only holds the mickeys probe.
    await insertFloppy(page, 0, img);

    await page.evaluate(() => {
      const m = (window as any).__test.machine;
      for (let i = 0; i < 20; i++) m.injectSerialMouse(40, 0, 0);
      m.runCycles(5_000_000);
    });

    await typeStr(page, "A:\\MOUSMIC");
    await expect.poll(() => page.evaluate(() => {
      const m = (window as any).__test.machine;
      return m.ramByte(0x500) | (m.ramByte(0x501) << 8);
    }), { timeout: 30_000 }).not.toBe(0);
    fs.rmSync(dir, { recursive: true, force: true });
  });
});
