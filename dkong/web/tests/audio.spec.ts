import { test, expect, type Page } from "@playwright/test";

// The self-test sound ROM parks the DAC, so the mix idles quiet; the
// discrete walk/jump/boom latches at $7D00-$7D02 must still reach it.

const blocks = (page: Page, n: number) => page.evaluate((k) => new Promise<void>((done) => {
  const w = window as any;
  const end = w.__audioBlocks + k;
  const tick = () => (w.__audioBlocks >= end ? done() : requestAnimationFrame(tick));
  tick();
}), n);

test.describe("Audio level", () => {
  test.beforeEach(async ({ page }) => {
    await page.addInitScript(() => {
      const w = window as any;
      w.__audioPeak = 0;
      w.__audioBlocks = 0;
      const post = MessagePort.prototype.postMessage;
      MessagePort.prototype.postMessage = function (this: MessagePort, data: any, ...rest: any[]) {
        if (data instanceof Float32Array) {
          w.__audioBlocks++;
          for (const s of data) w.__audioPeak = Math.max(w.__audioPeak, Math.abs(s));
        }
        return (post as any).call(this, data, ...rest);
      };
    });
    await page.goto("/?test=1");
    await page.waitForFunction(() => (window as any).__test);
    await page.locator("body").click({ position: { x: 5, y: 5 } });
    await page.waitForFunction(() => (window as any).__audioBlocks > 10);
  });

  test("the self-test idles quiet", async ({ page }) => {
    await page.evaluate(() => { (window as any).__audioPeak = 0; });
    await blocks(page, 30);
    expect(await page.evaluate(() => (window as any).__audioPeak)).toBeLessThan(0.01);
    const s = await page.evaluate(() => (window as any).__test.machine.state());
    expect(s.frames).toBeGreaterThan(0);
  });

  for (const [name, addr] of [["walk", 0x7D00], ["jump", 0x7D01], ["boom", 0x7D02]] as const) {
    test(`the ${name} latch reaches the speaker`, async ({ page }) => {
      await page.evaluate(() => { (window as any).__audioPeak = 0; });
      await page.evaluate((a) => (window as any).__test.machine.setMemByte(a, 1), addr);
      await blocks(page, 3);
      await page.evaluate((a) => (window as any).__test.machine.setMemByte(a, 0), addr);
      await blocks(page, 20);
      expect(await page.evaluate(() => (window as any).__audioPeak)).toBeGreaterThan(0.05);
    });
  }
});
