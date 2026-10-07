#Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- Shorten component header names (`<comp>/core.hpp`, `<comp>/types.hpp`);
  public API unchanged, only include paths moved
- Shorten component source names (`<comp>/core.cpp`); no functional change
- Flatten module umbrellas to `modules/` root and shorten partition file
  names (`<comp>/core.cppm`, `<comp>/types.cppm`); the `caudio.player:core`
  partition drops its legacy `player_core` name
- Move version helpers from `caudio::utils` to their own `caudio::version`
  namespace (standalone `version.hpp`, like `config.hpp`)

## [v1.0.0-rc3] - 2026-10-04

### Added
- All v0.43.x CLI behavior program changes promoted to the 1.0.0-rc3
  release candidate: forgiving transport, volume mute-restore, natural-end
  stop, queue/list rework, queue add/remove/create/delete/load redesign,
  playlist create/add/load/import, library scan/search/stats, tag field
  selector, config import validation, play autostart, local config get,
  direct file play with temp queues.
- Direct play: bare `caudio PATH...` plays files immediately in a new
  queue (temporary, purged on daemon shutdown; `--save` keeps it).
  `play` and direct play autostart the daemon when down (quiet under
  `--json`). `config get` answers from local file without daemon.
- New commands: `queue tracks [--order added|playback]`, `queue create`,
  `queue delete`, `playlist create`, `playlist add [--id/--path]`,
  `playlist load --replace`, `tag get [FIELD]`, `playlist tracks --json`,
  `config get --json`, `queue add --playlist [--replace]`.
- New flags: `library stats --most-played/--queue/--playlist`,
  `library scan --full-hash`, `queue add --id/--playlist/--recursive`,
  `queue remove --id/--pos`, `tag get [FIELD]`, `config get --json`,
  `playlist add --id/--path`, `playlist load --replace`,
  `queue add --id/--playlist/--recursive/--replace`.

### Changed
- **Behavior**: Play after stop replays the stopped track; pause/resume/
  prev warn instead of failing when idle/at start; resume-when-stopped
  plays from the cursor; natural track end stops on repeat Off (manual
  `next` still wraps); volume mute/zero remembers the level for unmute
  (persisted), `Volume: muted` shown only when muted; bare `queue
  repeat` cycles off/all/one; `seek`/`config set` print nothing on
  success; duplicate adds warn without duplicating (server-side) and
  exit success; `playlist load` targets a new queue; `playlist save`
  tolerates legacy duplicate queue rows.
- **Output**: `queue list` lists queues (with active/temp markers),
  `queue queues` hidden alias; `queue tracks`, `playlist tracks`,
  search results and import reports use `Playlist`/`Queues` headers
  with counts; `library scan` reports added tracks; `library search`
  matches filenames with match highlighting; `config import` validates
  before replacing; switch/tag-get name missing ids; `--log-level`
  rejects bad values.
- **Removed**: `library scan --mode` (use `--full-hash`), `library stats
  --detailed`, bare-numeric `queue remove`, `queue add` bare-query FTS
  fallback (use `--search`); `preview` is deprecated (hidden from help,
  use direct play).

### Fixed
- `config list` returning zero entries (array-only indexed access on
  objects); relative-seek whitespace detection; `playlist tracks`
  header; rescan duplicating untouched files; `queue add` skipping
  metadata extraction; missing-file vs empty-glob errors; separator-
  insensitive path removal/import; negative `history --limit`; man page
  EXAMPLES/FILES garble plus new EXIT STATUS section.
- Fingerprint modes documented as incompatible (cross-mode rescans
  re-add); new scan rows store generic paths.

## [v0.43.2] - 2026-10-04

### Fixed
- Negative `history --limit` values fall back to the default instead of
  reaching SQL.

## [v0.43.1] - 2026-10-04

### Added
- `play` and direct play autostart the daemon when it is down (quiet
  under `--json`).
- `config get` answers from the local file; no daemon needed.

## [v0.43.0] - 2026-10-04

### Added
- Direct play: bare `caudio PATH...` plays files immediately in a new
  queue via a `PlayFiles` command (temporary and purged on daemon
  shutdown; `--save` keeps it permanently). Shared ensure-track helper
  backs the add paths. `preview` stays as a hidden legacy alias.
- `queue list` shows a Temp marker; `queues.temp` column with migration.

### Fixed
- `play` while playing restarts the current track instead of starting the
  new queue (direct-play flow stops first).

## [v0.42.0] - 2026-10-04

### Added
- `library scan --full-hash` replaces `--mode`; output is `X tracks
  added` only; rescan guard (path+mtime) stops untouched files from
  duplicating, including across fingerprint modes.
- `library search` matches filenames; table
  `id/title/artist/album/genre/filename` with query highlighting.
- `library stats --most-played N`, `--queue all|ID` (repeatable),
  `--playlist ID` (repeatable); `--detailed` removed.
- `library remove` confirmations name the track
  (`Removed from library <label>`); files on disk untouched.
- `playlist create NAME`, `playlist add PID (--id ID… | PATH…
  [--recursive])` with duplicate warns; `tag get [FIELD]` with raw or
  single-key JSON output.
- `playlist import` returns matched/skipped/duplicate counts in the CLI
  result (was daemon stderr only); separator-insensitive path matching.
- `config import` validates JSON shape and known-key types before atomic
  replace; `config get --json` returns `{key: value}`.
- `switch` and `tag get` name missing ids; `--log-level` rejects bad values.

### Changed
- Duplicate-only adds exit success (warn on stderr, no rows created).

### Fixed
- `playlist save` tolerates legacy duplicate queue rows.

## [v0.41.0] - 2026-10-04

### Added
- `queue add` takes repeatable file/folder/glob PATHs (sorted client-side
  expansion), `--id ID`, `--playlist ID [--replace]`, `--recursive`;
  missing library rows auto-added with metadata extraction; appended
  tracks extend the shuffle permutation; per-track `Added …` lines plus
  `X tracks added` total; empty globs hint on stderr with success.
- `queue remove` requires `--id` (library id) or `--pos` (queue
  position) — no bare numbers; also accepts PATH/glob/folder selectors;
  per-track `Removed from queue <…>` confirmations, no count.
- `queue create NAME` / `queue delete QID` (active queue guarded).
- `playlist load` targets a NEW queue (playback and current queue stay
  intact); `--replace` overwrites the active queue; `--play` switches
  and plays. `queue add --playlist` appends playlist tracks.

### Changed
- Server-side duplicate suppression: warn-only re-adds create no rows
  (previously message-only — the server still duplicated).

## [v0.40.0] - 2026-10-04

### Added
- `queue tracks [--order added|playback] [--json]` lists the active queue
  in playback (shuffle) order with a current-track marker (`>`, green on
  TTY); `--order added` shows insertion order.
- `queue list` now lists queues for real (ID/Name/Tracks/Active);
  `queue queues` stays as a hidden alias; new `Queues` IPC result.

### Changed
- Natural track end with repeat Off **stops** (cursor parked past end,
  later `play` wraps to head); manual `next` still wraps in every mode
  (`autoNext()`/`advanceLocked()` split in engine).

## [v0.39.0] - 2026-10-04

### Added
- Volume mute-restore model: `mute`/`volume 0` snapshots the level into
  persisted `pre_mute_volume`; `unmute` restores it (was fixed 50%).
  `Volume: muted` prints only when muted, else `Volume: N%`; `status`
  reports the real mute state (was hardcoded `no`).

### Changed
- Play after stop replays the stopped track (`stop()` rewinds the
  cursor; no-op when already stopped).
- `pause` warns `nothing playing` instead of failing when idle; `resume`
  plays from the cursor when stopped and warns `already playing` when
  playing; `prev` warns `at queue start` instead of failing.

### Fixed
- `config list` returning zero entries (array-only indexed access on JSON
  objects); relative-seek whitespace detection (`" +5"` applied as
  absolute); man EXAMPLES/FILES de-garbled plus new EXIT STATUS section
  (0/1/105/106/109).

## [v0.38.1] - 2026-10-04

### Added
- `seek` prints nothing on success; `config set` silent on success;
  `config get --json`; `playlist tracks --json` with `Playlist (N
  tracks):` header (was `Queue (N tracks):`).

### Changed
- Bare `queue shuffle` reports the resulting state; bare `queue repeat`
  cycles off→all→one→off and reports it (was no-op `Repeat toggled`).

## [v0.38.0] - 2026-10-03

### Added
- CLI confirmations instead of bare `OK`, remedy hints on daemon-down
  errors, valid grammars echoed on validation errors, compact watch
  lines, transport one-liners without ids/queue positions, `--json` on
  mutating commands.
- Hermetic golden test harness (`tests/test_cli_golden.cpp`) with unique
  temp dirs per run.

## [1.0.0-rc2] - 2026-10-02

### Added
- nlohmann/json v3.12.0 vendored in `vendor/nlohmann/` (`json.hpp`, `json_fwd.hpp`):
  eliminates ~180MB git clone on every configure, zero network, zero FetchContent
  logic. `CMakeLists.txt` simplified to single `CAUDIO_NLOHMANN_PRIVATE_INCLUDE`.

### Changed
- `GIT_SHALLOW TRUE` on all FetchContent declares (nlohmann_json, CLI11, Catch2):
  reduces fresh configure time from ~93s to ~15–25s on typical CI networks.

## [0.37.1] - 2026-10-02

### Fixed
- macOS/brew-LLVM build: libc++ ships neither `std::generator` nor
  `std::move_only_function`, so `db/scan` and `db/write_thread` now use
  in-tree `caudio::utils::Generator` (coroutine-based,
  `utils/generator.hpp`) and `caudio::utils::MoveOnlyFunction`
  (type-erased, `utils/function.hpp`)
- Brew libc++ toolchain hardened: `-cxx-isystem` (wins over FFmpeg
  `-isystem` regardless of flag order), `-stdlib=libc++` at compile and
  link time, versioned `libc++*.dylib` lookup
- CI macOS setup: `brew update` before install, LLVM >= 20 version probe;
  packages built as TGZ+ZIP

## [0.37.0] - 2026-10-01

### Added
- Compiler toolchain files (`cmake/toolchains/`: `gcc`, `clang`, `msvc`,
  host-default `auto`, shared brew-LLVM resolver) + `CompilerPolicy.cmake`:
  Apple/GCC and Apple/AppleClang fail at configure (dummy audio backend /
  C++23 gaps; `CAUDIO_ALLOW_UNSUPPORTED_COMPILER` escape hatch), MinGW warns
  on binary size
- Compiler preset variants (`dev`/`ci`/`modules`/`release`/`release-lto`/
  `minsize` × `-gcc`/`-clang`/`-msvc`) over hidden `*-base` purpose presets;
  bare presets now select the host-default compiler via `auto`; `custom`
  preset for arbitrary build dirs; `-msvc` presets use the Visual Studio
  2026 generator (no vcvars needed)
- CI matrix across compilers: Linux `ci-gcc`, macOS `ci-clang` (brew LLVM),
  Windows `ci-gcc` (MinGW) + `ci-msvc`

### Changed
- **BREAKING (install)** Library de-noise: `db/detail`, `db/fingerprint`,
  `db/fts`, `db/queue` internals, `db/schema`, `db/statement`,
  `db/stmt_helpers`, `db/transaction`, `engine/history`, `engine/shuffle`
  moved to `src/` (not installed); `_impl` splits merged; fat headers
  slimmed; `HistoryEntry` unified on `db::`; vendor headers no longer
  installed
- Uniform `component_core` naming (`client_core`, `service_core`,
  `engine_core`, module `:core` partitions)
- Clang targets the native MSVC ABI on Windows (MinGW `--target` override
  and `-fsized-deallocation` dropped after full verification)
- `vendor/` include dirs marked `SYSTEM` (vendored headers stay
  warning-clean under all compilers); `_CRT_SECURE_NO_WARNINGS` now also
  covers Clang-on-Windows

### Fixed
- Downstream module exports: `sqlite3`/`blake3` dangling
  `INSTALL_INTERFACE` vendor path removed (broke every `find_package`
  configure); deleted `service :paths`/`:status`/`:audio` partitions
  (wrapped non-installed `src/` internals, broke downstream BMI rebuild);
  full 7-module downstream import smoke green
- MSVC warnings: `#pragma GCC` guarded (`thread.cpp`), `lk` shadowing
  renamed (`write_thread.cpp`), `int64_t`→`int32_t` narrowing casts
  (`queue.cpp`)

## [0.36.0] - 2026-09-28

### Added
- `caudio::utils::Json` opaque JSON value type (`utils/json.hpp`, backend in
  `src/utils/json.cpp`, `modules/utils/json.cppm` partition): `Json`/`JsonRef`/`JsonError`,
  insertion-order preserving, throwing `get<T>` matching previous backend behavior

### Changed
- **BREAKING (build)** Vendored sqlite target renamed `caudio` -> `sqlite3`
  (artifact `libsqlite3.a` / `sqlite3.lib`, exported as `caudio::sqlite3`),
  content-named like `blake3`; the only `caudio`-named artifact is the opt-in
  shared umbrella (`combined` -> `libcaudio`)
- nlohmann/json is now private (backend for `src/utils/json.cpp` only):
  `db/json.hpp` + `ipc/protocol.hpp` signatures use `caudio::utils::Json`;
  `ipc::detail::trackToJson/trackFromJson` deleted in favor of `db::` versions;
  `fromJson<Result>` trap fixed; `JSON_Install`, `find_dependency(nlohmann_json)`
  and `CAUDIO_JSON_EXTRA_INCLUDE` removed -- `install()` ships zero JSON headers

## [1.0.0-rc1] - 2026-09-28

### Changed
- **BREAKING (build)** Vendored sqlite static lib renamed `caudio_sqlite` ->
  `caudio` (artifact `libcaudio.a` / `caudio.lib`, exported as `caudio::caudio`)
- Executable target renamed `caudio` -> `caudio_cli` (shipped binary is still
  `caudio` / `caudio.exe` via `OUTPUT_NAME`); shared `combined` still produces
  `libcaudio`
- Dependencies to latest: nlohmann/json 3.11.3 -> 3.12.0, CLI11 2.4.2 -> 2.7.2,
  Catch2 3.7.1 -> 3.16.0, sqlite 3.46.1 -> 3.53.4 (BLAKE3 1.8.7 and miniaudio
  0.11.25 already current)

## [0.35.2] - 2026-09-27

### Fixed
- CPack output pinned to the build dir (`CPACK_OUTPUT_DIRECTORY`); packages
  previously landed in the repo root and CI uploaded a raw `caudio.exe`
- Windows packages now bundle runtime DLLs next to the executable (FFmpeg
  `avcodec/avformat/avutil/swresample` + MinGW `libstdc++/libgcc/libwinpthread`),
  so archives run on bare machines
- Release uploads accept only `*.tar.gz` / `*.zip` and fail if missing

## [0.35.1] - 2026-09-27

### Removed
- `tui` stub subcommand (+ stale "planned v0.28.0" promises in README/man)

### Added
- `VERSION` file + precedence (`-DCAUDIO_PROJECT_VERSION` -> git tag ->
  `VERSION` file -> `0.1.0`+warning); `VERSION`-vs-tag drift guarded in CI
- `CAUDIO_BUILD_CLI` option (OFF skips CLI11 fetch + executable)
- `caudio::print`/`println` facade (`utils/print.hpp`): works around missing
  `std::__open_terminal`/`__write_to_terminal` in msys2 MinGW GCC 16 libstdc++
- `tests/sanitizers/lsan.supp`: narrow suppression for the FFmpeg
  probe-buffer drip (system libavutil, tracked, all other reports fatal)

### Changed
- **BREAKING (build)** Shared `*_shared`/`combined` targets gated behind
  `CAUDIO_BUILD_SHARED=OFF` by default
- Components declare `PUBLIC cxx_std_23` (downstream module consumers need it)
- `checkPidAlive` includes `<signal.h>`; `truncate15` defined in-header
- macOS: `pthread_setname_np` Apple form, `MA_NO_COREAUDIO` for GNU builds
- CI on presets; `--allow-multiple-definition` skipped on Darwin; Windows
  toolchain resolved via msys2-location; FFmpeg DLLs on PATH for tests
- Tree normalized to ASCII (BOMs stripped, mojibake fixed)
- clangd pointed at the preset compile database (`.vscode/settings.json`)

## [0.35.0] - 2026-09-27

### Added
- `CMakePresets.json`: 19 configure presets (`default`, `dev`, `ci`, `ci-sanitizers`, `modules`, `minimal`, `shared`, `release`, `release-lto`, `minsize`, `docs`, `all`, plus `-no-cli` splits, `minsize-lto`, `release-native`); build + test presets; CI migrated to `cmake --preset`
- New options: `CAUDIO_BUILD_SHARED` (default OFF -- gates `*_shared` + `combined` whole-archive maze), `CAUDIO_BUILD_CLI` (default ON -- OFF skips CLI11 fetch + executable), `CAUDIO_REQUIRE_GIT_VERSION` (default OFF -- fails fast with no git tag)
- Lazy FetchContent: `find_package(nlohmann_json/CLI11)` first, fetch only as fallback

### Changed
- **BREAKING (build)** Shared `*_shared`/`combined` targets no longer built by default; opt in with `-DCAUDIO_BUILD_SHARED=ON`
- Tag-less configure emits a loud warning (fallback `0.1.0` is not a release build)
- `test_player_integration` now defines `TEST_DATA_DIR` (fixes fixture lookup in nested `build/<preset>` trees)
- Removed stale `e.g. "v0.25.4"` version strings from `version.hpp` / `version.hpp.in` / `ipc/result.hpp`
- `engine.hpp` slimmed 21 -> 12 includes
- Private `config::detail` moved to `src/config_detail.hpp`; stale test narration deleted
- Removed `tui` stub subcommand (+ stale "planned v0.28.0" promises in README/man)
- Version precedence: `-DCAUDIO_PROJECT_VERSION` -> git tag -> `VERSION` file -> `0.1.0`+warning; `VERSION` drift guarded in CI

## [0.34.3] - 2026-09-27

### Changed
- nlohmann/json propagated via installed package (`find_dependency` + PUBLIC link); `json_fwd` in public headers
- Decoder collapsed to a single pImpl class (`IDecoder`/`DecoderRegistry`/`decoder_common` deleted)
- Windows TUs include real `<windows.h>` first (hand-rolled declarations removed)
- Hygiene: pruned duplicates, stale refs, warning leaks

## [0.34.2] - 2026-09-26

### Removed
- Deleted unused `ipc_channel_unix.cpp` / `ipc_channel_win.cpp` implementations

## [0.34.1] - 2026-09-26

### Changed
- Module interface units moved to `modules/` mirroring `src/`; backfilled 0.25.6-0.34.0 release notes

## [0.34.0] - 2026-09-26

### Changed
- **BREAKING** Library namespace `caudio::cli` split into `caudio::ipc` (commands, results, protocol) and `caudio::config` (config, paths); module `caudio.cli` renamed to `caudio.ipc`

## [0.33.0] - 2026-09-26

### Changed
- **BREAKING** `RepeatMode::Queue` renamed to `All` (matches CLI `repeat all` and JSON `"all"`)
- Docs: refreshed references, split CI smoke into header-mode and modules-ON jobs

## [0.32.1] - 2026-09-26

### Changed
- CMake: single test helper, factored install rules, `FindFFmpeg.cmake` ships with the package, docs moved to `cmake/Docs.cmake`, TGZ/ZIP-only CPack
- CMake: removed `WITH_FFMPEG` fiction (FFmpeg is unconditionally required) and `CAUDIO_ENABLE_CCACHE` option

## [0.32.0] - 2026-09-26

### Changed
- Hex helpers `toHex`/`fromHex` moved from `db::internal` to `caudio::utils`

## [0.31.1] - 2026-09-26

### Changed
- Service split: 1200-line `dispatch()` divided into `dispatch_playback/queue/library/config.cpp` via `handle()` overloads; `service_detail` divided into `service_paths`/`service_status`/`service_audio`; duplicate fingerprint/extension helpers removed

## [0.31.0] - 2026-09-26

### Changed
- Public headers no longer include `sqlite3.h`/`blake3.h`/`miniaudio.h` (forward declarations, `AudioOutput` pImpl, `SqliteCloser`)
- `IDecoder`, `Service`, `Client` promoted to public headers; test-only `AudioOutput` methods removed
- Added `caudio::utils::clampVolume` (`utils/math.hpp`)

## [0.30.0] - 2026-09-26

### Changed
- **BREAKING** CMake library `cli_shared` renamed to `ipc` (`caudio::ipc`); CLI app code compiles into the executable only

## [0.29.0] - 2026-09-26

### Changed
- **BREAKING** Removed redundant version accessors (`Engine`/`Database`/`caudio::version()`, `gitHash()`); only `caudio::utils` helpers and raw constants remain
- Removed pure-forwarder `OutputFormatter` string helpers

## [0.28.0] - 2026-09-26

### Changed
- **BREAKING** Removed compat shims (`ShuffleMode`, `getState`/`getPosition`, `attachDb`, `getCached` pair, `getTrackName`/`listTracksSimple`, span-deprecated `bindBlob`, C-string `Error`, service `socketPathFor`, hex wrappers)
- `shouldMarkPlayedEx` renamed to `shouldMarkPlayed`

## [0.27.1] - 2026-09-26

### Fixed
- Build: removed self-nesting `src/caudio` junction; private headers use relative includes

## [0.27.0] - 2026-09-25

### Changed
- Promoted umbrella include paths (`<caudio.hpp>`, `<caudio/db.hpp>`, ...), privatized detail headers, unified angle-bracket includes
- C++23 modules are now opt-in (`CAUDIO_ENABLE_MODULES=OFF` by default); headers are canonical for tests, CLI and examples

## [0.26.1] - 2026-09-22

### Fixed
- IPC: restored `IpcServer` framing, bound client timeout worker leak, removed CLI11 from public header

### Changed
- Reusable IPC/service/client code moved to `include/caudio`, flattened `json` usage to `nlohmann::ordered_json`

## [0.26.0] - 2026-09-21

### Changed
- Dual header/source layout introduced: public headers installed, implementations moved to `.cpp` files across utils/db/engine/player/service/client/CLI

## [0.25.6] - 2026-09-15

### Changed
- Tests: `CAUDIO_TEST_NOAUDIO` build flag + env switch, dummy ring helper
- Internal: string-view `Error` construction; CI switched to GCC

## [0.25.5] - 2026-09-15

### Added
- `docs` + `man` + `service` packaging -- `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `docs/man/caudio.1` (roff), `packaging/caudio.service` (systemd user unit); Doxygen docs wiring via `docs/Doxyfile.in`
- Packaging pipeline -- install `caudio` binary, C++23 module sources (`src/` + `cli/src/` for BMI rebuild), man page, systemd unit; `CPack` per-platform generators (`TGZ`/`ZIP` + `DEB`/`RPM` on Linux, `NSIS` on Windows, `DragNDrop` on macOS)

### Changed
- Version wiring to `import caudio.utils` -- `cmake/version.hpp.in` (moved from `version.hpp.in`), `src/utils/version.cppm` (`version()`/`versionString()`/`shortVersion()`/`versionCommit()`), `Engine::version()`/`Database::version()` + `staticVersion()`, `Status.version` via `service_detail::buildStatus` + `protocol` + `output_formatter`, CLI `--version` now `versionFull` (`v0.25.5`) (`301f7c8`)

### Fixed
- `cmake/version.hpp.in` clang-format fix

## [0.25.4] - 2026-09-14

### Added
- Comprehensive Doxygen documentation for all public APIs (`2331e88`)

## [0.25.3] - 2026-09-13

### Added
- `library list` -- filtered library listing with `--query`/`--artist`/`--album`/`--genre`/`--limit`/`--offset`/`--json`
- `library stats --detailed` -- detailed stats with most-played and total play time (`LibraryStatsDetailed`)
- `info` -- current track info with metadata and play statistics (`Info` command)

## [0.25.2] - 2026-09-13

### Fixed
- Metadata extraction + display fallback for missing tags (`b499553`)

## [0.25.1] - 2026-09-13

### Added
- Device management -- `device list`/`set`/`test` (miniaudio device enumeration)

## [0.25.0] - 2026-09-13

### Added
- Playback history -- `history list [--limit N] [--json]` / `history clear`
- TUI preview polish -- `tui` shows `status` + guidance (`cedac0f`)

## [0.24.1] - 2026-09-13

### Added
- Multi-queue switch -- `queue switch <qid>` with `active_queue_id` in engine, `QueueSwitch` IPC command and `QueueQueues` listing (`4b7e70d`)

## [0.24.0] - 2026-09-13

### Added
- Library add/remove -- `library add <path> [--recursive]`, `library remove <id>` with metadata extraction and fingerprint
- Tag editing -- `tag edit <id> <field>` / `tag get <id>` with field validation
- Config reset -- `config reset [key]` to restore defaults
- Status watch mode -- `status --watch`/`--follow` with `--interval <ms>` for continuous polling

## [0.23.3] - 2026-09-13

### Added
- Playlist operations -- `playlist rename` / `export` (m3u/pls/json) / `import` (m3u)

### Fixed
- Queue shuffle/repeat -- correctly toggles when no arg provided (`3c6f4e8`)

## [0.23.2] - 2026-09-12

### Changed
- CMake: target-scoped sanitizers, deduplicated sqlite, export fix, `FindFFmpeg` + `ExternalProject` fallback (`02816e3`)
- Tests: renamed `helpers_test.hpp` -> `common.hpp`, deduped `tempDbPath`/`safeRemoveDb`

### Fixed
- `test_reader` parallel race and compiler warnings (`6871ee1`)

## [0.23.1] - 2026-09-12

### Changed
- CMake: removed `CAUDIO_WITH_FFMPEG` in favor of `CAUDIO_WITH_FETCH_FFMPEG`, DRY component split (`ac6ad42`)
- Tests: removed `MockClock` and stub tests, redundant `create_directories` cleanup

## [0.23.0] - 2026-09-12

### Changed
- Vendor: BLAKE3 dispatch fix, dead vendor files removed, README added

## [0.22.0] - 2026-09-12

### Changed
- **BREAKING** C++23 module moves per audit (`8d935af`, `aee0cf7`)
- **BREAKING** C++23 module naming and thin-aggregator removal -- `caudio.db`, `caudio.engine`, `caudio.utils` partition renames (`43506aa`)

## [0.21.0] - 2026-09-12

### Changed
- **BREAKING** C++23 type renames per audit -- `StatusCode`, `Error`, `Result<T>`, `EngineState`, `QueueState` (`c69338e`)
- Modernization per audit: RAII guards, `std::expected` error handling, `std::jthread`/`std::stop_token`, concept constraints

### Added
- Polyglot integration strategy (`docs/polyglot-integration.md`) -- Rust metadata, Tauri GUI, Python `nanobind`, Go sidecar
- Single-module BMI build fix (`bbc1f47`) -- `CMAKE_POSITION_INDEPENDENT_CODE ON`, static libs own BMIs, shared variants use `whole-archive`

## [0.20.0] - 2026-09-11

### Added
- IPC layer: channel abstraction, server/client modules, single framing layer, canonical socket paths
- Client SDK: `IpcClient` wrapper and output formatter
- Service daemon: owns Engine/DB, dispatches library/playlist/config commands, flock single-instance, SHM status at 10 fps, ordered_json config
- CLI: App dispatch and main entry, background `start` with daemon flag

### Fixed
- SHM lock-free atomics, Windows single-instance, module hygiene, spawn portability, pipe fixes

## [0.19.2] - 2026-09-10

### Fixed
- DB/service: batch scan and queue transactions

## [0.19.1] - 2026-09-10

### Fixed
- DB/log: unify cache API, queue hygiene, search lock, log clean

## [0.19.0] - 2026-09-10

### Fixed
- Engine/ring: harden withTransaction, seek, ring memory order

## [0.18.2] - 2026-09-10

### Fixed
- Engine: next wraps/reshuffles at end, play when Playing serializes

## [0.18.1] - 2026-09-10

### Fixed
- Engine: play resumes when paused, queue persists via cursor, prev for non-shuffle

## [0.18.0] - 2026-09-10

### Fixed
- Engine: align decodeLoop and preroll sample vs frame with player
- Engine/player: keep queue on peek for status, start audio output
- DB: bypass queue statement cache, include sqlite error

## [0.17.0] - 2026-09-10

### Fixed
- CLI: start spawn robust with longer poll and correct CreateProcess
- Service: ensure db parent dirs exist and default path is writable
- CLI: use config for db path, fix Windows pipe path
- Service: named pipe on Windows for socket path
- Queue: handle file paths directly

## [0.16.0] - 2026-09-10

### Added
- Daemon: flock single-instance, SHM status at 10 fps

### Fixed
- Service: disable flock on Windows, rely on socket bind
- CLI: run start in background, add daemon flag

### Tests
- Daemon command roundtrip and playback control tests

## [0.15.0] - 2026-09-09

### Added
- IPC: shared command/result/protocol modules
- IPC: channel abstraction and server/client modules
- Client: IpcClient wrapper and output formatter
- Service: own Engine/DB and dispatch commands
- CLI: App dispatch and main entry
- Service: wire library/playlist/config dispatch

## [0.14.0] - 2026-09-08

### Fixed
- Engine: serialize seek with decode loop to prevent race

### Changed
- DB/utils/engine: dedup helpers, add RAII, modernize thread/log
- DB/utils: split queue partition, restrict MpscQueue copy, simplify atomics
- Build/player/engine: clean warnings and ownership

### Tests
- Expanded coverage for db, output, scan, search and engine

## [0.13.0] - 2026-09-07

### Fixed
- Engine queue deadlock, `MpscQueue` hardening, `scan` encapsulation
- DB helper dedup, audio output hardening, CMake simplification

## [0.12.0] - 2026-09-06

### Changed
- DB: split database monolith into internal partitions; fix sanitizeFtsTerm and exports

## [0.11.2] - 2026-09-06

### Fixed
- Player: correct sample vs frame count in audio callback
- Player: cached file size in `FileReader`, seek thread-safety

## [0.11.1] - 2026-09-06

### Fixed
- Player: correct FfmpegDecoder AVIO cleanup double-free

## [0.11.0] - 2026-09-06

### Added
- DB/player: BLAKE3 64K head-tail fingerprint
- DB: cache prepared statements per connection
- Player: facade with jthread decode and miniaudio output
- DB: harden WriteOp RAII and wire Database flush + write batch size

## [0.10.0] - 2026-09-06

### Changed
- Engine: split monolithic module into partitions

### Added
- CI: sanitizers and linting
- Tests: engine/db/player parity and review fixes
- Player: fix FFmpeg streaming and playback

## [0.9.0] - 2026-09-05

### Changed
- Player: FFmpeg-only -- removed `miniaudio`/`dr_*` decoder leftovers
- Build: FFmpeg provider cascade (system -> vcpkg -> prebuilt -> source)

### Fixed
- Player: FFmpeg streaming and playback, hybrid streaming for all formats
- Player: miniaudio streaming cleanup, remove beep

## [0.8.0] - 2026-09-05

### Added
- DB/engine: complete core modules
- Player: hybrid MP3 decoding (memory for small files, streaming for large)

## [0.7.0] - 2026-09-05

### Added
- Player: miniaudio device playback with 440Hz fallback
- Examples: `caudio_mini` example with miniaudio decoder playback

## [0.6.0] - 2026-09-05

### Changed
- Player: `dr_*`/`stb_vorbis` replaced with miniaudio `ma_decoder` as primary

## [0.5.0] - 2026-09-05

### Added
- DB/player/utils: AudioOutput, database modules, and test fixes
- Player: output module import (GCC 14.2 module ICE workaround)

## [0.4.0] - 2026-09-04

### Added
- Build: CMake 3.28 modules scaffold
- Utils: Result, Arena, Ring, Queue ports
- Player: Reader and DecoderRegistry with dr fallbacks, drwav unification
- Player: FFmpeg primary decoder with dr fallback
- Tests: real vorbis fixture

[0.34.0]: https://github.com/AhmedowM/caudio/compare/v0.33.0...v0.34.0
[0.33.0]: https://github.com/AhmedowM/caudio/compare/v0.32.1...v0.33.0
[0.32.1]: https://github.com/AhmedowM/caudio/compare/v0.32.0...v0.32.1
[0.32.0]: https://github.com/AhmedowM/caudio/compare/v0.31.1...v0.32.0
[0.31.1]: https://github.com/AhmedowM/caudio/compare/v0.31.0...v0.31.1
[0.31.0]: https://github.com/AhmedowM/caudio/compare/v0.30.0...v0.31.0
[0.30.0]: https://github.com/AhmedowM/caudio/compare/v0.29.0...v0.30.0
[0.29.0]: https://github.com/AhmedowM/caudio/compare/v0.28.0...v0.29.0
[0.28.0]: https://github.com/AhmedowM/caudio/compare/v0.27.1...v0.28.0
[0.27.1]: https://github.com/AhmedowM/caudio/compare/v0.27.0...v0.27.1
[0.27.0]: https://github.com/AhmedowM/caudio/compare/v0.26.1...v0.27.0
[0.26.1]: https://github.com/AhmedowM/caudio/compare/v0.26.0...v0.26.1
[0.26.0]: https://github.com/AhmedowM/caudio/compare/v0.25.6...v0.26.0
[0.25.6]: https://github.com/AhmedowM/caudio/compare/v0.25.5...v0.25.6
[0.25.5]: https://github.com/AhmedowM/caudio/releases/tag/v0.25.5
[0.25.4]: https://github.com/AhmedowM/caudio/compare/v0.25.3...v0.25.4
[0.25.3]: https://github.com/AhmedowM/caudio/compare/v0.25.2...v0.25.3
[0.25.2]: https://github.com/AhmedowM/caudio/compare/v0.25.1...v0.25.2
[0.25.1]: https://github.com/AhmedowM/caudio/compare/v0.25.0...v0.25.1
[0.25.0]: https://github.com/AhmedowM/caudio/compare/v0.24.1...v0.25.0
[0.24.1]: https://github.com/AhmedowM/caudio/compare/v0.24.0...v0.24.1
[0.24.0]: https://github.com/AhmedowM/caudio/compare/v0.23.3...v0.24.0
[0.23.3]: https://github.com/AhmedowM/caudio/compare/v0.23.2...v0.23.3
[0.23.2]: https://github.com/AhmedowM/caudio/compare/v0.23.1...v0.23.2
[0.23.1]: https://github.com/AhmedowM/caudio/compare/v0.23.0...v0.23.1
[0.23.0]: https://github.com/AhmedowM/caudio/compare/v0.22.0...v0.23.0
[0.22.0]: https://github.com/AhmedowM/caudio/compare/v0.21.0...v0.22.0
[0.21.0]: https://github.com/AhmedowM/caudio/compare/v0.20.0...v0.21.0
[0.20.0]: https://github.com/AhmedowM/caudio/releases/tag/v0.20.0
[0.19.2]: https://github.com/AhmedowM/caudio/compare/v0.19.1...v0.19.2
[0.19.1]: https://github.com/AhmedowM/caudio/compare/v0.19.0...v0.19.1
[0.19.0]: https://github.com/AhmedowM/caudio/compare/v0.18.2...v0.19.0
[0.18.2]: https://github.com/AhmedowM/caudio/compare/v0.18.1...v0.18.2
[0.18.1]: https://github.com/AhmedowM/caudio/compare/v0.18.0...v0.18.1
[0.18.0]: https://github.com/AhmedowM/caudio/compare/v0.17.0...v0.18.0
[0.17.0]: https://github.com/AhmedowM/caudio/compare/v0.16.0...v0.17.0
[0.16.0]: https://github.com/AhmedowM/caudio/compare/v0.15.0...v0.16.0
[0.15.0]: https://github.com/AhmedowM/caudio/compare/v0.14.0...v0.15.0
[0.14.0]: https://github.com/AhmedowM/caudio/compare/v0.13.0...v0.14.0
[0.13.0]: https://github.com/AhmedowM/caudio/compare/v0.12.0...v0.13.0
[0.12.0]: https://github.com/AhmedowM/caudio/compare/v0.11.2...v0.12.0
[0.11.2]: https://github.com/AhmedowM/caudio/compare/v0.11.1...v0.11.2
[0.11.1]: https://github.com/AhmedowM/caudio/compare/v0.11.0...v0.11.1
[0.11.0]: https://github.com/AhmedowM/caudio/compare/v0.10.0...v0.11.0
[0.10.0]: https://github.com/AhmedowM/caudio/compare/v0.9.0...v0.10.0
[0.9.0]: https://github.com/AhmedowM/caudio/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/AhmedowM/caudio/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/AhmedowM/caudio/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/AhmedowM/caudio/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/AhmedowM/caudio/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/AhmedowM/caudio/releases/tag/v0.4.0
