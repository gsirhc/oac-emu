"use strict";
// Empty floppy images for the Disk library. Geometry from Microsoft FAT
// Specification (v1.03) §3.1 / ECMA-107 Table B.1.
const BLANK_FLOPPY = {
  360:  { size: 368640,  bps: 512, spc: 2, reserved: 1, fats: 2, root: 112, total: 720,  media: 0xFD, spf: 2, spt: 9,  heads: 2 },
  720:  { size: 737280,  bps: 512, spc: 2, reserved: 1, fats: 2, root: 112, total: 1440, media: 0xF9, spf: 3, spt: 9,  heads: 2 },
  1200: { size: 1228800, bps: 512, spc: 1, reserved: 1, fats: 2, root: 224, total: 2400, media: 0xF9, spf: 7, spt: 15, heads: 2 },
  1440: { size: 1474560, bps: 512, spc: 1, reserved: 1, fats: 2, root: 224, total: 2880, media: 0xF0, spf: 9, spt: 18, heads: 2 },
};

function blankFloppyLabel(formatK, formatted) {
  const size = formatK >= 1000 ? (formatK / 1000) + "MB" : formatK + "K";
  return formatted ? "blank " + size : "blank " + size + " (Unformatted)";
}

function floppyFormatK(byteLength) {
  for (const k of Object.keys(BLANK_FLOPPY)) {
    if (BLANK_FLOPPY[k].size === byteLength) return +k;
  }
  return 0;
}

function blankFloppySizes() {
  return Object.keys(BLANK_FLOPPY).map(Number).sort((a, b) => b - a);
}

// Root directory names from a FAT12 image, one per line (first 10). Directories end with \.
function floppyRootListing(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 512) return "Not a FAT disk.";
  if (bytes[510] !== 0x55 || bytes[511] !== 0xAA) return "Not a FAT disk.";
  const bps = bytes[11] | (bytes[12] << 8);
  const reserved = bytes[14] | (bytes[15] << 8);
  const fats = bytes[16];
  const root = bytes[17] | (bytes[18] << 8);
  const spf = bytes[22] | (bytes[23] << 8);
  if (!(bps >= 512 && fats > 0 && root > 0 && reserved > 0 && spf > 0)) return "Not a FAT disk.";
  const rootOff = (reserved + fats * spf) * bps;
  const names = [];
  let more = false;
  for (let i = 0; i < root; i++) {
    const o = rootOff + i * 32;
    if (o + 32 > bytes.length) break;
    if (bytes[o] === 0) break;
    if (bytes[o] === 0xE5) continue;
    const attr = bytes[o + 11];
    if (attr === 0x0F || (attr & 0x08)) continue;  // LFN or volume label
    let base = "";
    for (let j = 0; j < 8; j++) {
      const c = bytes[o + j];
      if (c === 0x20 || c === 0) break;
      if (c >= 32 && c < 127) base += String.fromCharCode(c);
    }
    let ext = "";
    for (let j = 8; j < 11; j++) {
      const c = bytes[o + j];
      if (c === 0x20 || c === 0) break;
      if (c >= 32 && c < 127) ext += String.fromCharCode(c);
    }
    if (!base) continue;
    if (names.length >= 10) {
      more = true;
      break;
    }
    const name = ext ? base + "." + ext : base;
    names.push(attr & 0x10 ? name + "\\" : name);
  }
  if (!names.length) return "(empty)";
  if (more) names.push("...");
  return names.join("\n");
}

// FAT12 volume label from the root entry (attr 0x08), else the BPB string.
function floppyVolumeLabel(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 512) return "";
  const decode = (u8) => {
    let s = "";
    for (let i = 0; i < u8.length; i++) {
      const c = u8[i];
      if (c === 0) break;
      if (c >= 32 && c < 127) s += String.fromCharCode(c);
    }
    s = s.trim();
    if (!s || /^NO\s*NAME$/i.test(s)) return "";
    return s;
  };
  const bps = bytes[11] | (bytes[12] << 8);
  const reserved = bytes[14] | (bytes[15] << 8);
  const fats = bytes[16];
  const root = bytes[17] | (bytes[18] << 8);
  const spf = bytes[22] | (bytes[23] << 8);
  if (bps >= 512 && fats > 0 && root > 0 && reserved > 0 && spf > 0) {
    const rootOff = (reserved + fats * spf) * bps;
    for (let i = 0; i < root; i++) {
      const o = rootOff + i * 32;
      if (o + 12 > bytes.length) break;
      if (bytes[o] === 0) break;
      if (bytes[o] === 0xE5) continue;
      if (bytes[o + 11] === 0x08) {
        const v = decode(bytes.subarray(o, o + 11));
        if (v) return v;
      }
    }
  }
  if (bytes[38] === 0x29) return decode(bytes.subarray(43, 54));
  return "";
}

function makeBlankFloppy(formatK, opts) {
  const g = BLANK_FLOPPY[formatK];
  if (!g) throw new Error("unknown floppy format " + formatK);
  const img = new Uint8Array(g.size);
  if (!(opts && opts.formatted)) return img;

  const view = new DataView(img.buffer);
  img[0] = 0xEB; img[1] = 0x3C; img[2] = 0x90;
  // OEM name "MSDOS5.0" is what FreeDOS FORMAT writes on these sizes.
  for (let i = 0; i < 8; i++) img[3 + i] = "MSDOS5.0".charCodeAt(i);
  view.setUint16(11, g.bps, true);
  img[13] = g.spc;
  view.setUint16(14, g.reserved, true);
  img[16] = g.fats;
  view.setUint16(17, g.root, true);
  view.setUint16(19, g.total, true);
  img[21] = g.media;
  view.setUint16(22, g.spf, true);
  view.setUint16(24, g.spt, true);
  view.setUint16(26, g.heads, true);
  view.setUint16(28, 0, true);
  view.setUint32(32, 0, true);
  img[36] = 0x00;
  img[37] = 0x00;
  img[38] = 0x29;
  view.setUint32(39, 0x12345678, true);
  const vol = "BLANK      ";
  for (let i = 0; i < 11; i++) img[43 + i] = vol.charCodeAt(i);
  const fs = "FAT12   ";
  for (let i = 0; i < 8; i++) img[54 + i] = fs.charCodeAt(i);
  img[510] = 0x55;
  img[511] = 0xAA;

  // First two FAT entries: media byte + EOC (0xFFF).
  const fatOff = g.reserved * g.bps;
  for (let f = 0; f < g.fats; f++) {
    const o = fatOff + f * g.spf * g.bps;
    img[o] = g.media;
    img[o + 1] = 0xFF;
    img[o + 2] = 0xFF;
  }
  // Root volume-label entry (attr 0x08); FreeDOS DIR reads this, not only the BPB string.
  const rootOff = (g.reserved + g.fats * g.spf) * g.bps;
  for (let i = 0; i < 11; i++) img[rootOff + i] = vol.charCodeAt(i);
  img[rootOff + 11] = 0x08;
  return img;
}
