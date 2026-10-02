#caudio - cpp

[![CI](https://github.com/AhmedowM/caudio/actions/workflows/ci.yml/badge.svg)](https://github.com/AhmedowM/caudio/actions/workflows/ci.yml)
[![Version](https://img.shields.io/github/v/release/AhmedowM/caudio)](CHANGELOG.md)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![CMake](https://img.shields.io/badge/CMake-%3E%3D3.28-red)](CMakeLists.txt)

> C++23 headless music player library -- utils, player, db, engine, CLI

`caudio-cpp` is the C++23 port of `caudio` (C11 headless music daemon). It provides a modular library (`caudio::utils`, `caudio::player`, `caudio::db`, `caudio::engine`) plus a `caudio` CLI that talks to a background daemon over IPC (JSON + 4-byte big-endian framing).

## Features

- **FFmpeg decode** -- primary decoder (libavformat/avcodec/avutil/swresample), streaming demux, sample-accurate seek, metadata extraction
- **Gapless playback** -- pre-roll double-buffer in `Engine::decodeLoop`, frame-accurate `AudioOutput` callback
- **Shuffle / Repeat** -- Fisher-Yates shuffle with stable permutation, `RepeatMode::Off|One|All` (`src/engine/shuffle.cpp`)
- **Queue & Playlist** -- SQLite-backed queues, atomic `clear+re-enqueue` for move, M3U/PLS/JSON import/export
- **FTS5 search** -- `SQLite FTS5` virtual table with `sanitizeFtsTerm` fallback to `LIKE` (`src/db/search.cpp`)
- **Fingerprint dedup** -- BLAKE3 64 KiB head+tail fingerprint (`src/db/fingerprint.cpp`), sampled/full scan modes
- **SHM 10 fps** -- lock-free `AtomicShmStatus` shared-memory block polled by TUI at 10 Hz (`src/service/shm_status.cpp`)
- **IPC JSON+framing** -- `protocol::frame` 4-byte length prefix + `ordered_json` (`src/ipc/protocol.cpp`), Unix Domain Socket / Windows Named Pipe
- **Daemon lifecycle** -- single-instance `flock` (POSIX) / socket-bind (Windows), `caudio start [--foreground]` / `shutdown`, `STATUS` via `--watch`
- **C++23 modules** -- `import caudio;` umbrella, `std::expected`, `std::print`, `Generator` scan, `std::jthread`/`std::stop_token`

## Quick Start

```sh
git clone https://github.com/AhmedowM/caudio.git
cd caudio
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/caudio --version
./build/dev/caudio start
./build/dev/caudio status
```
See `cmake --list-presets` for all configs (`ci`, `release`, `modules`, `minimal`, ...).

Typical first session:

```sh
./build/dev/caudio library scan --path ~/Music --mode sampled
./build/dev/caudio library search "beatles" --limit 10
./build/dev/caudio queue add ~/Music/album/track.flac
./build/dev/caudio play
./build/dev/caudio status --watch --interval 1000
```

## CLI Overview

Run `caudio --help` or `caudio <subcommand> --help` for details. All commands (except `preview`) talk to the daemon via IPC.

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
| `caudio seek <time>` | Seek -- `mm:ss`, seconds, or relative `+N`/`-N` |
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

Global options:

| Option | Description |
|---|---|
| `--config <FILE>` | Config file path (default: XDG / `%LOCALAPPDATA%`) |
| `--log-level trace|debug|info|warn|error` | Daemon log level |
| `--device <DEVICE>` | Audio output device id |
| `--version` | Show version (`caudio::versionFull`, e.g. `vX.Y.Z`) |
| `--help` / `-h` | Show help |

## Library Usage

### CMake consumer

```cmake
find_package(caudio CONFIG REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE caudio::engine)
# also available: caudio::utils caudio::player caudio::db caudio::engine caudio::ipc caudio::service caudio::client
```

### C++ example (headers are canonical)

```cpp
#include <caudio.hpp> // umbrella: utils, player, db, engine

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

More examples in `examples/` (targets `caudio_mini`, `caudio_player_db_demo`, `caudio_engine_demo`):

- `examples/mini_cpp.cpp` -- minimal `caudio::player` playback
- `examples/player_db_demo.cpp` -- player + db scan/search
- `examples/engine_demo.cpp` -- engine queue/history/events

> **Packaging note -- C++23 modules (optional)**
>
> Headers under `include/caudio/` are the canonical interface and always build.
> C++23 modules (`import caudio;`, `*.cppm` units) are opt-in via
> `-DCAUDIO_ENABLE_MODULES=ON` (default `OFF`). When enabled, an installed
> `caudio` ships `*.cppm` via `FILE_SET CXX_MODULES` and consumers must rebuild
> BMIs against the consuming compiler/flags -- BMI CRC covers defines/flags, so
> sharing prebuilt BMIs across toolchains is not portable. See **Packaging**
> section below.

## Build Options

| Option | Default | Description |
|---|---|---|
| `CAUDIO_WITH_FETCH_FFMPEG` | `ON` | Auto-fetch FFmpeg if not found on system (system -> vcpkg/Conan -> prebuilt -> source) |
| `CAUDIO_ENABLE_TESTS` | `OFF` | Build Catch2 tests (`ctest --test-dir build -j4`) |
| `CAUDIO_ENABLE_SANITIZERS` | `OFF` | Enable ASan+UBSan (`-fsanitize=address,undefined`) -- Linux/GCC+Clang only; ignored on Windows/MinGW |
| `CAUDIO_BUILD_DOCS` | `OFF` | Build Doxygen docs (requires `doxygen`; optional `dot`) |
| `CAUDIO_ENABLE_EXAMPLES` | `OFF` | Build `examples/` (`caudio_mini`, `player_db_demo`, `engine_demo`) |
| `CAUDIO_ENABLE_MODULES` | `OFF` | Build/install C++23 module interfaces (`import caudio.*`); headers always build |
| `CAUDIO_TEST_NOAUDIO` | `OFF` | Skip audio device tests (no beep) for headless CI |
| `CAUDIO_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy checks during build |
| `CMAKE_BUILD_TYPE` | -- | `Debug` / `Release` / `RelWithDebInfo` |
| `FFmpeg_ROOT` | -- | Override FFmpeg location (passed to `find_package(FFmpeg)`) |

Toolchain requirements:

- **MinGW GCC 14+** or **GCC 14+ / Clang 17+** on Linux, **AppleClang 17+** on macOS
- **Ninja** (`-G Ninja`) recommended (required for C++23 modules with CMake)
- **FFmpeg 9.0.1+** (`libavcodec`, `libavformat`, `libavutil`, `libswresample`)

## Documentation

- **Doxygen API docs** -- `cmake --preset docs && cmake --build --preset docs` -> `build/docs/docs/html/`. Configured via `docs/Doxyfile.in` / `Doxyfile`.
- **Man page** -- `docs/man/caudio.1` (roff), installed to `${CMAKE_INSTALL_MANDIR}/man1`; view with `man ./docs/man/caudio.1`.
- **Polyglot integration** -- `docs/polyglot-integration.md` (Rust metadata, Svelte/Tauri GUI, Python bindings, Go sidecar).
- **Specs / audits** -- `docs/specs/`.

## Packaging

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix /usr/local
# man page: /usr/local/share/man/man1/caudio.1
# modules (CAUDIO_ENABLE_MODULES=ON only): /usr/local/modules/*.cppm (same level as include/)
# config:   /usr/local/lib/cmake/caudio/caudioConfig.cmake
```

CPack archives: `cpack --config build/<preset>/CPackConfig.cmake` -> `caudio-X.Y.Z-<system>.tar.gz` / `.zip`.

C++ modules packaging caveat: downstream projects must have CMake >= 3.28 and a compiler with C++23 module support. When built with `CAUDIO_ENABLE_MODULES=ON`, the `caudioTargets.cmake` exports `FILE_SET CXX_MODULES`; CMake will rebuild BMIs during the consumer's configure step. Do not ship prebuilt `*.pcm`/`*.ifc` BMIs.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT -- see [LICENSE](LICENSE) (if present) or `CPACK_RESOURCE_FILE_LICENSE`.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
