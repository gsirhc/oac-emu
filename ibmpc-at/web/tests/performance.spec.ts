import { test, expect } from "./fixtures";
import { boot } from "./helpers";

// Tier 1 (clock, dropped cycles, main-thread share, emul/draw split, fps) is host-side
// timing and works on the shipped binary. Tier 2 is the emulator's own counters, only in
// the instrumented build (ibmpcat-perf, IBMPCAT_PERF). ?perf asks for it and falls back if absent.

function clkPct(text: string, label: string): number {
  const m = new RegExp(label + "\\s+([\\d.]+)%").exec(text);
  return m ? Number(m[1]) : 0;
}

test.describe("Performance panel", () => {
  test("is absent without ?perf", async ({ page }) => {
    await boot(page);
    await expect(page.locator("#perfCard")).toBeHidden();
  });

  test("?perf shows Tier 1 host metrics and the charts", async ({ page }) => {
    await boot(page, { params: "perf=1" });
    const card = page.locator("#perfCard");
    await expect(card).toBeVisible();

    const out = page.locator("#perfReadout");
    await expect(out).toContainText("MHz of 8.0", { timeout: 15_000 });
    const text = (await out.textContent())!;
    for (const want of ["clock", "dropped", "host", "of one core", "emul", "draw", "fps"]) {
      expect(text).toContain(want);
    }
    const fps = Number(/([\d.]+) fps/.exec(text)![1]);
    expect(fps).toBeGreaterThan(1);
    expect(fps).toBeLessThan(200);
    const host = Number(/host\s+(\d+)% of one core/.exec(text)![1]);
    expect(host).toBeGreaterThanOrEqual(0);
    expect(host).toBeLessThanOrEqual(100);

    const html = await out.innerHTML();
    expect(html).toMatch(/<b>host<\/b>/);

    const painted = await page.evaluate(() =>
      ["perfChart", "perfChart2"].map((id) => {
        const c = document.getElementById(id) as HTMLCanvasElement;
        const d = c.getContext("2d")!.getImageData(0, 0, c.width, c.height).data;
        const seen = new Set<string>();
        for (let i = 0; i < d.length; i += 4) seen.add(d[i] + "," + d[i + 1] + "," + d[i + 2]);
        return seen.size;
      }));
    expect(painted[0]).toBeGreaterThan(2);
    expect(painted[1]).toBeGreaterThan(2);
  });

  test("Tier 2 counters are non-zero on the instrumented build", async ({ page }) => {
    // A second at the DOS prompt can be halt and refresh only, so watch POST and keep the peak.
    await boot(page, { params: "perf=1", expectScreen: null });
    const out = page.locator("#perfReadout");
    await expect(out).toContainText("MHz of 8.0", { timeout: 15_000 });
    await expect(page.locator("#perfTier")).toContainText("Instrumented build");
    const peak = { ram: 0, prefetch: 0, qtax: 0, refresh: 0, cls: 0 };
    await expect
      .poll(async () => {
        const text = (await out.textContent())!;
        peak.ram = Math.max(peak.ram, clkPct(text, "RAM"));
        peak.prefetch = Math.max(peak.prefetch, clkPct(text, "prefetch"));
        peak.qtax = Math.max(peak.qtax, clkPct(text, "queue tax"));
        peak.refresh = Math.max(peak.refresh, clkPct(text, "refresh"));
        peak.cls = Math.max(peak.cls, clkPct(text, "alu") + clkPct(text, "flow"));
        return peak.ram > 0 && peak.prefetch > 0 && peak.qtax > 0 &&
          peak.refresh > 0 && peak.cls > 0;
      }, { timeout: 20_000 })
      .toBe(true);
  });

  test("the shipped binary has no Tier 2", async ({ page }) => {
    await page.route("**/ibmpcat-perf.js", (route) => route.abort());
    await boot(page, { params: "perf=1" });
    await expect(page.locator("#perfCard")).toBeVisible();
    const out = page.locator("#perfReadout");
    await expect(out).toContainText("MHz of 8.0", { timeout: 15_000 });
    const text = (await out.textContent())!;
    expect(text).toContain("clock");
    expect(text).toContain("emul");
    expect(text).not.toContain("prefetch");
    expect(text).not.toContain("queue tax");
    await expect(page.locator("#perfTier")).toContainText("Shipped build");
    const tier2 = await page.evaluate(() => {
      const m = (window as any).__test.machine;
      return typeof m.perfBuild === "function" && m.perfBuild();
    });
    expect(tier2).toBe(false);
  });
});
