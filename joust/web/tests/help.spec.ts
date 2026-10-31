import { test, expect } from "@playwright/test";

test("self-test ROM ends on a copyright / load-it-yourself help screen", async ({ page }) => {
  await page.goto("/?test=1");
  await expect(page.locator("#romStatus")).toHaveText("Running test ROM");
  await page.waitForFunction(() => !!(window as any).__test?.machine);
  await page.waitForFunction(() => (window as any).__test?.machine.state().frames >= 200);
  await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))));

  const stats = await page.evaluate(() => {
    const c = document.getElementById("screen") as HTMLCanvasElement;
    const data = c.getContext("2d")!.getImageData(0, 0, c.width, c.height).data;
    let red = 0, green = 0, yellow = 0, magenta = 0, white = 0, lit = 0;
    for (let i = 0; i < data.length; i += 4) {
      const r = data[i], g = data[i + 1], b = data[i + 2];
      if (r | g | b) lit++;
      if (r > 180 && g < 60 && b < 60) red++;
      if (r < 60 && g > 180 && b < 60) green++;
      if (r > 180 && g > 180 && b < 60) yellow++;
      if (r > 180 && g < 60 && b > 180) magenta++;
      if (r > 180 && g > 180 && b > 180) white++;
    }
    return { red, green, yellow, magenta, white, lit };
  });
  expect(stats.red).toBeGreaterThan(80);
  expect(stats.green).toBeGreaterThan(80);
  expect(stats.yellow).toBeGreaterThan(200);
  expect(stats.magenta).toBeGreaterThan(100);
  expect(stats.white).toBeGreaterThan(500);
  expect(stats.lit).toBeLessThan(300 * 248 * 0.5);
});
