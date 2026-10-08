# caudio

[![CI](https://github.com/AhmedowM/caudio/actions/workflows/ci.yml/badge.svg)](https://github.com/AhmedowM/caudio/actions/workflows/ci.yml)
[![Version](https://img.shields.io/github/v/release/AhmedowM/caudio)](CHANGELOG.md)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![CMake](https://img.shields.io/badge/CMake-%3E%3D3.28-red)](CMakeLists.txt)

## Overview

`caudio` is a headless music player: seven C++23 libraries (utils, player, db, engine, ipc, client, service) plus a `caudio` CLI that drives a background daemon over IPC (JSON with 4-byte big-endian framing). It plays local files with FFmpeg decoding, SQLite-backed library management, and gapless miniaudio output — no GUI needed.

## Features

- **FFmpeg decode** -- primary decoder (libavformat/avcodec/avutil/swresample), streaming demux, sample-accurate seek, metadata extraction
- **Gapless playback** -- pre-roll double-buffer in `Engine::decodeLoop`, frame-accurate `AudioOutput` callback
- **Shuffle / Repeat** -- Fisher-Yates shuffle with stable permutation, `RepeatMode::Off|One|All` (`src/engine/shuffle.cpp`)
- **Queue & Playlist** -- SQLite-backed queues, atomic `clear+re-enqueue` for move, M3U/PLS/JSON import/export
- **FTS5 search** -- `SQLite FTS5` virtual table with `sanitizeFtsTerm` fallback to `LIKE` (`src/db/search.cpp`)
- **Fingerprint dedup** -- BLAKE3 64 KiB head+tail fingerprint (`src/db/fingerprint.cpp`), sampled/full scan modes
- **SHM status at 10 Hz** -- lock-free `AtomicShmStatus` shared-memory block polled by TUIs (`src/service/shm_status.cpp`)
- **Daemon lifecycle** -- single-instance `flock` (POSIX) / socket-bind (Windows), `caudio start [--foreground]` / `shutdown`, live `STATUS` via `--watch`

## Quickstart

Pick the preset matching your machine (`cmake --list-presets` shows all):

```sh
git clone https://github.com/AhmedowM/caudio.git
cd caudio

# Linux (GCC is the default) / macOS (brew LLVM Clang) / Windows (see below)
cmake --preset dev        # tests + examples
cmake --build --preset dev
ctest --preset dev
./build/dev/caudio --version
./build/dev/caudio start
./build/dev/caudio status
```

| Host | Preset | Toolchain |
|---|---|---|
| Linux | `dev` (default) or `dev-gcc` | GCC 14+ (default), `dev-clang` also works |
| macOS | `dev-clang` | brew LLVM Clang only — AppleClang and GCC fail configure (C++23 gaps / dummy audio backend) |
| Windows | `dev` (default) | LLVM Clang from `PATH` when installed (MSVC ABI target); falls back to MSVC / MinGW GCC |
| Windows | `dev-msvc` | MSVC via the Visual Studio 2026 generator (no vcvars needed) — CI stays on MSVC |
| Windows | `dev-gcc` | MinGW GCC 14+ (ships larger binaries) |

Other ready-made presets: `ci` (headless tests, no audio), `ci-sanitizers` (ASan+UBSan, Linux only), `release` / `release-lto` (optimized, tag required), `minsize` (smallest binary), `shared` (shared libs + `libcaudio`), `modules` (C++23 modules, no CLI), `minimal` (no CLI), `docs`, `all`. Compiler variants append `-gcc` / `-clang` / `-msvc` (`ci-clang`, `release-msvc`, …). The `custom` preset builds into `$CAUDIO_BUILD_DIR` with the compiler from `PATH`.

Typical first session:

```sh
./build/dev/caudio library scan --path ~/Music
./build/dev/caudio library search "beatles" --limit 10
./build/dev/caudio queue add ~/Music/album/track.flac
./build/dev/caudio play
./build/dev/caudio status --watch --interval 1000
# ...or skip the queue entirely:
./build/dev/caudio ~/Music/album/track.flac
```

## Configuration

Everything is a preset; individual options exist for scripting and CI:

| Option | Default | Effect |
|---|---|---|
| `CAUDIO_WITH_FETCH_FFMPEG` | `ON` | Auto-fetch FFmpeg when missing (system → vcpkg/Conan → prebuilt → source) |
| `CAUDIO_BUILD_CLI` | `ON` | Build the `caudio` executable (`OFF` skips the CLI11 fetch entirely) |
| `CAUDIO_BUILD_SHARED` | `OFF` | Shared `*_shared` variants + combined `libcaudio` |
| `CAUDIO_ENABLE_TESTS` | `OFF` | Catch2 tests (`ctest --preset dev`) |
| `CAUDIO_ENABLE_EXAMPLES` | `OFF` | `examples/` (`caudio_mini`, `player_db_demo`, `engine_demo`) |
| `CAUDIO_ENABLE_SANITIZERS` | `OFF` | ASan+UBSan — Linux/GCC+Clang only, ignored on Windows/MinGW |
| `CAUDIO_ENABLE_MODULES` | `OFF` | C++23 module interfaces (`import caudio.*`); headers always build |
| `CAUDIO_BUILD_DOCS` | `OFF` | Doxygen docs (needs `doxygen`; optional `dot`) |
| `CAUDIO_TEST_NOAUDIO` | `OFF` | Skip audio-device tests (no beep) for headless CI |
| `CAUDIO_ENABLE_CLANG_TIDY` | `OFF` | clang-tidy during build |
| `CAUDIO_REQUIRE_GIT_VERSION` | `OFF` | Fail configure without a git tag (release presets enable it) |
| `CMAKE_BUILD_TYPE` | -- | `Debug` / `Release` / `RelWithDebInfo` (single-config generators) |
| `FFmpeg_ROOT` | -- | Override FFmpeg location |

Requirements:

- **Compilers** -- Linux: GCC 14+ (default) or Clang 17+; macOS: brew LLVM Clang (`brew install llvm`);
Windows: LLVM Clang preferred (MSVC ABI target, also what releases build with), MSVC 2022+ via `dev-msvc`-style presets (CI), or MinGW GCC 14+
- **CMake ≥ 3.28**, **Ninja** (required for C++23 modules, recommended everywhere)
- **FFmpeg** -- system install preferred (Ubuntu: `libavcodec-dev libavformat-dev libavutil-dev libswresample-dev`; macOS: `brew install ffmpeg`; Windows: `choco install ffmpeg`), else auto-fetched
- **Auto-fetched, no action needed** -- Catch2 3.16.0 (tests only), CLI11 2.7.2 (CLI only)
- **Vendored in `vendor/`** -- nlohmann/json 3.12.0, SQLite 3.53.4, miniaudio 0.11.25, BLAKE3 1.8.7 (see `vendor/README.md` for provenance)
- **Optional** -- Doxygen (+ Graphviz `dot`) for docs; `ccache` via `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`

## Usage

Run `caudio --help` or `caudio <subcommand> --help`. Most commands talk to the daemon over IPC; `config get`, client-side validation, and PATH expansion work without one, and `play` (plus direct play below) autostarts a missing daemon.

| Command | Effect |
|---|---|
| `caudio [PATH]... [--save]` | Play files right away in a new queue (temporary, purged on shutdown; `--save` keeps it). PATH is a file, folder, or glob |
| `caudio start [--foreground]` | Start daemon (background by default; `--foreground` for systemd/debug) |
| `caudio shutdown` | Stop daemon (sends `Shutdown`, polls pid+socket) |
| `caudio play` | Start/resume playback (replays stopped track; autostarts daemon) |
| `caudio pause` | Pause playback (warns when idle) |
| `caudio resume` | Resume from pause (plays from cursor when stopped) |
| `caudio restart` | Seek to 0 and play |
| `caudio stop` | Stop playback |
| `caudio next` / `prev` | Next / previous track (shuffle-aware, repeat-aware; `next` wraps, `prev` warns at start) |
| `caudio seek <time>` | Seek -- `mm:ss`, seconds, or relative `+N`/`-N` (silent on success) |
| `caudio status [--json] [--watch] [--interval <ms>]` | Show status; `--watch`/`--follow` polls every `--interval` ms (default 1000), JSON streams one object per line |
| `caudio volume [0-100\|+N\|-N\|mute\|unmute]` | Get or set volume (`mute`/`0` remembers level, `unmute` restores) |
| `caudio queue tracks [--order added\|playback] [--json]` | Tracks in active queue (playback order, current marked `>`) |
| `caudio queue list [--json]` | List all queues (id, name, track count, active, temp) |
| `caudio queue switch <qid>` | Switch active queue (cursor resets) |
| `caudio queue create <name>` / `queue delete <qid>` | Create queue / delete queue (never the active one) |
| `caudio queue add [PATH]... [--id <id>] [--playlist <pid> [--replace]] [--search] [--recursive] [--json]` | Add files (auto-library), ids, search hits, or playlist tracks; re-adds warn |
| `caudio queue remove [PATH]... [--id <id>] [--pos <n>] [--json]` | Remove by path, library id, or queue position (exclusive) |
| `caudio queue move <from> <to>` | Reorder queue |
| `caudio queue clear` | Clear queue (playback of current track continues) |
| `caudio queue shuffle [on\|off]` | Toggle or set shuffle (reports state) |
| `caudio queue repeat [off\|one\|all]` | Set repeat mode (bare cycles and reports) |
| `caudio playlist list [--json]` | List playlists |
| `caudio playlist tracks <pid> [--json]` | Tracks in playlist |
| `caudio playlist create <name>` | Create empty playlist |
| `caudio playlist add <pid> [--id <id>...] [PATH]...` | Append tracks (re-adds warn) |
| `caudio playlist load <pid> [--play] [--replace]` | Load into a new queue (`--replace`: overwrite active; `--play`: switch and play) |
| `caudio playlist save <name> [--queue <qid>]` | Save queue as playlist |
| `caudio playlist delete <pid>` | Delete playlist |
| `caudio playlist rename <pid> <name>` | Rename playlist |
| `caudio playlist export <pid> <path> [--format m3u\|pls\|json]` | Export to file |
| `caudio playlist import <path> [--name <n>]` | Import from file (reports matched/skipped/duplicates) |
| `caudio library scan [--path <p>] [--full-hash]` | Scan directory (reports added tracks) |
| `caudio library search <query> [--limit N] [--json]` | FTS5 + filename search with match highlighting |
| `caudio library stats [--most-played N] [--queue all\|<qid>] [--playlist <pid>] [--json]` | Counts, top-N, queue/playlist overviews |
| `caudio library list [--query <q>] [--artist <a>] [--album <a>] [--genre <g>] [--limit N] [--offset N] [--json]` | Filtered listing |
| `caudio library add <path> [--recursive]` | Add file/dir to library |
| `caudio library remove <id-or-path>` | Remove track from library (files untouched) |
| `caudio tag edit <id> <field> <value>` | Edit tag in database (`title`,`artist`,`album`,`album_artist`,`genre`,`year`,`track_number`,`disc_number`; files untouched) |
| `caudio tag get <id> [field] [--json]` | Get track tags (or one field) |
| `caudio info [--json]` | Current track info (metadata + play count) |
| `caudio history list [--limit N] [--json]` | Playback history |
| `caudio history clear` | Clear history |
| `caudio device list [--json]` | List audio output devices |
| `caudio device set <id>` | Set default device |
| `caudio device test [--id <id>]` | Check device is available (enumeration only, no sound) |
| `caudio config get <key> [--json]` | Get config value (no daemon needed) |
| `caudio config set <key> <value>` | Set config value (silent) |
| `caudio config list [--json]` | List config |
| `caudio config export <path>` | Export config file |
| `caudio config import <path>` | Validate and import config file |
| `caudio config reset [key]` | Reset key or all to defaults |

Global options: `--config <FILE>` (default XDG / `%LOCALAPPDATA%`), `--log-level trace|debug|info|warn|error`, `--device <DEVICE>`, `--version` (prints `caudio::versionFull`, e.g. `vX.Y.Z`), `--help` / `-h`. Exit codes: `0` success, `1` runtime/validation error, `105` bad option value, `106` missing argument, `109` unknown command (see `EXIT STATUS` in `docs/man/caudio.1`).

### As a library

Seven components, link only what you use:

| Target | Covers |
|---|---|
| `caudio::utils` | Errors, logging, JSON facade, threads, queues/rings, printing, versioning |
| `caudio::player` | File reading, FFmpeg decoding, miniaudio output |
| `caudio::db` | SQLite storage: tracks, library scan, FTS5 search, batched writes |
| `caudio::engine` | Playback state machine: queue, shuffle, history, gapless, events |
| `caudio::ipc` | Daemon wire protocol: commands, results, framing, config |
| `caudio::client` | Daemon client + output formatting |
| `caudio::service` | The daemon: IPC server, dispatch, shared-memory status |

Headers under `include/caudio/` are canonical and always build:

```cmake
find_package(caudio CONFIG REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE caudio::engine)
```

```cpp
#include <caudio.hpp> // umbrella: utils, player, db, engine, ipc, client, service
#include <print>

int main() {
    auto engRes = caudio::engine::Engine::open(":memory:");
    if (!engRes) {
        std::println(stderr, "open: {}", engRes.error().message);
        return 1;
    }
    auto eng = std::move(engRes.value());
    if (auto r = eng->play(); !r) {
        std::println(stderr, "play: {}", r.error().message);
        return 1;
    }
    std::println("state={}", (int)eng->state());
}
```

`examples/` holds runnable versions (built by the `dev` preset): `mini_cpp.cpp` (minimal `caudio::player`), `player_db_demo.cpp` (player + db scan/search), `engine_demo.cpp` (queue/history/events).

### As C++23 modules (opt-in)

Configure with the `modules` preset (`CAUDIO_ENABLE_MODULES=ON`, no CLI), then import instead of including:

```sh
cmake --preset modules-clang   # or modules-gcc / modules-msvc
cmake --build --preset modules-clang
```

```cmake
cmake_minimum_required(VERSION 3.28)
project(mod_consumer CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
find_package(caudio CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE caudio::db)
```

```cpp
import caudio.db;

int main() {
    auto tracks = caudio::db::scanDirectory(".");

    return tracks.has_value() ? 0 : 1;
}
```

Available modules mirror the libraries: `caudio`, `caudio.utils`, `caudio.player`, `caudio.db`, `caudio.engine`, `caudio.ipc`, `caudio.service`, `caudio.client` (plus `:partitions` such as `caudio.db:scan`). An install ships the `*.cppm` sources under `<prefix>/modules/`; your toolchain rebuilds BMIs at consumer-configure time — BMI CRC covers defines/flags, so prebuilt BMIs never travel across compilers. Requires CMake ≥ 3.28 and a compiler with C++23 module support (GCC 14+, Clang 17+, MSVC 2022+).

API docs: `cmake --preset docs && cmake --build --preset docs` → `build/docs/docs/html` (`docs/Doxyfile.in`); man page at `docs/man/caudio.1`, installed to `${CMAKE_INSTALL_MANDIR}/man1`.

Packaging:

```sh
cmake --preset release-lto-clang   # or release-lto-gcc (tag required); releases ship from release-lto-*
cmake --build --preset release-lto-clang
cmake --install build/release-lto-clang --prefix /usr/local
# man page: /usr/local/share/man/man1/caudio.1
# modules (CAUDIO_ENABLE_MODULES=ON only): /usr/local/modules/*.cppm (same level as include/)
# config:   /usr/local/lib/cmake/caudio/caudioConfig.cmake
```

`cpack --config build/<preset>/CPackConfig.cmake` produces `caudio-X.Y.Z-<system>.tar.gz` / `.zip`.

## License

MIT -- see [LICENSE](LICENSE). Contributing: [CONTRIBUTING.md](CONTRIBUTING.md). Changes: [CHANGELOG.md](CHANGELOG.md).
