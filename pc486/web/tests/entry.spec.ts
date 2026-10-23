import { test, expect } from "./fixtures";
import { bootLive, PC486_FULL } from "./helpers";

// The suite's server roots the machine page at "/", where a missing token has nowhere to go.
// Serving the same files under /pc486/ gives the redirect a home to land on, as on the live site.
async function serveUnderPc486(page) {
  await page.route("**/pc486/**", async (route) => {
    const url = new URL(route.request().url());
    url.pathname = url.pathname.replace(/^\/pc486\//, "/");
    await route.fulfill({ response: await route.fetch({ url: url.href }) });
  });
}

test.describe("entry page", () => {
  test("the token runs the machine with every drive control live", async ({ page }) => {
    await bootLive(page);
    await expect(page.locator("#hddStatus")).toHaveText(/Using: FreeDOS \(default\)/);
    await expect(page.locator("#libraryBtn")).toBeEnabled();
    await expect(page.locator("#hddDownloadBtn")).toBeEnabled();
    await expect(page.locator('.at-bay[data-drive="0"] [data-role="file"]')).toBeEnabled();
    await expect(page.locator('.at-bay[data-drive="cdrom"] [data-role="file"]')).toBeEnabled();
    await expect(page.locator("#siteFooter a.about-sign")).toHaveText("Help");
    const note = page.locator("#siteFooter .site-footer-note");
    await expect(note).toContainText("All shareware and freeware titles remain the property of their respective owners");
    await expect(note.locator("a")).toHaveAttribute("href", "../about.html#rights");
    await expect(page.locator(".inner h1")).toHaveText("486 DX2-66 Gaming PC");
  });

  test("/pc486/ with the token loads the machine in place", async ({ page }) => {
    await serveUnderPc486(page);
    await page.goto(`/pc486/?test=1&fast=1&full=${PC486_FULL}`);
    await page.waitForFunction(() => !!(window as any).__test?.machine, null, { timeout: 15_000 });
    expect(new URL(page.url()).pathname).toBe("/pc486/");
  });

  for (const qs of ["?test=1&fast=1", "?test=1&fast=1&full=nope"]) {
    test(`/pc486/${qs} redirects to the site home`, async ({ page }) => {
      await serveUnderPc486(page);
      await page.goto(`/pc486/${qs}`);
      await page.waitForURL((url) => !url.pathname.startsWith("/pc486"), { timeout: 5_000 });
      expect(new URL(page.url()).pathname).toBe("/");
    });
  }

  test("a stored token does not unlock the page", async ({ page }) => {
    await serveUnderPc486(page);
    await page.addInitScript((token) => {
      localStorage.setItem("retroweb.pc486.full", token);
    }, PC486_FULL);
    await page.goto("/pc486/?test=1&fast=1");
    await page.waitForURL((url) => !url.pathname.startsWith("/pc486"), { timeout: 5_000 });
  });
});
