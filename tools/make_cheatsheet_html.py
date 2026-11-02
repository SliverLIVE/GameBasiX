#!/usr/bin/env python3
"""Builds CHEATSHEET.html: every GameBasiX command, one row per command, with
its signature on the left and a short description on the right.

The source is ide/data/gbx-dataset.json, the same dataset the IDE uses for its
reference and completion. Usage:

    python tools/make_cheatsheet_html.py [--lang en|de] [--out CHEATSHEET.html]
"""
from __future__ import annotations

import argparse
import html
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATASET = ROOT / "ide" / "data" / "gbx-dataset.json"

# Order of the groups; categories that are missing in this list follow behind.
ORDER = [
    ("Fenster und Bildschirm", "Window and screen"),
    ("Zeichnen", "Drawing"),
    ("Bildpuffer und Text", "Screen buffer and text"),
    ("Sprites", "Sprites"),
    ("Eingabe", "Input"),
    ("Sound", "Sound"),
    ("Mathe", "Maths"),
    ("Strings", "Strings"),
    ("Dateien", "Files"),
    ("Arrays", "Arrays"),
    ("Arrays und Dictionaries", "Arrays and dictionaries"),
    ("Netzwerk", "Network"),
    ("Steam", "Steam"),
    ("Laufzeit und Debug", "Runtime and debug"),
    ("Sprache", "Language"),
    ("Konstanten", "Constants"),
]

KIND_LABEL = {
    "de": {
        "command": "Befehl",
        "function": "Funktion",
        "keyword": "Schl\u00fcsselwort",
        "constant": "Konstante",
    },
    "en": {
        "command": "command",
        "function": "function",
        "keyword": "keyword",
        "constant": "constant",
    },
}

TEXT = {
    "de": {
        "subtitle": "Befehle, Funktionen, Schl\u00fcsselw\u00f6rter und Konstanten - gruppiert nach Art",
        "generated": "erzeugt aus ide/data/gbx-dataset.json",
        "entries": "Eintr\u00e4ge",
        "jump": "Springe zu",
        "foot": "GameBasiX - BASIC f\u00fcr 2D-Spiele. Werte ohne Klammern sind Befehle, solche mit Klammern Funktionen.",
    },
    "en": {
        "subtitle": "Commands, functions, keywords and constants, grouped by category",
        "generated": "generated from ide/data/gbx-dataset.json",
        "entries": "entries",
        "jump": "Jump to",
        "foot": "GameBasiX - BASIC for 2D games. Names without brackets are commands, names with brackets are functions.",
    },
}


def shorten(text: str, limit: int = 130) -> str:
    """First paragraph, cut at a sentence boundary when it gets long."""
    text = (text or "").strip()
    if not text:
        return ""
    text = text.split("\n\n")[0].strip()
    if len(text) <= limit:
        return text
    cut = text.rfind(". ", 0, limit)
    if cut > 40:
        return text[: cut + 1]
    cut = text.rfind(" ", 0, limit)
    return text[: cut if cut > 40 else limit].rstrip(",;:-") + "\u2026"


def slug(name: str) -> str:
    ascii_name = name.translate(
        str.maketrans("\u00e4\u00f6\u00fc\u00df\u00c4\u00d6\u00dc", "aousAOU")
    )
    return re.sub(r"[^a-z0-9]+", "-", ascii_name.lower()).strip("-")


def collect(lang: str):
    data = json.loads(DATASET.read_text(encoding="utf-8"))
    groups: dict[str, list[dict]] = {}
    for entry in data["commands"]:
        if entry.get("kind") == "overview":
            continue
        groups.setdefault(entry.get("category") or "Language", []).append(entry)
    return groups


def build(lang: str) -> str:
    groups = collect(lang)
    labels = KIND_LABEL[lang]
    words = TEXT[lang]
    titles = dict(ORDER)
    order = [de for de, _ in ORDER if de in groups]
    order += [name for name in groups if name not in titles and groups[name]]

    total = sum(len(groups[name]) for name in order)
    single = lang == "de"
    title = "GameBasiX Cheatsheet" if single else "GameBasiX cheat sheet"

    parts: list[str] = []
    parts.append(
        f"""<!doctype html>
<html lang="{lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
  :root {{
    --bg: #0d1117;
    --panel: #141a22;
    --line: #222b36;
    --text: #e6ebf2;
    --muted: #8b99a8;
    --accent: #7ed184;
    --command: #8fd6ff;
    --function: #b8f0a8;
    --keyword: #d7c2ff;
    --constant: #ffd68a;
  }}
  * {{ box-sizing: border-box; }}
  html {{ scroll-behavior: smooth; }}
  body {{
    margin: 0;
    background:
      radial-gradient(900px 500px at 15% -10%, #1b2a22 0%, transparent 60%),
      radial-gradient(700px 400px at 95% 0%, #1a2431 0%, transparent 55%),
      var(--bg);
    color: var(--text);
    font: 15px/1.55 ui-sans-serif, "Segoe UI", system-ui, -apple-system, sans-serif;
    padding: 0 0 60px;
  }}
  .wrap {{ max-width: 1260px; margin: 0 auto; padding: 0 24px; }}

  header.head {{ padding: 32px 0 16px; }}
  h1 {{
    margin: 0;
    font-size: 30px;
    letter-spacing: -0.5px;
    font-weight: 700;
  }}
  h1 span {{ color: var(--accent); font-weight: 600; }}
  .sub {{ margin: 5px 0 0; color: var(--muted); font-size: 14px; }}
  .meta {{ margin: 8px 0 0; color: #6f7d8c; font-size: 12px; }}

  nav.toc {{
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-top: 16px;
    padding-top: 14px;
    border-top: 1px solid var(--line);
  }}
  nav.toc a {{
    display: inline-block;
    padding: 3px 9px;
    border: 1px solid var(--line);
    border-radius: 999px;
    color: #c3cdd9;
    text-decoration: none;
    font-size: 12px;
    background: #121821;
  }}
  nav.toc a:hover {{ border-color: var(--accent); color: var(--accent); }}

  section {{ margin-top: 22px; }}
  h2 {{
    position: sticky;
    top: 0;
    z-index: 2;
    margin: 0 0 5px;
    padding: 6px 2px 5px;
    font-size: 16px;
    letter-spacing: 0.2px;
    background: linear-gradient(180deg, var(--bg) 68%, rgba(13, 17, 23, 0));
    display: flex;
    align-items: baseline;
    gap: 12px;
  }}
  h2 .count {{ color: var(--muted); font-size: 12px; font-weight: 400; }}

  .rows {{ border: 1px solid var(--line); border-radius: 9px; overflow: hidden; background: var(--panel); }}
  .row {{
    display: grid;
    grid-template-columns: minmax(200px, 29%) 1fr;
    gap: 12px;
    padding: 3px 11px;
    border-bottom: 1px solid var(--line);
    align-items: baseline;
  }}
  .row:last-child {{ border-bottom: 0; }}
  .row:hover {{ background: #1a212b; }}
  code {{
    font: 12.5px/1.3 ui-monospace, "Cascadia Mono", Consolas, monospace;
    color: var(--command);
    word-break: break-word;
  }}
  .row[data-kind="function"] code {{ color: var(--function); }}
  .row[data-kind="keyword"] code {{ color: var(--keyword); }}
  .row[data-kind="constant"] code {{ color: var(--constant); }}
  .desc {{ margin: 0; color: #c4cfdb; font-size: 12.5px; line-height: 1.35; }}
  .kind {{
    margin-left: 7px;
    color: #4f5a67;
    font: 9.5px/1.3 ui-monospace, Consolas, monospace;
    text-transform: uppercase;
    letter-spacing: 0.6px;
  }}

  footer {{ margin-top: 30px; padding-top: 12px; border-top: 1px solid var(--line); color: #6f7d8c; font-size: 12px; }}

  @media (max-width: 760px) {{
    .row {{ grid-template-columns: 1fr; gap: 4px; }}
    h1 {{ font-size: 24px; }}
  }}
  @media print {{
    body {{ background: #fff; color: #111; padding: 0; }}
    nav.toc, .kind {{ display: none; }}
    h2 {{ position: static; background: none; color: #111; }}
    .rows {{ border-color: #ccc; background: #fff; }}
    .row {{ border-color: #ddd; }}
    .row:hover {{ background: #fff; }}
    code {{ color: #0b3d91; }}
    .desc {{ color: #222; }}
    .sub, .meta, footer {{ color: #555; }}
  }}
</style>
</head>
<body>
<header class="head wrap">
  <h1>GameBasiX <span>{"Cheatsheet" if single else "cheat sheet"}</span></h1>
  <p class="sub">{words["subtitle"]}</p>
  <p class="meta">{total} {words["entries"]} &middot; {words["generated"]}</p>
  <nav class="toc" aria-label="{words["jump"]}">
"""
    )
    for name in order:
        label = titles.get(name) or name
        parts.append(
            f'    <a href="#{slug(name)}">{html.escape(label)}'
            f' <span style="opacity:.55">{len(groups[name])}</span></a>\n'
        )
    parts.append("  </nav>\n</header>\n<main class=\"wrap\">\n")

    for name in order:
        label = titles.get(name) or name
        parts.append(f'<section id="{slug(name)}">\n')
        parts.append(
            f'  <h2>{html.escape(label)}'
            f'<span class="count">{len(groups[name])}</span></h2>\n'
        )
        parts.append('  <div class="rows">\n')
        for entry in sorted(groups[name], key=lambda e: e.get("name", "")):
            kind = entry.get("kind", "command")
            signature = entry.get("signatureEn") or entry.get("signature") or entry.get("name", "")
            description = entry.get("description") or ""
            if isinstance(description, dict):
                description = description.get(lang) or description.get("en") or ""
            description = shorten(str(description))
            if not description:
                description = "\u2013"
            parts.append(
                f'    <div class="row" data-kind="{html.escape(kind)}">'
                f'<div><code>{html.escape(signature)}</code>'
                f'<span class="kind">{labels.get(kind, kind)}</span></div>'
                f'<p class="desc">{html.escape(description)}</p></div>\n'
            )
        parts.append("  </div>\n</section>\n")

    parts.append(f'<footer>{words["foot"]}</footer>\n</main>\n</body>\n</html>\n')
    return "".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build CHEATSHEET.html")
    parser.add_argument("--lang", choices=["en", "de"], default="en")
    parser.add_argument("--out", default=str(ROOT / "CHEATSHEET.html"))
    args = parser.parse_args()

    page = build(args.lang)
    out = pathlib.Path(args.out)
    out.write_text(page, encoding="utf-8")
    rows = page.count('class="row"')
    print(f"{out} geschrieben: {rows} Befehle, {len(page) // 1024} KB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
