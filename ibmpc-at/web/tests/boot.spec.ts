import { test, expect } from "./fixtures";
import { boot, screenText, setPowerSwitch, waitForScreen, focusScreen, rafTicks } from "./helpers";

// Auto-boot on load and the power switch.

test.describe("boot and power", () => {
  test("boots itself to a live C:\\> prompt with no interaction", async ({ page }) => {
    await boot(page);
    await expect(page.locator("#powerSwitch")).toBeChecked();
    await expect(page.locator("#powerLed")).toHaveClass(/power-on/);
    await expect.poll(() => screenText(page)).toMatch(/C:\\>/);
  });

  test("the shipped C: skips the menu countdown, MEM and CD-ROM, and says so in its startup files", async ({ page }) => {
    await boot(page);
    const screen = await screenText(page);
    expect(screen).toMatch(/Done processing startup files/);
    expect(screen).not.toMatch(/Modules using memory|CD-ROM not configured|CuteMouse/);
    const img = await page.evaluate(async () => {
      const r = await fetch("disks/freedos-hdd.img");
      return Array.from(new Uint8Array(await r.arrayBuffer()), (b) => String.fromCharCode(b)).join("");
    });
    expect(img).toContain("MENUDEFAULT=1,0\r\n");
    expect(img).not.toContain("MENUDEFAULT=1,5");
    expect(img).toContain("; Changed by oac-emu 2026-10-23:");
    expect(img).toContain("REM Changed by oac-emu 2026-10-23:");
  });

  test("the EGA BIOS sets 350-line text for the Enhanced Color Display", async ({ page }) => {
    await boot(page);
    const size = await page.evaluate(() => {
      const fc = (window as any).__test.frameCanvas as HTMLCanvasElement;
      return [fc.width, fc.height];
    });
    expect(size).toEqual([640, 350]);
  });

  test("power switch off discards the running machine; on boots a fresh one", async ({ page }) => {
    await boot(page);
    const cyclesRunning = await page.evaluate(() => (window as any).__test.machine.totalCycles());
    expect(cyclesRunning).toBeGreaterThan(0);

    await setPowerSwitch(page, false);
    await expect(page.locator("#powerLed")).not.toHaveClass(/power-on/);
    // window.__test.machine still points at the old instance, so check that the cycle count stops.
    const framesLater = await page.evaluate(() => (window as any).__test.machine.totalCycles());
    await rafTicks(page, 20);
    const stillFramesLater = await page.evaluate(() => (window as any).__test.machine.totalCycles());
    expect(stillFramesLater).toBe(framesLater);

    await setPowerSwitch(page, true);
    await expect(page.locator("#powerLed")).toHaveClass(/power-on/);
    await waitForScreen(page, /C:\\>/);
  });

  test("the drawn keyboard is live while on and inert while off", async ({ page }) => {
    await boot(page);
    await expect(page.locator('#pcKeyboard [data-key="F1"]')).toHaveAttribute("aria-disabled", "false");
    await expect(page.locator("#escBtn")).toBeEnabled();

    await setPowerSwitch(page, false);
    await expect(page.locator('#pcKeyboard [data-key="F1"]')).toHaveAttribute("aria-disabled", "true");
    await expect(page.locator("#escBtn")).toBeDisabled();
  });

  test("clicking a control focuses the control, not the screen", async ({ page }) => {
    // Only clicking the screen focuses it; other controls keep normal focus.
    await boot(page);
    await focusScreen(page);
    await expect(page.locator("#screen")).toBeFocused();

    await page.locator("#speakerEnabled").click();
    await expect(page.locator("#speakerEnabled")).toBeFocused();
  });
});
