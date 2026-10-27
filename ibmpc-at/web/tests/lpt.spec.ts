import { test, expect } from "./fixtures";
import { boot, expectDownload, typeStr, openKeyboard, boardKey, screenText } from "./helpers";

test.describe("LPT1 printer capture", () => {
  test("dialog stays closed until something prints", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await expect(page.locator("#lptDialog")).toBeHidden();
  });

  test("Print Screen dumps the text screen to LPT1, not a *", async ({ page }) => {
    await boot(page);
    const before = await screenText(page);
    await openKeyboard(page);
    await boardKey(page, "PrintScreen").click();
    await expect(page.locator("#lptDialog")).toBeVisible({ timeout: 30_000 });
    await expect
      .poll(() => page.evaluate(() => (window as any).__test.machine.lptSize()))
      .toBeGreaterThan(80 * 20);
    expect(await screenText(page)).toBe(before);
  });

  test("ECHO to PRN opens the dialog and download works", async ({ page }) => {
    await boot(page);
    await typeStr(page, "ECHO HELLO>PRN");
    await expect(page.locator("#lptDialog")).toBeVisible({ timeout: 30_000 });
    await expect(page.locator("#lptDialogBody")).toContainText(/bytes to the printer/i);

    const download = await expectDownload(page, () => page.locator("#lptDownloadBtn").click());
    expect(download.suggestedFilename()).toBe("ibmpcat-lpt1.prn");
    await expect(page.locator("#lptDialog")).toBeHidden();
    expect(await page.evaluate(() => (window as any).__test.machine.lptSize())).toBe(0);
  });

  test("Discard empties the capture", async ({ page }) => {
    await boot(page);
    await typeStr(page, "ECHO X>PRN");
    await expect(page.locator("#lptDialog")).toBeVisible({ timeout: 30_000 });
    await page.locator("#lptDiscardBtn").click();
    await expect(page.locator("#lptDialog")).toBeHidden();
    expect(await page.evaluate(() => (window as any).__test.machine.lptSize())).toBe(0);
  });

  test("lptBytes embind returns the capture", async ({ page }) => {
    await boot(page);
    await typeStr(page, "ECHO AB>PRN");
    await expect
      .poll(() => page.evaluate(() => (window as any).__test.machine.lptSize()))
      .toBeGreaterThan(0);
    const text = await page.evaluate(() => {
      const b = (window as any).__test.machine.lptBytes();
      return String.fromCharCode(...b);
    });
    expect(text.toLowerCase()).toMatch(/ab/);
  });
});
