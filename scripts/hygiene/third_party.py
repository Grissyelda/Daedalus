#!/usr/bin/env python3
"""The third-party list: third_party.json and the THIRD_PARTY.md made from it.

Usage:
  third_party.py write     regenerate THIRD_PARTY.md from third_party.json
  third_party.py check     fail if anything bundled isn't listed, a listed path
                           has gone, or THIRD_PARTY.md is out of date

What has to be listed: every folder in neo/libs and neo/extern, every font in
content/editor/fonts, and every font folder in content/newfonts. The hygiene
check (check_hygiene.py) runs the same check on every commit.
"""

import json
import os
import sys

JSON_NAME = "third_party.json"
MD_NAME = "THIRD_PARTY.md"


def load(root):
    with open(os.path.join(root, JSON_NAME), encoding="utf-8") as f:
        return json.load(f)


def required_paths(root):
    """Everything bundled that must appear in the list."""
    paths = []
    for folder in ("neo/libs", "neo/extern", "content/newfonts"):
        full = os.path.join(root, folder)
        if os.path.isdir(full):
            paths += [folder + "/" + name for name in sorted(os.listdir(full)) if os.path.isdir(os.path.join(full, name))]
    fonts = os.path.join(root, "content/editor/fonts")
    if os.path.isdir(fonts):
        paths += ["content/editor/fonts/" + name for name in sorted(os.listdir(fonts)) if name.lower().endswith((".ttf", ".otf"))]
    return paths


def render(data):
    lines = [
        "# Third-party components",
        "",
        "Daedalus is built on the Doom 3 BFG Edition GPL source and RBDOOM-3-BFG, and bundles the components below.",
        "Full license texts are in [LICENSE_EXCEPTIONS.md](LICENSE_EXCEPTIONS.md) and next to each component.",
        "",
        "This file is generated from `third_party.json` by `scripts/hygiene/third_party.py write`; edit the JSON, not this file.",
        "",
        "| Component | License | Used | Where |",
        "|---|---|---|---|",
    ]
    for c in data["components"]:
        source = c.get("source", "")
        first = source.split(",")[0].split(" ")[0] if source.startswith("http") else ""
        name = f"[{c['name']}]({first})" if first else c["name"]
        use = c.get("use", "")
        if c.get("notes"):
            use += ". " + c["notes"].rstrip(".")
        where = "<br>".join(f"`{p}`" for p in c["paths"])
        cells = [name, c["license"], use, where]
        lines.append("| " + " | ".join(cell.replace("|", "\\|") for cell in cells) + " |")
    lines.append("")
    return "\n".join(lines)


def check(root):
    """Problems with the list, as plain sentences (empty when all is well)."""
    problems = []
    try:
        data = load(root)
    except (OSError, ValueError) as e:
        return [f"{JSON_NAME} can't be read: {e}"]
    listed = [p.rstrip("/") for c in data["components"] for p in c["paths"]]
    for p in listed:
        if not os.path.exists(os.path.join(root, p)):
            problems.append(f"{JSON_NAME} lists {p}, which isn't in the repository any more")
    for p in required_paths(root):
        # named itself (or something inside it): the engine's own "neo" entry doesn't count
        if not any(l == p or l.startswith(p + "/") for l in listed):
            problems.append(f"{p} isn't in {JSON_NAME}: add it, with its license, then run scripts/hygiene/third_party.py write")
    md = os.path.join(root, MD_NAME)
    current = open(md, encoding="utf-8").read() if os.path.exists(md) else ""
    if current != render(data):
        problems.append(f"{MD_NAME} is out of date: run scripts/hygiene/third_party.py write")
    return problems


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    if len(sys.argv) != 2 or sys.argv[1] not in ("write", "check"):
        print(__doc__)
        return 2
    if sys.argv[1] == "write":
        with open(os.path.join(root, MD_NAME), "w", encoding="utf-8", newline="\n") as f:
            f.write(render(load(root)))
        print("wrote", MD_NAME)
        return 0
    problems = check(root)
    for p in problems:
        print("  - " + p)
    print("Third-party list: " + ("problems found." if problems else "every component is listed."))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
