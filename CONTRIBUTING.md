#Contributing to caudio - cpp

## Development Setup

### Prerequisites

| Tool | Minimum Version | Notes |
|---|---|---|
| **Compiler** | GCC 14+ / Clang 17+ / AppleClang 17+ | C++23 required (`std::expected`, `std::print`, `std::generator`, modules). MinGW GCC 14+ on Windows. |
| **CMake** | 3.28+ | Required for `FILE_SET CXX_MODULES` and BMI handling |
| **Ninja** | 1.11+ | **Required** — C++23 modules only work reliably with Ninja generator |
| **FFmpeg** | 9.0.1+ | `libavformat`, `libavcodec`, `libavutil`, `libswresample`. Auto-fetched if not found when `CAUDIO_WITH_FETCH_FFMPEG=ON` |
| **Doxygen** | 1.9+ | Optional, only for `CAUDIO_BUILD_DOCS=ON`. `dot` (Graphviz) optional for graphs |
| **Catch2** | 3.7.1 | Auto-fetched via `FetchContent` when `CAUDIO_ENABLE_TESTS=ON` |
| **Git** | 2.30+ | For `git describe --tags` version stamping |

### Clone and Configure

```sh
git clone https://github.com/AhmedowM/caudio.git
cd caudio

#Recommended : Ninja + tests enabled
cmake -B build -G Ninja -DCAUDIO_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

#With docs
cmake -B build -G Ninja -DCAUDIO_ENABLE_TESTS=ON -DCAUDIO_BUILD_DOCS=ON

#Sanitizers(Linux only, ignored on Windows / MinGW)
cmake -B build -G Ninja -DCAUDIO_ENABLE_TESTS=ON -DCAUDIO_ENABLE_SANITIZERS=ON
```

### Build and Test

```sh
cmake --build build -j4
ctest --test-dir build -j4 --output-on-failure

#Run a single test binary
./build/test_engine_queue --success

#Manual CLI smoke test
./build/caudio --version
./build/caudio start
./build/caudio status
./build/caudio shutdown
```

### Documentation

```sh
cmake -B build -G Ninja -DCAUDIO_BUILD_DOCS=ON
cmake --build build --target doc
#HTML output : docs / html / and build / docs / html /
#Open : docs / html / index.html
```

Man page is at `docs/man/caudio.1` — preview with `man ./docs/man/caudio.1` or `groff -man -Tascii docs/man/caudio.1 | less`.

### Code Style

- **Formatter:** `clang-format` using `.clang-format` at repo root. Run before committing:
  ```sh
  clang-format -i src/**/*.cppm cli/src/**/*.cppm cli/src/**/*.cpp
  ```
- **Linter:** `clang-tidy` using `.clang-tidy` (optional, not enforced in CI yet).
- **Naming:** `PascalCase` for types, `camelCase` for functions/variables, `kConstant` for constants, `snake_case` for module partitions.
- **Modules:** Use `export module caudio.xxx:partition;
` with umbrella `import caudio;
` where appropriate.Do not add new headers — prefer `.cppm`.- **Errors
    : **Return `std::expected<T, utils::Error>`;
do not throw across module boundaries. Use `StatusCode` enum.

## Commit Style

This project uses [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <description>

[optional body]

[optional footer(s)]
```

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `perf`, `build`, `ci`

Scopes: `cli`, `engine`, `db`, `player`, `utils`, `service`, `ipc`, `cmake`, `vendor`, `docs`

Examples:

```
feat(cli): add library list + enhanced stats + info subcommand
fix(engine): align decodeLoop and preroll sample vs frame with player
docs: comprehensive Doxygen documentation for all public APIs
refactor(cmake): SANITIZERS target-scoped, sqlite dedup
```

- Keep subject line ≤ 72 chars.
- Use `BREAKING CHANGE:` footer for API-breaking changes.

## Pull Request Process

1. **Branch** from `main` (or current release branch):
   ```sh
   git checkout -b feat/my-feature
   ```
2. **Implement** with tests. No logic changes to `src/` without corresponding test updates. New IPC commands need both `cli/src/shared/command.cppm` and `cli/src/service/service_detail.cppm` updates plus `tests/test_ipc.cpp` coverage.
3. **Configure and test** locally:
   ```sh
   cmake -B build -G Ninja -DCAUDIO_ENABLE_TESTS=ON
   cmake --build build -j4
   ctest --test-dir build -j4
   cmake -B build -G Ninja -DCAUDIO_BUILD_DOCS=ON && cmake --build build --target doc
   ```
4. **Format** changed files with `clang-format`.
5. **Push** and open a PR against `main`. Include:
   - Summary of changes (Added/Changed/Fixed).
   - Any `BREAKING CHANGE` notes.
   - Screenshots or `caudio --help` output for CLI changes.
6. **CI** must pass (build + tests on Linux/Windows/macOS if configured).
7. **Review** — address feedback; squash or rebase as requested.

## Project Structure

```
caudio-cpp/
├── src/                 # Library (C++23 modules)
│   ├── utils/           # Error, Result, Ring, MpscQueue, Log, Thread
│   ├── player/          # Decoder (FFmpeg), FileReader, AudioOutput
│   ├── db/              # SQLite + FTS5, Library/Queue/Playlist/History, Scan, WriteThread
│   ├── engine/          # Playback engine, QueueState, Shuffle, History, EngineTypes
│   ├── json/            # ordered_json wrapper
│   └── caudio.cppm      # Umbrella module
├── cli/                 # CLI daemon + client
│   └── src/
│       ├── app/         # App dispatch (CLI11), parse helpers
│       ├── shared/      # Command/Result/Protocol (JSON + framing)
│       ├── service/     # Service daemon, IpcServer, ShmStatus
│       └── client/      # IpcClient, OutputFormatter
├── cmake/               # FindFFmpeg, CaudioHelpers, components/, version.hpp.in
├── docs/
│   ├── Doxyfile.in      # CMake-configured Doxygen template
│   ├── man/caudio.1     # Man page (roff)
│   └── polyglot-integration.md
├── tests/               # Catch2 tests
├── examples/            # mini_cpp, player_db_demo, engine_demo
└── vendor/              # sqlite3.c, blake3.c (vendored)
```

## Reporting Issues

Open an issue with:

- `caudio --version` and `cmake --version` / compiler version
- Config: `caudio config list --json` if relevant
- Repro steps and expected vs actual behavior
- Logs: set `--log-level debug` or check daemon output

## License

By contributing, you agree that your contributions will be licensed under the same license as the project (MIT).
