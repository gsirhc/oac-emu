import { test, expect, type Page } from "@playwright/test";

const door = (page: Page) => page.evaluate(() => (window as any).__test.machine.door());
const starts = (page: Page) => page.evaluate(() => (window as any).__test.machine.starts());

// Record every door value the page hands the board, so a short pulse can't be missed between polls.
const recordDoor = (page: Page) => page.evaluate(() => {
  const w = window as any;
  const m = w.__test.machine;
  const set = m.setDoor.bind(m);
  w.__doorSeen = 0;
  m.setDoor = (v: number) => { w.__doorSeen |= v; set(v); };
});
const seen = (page: Page) => page.evaluate(() => (window as any).__doorSeen as number);

test.describe("coin door", () => {
  test.beforeEach(async ({ page }) => {
    await page.goto("/?test=1");
    await expect(page.locator("#coinDoor")).toBeVisible();
    await expect.poll(() => page.evaluate(() => !!(window as any).__test)).toBe(true);
  });

  test("shows two 25¢ lamps and coin slots on the blue cabinet fascia", async ({ page }) => {
    const face = page.locator(".cab-face");
    await expect(face).toBeVisible();
    const bg = await face.evaluate((el) => getComputedStyle(el).backgroundImage);
    expect(bg).toMatch(/42,\s*42,\s*106|28,\s*28,\s*76|14,\s*14,\s*42/);
    await expect(page.locator("#coinDoor [data-coin]")).toHaveCount(2);
    await expect(page.locator(".cd-lamp")).toHaveCount(2);
    await expect(page.locator(".cd-lamp").first()).toHaveText("25¢");
    await expect(page.locator("#coinDoor [data-coin='left']")).toHaveAttribute("aria-label", /Left coin slot/);
    await expect(page.locator("#coinDoor [data-coin='right']")).toHaveAttribute("aria-label", /Right coin slot/);
  });

  test("shows 1 PLAYER and 2 PLAYER start buttons above the slots", async ({ page }) => {
    await expect(page.locator("[data-start]")).toHaveCount(2);
    await expect(page.locator("[data-start='1']")).toHaveAttribute("aria-label", "1 Player start");
    await expect(page.locator("[data-start='2']")).toHaveAttribute("aria-label", "2 Player start");
    await expect(page.locator("[data-start='1'] .cab-start-img")).toBeVisible();
    await expect(page.locator("[data-start='2'] .cab-start-img")).toBeVisible();
  });

  test("holding 1 PLAYER sets start bit 0x20, then releases", async ({ page }) => {
    expect(await starts(page)).toBe(0);
    const btn = page.locator("[data-start='1']");
    await btn.dispatchEvent("pointerdown", { button: 0, pointerId: 1 });
    await expect.poll(() => starts(page)).toBe(0x20);
    await btn.dispatchEvent("pointerup", { button: 0, pointerId: 1 });
    await expect.poll(() => starts(page)).toBe(0);
  });

  test("holding 2 PLAYER sets start bit 0x10, then releases", async ({ page }) => {
    const btn = page.locator("[data-start='2']");
    await btn.dispatchEvent("pointerdown", { button: 0, pointerId: 1 });
    await expect.poll(() => starts(page)).toBe(0x10);
    await btn.dispatchEvent("pointerup", { button: 0, pointerId: 1 });
    await expect.poll(() => starts(page)).toBe(0);
  });

  test("the left slot pulses door bit 0x10 only, then releases", async ({ page }) => {
    await recordDoor(page);
    await page.locator("#coinDoor [data-coin='left']").click();
    await expect.poll(() => seen(page)).toBe(0x01 | 0x10);
    await expect.poll(() => door(page)).toBe(0x01);
  });

  test("the right slot pulses door bit 0x04 only, then releases", async ({ page }) => {
    await recordDoor(page);
    await page.locator("#coinDoor [data-coin='right']").click();
    await expect.poll(() => seen(page)).toBe(0x01 | 0x04);
    await expect.poll(() => door(page)).toBe(0x01);
  });

  test("a coin click lights the slot briefly", async ({ page }) => {
    await page.locator("#coinDoor [data-coin='left']").click();
    await expect(page.locator("#coinDoor")).toHaveClass(/coined/);
    await expect(page.locator("#coinDoor")).not.toHaveClass(/coined/);
  });
});
