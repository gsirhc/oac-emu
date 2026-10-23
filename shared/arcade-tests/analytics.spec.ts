import { test, expect } from "@playwright/test";
import * as path from "path";

// GoatCounter events, recorded through a stubbed goatcounter.count.
const MACHINE = path.basename(path.dirname(process.cwd()));

test.describe("analytics", () => {
  test.beforeEach(async ({ page }) => {
    await page.addInitScript(() => {
      (window as any).__gc = [];
      (window as any).goatcounter = { count: (o: any) => (window as any).__gc.push(o.path) };
    });
    await page.goto("/");
    await expect(page.locator("#screen")).toBeVisible();
  });

  const events = (page: import("@playwright/test").Page) => page.evaluate(() => (window as any).__gc as string[]);

  test("the first key press counts the cabinet as started, once", async ({ page }) => {
    await page.locator("#pageThemeBtn").click();
    await page.locator("#themeDialogDone").click();
    expect(await events(page)).toEqual([]);
    await page.keyboard.press("Shift");
    await page.keyboard.press("Shift");
    expect(await events(page)).toEqual([`${MACHINE}-started`]);
  });

  test("off the live site, count.js never loads", async ({ page }) => {
    await expect(page.locator('script[src*="gc.zgo.at"]')).toHaveCount(0);
  });
});
