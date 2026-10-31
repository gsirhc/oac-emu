import { test, expect, type Page } from "@playwright/test";

const door = (page: Page) => page.evaluate(() => (window as any).__test.machine.door());
const doorOpen = (page: Page) => page.evaluate(() => (window as any).__test.machine.doorOpen());
const prefs = (page: Page) => page.evaluate(() => JSON.parse(localStorage.getItem("retroweb.joust.door") || "null"));

test.describe("inside the coin door", () => {
  test.beforeEach(async ({ page }) => {
    await page.goto("/?test=1");
    await page.waitForFunction(() => !!(window as any).__test?.machine);
  });

  test("starts shut with AUTO-UP on", async ({ page }) => {
    await expect(page.locator("#doorOpen")).not.toBeChecked();
    await expect(page.locator("#autoUp")).toBeChecked();
    expect(await doorOpen(page)).toBe(false);
    expect(await door(page)).toBe(0x01);
  });

  test("the Coin door open checkbox opens and shuts the door", async ({ page }) => {
    await page.locator("#doorOpen").check();
    expect(await doorOpen(page)).toBe(true);
    await page.locator("#doorOpen").uncheck();
    expect(await doorOpen(page)).toBe(false);
  });

  test("AUTO-UP toggles door bit 0x01", async ({ page }) => {
    await page.locator("#autoUp").uncheck();
    expect(await door(page)).toBe(0x00);
    await page.locator("#autoUp").check();
    expect(await door(page)).toBe(0x01);
  });

  for (const [id, bit] of [["advanceBtn", 0x02], ["hsResetBtn", 0x08]] as const) {
    test(`#${id} is momentary on door bit 0x${bit.toString(16).padStart(2, "0")}`, async ({ page }) => {
      const btn = page.locator(`#${id}`);
      await btn.dispatchEvent("pointerdown", { button: 0, pointerId: 1 });
      await expect.poll(() => door(page)).toBe(0x01 | bit);
      await expect(btn).toHaveClass(/pressed/);
      await btn.dispatchEvent("pointerup", { button: 0, pointerId: 1 });
      await expect.poll(() => door(page)).toBe(0x01);
      await expect(btn).not.toHaveClass(/pressed/);
    });
  }

  test("pointercancel releases a held button", async ({ page }) => {
    const btn = page.locator("#advanceBtn");
    await btn.dispatchEvent("pointerdown", { button: 0, pointerId: 1 });
    await expect.poll(() => door(page)).toBe(0x03);
    await btn.dispatchEvent("pointercancel", { pointerId: 1 });
    await expect.poll(() => door(page)).toBe(0x01);
  });

  test("the door and AUTO-UP choices persist across a reload", async ({ page }) => {
    await page.locator("#doorOpen").check();
    await page.locator("#autoUp").uncheck();
    expect(await prefs(page)).toEqual({ autoUp: false, open: true });
    await page.reload();
    await page.waitForFunction(() => !!(window as any).__test?.machine);
    await expect(page.locator("#doorOpen")).toBeChecked();
    await expect(page.locator("#autoUp")).not.toBeChecked();
    expect(await doorOpen(page)).toBe(true);
    expect(await door(page)).toBe(0x00);
  });

  test("Power cycle restarts the board and the CMOS survives", async ({ page }) => {
    await page.waitForFunction(() => (window as any).__test.machine.state().frames >= 120);
    await page.evaluate(() => (window as any).__test.machine.setCmosByte(0x3FF, 0x9));
    await page.locator("#powerBtn").click();
    await expect.poll(() => page.evaluate(() => (window as any).__test.machine.state().frames)).toBeLessThan(120);
    expect(await page.evaluate(() => (window as any).__test.machine.cmosByte(0x3FF))).toBe(0x9);
  });
});
