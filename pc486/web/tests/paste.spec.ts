import { Page } from "@playwright/test";
import { test, expect } from "./fixtures";
import { screenText, waitForScreen, focusScreen, keysDrained, tap, emuWait } from "./helpers";

const SAMPLE = "Az09 `~!@#$%^&*()-_=+[{]}\\|;:'\",<.>/?";

async function paste(page: Page, text: string): Promise<void> {
  await page.evaluate((t) => {
    const dt = new DataTransfer();
    dt.setData("text/plain", t);
    (window as any).__test.screenEl.dispatchEvent(
      new ClipboardEvent("paste", { clipboardData: dt, bubbles: true, cancelable: true }),
    );
  }, text);
}

async function pasteDone(page: Page): Promise<void> {
  await page.waitForFunction(() => !(window as any).__test.keyboard.pasteActive);
  await keysDrained(page);
}

function pasteActive(page: Page): Promise<boolean> {
  return page.evaluate(() => (window as any).__test.keyboard.pasteActive);
}

function capsOn(page: Page): Promise<boolean> {
  return page.evaluate(() => ((window as any).__test.machine.keyboardLeds() & 4) !== 0);
}

test.describe("paste", () => {
  test("pasted text types every printable character, honours Caps Lock, and runs lines", async ({ promptPage: page }) => {
    await focusScreen(page);
    try {
      await paste(page, SAMPLE);
      await pasteDone(page);
      await expect.poll(() => screenText(page)).toContain("C:\\>" + SAMPLE);
      await tap(page, "Escape");

      await tap(page, "CapsLock");
      await expect.poll(() => capsOn(page)).toBe(true);
      await paste(page, "aB1!");
      await pasteDone(page);
      await expect.poll(() => screenText(page)).toContain("C:\\>aB1!");
      await tap(page, "CapsLock");
      await expect.poll(() => capsOn(page)).toBe(false);
      await tap(page, "Escape");

      await paste(page, "echo ONE\r\necho TWO\r\n");
      await waitForScreen(page, /^ONE\s*$/m);
      await waitForScreen(page, /^TWO\s*$[\s\S]*C:\\>\s*$/m);
    } finally {
      await page.evaluate(() => (window as any).__test.keyboard.cancelPaste());
      if (await capsOn(page)) await tap(page, "CapsLock");
    }
  });

  test("a paste waits on the BIOS type-ahead, stops on a keypress, and leaves Ctrl+V to the guest", async ({ promptPage: page }) => {
    await page.evaluate(() => {
      const t = (window as any).__test;
      const m = t.machine;
      t.sent = [];
      t.realRamByte = m.ramByte;
      t.realInject = m.injectScancode;
      m.injectScancode = (code: number) => {
        t.sent.push(code);
        t.realInject.call(m, code);
      };
      // head 40:1A points nowhere near tail 40:1C
      m.ramByte = (addr: number) => (addr === 0x41b ? 0xaa : t.realRamByte.call(m, addr));
    });
    try {
      await paste(page, "ab");
      await emuWait(page, 500);
      expect(await pasteActive(page)).toBe(true);
      expect(await page.evaluate(() => (window as any).__test.sent.length)).toBe(0);

      await page.evaluate(() => {
        const t = (window as any).__test;
        t.machine.ramByte = t.realRamByte;
      });
      await pasteDone(page);
      expect(await page.evaluate(() => (window as any).__test.sent)).toEqual([0x1e, 0x9e, 0x30, 0xb0]);

      await page.evaluate(() => {
        const t = (window as any).__test;
        const m = t.machine;
        m.ramByte = (addr: number) => (addr === 0x41b ? 0xaa : t.realRamByte.call(m, addr));
      });
      await paste(page, "abc");
      expect(await pasteActive(page)).toBe(true);
      await page.evaluate(() => {
        const el = (window as any).__test.screenEl;
        el.dispatchEvent(new KeyboardEvent("keydown", { code: "KeyX", bubbles: true, cancelable: true }));
      });
      await keysDrained(page);
      await page.evaluate(() => {
        const el = (window as any).__test.screenEl;
        el.dispatchEvent(new KeyboardEvent("keyup", { code: "KeyX", bubbles: true, cancelable: true }));
      });
      expect(await pasteActive(page)).toBe(false);

      const prevented = await page.evaluate(() => {
        const el = (window as any).__test.screenEl;
        const down = (init: KeyboardEventInit) =>
          !el.dispatchEvent(new KeyboardEvent("keydown", { bubbles: true, cancelable: true, ...init }));
        return {
          cmdV: down({ code: "KeyV", metaKey: true }),
          ctrlShiftV: down({ code: "KeyV", ctrlKey: true, shiftKey: true }),
          ctrlV: down({ code: "KeyV", ctrlKey: true }),
        };
      });
      await keysDrained(page);
      await page.evaluate(() => {
        const el = (window as any).__test.screenEl;
        el.dispatchEvent(new KeyboardEvent("keyup", { code: "KeyV", bubbles: true, cancelable: true }));
      });
      await keysDrained(page);
      expect(prevented).toEqual({ cmdV: false, ctrlShiftV: false, ctrlV: true });
    } finally {
      await page.evaluate(() => {
        const t = (window as any).__test;
        t.keyboard.cancelPaste();
        t.machine.ramByte = t.realRamByte;
        t.machine.injectScancode = t.realInject;
      });
    }
  });
});
