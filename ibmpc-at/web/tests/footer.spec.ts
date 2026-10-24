import { test, expect } from "./fixtures";

// Shared site footer (shared/footer.js).

test("site footer is present and the Help control points at ../news_info.html", async ({ page }) => {
  await page.goto("/?test=1");
  const footer = page.locator("#siteFooter");
  await expect(footer).toContainText(/© 2026 oac-emu\.dev/);
  await expect(footer).toContainText(/Old-Ass Computer/);
  await expect(footer).toContainText(/Source code available on GitHub/);
  await expect(footer.locator("a.source-link")).toHaveText("GitHub");
  await expect(footer.locator("a.source-link")).toHaveAttribute(
    "href",
    "https://github.com/gsirhc/oac-emu/tree/main/",
  );
  const help = footer.locator("a.about-sign");
  await expect(help).toHaveAttribute("href", "../news_info.html");
  await expect(help).toHaveText("Help");
  await expect(footer).not.toContainText("shareware");
});

test("with the catalog token, Help points at ../about.html", async ({ page }) => {
  await page.goto("/?test=1&full=c4e8a1f07b2d49e6a3c58f1d6e90b274");
  await expect(page.locator("#siteFooter a.about-sign")).toHaveAttribute("href", "../about.html");
});
