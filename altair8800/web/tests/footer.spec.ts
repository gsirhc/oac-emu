import { test, expect } from "@playwright/test";

// Shared site footer (shared/footer.js); Help points one level up at news_info.html.

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
});
