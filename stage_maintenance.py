#!/usr/bin/env python3
"""Stage the kill-switch site: maintenance.html as index.html and 404.html, plus only what it loads."""
import argparse
import html
import os
import re
import shutil
import sys

from check_site import HTML_REF, check

CSS_URL = re.compile(r"""url\(\s*["']?(?!data:)([^"')]+)""")
ROOT = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(ROOT, "maintenance.html")


def set_text(page, elem_id, text):
    pat = re.compile(r'(<p\b[^>]*\bid="%s"[^>]*>)(.*?)(</p>)' % elem_id, re.S)
    page, n = pat.subn(lambda m: m.group(1) + html.escape(text) + m.group(3), page)
    if n != 1:
        sys.exit(f"stage_maintenance: #{elem_id} not found once in maintenance.html")
    return page


def stage(out, title=None, message=None):
    page = open(PAGE, encoding="utf-8").read()
    if title:
        page = set_text(page, "downTitle", title)
    if message:
        page = set_text(page, "downText", message)
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)
    copied = []
    for ref in HTML_REF.findall(page):
        ref = ref.split("#")[0].split("?")[0]
        if not ref or ref == "/" or re.match(r"^[a-z]+:|^//", ref, re.I):
            continue
        refs = [ref]
        if ref.endswith(".css"):
            css = open(os.path.join(ROOT, ref), encoding="utf-8").read()
            refs += [os.path.normpath(os.path.join(os.path.dirname(ref), u)) for u in CSS_URL.findall(css)]
        for r in refs:
            dest = os.path.join(out, r)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copyfile(os.path.join(ROOT, r), dest)
            copied.append(r)
    for name in ("index.html", "404.html"):
        open(os.path.join(out, name), "w", encoding="utf-8").write(page)
    errs = check(out)
    if errs:
        sys.exit("\n".join("stage_maintenance: " + e for e in errs))


def selftest():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "site")
        stage(out, "Gone <fishing>", "Back & soon")
        page = open(os.path.join(out, "404.html"), encoding="utf-8").read()
        assert "Gone &lt;fishing&gt;" in page and "Back &amp; soon" in page, "message not escaped"
        assert open(os.path.join(out, "index.html"), encoding="utf-8").read() == page
        staged = {os.path.relpath(os.path.join(r, f), out) for r, _, fs in os.walk(out) for f in fs}
        assert not any(p.endswith((".wasm", ".img", ".gz")) for p in staged), staged
        assert {"shared/theme-picker.js", "shared/theme.css", "shared/footer.js"} <= staged, staged
        stage(out)
        page = open(os.path.join(out, "index.html"), encoding="utf-8").read()
        assert ">Out of Order<" in page, "default title"


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out", nargs="?")
    ap.add_argument("--title")
    ap.add_argument("--message")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        selftest()
    elif a.out:
        stage(a.out, a.title, a.message)
    else:
        ap.error("out is required")
