#caudio - cpp

[![CI](https://github.com/anomalyco/caudio-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/anomalyco/caudio-cpp/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-v0.25.4-blue)](CHANGELOG.md)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![CMake](https://img.shields.io/badge/CMake-%3E%3D3.28-red)](CMakeLists.txt)

> C++23 headless music player library — utils, player, db, engine, CLI

`caudio-cpp` is the C++23 port of `caudio` (C11 headless music daemon). It provides a modular library (`caudio::utils`, `caudio::player`, `caudio::db`, `caudio::engine`) plus a `caudio` CLI that talks to a background daemon over IPC (JSON + 4-byte big-endian framing).

## Features

- **FFmpeg decode** — primary decoder (libavformat/avcodec/avutil/swresample), streaming demux, sample-accurate seek, metadata extraction
- **Gapless playback** — pre-roll double-buffer in `Engine::decodeLoop`, frame-accurate `AudioOutput` callback
- **Shuffle / Repeat** — Fisher-Yates shuffle with stable permutation, `RepeatMode::Off|One|Queue` (`src/engine/shuffle.cppm:1`)
- **Queue & Playlist** — SQLite-backed queues, atomic `clear+re-enqueue` for move, M3U/PLS/JSON import/export
- **FTS5 search** — `SQLite FTS5` virtual table with `sanitizeFtsTerm` fallback to `LIKE` (`src/db/search.cppm:10`)
- **Fingerprint dedup** — BLAKE3 64 KiB head+tail fingerprint (`src/db/fingerprint.cppm:8`), sampled/full scan modes
- **SHM 10 fps** — lock-free `AtomicShmStatus` shared-memory block polled by TUI at 10 Hz (`cli/src/service/shm_status.cppm:20`)
- **IPC JSON+framing** — `protocol::frame` 4-byte length prefix + `ordered_json` (`cli/src/shared/protocol.cppm:750`), Unix Domain Socket / Windows Named Pipe
- **Daemon lifecycle** — single-instance `flock` (POSIX) / socket-bind (Windows), `caudio start [--foreground]` / `shutdown`, `STATUS` via `--watch`
- **C++23 modules** — `import caudio;` umbrella, `std::expected`, `std::print`, `std::generator` scan, `std::jthread`/`std::stop_token`

## Quick Start

```sh
cmake -B build -G Ninja -DCAUDIO_ENABLE_TESTS=ON
cmake --build build -j4
ctest --test-dir build -j4
./build/caudio --version
./build/caudio start
./build/caudio status
```

Typical first session:

```sh
./build/caudio library scan --path ~/Music --mode sampled
./build/caudio library search "beatles" --limit 10
./build/caudio queue add ~/Music/album/track.flac
./build/caudio play
./build/caudio status --watch --interval 1000
```

## CLI Overview

Run `caudio --help` or `caudio <subcommand> --help` for details. All commands (except `preview`/`tui`) talk to the daemon via IPC.

| Command | Description |
|---|---|
| `caudio start [--foreground]` | Start daemon (background by default; `--foreground` for systemd/debug) |
| `caudio shutdown` | Stop daemon (sends `Shutdown`, polls pid+socket) |
| `caudio play` | Start/resume playback |
| `caudio pause` | Pause playback |
| `caudio resume` | Resume from pause |
| `caudio restart` | Seek to 0 and play |
| `caudio stop` | Stop playback |
| `caudio next` / `prev` | Next / previous track (shuffle-aware, repeat-aware) |
| `caudio seek <time>` | Seek — `mm:ss`, seconds, or relative `+N`/`-N` |
| `caudio status [--json] [--watch] [--interval <ms>]` | Show status; `--watch`/`--follow` polls every `--interval` ms (default 1000) |
| `caudio volume [0-100|+N|-N|mute|unmute]` | Get or set volume |
| `caudio queue list [--json]` | List tracks in active queue |
| `caudio queue queues` | List all queues |
| `caudio queue switch <qid>` | Switch active queue |
| `caudio queue add <query> [--search]` | Add by path / track id / FTS query |
| `caudio queue remove <id>` | Remove by position or track id |
| `caudio queue move <from> <to>` | Reorder queue |
| `caudio queue clear` | Clear queue |
| `caudio queue shuffle [on|off]` | Toggle or set shuffle |
| `caudio queue repeat [off|one|all]` | Set repeat mode |
| `caudio playlist list [--json]` | List playlists |
| `caudio playlist tracks <pid>` | Tracks in playlist |
| `caudio playlist load <pid> [--play]` | Load playlist into queue |
| `caudio playlist save <name> [--queue <qid>]` | Save queue as playlist |
| `caudio playlist delete <pid>` | Delete playlist |
| `caudio playlist rename <pid> <name>` | Rename playlist |
| `caudio playlist export <pid> <path> [--format m3u|pls|json]` | Export to file |
| `caudio playlist import <path> [--name <n>]` | Import from file |
| `caudio library scan [--path <p>] [--mode sampled|full]` | Scan directory |
| `caudio library search <query> [--limit N] [--json]` | FTS5 search |
| `caudio library stats [--json] [--detailed]` | Library counts (+ most-played / total time with `--detailed`) |
| `caudio library list [--query <q>] [--artist <a>] [--album <a>] [--genre <g>] [--limit N] [--offset N] [--json]` | Filtered library listing |
| `caudio library add <path> [--recursive]` | Add file/dir to library |
| `caudio library remove <id>` | Remove track from library |
| `caudio tag edit <id> <field> <value>` | Edit tag (`title`,`artist`,`album`,`album_artist`,`genre`,`year`,`track_number`,`disc_number`) |
| `caudio tag get <id> [--json]` | Get track tags |
| `caudio info [--json]` | Current track info (metadata + play count) |
| `caudio history list [--limit N] [--json]` | Playback history |
| `caudio history clear` | Clear history |
| `caudio device list [--json]` | List audio output devices |
| `caudio device set <id>` | Set default device |
| `caudio device test [--id <id>]` | Play test tone on device |
| `caudio config get <key>` | Get config value |
| `caudio config set <key> <value>` | Set config value |
| `caudio config list [--json]` | List config |
| `caudio config export <path>` | Export config file |
| `caudio config import <path>` | Import config file |
| `caudio config reset [key]` | Reset key or all to defaults |
| `caudio preview <file>` | Ephemeral playback without daemon (direct `Player`) |
| `caudio tui` | TUI preview — shows `status` + guidance (full ratatui TUI planned v0.28.0) |

Global options:

| Option | Description |
|---|---|
| `--config <FILE>` | Config file path (default: XDG / `%LOCALAPPDATA%`) |
| `--log-level trace|debug|info|warn|error` | Daemon log level |
| `--device <DEVICE>` | Audio output device id |
| `--version` | Show version (`caudio::kVersionFull`, e.g. `v0.25.4`) |
| `--help` / `-h` | Show help |

## Library Usage

### CMake consumer

```cmake
find_package(caudio 0.25 CONFIG REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE caudio::engine)
#also available : caudio::utils caudio::player caudio::db caudio::json
```

### C++ example (C++23 modules)

```cpp
import caudio;                 // umbrella re-exports utils, player, db, engine
import caudio.utils;
import caudio.engine;

#include <print>

#include "caudio/version.hpp"

int main() {
    std::println("caudio {}", caudio::kVersionFull); // v0.25.4

    auto db = caudio::db::Database::open(":memory:").value();
    caudio::engine::EngineConfig cfg{.dbPath = ":memory:",
                                     .onEvent = [](const caudio::engine::EngineEvent& ev) {
                                         std::println("event {}", (int)ev.type);
                                     }};
    auto eng = caudio::engine::Engine::create(cfg, db).value();
    eng->play();
    auto st = eng->status();
    std::println("state={} queue={}", (int)st.state, st.queueSize);
}
```

More examples in `examples/`:

- `examples/mini_cpp.cpp` — minimal `caudio::player` playback
- `examples/player_db_demo.cpp` — player + db scan/search
- `examples/engine_demo.cpp` — engine queue/history/events

> **Packaging note — C++20/23 modules**
>
> `import caudio;` requires the `*.cppm` module interface units *and* a BMI rebuild.
> An installed `caudio` ships `*.cppm` under `${CMAKE_INSTALL_INCLUDEDIR}/caudio` via
> `FILE_SET CXX_MODULES` (see `CMakeLists.txt:272`). Consumers must rebuild BMIs
> against the consuming compiler/flags — BMI CRC covers defines/flags, so sharing
> prebuilt BMIs across toolchains is not portable. See **Packaging** section below.

## Build Options

| Option | Default | Description |
|---|---|---|
| `CAUDIO_WITH_FETCH_FFMPEG` | `ON` | Auto-fetch FFmpeg if not found on system (system → vcpkg/Conan → prebuilt → source) |
| `CAUDIO_ENABLE_TESTS` | `OFF` | Build Catch2 tests (`ctest --test-dir build -j4`) |
| `CAUDIO_ENABLE_SANITIZERS` | `OFF` | Enable ASan+UBSan (`-fsanitize=address,undefined`) — Linux/GCC+Clang only; ignored on Windows/MinGW |
| `CAUDIO_BUILD_DOCS` | `OFF` | Build Doxygen docs (requires `doxygen`; optional `dot`) |
| `CAUDIO_ENABLE_EXAMPLES` | `OFF` | Build `examples/` (`caudio_mini`, `player_db_demo`, `engine_demo`) |
| `CMAKE_BUILD_TYPE` | — | `Debug` / `Release` / `RelWithDebInfo` |
| `FFmpeg_ROOT` | — | Override FFmpeg location (passed to `find_package(FFmpeg)`) |

Toolchain requirements:

- **MinGW GCC 14+** or **GCC 14+ / Clang 17+** on Linux, **AppleClang 17+** on macOS
- **Ninja** (`-G Ninja`) recommended (required for C++23 modules with CMake)
- **FFmpeg 9.0.1+** (`libavcodec`, `libavformat`, `libavutil`, `libswresample`)

## Documentation

- **Doxygen API docs** — `cmake -B build -G Ninja -DCAUDIO_BUILD_DOCS=ON && cmake --build build --target doc` → `docs/html/` (and `build/docs/html/`). Configured via `docs/Doxyfile.in` / `Doxyfile`.
- **Man page** — `docs/man/caudio.1` (roff), installed to `${CMAKE_INSTALL_MANDIR}/man1`; view with `man ./docs/man/caudio.1`.
- **Polyglot integration** — `docs/polyglot-integration.md` (Rust metadata, Svelte/Tauri GUI, Python bindings, Go sidecar).
- **Specs / audits** — `docs/specs/`.

## Packaging

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
cmake --install build --prefix /usr/local
#man page : / usr / local / share / man / man1 / caudio.1
#modules : / usr / local / include / caudio/*.cppm
# config:   /usr/local/lib/cmake/caudio/caudioConfig.cmake
```

CPack archives: `cpack --config build/CPackConfig.cmake` → `caudio-0.25.4-<system>.tar.gz` / `.zip`.

C++ modules packaging caveat: downstream projects must have CMake ≥ 3.28 and a compiler with C++23 module support. The `caudioTargets.cmake` exports `FILE_SET CXX_MODULES`; CMake will rebuild BMIs during the consumer's configure step. Do not ship prebuilt `*.pcm`/`*.ifc` BMIs.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT — see [LICENSE](LICENSE) (if present) or `CPACK_RESOURCE_FILE_LICENSE`.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
