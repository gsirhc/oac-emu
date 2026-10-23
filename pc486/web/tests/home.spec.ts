import { test, expect } from "@playwright/test";
import { PC486_FULL } from "./helpers";

function catalogHref(href: string): string {
  if (!href.startsWith("pc486/")) return href;
  const rest = href.slice("pc486/".length);
  if (rest.startsWith("?")) return `pc486/${rest}&full=${PC486_FULL}`;
  return `pc486/?full=${PC486_FULL}`;
}


async function chooseTheme(page, theme, mode = "light") {
  await page.locator("#pageThemeBtn").click();
  await page.locator(`#themeDialog input[name="pageThemeFamily"][value="${theme}"]`).check();
  await page.locator(`#themeDialog input[name="pageThemeMode"][value="${mode}"]`).check();
  await page.locator("#themeDialogDone").click();
}


// The root landing page (:8410). It shares the `retro8080.theme` localStorage key with the
// emulator, so a theme picked here carries into /pc486/.

const HOME = "http://localhost:8410/";
const CATALOG = HOME + "super_secret.html";

test.describe("landing page", () => {
  test("lists the 486 DX2-66 Gaming PC with a launch link to /pc486/", async ({ page }) => {
    await page.goto(CATALOG);
    await expect(page).toHaveTitle(/Welcome to OAC Emulators/);
    await expect(page.locator("#siteFooter a.about-sign")).toHaveText("Help");

    const card = page.locator(`a.machine-card[href="${catalogHref("pc486/")}"]`);
    await expect(card).toHaveAttribute("href", catalogHref("pc486/"));
    await expect(card.locator(".name")).toHaveText(/486 DX2-66/i);

    const shot = card.locator("img.shot");
    await expect(shot).toHaveAttribute("src", /assets\/pc486-panel\.jpg$/);
    await expect(shot).toHaveAttribute("width", "286");
    await expect(shot).toHaveAttribute("height", "128");
    await expect(shot).toHaveJSProperty("complete", true);
    expect(await shot.evaluate((img: HTMLImageElement) => img.naturalWidth)).toBe(286);
    expect(await shot.evaluate((img: HTMLImageElement) => img.naturalHeight)).toBe(128);
  });

  test("Ready to Play has no DOS titles", async ({ page }) => {
    await page.goto(CATALOG);
    await expect(page.locator("h2.section-heading")).toHaveText([
      "Systems",
      "Ready to Play",
      "Arcade",
      "Homebrew",
    ]);
    await expect(page.locator('a.machine-card[href*="disk="]')).toHaveCount(1);
    await expect(page.locator('a.machine-card[href="altair8800/?disk=cpm"]')).toHaveCount(1);
    await expect(page.locator("a.machine-card[data-art]")).toHaveCount(0);
    await expect(page.locator("#readyArt")).toHaveCount(0);
  });

  test("the public page shows only the coming-soon 486", async ({ page }) => {
    await page.goto(HOME);
    await expect(page.locator("h2.section-heading")).toHaveCount(0);
    await expect(page.locator("#siteFooter a.about-sign")).toHaveCount(0);
    await expect(page.locator(".machines .machine-card")).toHaveCount(1);
    await expect(page.locator("a.machine-card")).toHaveCount(0);
    const soon = page.locator("#pc486Soon");
    await expect(soon.locator(".name")).toHaveText("486 DX2-66 Gaming PC");
    await expect(soon.locator(".meta")).toHaveText("Coming soon");
    await expect(soon.locator(".soon-banner")).toHaveText("Coming soon");
    const dlg = page.locator("#pc486SoonDialog");
    await expect(dlg).toBeHidden();
    await soon.click();
    await expect(dlg).toBeVisible();
    await expect(dlg.locator("h2")).toHaveText("Coming Soon!");
    await expect(dlg.locator("p")).toHaveText("A 66 MHz 486 with VGA, Sound Blaster, a hard disk, a floppy drive, and a CD-ROM. The full PC will be available soon where you have full control over your system just like the real thing.");
    const winBg = await dlg.evaluate((el) => getComputedStyle(el).backgroundColor);
    await page.locator("#pc486SoonOk").click();
    await expect(dlg).toBeHidden();
    await chooseTheme(page, "aurora", "dark");
    await soon.click();
    await expect(dlg).toBeVisible();
    const auroraBg = await dlg.evaluate((el) => getComputedStyle(el).backgroundColor);
    expect(auroraBg).not.toBe(winBg);
  });

  test("the welcome banner links to the news, and that page returns home", async ({ page }) => {
    await page.goto(HOME);
    const news = page.locator(".welcome-banner a");
    await expect(news).toHaveText("News & Info");
    await expect(news).toHaveAttribute("href", "news_info.html");
    await news.click();
    await expect(page).toHaveURL(/\/news_info\.html/);
    await expect(page).toHaveTitle("News & Info");
    await expect(page.locator(".inner h1")).toHaveText("News & Info");
    await expect(page.locator("#news")).toHaveText("News");
    await expect(page.locator("#info")).toHaveText("Site Information");
    await expect(page.locator("#siteFooter a.about-sign")).toHaveCount(0);
    await expect(page.locator("a.pb-close")).toHaveAttribute("href", "./");
    await page.locator("a.pb-close").click();
    await expect(page).toHaveTitle(/Welcome to OAC Emulators/);
  });

  test("the theme selector switches the page and persists to retro8080.theme", async ({ page }) => {
    await page.goto(HOME);
    const root = page.locator("html");
    await expect(root).toHaveAttribute("data-theme", "subtle");
    await expect(root).not.toHaveAttribute("data-mode");

    await chooseTheme(page, "win");
    await expect(root).toHaveAttribute("data-theme", "win");
    expect(await page.evaluate(() => localStorage.getItem("retro8080.theme"))).toBe("win");

    await chooseTheme(page, "web94");
    await expect(root).toHaveAttribute("data-theme", "web94");
    expect(await page.evaluate(() => localStorage.getItem("retro8080.theme"))).toBe("web94");
  });

  test("a stored retro8080.theme is honoured on load", async ({ page }) => {
    await page.goto(HOME);
    await page.evaluate(() => localStorage.setItem("retro8080.theme", "win"));
    await page.reload();
    await expect(page.locator("html")).toHaveAttribute("data-theme", "win");
    await expect(page.locator("#pageThemeBtn")).toHaveText("Windows 95");
  });

  test("Windows title-bar buttons: soft-minimize collapses the page; close is disabled", async ({ page }) => {
    await page.goto(HOME);
    await expect(page.locator(".pb-close.is-disabled")).toHaveAttribute("aria-disabled", "true");
    await expect(page.locator("a.pb-close")).toHaveCount(0);
    await expect(page.locator(".pb-max")).toBeDisabled();
    await expect(page.locator(".inner")).toBeVisible();
    await page.locator(".pb-min").click();
    await expect(page.locator(".page")).toHaveClass(/is-minimized/);
    await expect(page.locator(".inner")).toBeHidden();
    await expect(page.locator("#siteFooter")).toBeHidden();
    await page.locator(".pb-min").click();
    await expect(page.locator(".page")).not.toHaveClass(/is-minimized/);
    await expect(page.locator(".inner")).toBeVisible();
  });

});
