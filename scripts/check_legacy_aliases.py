#!/usr/bin/env python3
"""Guard: reject new uses of the deprecated caudio spellings.

Every entry below has a canonical spelling (see the deprecation inventory in
.dev/public_api_audit_v2.md) and the old spelling only exists as a
compatibility shim until v2.0.0. New first-party code -- sources, headers,
tests, modules, examples -- must use the canonical spelling directly.

Covered:
  * moved value types (include/caudio/legacy_aliases.hpp) and the Track JSON
    codec forwarders (include/caudio/db/json.hpp);
  * deprecated caudio/<comp>/core.hpp wrappers;
  * renamed App / Database / Engine members;
  * caudio::player::* / caudio::media::TrackMetadata aliases;
  * caudio::ipc::* result aliases;
  * caudio::db::* scan forwarders and LibraryStatsDetailedData.

Comments and string literals are ignored, and re-export sites (the alias
declarations, module partitions that keep the deprecated names exported) are
excluded, so only real uses are reported. Compatibility tests that must name
the old spelling suppress the line with a trailing `// legacy-shim-ok`
comment (same line or the line above).

Exit 0 when clean, 1 when any offending spelling is found.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

EXCLUDE = {
    (ROOT / "include/caudio/legacy_aliases.hpp").resolve(),
    # Re-export the deprecated names so the module API keeps working during the
    # deprecation window; first-party code must not use them.
    (ROOT / "modules/caudio/db/json.cppm").resolve(),
    (ROOT / "modules/caudio/db/scan.cppm").resolve(),
    (ROOT / "modules/caudio/db/types.cppm").resolve(),
    (ROOT / "modules/caudio/ipc/result.cppm").resolve(),
    (ROOT / "modules/caudio/player.cppm").resolve(),
    (ROOT / "modules/caudio/player/decoder.cppm").resolve(),
    (ROOT / "modules/caudio/player/output.cppm").resolve(),
    (ROOT / "modules/caudio/player/reader.cppm").resolve(),
    (ROOT / "modules/caudio/media/decoder.cppm").resolve(),
}
EXT = {".hpp", ".hh", ".h", ".cpp", ".cc", ".cxx", ".cppm"}
SCAN_DIRS = ("include", "src", "cli", "tests", "examples", "modules")
SUPPRESS = "legacy-shim-ok"

# (regex, canonical spelling / guidance). Qualified-identifier patterns for
# unambiguous names; receiver-qualified patterns where the old and the new
# member share a name across classes (App::queueList vs Database::queueList,
# Database::getStats vs Engine::getStats, Engine::switchQueue vs
# App::switchQueue).
SHIMS = [
    # include/caudio/legacy_aliases.hpp
    (r"\bcaudio::db::(Bookmark|HistoryEntry|Playlist|QueueItem|Track)\b",
     "caudio::types::\\1"),
    (r"\bcaudio::engine::(PlaybackState|RepeatMode)\b", "caudio::types::\\1"),
    # include/caudio/db/json.hpp
    (r"\bcaudio::db::(trackFromJson|trackToJson)\b", "caudio::utils::\\1"),
    # <caudio/player/*.hpp>, <caudio/media/decoder.hpp>
    (r"\bcaudio::player::(Reader|FileReader|MemoryReader|Decoder)\b",
     "caudio::media::\\1"),
    (r"\bcaudio::player::extractMetadata\b", "caudio::media::extractMetadata"),
    (r"\bcaudio::(?:player|media)::TrackMetadata\b", "caudio::types::TrackMetadata"),
    (r"\bcaudio::player::(DeviceInfo|DeviceList)\b", "caudio::types::\\1"),
    # include/caudio/ipc/result/*.hpp
    (r"\bcaudio::ipc::CliError\b", "caudio::utils::Error"),
    (r"\bcaudio::ipc::Empty\b", "std::monostate"),
    (r"\bcaudio::ipc::HistoryEntry\b", "caudio::types::HistoryEntry"),
    (r"\bcaudio::ipc::BookmarkEntry\b", "caudio::types::Bookmark"),
    (r"\bcaudio::ipc::DeviceInfo\b", "caudio::types::DeviceInfo"),
    (r"\bcaudio::ipc::LibraryStatsData\b", "caudio::types::LibraryStats"),
    (r"\bcaudio::ipc::LibraryStatsDetailedData\b",
     "caudio::types::LibraryStatsDetailed"),
    # include/caudio/db/scan.hpp, include/caudio/db/types.hpp
    (r"\bcaudio::db::ScanMode\b", "caudio::media::ScanMode"),
    (r"\bcaudio::db::scan\b", "caudio::media::scanTracks"),
    (r"\bcaudio::db::scanDirectory\b", "caudio::media::scanTracksToVector"),
    (r"\bcaudio::db::detail::hasAudioExt\b", "caudio::media::hasAudioExt"),
    (r"\bcaudio::db::LibraryStatsDetailedData\b",
     "caudio::types::LibraryStatsDetailed"),
    # include/caudio/app/app.hpp
    (r"\b(?:App|caudio::app::App)::(doPlay|queues|switchQueue)\b",
     "play(), queueList(), queueSwitch()"),
    (r"\bapp_?\s*(?:\.|->)\s*(doPlay|queues|switchQueue)\s*\(",
     "play(), queueList(), queueSwitch()"),
    # include/caudio/db/database.hpp
    (r"\b(?:Database|caudio::db::Database)::"
     r"(queueList|historyList|bookmarkList|libraryList)\b",
     "listQueueItems(), listHistory(), listBookmarks(), listLibraries()"),
    (r"\b(?:db|db_|database|database_)\s*(?:\.|->)\s*"
     r"(queueList|historyList|bookmarkList|libraryList)\s*\(",
     "listQueueItems(), listHistory(), listBookmarks(), listLibraries()"),
    # include/caudio/engine/engine.hpp
    (r"\b(?:Engine|caudio::engine::Engine)::getStats\b", "stats()"),
    (r"\b(?:engine|engine_|eng)\s*(?:\.|->)\s*getStats\s*\(", "stats()"),
]
PATTERNS = [(re.compile(pattern), suggestion) for pattern, suggestion in SHIMS]

# Deprecated caudio/<comp>/core.hpp wrappers. First-party code must include the
# canonical domain header (app/app.hpp, client/client.hpp, db/database.hpp,
# engine/engine.hpp, player/player.hpp, service/service.hpp) instead.
INCLUDE_PATTERN = re.compile(
    r"#\s*include\s*<caudio/(?:app|client|db|engine|player|service)/core\.hpp>"
)


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
        lines = text.splitlines()

        def suppressed(line_no: int) -> bool:
            # line_no is 1-based; allow the marker on the same line or on the
            # line directly before/after (multi-line declarations).
            for candidate in (line_no, line_no - 1, line_no + 1):
                if 1 <= candidate <= len(lines) and SUPPRESS in lines[candidate - 1]:
                    return True
            return False

        found = []
        for pat, suggestion in PATTERNS:
            for m in pat.finditer(masked):
                found.append((m, suggestion))
        for m in INCLUDE_PATTERN.finditer(masked):
            found.append((m, "the canonical caudio/<comp>/<domain>.hpp header"))

        for m, suggestion in found:
            line = masked.count("\n", 0, m.start()) + 1
            if suppressed(line):
                continue
            col = m.start() - masked.rfind("\n", 0, m.start())
            hint = m.expand(suggestion)
            offenders.append(f"{p.relative_to(ROOT).as_posix()}:{line}:{col}: {m.group(0)} (use {hint})")

    if offenders:
        print("Deprecated API spellings / core.hpp includes found:")
        for o in offenders:
            print(f"  {o}")
        print(
            f"\n{len(offenders)} offending use(s); "
            "use the canonical caudio::types:: / caudio::media:: / "
            "caudio::utils:: spelling and the canonical "
            "caudio/<comp>/<domain>.hpp header instead. If the line is a "
            f"compatibility test, mark it with `// {SUPPRESS}`."
        )
        return 1

    print("check_legacy_aliases: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
