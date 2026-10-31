import { test, expect, type Page } from "@playwright/test";
import { readFileSync } from "fs";
import { HWTEST_FILES, ROM_DB, USER_BYTE } from "./cabinet";

function userSet() {
  return HWTEST_FILES.map((path) => {
    const name = path.split("/").pop()!;
    let buffer = readFileSync(path);
    if (name === "bank.bin") {
      buffer = Buffer.from(buffer);
      buffer[USER_BYTE] ^= 1;
    }
    return { name, mimeType: "application/octet-stream" as const, buffer };
  });
}

// The whole 1024-nibble CMOS is the saved image.
const SAMPLE = Array.from({ length: 1024 }, (_, i) => (i * 7 + 3) & 0x0F);
const BLANK = new Array(1024).fill(0);

const t = (page: Page) => page.evaluate(() => (window as any).__test.readHiscore() as number[]);
const frames = (page: Page) => page.evaluate(() => (window as any).__test.machine.state().frames as number);
const saved = (page: Page) => page.evaluate(() => (window as any).__test.savedHiscores() as Promise<Record<string, number[]>>);

async function loadUserSet(page: Page) {
  await page.locator("#romFile").setInputFiles(userSet());
  await expect(page.locator("#romStatus")).toHaveText("Loaded ROM set");
  await expect.poll(() => page.evaluate(() => (window as any).__test.hiscoreRestored)).toBe(true);
}

async function saveSample(page: Page) {
  await page.evaluate(async (bytes) => {
    const x = (window as any).__test;
    x.writeHiscore(bytes);
    await x.saveHiscoreNow(x.readHiscore());
  }, SAMPLE);
}

test.describe("CMOS persist", () => {
  test.beforeEach(async ({ page }) => {
    await page.goto("/?test=1");
    await page.waitForFunction(() => (window as any).__test);
  });

  test("Reset is present and disabled on the self-test ROM", async ({ page }) => {
    const btn = page.locator("#resetHiscore");
    await expect(btn).toBeVisible();
    await expect(btn).toBeDisabled();
    await expect(page.locator("p.legal").filter({ hasText: "battery-backed CMOS" })).toBeVisible();
  });

  test("the self-test ROM never saves its CMOS", async ({ page }) => {
    await page.evaluate(() => (window as any).__test.writeHiscore(new Array(1024).fill(5)));
    await page.evaluate(() => (window as any).__test.maybeSaveHiscore());
    const start = await frames(page);
    await page.waitForFunction((n) => (window as any).__test.machine.state().frames > n + 30, start);
    expect(await saved(page)).toEqual({});
    await expect(page.locator("#factoryNote")).toBeHidden();
  });

  test("a saved CMOS image is restored into a reload", async ({ page }) => {
    await loadUserSet(page);
    await saveSample(page);
    expect(Object.keys(await saved(page))).toHaveLength(1);

    await page.reload();
    await expect(page.locator("#romStatus")).toHaveText("Loaded stored ROM set");
    await expect.poll(() => page.evaluate(() => (window as any).__test.hiscoreRestored)).toBe(true);
    expect(await t(page)).toEqual(SAMPLE);
    expect(await page.evaluate(() => (window as any).__test.factoryActive)).toBe(false);
    await expect(page.locator("#factoryNote")).toBeHidden();
  });

  test("the image is stored under the ROM set key in the hiscores record", async ({ page }) => {
    await loadUserSet(page);
    await saveSample(page);
    const all = await page.evaluate((db) => new Promise<Record<string, number[]>>((ok) => {
      const r = indexedDB.open(db);
      r.onsuccess = () => {
        const q = r.result.transaction("roms").objectStore("roms").get("hiscores");
        q.onsuccess = () => { r.result.close(); ok(q.result); };
      };
    }), ROM_DB);
    const keys = Object.keys(all);
    expect(keys).toHaveLength(1);
    expect(all[keys[0]]).toEqual(SAMPLE);
  });

  test("a blank CMOS on a user set starts the factory setup with the door open", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(userSet());
    await expect(page.locator("#romStatus")).toHaveText("Loaded ROM set");
    await expect(page.locator("#factoryNote")).toBeVisible();
    expect(await page.evaluate(() => (window as any).__test.factoryActive)).toBe(true);
    expect(await page.evaluate(() => (window as any).__test.machine.doorOpen())).toBe(true);
    expect(await t(page)).toEqual(BLANK);
  });

  test("a restored non-blank CMOS skips the factory setup", async ({ page }) => {
    await loadUserSet(page);
    await saveSample(page);
    await page.reload();
    await expect(page.locator("#romStatus")).toHaveText("Loaded stored ROM set");
    await expect.poll(() => page.evaluate(() => (window as any).__test.hiscoreRestored)).toBe(true);
    await expect(page.locator("#factoryNote")).toBeHidden();
    expect(await page.evaluate(() => (window as any).__test.machine.doorOpen())).toBe(false);
  });

  test("the factory setup ends by shutting the door and powering up again", async ({ page }) => {
    test.setTimeout(60_000);
    await page.locator("#romFile").setInputFiles(userSet());
    await expect(page.locator("#factoryNote")).toBeVisible();
    await expect(page.locator("#factoryNote")).toBeHidden({ timeout: 40_000 });
    expect(await page.evaluate(() => (window as any).__test.factoryActive)).toBe(false);
    expect(await page.evaluate(() => (window as any).__test.machine.doorOpen())).toBe(false);
    expect(await frames(page)).toBeLessThan(600);
  });

  test("Remove ROMs disables Reset again", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(userSet());
    await expect(page.locator("#resetHiscore")).toBeEnabled();
    await page.locator("#removeRomBtn").click();
    await expect(page.locator("#romStatus")).toHaveText("Running test ROM");
    await page.waitForFunction(() => !!(window as any).__test?.machine);
    await expect(page.locator("#resetHiscore")).toBeDisabled();
  });

  test("Reset Cancel leaves the saved CMOS in place", async ({ page }) => {
    await loadUserSet(page);
    await saveSample(page);
    await page.locator("#resetHiscore").click();
    await expect(page.locator("#hiscoreResetHint")).toBeVisible();
    await page.locator("#hiscoreResetCancel").click();
    await expect(page.locator("#hiscoreResetHint")).toBeHidden();
    expect(await t(page)).toEqual(SAMPLE);
    expect(Object.keys(await saved(page))).toHaveLength(1);
  });

  test("Reset confirm clears the saved image and starts the factory setup", async ({ page }) => {
    await loadUserSet(page);
    await saveSample(page);
    expect(Object.keys(await saved(page))).toHaveLength(1);

    await page.locator("#resetHiscore").click();
    await page.locator("#hiscoreResetOk").click();
    await expect(page.locator("#hiscoreResetHint")).toBeHidden();
    await expect.poll(async () => Object.keys(await saved(page)).length).toBe(0);
    await expect(page.locator("#factoryNote")).toBeVisible();
    expect(await page.evaluate(() => (window as any).__test.factoryActive)).toBe(true);
    expect(await t(page)).toEqual(BLANK);
  });
});
