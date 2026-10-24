"""Fails if a staged site carries dev files, a test-only CPU speed path, a dangling reference, a page without analytics, a font without its license, or tops 1 GB."""
import json
import os
import re
import sys

DEV_NAMES = {"Makefile", "__pycache__", "node_modules", "tests", ".DS_Store"}
DEV_EXT = (".py", ".pyc", ".sh", ".md", ".h", ".cpp", ".ts")
FAST = re.compile(r"""\.(?:get|has)\(\s*["']fast["']\s*\)""")
HTML_REF = re.compile(r"""\b(?:src|href)\s*=\s*["']([^"']*)["']""")
BASE_HREF = re.compile(r"""<base\s+[^>]*\bhref\s*=\s*["']([^"']*)["']""", re.I)
JS_REF = re.compile(r"""(?:fetch|importScripts|locateFile)\(\s*["']([^"'`?#]+)|new Worker\([^)]*?["']([^"'?#]+\.js)""")
PAGES_LIMIT = 10**9  # GitHub Pages: "may be no larger than 1 GB"
MEDIA_REF = re.compile(r"""["']((?:disks|roms|tapes|examples|assets)/[\w./-]+\.[A-Za-z0-9]+)["']""")


def dev_file(name):
    return name in DEV_NAMES or name.startswith("package") and name.endswith(".json") or name.endswith(DEV_EXT)


def check(site, limit=PAGES_LIMIT):
    site = os.path.abspath(site)
    if not os.path.isdir(site):
        return [f"{site} is not a directory"]
    errors = []

    def resolve(base, ref, where):
        path = os.path.normpath(os.path.join(base, ref))
        if not path.startswith(site + os.sep) and path != site:
            errors.append(f"{where}: {ref} leaves the site")
        elif ref.endswith("/"):
            if not os.path.isfile(os.path.join(path, "index.html")) and not os.path.isdir(path):
                errors.append(f"{where}: {ref} missing")
        elif not os.path.exists(path):
            errors.append(f"{where}: {ref} missing")

    total = 0
    for root, dirs, files in os.walk(site):
        # OFL 1.1: the license travels with the font files
        if any(f.endswith(".woff2") for f in files) and not any(f.endswith(".txt") for f in files):
            errors.append(f"{os.path.relpath(root, site)}: fonts without a license file")
        # LGPL 2.1 §1: the license text goes with the Bochs and VGA BIOS binaries
        if {"BIOS-bochs-legacy", "VGABIOS-lgpl-latest.bin"} & set(files) and "LGPL-2.1.txt" not in files:
            errors.append(f"{os.path.relpath(root, site)}: BIOS without LGPL-2.1.txt")
        for name in dirs + files:
            if dev_file(name):
                errors.append(f"dev file staged: {os.path.relpath(os.path.join(root, name), site)}")
        for name in files:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, site)
            total += os.path.getsize(full)
            if name.endswith(".html"):
                text = open(full, encoding="utf-8").read()
                if "footer.js" in text and "analytics.js" not in text:
                    errors.append(f"{rel}: page without analytics.js")
                base = root
                m = BASE_HREF.search(text)
                if m:
                    href = m.group(1).split("#")[0].split("?")[0] or "."
                    # root-absolute base: a page Pages serves at any depth (404.html)
                    base = os.path.normpath(os.path.join(site, href.lstrip("/")) if href.startswith("/") else os.path.join(root, href))
                for ref in HTML_REF.findall(text):
                    ref = ref.split("#")[0].split("?")[0]
                    if not ref or re.match(r"^[a-z]+:|^//|^/", ref, re.I):
                        continue
                    # The <base href> itself is resolved from the page dir, not from base.
                    resolve_from = root if m and ref == m.group(1).split("#")[0].split("?")[0] else base
                    resolve(resolve_from, ref, rel)
            elif name.endswith(".js"):
                text = open(full, encoding="utf-8", errors="replace").read()
                if FAST.search(text):
                    errors.append(f"{rel}: test-only CPU multiplier staged")
                for a, b in JS_REF.findall(text):
                    resolve(root, a or b, rel)
                for ref in MEDIA_REF.findall(text):
                    resolve(root, ref, rel)
            elif name.endswith(".json"):
                try:
                    data = json.load(open(full, encoding="utf-8"))
                except ValueError:
                    continue
                stack = [data]
                while stack:
                    v = stack.pop()
                    if isinstance(v, dict):
                        f = v.get("file")
                        if isinstance(f, str):
                            resolve(root, f, rel)
                        stack.extend(v.values())
                    elif isinstance(v, list):
                        stack.extend(v)
    if total > limit:
        errors.append(f"site is {total:,} bytes, over the {limit:,} host limit")
    return errors


def selftest():
    import tempfile
    cases = {
        "": None,
        "rm m/w.js": "w.js missing",
        "rm m/r/a.bin": "a.bin missing",
        "rm m/d/x.dsk": "x.dsk missing",
        "rm m/s.css": "s.css missing",
        "rm m/m.wasm": "m.wasm missing",
        "rm m/disks/hdd.img.gz": "hdd.img.gz missing",
        "rm img.png": "img.png missing",
        "fast": "test-only CPU multiplier",
        "dev": "dev file staged",
        "big": "over the",
        "nostats": "without analytics.js",
        "rm img.png via /": "img.png missing",
        "rm m/f/OFL.txt": "fonts without a license file",
        "rm m/r/LGPL-2.1.txt": "BIOS without LGPL-2.1.txt",
    }
    for case, want in cases.items():
        with tempfile.TemporaryDirectory() as site:
            files = {
                "index.html": '<img src="img.png"><a href="m/">m</a><a href="https://x/">x</a>',
                "img.png": "",
                "m/index.html": '<link href="s.css"><script src="app.js"></script>',
                "m/s.css": "",
                "m/app.js": 'fetch("r/a.bin"); fetch("d/" + n); new Worker(p ? "w.js?x" : "w.js"); const f = "disks/hdd.img.gz";',
                "m/m.js": 'locateFile("m.wasm")',
                "m/m.wasm": "",
                "m/w.js": "",
                "m/r/a.bin": "",
                "m/r/BIOS-bochs-legacy": "",
                "m/r/LGPL-2.1.txt": "",
                "m/d/list.json": '{"disks": [{"file": "x.dsk"}]}',
                "m/d/x.dsk": "",
                "m/disks/hdd.img.gz": "",
                "m/f/a.woff2": "",
                "m/f/OFL.txt": "",
                # Shared entry under a subdir: assets resolve via <base href="../">.
                "m/sub/index.html": '<base href="../"><script src="app.js"></script><link href="s.css">',
                "404.html": '<base href="/"><img src="img.png"><link href="m/s.css">',
            }
            if case == "rm img.png via /":
                del files["img.png"]
                files["index.html"] = ""
            elif case.startswith("rm "):
                del files[case[3:]]
            elif case == "fast":
                files["m/app.js"] += ' q.get("fast");'
            elif case == "dev":
                files["m/notes.md"] = ""
            elif case == "nostats":
                files["m/index.html"] += '<script src="footer.js"></script>'
                files["m/footer.js"] = ""
            elif case == "big":
                files["m/r/a.bin"] = "x" * 2000
            for rel, text in files.items():
                os.makedirs(os.path.dirname(os.path.join(site, rel)), exist_ok=True)
                open(os.path.join(site, rel), "w").write(text)
            errs = check(site, limit=1000)
            ok = not errs if want is None else any(want in e for e in errs)
            if not ok:
                sys.exit(f"check_site selftest {case or 'clean'}: {errs}")


if __name__ == "__main__":
    if sys.argv[1] == "--selftest":
        selftest()
        sys.exit(0)
    errs = check(sys.argv[1])
    for e in errs:
        print("check_site:", e, file=sys.stderr)
    sys.exit(1 if errs else 0)
