// Joust arcade front end. Guest CPU is always 1 MHz E vs wall clock.

const CPU_HZ = 1_000_000;
const FRAME_CYCLES = 64 * 260;
const IDB_NAME = "retroweb-joust";
const TEST = new URLSearchParams(location.search).has("test");
const DOOR_KEY = "retroweb.joust.door";
const ROM_KEY = "set";
const CMOS_LEN = 0x400;
// Long enough for the ROM's power-up tests and its factory restore with the door open.
const FACTORY_SETUP_FRAMES = 1200;

// Coin door into ROM PIA port A, player panel into the widget PIA (machine.h).
const DOOR = { autoUp: 0x01, advance: 0x02, rightCoin: 0x04, hsReset: 0x08, leftCoin: 0x10 };
const PANEL = { left: 0x01, right: 0x02, flap: 0x04, start2: 0x10, start1: 0x20 };

function clip(bytes, n) {
  if (!bytes || bytes.length < n) return null;
  return bytes.length === n ? bytes : bytes.subarray(0, n);
}

function concat(parts, total) {
  const out = new Uint8Array(total);
  let o = 0;
  for (const p of parts) {
    out.set(p, o);
    o += p.length;
  }
  return out;
}

// Self-test files, then the community dump names. ROM n is the D-9144 socket in
// the ROM board chart (4E ... 7E); 3006-(12+n).nb is its label.
const CHIPS = {
  "bank.bin": "bank", "fixed.bin": "fixed", "decoder4.bin": "d4", "decoder6.bin": "d6",
  "sound.bin": "sound", "decoder.4": "d4", "decoder.6": "d6", "joust.snd": "sound",
};
for (let n = 1; n <= 12; n++) CHIPS[`3006-${12 + n}.${n}b`] = `rom${n}`;

function roleOf(name) {
  return CHIPS[String(name).toLowerCase().split("/").pop()] || null;
}

function joined(found, whole, size, chips, chipSize) {
  const w = clip(found[whole], size);
  if (w) return w;
  const parts = chips.map((c) => clip(found[c], chipSize));
  return parts.every(Boolean) ? concat(parts, size) : null;
}

function identifySet(files, hwtestCrcs) {
  const found = {};
  for (const [name, data] of Object.entries(files)) {
    const role = roleOf(name);
    if (role && !found[role]) found[role] = data;
  }
  const bankChips = [1, 2, 3, 4, 5, 6, 7, 8, 9].map((n) => `rom${n}`);
  const set = {
    bank: joined(found, "bank", 0x9000, bankChips, 0x1000),
    fixed: joined(found, "fixed", 0x3000, ["rom10", "rom11", "rom12"], 0x1000),
    d4: clip(found.d4, 0x200),
    d6: clip(found.d6, 0x200),
    sound: clip(found.sound, 0x1000),
  };
  if (!set.bank || !set.fixed || !set.d4 || !set.d6 || !set.sound) return null;
  const files5 = { bank: "bank.bin", fixed: "fixed.bin", d4: "decoder4.bin", d6: "decoder6.bin", sound: "sound.bin" };
  const hw = hwtestCrcs && Object.entries(files5).every(([k, f]) => arcadeHexCrc(set[k]) === hwtestCrcs[f]);
  return { kind: hw ? "hwtest" : "user", ...set };
}

function labelFor(kind) {
  if (kind === "hwtest") return "Hardware test ROM";
  return "Loaded ROM set";
}

initThemePicker();
initFullscreen({
  bezelEl: document.getElementById("bezel"),
  screenEl: document.getElementById("screen"),
  fullscreenBtn: document.getElementById("fullscreenBtn"),
  isRunning: () => true,
});
const updateFocusHint = initFocusHint(document.getElementById("screen"), () => true);
updateFocusHint();

JoustArcade().then(async (Module) => {
  const machine = new Module.Machine();
  const screen = document.getElementById("screen");
  const ctx = screen.getContext("2d");
  const img = ctx.createImageData(machine.screenWidth(), machine.screenHeight());
  const status = document.getElementById("romStatus");
  const store = arcadeStore(IDB_NAME);
  const audio = arcadeAudio(machine, document.getElementById("mute"), "m6808");

  let usingUserRom = false;
  let programCrc = 0;
  let factoryUntil = -1;
  const keys = {};
  const coinDoor = document.getElementById("coinDoor");
  const autoUp = document.getElementById("autoUp");
  const doorOpen = document.getElementById("doorOpen");
  const factoryNote = document.getElementById("factoryNote");

  function stick(left, right, flap) {
    let v = 0;
    if (left) v |= PANEL.left;
    if (right) v |= PANEL.right;
    if (flap) v |= PANEL.flap;
    return v;
  }

  function applyKeys() {
    const down = (k) => !!keys[k];
    machine.setP1(stick(down("ArrowLeft") || down("KeyA"), down("ArrowRight") || down("KeyD"),
      down("Space") || down("KeyZ") || down("ArrowUp") || down("KeyW")));
    machine.setP2(stick(down("KeyJ"), down("KeyL"), down("KeyK") || down("KeyI")));
    let starts = 0;
    if (down("Digit1") || down("Numpad1")) starts |= PANEL.start1;
    if (down("Digit2") || down("Numpad2")) starts |= PANEL.start2;
    machine.setStarts(starts);
    let door = autoUp.checked ? DOOR.autoUp : 0;
    if (down("Digit5") || down("Numpad5") || keys.leftCoin) door |= DOOR.leftCoin;
    if (down("Digit6") || down("Numpad6") || keys.rightCoin) door |= DOOR.rightCoin;
    if (down("F2") || keys.advance) door |= DOOR.advance;
    if (keys.hsReset) door |= DOOR.hsReset;
    machine.setDoor(door);
    machine.setDoorOpen(factoryUntil >= 0 || doorOpen.checked);
  }

  // A coin switch closes for a moment; the ROM debounces it on its 4 ms IRQ.
  function insertCoin(slot) {
    keys[slot] = true;
    applyKeys();
    window.setTimeout(() => { keys[slot] = false; applyKeys(); }, 120);
    coinDoor.classList.add("coined");
    window.setTimeout(() => coinDoor.classList.remove("coined"), 180);
    screen.focus();
    audio.ensure().catch(() => {});
  }
  coinDoor.querySelectorAll("[data-coin]").forEach((el) => {
    el.addEventListener("click", () => insertCoin(el.getAttribute("data-coin") === "right" ? "rightCoin" : "leftCoin"));
  });

  function holdButton(el, onDown, onUp) {
    const down = (e) => {
      if (e.button != null && e.button !== 0) return;
      e.preventDefault();
      try { el.setPointerCapture(e.pointerId); } catch {}
      el.classList.add("pressed");
      onDown();
      applyKeys();
      audio.ensure().catch(() => {});
    };
    const up = () => {
      el.classList.remove("pressed");
      onUp();
      applyKeys();
    };
    el.addEventListener("pointerdown", down);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
  }

  document.querySelectorAll("[data-start]").forEach((el) => {
    const code = el.getAttribute("data-start") === "2" ? "Digit2" : "Digit1";
    holdButton(el, () => { keys[code] = true; screen.focus(); }, () => { keys[code] = false; });
  });
  holdButton(document.getElementById("advanceBtn"), () => { keys.advance = true; }, () => { keys.advance = false; });
  holdButton(document.getElementById("hsResetBtn"), () => { keys.hsReset = true; }, () => { keys.hsReset = false; });

  const touch = initArcadeTouch({
    root: document.getElementById("touchControls"),
    stick: "2way",
    buttons: [{ code: "Space", label: "Flap", cls: "red" }],
    setKey: (code, down) => {
      keys[code] = down;
      applyKeys();
    },
  });

  window.addEventListener("keydown", (e) => {
    keys[e.code] = true;
    applyKeys();
    if (["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space", "F2"].includes(e.code)) e.preventDefault();
    audio.ensure().catch(() => {});
  });
  window.addEventListener("keyup", (e) => {
    keys[e.code] = false;
    applyKeys();
  });

  function blit() {
    img.data.set(machine.frameBuffer());
    ctx.putImageData(img, 0, 0);
  }

  function setStatus(t) { status.textContent = t; }

  function readCmos() {
    const b = new Array(CMOS_LEN);
    for (let i = 0; i < CMOS_LEN; i++) b[i] = machine.cmosByte(i);
    return b;
  }

  function writeCmos(bytes) {
    for (let i = 0; i < CMOS_LEN; i++) machine.setCmosByte(i, bytes[i] & 0x0f);
  }

  const isBlank = (b) => b.every((x) => x === 0);

  // A board leaves Williams with its CMOS set up. A blank one gets that setup:
  // one power-up with the coin door open, then the door shuts and power cycles.
  function factorySetup() {
    factoryUntil = machine.state().frames + FACTORY_SETUP_FRAMES;
    factoryNote.hidden = false;
    applyKeys();
  }

  function finishFactorySetup() {
    if (factoryUntil < 0 || machine.state().frames < factoryUntil) return;
    factoryUntil = -1;
    factoryNote.hidden = true;
    applyKeys();
    machine.reset();
  }

  // The 5114 is battery backed: the saved image is in place before the board powers up.
  const hiscore = arcadeHiscore({
    store,
    len: CMOS_LEN,
    active: () => usingUserRom,
    key: () => String(programCrc >>> 0),
    read: readCmos,
    write: writeCmos,
    isFactory: isBlank,
    restore: (saved) => {
      if (saved && saved.length >= CMOS_LEN) writeCmos(saved);
      machine.reset();
      if (isBlank(readCmos())) factorySetup();
      return readCmos();
    },
  });

  async function resetHiscoreNow() {
    if (!usingUserRom) return;
    await hiscore.forget();
    writeCmos(new Array(CMOS_LEN).fill(0));
    machine.reset();
    factorySetup();
  }
  const syncHiscoreResetBtn = arcadeHiscoreReset(() => usingUserRom, resetHiscoreNow);

  const door = arcadePrefs(DOOR_KEY);
  function saveDoor() {
    door.save({ autoUp: autoUp.checked, open: doorOpen.checked });
    applyKeys();
  }
  const savedDoor = door.load();
  if (savedDoor) {
    if (savedDoor.autoUp !== undefined) autoUp.checked = !!savedDoor.autoUp;
    if (savedDoor.open !== undefined) doorOpen.checked = !!savedDoor.open;
  }
  [autoUp, doorOpen].forEach((el) => el.addEventListener("change", saveDoor));
  document.getElementById("powerBtn").addEventListener("click", () => {
    machine.reset();
    applyKeys();
    screen.focus();
  });
  applyKeys();

  function applySet(set, label) {
    if (!set || !set.bank || !set.fixed || !set.d4 || !set.d6 || !set.sound)
      throw new Error("incomplete ROM set");
    machine.loadRomSet(set.bank, set.fixed, set.d4, set.d6, set.sound);
    usingUserRom = set.kind !== "hwtest";
    programCrc = arcadeCrc32(concat([set.bank, set.fixed], 0xC000));
    factoryUntil = -1;
    factoryNote.hidden = true;
    hiscore.rearm();
    syncHiscoreResetBtn();
    applyKeys();
    setStatus(label);
  }

  let hwtestCrcs = null;
  try { hwtestCrcs = await (await fetch("roms/crc.json")).json(); } catch {}

  function loadBuiltInRom() {
    machine.loadHwtest();
    usingUserRom = false;
    factoryUntil = -1;
    factoryNote.hidden = true;
    hiscore.rearm();
    syncHiscoreResetBtn();
    applyKeys();
    setStatus("Running test ROM");
  }

  const roms = arcadeRomLoader({
    store,
    key: ROM_KEY,
    identify: (files) => identifySet(files, hwtestCrcs),
    apply: (set, stored) => applySet(set, stored ? "Loaded stored ROM set" : labelFor(set.kind)),
    builtIn: loadBuiltInRom,
    setStatus,
  });
  await roms.boot();

  arcadeLoop({
    machine,
    hz: CPU_HZ,
    audio,
    hiscore,
    blit,
    afterRun: finishFactorySetup,
  });

  if (TEST) {
    window.__test = {
      machine,
      touch,
      FRAME_CYCLES,
      get status() { return status.textContent; },
      get usingUserRom() { return usingUserRom; },
      get factoryActive() { return factoryUntil >= 0; },
      mute: document.getElementById("mute"),
      get muted() { return audio.isMuted(); },
      readHiscore: readCmos,
      writeHiscore: writeCmos,
      saveHiscoreNow: hiscore.saveNow,
      maybeSaveHiscore: hiscore.maybeSave,
      get hiscoreRestored() { return hiscore.restored; },
      resetHiscoreNow,
      restoreHiscoreNow: hiscore.restoreNow,
      hiscoreReady: hiscore.ready,
      savedHiscores: hiscore.loadAll,
      crc32: arcadeCrc32,
      identifySet,
      hwtestCrcs,
      applySet,
      ROM_KEY,
    };
  }
});
