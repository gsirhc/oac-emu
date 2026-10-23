import { test, expect } from "./fixtures";
import * as fs from "fs";
import * as path from "path";

const repo = path.join(__dirname, "../../..");
const read = (rel: string) => fs.readFileSync(path.join(repo, rel), "utf8");
const BOCHS = read("ibmpc-at/roms/fetch-bios.sh").match(/bochs-emu\/Bochs\/([0-9a-f]{40})/)![1];
const LIVECD = read("pc486/disks/fetch-freedos-cd.sh").match(/^URL="(.*FD13-LiveCD\.zip)"/m)![1];
const MIRROR = "https://github.com/gsirhc/oac-emu/releases/tag/third-party-sources";

test.describe("footer notices", () => {
  test("credit each third-party part, link its pinned source, and disclaim IBM", async ({ page }) => {
    await page.goto("/?test=1");
    const row = (name: string) => page.locator(".footer .notices p", { has: page.locator(`b:text-is("${name}:")`) });
    await expect(row("Bochs BIOS")).toContainText("GNU LGPL 2.1");
    await expect(row("Bochs BIOS").locator("a").first()).toHaveAttribute("href", `https://github.com/bochs-emu/Bochs/tree/${BOCHS}/bochs/bios`);
    await expect(row("EGA BIOS")).toContainText("Yossi Gil");
    await expect(row("FreeDOS").locator("a").first()).toHaveAttribute("href", LIVECD);
    for (const name of ["Bochs BIOS", "FreeDOS"]) {
      await expect(row(name).locator(`a[href="${MIRROR}"]`)).toHaveCount(1);
    }
    await expect(row("Trademarks")).toHaveText(/IBM, PC\/AT and EGA are trademarks .*isn't affiliated with or endorsed by IBM\./);
    const ofl = await page.request.get(await row("Typeface").locator("a").getAttribute("href") as string);
    expect(ofl.status()).toBe(200);
    expect(await ofl.text()).toContain("SIL OPEN FONT LICENSE Version 1.1");
  });

  test("library disks carry a source pointer and the licenses their packages have", async ({ page }) => {
    await page.goto("/?test=1");
    const disk = await page.evaluate(async () => {
      const r = await fetch("disks/library/fdgames.img");
      return Array.from(new Uint8Array(await r.arrayBuffer()), (b) => String.fromCharCode(b)).join("");
    });
    expect(disk).toContain(MIRROR);
    expect(disk).toContain("GNU GENERAL PUBLIC LICENSE");
  });
});
