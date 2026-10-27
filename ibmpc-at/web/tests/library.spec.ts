import { test, expect } from "./fixtures";
import { boot, bay, typeStr, focusScreen, waitForScreen } from "./helpers";
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
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
    await expect(page.locator("#libraryDialog .lib-meta")).toHaveText("2022  ·  FreeDOS Project  ·  Needs 1.2MB drive A:");
    await expect(page.locator("#libraryDialog .lib-help")).toContainText("A:\\SENET\\SENET");
    await page.locator("#libraryDialog .lib-close").click();
    await expect(page.locator("#libraryDialog")).toBeHidden();
  });

  test("a 360K disk offers both drives, a 1.2MB disk only A:", async ({ page }) => {
    await page.route("**/disks/library/library.json", async (route) => {
      const lib = await (await route.fetch()).json();
      lib.disks.push({ id: "t360", title: "Test 360K", format: 360, images: [{ file: "fdgames.img", label: "360K" }] });
      await route.fulfill({ json: lib });
    });
    await boot(page, { expectScreen: null });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Test 360K" }).click();
    await expect(page.locator("#libraryDialog .lib-disks button")).toHaveText(["Insert in A:", "Insert in B:"]);
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
    await expect(page.locator("#libraryDialog .lib-disks button")).toHaveText(["Insert in A:"]);
  });

  test("Blank diskette offers unformatted and formatted sizes per drive", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Blank diskette" }).click();
    await expect(page.locator("#libraryDialog .lib-help")).toHaveText("");
    const rows = page.locator("#libraryDialog .lib-disk");
    await expect(rows).toHaveCount(4);
    await expect(rows.nth(0).locator("span")).toHaveText("blank 1.2MB (Unformatted)");
    await expect(rows.nth(0).locator("button")).toHaveText(["Insert in A:"]);
    await expect(rows.nth(1).locator("span")).toHaveText("blank 1.2MB");
    await expect(rows.nth(2).locator("span")).toHaveText("blank 360K (Unformatted)");
    await expect(rows.nth(2).locator("button")).toHaveText(["Insert in A:", "Insert in B:"]);
    await expect(rows.nth(3).locator("span")).toHaveText("blank 360K");
  });

  test("inserting an unformatted blank mounts the right size", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Blank diskette" }).click();
    const row = page.locator("#libraryDialog .lib-disk").filter({ has: page.locator("span", { hasText: /^blank 1\.2MB \(Unformatted\)$/ }) });
    await row.locator("button").click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("blank 1.2MB (Unformatted)");
    expect(await page.evaluate(() => (window as any).__test.machine.floppyImage(0).length)).toBe(1228800);
    const zeros = await page.evaluate(() => {
      const img = (window as any).__test.machine.floppyImage(0);
      return img[0] === 0 && img[510] === 0 && img[511] === 0;
    });
    expect(zeros).toBe(true);
  });

  test("a formatted blank is empty FAT12 that DIR can read", async ({ page }) => {
    await boot(page);
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Blank diskette" }).click();
    const row = page.locator("#libraryDialog .lib-disk").filter({ has: page.locator("span", { hasText: /^blank 360K$/ }) });
    await row.locator("button", { hasText: "Insert in B:" }).click();
    await expect(bay(page, 1)).toHaveClass(/loaded/);
    await page.locator("#libraryDialog .lib-close").click();
    await focusScreen(page);
    await waitForScreen(page, /C:\\>\s*$/);
    await typeStr(page, "DIR B:");
    await waitForScreen(page, /Volume in drive B is BLANK[\s\S]*C:\\>\s*$/i, 60_000);
  });

  test("dirty eject Download hands back a file then empties the bay", async ({ page }) => {
    await boot(page);
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
    await page.locator("#libraryDialog .lib-disks button").first().click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await page.locator("#libraryDialog .lib-close").click();
    await focusScreen(page);
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
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Editors and Tools" }).click();
    await page.locator('#libraryDialog .lib-disks button[data-file="fdtools.img"]').click();
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Cancel" }).click();
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("FreeDOS Games");
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeVisible();

    await page.locator('#libraryDialog .lib-disks button[data-file="fdtools.img"]').click();
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Eject without saving" }).click();
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("FreeDOS Editors and Tools");
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeHidden();
  });

  test("dropping over a dirty disk opens the same save dialog", async ({ page }) => {
    await boot(page);
    await dirtyDriveA(page);

    await dropFile(page, "replacement.img", 1228800);
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Cancel" }).click();
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("FreeDOS Games");
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeVisible();

    await dropFile(page, "replacement.img", 1228800);
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

  test("inserting from the library mounts the image and labels the bay", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Editors and Tools" }).click();
    await page.locator('#libraryDialog .lib-disks button[data-file="fdtools.img"]').click();
    await expect(page.locator("#libraryDialog .lib-status")).toHaveText("It is in A:");
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("FreeDOS Editors and Tools");
    expect(await page.evaluate(() => (window as any).__test.machine.floppyImage(0).length)).toBe(1228800);
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
      const formatK = 1200;
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
      const store = w.initFloppyStore({ db: "ibmpcat-floppy" });
      await store.put("Many files", formatK, img);
    });
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "Many files" }).click();
    const expected = Array.from({ length: 10 }, (_, i) => "FILE" + (i + 1) + ".TXT").concat("...").join("\n");
    await expect(page.locator("#libraryDialog .lib-help")).toHaveText(expected);
  });

  test("dirty eject offers save choices; browser save restores from the library", async ({ page }) => {
    await boot(page);
    await openLibrary(page);
    await page.locator("#libraryDialog .lib-item", { hasText: "FreeDOS Games" }).click();
    await page.locator("#libraryDialog .lib-disks button").first().click();
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await page.locator("#libraryDialog .lib-close").click();
    await focusScreen(page);
    await waitForScreen(page, /C:\\>\s*$/);
    await typeStr(page, "COPY C:\\FDCONFIG.SYS A:");
    await waitForScreen(page, /=>> a:fdconfig\.sys[\s\S]*C:\\>\s*$/i, 60_000);
    await expect(bay(page, 0).locator('[data-role="save"]')).toBeVisible();

    await bay(page, 0).locator('[data-role="eject"]').click();
    await expectDirtyDialog(page);
    await page.locator("#floppyDirtyDialog button", { hasText: "Cancel" }).click();
    await expect(page.locator("#floppyDirtyDialog")).toBeHidden();
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
    expect(await page.evaluate(() => (window as any).__test.machine.floppyImage(0).length)).toBe(1228800);
    await page.locator("#libraryDialog .lib-close").click();

    // Reloaded from IndexedDB: the name prompt prefers that saved name.
    await focusScreen(page);
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

  test("a dropped image goes to the first empty drive that takes it", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await dropFile(page, "big.img", 1228800);
    await expect(bay(page, 0)).toHaveClass(/loaded/);
    await dropFile(page, "small.img", 368640);
    await expect(bay(page, 1)).toHaveClass(/loaded/);
    await expect(bay(page, 1).locator('[data-role="label"]')).toHaveText("small.img");
  });

  test("a dropped 1.2MB image never lands in the 360K drive", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await dropFile(page, "one.img", 1228800);
    await dropFile(page, "two.img", 1228800);
    await expect(bay(page, 0).locator('[data-role="label"]')).toHaveText("two.img");
    await expect(bay(page, 1)).not.toHaveClass(/loaded/);
  });

  test("a drop that isn't a floppy image is refused with a reason", async ({ page }) => {
    await boot(page, { expectScreen: null });
    await dropFile(page, "photo.jpg", 100);
    await expect(page.locator(".disk-drop-msg")).toHaveText("photo.jpg isn't a floppy image. The drives take .img files.");
    await dropFile(page, "odd.img", 5000);
    await expect(page.locator(".disk-drop-msg")).toHaveText("odd.img isn't a 1.2MB or 360K floppy image.");
    await expect(bay(page, 0)).not.toHaveClass(/loaded/);
  });
});
