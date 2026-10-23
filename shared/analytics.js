"use strict";
// GoatCounter page views and events. count.js loads only on the live site.
// data-machine on the script tag names the machine its events belong to.
const ANALYTICS_HOSTS = ["gsirhc.github.io", "oac-emu.dev", "www.oac-emu.dev"];
const ANALYTICS_MACHINE = (document.currentScript && document.currentScript.dataset.machine) || "";
const PLAY_MARKS = [[5, "playing-5m"], [30, "playing-30m"]];
const IDLE_MS = 2 * 60000;
const analyticsQueue = [];

function trackEvent(name, perMachine) {
  if (perMachine) {
    if (!ANALYTICS_MACHINE) return;
    name = ANALYTICS_MACHINE + "-" + name;
  }
  const gc = window.goatcounter;
  if (gc && typeof gc.count === "function") gc.count({ path: name, title: name, event: true });
  else analyticsQueue.push(name);
}

(function () {
  // keep only the Ready to Play preset; ?full= is an access token
  function pagePath() {
    const disk = new URLSearchParams(location.search).get("disk");
    return location.pathname + (disk ? "?disk=" + disk : "");
  }

  if (ANALYTICS_HOSTS.includes(location.hostname)) {
    window.goatcounter = Object.assign(window.goatcounter || {}, { path: pagePath });
    const s = document.createElement("script");
    s.async = true;
    s.src = "https://gc.zgo.at/count.js";
    s.dataset.goatcounter = "https://oac-emu.goatcounter.com/count";
    s.addEventListener("load", () => analyticsQueue.splice(0).forEach((n) => trackEvent(n)));
    document.head.appendChild(s);
  }

  if (!ANALYTICS_MACHINE) return;
  let started = false;
  let lastInput = 0;
  // arcade keys reach the game from anywhere, so only clicks on page chrome are skipped
  const SKIP = { keydown: "dialog, input, select", pointerdown: ".pagebar, #siteFooter, dialog" };
  function onInput(e) {
    if (!e.isTrusted || (e.target.closest && e.target.closest(SKIP[e.type]))) return;
    lastInput = performance.now();
    if (started) return;
    started = true;
    trackEvent("started", true);
    for (const [min, name] of PLAY_MARKS) {
      setTimeout(() => {
        // a tab left open in the background isn't still playing
        if (!document.hidden && performance.now() - lastInput <= IDLE_MS) trackEvent(name, true);
      }, min * 60000);
    }
  }
  document.addEventListener("keydown", onInput, true);
  document.addEventListener("pointerdown", onInput, true);
})();
