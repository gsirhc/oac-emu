// Donkey Kong arcade front end. Guest CPU is always 3.072 MHz vs wall clock.

const CPU_HZ = 3_072_000;
const IDB_NAME = "retroweb-dkong";
const TEST = new URLSearchParams(location.search).has("test");
const DIP_KEY = "retroweb.dkong.dips";
const ROM_KEY = "set";
// Cold start LDIRs $01BA to $60B2 ($01C6); HI-SCORE is $60B8-$60BA. The
// rank table is $AA bytes LDIR'd from ROM $3565 to $6100 ($025D).
const HISCORE_ADDR = 0x60B8;
const HISCORE_LEN = 3;
const HISCORE_ROM = 0x01C0;
const RANK_ADDR = 0x6100;
const RANK_LEN = 0xAA;
const RANK_ROM = 0x3565;

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

// Self-test files, then the community dump names. Board positions are the
// TKG4-11/14 sockets the chips sit in (DKONG_REVIEW.md §1).
const CHIPS = {
  "program.bin": "program", "char.bin": "chars", "obj.bin": "objs",
  "prom.bin": "proms", "sound.bin": "sound",
  "c_5et_g.bin": "5e", "c_5ct_g.bin": "5c", "c_5bt_g.bin": "5b", "c_5at_g.bin": "5a",
  "v_3pt.bin": "3p", "v_5h_b.bin": "3n",
  "l_4m_b.bin": "7c", "l_4n_b.bin": "7d", "l_4r_b.bin": "7e", "l_4s_b.bin": "7f",
  "c-2j.bpr": "2f", "c-2k.bpr": "2e", "v-5e.bpr": "2n",
  "s_3i_b.bin": "3h", "s_3j_b.bin": "3f",
};

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
  const set = {
    program: joined(found, "program", 0x4000, ["5e", "5c", "5b", "5a"], 0x1000),
    chars: joined(found, "chars", 0x1000, ["3p", "3n"], 0x800),
    objs: joined(found, "objs", 0x2000, ["7c", "7d", "7e", "7f"], 0x800),
    proms: joined(found, "proms", 0x300, ["2f", "2e", "2n"], 0x100),
    sound: joined(found, "sound", 0x1000, ["3h", "3f"], 0x800),
  };
  if (!set.program || !set.chars || !set.objs || !set.proms || !set.sound) return null;
  const files5 = { program: "program.bin", chars: "char.bin", objs: "obj.bin", proms: "prom.bin", sound: "sound.bin" };
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

DkongArcade().then(async (Module) => {
  const machine = new Module.Machine();
  const screen = document.getElementById("screen");
  const ctx = screen.getContext("2d");
  const img = ctx.createImageData(224, 256);
  const status = document.getElementById("romStatus");
  const store = arcadeStore(IDB_NAME);
  const audio = arcadeAudio(machine, document.getElementById("mute"), "mcs48");

  let usingUserRom = false;
  let programCrc = 0;
  const keys = {};
  const coinDoor = document.getElementById("coinDoor");
  const dipCoinage = document.getElementById("dipCoinage");
  const dipLives = document.getElementById("dipLives");
  const dipCabinet = document.getElementById("dipCabinet");
  const dipBonus = document.getElementById("dipBonus");

  function applyKeys() {
    // 2P LS240: D0 right, D1 left, D2 up, D3 down, D4 jump. 3P: D2/D3 start.
    let n0 = 0;
    let n2 = 0;
    const down = (k) => keys[k];
    if (down("ArrowRight") || down("KeyD")) n0 |= 0x01;
    if (down("ArrowLeft") || down("KeyA")) n0 |= 0x02;
    if (down("ArrowUp") || down("KeyW")) n0 |= 0x04;
    if (down("ArrowDown") || down("KeyS")) n0 |= 0x08;
    if (down("Space") || down("KeyZ")) n0 |= 0x10;
    if (down("Digit1") || down("Numpad1")) n2 |= 0x04;
    if (down("Digit2") || down("Numpad2")) n2 |= 0x08;
    machine.setIn0(n0);
    machine.setIn1(0);
    machine.setIn2(n2);
    machine.setCoin(!!(down("Digit5") || down("Numpad5") || keys.coinDoor));
    machine.setDip((Number(dipLives.value) | Number(dipBonus.value) | Number(dipCoinage.value) | Number(dipCabinet.value)) & 0xff);
  }

  // The coin switch closes briefly; the board's LS123 stretches the pulse.
  function insertCoin() {
    keys.coinDoor = true;
    applyKeys();
    window.setTimeout(() => { keys.coinDoor = false; applyKeys(); }, 50);
    coinDoor.classList.add("coined");
    window.setTimeout(() => coinDoor.classList.remove("coined"), 180);
    screen.focus();
    audio.ensure().catch(() => {});
  }
  coinDoor.querySelectorAll("[data-coin]").forEach((el) => {
    el.addEventListener("click", insertCoin);
  });
  document.querySelectorAll("[data-start]").forEach((el) => {
    const code = el.getAttribute("data-start") === "2" ? "Digit2" : "Digit1";
    const down = (e) => {
      if (e.button != null && e.button !== 0) return;
      e.preventDefault();
      try { el.setPointerCapture(e.pointerId); } catch {}
      el.classList.add("pressed");
      keys[code] = true;
      applyKeys();
      screen.focus();
      audio.ensure().catch(() => {});
    };
    const up = () => {
      el.classList.remove("pressed");
      keys[code] = false;
      applyKeys();
    };
    el.addEventListener("pointerdown", down);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
  });

  const touch = initArcadeTouch({
    root: document.getElementById("touchControls"),
    stick: "4way",
    buttons: [{ code: "Space", label: "Jump", cls: "red" }],
    setKey: (code, down) => {
      keys[code] = down;
      applyKeys();
    },
  });

  window.addEventListener("keydown", (e) => {
    keys[e.code] = true;
    applyKeys();
    if (["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space"].includes(e.code)) e.preventDefault();
    audio.ensure().catch(() => {});
  });
  window.addEventListener("keyup", (e) => {
    keys[e.code] = false;
    applyKeys();
  });

  function blit() {
    const rgba = machine.frameBuffer();
    img.data.set(rgba);
    ctx.putImageData(img, 0, 0);
  }

  function setStatus(t) { status.textContent = t; }

  function readHiscore() {
    const b = [];
    for (let i = 0; i < HISCORE_LEN; i++) b.push(machine.ramByte(HISCORE_ADDR + i) & 0xff);
    for (let i = 0; i < RANK_LEN; i++) b.push(machine.ramByte(RANK_ADDR + i) & 0xff);
    return b;
  }

  function writeHiscore(bytes) {
    if (!bytes || bytes.length < HISCORE_LEN + RANK_LEN) return;
    for (let i = 0; i < HISCORE_LEN; i++) machine.setRamByte(HISCORE_ADDR + i, bytes[i] & 0xff);
    for (let i = 0; i < RANK_LEN; i++) machine.setRamByte(RANK_ADDR + i, bytes[HISCORE_LEN + i] & 0xff);
  }

  let program = null;

  function hiscoreIsFactory(b) {
    if (!program) return false;
    for (let i = 0; i < HISCORE_LEN; i++) if (b[i] !== program[HISCORE_ROM + i]) return false;
    for (let i = 0; i < RANK_LEN; i++) if (b[HISCORE_LEN + i] !== program[RANK_ROM + i]) return false;
    return true;
  }

  const hiscore = arcadeHiscore({
    store,
    len: HISCORE_LEN + RANK_LEN,
    active: () => usingUserRom,
    key: () => String(programCrc >>> 0),
    read: readHiscore,
    write: writeHiscore,
    isFactory: (b) => hiscoreIsFactory(b),
    ready: () => hiscoreIsFactory(readHiscore()),
  });

  async function resetHiscoreNow() {
    if (!usingUserRom) return;
    await hiscore.forget();
    machine.reset();
    applyKeys();
  }
  const syncHiscoreResetBtn = arcadeHiscoreReset(() => usingUserRom, resetHiscoreNow);

  const dips = arcadePrefs(DIP_KEY);
  function saveDips() {
    dips.save({
      lives: Number(dipLives.value),
      coinage: Number(dipCoinage.value),
      cabinet: Number(dipCabinet.value),
      bonus: Number(dipBonus.value),
    });
    applyKeys();
  }

  function loadDips() {
    const s = dips.load();
    if (s) {
      if (s.lives !== undefined) dipLives.value = String(s.lives);
      if (s.coinage !== undefined) dipCoinage.value = String(s.coinage);
      if (s.cabinet !== undefined) dipCabinet.value = String(s.cabinet);
      if (s.bonus !== undefined) dipBonus.value = String(s.bonus);
    }
    applyKeys();
  }

  [dipCoinage, dipLives, dipCabinet, dipBonus].forEach((el) => el.addEventListener("change", saveDips));
  loadDips();

  function applySet(set, label) {
    if (!set || !set.program || !set.chars || !set.objs || !set.proms || !set.sound)
      throw new Error("incomplete ROM set");
    machine.loadRomSet(set.program, set.chars, set.objs, set.proms, set.sound);
    program = set.program;
    usingUserRom = set.kind !== "hwtest";
    programCrc = arcadeCrc32(set.program);
    hiscore.rearm();
    syncHiscoreResetBtn();
    setStatus(label);
  }

  let hwtestCrcs = null;
  try { hwtestCrcs = await (await fetch("roms/crc.json")).json(); } catch {}

  function loadBuiltInRom() {
    machine.loadHwtest();
    program = null;
    usingUserRom = false;
    hiscore.rearm();
    syncHiscoreResetBtn();
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
  });

  if (TEST) {
    window.__test = {
      machine,
      touch,
      get status() { return status.textContent; },
      get usingUserRom() { return usingUserRom; },
      mute: document.getElementById("mute"),
      get muted() { return audio.isMuted(); },
      readHiscore,
      writeHiscore,
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
