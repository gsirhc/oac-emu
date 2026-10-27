"use strict";
// Disk library dialog for the PC machines, fed by disks/library/library.json.
// opts: {
//   button, drives: [{ id, name, accepts(formatK) }],
//   insert(driveId, label, bytes, meta?) -> Promise,
//     meta.idbName marks a disk loaded from Saved in this browser,
//   blank: true to offer Blank diskette rows,
//   floppyStore: initFloppyStore() result (optional; Saved section + browser save),
// }
function initDiskLibrary(opts) {
  const BASE = "disks/library/";
  const dlg = document.createElement("dialog");
  dlg.id = "libraryDialog";
  dlg.innerHTML =
    '<h2>Disk library</h2>' +
    '<div class="lib-body">' +
    '<div class="lib-list" role="listbox" aria-label="Disks"></div>' +
    '<div class="lib-detail">' +
    '<h3 class="lib-title"></h3>' +
    '<p class="lib-meta"></p>' +
    '<p class="lib-desc"></p>' +
    '<div class="lib-disks"></div>' +
    '<pre class="lib-help"></pre>' +
    '<p class="lib-license"></p>' +
    '</div></div>' +
    '<div class="row"><span class="lib-status"></span><button type="button" class="lib-close">Close</button></div>';
  document.body.appendChild(dlg);

  const $ = (sel) => dlg.querySelector(sel);
  const list = $(".lib-list");
  const status = $(".lib-status");
  let catalog = null;
  let saved = [];
  let storeOk = false;
  let current = null; // { kind: "blank"|"saved"|"catalog", ... }

  const formatOf = (entry) => (entry.build && entry.build.format) || entry.format || 1440;
  const metaLine = (e) => [e.year, e.publisher, e.needs && "Needs " + e.needs].filter(Boolean).join("  ·  ");
  const sizeLabel = (k) => (k >= 1000 ? k / 1000 + "MB" : k + "K");

  function clearDetail() {
    $(".lib-title").textContent = "";
    $(".lib-meta").textContent = "";
    $(".lib-desc").textContent = "";
    $(".lib-help").textContent = "";
    $(".lib-license").textContent = "";
    $(".lib-disks").innerHTML = "";
  }

  function setSelected(el) {
    list.querySelectorAll(".lib-item").forEach((n) => n.setAttribute("aria-selected", String(n === el)));
  }

  function insertButtons(row, formatK, onInsert, file) {
    for (const d of opts.drives) {
      if (!d.accepts(formatK)) continue;
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = "Insert in " + d.name;
      if (file) {
        b.dataset.file = file;
        b.dataset.drive = String(d.id);
      }
      b.addEventListener("click", () => onInsert(d));
      row.appendChild(b);
    }
  }

  function showBlank() {
    current = { kind: "blank" };
    clearDetail();
    $(".lib-title").textContent = "Blank diskette";
    $(".lib-desc").textContent =
      "An empty disk for the drive. Unformatted needs FORMAT in DOS. Formatted is an empty FAT12 volume.";
    $(".lib-help").textContent = "";
    const disks = $(".lib-disks");
    const sizes = blankFloppySizes().filter((k) => opts.drives.some((d) => d.accepts(k)));
    for (const k of sizes) {
      for (const formatted of [false, true]) {
        const row = document.createElement("div");
        row.className = "lib-disk";
        const name = document.createElement("span");
        name.textContent = blankFloppyLabel(k, formatted);
        row.appendChild(name);
        insertButtons(row, k, async (drive) => {
          status.textContent = "Making disk\u2026";
          try {
            const bytes = makeBlankFloppy(k, { formatted });
            const label = blankFloppyLabel(k, formatted);
            await opts.insert(drive.id, label, bytes);
            status.textContent = label + " is in " + drive.name;
          } catch (err) {
            console.error("disk library:", err);
            status.textContent = "Couldn't make that disk. See the console.";
          }
        });
        disks.appendChild(row);
      }
    }
    status.textContent = "";
  }

  async function showSaved(entry) {
    current = { kind: "saved", entry };
    clearDetail();
    $(".lib-title").textContent = entry.name;
    $(".lib-meta").textContent = sizeLabel(entry.formatK) + "  ·  saved in this browser";
    $(".lib-desc").textContent = "A floppy you saved from this browser. It stays here until you remove it.";
    $(".lib-help").textContent = "Reading\u2026";
    const disks = $(".lib-disks");
    const row = document.createElement("div");
    row.className = "lib-disk";
    const name = document.createElement("span");
    name.textContent = sizeLabel(entry.formatK) + " diskette";
    row.appendChild(name);
    insertButtons(row, entry.formatK, async (drive) => {
      status.textContent = "Loading\u2026";
      try {
        const rec = await opts.floppyStore.get(entry.id);
        if (!rec) throw new Error("missing");
        await opts.insert(drive.id, rec.name, rec.bytes, { idbName: rec.name });
        status.textContent = "It is in " + drive.name;
      } catch (err) {
        console.error("disk library:", err);
        status.textContent = "Couldn't load that disk. See the console.";
      }
    });
    const rm = document.createElement("button");
    rm.type = "button";
    rm.textContent = "Remove";
    rm.addEventListener("click", async () => {
      try {
        await opts.floppyStore.remove(entry.id);
        status.textContent = "Removed.";
        await rebuildList();
      } catch (err) {
        console.error("disk library:", err);
        status.textContent = "Couldn't remove it. See the console.";
      }
    });
    row.appendChild(rm);
    disks.appendChild(row);
    status.textContent = "";
    try {
      const rec = await opts.floppyStore.get(entry.id);
      if (current && current.kind === "saved" && current.entry.id === entry.id) {
        $(".lib-help").textContent = rec ? floppyRootListing(rec.bytes) : "Couldn't read that disk.";
      }
    } catch (err) {
      console.error("disk library:", err);
      if (current && current.kind === "saved" && current.entry.id === entry.id) {
        $(".lib-help").textContent = "Couldn't read that disk.";
      }
    }
  }

  function showCatalog(i) {
    const entry = catalog[i];
    current = { kind: "catalog", entry, index: i };
    clearDetail();
    $(".lib-title").textContent = entry.title;
    $(".lib-meta").textContent = metaLine(entry);
    $(".lib-desc").textContent = entry.description || "";
    $(".lib-help").textContent = entry.help || "";
    $(".lib-license").textContent = entry.license || "";
    const disks = $(".lib-disks");
    const fmt = formatOf(entry);
    for (const img of entry.images) {
      const row = document.createElement("div");
      row.className = "lib-disk";
      const name = document.createElement("span");
      name.textContent = entry.images.length > 1 ? img.label
        : sizeLabel(fmt) + " diskette";
      row.appendChild(name);
      insertButtons(row, fmt, (drive) => insertCatalog(entry, img, drive), img.file);
      disks.appendChild(row);
    }
    status.textContent = "";
  }

  async function insertCatalog(entry, img, drive) {
    status.textContent = "Fetching\u2026";
    try {
      const r = await fetch(BASE + img.file);
      if (!r.ok) throw new Error("HTTP " + r.status);
      const bytes = new Uint8Array(await r.arrayBuffer());
      const label = entry.images.length > 1 ? entry.title + ", " + img.label.toLowerCase() : entry.title;
      await opts.insert(drive.id, label, bytes);
      status.textContent = (entry.images.length > 1 ? img.label : "It") + " is in " + drive.name;
    } catch (err) {
      console.error("disk library:", err);
      status.textContent = "Couldn't fetch that disk. See the console.";
    }
  }

  function addItem(title, year, onSelect) {
    const item = document.createElement("button");
    item.type = "button";
    item.className = "lib-item";
    item.setAttribute("role", "option");
    item.innerHTML = '<span class="lib-item-title"></span><span class="lib-item-year"></span>';
    item.querySelector(".lib-item-title").textContent = title;
    item.querySelector(".lib-item-year").textContent = year || "";
    item.addEventListener("click", () => {
      setSelected(item);
      onSelect();
    });
    list.appendChild(item);
    return item;
  }

  function addHeading(text) {
    const h = document.createElement("div");
    h.className = "lib-heading";
    h.textContent = text;
    list.appendChild(h);
  }

  async function rebuildList() {
    list.innerHTML = "";
    clearDetail();
    const items = [];

    if (opts.blank) {
      addHeading("New");
      items.push(addItem("Blank diskette", "", showBlank));
    }

    if (opts.floppyStore && storeOk) {
      try {
        saved = await opts.floppyStore.list();
      } catch (err) {
        console.warn("disk library:", err);
        saved = [];
        storeOk = false;
      }
      if (saved.length) {
        addHeading("Saved in this browser");
        for (const s of saved) {
          items.push(addItem(s.name, sizeLabel(s.formatK), () => showSaved(s)));
        }
      }
    }

    if (catalog === null) {
      try {
        const r = await fetch(BASE + "library.json");
        if (!r.ok) throw new Error("HTTP " + r.status);
        catalog = (await r.json()).disks || [];
      } catch (err) {
        console.warn("disk library:", err);
        catalog = [];
      }
    }
    if (catalog.length) {
      addHeading("Library");
      catalog.forEach((e, i) => {
        items.push(addItem(e.title, e.year || "", () => showCatalog(i)));
      });
    } else if (!opts.blank && !(opts.floppyStore && storeOk && saved.length)) {
      list.textContent = "The library isn't installed on this server.";
      return;
    }

    if (items.length) items[0].click();
  }

  opts.button.addEventListener("click", async () => {
    dlg.showModal();
    if (opts.floppyStore && !storeOk && !opts.floppyStore.blocked) {
      storeOk = await opts.floppyStore.probe();
    }
    await rebuildList();
  });
  $(".lib-close").addEventListener("click", () => dlg.close());
  dlg.addEventListener("click", (e) => { if (e.target === dlg) dlg.close(); });
}
