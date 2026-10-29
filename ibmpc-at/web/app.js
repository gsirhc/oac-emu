"use strict";
(() => {
  // ---- page theme ----
  // shared retro8080.theme localStorage key
  initThemePicker();

  // "Last built" is the wasm's mtime from the server
  (async () => {
    const el = document.getElementById("buildDate");
    for (const url of ["ibmpcat.wasm", "ibmpcat.js", "app.js"]) {
      try {
        const r = await fetch(url, { method: "HEAD", cache: "no-store" });
        const lm = r.headers.get("Last-Modified");
        if (lm) {
          el.textContent = new Date(lm).toLocaleString(undefined,
            { year: "numeric", month: "long", day: "numeric", hour: "numeric", minute: "2-digit" });
          return;
        }
      } catch {}
    }
    /* v8 ignore next, only if every HEAD request fails / lacks Last-Modified */
    el.textContent = "unknown";
  })();

  // Test-only CPU multiplier (?test=1&fast=1). Bare ?test=1 stays real speed.
  const testParams = new URLSearchParams(location.search);
  const TEST_CPU_MULTIPLIER =
    testParams.get("test") === "1" && testParams.get("fast") === "1" ? 20 : 1;
  // ?perf loads the instrumented build, a separate binary with the emulator's counters (Tier 2).
  const perfRequested = testParams.has("perf");

  // ---- keyboard: key -> AT Set 1 scan code ----
  // number = one byte, array = 0xE0-prefixed. Break = make | 0x80 on the last byte.
  const SET1 = {
    Escape: 0x01,
    Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06,
    Digit6: 0x07, Digit7: 0x08, Digit8: 0x09, Digit9: 0x0A, Digit0: 0x0B,
    Minus: 0x0C, Equal: 0x0D, Backspace: 0x0E,
    Tab: 0x0F, KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12, KeyR: 0x13, KeyT: 0x14,
    KeyY: 0x15, KeyU: 0x16, KeyI: 0x17, KeyO: 0x18, KeyP: 0x19,
    BracketLeft: 0x1A, BracketRight: 0x1B, Enter: 0x1C,
    ControlLeft: 0x1D,
    KeyA: 0x1E, KeyS: 0x1F, KeyD: 0x20, KeyF: 0x21, KeyG: 0x22, KeyH: 0x23,
    KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26,
    Semicolon: 0x27, Quote: 0x28, Backquote: 0x29,
    ShiftLeft: 0x2A, Backslash: 0x2B,
    KeyZ: 0x2C, KeyX: 0x2D, KeyC: 0x2E, KeyV: 0x2F, KeyB: 0x30, KeyN: 0x31, KeyM: 0x32,
    Comma: 0x33, Period: 0x34, Slash: 0x35, ShiftRight: 0x36,
    NumpadMultiply: 0x37, AltLeft: 0x38, Space: 0x39, CapsLock: 0x3A,
    F1: 0x3B, F2: 0x3C, F3: 0x3D, F4: 0x3E, F5: 0x3F, F6: 0x40,
    F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44,
    NumLock: 0x45, ScrollLock: 0x46,
    Numpad7: 0x47, Numpad8: 0x48, Numpad9: 0x49, NumpadSubtract: 0x4A,
    Numpad4: 0x4B, Numpad5: 0x4C, Numpad6: 0x4D, NumpadAdd: 0x4E,
    Numpad1: 0x4F, Numpad2: 0x50, Numpad3: 0x51, Numpad0: 0x52, NumpadDecimal: 0x53,
    F11: 0x57, F12: 0x58,
    ControlRight: [0xE0, 0x1D], AltRight: [0xE0, 0x38],
    Insert: [0xE0, 0x52], Delete: [0xE0, 0x53],
    Home: [0xE0, 0x47], End: [0xE0, 0x4F], PageUp: [0xE0, 0x49], PageDown: [0xE0, 0x51],
    ArrowUp: [0xE0, 0x48], ArrowLeft: [0xE0, 0x4B], ArrowRight: [0xE0, 0x4D], ArrowDown: [0xE0, 0x50],
    NumpadEnter: [0xE0, 0x1C], NumpadDivide: [0xE0, 0x35],
    // Fixed sequences, no simple break-bit form. Pause has no break code (Set 1).
    PrintScreen: { make: [0xE0, 0x2A, 0xE0, 0x37], break: [0xE0, 0xB7, 0xE0, 0xAA] },
    Pause: { make: [0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5], break: [] },
  };

  let machine = null;

  // The keyboard buffers and paces bytes itself, so a whole sequence goes in at once.
  function injectScancodeSequence(codes) {
    if (!machine) return;
    for (const code of codes) machine.injectScancode(code);
  }

  function sendKey(code, isBreak) {
    const entry = SET1[code];
    if (entry === undefined || !machine) return;
    if (entry && typeof entry === "object" && !Array.isArray(entry)) {
      // Print Screen / Pause: fixed sequences
      injectScancodeSequence(isBreak ? entry.break : entry.make);
      return;
    }
    const bytes = Array.isArray(entry) ? entry.slice() : [entry];
    const last = bytes.length - 1;
    bytes[last] = isBreak ? (bytes[last] | 0x80) : bytes[last];
    injectScancodeSequence(bytes);
  }
  const screenEl = document.getElementById("screen");
  // The keyboard repeats on its own clock, so browser repeats are dropped (pc-keyboard.js).
  const keyboard = initPcKeyboard({
    root: document.getElementById("keyboardBody"),
    toggleId: "keyboardToggle",
    cardId: "keyboardCard",
    sendKey,
    isEnabled: () => poweredOn && !!machine,
    readLeds: () => (machine ? machine.keyboardLeds() : 0),
    keysIdle: () => !!machine && !machine.keysPending(),
    readRam: (addr) => (machine ? machine.ramByte(addr) : 0),
    keymapStorageKey: "retro8080.ibmpcat.keymap",
    openStateKey: "retro8080.ibmpcat.keyboardOpen",
    bezelKeysBtn: document.getElementById("keymapAllBtn"),
  });
  screenEl.addEventListener("keydown", keyboard.keyDown);
  screenEl.addEventListener("keyup", keyboard.keyUp);
  screenEl.addEventListener("paste", keyboard.paste);
  screenEl.addEventListener("blur", keyboard.releaseAll);
  document.addEventListener("visibilitychange", () => { if (document.hidden) keyboard.releaseAll(); });
  // ---- serial mouse (COM1); off by default, remembered ----
  const MOUSE_KEY = "retro8080.ibmpcatMouse";
  const mouseCaptureCheckbox = document.getElementById("mouseCaptureEnabled");
  const mouseCaptureBtn = document.getElementById("mouseCaptureBtn");
  try { mouseCaptureCheckbox.checked = localStorage.getItem(MOUSE_KEY) === "1"; } catch { mouseCaptureCheckbox.checked = false; }
  function syncMouseBezel() {
    mouseCaptureBtn.setAttribute("aria-pressed", mouseCaptureCheckbox.checked ? "true" : "false");
  }
  syncMouseBezel();
  mouseCaptureCheckbox.addEventListener("change", () => {
    try { localStorage.setItem(MOUSE_KEY, mouseCaptureCheckbox.checked ? "1" : "0"); } catch {}
    syncMouseBezel();
    if (!mouseCaptureCheckbox.checked && document.pointerLockElement === screenEl) {
      document.exitPointerLock();
    }
  });
  mouseCaptureBtn.addEventListener("click", () => {
    mouseCaptureCheckbox.checked = !mouseCaptureCheckbox.checked;
    mouseCaptureCheckbox.dispatchEvent(new Event("change"));
  });
  let mouseButtons = 0;
  function mouseButtonBit(button) {
    if (button === 0) return 0x01;
    if (button === 2) return 0x02;
    if (button === 1) return 0x04;
    return 0;
  }
  document.addEventListener("pointerlockchange", () => {
    if (document.pointerLockElement !== screenEl) {
      mouseButtons = 0;
      keyboard.releaseAll();
    }
  });
  screenEl.addEventListener("mousedown", (e) => {
    if (!machine || document.pointerLockElement !== screenEl) return;
    mouseButtons |= mouseButtonBit(e.button);
    machine.injectSerialMouse(0, 0, mouseButtons);
    e.preventDefault();
  });
  screenEl.addEventListener("mouseup", (e) => {
    if (!machine || document.pointerLockElement !== screenEl) return;
    mouseButtons &= ~mouseButtonBit(e.button);
    machine.injectSerialMouse(0, 0, mouseButtons);
    e.preventDefault();
  });
  screenEl.addEventListener("contextmenu", (e) => {
    if (document.pointerLockElement === screenEl) e.preventDefault();
  });
  document.addEventListener("mousemove", (e) => {
    if (!machine || document.pointerLockElement !== screenEl) return;
    if (e.movementX === 0 && e.movementY === 0) return;
    // Microsoft serial: +Y is downward (CuteMouse PROTOCOL.TXT).
    machine.injectSerialMouse(e.movementX, e.movementY, mouseButtons);
  });

  screenEl.addEventListener("click", () => {
    screenEl.focus();
    if (mouseCaptureCheckbox.checked && document.pointerLockElement !== screenEl) {
      screenEl.requestPointerLock();
    }
  });

  // poweredOn is declared later, so pass it as a predicate
  const isRunning = () => poweredOn;
  const updateFocusHint = initFocusHint(screenEl, isRunning);
  initFullscreen({
    bezelEl: document.getElementById("bezel"),
    screenEl,
    fullscreenBtn: document.getElementById("fullscreenBtn"),
    fsEscHint: document.getElementById("fsEscHint"),
    fsEscHintOkBtn: document.getElementById("fsEscHintOk"),
    isRunning,
  });

  // Nestable load overlay. Two rAFs let the spinner paint before a sync mount.
  const loadOverlayEl = document.getElementById("loadOverlay");
  const loadOverlayLabel = document.getElementById("loadOverlayLabel");
  let loadBusyDepth = 0;
  function beginLoad(msg) {
    loadBusyDepth++;
    if (msg) loadOverlayLabel.textContent = msg;
    loadOverlayEl.classList.add("visible");
    loadOverlayEl.setAttribute("aria-hidden", "false");
  }
  function endLoad() {
    loadBusyDepth = Math.max(0, loadBusyDepth - 1);
    if (loadBusyDepth === 0) {
      loadOverlayEl.classList.remove("visible");
      loadOverlayEl.setAttribute("aria-hidden", "true");
    }
  }
  function paintLoadOverlay() {
    return new Promise((resolve) => {
      requestAnimationFrame(() => requestAnimationFrame(resolve));
    });
  }
  async function withLoad(msg, fn) {
    beginLoad(msg);
    try {
      await paintLoadOverlay();
      return await fn();
    } finally {
      endLoad();
    }
  }

  // ---- floppy drives ----
  // insert/eject works powered off too; pendingFloppy is remounted on power-on
  const bays = Array.from(document.querySelectorAll(".at-bay"));
  const driveDefaultLabel = (d) => (d === 0 ? "empty (1.2MB, 5.25″)" : "empty (360KB, 5.25″)");
  function setBayLoaded(bay, name) {
    bay.classList.add("loaded");
    const label = bay.querySelector('[data-role="label"]');
    label.textContent = name;
    label.classList.remove("empty");
    bay.querySelector('[data-role="eject"]').disabled = false;
  }
  function setBayEmpty(bay, drive) {
    bay.classList.remove("loaded");
    const label = bay.querySelector('[data-role="label"]');
    label.textContent = driveDefaultLabel(drive);
    label.classList.add("empty");
    bay.querySelector('[data-role="eject"]').disabled = true;
  }
  const floppyStore = initFloppyStore({ db: "ibmpcat-floppy" });
  // Bay Save, eject, drop/file insert-over, and library insert-over all use the same prompt.
  function downloadFloppyImage(drive) {
    const bay = bays[drive];
    const label = bay.querySelector('[data-role="label"]');
    const blob = new Blob([machine.floppyImage(drive)], { type: "application/octet-stream" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = (label.textContent || "disk").replace(/\.img$/i, "").replace(/[^\w.-]+/g, "_") + ".img";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 4000);
    machine.clearFloppyDirty(drive);
    bay.querySelector('[data-role="save"]').hidden = true;
  }
  async function saveFloppyToBrowser(drive) {
    const bay = bays[drive];
    const bytes = machine.floppyImage(drive);
    const bayLabel = (bay.querySelector('[data-role="label"]').textContent || "").trim();
    const idbName = pendingFloppy[drive] && pendingFloppy[drive].idbName;
    const name = await promptFloppySaveName({
      suggested: idbName || floppyVolumeLabel(bytes) || bayLabel,
    });
    if (!name) return false;
    try {
      if (await floppyStore.findByName(name)) {
        if (!(await promptFloppyOverwrite(name))) return false;
      }
      await floppyStore.put(name, floppyFormatK(bytes.length), bytes);
    } catch (err) {
      console.error("floppy store:", err);
      await promptFloppySaveFailed();
      return false;
    }
    machine.clearFloppyDirty(drive);
    bay.querySelector('[data-role="save"]').hidden = true;
    return true;
  }
  // true = proceed (saved, downloaded, or discarded); false = cancel / failed save.
  async function resolveDirtyFloppy(drive) {
    if (!machine || !machine.floppyDirty(drive)) return true;
    let canBrowser = !floppyStore.blocked;
    if (canBrowser) canBrowser = await floppyStore.probe();
    const choice = await promptFloppyDirty({ canSaveBrowser: canBrowser });
    if (choice === "cancel") return false;
    if (choice === "download") {
      downloadFloppyImage(drive);
      return true;
    }
    if (choice === "browser") return saveFloppyToBrowser(drive);
    machine.clearFloppyDirty(drive);
    bays[drive].querySelector('[data-role="save"]').hidden = true;
    return true;
  }
  function saveFloppy(drive) {
    return resolveDirtyFloppy(drive);
  }
  async function insertFloppy(drive, name, data, meta) {
    if (!(await resolveDirtyFloppy(drive))) return;
    const bay = bays[drive];
    const label = bay.querySelector('[data-role="label"]');
    const before = { text: label.textContent, empty: label.classList.contains("empty") };
    // the front panel is often scrolled away from the screen's overlay
    bay.classList.add("loading");
    label.textContent = "Loading\u2026";
    label.classList.remove("empty");
    try {
      await withLoad("Loading floppy\u2026", async () => {
        const bytes = await data;
        pendingFloppy[drive] = {
          name,
          bytes,
          idbName: meta && meta.idbName ? String(meta.idbName) : "",
        };
        if (machine) machine.mountFloppy(drive, bytes);
        setBayLoaded(bay, name);
      });
    } catch (e) {
      label.textContent = before.text;
      label.classList.toggle("empty", before.empty);
      throw e;
    } finally {
      bay.classList.remove("loading");
    }
  }
  for (const bay of bays) {
    const drive = parseInt(bay.dataset.drive, 10);
    const fileInput = bay.querySelector('[data-role="file"]');
    fileInput.addEventListener("change", async () => {
      const f = fileInput.files[0];
      fileInput.value = "";
      if (!f) return;
      await insertFloppy(drive, f.name, f.arrayBuffer().then((b) => new Uint8Array(b)));
    });
    bay.querySelector('[data-role="save"]').addEventListener("click", () => saveFloppy(drive));
    bay.querySelector('[data-role="eject"]').addEventListener("click", async () => {
      if (!(await resolveDirtyFloppy(drive))) return;
      if (machine) machine.unmountFloppy(drive);
      pendingFloppy[drive] = null;
      setBayEmpty(bay, drive);
    });
  }

  // A: is a 1.2MB drive, which also reads 360K media; B: is 360K only.
  const driveTakes = [(k) => k === 1200 || k === 360, (k) => k === 360];
  initDiskLibrary({
    button: document.getElementById("libraryBtn"),
    drives: [{ id: 0, name: "A:", accepts: driveTakes[0] }, { id: 1, name: "B:", accepts: driveTakes[1] }],
    insert: (drive, label, bytes, meta) => insertFloppy(drive, label, bytes, meta),
    blank: true,
    floppyStore,
  });

  const FLOPPY_K = { 1228800: 1200, 368640: 360 };
  const emptyDrive = () => (pendingFloppy[0] ? (pendingFloppy[1] ? 0 : 1) : 0);
  const diskDrop = initDiskDrop({
    accept: /\.(img|ima)$/i,
    message: () => `Drop a floppy image to put it in drive ${emptyDrive() ? "B" : "A"}:`,
    onDrop: async (f) => {
      const bytes = new Uint8Array(await f.arrayBuffer());
      const k = FLOPPY_K[bytes.length];
      if (!k) {
        diskDrop.flash(`${f.name} isn't a 1.2MB or 360K floppy image.`);
        return;
      }
      const fits = [0, 1].filter((d) => driveTakes[d](k));
      const drive = fits.find((d) => !pendingFloppy[d]) ?? fits[0];
      await insertFloppy(drive, f.name, bytes);
    },
    onReject: (f) => diskDrop.flash(`${f.name} isn't a floppy image. The drives take .img files.`),
  });

  // ---- PC speaker (muted every page load) ----
  // AudioWorklet ring buffer: a main-thread stall holds the last sample instead of
  // leaving a gap that clicks.
  const speakerCheckbox = document.getElementById("speakerEnabled");
  speakerCheckbox.checked = false;
  let audioCtx = null, speakerNode = null, lastLevel = false;

  // worklet source loaded from a Blob URL to keep it in this script
  const kSpeakerWorkletSrc = `
    class PcSpeakerProcessor extends AudioWorkletProcessor {
      constructor() {
        super();
        // ~350ms at 48kHz of jank headroom
        this.ring = new Float32Array(16384);
        this.writeIdx = 0;
        this.readIdx = 0;
        this.available = 0;
        this.lastSample = 0;
        this.port.onmessage = (e) => {
          const chunk = e.data;
          for (let i = 0; i < chunk.length; i++) {
            this.ring[this.writeIdx] = chunk[i];
            this.writeIdx = (this.writeIdx + 1) % this.ring.length;
            if (this.available < this.ring.length) {
              this.available++;
            } else {
              // overflow: drop oldest, like an unread FIFO
              this.readIdx = (this.readIdx + 1) % this.ring.length;
            }
          }
        };
      }
      process(_inputs, outputs) {
        const out = outputs[0][0];
        for (let i = 0; i < out.length; i++) {
          if (this.available > 0) {
            this.lastSample = this.ring[this.readIdx];
            this.readIdx = (this.readIdx + 1) % this.ring.length;
            this.available--;
          }
          // underrun: hold last sample to avoid a click
          out[i] = this.lastSample;
        }
        return true;
      }
    }
    registerProcessor("pc-speaker-processor", PcSpeakerProcessor);

    // Synthesized drive noise, driven by the controllers' own step, motor and access events.
    class DriveNoiseProcessor extends AudioWorkletProcessor {
      constructor() {
        super();
        this.t = 0;
        this.next = 0;
        this.on = false;
        this.power = false;
        this.spin = 0;
        this.motor = [false, false];
        this.motorLevel = [0, 0];
        this.accessUntil = [0, 0, 0];
        this.hddRustle = this.bandpass(1200, 1.5);
        this.rot = [0, 0];
        this.hum = [0, 0];
        this.spinPhase = 0;
        this.air = 0;
        this.pending = [];
        this.voices = [];
        this.port.onmessage = (e) => this.message(e.data);
      }
      message(m) {
        if (m.type === "enable") {
          this.on = m.on;
          this.power = m.power;
          if (!m.on) { this.pending = []; this.voices = []; this.motor = [false, false]; }
          return;
        }
        if (m.type === "power") {
          this.power = m.on;
          if (!m.on) this.spin = 0;
          this.pending = []; this.voices = []; this.motor = [false, false];
          return;
        }
        // 50 ms behind real time absorbs frame jitter; a stall resyncs instead of piling up
        const lead = 0.05 * sampleRate;
        if (this.next < this.t + lead || this.next > this.t + 6 * lead) this.next = this.t + lead;
        const ev = m.ev;
        for (let i = 0; i < ev.length; i += 5) {
          const at = this.next + ev[i], kind = ev[i + 1], src = ev[i + 2], steps = ev[i + 3], per = ev[i + 4];
          if (kind === 0 || kind === 1) this.pending.push({ at, kind: "motor", src, on: kind === 0 });
          else if (kind === 3) this.pending.push({ at, kind: "access", src, len: per });
          else if (src === 2 && steps === 1) this.pending.push({ at, kind: "swing", src, len: per });
          else for (let k = 0; k < Math.min(steps, 1024); k++) this.pending.push({ at: at + k * per, kind: "step", src });
        }
        this.pending.sort((a, b) => a.at - b.at);
        this.next += m.samples;
      }
      bandpass(f, q) {
        const w = 2 * Math.PI * f / sampleRate, al = Math.sin(w) / (2 * q), a0 = 1 + al;
        return { b0: al / a0, b2: -al / a0, a1: -2 * Math.cos(w) / a0, a2: (1 - al) / a0, x1: 0, x2: 0, y1: 0, y2: 0 };
      }
      filt(b, x) {
        const y = b.b0 * x + b.b2 * b.x2 - b.a1 * b.y1 - b.a2 * b.y2;
        b.x2 = b.x1; b.x1 = x; b.y2 = b.y1; b.y1 = y;
        return y;
      }
      start(h) {
        if (h.kind === "motor") { this.motor[h.src] = h.on; return; }
        const sr = sampleRate;
        if (h.kind === "access") {
          this.accessUntil[h.src] = Math.max(this.accessUntil[h.src], h.at + h.len);
          if (h.src === 2) this.hddTick(0, 0.4);
          return;
        }
        if (h.kind === "step" && h.src < 2) {
          this.voices.push({ age: 0, len: 0.008 * sr, decay: 0.0012 * sr, gain: 0.45, bp: this.bandpass(2800, 4), thud: 420, thudGain: 0.25, thudDecay: 0.0015 * sr });
        } else if (h.kind === "step") {
          this.hddTick(0, 1);
        } else {
          const len = Math.max(h.len, 0.004 * sr);
          this.hddTick(0, 1.1);
          this.voices.push({ age: 0, len, decay: len, gain: 0.04, bp: this.bandpass(1800, 3), swell: len });
          this.hddTick(-len, 1);
        }
      }
      // Sharp tick, a low knock and a short ring, as of the actuator hitting the casting.
      hddTick(age, k) {
        const sr = sampleRate;
        this.voices.push({ age, len: 0.03 * sr, decay: 0.0005 * sr, gain: 0.35 * k, bp: this.bandpass(4800, 3), thud: 180, thudGain: 0.35 * k, thudDecay: 0.004 * sr });
        this.voices.push({ age, len: 0.04 * sr, decay: 0.02 * sr, gain: 0.25 * k, bp: this.bandpass(650, 8), impulse: true });
      }
      process(_inputs, outputs) {
        const out = outputs[0][0];
        const sr = sampleRate;
        for (let i = 0; i < out.length; i++, this.t++) {
          while (this.pending.length && this.pending[0].at <= this.t) this.start(this.pending.shift());
          if (!this.on) { out[i] = 0; continue; }
          let s = 0;
          for (let d = 0; d < 2; d++) {
            const target = this.motor[d] && this.power ? 1 : 0;
            this.motorLevel[d] += (target - this.motorLevel[d]) * (target ? 8 : 4) / sr;
            const lvl = this.motorLevel[d];
            if (lvl < 1e-4) continue;
            // A: turns at 360 rpm, B: at 300 (kDrive12M, kDrive360K)
            this.rot[d] += (d === 0 ? 6 : 5) / sr;
            const n = Math.random() * 2 - 1;
            this.hum[d] += (n - this.hum[d]) * 0.01;
            const w = 2 * Math.PI * this.rot[d] * 20;
            s += lvl * ((0.02 * Math.sin(w) + 0.008 * Math.sin(2 * w)) * (1 + 0.05 * Math.sin(2 * Math.PI * this.rot[d])) + this.hum[d] * 0.15);
            if (this.t < this.accessUntil[d]) s += lvl * n * 0.003;
          }
          this.spin += ((this.power ? 1 : 0) - this.spin) * (this.power ? 0.25 : 0.4) / sr;
          if (this.spin > 1e-4) {
            this.spinPhase += 60 * this.spin / sr;
            const p = 2 * Math.PI * this.spinPhase;
            this.air += ((Math.random() * 2 - 1) - this.air) * 0.05;
            if (this.t < this.accessUntil[2]) s += this.filt(this.hddRustle, this.air) * 0.15;
            s += this.spin * (0.015 * Math.sin(p) + 0.003 * Math.sin(20 * p) + 0.0015 * Math.sin(60 * p) + 0.015 * this.air);
          }
          for (let v = this.voices.length - 1; v >= 0; v--) {
            const vo = this.voices[v];
            if (vo.age++ < 0) continue;
            if (vo.impulse) {
              s += this.filt(vo.bp, vo.age === 1 ? 80 : 0) * vo.gain;
            } else {
              const env = vo.swell ? Math.sin(Math.PI * vo.age / vo.swell) : Math.exp(-vo.age / vo.decay);
              s += this.filt(vo.bp, Math.random() * 2 - 1) * 6 * env * vo.gain;
            }
            if (vo.thud) s += vo.thudGain * Math.sin(2 * Math.PI * vo.thud * vo.age / sr) * Math.exp(-vo.age / vo.thudDecay);
            if (vo.age >= vo.len) this.voices.splice(v, 1);
          }
          out[i] = Math.max(-1, Math.min(1, s * 0.5));
        }
        return true;
      }
    }
    registerProcessor("drive-noise-processor", DriveNoiseProcessor);
  `;

  async function ensureAudioStarted() {
    if (audioCtx) return;
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    const blobUrl = URL.createObjectURL(new Blob([kSpeakerWorkletSrc], { type: "application/javascript" }));
    try {
      await audioCtx.audioWorklet.addModule(blobUrl);
    } finally {
      URL.revokeObjectURL(blobUrl);
    }
    speakerNode = new AudioWorkletNode(audioCtx, "pc-speaker-processor", { numberOfOutputs: 1, outputChannelCount: [1] });
    speakerNode.connect(audioCtx.destination);
    driveNode = new AudioWorkletNode(audioCtx, "drive-noise-processor", { numberOfOutputs: 1, outputChannelCount: [1] });
    driveNode.connect(audioCtx.destination);
    driveNode.onprocessorerror = () => { driveNoiseLog.errors++; console.error("drive noise worklet failed"); };
    driveNode.port.postMessage({ type: "enable", on: driveNoiseCheckbox.checked, power: poweredOn });
  }
  speakerCheckbox.addEventListener("change", () => {
    if (speakerCheckbox.checked) ensureAudioStarted();
  });

  // ---- drive noise (synthesized, muted every page load) ----
  const driveNoiseCheckbox = document.getElementById("driveNoiseEnabled");
  driveNoiseCheckbox.checked = false;
  let driveNode = null;
  const driveNoiseLog = { motorOn: 0, motorOff: 0, access: [0, 0, 0], seeks: [], frames: 0, errors: 0 };
  driveNoiseCheckbox.addEventListener("change", async () => {
    const on = driveNoiseCheckbox.checked;
    if (machine) machine.setDriveSound(on);
    if (on) await ensureAudioStarted();
    if (driveNode) driveNode.port.postMessage({ type: "enable", on, power: poweredOn });
  });

  function pumpDriveNoise(frameStartCycle, cyclesThisFrame, dtSeconds) {
    const ev = machine.driveSoundEvents();
    if (!driveNoiseCheckbox.checked || cyclesThisFrame <= 0) return;
    for (let i = 0; i < ev.length; i += 5) {
      const kind = ev[i + 1], src = ev[i + 2], steps = ev[i + 3];
      if (kind === 0) driveNoiseLog.motorOn++;
      else if (kind === 1) driveNoiseLog.motorOff++;
      else if (kind === 3) driveNoiseLog.access[src]++;
      else if (driveNoiseLog.seeks.length < 256) driveNoiseLog.seeks.push({ src, steps });
    }
    if (!driveNode) return;
    const sampleRate = audioCtx.sampleRate;
    const cyclesPerRealSecond = cyclesThisFrame / dtSeconds;
    const samples = Math.max(1, Math.round(dtSeconds * sampleRate));
    // offsets follow the frame; step periods and access lengths keep the real 8 MHz time
    for (let i = 0; i < ev.length; i += 5) {
      ev[i] = Math.min(samples, Math.max(0, ((ev[i] - frameStartCycle) / cyclesPerRealSecond) * sampleRate));
      ev[i + 4] = (ev[i + 4] / kCpuHz) * sampleRate;
    }
    driveNode.port.postMessage({ type: "frame", samples, ev }, [ev.buffer]);
    driveNoiseLog.frames++;
  }

  // Turns this frame's (cpu_cycle, level) edges into samples for the worklet ring.
  function pumpAudio(frameStartCycle, cyclesThisFrame, dtSeconds) {
    const edges = machine.speakerEdges();  // always drain, even if muted
    if (!audioCtx || !speakerNode || !speakerCheckbox.checked || cyclesThisFrame <= 0) return;
    const sampleRate = audioCtx.sampleRate;
    // real dtSeconds, not cycles/8MHz, so the fast-test multiplier doesn't overproduce samples
    const sampleCount = Math.max(1, Math.round(dtSeconds * sampleRate));
    const data = new Float32Array(sampleCount);

    let level = lastLevel, sampleIdx = 0;
    const cycles = edges.cycles, levels = edges.levels;
    // effective cycles/sec this frame
    const cyclesPerRealSecond = cyclesThisFrame / dtSeconds;
    for (let i = 0; i < cycles.length; i++) {
      let edgeSample = Math.round(((cycles[i] - frameStartCycle) / cyclesPerRealSecond) * sampleRate);
      if (edgeSample < 0) edgeSample = 0;
      if (edgeSample > sampleCount) edgeSample = sampleCount;
      const v = level ? 0.25 : -0.25;
      for (; sampleIdx < edgeSample; sampleIdx++) data[sampleIdx] = v;
      level = levels[i] !== 0;
    }
    const vTail = level ? 0.25 : -0.25;
    for (; sampleIdx < sampleCount; sampleIdx++) data[sampleIdx] = vTail;
    lastLevel = level;

    speakerNode.port.postMessage(data, [data.buffer]);
  }

  // ---- main loop ----
  const ctx = screenEl.getContext("2d");
  const hddLed = document.getElementById("hddLed");
  let cycleCredit = 0, lastT = null;

  function clearScreenToBlack() {
    setFrameSize(kTextRenderWidth, kTextRenderHeight);
    frameCtx.fillStyle = "#000";
    frameCtx.fillRect(0, 0, frameCanvas.width, frameCanvas.height);
    presentFrame();
  }
  const kTextRenderWidth = 640, kTextRenderHeight = 350;  // ega_render.h text-mode default

  // The guest frame lands in frameCanvas at native resolution, is scaled by a whole factor per
  // axis (nearest) into #screen, then smooth-scaled to the CSS box, so guest pixels stay uniform.
  // The 5154 Enhanced Color Display is a 4:3 tube in every mode, so 640x350 and 320x200 stretch tall.
  const frameCanvas = document.createElement("canvas");
  const frameCtx = frameCanvas.getContext("2d");
  const kMaxScreenScale = 4;  // past 4x the final smoothing pass is invisible
  let screenScaleX = 0, screenScaleY = 0;

  function setFrameSize(w, h) {
    if (frameCanvas.width === w && frameCanvas.height === h && screenScaleX) return;
    frameCanvas.width = w;
    frameCanvas.height = h;
    screenScaleX = screenScaleY = 0;
    fitScreen();
  }

  function fitScreen() {
    const dpr = window.devicePixelRatio || 1;
    const axis = (box, n) => Math.min(kMaxScreenScale, Math.max(1, Math.ceil(box * dpr / n - 0.01)));
    const sx = axis(screenEl.clientWidth, frameCanvas.width);
    const sy = axis(screenEl.clientHeight, frameCanvas.height);
    if (sx === screenScaleX && sy === screenScaleY) return;
    screenScaleX = sx;
    screenScaleY = sy;
    screenEl.width = frameCanvas.width * sx;
    screenEl.height = frameCanvas.height * sy;
    ctx.imageSmoothingEnabled = false;
    presentFrame();
  }

  function presentFrame() {
    ctx.drawImage(frameCanvas, 0, 0, screenEl.width, screenEl.height);
  }

  const screenResizeObserver = new ResizeObserver(fitScreen);
  try {
    screenResizeObserver.observe(screenEl, { box: "device-pixel-content-box" });
  } catch {
    screenResizeObserver.observe(screenEl);
    // Without device-pixel-content-box a monitor move changes no CSS size, so watch the ratio.
    (function watchDpr() {
      matchMedia("(resolution: " + (window.devicePixelRatio || 1) + "dppx)")
        .addEventListener("change", () => { fitScreen(); watchDpr(); }, { once: true });
    })();
  }

  // Per-second accumulators for the Performance panel. dbg.on is set only while it is open.
  const dbg = { on: false, emuMs: 0, renderMs: 0, frames: 0, dropped: 0 };
  const kCpuHz = 8000000;

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const el = document.createElement("script");
      el.src = src;
      el.onload = () => resolve();
      el.onerror = () => reject(new Error("could not load " + src));
      document.head.appendChild(el);
    });
  }
  // Only one module is fetched. A missing twin falls back so the panel keeps its host-side half.
  async function loadEmulatorModule() {
    let src = "ibmpcat.js";
    if (perfRequested) {
      try {
        const head = await fetch("ibmpcat-perf.js", { method: "HEAD" });
        if (head.ok) src = "ibmpcat-perf.js";
      } catch {
        // The panel says the counters are absent.
      }
    }
    await loadScript(src);
  }

  // Clock against 8 MHz over the last minute. 100% is the line the clock should sit on.
  function drawClockChart(c, canvas, history) {
    const w = canvas.width, h = canvas.height;
    c.fillStyle = "#0b0f0b";
    c.fillRect(0, 0, w, h);
    c.strokeStyle = "#1e2c1e";
    c.lineWidth = 1;
    for (let i = 1; i < 4; i++) {
      const y = Math.round(h * i / 4) + 0.5;
      c.beginPath(); c.moveTo(0, y); c.lineTo(w, y); c.stroke();
    }
    for (let i = 1; i < 6; i++) {
      const x = Math.round(w * i / 6) + 0.5;
      c.beginPath(); c.moveTo(x, 0); c.lineTo(x, h); c.stroke();
    }
    const n = 60;
    const xAt = (i) => (i / (n - 1)) * w;
    const yAt = (pct) => h - Math.max(0, Math.min(120, pct)) / 120 * h;
    const first = n - history.length;
    if (history.length > 1) {
      c.beginPath();
      history.forEach((p, i) => {
        const x = xAt(first + i), y = yAt(p.clock);
        if (i === 0) c.moveTo(x, y); else c.lineTo(x, y);
      });
      c.strokeStyle = "#6f6";
      c.lineWidth = 1.5;
      c.stroke();
    }
    const full = Math.round(yAt(100)) + 0.5;
    c.strokeStyle = "#4a5a4a";
    c.setLineDash([3, 3]);
    c.beginPath(); c.moveTo(0, full); c.lineTo(w, full); c.stroke();
    c.setLineDash([]);
    c.font = "11px ui-monospace, Menlo, Consolas, monospace";
    c.fillStyle = "#6f6";
    c.fillText("clock", 6, 13);
    c.fillStyle = "#6a7a6a";
    c.fillText("60s", w - 26, h - 5);
  }

  // Share of one core, and frame rate on its own scale. A stall shows here while the clock barely moves.
  let fpsScale = 60, fpsShrinkFrames = 0;
  function drawHostChart(c, canvas, history) {
    const w = canvas.width, h = canvas.height;
    c.fillStyle = "#0b0f0b";
    c.fillRect(0, 0, w, h);
    c.strokeStyle = "#1e2c1e";
    c.lineWidth = 1;
    for (let i = 1; i < 4; i++) {
      const y = Math.round(h * i / 4) + 0.5;
      c.beginPath(); c.moveTo(0, y); c.lineTo(w, y); c.stroke();
    }
    for (let i = 1; i < 6; i++) {
      const x = Math.round(w * i / 6) + 0.5;
      c.beginPath(); c.moveTo(x, 0); c.lineTo(x, h); c.stroke();
    }
    const n = 60;
    const xAt = (i) => (i / (n - 1)) * w;
    const first = n - history.length;
    let wanted = 60;
    for (const p of history) if (p.fps > wanted) wanted = p.fps;
    wanted = Math.ceil(wanted / 30) * 30;
    if (wanted >= fpsScale) {
      fpsScale = wanted;
      fpsShrinkFrames = 0;
    } else if (++fpsShrinkFrames >= 90) {
      fpsScale = wanted;
      fpsShrinkFrames = 0;
    }
    const fpsMax = fpsScale;
    if (history.length > 1) {
      c.beginPath();
      c.moveTo(xAt(first), h);
      history.forEach((p, i) => c.lineTo(xAt(first + i), h - Math.min(100, p.host) / 120 * h));
      c.lineTo(xAt(first + history.length - 1), h);
      c.closePath();
      c.fillStyle = "rgba(255, 176, 64, 0.26)";
      c.fill();
      c.beginPath();
      history.forEach((p, i) => {
        const x = xAt(first + i), y = h - Math.min(100, p.host) / 120 * h;
        if (i === 0) c.moveTo(x, y); else c.lineTo(x, y);
      });
      c.strokeStyle = "#ffb040";
      c.lineWidth = 1.5;
      c.stroke();
      c.beginPath();
      history.forEach((p, i) => {
        const x = xAt(first + i), y = h - Math.min(1, p.fps / fpsMax) * h * (100 / 120);
        if (i === 0) c.moveTo(x, y); else c.lineTo(x, y);
      });
      c.strokeStyle = "#8ad";
      c.lineWidth = 1.5;
      c.stroke();
    }
    const full = Math.round(h - 100 / 120 * h) + 0.5;
    c.strokeStyle = "#4a5a4a";
    c.setLineDash([3, 3]);
    c.beginPath(); c.moveTo(0, full); c.lineTo(w, full); c.stroke();
    c.setLineDash([]);
    c.font = "11px ui-monospace, Menlo, Consolas, monospace";
    c.fillStyle = "#ffb040";
    c.fillText("host core", 6, 13);
    c.fillStyle = "#8ad";
    c.fillText("fps (max " + fpsMax + ")", 72, 13);
    c.fillStyle = "#6a7a6a";
    c.fillText("60s", w - 26, h - 5);
  }

  // Output goes in the page, not the console: DevTools and a log line a second spend the budget being measured.
  let perfStarted = false;
  function startPerfPanel() {
    if (perfStarted) return;
    perfStarted = true;
    const card = document.getElementById("perfCard");
    const out = document.getElementById("perfReadout");
    card.hidden = false;
    dbg.on = true;
    const tier2 = typeof machine.perfBuild === "function" && machine.perfBuild() &&
      typeof machine.perfStats === "function";
    document.getElementById("perfTier").textContent = tier2
      ? "Instrumented build (ibmpcat-perf.wasm): host metrics and the emulator's own counters."
      : "Shipped build: host metrics only. Reload with ?perf after 'make perf-build' for " +
        "the emulator's internal counters.";
    const chart = document.getElementById("perfChart");
    const cctx = chart.getContext("2d");
    const chart2 = document.getElementById("perfChart2");
    const cctx2 = chart2.getContext("2d");
    const history = [];
    let c0 = machine.totalCycles(), t0 = performance.now();
    // The panel opens as the machine powers on, so the first sample spans that idle gap.
    let first = true;
    const recent = [];
    if (tier2) machine.perfStats();
    const classes = ["alu", "mov", "stack", "flow", "string", "io", "misc", "hlt"];
    const waits = [["ram", "RAM"], ["rom", "ROM"], ["ega", "EGA"],
      ["io8", "8-bit I/O"], ["io16", "16-bit I/O"], ["slot", "slot"]];
    setInterval(() => {
      if (!machine) return;
      const secs = (performance.now() - t0) / 1000;
      const cyc = Number(machine.totalCycles() - c0);
      const stats = tier2 ? machine.perfStats() : "";
      c0 = machine.totalCycles();
      t0 = performance.now();
      if (cyc < 0) return;
      if (secs <= 0 || cyc <= 0) return;
      const mhz = cyc / secs / 1e6;
      if (first) {
        first = false;
        dbg.emuMs = dbg.renderMs = dbg.dropped = 0;
        dbg.frames = 0;
        return;
      }
      recent.push(mhz);
      if (recent.length > 30) recent.shift();
      const avg = recent.reduce((a, b) => a + b, 0) / recent.length;
      const worst = Math.min(...recent);
      const raw = {};
      for (const pair of stats.split(" ")) {
        const eq = pair.indexOf("=");
        if (eq <= 0) continue;
        raw[pair.slice(0, eq)] = Number(pair.slice(eq + 1));
      }
      const emuMs = dbg.emuMs / secs, renderMs = dbg.renderMs / secs;
      const fps = dbg.frames / secs, dropped = dbg.dropped / secs;
      dbg.emuMs = dbg.renderMs = dbg.dropped = 0;
      dbg.frames = 0;
      const cpuPct = Math.min(100, (emuMs + renderMs) / 10);
      const cores = navigator.hardwareConcurrency || 0;
      history.push({ clock: mhz / 8 * 100, host: cpuPct, fps });
      if (history.length > 60) history.shift();
      drawClockChart(cctx, chart, history);
      drawHostChart(cctx2, chart2, history);
      const row = (key, label) => {
        const n = raw[key] || 0;
        const pct = cyc > 0 ? n / cyc * 100 : 0;
        return "  " + label.padEnd(10) + pct.toFixed(1).padStart(5) + "%  " +
          (n / secs / 1e6).toFixed(2) + "M clk/s\n";
      };
      let detail = "";
      if (tier2) {
        detail = "\nclasses\n" + classes.map((k) => row(k, k)).join("") +
          "waits\n" + waits.map(([k, label]) => row(k, label)).join("") +
          row("stall", "prefetch") +
          row("qtax", "queue tax") +
          row("refresh", "refresh") +
          row("dma", "dma");
      }
      out.innerHTML =
        "clock   " + mhz.toFixed(1) + " MHz of 8.0  (" + (mhz / 8 * 100).toFixed(0) + "%)\n" +
        "        avg " + avg.toFixed(1) + " (30s)   min " + worst.toFixed(1) + "\n" +
        "dropped " + (dropped / 1e6).toFixed(2) + "M cyc/s  " +
          (dropped / kCpuHz * 100).toFixed(1) + "% of clock\n" +
        "\n" +
        "<b>host</b>    " + cpuPct.toFixed(0) + "% of one core" +
        (cores ? "  (" + cores + " cores)" : "") + "\n" +
        "  emul  " + emuMs.toFixed(0) + " ms/s\n" +
        "  draw  " + renderMs.toFixed(0) + " ms/s   " + fps.toFixed(1) + " fps\n" +
        detail;
    }, 1000);
  }

  function frame(t) {
    if (!poweredOn || !machine) return;  // powered off mid-loop, stop rescheduling
    if (lastT === null) lastT = t;
    let dtSeconds = (t - lastT) / 1000;
    lastT = t;
    if (dtSeconds > 0.25) {
      if (dbg.on) dbg.dropped += (dtSeconds - 0.25) * kCpuHz * TEST_CPU_MULTIPLIER;
      dtSeconds = 0.25;  // clamp backgrounded-tab gap
    }

    const emuT0 = dbg.on ? performance.now() : 0;
    // real 8 MHz; TEST_CPU_MULTIPLIER is 1 outside ?test=1&fast=1
    cycleCredit += dtSeconds * kCpuHz * TEST_CPU_MULTIPLIER;
    let cyclesThisFrame = Math.floor(cycleCredit);
    cycleCredit -= cyclesThisFrame;
    const frameStartCycle = machine.totalCycles();
    if (TEST_CPU_MULTIPLIER === 1) {
      if (cyclesThisFrame > 0) machine.runCycles(cyclesThisFrame);
    } else {
      // fast test mode: stop at a wall-clock budget and drop the rest, or input starves behind 250ms frames
      const deadline = performance.now() + 12;
      while (cyclesThisFrame > 0 && performance.now() < deadline) {
        const n = Math.min(cyclesThisFrame, 200000);
        machine.runCycles(n);
        cyclesThisFrame -= n;
      }
      if (dbg.on) dbg.dropped += cyclesThisFrame;
      cycleCredit = 0;
    }
    if (dbg.on) dbg.emuMs += performance.now() - emuT0;

    pumpAudio(frameStartCycle, machine.totalCycles() - frameStartCycle, dtSeconds);
    pumpDriveNoise(frameStartCycle, machine.totalCycles() - frameStartCycle, dtSeconds);

    const drawT0 = dbg.on ? performance.now() : 0;
    const rgba = machine.renderFrame();
    // Resolution varies by mode (640x350 text, 320x200 CGA graphics, ega_render.h); the 4:3 box stays put.
    const frameW = machine.renderWidth(), frameH = machine.renderHeight();
    setFrameSize(frameW, frameH);
    const img = frameCtx.createImageData(frameW, frameH);
    img.data.set(rgba);
    frameCtx.putImageData(img, 0, 0);
    presentFrame();
    if (dbg.on) {
      dbg.renderMs += performance.now() - drawT0;
      dbg.frames++;
    }

    for (const bay of bays) {
      const drive = parseInt(bay.dataset.drive, 10);
      const led = bay.querySelector('[data-role="led"]');
      led.classList.toggle("on", machine.floppyPresent(drive) && machine.floppyMotorOn(drive));
      bay.querySelector('[data-role="save"]').hidden = !(machine.floppyPresent(drive) && machine.floppyDirty(drive));
    }
    hddLed.classList.toggle("on", machine.hddBusy());
    pollLptCapture();

    requestAnimationFrame(frame);
  }

  const kFactoryHddLabel = "factory FreeDOS (default)";

  // ---- hard disk persistence (IndexedDB, shared/hdd-worker.js) ----
  // The Machine is discarded on power-off, so C: is mirrored to IndexedDB, written sectors only.
  const hddWorker = (() => {
    const worker = new Worker("shared/hdd-worker.js?model=WD1003&db=ibmpcat-hdd");
    const pending = new Map();
    let nextId = 1;
    worker.onmessage = (e) => {
      const m = e.data;
      if (m.progress) return;
      const p = pending.get(m.id);
      pending.delete(m.id);
      if (m.ok) p.resolve(m.result);
      else p.reject(new Error(m.error));
    };
    worker.onerror = (e) => {
      console.error("hard disk worker failed:", e.message);
      for (const p of pending.values()) p.reject(new Error(e.message));
      pending.clear();
    };
    return {
      call(op, args, transfer) {
        const id = nextId++;
        return new Promise((resolve, reject) => {
          pending.set(id, { resolve, reject });
          worker.postMessage({ id, op, args }, transfer || []);
        });
      },
    };
  })();
  const HDD_STORAGE_BLOCKED = "Warning: the browser blocked storage (IndexedDB). Changes to C: won't be there when you return.";
  let hddStorageOk = true;
  function hddStorageFailed(what, err) {
    console.error(what, err);
    hddStorageOk = false;
    refreshHddControls();
  }
  // A save from the single-record builds loads too; the worker rewrites it as chunks.
  async function loadSavedHdd() {
    try {
      const r = await hddWorker.call("load");
      return r.status === "saved" || r.status === "converted" ? new Uint8Array(r.buffer) : null;
    } catch (err) {
      hddStorageFailed("could not read saved hard disk, using factory default:", err);
      return null;
    }
  }
  let hddPersistChain = Promise.resolve();
  function queueHddOp(what, op, args, transfer) {
    hddPersistChain = hddPersistChain.then(() => hddWorker.call(op, args, transfer)).catch((err) => {
      hddStorageFailed(what, err);
    });
    return hddPersistChain;
  }

  // ---- power switch ----
  // power-off discards the Machine; power-on remounts floppies from pendingFloppy.
  // No reset button on the 5170.
  const powerSwitch = document.getElementById("powerSwitch");
  const powerLed = document.getElementById("powerLed");
  let poweredOn = false;
  let firmware = null;  // {Module, bios, videoBios, hdd}, fetched once
  const pendingFloppy = [null, null];  // {name, bytes} per drive

  // next power-on mounts the saved IndexedDB image if any, else the factory image
  let savedHdd = null;   // Uint8Array | null
  let hddLabel = kFactoryHddLabel;
  const hddStatus = document.getElementById("hddStatus");
  const hddResetBtn = document.getElementById("hddResetBtn");
  const hddBlankBtn = document.getElementById("hddBlankBtn");
  const hddDownloadBtn = document.getElementById("hddDownloadBtn");
  const hddUploadInput = document.getElementById("hddUploadInput");
  function refreshHddControls() {
    hddStatus.textContent = "Using: " + hddLabel + (hddStorageOk ? "" : ". " + HDD_STORAGE_BLOCKED);
    // C: only changes at the next power-on
    hddResetBtn.disabled = !firmware || poweredOn;
    hddBlankBtn.disabled = !firmware || poweredOn;
    hddDownloadBtn.disabled = !firmware;  // read-only, works while running
    hddUploadInput.disabled = !firmware || poweredOn;
    document.getElementById("hddUploadBtn").disabled = !firmware || poweredOn;
  }
  hddResetBtn.addEventListener("click", () => {
    savedHdd = null;
    hddLabel = kFactoryHddLabel + " -- takes effect next power-on";
    refreshHddControls();
    queueHddOp("could not clear saved hard disk:", "clear");
  });
  hddBlankBtn.addEventListener("click", () => {
    if (!firmware) return;
    savedHdd = new Uint8Array(firmware.hdd.byteLength);  // all zero, unformatted
    hddLabel = "blank drive, unformatted (FDISK/FORMAT and install your own OS) -- takes effect next power-on";
    refreshHddControls();
    queueHddOp("could not save blank hard disk:", "blank", { length: savedHdd.byteLength });
  });
  // download C: as a file
  hddDownloadBtn.addEventListener("click", () => {
    if (!firmware) return;
    // live image if running, else staged, else factory
    const bytes = (poweredOn && machine) ? machine.hddImage() : (savedHdd || new Uint8Array(firmware.hdd));
    const blob = new Blob([bytes], { type: "application/octet-stream" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "ibmpcat-hdd.img";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  });
  hddUploadInput.addEventListener("change", async () => {
    const f = hddUploadInput.files[0];
    hddUploadInput.value = "";
    if (!f || !firmware) return;
    await withLoad("Loading hard disk\u2026", async () => {
      const bytes = new Uint8Array(await f.arrayBuffer());
      // WD1003 geometry (733/5/17) is fixed in CMOS, so reject wrong-size images
      if (bytes.byteLength !== firmware.hdd.byteLength) {
        alert("That file is " + bytes.byteLength + " bytes; this machine's hard disk " +
              "must be exactly " + firmware.hdd.byteLength + " bytes (733 cyl / 5 head / " +
              "17 sec/track). Not mounted.");
        return;
      }
      savedHdd = bytes;
      hddLabel = "uploaded image (" + f.name + ") -- takes effect next power-on";
      refreshHddControls();
      const copy = savedHdd.slice().buffer;
      await queueHddOp("could not store uploaded hard disk:", "replace", { buffer: copy, modified: true }, [copy]);
    });
  });

  // ---- LPT1 print capture (always fitted; dialog after a job settles) ----
  const lptDialog = document.getElementById("lptDialog");
  const lptDialogBody = document.getElementById("lptDialogBody");
  const lptDownloadBtn = document.getElementById("lptDownloadBtn");
  const lptDiscardBtn = document.getElementById("lptDiscardBtn");
  const LPT_IDLE_MS = 400;
  let lptLastSize = 0;
  let lptIdleSince = 0;
  let lptOfferedSize = 0;
  function resetLptOffer() {
    lptLastSize = 0;
    lptIdleSince = 0;
    lptOfferedSize = 0;
    if (lptDialog.open) lptDialog.close();
  }
  function downloadLptCapture() {
    if (!machine || machine.lptSize() === 0) return;
    const bytes = machine.lptBytes();
    const blob = new Blob([bytes], { type: "application/octet-stream" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "ibmpcat-lpt1.prn";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  }
  function discardLptCapture() {
    if (machine) machine.lptClear();
    resetLptOffer();
  }
  function pollLptCapture() {
    if (!poweredOn || !machine) return;
    const n = machine.lptSize();
    if (n !== lptLastSize) {
      lptLastSize = n;
      lptIdleSince = performance.now();
      return;
    }
    if (n === 0 || n === lptOfferedSize || lptDialog.open) return;
    if (performance.now() - lptIdleSince < LPT_IDLE_MS) return;
    lptOfferedSize = n;
    lptDialogBody.textContent = n === 1
      ? "DOS sent 1 byte to the printer. Download it as a file, or discard."
      : ("DOS sent " + n + " bytes to the printer. Download them as a file, or discard.");
    hideBootNotice();
    lptDialog.showModal();
  }
  lptDownloadBtn.addEventListener("click", () => {
    downloadLptCapture();
    discardLptCapture();
  });
  lptDiscardBtn.addEventListener("click", () => {
    discardLptCapture();
  });
  lptDialog.addEventListener("cancel", () => {
    // Esc / backdrop: leave the capture; don't re-offer until more bytes arrive.
  });

  // ---- memory ----
  // Memory is fitted at power-on, so the choices are locked while running.
  const EXT_MEM_KEY = "retro8080.ibmpcatExtMemKb";
  const BASE_MEM_KEY = "retro8080.ibmpcatBaseMemKb";
  const extMemSelect = document.getElementById("extMemSelect");
  const baseMemSelect = document.getElementById("baseMemSelect");
  for (const [select, key] of [[extMemSelect, EXT_MEM_KEY], [baseMemSelect, BASE_MEM_KEY]]) {
    try {
      const v = localStorage.getItem(key);
      if (v !== null && [...select.options].some((o) => o.value === v)) select.value = v;
    } catch {}
    select.addEventListener("change", () => {
      try { localStorage.setItem(key, select.value); } catch {}
    });
  }
  function refreshMemoryControls() {
    extMemSelect.disabled = poweredOn;
    baseMemSelect.disabled = poweredOn;
  }

  function remountPendingFloppies() {
    for (let d = 0; d < 2; d++) {
      const p = pendingFloppy[d];
      if (!p) continue;
      machine.mountFloppy(d, p.bytes);
      setBayLoaded(bays[d], p.name);
    }
  }

  // Shown only over the factory C:, until the screen first gains focus.
  const BOOT_NOTICE_KEY = "retro8080.ibmpcatBootNoticeDismissed";
  const bootNoticeEl = document.getElementById("bootNotice");
  function hideBootNotice() { bootNoticeEl.classList.remove("visible"); }
  function dismissBootNotice() {
    hideBootNotice();
    try { localStorage.setItem(BOOT_NOTICE_KEY, "1"); } catch {}
  }
  document.addEventListener("focusin", () => {
    if (screenEl.contains(document.activeElement)) dismissBootNotice();
  });

  function powerOn() {
    if (poweredOn || !firmware) return;
    const factoryC = !savedHdd;
    if (!machine) {
      machine = new firmware.Module.Machine(Number(extMemSelect.value), Number(baseMemSelect.value));
      if (perfRequested) startPerfPanel();
      machine.loadRom(0x100000 - firmware.bios.byteLength, new Uint8Array(firmware.bios));
      machine.loadRom(0xC0000, new Uint8Array(firmware.videoBios));
      machine.mountHdd(savedHdd || new Uint8Array(firmware.hdd));
      machine.setDriveSound(driveNoiseCheckbox.checked);
      remountPendingFloppies();
      // The battery kept the clock running, so a fresh power-on reads the visitor's local time.
      const now = new Date();
      machine.setRtc(now.getFullYear(), now.getMonth() + 1, now.getDate(),
        now.getHours(), now.getMinutes(), now.getSeconds(), now.getDay() + 1);
    }
    poweredOn = true;
    powerLed.classList.add("power-on");
    if (audioCtx) audioCtx.resume().catch(() => {});
    if (driveNode) driveNode.port.postMessage({ type: "power", on: true });
    lastT = null;
    requestAnimationFrame(frame);
    refreshHddControls();
    refreshMemoryControls();
    resetLptOffer();
    refreshFkeyControls();
    updateFocusHint();
    let noticeDismissed = false;
    try { noticeDismissed = localStorage.getItem(BOOT_NOTICE_KEY) === "1"; } catch {}
    if (factoryC && !noticeDismissed) bootNoticeEl.classList.add("visible");
    if (new URLSearchParams(location.search).get("test") === "1") {
      window.__test = {
        machine, sendKey, screenEl, frameCanvas, keyboard,
        cpuMultiplier: TEST_CPU_MULTIPLIER,
        get loadOverlayVisible() { return loadOverlayEl.classList.contains("visible"); },
        get loadOverlayText() { return loadOverlayLabel.textContent; },
        beginLoad, endLoad,
        persistHdd: () => persistHddIfDirty(),
        whenHddSaved: () => hddPersistChain,
        // Skips the one-minute wait before retrying a failed save.
        retryHddSave: () => { hddRetryAt = 0; return persistHddIfDirty(); },
        driveNoiseLog,
      };
    }
  }

  // A failed save already took its sectors out of the dirty set, so the next attempt stores the
  // whole image, at most once a minute while storage keeps refusing.
  let hddNeedsFull = false;
  let hddRetryAt = Infinity;
  let hddWrittenThisSession = false;
  function queueHddSave(m, patches) {
    const full = hddNeedsFull;
    hddRetryAt = Infinity;
    hddPersistChain = hddPersistChain.then(async () => {
      const r = full ? { needFull: true } : await hddWorker.call("patch", { patches }, patches.map((p) => p.bytes.buffer));
      // Nothing stored to patch yet: store the whole image.
      if (r.needFull) {
        const img = m.hddImage();
        await hddWorker.call("replace", { buffer: img.buffer, modified: true }, [img.buffer]);
      }
      hddNeedsFull = false;
      hddStorageOk = true;
      hddLabel = "saved state (changes from this session)";
      refreshHddControls();
    }).catch((err) => {
      hddNeedsFull = true;
      hddRetryAt = performance.now() + 60_000;
      hddLabel = "changes from this session";
      hddStorageFailed("could not save hard disk changes:", err);
    });
    return hddPersistChain;
  }
  function persistHddIfDirty() {
    if (!machine) return hddPersistChain;
    if (!machine.hddDirty() && !(hddNeedsFull && performance.now() >= hddRetryAt)) return hddPersistChain;
    const patches = machine.hddDirtyPatches();
    machine.clearHddDirty();
    hddWrittenThisSession = true;
    return queueHddSave(machine, patches);
  }

  function powerOff() {
    if (!poweredOn) return;
    persistHddIfDirty();
    if (hddWrittenThisSession) savedHdd = machine.hddImage();  // what the next power-on mounts
    hddWrittenThisSession = false;
    poweredOn = false;
    machine = null;      // RAM is gone on power cut
    powerLed.classList.remove("power-on");
    for (const bay of bays) bay.querySelector('[data-role="led"]').classList.remove("on");
    hddLed.classList.remove("on");
    clearScreenToBlack();
    if (driveNode) driveNode.port.postMessage({ type: "power", on: false });
    if (audioCtx) { audioCtx.suspend().catch(() => {}); }
    refreshHddControls();
    refreshMemoryControls();
    resetLptOffer();
    refreshFkeyControls();
    updateFocusHint();
    hideBootNotice();
  }

  // ---- bezel Esc ----
  // Fullscreen swallows the real Esc key, so the bezel sends it.
  const escBtn = document.getElementById("escBtn");
  function refreshFkeyControls() {
    escBtn.disabled = !(poweredOn && !!machine);
    keyboard.refresh();
  }
  escBtn.addEventListener("click", () => {
    sendKey("Escape", false);
    setTimeout(() => sendKey("Escape", true), 50);
  });

  powerSwitch.checked = false;
  powerSwitch.disabled = true;
  clearScreenToBlack();
  refreshFkeyControls();  // start disabled while machine is off
  powerSwitch.addEventListener("change", () => { if (powerSwitch.checked) powerOn(); else powerOff(); });

  // autosave so closing the tab doesn't lose writes
  setInterval(persistHddIfDirty, 5000);

  // last-ditch prompt; gesture navigation can bypass beforeunload
  window.addEventListener("beforeunload", (e) => {
    if (poweredOn && machine && machine.hddDirty()) {
      e.preventDefault();
      e.returnValue = "";
    }
  });

  // ---- fetch firmware + HDD image ----
  // web delivery only, not gated by the power switch
  (async () => {
    beginLoad("Loading\u2026");
    try {
      await paintLoadOverlay();
      await loadEmulatorModule();
      const [Module, savedHddResult, bios, videoBios, hdd] = await Promise.all([
        IbmPcAt({}),
        loadSavedHdd(),
        fetch("roms/BIOS-bochs-legacy").then((r) => r.arrayBuffer()),
        fetch("roms/egabios.bin").then((r) => r.arrayBuffer()),
        fetch("disks/freedos-hdd.img").then((r) => r.arrayBuffer()),
      ]);
      firmware = { Module, bios, videoBios, hdd };
      if (savedHddResult) {
        savedHdd = savedHddResult;
        hddLabel = "saved state (from a previous visit)";
      }

      powerSwitch.disabled = false;
      refreshHddControls();
      // boot once firmware is ready
      powerSwitch.checked = true;
      powerOn();
    } finally {
      endLoad();
    }
  })().catch((err) => {
    // no on-page error; the power switch never enables
    console.error(err);
  });
})();
