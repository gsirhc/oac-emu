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
    const notices = page.locator(".footer .notices");
    const link = (name: string) => notices.locator(`a:text-is("${name}")`);
    await expect(link("Bochs BIOS")).toHaveAttribute("href", `https://github.com/bochs-emu/Bochs/tree/${BOCHS}/bochs/bios`);
    await expect(notices).toContainText("Bochs BIOS (LGPL 2.1)");
    await expect(notices.locator('a:text-is("LGPL 2.1")')).toHaveCount(1);
    for (const a of await notices.locator('a:text-is("LGPL 2.1")').all()) await expect(a).toHaveAttribute("href", "roms/LGPL-2.1.txt");
    const lgpl = await page.evaluate(async () => (await fetch("roms/LGPL-2.1.txt")).text());
    expect(lgpl).toContain("GNU LESSER GENERAL PUBLIC LICENSE\n                       Version 2.1, February 1999");
    await expect(notices).toContainText("VGA ROM fonts by Yossi Gil (public domain)");
    // ibiblio's FreeDOS tree is unreachable; the Wayback copy matches fetch-freedos-cd.sh's SHA-256
    await expect(link("FreeDOS 1.3")).toHaveAttribute("href", `https://web.archive.org/web/2023id_/${LIVECD}`);
    await expect(link("Sources")).toHaveAttribute("href", MIRROR);
    await expect(notices.locator("p").last()).toHaveText(/^IBM, PC\/AT and EGA are trademarks of IBM Corp\. .*isn't affiliated with or endorsed by IBM\.$/);
    await expect(link("Inter")).toHaveCount(0);
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
