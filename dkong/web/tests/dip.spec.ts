import { test, expect } from "@playwright/test";

test.describe("DIP switches", () => {
  test.beforeEach(async ({ page }) => {
    await page.goto("/?test=1");
    await page.waitForFunction(() => (window as any).__test);
  });

  const dip = (page: import("@playwright/test").Page) =>
    page.evaluate(() => (window as any).__test.machine.dip());

  test("factory default is 3 lives, 7000 bonus, 1C/1C, upright", async ({ page }) => {
    await expect(page.locator("#dipPanel")).toBeVisible();
    await expect(page.locator("#dipLives")).toHaveValue("0");
    await expect(page.locator("#dipBonus")).toHaveValue("0");
    await expect(page.locator("#dipCoinage")).toHaveValue("0");
    await expect(page.locator("#dipCabinet")).toHaveValue("128");
    expect(await dip(page)).toBe(0x80);
    await expect(page.locator("#dipPanel .legal")).toContainText(/just like on the real arcade cabinet/i);
  });

  test("6 lives writes DIP bits 0-1 = 11", async ({ page }) => {
    await page.locator("#dipLives").selectOption({ value: "3" });
    expect((await dip(page)) & 0x03).toBe(0x03);
  });

  test("20000 bonus writes DIP bits 2-3 = 11", async ({ page }) => {
    await page.locator("#dipBonus").selectOption("12");
    expect((await dip(page)) & 0x0C).toBe(0x0C);
  });

  test("2 coins / 1 credit writes DIP bits 4-6 = 100 (E)", async ({ page }) => {
    // TKG4-UP Operation Manual §5-1: 2 coins/1 play is E=ON, F=G=OFF → bit4.
    await page.locator("#dipCoinage").selectOption("16");
    expect((await dip(page)) & 0x70).toBe(0x10);
  });

  test("cocktail cabinet clears DIP bit 7", async ({ page }) => {
    await page.locator("#dipCabinet").selectOption("0");
    expect((await dip(page)) & 0x80).toBe(0x00);
  });

  test("a DIP change survives a reload", async ({ page }) => {
    await page.locator("#dipLives").selectOption({ value: "3" });
    await page.locator("#dipCoinage").selectOption("112"); // 5 coins / 1 credit
    await page.reload();
    await page.waitForFunction(() => (window as any).__test);
    await expect(page.locator("#dipLives")).toHaveValue("3");
    await expect(page.locator("#dipCoinage")).toHaveValue("112");
    expect(await dip(page)).toBe(0x80 | 0x70 | 0x03);
  });
});
