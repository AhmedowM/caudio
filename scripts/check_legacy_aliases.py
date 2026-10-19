#!/usr/bin/env python3
"""Guard: reject new uses of the deprecated caudio::db:: / caudio::engine::
spellings (moved types in include/caudio/legacy_aliases.hpp, Track JSON
helpers in include/caudio/db/json.hpp).

Every moved type has a canonical `caudio::types::` spelling and the Track
codec has `caudio::utils::`; the old qualified spellings only exist as
deprecated shims until v2.0.0 (U-6c). New code (and new docs examples in
headers) must use the canonical spelling directly. Comments and string
literals are ignored, and the deprecation declarations themselves are
excluded, so only real uses are reported.

Exit 0 when clean, 1 when any offending spelling is found.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

NAMES = {
    "db": (
        "Bookmark",
        "HistoryEntry",
        "Playlist",
        "QueueItem",
        "Track",
        "trackFromJson",
        "trackToJson",
    ),
    "engine": ("PlaybackState", "RepeatMode"),
}

EXCLUDE = {
    (ROOT / "include/caudio/legacy_aliases.hpp").resolve(),
    # Re-exports the deprecated db:: forwarders so `import caudio.db:json`
    # keeps working during the deprecation window.
    (ROOT / "modules/caudio/db/json.cppm").resolve(),
}
EXT = {".hpp", ".hh", ".h", ".cpp", ".cc", ".cxx", ".cppm"}
SCAN_DIRS = ("include", "src", "cli", "tests", "examples", "modules")

PATTERNS = [
    re.compile(r"\bcaudio::db::(" + "|".join(NAMES["db"]) + r")\b"),
    re.compile(r"\bcaudio::engine::(" + "|".join(NAMES["engine"]) + r")\b"),
]


def mask(text: str) -> str:
    """Blank out comments and string/char literals (keeping newlines) so the
    regexes only see code. Length and line breaks are preserved."""
    out = list(text)
    L = len(text)

    def blank(a: int, b: int) -> None:
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    i = 0
    while i < L:
        c = text[i]
        if c == "/" and i + 1 < L and text[i + 1] == "/":
            j = text.find("\n", i)
            j = L if j == -1 else j
            blank(i, j)
            i = j
            continue
        if c == "/" and i + 1 < L and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = L if j == -1 else j + 2
            blank(i, j)
            i = j
            continue
        if c == "R" and i + 1 < L and text[i + 1] == '"':
            p = text.find("(", i + 2)
            if p != -1:
                delim = text[i + 2:p]
                term = ")" + delim + '"'
                e = text.find(term, p + 1)
                e = L if e == -1 else e + len(term)
                blank(i, e)
                i = e
                continue
        if c == '"':
            j = i + 1
            while j < L:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            blank(i, j)
            i = j
            continue
        if c == "'":
            j = i + 1
            while j < L:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "'":
                    j += 1
                    break
                j += 1
            blank(i, j)
            i = j
            continue
        i += 1
    return "".join(out)


def main() -> int:
    files = []
    for d in SCAN_DIRS:
        base = ROOT / d
        if base.is_dir():
            files += [p for p in base.rglob("*") if p.suffix in EXT and p.is_file()]

    offenders = []
    for p in sorted(set(files)):
        if p.resolve() in EXCLUDE:
            continue
        try:
            text = p.read_bytes().decode("utf-8")
        except UnicodeDecodeError:
            continue
        masked = mask(text)
        for pat in PATTERNS:
            for m in pat.finditer(masked):
                line = masked.count("\n", 0, m.start()) + 1
                col = m.start() - masked.rfind("\n", 0, m.start())
                offenders.append(
                    f"{p.relative_to(ROOT).as_posix()}:{line}:{col}: {m.group(0)}"
                )

    if offenders:
        print("Deprecated caudio::db:: / caudio::engine:: spellings found:")
        for o in offenders:
            print(f"  {o}")
        print(
            f"\n{len(offenders)} offending use(s); "
            "use the caudio::types:: / caudio::utils:: spelling instead."
        )
        return 1

    print("check_legacy_aliases: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
