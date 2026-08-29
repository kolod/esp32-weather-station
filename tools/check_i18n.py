#!/usr/bin/env python3
"""i18n inventory consistency checker (spec 004-fix-web-i18n, SC-006).

Failures (exit 1):
  1. key sets differ between the four language packs
  2. a data-i18n / data-i18n-placeholder key in the HTML pages is missing
     from en.json
  3. a t('key') / tn('key') reference in the page scripts is missing from
     en.json
  4. a pack value is empty, or its {n} placeholders do not match en.json

Warnings (exit 0):
  - pack keys that no page or script references

Run from anywhere:
    python tools/check_i18n.py
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

WWW = Path(__file__).resolve().parent.parent / "components" / "web_server" / "www"
LANGS = ("en", "de", "fr", "uk")

HTML_FILES = (WWW / "portal" / "index.html", WWW / "mgmt" / "mgmt.html")
JS_FILES = (WWW / "portal" / "portal.js", WWW / "mgmt" / "mgmt.js",
            WWW / "common" / "i18n.js")

HTML_KEY_RE = re.compile(r'data-i18n(?:-placeholder)?="([^"]+)"')
JS_KEY_RE = re.compile(r"\bt[n]?\(\s*'([^']+)'")


def main():
    errors = []
    warnings = []

    packs = {}
    for lang in LANGS:
        path = WWW / "i18n" / f"{lang}.json"
        try:
            packs[lang] = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            errors.append(f"{path.name}: cannot load ({exc})")
            packs[lang] = {}

    en_keys = set(packs["en"])

    # 1. identical key sets across packs
    for lang in LANGS[1:]:
        keys = set(packs[lang])
        for key in sorted(en_keys - keys):
            errors.append(f"{lang}.json: missing key '{key}'")
        for key in sorted(keys - en_keys):
            errors.append(f"{lang}.json: extra key '{key}' (not in en.json)")

    # 2./3. every referenced key exists in en.json
    referenced = set()
    for path, regex in [(p, HTML_KEY_RE) for p in HTML_FILES] + \
                       [(p, JS_KEY_RE) for p in JS_FILES]:
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            errors.append(f"{path.name}: cannot read ({exc})")
            continue
        for key in regex.findall(text):
            referenced.add(key)
            if key not in en_keys:
                errors.append(f"{path.name}: references unknown key '{key}'")
        if regex is JS_KEY_RE:
            # Keys can also be picked at runtime (e.g. `t(key)` after a
            # ternary over quoted key names) — count any string literal
            # that names a pack key as a reference.
            for literal in re.findall(r"'([a-z][a-z0-9_]*)'", text):
                if literal in en_keys:
                    referenced.add(literal)

    # 4. no empty values; {n} placeholders consistent with en.json
    for lang in LANGS:
        for key, value in packs[lang].items():
            if not isinstance(value, str) or not value.strip():
                errors.append(f"{lang}.json: key '{key}' has an empty value")
            elif key in packs["en"] and isinstance(packs["en"][key], str):
                want = sorted(re.findall(r"\{[a-z]+\}", packs["en"][key]))
                have = sorted(re.findall(r"\{[a-z]+\}", value))
                if want != have:
                    errors.append(f"{lang}.json: key '{key}' placeholders "
                                  f"{have} do not match en.json {want}")

    # warnings: unused pack keys
    for key in sorted(en_keys - referenced):
        warnings.append(f"unused pack key: '{key}'")

    for msg in warnings:
        print(f"WARNING: {msg}")
    for msg in errors:
        print(f"ERROR: {msg}")
    print(f"check_i18n: {len(errors)} discrepancies, {len(warnings)} warnings "
          f"({len(en_keys)} keys x {len(LANGS)} languages)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
