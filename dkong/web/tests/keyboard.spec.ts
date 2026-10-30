import { test, expect } from "@playwright/test";

test.describe("keyboard input", () => {
  test.beforeEach(async ({ page }) => {
    await page.goto("/?test=1");
    await page.waitForFunction(() => !!(window as any).__test?.machine);
    await page.locator("#screen").click();
  });

  const in0 = (page: import("@playwright/test").Page) =>
    page.evaluate(() => (window as any).__test.machine.in0());
  const in2 = (page: import("@playwright/test").Page) =>
    page.evaluate(() => (window as any).__test.machine.in2());

  test("released state is the factory idle on all three ports", async ({ page }) => {
    expect(await in0(page)).toBe(0x00);
    expect(await page.evaluate(() => (window as any).__test.machine.in1())).toBe(0x00);
    expect(await in2(page)).toBe(0x00);
  });

  test("ArrowRight / ArrowLeft / ArrowUp / ArrowDown set IN0 bits 0x01/0x02/0x04/0x08", async ({ page }) => {
    await page.keyboard.down("ArrowRight");
    expect(await in0(page)).toBe(0x01);
    await page.keyboard.up("ArrowRight");
    await page.keyboard.down("ArrowLeft");
    expect(await in0(page)).toBe(0x02);
    await page.keyboard.up("ArrowLeft");
    await page.keyboard.down("ArrowUp");
    expect(await in0(page)).toBe(0x04);
    await page.keyboard.up("ArrowUp");
    await page.keyboard.down("ArrowDown");
    expect(await in0(page)).toBe(0x08);
    await page.keyboard.up("ArrowDown");
    expect(await in0(page)).toBe(0x00);
  });

  test("WASD maps to the same joystick bits", async ({ page }) => {
    await page.keyboard.down("KeyD");
    expect(await in0(page)).toBe(0x01);
    await page.keyboard.up("KeyD");
    await page.keyboard.down("KeyA");
    expect(await in0(page)).toBe(0x02);
    await page.keyboard.up("KeyA");
    await page.keyboard.down("KeyW");
    expect(await in0(page)).toBe(0x04);
    await page.keyboard.up("KeyW");
    await page.keyboard.down("KeyS");
    expect(await in0(page)).toBe(0x08);
    await page.keyboard.up("KeyS");
  });

  // The LS123 one-shot stretches the switch to ~94 ms, held key or not.
  const coinPulse = (page: import("@playwright/test").Page) => page.evaluate(() => {
    const m = (window as any).__test.machine;
    return new Promise<boolean>((resolve) => {
      const start = performance.now();
      const id = setInterval(() => {
        if ((m.in2() & 0x80) !== 0) {
          clearInterval(id);
          resolve(true);
        } else if (performance.now() - start > 1500) {
          clearInterval(id);
          resolve(false);
        }
      }, 10);
    });
  });

  test("5 inserts a coin (IN2 bit 0x80)", async ({ page }) => {
    const saw = coinPulse(page);
    await page.keyboard.down("Digit5");
    expect(await saw).toBe(true);
    await page.keyboard.up("Digit5");
    await expect.poll(() => in2(page)).toBe(0x00);
  });

  test("1 and 2 start 1P/2P (IN2 bits 0x04/0x08)", async ({ page }) => {
    await page.keyboard.down("Digit1");
    expect(await in2(page)).toBe(0x04);
    await page.keyboard.up("Digit1");
    expect(await in2(page)).toBe(0x00);

    await page.keyboard.down("Digit2");
    expect(await in2(page)).toBe(0x08);
    await page.keyboard.up("Digit2");
    expect(await in2(page)).toBe(0x00);
  });

  test("numpad 5/1/2 map to the same coin and start bits", async ({ page }) => {
    const saw = coinPulse(page);
    await page.keyboard.down("Numpad5");
    expect(await saw).toBe(true);
    await page.keyboard.up("Numpad5");
    await expect.poll(() => in2(page)).toBe(0x00);
    await page.keyboard.down("Numpad1");
    expect(await in2(page)).toBe(0x04);
    await page.keyboard.up("Numpad1");
    await page.keyboard.down("Numpad2");
    expect(await in2(page)).toBe(0x08);
    await page.keyboard.up("Numpad2");
  });

  test("Z / Space jump sets IN0 bit 0x10", async ({ page }) => {
    await page.keyboard.down("KeyZ");
    expect(await in0(page)).toBe(0x10);
    await page.keyboard.up("KeyZ");
    await page.keyboard.down("Space");
    expect(await in0(page)).toBe(0x10);
    await page.keyboard.up("Space");
    expect(await in0(page)).toBe(0x00);
  });
});
