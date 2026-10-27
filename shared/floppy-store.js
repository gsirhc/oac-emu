"use strict";
// Named floppy images in IndexedDB for the PC Disk library / dirty-eject save.
// opts: { db } database name, e.g. "ibmpcat-floppy" / "pc486-floppy".
function initFloppyStore(opts) {
  const DB_NAME = opts.db;
  const STORE = "floppies";
  let dbPromise = null;
  let blocked = false;

  function openDb() {
    if (blocked) return Promise.reject(new DOMException("blocked", "SecurityError"));
    if (!dbPromise) {
      dbPromise = new Promise((resolve, reject) => {
        let req;
        try {
          req = indexedDB.open(DB_NAME, 1);
        } catch (e) {
          blocked = true;
          reject(e);
          return;
        }
        req.onupgradeneeded = () => req.result.createObjectStore(STORE, { keyPath: "id" });
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => {
          blocked = true;
          reject(req.error);
        };
      });
      dbPromise.catch(() => { dbPromise = null; });
    }
    return dbPromise;
  }

  function reqP(req) {
    return new Promise((resolve, reject) => {
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error);
    });
  }

  function newId() {
    return Date.now().toString(36) + "-" + Math.random().toString(36).slice(2, 8);
  }

  async function list() {
    const db = await openDb();
    const rows = await reqP(db.transaction(STORE, "readonly").objectStore(STORE).getAll());
    rows.sort((a, b) => (b.savedAt || 0) - (a.savedAt || 0));
    return rows.map((r) => ({ id: r.id, name: r.name, formatK: r.formatK, savedAt: r.savedAt }));
  }

  async function findByName(name) {
    const trimmed = String(name || "").trim();
    if (!trimmed) return null;
    const db = await openDb();
    const rows = await reqP(db.transaction(STORE, "readonly").objectStore(STORE).getAll());
    const key = trimmed.toLowerCase();
    const hit = rows.find((r) => String(r.name || "").trim().toLowerCase() === key);
    return hit ? { id: hit.id, name: hit.name, formatK: hit.formatK, savedAt: hit.savedAt } : null;
  }

  // Upserts by name (case-insensitive). Same name replaces the existing record.
  async function put(name, formatK, bytes) {
    const trimmed = String(name || "").trim();
    if (!trimmed) throw new Error("name required");
    const db = await openDb();
    const existing = await findByName(trimmed);
    const copy = bytes instanceof Uint8Array ? bytes.slice() : new Uint8Array(bytes);
    const rec = {
      id: existing ? existing.id : newId(),
      name: trimmed,
      formatK: formatK | 0,
      bytes: copy.buffer,
      savedAt: Date.now(),
    };
    await reqP(db.transaction(STORE, "readwrite").objectStore(STORE).put(rec));
    return { id: rec.id, name: rec.name, formatK: rec.formatK, savedAt: rec.savedAt };
  }

  async function get(id) {
    const db = await openDb();
    const rec = await reqP(db.transaction(STORE, "readonly").objectStore(STORE).get(id));
    if (!rec) return null;
    return {
      id: rec.id,
      name: rec.name,
      formatK: rec.formatK,
      savedAt: rec.savedAt,
      bytes: new Uint8Array(rec.bytes),
    };
  }

  async function remove(id) {
    const db = await openDb();
    await reqP(db.transaction(STORE, "readwrite").objectStore(STORE).delete(id));
  }

  async function probe() {
    try {
      await openDb();
      return true;
    } catch {
      blocked = true;
      return false;
    }
  }

  return {
    list,
    findByName,
    put,
    get,
    remove,
    probe,
    get blocked() { return blocked; },
  };
}

// Dirty floppy prompt used for bay Save, eject, and insert-over.
// Same text and buttons every time. Resolves to
// "browser" | "download" | "discard" | "cancel".
function promptFloppyDirty(opts) {
  const canBrowser = !!(opts && opts.canSaveBrowser);
  return new Promise((resolve) => {
    let settled = false;
    const finish = (v) => {
      if (settled) return;
      settled = true;
      dlg.close();
      dlg.remove();
      resolve(v);
    };
    const dlg = document.createElement("dialog");
    dlg.id = "floppyDirtyDialog";
    dlg.className = "site-dialog";
    dlg.innerHTML =
      "<h2>Disk has changes</h2>" +
      "<p>Would you like to save the changes?</p>" +
      '<div class="row floppy-dirty-actions"></div>';
    const actions = dlg.querySelector(".floppy-dirty-actions");
    const add = (label, value, primary) => {
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = label;
      if (primary) b.className = "primary";
      b.addEventListener("click", () => finish(value));
      actions.appendChild(b);
    };
    if (canBrowser) add("Save in this browser", "browser", true);
    add("Download", "download", !canBrowser);
    add("Eject without saving", "discard");
    add("Cancel", "cancel");
    document.body.appendChild(dlg);
    dlg.addEventListener("cancel", (e) => { e.preventDefault(); finish("cancel"); });
    dlg.addEventListener("click", (e) => { if (e.target === dlg) finish("cancel"); });
    dlg.showModal();
  });
}

// Name for a browser save. suggested is the FAT volume label, else the bay label.
// Resolves to a trimmed non-empty string, or null if cancelled.
function promptFloppySaveName(opts) {
  const suggested = String((opts && opts.suggested) || "").trim();
  return new Promise((resolve) => {
    let settled = false;
    const finish = (v) => {
      if (settled) return;
      settled = true;
      dlg.close();
      dlg.remove();
      resolve(v);
    };
    const dlg = document.createElement("dialog");
    dlg.id = "floppySaveNameDialog";
    dlg.className = "site-dialog";
    dlg.innerHTML =
      "<h2>Save in this browser</h2>" +
      "<p>Provide a name for the disk. It will be saved to the Disk Library.</p>" +
      '<label class="floppy-save-name-label">Name' +
      '<input type="text" class="floppy-save-name" maxlength="64" required autocomplete="off" spellcheck="false">' +
      "</label>" +
      '<div class="row floppy-dirty-actions">' +
      '<button type="button" class="floppy-save-cancel">Cancel</button>' +
      '<button type="button" class="primary floppy-save-ok" disabled>Save</button>' +
      "</div>";
    const input = dlg.querySelector(".floppy-save-name");
    const ok = dlg.querySelector(".floppy-save-ok");
    const sync = () => {
      ok.disabled = !input.value.trim();
    };
    input.value = suggested;
    sync();
    input.addEventListener("input", sync);
    ok.addEventListener("click", () => {
      const name = input.value.trim();
      if (!name) return;
      finish(name);
    });
    dlg.querySelector(".floppy-save-cancel").addEventListener("click", () => finish(null));
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        if (!ok.disabled) ok.click();
      }
    });
    document.body.appendChild(dlg);
    dlg.addEventListener("cancel", (e) => { e.preventDefault(); finish(null); });
    dlg.addEventListener("click", (e) => { if (e.target === dlg) finish(null); });
    dlg.showModal();
    input.focus();
    input.select();
  });
}

// Replace an existing Saved name. Resolves true to overwrite, false to cancel.
function promptFloppyOverwrite(name) {
  const label = String(name || "").trim() || "disk";
  return new Promise((resolve) => {
    let settled = false;
    const finish = (v) => {
      if (settled) return;
      settled = true;
      dlg.close();
      dlg.remove();
      resolve(v);
    };
    const dlg = document.createElement("dialog");
    dlg.id = "floppyOverwriteDialog";
    dlg.className = "site-dialog";
    dlg.innerHTML =
      "<h2>Replace disk?</h2>" +
      "<p></p>" +
      '<div class="row floppy-dirty-actions">' +
      '<button type="button" class="floppy-overwrite-cancel">Cancel</button>' +
      '<button type="button" class="primary floppy-overwrite-ok">Replace</button>' +
      "</div>";
    dlg.querySelector("p").textContent =
      "A disk named \u201c" + label + "\u201d is already saved in this browser. Replace it?";
    dlg.querySelector(".floppy-overwrite-ok").addEventListener("click", () => finish(true));
    dlg.querySelector(".floppy-overwrite-cancel").addEventListener("click", () => finish(false));
    document.body.appendChild(dlg);
    dlg.addEventListener("cancel", (e) => { e.preventDefault(); finish(false); });
    dlg.addEventListener("click", (e) => { if (e.target === dlg) finish(false); });
    dlg.showModal();
  });
}

// IndexedDB put failed after the visitor chose Save in this browser.
function promptFloppySaveFailed() {
  return new Promise((resolve) => {
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      dlg.close();
      dlg.remove();
      resolve();
    };
    const dlg = document.createElement("dialog");
    dlg.id = "floppySaveFailedDialog";
    dlg.className = "site-dialog";
    dlg.innerHTML =
      "<h2>Couldn't save</h2>" +
      "<p>This browser wouldn't store the disk. Try Download instead, or free some storage and try again.</p>" +
      '<div class="row floppy-dirty-actions">' +
      '<button type="button" class="primary floppy-save-failed-ok">OK</button>' +
      "</div>";
    dlg.querySelector(".floppy-save-failed-ok").addEventListener("click", finish);
    document.body.appendChild(dlg);
    dlg.addEventListener("cancel", (e) => { e.preventDefault(); finish(); });
    dlg.addEventListener("click", (e) => { if (e.target === dlg) finish(); });
    dlg.showModal();
  });
}
