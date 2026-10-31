import { test, expect } from "@playwright/test";
import { readFileSync } from "fs";
import { HWTEST_FILES, ROM_DB, USER_BYTE } from "./cabinet";

function buf(path: string, name: string, patch?: (b: Buffer) => Buffer) {
  let buffer: Buffer = readFileSync(path);
  if (patch) buffer = patch(buffer);
  return { name, mimeType: "application/octet-stream", buffer };
}

function hwtestNamed(names: Record<string, string>, patchProgram?: (b: Buffer) => Buffer) {
  return HWTEST_FILES.map((path) => {
    const base = path.split("/").pop()!;
    const patch = base === "bank.bin" ? patchProgram : undefined;
    return buf(path, names[base] || base, patch);
  });
}

function crc32(buf: Buffer) {
  let c = ~0;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i];
    for (let j = 0; j < 8; j++) c = (c >>> 1) ^ (0xEDB88320 & -(c & 1));
  }
  return (~c) >>> 0;
}

function zipStore(files: { name: string; data: Buffer }[]) {
  const locals: Buffer[] = [];
  const centrals: Buffer[] = [];
  let offset = 0;
  for (const f of files) {
    const name = Buffer.from(f.name);
    const crc = crc32(f.data);
    const local = Buffer.alloc(30 + name.length);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0, 8);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(f.data.length, 18);
    local.writeUInt32LE(f.data.length, 22);
    local.writeUInt16LE(name.length, 26);
    name.copy(local, 30);
    const piece = Buffer.concat([local, f.data]);
    locals.push(piece);
    const central = Buffer.alloc(46 + name.length);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(20, 4);
    central.writeUInt16LE(20, 6);
    central.writeUInt32LE(crc, 16);
    central.writeUInt32LE(f.data.length, 20);
    central.writeUInt32LE(f.data.length, 24);
    central.writeUInt16LE(name.length, 28);
    central.writeUInt32LE(offset, 42);
    name.copy(central, 46);
    centrals.push(central);
    offset += piece.length;
  }
  const cd = Buffer.concat(centrals);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(files.length, 8);
  end.writeUInt16LE(files.length, 10);
  end.writeUInt32LE(cd.length, 12);
  end.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, cd, end]);
}

test.describe("ROM set loader", () => {
  test.beforeEach(async ({ page }) => {
    await page.goto("/?test=1");
    await page.waitForFunction(() => (window as any).__test);
  });

  test("a rejected upload opens a themed dialog with load instructions", async ({ page }) => {
    await page.locator("#romFile").setInputFiles({
      name: "junk.bin",
      mimeType: "application/octet-stream",
      buffer: Buffer.from("this is not a Joust ROM set"),
    });
    const dlg = page.locator("#romErrorHint");
    await expect(dlg).toBeVisible();
    await expect(dlg).toContainText(/isn't a complete set/i);
    await expect(dlg).toContainText(/joust\.zip/);
    await expect(page.locator("#romStatus")).toContainText("ROM rejected");
    expect(await page.evaluate(() => (window as any).__test.usingUserRom)).toBe(false);
    await page.locator("#romErrorOk").click();
    await expect(dlg).toBeHidden();
  });

  test("re-loading the generated hardware self-test ROM is accepted and identified as hwtest", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(HWTEST_FILES);
    await expect(page.locator("#romStatus")).toHaveText("Hardware test ROM");
    expect(await page.evaluate(() => (window as any).__test.usingUserRom)).toBe(false);
  });

  test("an accepted set is persisted in IndexedDB and restored on reload", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(HWTEST_FILES);
    await expect(page.locator("#romStatus")).toHaveText("Hardware test ROM");
    await expect.poll(() => page.evaluate((db) => new Promise((ok) => {
      const r = indexedDB.open(db);
      r.onsuccess = () => {
        const q = r.result.transaction("roms").objectStore("roms").get("set");
        q.onsuccess = () => { r.result.close(); ok(!!q.result); };
      };
    }), ROM_DB)).toBe(true);
    await page.reload();
    await expect(page.locator("#romStatus")).toHaveText("Loaded stored ROM set");
  });

  test("Remove ROMs clears the stored set and reverts to the test ROM", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(HWTEST_FILES);
    await expect(page.locator("#romStatus")).toHaveText("Hardware test ROM");
    await page.locator("#removeRomBtn").click();
    await expect(page.locator("#romStatus")).toHaveText("Running test ROM");
    await page.waitForFunction(() => !!(window as any).__test?.machine);
    await page.reload();
    await expect(page.locator("#romStatus")).toHaveText("Running test ROM");
    await page.waitForFunction(() => !!(window as any).__test?.machine);
  });

  test("an incomplete set is rejected", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(["roms/bank.bin"]);
    await expect(page.locator("#romStatus")).toContainText("ROM rejected");
  });

  test("tells the visitor which sets are allowed", async ({ page }) => {
    const legal = page.locator(".legal").first();
    await expect(legal).toContainText(/Load the ROMs yourself/i);
    await expect(legal).toContainText(/joust\.zip/);
    await expect(legal).toContainText(/checked by size, not a particular CRC/i);
  });

  test("a complete set with a different CRC is still accepted as a user ROM", async ({ page }) => {
    await page.locator("#romFile").setInputFiles(hwtestNamed({}, (b) => {
      const out = Buffer.from(b);
      out[USER_BYTE] ^= 1;
      return out;
    }));
    await expect(page.locator("#romStatus")).toHaveText("Loaded ROM set");
    expect(await page.evaluate(() => (window as any).__test.usingUserRom)).toBe(true);
  });

  test("community dump names assemble the board sockets", async ({ page }) => {
    const split = (path: string, size: number, names: string[]) => {
      const b = readFileSync(path);
      return names.map((name, i) => ({ name, mimeType: "application/octet-stream", buffer: b.subarray(i * size, (i + 1) * size) }));
    };
    const rom = (n: number) => `3006-${12 + n}.${n}b`;
    await page.locator("#romFile").setInputFiles([
      ...split("roms/bank.bin", 0x1000, [1, 2, 3, 4, 5, 6, 7, 8, 9].map(rom)),
      ...split("roms/fixed.bin", 0x1000, [10, 11, 12].map(rom)),
      ...split("roms/decoder4.bin", 0x200, ["decoder.4"]),
      ...split("roms/decoder6.bin", 0x200, ["decoder.6"]),
      ...split("roms/sound.bin", 0x1000, ["joust.snd"]),
    ]);
    await expect(page.locator("#romStatus")).toHaveText("Hardware test ROM");
  });

  test("a .zip of the generated set is accepted", async ({ page }) => {
    const members = hwtestNamed({});
    const zip = zipStore(members.map((f) => ({ name: f.name, data: f.buffer })));
    await page.locator("#romFile").setInputFiles({
      name: "joust.zip",
      mimeType: "application/zip",
      buffer: zip,
    });
    await expect(page.locator("#romStatus")).toHaveText("Hardware test ROM");
  });
});
