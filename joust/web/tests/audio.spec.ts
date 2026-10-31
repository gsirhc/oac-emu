import { test, expect, type Page } from "@playwright/test";

// The self-test sound ROM parks the DAC, so the mix idles quiet; a DAC
// write on the 6808's PIA must still reach the speaker.

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

  test("a DAC write on the sound PIA reaches the speaker", async ({ page }) => {
    await page.evaluate(() => { (window as any).__audioPeak = 0; });
    await page.evaluate(() => {
      const m = (window as any).__test.machine;
      m.soundWrite(0x0401, 0x00);
      m.soundWrite(0x0400, 0xFF);
      m.soundWrite(0x0401, 0x04);
      m.soundWrite(0x0400, 0xFF);
    });
    await expect.poll(() => page.evaluate(() => (window as any).__test.machine.state().dac)).toBe(0xFF);
    await blocks(page, 3);
    await page.evaluate(() => (window as any).__test.machine.soundWrite(0x0400, 0x80));
    await blocks(page, 20);
    expect(await page.evaluate(() => (window as any).__audioPeak)).toBeGreaterThan(0.05);
  });
});
