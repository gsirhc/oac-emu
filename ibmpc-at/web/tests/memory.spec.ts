import { test, expect } from "./fixtures";
import { boot, waitForScreen, setPowerSwitch, typeStr } from "./helpers";

const EXT_MEM_KEY = "retro8080.ibmpcatExtMemKb";

test.describe("memory", () => {
  test("the extended memory choice is locked while running and remembered in this browser", async ({ page }) => {
    await boot(page, { expectScreen: null });
    const select = page.locator("#extMemSelect");
    await expect(select).toHaveValue("0");
    await expect(select).toBeDisabled();
    await setPowerSwitch(page, false);
    await expect(select).toBeEnabled();
    await select.selectOption("1024");
    expect(await page.evaluate((k) => localStorage.getItem(k), EXT_MEM_KEY)).toBe("1024");
    await setPowerSwitch(page, true);
    await expect(select).toBeDisabled();
    await page.reload();
    await expect(page.locator("#extMemSelect")).toHaveValue("1024");
  });

  test("an unknown saved value falls back to none", async ({ page }) => {
    await page.addInitScript((k) => localStorage.setItem(k, "640"), EXT_MEM_KEY);
    await boot(page, { expectScreen: null });
    await expect(page.locator("#extMemSelect")).toHaveValue("0");
  });

  test("with no extended memory, MEM shows none and DOS stays low", async ({ page }) => {
    await boot(page);
    await typeStr(page, "MEM", { pressEnterAfter: true });
    await waitForScreen(page, /Largest executable/, 30_000);
    await waitForScreen(page, /Extended \(XMS\)\s+0K/);
    await expect.poll(() => page.evaluate(() => (window as any).__test.machine.textScreen())).not.toMatch(/high memory area/);
  });

  test("fitted extended memory reaches DOS through the BIOS and the XMS driver", async ({ page }) => {
    await page.addInitScript((k) => localStorage.setItem(k, "2048"), EXT_MEM_KEY);
    await boot(page);
    await typeStr(page, "MEM", { pressEnterAfter: true });
    await waitForScreen(page, /Extended \(XMS\)\s+2,048K/, 30_000);
    await waitForScreen(page, /FreeDOS is resident in the high memory area/);
  });

  test("XMS block moves into extended memory verify, through the BIOS's protected-mode copy", async ({ page }) => {
    test.setTimeout(240_000);
    await page.addInitScript((k) => localStorage.setItem(k, "1024"), EXT_MEM_KEY);
    await boot(page);
    await typeStr(page, "CLS", { pressEnterAfter: true });
    await typeStr(page, "XMSTEST", { pressEnterAfter: true });
    await waitForScreen(page, /total time \(clocks\)/, 180_000);
    await waitForScreen(page, /verify handle/);
    await expect.poll(() => page.evaluate(() => (window as any).__test.machine.textScreen())).not.toMatch(/error|fail/i);
  });
});
