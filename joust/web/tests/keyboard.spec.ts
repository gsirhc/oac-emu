import { test, expect, type Page } from "@playwright/test";

const read = (page: Page, fn: string) => page.evaluate((f) => (window as any).__test.machine[f](), fn);
const p1 = (page: Page) => read(page, "p1");
const p2 = (page: Page) => read(page, "p2");
const starts = (page: Page) => read(page, "starts");
const door = (page: Page) => read(page, "door");

test.describe("keyboard input", () => {
  test.beforeEach(async ({ page }) => {
    await page.goto("/?test=1");
    await page.waitForFunction(() => !!(window as any).__test?.machine);
    await page.locator("#screen").click();
  });

  test("released state is idle, with only AUTO-UP set on the door", async ({ page }) => {
    expect(await p1(page)).toBe(0);
    expect(await p2(page)).toBe(0);
    expect(await starts(page)).toBe(0);
    expect(await door(page)).toBe(0x01);
  });

  test("ArrowLeft / ArrowRight / Space set P1 bits 0x01 / 0x02 / 0x04", async ({ page }) => {
    await page.keyboard.down("ArrowLeft");
    expect(await p1(page)).toBe(0x01);
    await page.keyboard.up("ArrowLeft");
    await page.keyboard.down("ArrowRight");
    expect(await p1(page)).toBe(0x02);
    await page.keyboard.up("ArrowRight");
    await page.keyboard.down("Space");
    expect(await p1(page)).toBe(0x04);
    await page.keyboard.up("Space");
    expect(await p1(page)).toBe(0);
  });

  test("A / D / Z / W / ArrowUp map to the same P1 bits", async ({ page }) => {
    for (const [key, bit] of [["KeyA", 0x01], ["KeyD", 0x02], ["KeyZ", 0x04], ["KeyW", 0x04], ["ArrowUp", 0x04]] as const) {
      await page.keyboard.down(key);
      expect(await p1(page)).toBe(bit);
      await page.keyboard.up(key);
      expect(await p1(page)).toBe(0);
    }
  });

  test("run and flap combine", async ({ page }) => {
    await page.keyboard.down("ArrowRight");
    await page.keyboard.down("Space");
    expect(await p1(page)).toBe(0x06);
    await page.keyboard.up("ArrowRight");
    await page.keyboard.up("Space");
    expect(await p1(page)).toBe(0);
  });

  test("J / L / K set P2 bits 0x01 / 0x02 / 0x04, leaving P1 alone", async ({ page }) => {
    for (const [key, bit] of [["KeyJ", 0x01], ["KeyL", 0x02], ["KeyK", 0x04], ["KeyI", 0x04]] as const) {
      await page.keyboard.down(key);
      expect(await p2(page)).toBe(bit);
      expect(await p1(page)).toBe(0);
      await page.keyboard.up(key);
      expect(await p2(page)).toBe(0);
    }
  });

  test("5 is the left coin (door bit 0x10), 6 the right coin (0x04)", async ({ page }) => {
    await page.keyboard.down("Digit5");
    expect(await door(page)).toBe(0x01 | 0x10);
    await page.keyboard.up("Digit5");
    expect(await door(page)).toBe(0x01);
    await page.keyboard.down("Digit6");
    expect(await door(page)).toBe(0x01 | 0x04);
    await page.keyboard.up("Digit6");
    expect(await door(page)).toBe(0x01);
  });

  test("F2 is ADVANCE (door bit 0x02)", async ({ page }) => {
    await page.keyboard.down("F2");
    expect(await door(page)).toBe(0x01 | 0x02);
    await page.keyboard.up("F2");
    expect(await door(page)).toBe(0x01);
  });

  test("1 and 2 start 1P (0x20) and 2P (0x10)", async ({ page }) => {
    await page.keyboard.down("Digit1");
    expect(await starts(page)).toBe(0x20);
    await page.keyboard.up("Digit1");
    expect(await starts(page)).toBe(0);
    await page.keyboard.down("Digit2");
    expect(await starts(page)).toBe(0x10);
    await page.keyboard.up("Digit2");
    expect(await starts(page)).toBe(0);
  });

  test("numpad 5/6/1/2 map to the same coin and start bits", async ({ page }) => {
    await page.keyboard.down("Numpad5");
    expect(await door(page)).toBe(0x01 | 0x10);
    await page.keyboard.up("Numpad5");
    await page.keyboard.down("Numpad6");
    expect(await door(page)).toBe(0x01 | 0x04);
    await page.keyboard.up("Numpad6");
    await page.keyboard.down("Numpad1");
    expect(await starts(page)).toBe(0x20);
    await page.keyboard.up("Numpad1");
    await page.keyboard.down("Numpad2");
    expect(await starts(page)).toBe(0x10);
    await page.keyboard.up("Numpad2");
    expect(await starts(page)).toBe(0);
  });

  test("the widget PIA mux reads P1 and the starts, or P2, by CB2", async ({ page }) => {
    const port = () => page.evaluate(() => (window as any).__test.machine.widgetPort());
    const cb2 = (v: number) => page.evaluate((x) => (window as any).__test.machine.setMemByte(0xC807, x), v);
    await page.evaluate(() => {
      const m = (window as any).__test.machine;
      m.setMemByte(0xC805, 0x00);
      m.setMemByte(0xC804, 0x00);
      m.setMemByte(0xC805, 0x04);
    });
    await page.keyboard.down("ArrowLeft");
    await page.keyboard.down("KeyL");
    await page.keyboard.down("Digit1");
    await cb2(0x3C);
    await expect.poll(async () => (await port()) & 0x27).toBe(0x21);
    await cb2(0x34);
    await expect.poll(async () => (await port()) & 0x27).toBe(0x22 | 0x20);
    await page.keyboard.up("ArrowLeft");
    await page.keyboard.up("KeyL");
    await page.keyboard.up("Digit1");
    await expect.poll(async () => (await port()) & 0x27).toBe(0);
  });
});
