import { test, expect, Page } from "@playwright/test";
import { boot } from "./helpers";

// GoatCounter events, recorded through a stubbed goatcounter.count.

async function stubCounter(page: Page) {
  await page.addInitScript(() => {
    (window as any).__gc = [];
    (window as any).goatcounter = { count: (o: any) => (window as any).__gc.push(o.path) };
  });
}

const events = (page: Page) => page.evaluate(() => (window as any).__gc as string[]);

async function load(page: Page) {
  await page.goto("/?test=1");
  await page.waitForFunction(() => !!(window as any).__test?.machine);
}

test.describe("analytics", () => {
  test.beforeEach(async ({ page }) => {
    await stubCounter(page);
  });

  test("first input on the machine counts as started, once; page chrome doesn't", async ({ page }) => {
    await load(page);
    await page.locator("#pageThemeBtn").click();
    await page.locator("#themeDialogDone").click();
    expect(await events(page)).toEqual([]);
    await page.locator("#screen").click();
    await page.keyboard.press("A");
    expect(await events(page)).toEqual(["altair8800-started"]);
  });

  test("still-playing marks fire only with recent input", async ({ page }) => {
    await page.clock.install();
    await load(page);
    await page.locator("#screen").click();
    await page.clock.fastForward("04:00");
    await page.keyboard.press("A");
    await page.clock.fastForward("01:00");
    await expect.poll(() => events(page)).toEqual(["altair8800-started", "altair8800-playing-5m"]);
    await page.clock.fastForward("25:00");
    expect(await events(page)).toEqual(["altair8800-started", "altair8800-playing-5m"]);
  });

  test("a theme change counts once, when the dialog closes", async ({ page }) => {
    await load(page);
    await page.locator("#pageThemeBtn").click();
    await page.locator('#themeDialog input[name="pageThemeFamily"][value="subtle"]').check();
    await page.locator('#themeDialog input[name="pageThemeFamily"][value="winxp"]').check();
    await page.locator('#themeDialog input[name="pageThemeMode"][value="dark"]').check();
    await page.locator("#themeDialogDone").click();
    await page.locator("#pageThemeBtn").click();
    await page.locator("#themeDialogDone").click();
    expect(await events(page)).toEqual(["theme-winxp-dark"]);
  });

  test("fullscreen counts on the way in", async ({ page }) => {
    await load(page);
    await page.locator("#fullscreenBtn").click();
    expect(await events(page)).toContain("altair8800-fullscreen");
  });

  test("the disk LOAD SPEED choice is counted", async ({ page }) => {
    await boot(page, { params: "preset=cpm" });
    await page.selectOption("#dcdd .dcdd-speed", "x50");
    expect(await events(page)).toContain("altair8800-disk-speed-x50");
  });

  test("the paper tape and cassette speeds are counted", async ({ page }) => {
    await boot(page, { params: "preset=cassette" });
    await page.selectOption("#ptr .ptr-speed", "x25");
    await page.selectOption("#acr .acr-speed", "x5");
    const got = await events(page);
    expect(got).toContain("altair8800-paper-tape-speed-x25");
    expect(got).toContain("altair8800-cassette-speed-x5");
  });

  test("off the live site, count.js never loads", async ({ page }) => {
    await load(page);
    await expect(page.locator('script[src*="gc.zgo.at"]')).toHaveCount(0);
  });

  test("on the live site, count.js loads and sees the preset but not the access token", async ({ page, baseURL }) => {
    await page.route("https://gsirhc.github.io/**", async (route) => {
      const u = new URL(route.request().url());
      const local = baseURL + u.pathname.replace(/^\/oac-emu\/altair8800/, "") + u.search;
      await route.fulfill({ response: await route.fetch({ url: local }) });
    });
    await page.route("https://gc.zgo.at/count.js", (route) =>
      route.fulfill({
        contentType: "text/javascript",
        body: "window.__gcPath = window.goatcounter.path(location.pathname + location.search);",
      }),
    );
    await page.goto("https://gsirhc.github.io/oac-emu/altair8800/?full=secret&disk=cpm");
    await expect.poll(() => page.evaluate(() => (window as any).__gcPath)).toBe("/oac-emu/altair8800/?disk=cpm");
    await expect(page.locator('script[data-goatcounter="https://oac-emu.goatcounter.com/count"]')).toHaveCount(1);
    await page.unrouteAll({ behavior: "ignoreErrors" });
  });
});
