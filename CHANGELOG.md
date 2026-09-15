#Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.25.5] - 2026-09-15

### Added
- `docs` + `man` + `service` packaging — `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `docs/man/caudio.1` (roff), `packaging/caudio.service` (systemd user unit); Doxygen docs wiring via `docs/Doxyfile.in`
- Packaging pipeline — install `caudio` binary, C++23 module sources (`src/` + `cli/src/` for BMI rebuild), man page, systemd unit; `CPack` per-platform generators (`TGZ`/`ZIP` + `DEB`/`RPM` on Linux, `NSIS` on Windows, `DragNDrop` on macOS)

### Changed
- Version wiring to `import caudio.utils` — `cmake/version.hpp.in` (moved from `version.hpp.in`), `src/utils/version.cppm` (`version()`/`versionString()`/`shortVersion()`/`versionCommit()`), `Engine::version()`/`Database::version()` + `staticVersion()`, `Status.version` via `service_detail::buildStatus` + `protocol` + `output_formatter`, CLI `--version` now `kVersionFull` (`v0.25.5`) (`301f7c8`)

### Fixed
- `cmake/version.hpp.in` clang-format fix

## [0.25.4] - 2026-09-14

CLI feature-completeness + documentation release. No breaking API changes.

### Added
- `library list` — filtered library listing with `--query`/`--artist`/`--album`/`--genre`/`--limit`/`--offset`/`--json` (`cli/src/app/core.cppm:618`, `cli/src/shared/command.cppm:599`)
- `library stats --detailed` — detailed stats with most-played and total play time (`LibraryStatsDetailed`)
- `info` — current track info with metadata and play statistics (`Info` command)
- Comprehensive Doxygen documentation for all public APIs (`6213c12`, `2331e88`) — modules `caudio.utils`, `caudio.player`, `caudio.db`, `caudio.engine`, `caudio.cli`, `caudio.service`, `caudio.client`, `caudio.json`
- Project markdown docs: `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md` and man page `docs/man/caudio.1`

### Changed
- Docs build via `docs/Doxyfile.in` with `@PROJECT_VERSION@` / `@DOXYGEN_HAVE_DOT@` substitution; `cmake --build build --target doc` generates to `build/docs/html` and `docs/html` (see `CMakeLists.txt:186`)

### Fixed
- Metadata extraction + display fallback for missing tags (`b499553`)

## [0.25.3] - 2026-09-13

### Added
- Device management — `device list`/`set`/`test` (miniaudio device enumeration via `src/player/output.cppm:140`)

### Fixed
- Tag/metadata fallback and formatter edge cases

## [0.25.2] - 2026-09-13

### Added
- Tag editing — `tag edit <id> <field> <value>` / `tag get <id>` with field validation (`cli/src/app/core.cppm:627`)

### Changed
- Output formatter JSON handling for tag/playlist data

## [0.25.1] - 2026-09-12

### Added
- Library add/remove — `library add <path> [--recursive]`, `library remove <id>` with metadata extraction and fingerprint
- Config reset — `config reset [key]` to restore defaults

### Fixed
- Queue `shuffle`/`repeat` toggle behavior — now correctly toggles when no arg provided

## [0.25.0] - 2026-09-12

### Added
- Playback history — `history list [--limit N] [--json]` / `history clear` (`Tag: history list/clear`)
- TUI preview polish — `tui` shows `status` + guidance, points to `status --watch` and future ratatui TUI (`cedac0f`)
- Status watch mode — `status --watch`/`--follow` with `--interval <ms>` for continuous polling (for scripts/TUI)

## [0.24.0] - 2026-09-11

### Added
- Multi-queue switch — `queue switch <qid>` with `active_queue_id` in engine, `QueueSwitch` IPC command and `QueueQueues` listing (`4b7e70d`)

### Fixed
- Queue shuffle/repeat — `fix queue shuffle/repeat toggle + add playlist rename/export/import` (`3c6f4e8`)
- Playlist operations — `playlist rename` / `export` (m3u/pls/json) / `import` (m3u)

## [0.23.2] - 2026-09-10

### Changed
- CMake: target-scoped sanitizers, deduplicated sqlite, export fix, `FindFFmpeg` + `ExternalProject` fallback (`02816e3`)
- Tests: renamed `helpers_test.hpp` → `common.hpp`, deduped `tempDbPath`/`safeRemoveDb`

### Fixed
- `test_reader` parallel race and compiler warnings (`6871ee1`)

## [0.23.1] - 2026-09-09

### Changed
- CMake: removed `CAUDIO_WITH_FFMPEG` in favor of `CAUDIO_WITH_FETCH_FFMPEG`, DRY component split (`ac6ad42`)
- Tests: removed `MockClock` and stub tests, redundant `create_directories` cleanup

## [0.23.0] - 2026-09-09

### Changed
- **BREAKING** CMake packaging and component split — `cmake/components/*.cmake`, `CAUDIO_WITH_FETCH_FFMPEG`, `FindFFmpeg` improvements
- Vendor: BLAKE3 dispatch fix, dead vendor files removed

## [0.22.0] - 2026-09-08

### Changed
- **BREAKING** C++23 module naming and thin-aggregator removal — `caudio.db`, `caudio.engine`, `caudio.utils` partition renames (`43506aa..aee0cf7`)
- Modernization per audit: RAII guards, `std::expected` error handling, `std::jthread`/`std::stop_token`, concept constraints

### Added
- Polyglot integration strategy (`docs/polyglot-integration.md`) — Rust metadata, Tauri GUI, Python `nanobind`, Go sidecar
- Single-module BMI build fix (`bbc1f47`) — `CMAKE_POSITION_INDEPENDENT_CODE ON`, static libs own BMIs, shared variants use `whole-archive`

## [0.21.0] - 2026-09-07

### Changed
- **BREAKING** C++23 type renames per audit — `StatusCode`, `Error`, `Result<T>`, `EngineState`, `QueueState` (`c69338e`)

### Added
- Engine/DB/IPC/Service split with SHM `AtomicShmStatus` and `IpcChannel` framing

## [0.20.3] - 2026-09-05

### Fixed
- Engine queue deadlock, `MpscQueue` hardening, `scan` encapsulation
- DB helper dedup, audio output hardening, CMake simplification

## [0.20.2] - 2026-09-05

### Changed
- FFmpeg-only player — removed `miniaudio`/`dr_*` decoders (`d499048`)

### Fixed
- FFmpeg streaming and playback (`ff41284`, `6da65f3`)

## [0.20.1] - 2026-09-04

### Fixed
- Player: cached file size in `FileReader`, seek thread-safety

## [0.20.0] - 2026-09-04

### Added
- Hybrid MP3 decoding (memory for small files, streaming for large) and FFmpeg provider cascade (system → vcpkg → prebuilt → source)

## [0.19.0] and earlier

See `git log --oneline --tags` and `git tag -n` for history:

- `v0.19.0` — Engine split into partitions, `MockClock` removed
- `v0.18.0` — Player `AudioOutput` with miniaudio backend
- `v0.16.x` — DB statement cache, `scan`/`search`/`json` modules
- `v0.14.0–v0.15.0` — BLAKE3 64K head-tail fingerprint, sanitizers/CI
- `v0.10.0–v0.13.x` — FFmpeg bring-up, hybrid streaming, vendor cleanup
- `v0.8.x–v0.9.x` — Module system introduction, `clang-format`
- `v0.3.0-modules` — Initial C++23 module migration

[0.25.5]: https://github.com/AhmedowM/caudio/releases/tag/v0.25.5
[0.25.4]: https://github.com/AhmedowM/caudio/compare/v0.25.3...v0.25.4
[0.25.3]: https://github.com/AhmedowM/caudio/compare/v0.25.2...v0.25.3
[0.25.2]: https://github.com/AhmedowM/caudio/compare/v0.25.1...v0.25.2
[0.25.1]: https://github.com/AhmedowM/caudio/compare/v0.25.0...v0.25.1
[0.25.0]: https://github.com/AhmedowM/caudio/compare/v0.24.0...v0.25.0
[0.24.0]: https://github.com/AhmedowM/caudio/compare/v0.23.2...v0.24.0
[0.23.2]: https://github.com/AhmedowM/caudio/compare/v0.23.1...v0.23.2
[0.23.1]: https://github.com/AhmedowM/caudio/compare/v0.23.0...v0.23.1
[0.23.0]: https://github.com/AhmedowM/caudio/compare/v0.22.0...v0.23.0
[0.22.0]: https://github.com/AhmedowM/caudio/compare/v0.21.0...v0.22.0
[0.21.0]: https://github.com/AhmedowM/caudio/compare/v0.20.3...v0.21.0
[0.20.3]: https://github.com/AhmedowM/caudio/compare/v0.20.2...v0.20.3
[0.20.2]: https://github.com/AhmedowM/caudio/compare/v0.20.1...v0.20.2
[0.20.1]: https://github.com/AhmedowM/caudio/compare/v0.20.0...v0.20.1
[0.20.0]: https://github.com/AhmedowM/caudio/releases/tag/v0.20.0
