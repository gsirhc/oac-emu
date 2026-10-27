import { test, expect } from "./fixtures";
import { boot, bay, typeStr, focusScreen, waitForScreen, tap } from "./helpers";
import type { Page } from "@playwright/test";

async function openLibrary(page: Page) {
  await page.locator("#libraryBtn").click();
  await expect(page.locator("#libraryDialog")).toBeVisible();
  await expect(page.locator("#libraryDialog .lib-item").first()).toBeVisible();
}

async function dropFile(page: Page, name: string, size: number) {
  await page.evaluate(({ name, size }) => {
    const dt = new DataTransfer();
    dt.items.add(new File([new Uint8Array(size)], name));
    for (const type of ["dragenter", "dragover", "drop"])
      document.body.dispatchEvent(new DragEvent(type, { dataTransfer: dt, bubbles: true, cancelable: true }));
  }, { name, size });
}

const DIRTY_DIALOG_BUTTONS = [
  "Save in this browser", "Download", "Eject without saving", "Cancel",
];

async function expectDirtyDialog(page: Page) {
  await expect(page.locator("#floppyDirtyDialog")).toBeVisible();
  await expect(page.locator("#floppyDirtyDialog")).toContainText("Would you like to save the changes?");
  await expect(page.locator("#floppyDirtyDialog button")).toHaveText(DIRTY_DIALOG_BUTTONS);
}

async function dirtyDriveA(page: Page) {
  await openLibrary(page);
  await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
  await page.locator("#libraryDialog .lib-disks button").first().click();
  await expect(bay(page, 0)).toHaveClass(/loaded/);
  await page.locator("#libraryDialog .lib-close").click();
  await focusScreen(page);
  await tap(page, "Escape");
  await waitForScreen(page, /C:\\>\s*$/);
  await typeStr(page, "COPY C:\\FDCONFIG.SYS A:");
  await waitForScreen(page, /=>> a:fdconfig\.sys[\s\S]*C:\\>\s*$/i, 60_000);
  await expect(bay(page, 0).locator('[data-role="save"]')).toBeVisible();
}

test.describe("Disk library", () => {
  test("every image in library.json is served as a FAT12 floppy of its format", async ({ page }) => {
    await boot(page, { expectScreen: null });
    const report = await page.evaluate(async () => {
      const lib = await (await fetch("disks/library/library.json")).json();
      const out: { file: string; len: number; k: number; sig: boolean; sectors: number }[] = [];
      for (const e of lib.disks) {
        for (const img of e.images) {
          const b = new Uint8Array(await (await fetch("disks/library/" + img.file)).arrayBuffer());
          out.push({
            file: img.file,
            len: b.length,
            k: e.build ? e.build.format : e.format,
            sig: b[510] === 0x55 && b[511] === 0xaa,
            sectors: b[19] | (b[20] << 8),
          });
        }
      }
      return out;
    });
    expect(report.length).toBeGreaterThan(0);
    for (const r of report) {
      expect(r.len, r.file).toBe(r.k * 1024);
      expect(r.sectors, r.file).toBe(r.len / 512);
      expect(r.sig, r.file).toBe(true);
    }
  });

  test("lists each title with its year and shows what it needs", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await openLibrary(page);
    const lib = await page.evaluate(() => fetch("disks/library/library.json").then((r) => r.json()));
    await expect(page.locator("#libraryDialog .lib-item")).toHaveCount(lib.disks.length + 1);
    await expect(page.locator("#libraryDialog .lib-item", { hasText: "Blank diskette" })).toBeVisible();
    await page.locator("#libraryDialog .lib-item", { hasText: "Wing" }).click();
    await expect(page.locator("#libraryDialog .lib-meta")).toHaveText("2000  ·  Adam Hiatt, Anil Shrestra  ·  Needs 386, VGA, hard disk");
    await expect(page.locator("#libraryDialog .lib-disks button")).toHaveText(["Insert in A:", "Insert in A:", "Insert in A:"]);
    await page.locator("#libraryDialog .lib-close").click();
    await expect(page.locator("#libraryDialog")).toBeHidden();
  });

  test("Blank diskette offers unformatted and formatted sizes for A:", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Blank diskette" }).click();
    await expect(page.locator("#libraryDialog .lib-help")).toHaveText("");
    const rows = page.locator("#libraryDialog .lib-disk");
    await expect(rows).toHaveCount(4);
    await expect(rows.nth(0).locator("span")).toHaveText("blank 1.44MB (Unformatted)");
    await expect(rows.nth(0).locator("button")).toHaveText(["Insert in A:"]);
    await expect(rows.nth(1).locator("span")).toHaveText("blank 1.44MB");
    await expect(rows.nth(2).locator("span")).toHaveText("blank 720K (Unformatted)");
    await expect(rows.nth(3).locator("span")).toHaveText("blank 720K");
  });

  test("inserting an unformatted blank mounts the right size", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Blank diskette" }).click();
    const row = page.locator("#libraryDialog .lib-disk").filter({ has: page.locator("span", { hasText: /^blank 1\.44MB \(Unformatted\)$/ }) });
    await row.locator("button").click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("blank 1.44MB (Unformatted)");
    expect(await page.evaluate(() => (window as any).__test.machine.floppyImage().length)).toBe(1474560);
  });

  test("a formatted blank is empty FAT12 that DIR can read", async ({ page }) => {
    await boot(page);
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Blank diskette" }).click();
    const row = page.locator("#libraryDialog .lib-disk").filter({ has: page.locator("span", { hasText: /^blank 1\.44MB$/ }) });
    await row.locator("button").click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await page.locator("#libraryDialog .lib-close").click();
    await focusScreen(page);
    await tap(page, "Escape");
    await waitForScreen(page, /C:\\>\s*$/);
    await typeStr(page, "DIR A:");
    await waitForScreen(page, /Volume in drive A is BLANK[\s\S]*C:\\>\s*$/i, 60_000);
  });

  test("inserting from the library mounts the image and labels the bay", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Wing" }).click();
    await page.locator('#libraryDialog .lib-disks button[data-file="wing-3.img"]').click();
    await expect(page.locator("#libraryDialog .lib-status")).toHaveText("Disk 3 is in A:");
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("Wing, disk 3");
    expect(await page.evaluate(() => (window as any).__test.machine.floppyImage().length)).toBe(1474560);
  });

  test("SAVE shows once DOS writes to the disk, and opens the save dialog", async ({ page }) => {
    await boot(page);
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
    await page.locator("#libraryDialog .lib-disks button").first().click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await page.locator("#libraryDialog .lib-close").click();
    const save = bay(page, 0).locator('[data-role="save"]');
    await expect(save).toBeHidden();
    await focusScreen(page);
    await tap(page, "Escape");
    await waitForScreen(page, /C:\\>\s*$/);
    await typeStr(page, "COPY C:\\FDCONFIG.SYS A:");
    await waitForScreen(page, /=>> a:fdconfig\.sys[\s\S]*C:\\>\s*$/i, 60_000);
    await expect(save).toBeVisible();
    await save.click();
    await expectDirtyDialog(page);
    const dl = page.waitForEvent("download");
    await page.locator("#floppyDirtyDialog button", { hasText: "Download" }).click();
    expect((await dl).suggestedFilename()).toBe("FreeDOS_Games.img");
    await expect(save).toBeHidden();
  });

  test("Saved detail lists at most 10 root files then ...", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await page.evaluate(async () => {
      const w = window as any;
      const formatK = 1440;
      const img = w.makeBlankFloppy(formatK, { formatted: true });
      const bps = img[11] | (img[12] << 8);
      const reserved = img[14] | (img[15] << 8);
      const fats = img[16];
      const spf = img[22] | (img[23] << 8);
      const rootOff = (reserved + fats * spf) * bps;
      for (let n = 1; n <= 12; n++) {
        const o = rootOff + n * 32;
        const base = ("FILE" + n).padEnd(8, " ");
        for (let i = 0; i < 8; i++) img[o + i] = base.charCodeAt(i);
        img[o + 8] = 0x54; img[o + 9] = 0x58; img[o + 10] = 0x54;  // TXT
        img[o + 11] = 0x20;
      }
      const store = w.initFloppyStore({ db: "pc486-floppy" });
      await store.put("Many files", formatK, img);
    });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Many files" }).click();
    const expected = Array.from({ length: 10 }, (_, i) => "FILE" + (i + 1) + ".TXT").concat("...").join("\n");
    await expect(page.locator("#libraryDialog .lib-help")).toHaveText(expected);
  });

  test("dirty eject Cancel keeps the disk; Save in this browser restores it", async ({ page }) => {
    await boot(page);
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
    await page.locator("#libraryDialog .lib-disks button").first().click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await page.locator("#libraryDialog .lib-close").click();
    await focusScreen(page);
    await tap(page, "Escape");
    await waitForScreen(page, /C:\\>\s*$/);
    await typeStr(page, "COPY C:\\FDCONFIG.SYS A:");
    await waitForScreen(page, /=>> a:fdconfig\.sys[\s\S]*C:\\>\s*$/i, 60_000);

    await bay(page, 0).locator('[data-role="eject"]').click();
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Cancel" }).click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);

    await bay(page, 0).locator('[data-role="eject"]').click();
    await page.locator("#floppyDirtyDialog button", { hasText: "Save in this browser" }).click();
    const nameDlg = page.locator("#floppySaveNameDialog");
    await expect(nameDlg).toBeVisible();
    await expect(nameDlg.locator(".floppy-save-name")).not.toHaveValue("");
    await nameDlg.locator(".floppy-save-name").fill("My games disk");
    await nameDlg.locator(".floppy-save-ok").click();
    await expect(bay(page, 0)).not.toHaveClass(/loaded/);

    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "My games disk" }).click();
    await expect(page.locator("#libraryDialog .lib-meta")).toContainText("saved in this browser");
    await expect(page.locator("#libraryDialog .lib-help")).toContainText(/FDCONFIG\.SYS/i);
    await page.locator("#libraryDialog .lib-disks button", { hasText: "Insert in A:" }).click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    expect(await page.evaluate(() => (window as any).__test.machine.floppyImage().length)).toBe(1474560);
    await page.locator("#libraryDialog .lib-close").click();

    await focusScreen(page);
    await tap(page, "Escape");
    await waitForScreen(page, /C:\\>\s*$/);
    await typeStr(page, "COPY C:\\FDAUTO.BAT A:");
    await waitForScreen(page, /=>> a:fdauto\.bat[\s\S]*C:\\>\s*$/i, 60_000);
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeVisible();
    await bay(page, 0).locator('[data-role="eject"]').click();
    await page.locator("#floppyDirtyDialog button", { hasText: "Save in this browser" }).click();
    await expect(page.locator("#floppySaveNameDialog .floppy-save-name")).toHaveValue("My games disk");
    await page.locator("#floppySaveNameDialog .floppy-save-ok").click();
    await expect(page.locator("#floppyOverwriteDialog")).toBeVisible();
    await page.locator("#floppyOverwriteDialog .floppy-overwrite-ok").click();
    await expect(bay(page, 0)).not.toHaveClass(/loaded/);

    await openLibrary(page);
    await expect(page.locator("#libraryDialog .lib-item", { hasText: "My games disk" })).toHaveCount(1);
  });

  test("dirty eject Download hands back a file then empties the bay", async ({ page }) => {
    await boot(page);
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
    await page.locator("#libraryDialog .lib-disks button").first().click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await page.locator("#libraryDialog .lib-close").click();
    await focusScreen(page);
    await tap(page, "Escape");
    await waitForScreen(page, /C:\\>\s*$/);
    await typeStr(page, "COPY C:\\FDCONFIG.SYS A:");
    await waitForScreen(page, /=>> a:fdconfig\.sys[\s\S]*C:\\>\s*$/i, 60_000);

    const dl = page.waitForEvent("download");
    await bay(page, 0).locator('[data-role="eject"]').click();
    await page.locator("#floppyDirtyDialog button", { hasText: "Download" }).click();
    expect((await dl).suggestedFilename()).toBe("FreeDOS_Games.img");
    await expect(bay(page, 0)).not.toHaveClass(/loaded/);
  });

  test("dirty eject without saving empties the bay and skips IndexedDB", async ({ page }) => {
    await boot(page);
    await dirtyDriveA(page);

    await bay(page, 0).locator('[data-role="eject"]').click();
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Eject without saving" }).click();
    await expect(bay(page, 0)).not.toHaveClass(/loaded/);
    await openLibrary(page);
    await expect(page.locator("#libraryDialog .lib-heading", { hasText: "Saved in this browser" })).toHaveCount(0);
  });

  test("inserting over a dirty disk opens the same save dialog", async ({ page }) => {
    await boot(page);
    await dirtyDriveA(page);
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("FreeDOS Games");

    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Wing" }).click();
    await page.locator('#libraryDialog .lib-disks button[data-file="wing-3.img"]').click();
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Cancel" }).click();
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("FreeDOS Games");
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeVisible();

    await page.locator('#libraryDialog .lib-disks button[data-file="wing-3.img"]').click();
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Eject without saving" }).click();
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("Wing, disk 3");
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeHidden();
  });

  test("dropping over a dirty disk opens the same save dialog", async ({ page }) => {
    await boot(page);
    await dirtyDriveA(page);

    await dropFile(page, "replacement.img", 1474560);
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Cancel" }).click();
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("FreeDOS Games");
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeVisible();

    await dropFile(page, "replacement.img", 1474560);
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Eject without saving" }).click();
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("replacement.img");
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeHidden();
  });

  test("a failed browser save tells the visitor and keeps the dirty disk", async ({ page }) => {
    await boot(page);
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
    await page.locator("#libraryDialog .lib-disks button").first().click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await page.locator("#libraryDialog .lib-close").click();
    await focusScreen(page);
    await tap(page, "Escape");
    await waitForScreen(page, /C:\\>\s*$/);
    await typeStr(page, "COPY C:\\FDCONFIG.SYS A:");
    await waitForScreen(page, /=>> a:fdconfig\.sys[\s\S]*C:\\>\s*$/i, 60_000);

    await page.evaluate(() => {
      const put = IDBObjectStore.prototype.put;
      IDBObjectStore.prototype.put = function () {
        throw new DOMException("QuotaExceededError");
      };
      (window as any).__restoreIdbPut = () => { IDBObjectStore.prototype.put = put; };
    });
    try {
      await bay(page, 0).locator('[data-role="save"]').click();
      await page.locator("#floppyDirtyDialog button", { hasText: "Save in this browser" }).click();
      const nameDlg = page.locator("#floppySaveNameDialog");
      await expect(nameDlg).toBeVisible();
      await nameDlg.locator(".floppy-save-name").fill("Should fail");
      await nameDlg.locator(".floppy-save-ok").click();
      await expect(page.locator("#floppySaveFailedDialog")).toBeVisible();
      await page.locator("#floppySaveFailedDialog .floppy-save-failed-ok").click();
      await expect(bay(page, 0)).toHaveClass(/loaded/);
      await expect(bay(page, 0).locator('[data-role="save"]')).toBeVisible();
    } finally {
      await page.evaluate(() => (window as any).__restoreIdbPut());
    }
  });

  test("a dropped image goes into drive A:, replacing what's there", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await dropFile(page, "one.img", 1474560);
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("one.img");
    await dropFile(page, "two.img", 737280);
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("two.img");
  });

  test("a drop that isn't a floppy image is refused with a reason", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await dropFile(page, "photo.jpg", 100);
    await expect(page.locator(".disk-drop-msg")).toHaveText("photo.jpg isn't a floppy image. Drive A: takes .img files.");
    await dropFile(page, "odd.img", 5000);
    await expect(page.locator(".disk-drop-msg")).toHaveText("odd.img isn't a 1.44MB or 720K floppy image.");
    await expect(bay(page, 0)).not.toHaveClass(/loaded/);
  });
});
