# caudio C++23 Codebase Review

**Date:** 2026-09-27 (UTC) — all addressed entries removed; only open (deferred/undone) items remain.
**Scope:** `CMakeLists.txt`, `cmake/*.cmake`, `cmake/components/*.cmake`, `include/caudio/**/*.hpp` (+ `include/caudio.hpp`), `src/**/*.cpp` (+ `src/**/*.cppm` sampled), `cli/src/**/*`, `tests/common.hpp` + sample of `tests/*.cpp`, `examples/*.cpp`, `README.md`, `CONTRIBUTING.md`, `.github/workflows/ci.yml`, `.gitignore`, `cmake/version.hpp.in`, `cmake/caudioConfig.cmake.in`, `docs/man/caudio.1` + `docs/polyglot-integration.md` (sampled). Headers treated as canonical; modules (`*.cppm`) opt-in via `CAUDIO_ENABLE_MODULES=OFF` default. Large directories sampled via glob/grep + targeted reads, not every file fully read.

## Executive summary

Fifty-one of the original 53 findings were addressed across twelve batches (header detox, shim deletion, service split, utils consolidation, cmake/docs cleanup, `cli` → `ipc`/`config` namespace rename, module gating, decoder collapse, Windows-declaration fix, engine header slim, shared-lib gating, version/fetch/comment cleanup). Two items remain open by explicit decision plus two tracked third-party issues — no critical blockers.

**Counts by severity (4 open findings):**

| Severity | Count |
|---|---|
| Critical | 0 |
| Major | 0 |
| Minor | 3 |
| Nit | 1 |
| **Total** | **4** |

---

## 1. KISS violations (remaining)

| ID | Severity | Location | Finding | Suggested fix |
|---|---|---|---|---|
| KISS-4 | Minor | `src/client/output_formatter.cpp:48-~260` | `OutputFormatter::print` is a long `if constexpr (is_same_v<T, …>)` chain over ~10 `Result` alternatives. | Use `std::visit(overloaded{…})` or a table of formatters; one case per small function. **Deferred by user decision:** formatting logic is behavior-sensitive; needs output-golden tests first. |

---

---

## 2. Naming (declined)

| ID | Severity | Location | Finding | Suggested fix |
|---|---|---|---|---|
| NAME-7 | Nit | `include/caudio/engine.hpp:132-133` (`ExpectedVoid`, `ExpectedEngine`), `include/caudio/engine/history.hpp:110-112`, `src/player/player_core.cpp:26` (`Player::ExpectedVoid`) | Per-class `ExpectedVoid` aliases triple-define the same `std::expected<void, Error>`. | One `using ExpectedVoid = utils::Expected<void>;` in `utils/error.hpp` if ever touched again; delete per-class aliases. **Declined by user decision:** zero-cost aliases, churn would span module files. |

---

## 3. Known third-party issues (tracked, not ours)

| ID | Severity | Location | Finding | Status |
|---|---|---|---|---|
| EXT-1 | Minor | `src/player/decoders/ffmpeg.cpp` (`Decoder::init`, `extractMetadata`) + `tests/sanitizers/lsan.supp` | System FFmpeg (Ubuntu 24.04 libavutil, 6.x) leaks one 8192-byte probe buffer per `avformat_open_input` via `av_realloc_f`, on success and failure paths. Our lifecycle (alloc/open/close, AVIO setup/teardown, codec/swr alloc+free, packet/frame/layout RAII) is balanced; all affected tests pass functionally; zero UBSan/ASan-OOB reports. Suppressed narrowly (`leak:av_realloc_f` — caudio never calls it, so nothing of ours is masked). | Revisit with a Linux+ASan environment or newer system FFmpeg. |
| EXT-2 | Minor | `include/caudio/utils/print.hpp`, `src/**/dispatch_*.cpp`, `cli/src/app/core.cpp` | msys2 MinGW GCC 16.x `libstdc++` lacks `std::__open_terminal`/`std::__write_to_terminal`, so every `std::print` stream/`FILE*` overload fails at link. Worked around with the `caudio::print`/`println` facade (format+insert on MinGW, plain forward elsewhere). | Drop the MinGW branch if a future msys2 build ships the symbols. |

---

## 4. FFmpeg trimming plan (post-1.0 packaging track, approved)

Inventory below is from our exact build (Gyan 8.1.2 full: 215 audio + 274 video
decoders, 131 demuxers, 131 muxers, 40 protocols, 50 bsfs, 578 filters).
Decode-only product: tag editing writes SQLite rows, never audio files, so no
muxers/encoders are needed. Supported formats = whatever demuxers stay enabled
(scanner probes via FFmpeg, no hardcoded extension list).

| Category | Full size | Verdict |
|---|---|---|
| avcodec/avformat/avutil/swresample | 4 libs | **Keep** — the entire backend |
| avfilter (578 filters, 111 audio), avdevice, swscale | 3 libs + tools | **Drop** — resample via libswresample directly; I/O is miniaudio's job. *Future: server-side EQ/transcode pipelines only* |
| Programs (ffmpeg/ffplay/ffprobe) | 3 binaries | **Drop** — libs only |
| Video decoders (274), subtitle (22) | — | **Drop all.** Not a video player; cover art needs mjpeg/png only, if ever |
| Audio decoders (215) | — | **Keep ~40**: pcm_* family, adpcm_ms, adpcm_ima_wav, flac, mp3float, vorbis, opus, aac, aac_latm, alac, ape, musepack7/8, wavpack, tta, tak, shorten, wmalossless, wmapro, wmav1/2, ac3, eac3, dts, truehd, mlp, dsd_lsbf/msbf(+planar), amr_nb/wb, speex, gsm. Drop ~170: game audio, telephony, retro, dead formats |
| Encoders (81 audio + 104 video) | — | **Drop all.** *Future: transcode/file-tag-write product decisions only* |
| Demuxers (131) | — | **Keep ~28**: wav, w64, aiff, au, caf, flac, mp3, ogg, opus, mov/mp4/m4a, aac, asf, ape, mpc, mpc8, wv, tta, amr, matroska-audio, ac3, eac3, dts, truehd, mlp, spdif, voc, ircam, sox, gsm, sbc. Drop ~100 video/subtitle/game/telecom/streaming containers |
| Muxers (131) | — | **Drop all** (same future as encoders) |
| Parsers | — | Keep aac, aac_latm, ac3, dca, flac, mpegaudio, opus, vorbis, tak |
| Protocols (40) | — | Keep `file` + `pipe`; `--disable-network` kills http/hls/rtmp/tls/srt/ftp/ssh + the gnutls/openssl dependency subtree. *Future: internet radio = re-enable http/https/hls + TLS* |
| Bitstream filters (50) | — | Keep aac_adtstoasc (raw .aac!), opus_metadata, truehd_core, eac3_core, dca_core, pcm_rechunk, dump_extra; drop ~16 video/metadata ones |
| HWaccels (cuda/vaapi/dxva2/vulkan…) | — | **Drop all** (software decode; drops SDK chains). *Future: only on decode-CPU complaints* |
| External libs (~40: x264, bluray, srt, ssh, fonts…) | — | Keep `zlib` (mov needs it); drop the rest |
| Docs/programs/debug | — | `--disable-doc --disable-programs --disable-debug` |

Safety property: explicit `--enable-*` whitelist fails `configure` loudly on
missing deps — never silently drops a format. Expected payoff: ~30-60 MB of
DLLs → ~8-15 MB, smaller attack surface, one FFmpeg everywhere instead of
system/brew/Gyan variance. License unchanged (already shipping FFmpeg).

Phasing: (1) fixtures for mp3/flac/m4a/opus/wma + decode cases so CI guards
the whitelist (valuable regardless); (2) `cmake/FFmpegTrimmed.cmake` flag set
+ dedicated per-OS `ffmpeg` CI job with `actions/cache` (key: flags hash +
version), Windows first (replaces the ~500 MB Gyan fetch; builds consume via
`FFmpeg_ROOT` with fallback to current behavior so trimming outages never red
the pipeline); (3) all-OS trimmed post-1.0. Shared libs throughout (no new
LGPL burden). Full-static stays rejected (see prior discussion).

---

## 5. Dependency audit (2026-09-27) — versions, bumps, trims

> Applied 2026-09-28 (v1.0.0-rc1): nlohmann v3.12.0, CLI11 v2.7.2,
> Catch2 v3.16.0, sqlite 3.53.4. BLAKE3/miniaudio already current. Trims
> (sqlite OMIT set, miniaudio MA_NO_* set) still pending -- §4 track.

| Dep | Current | Fetch | Latest stable | Verdict |
|---|---|---|---|---|
| nlohmann/json | 3.11.3 | FetchContent fallback | **3.12.0** (backward-compat) | Bump tag; low risk (`ordered_json` + fwd, no UDLs) |
| CLI11 | 2.4.2 | FetchContent fallback | **2.7.2** (header-only mode unchanged) | Bump tag; low risk (no `prefix_command` use, so the 2.6.2 behavior change can't bite) |
| Catch2 | 3.7.1 | FetchContent (tests) | **3.16.0** (same major; verified via ls-remote) | Bump tag; low risk |
| sqlite3 | 3.46.1 | Vendored amalgamation | **3.53.4** (skip withdrawn 3.52.0) | Drop-in swap, public domain |
| BLAKE3 | 1.8.7 | Vendored (portable) | 1.8.7 | Current. Inverse option: SIMD files for ~10x fingerprint speed (size up); stay portable |
| miniaudio | 0.11.25 | Vendored header | 0.11.25 | Current. Watch: **0.12 splits `.c`/`.h`**, ending `MINIAUDIO_IMPLEMENTATION` |
| FFmpeg | 6.x-9.x varies | system/prebuilt/source | Rolling | Covered by §4 trim track |

Audited trims (verified against actual API usage, not guesses):
- **sqlite3** (27 APIs used; WAL + `foreign_keys=ON` + FTS5 required; zero JSON1
  SQL uses, no `create_function`/blob-I/O/backup/authorizer/extensions):
  `SQLITE_OMIT_LOAD_EXTENSION` (also hardening), `SQLITE_OMIT_DEPRECATED`,
  `SQLITE_OMIT_AUTHORIZER`, `SQLITE_OMIT_PROGRESS_CALLBACK`,
  `SQLITE_OMIT_GET_TABLE`, `SQLITE_OMIT_JSON` (~5-15% off; FTS5 dominates).
  Optional second wave: `OMIT_UTF16/SHARED_CACHE/DECLTYPE/TRACE/COMPLETE`.
- **miniaudio** (18 APIs, device playback + enumeration only; zero
  decoder/encoder/engine/node/graph/waveform calls): `MA_NO_DECODING`,
  `MA_NO_ENCODING`, `MA_NO_GENERATION`, `MA_NO_RESOURCE_MANAGER`,
  `MA_NO_NODE_GRAPH`, `MA_NO_ENGINE`, `MA_NO_WAV/FLAC/MP3`. Keep all backends
  (portability) and device I/O. Expect ~40-60% smaller object.
- nlohmann/CLI11/Catch2: header/test-only, nothing to trim.

---

## 6. rc2/rc3 goals — library de-noise + CLI polish (marked 2026-09-28)

Phase: rc1 is feature-complete (library + CLI working). rc2 = de-noise the
public install + CLI polish; rc3 = freeze. De-noise is API-breaking by
design (headers move out of the install) — document in CHANGELOG.

### 6.1 Library de-noise (audited 2026-09-28: 50 headers, include+symbol use)

Rule: `install(DIRECTORY include/...)` ships everything with no excludes,
so anything under `include/` IS the SDK. Internals must move to `src/`
(same pattern as `src/config_detail.hpp`); `modules/*.cppm` mirrors get
the same treatment (internal partitions installed today).

Move to `src/` (consumed only by our own TUs / inline code in siblings):

| Header | Evidence |
|---|---|
| `db/detail.hpp` | Re-export hub for `internal::` helpers; docstring even says "keep installed" — the chain to break, not bless |
| `db/stmt_helpers.hpp` | `internal::` guards/row-mapper/col-list; 0 external users |
| `db/fingerprint.hpp`, `db/fts.hpp` | `internal::` helpers; 0 external users |
| `db/schema.hpp` | `kSchema` DDL consumed only via `db_core.hpp`; decide: move to `src/` (default) or keep as embedder-facing DDL source |
| `db/queue.hpp` `*Locked(sqlite3*,…)` fns | Raw-handle mechanics in a public header; keep only `Database` methods public |
| `ipc/protocol.hpp` `namespace detail` | 12 helpers incl. `trackToJson/trackFromJson` that **duplicated** `db/json.hpp` — DONE 2026-09-28: unified on `db::`, copies deleted (playlist/error/enum converters stay: wire SDK) |
| `client/client_impl.hpp`, `service/service_impl.hpp` | Full class bodies + `<poll.h>` + windows.h dance in public. Decision: Client/Service ARE the public SDK (CLI + third-party frontends) → **merge** into `client.hpp`/`service.hpp`, delete the `_impl` split (same for the pure re-export shims `client/client.hpp`, `service/service.hpp`) |
| Fat headers | `db_core.hpp` (709 lines), `player_core.hpp` (416), `service_impl.hpp` (296): slim to declarations, bodies to `.cpp` |

Fix, don't move:
- `fromJson<T>` (`ipc/protocol.hpp`): the `Result` branch was a trap (always errored `"use resultFromJson"`) — DONE 2026-09-28: dispatches to `resultFromJson`.

JSON privatization (DONE 2026-09-28, committed `2c7da67` — planning bullets removed):
- `include/caudio/utils/json.hpp` (`Json`/`JsonRef`/`JsonError`, `src/utils/json.cpp` backend, `modules/utils/json.cppm` partition); `db/json.hpp` + `ipc/protocol.hpp` signatures migrated; `ipc::detail` track dupes deleted; `protocol.cpp`/`config.cpp`/`db/json.cpp`/`output_formatter.cpp`/`dispatch_library.cpp` + 6 CLI sites + `test_db_json` converted; CMake backend is plain PRIVATE `-I` (`CAUDIO_NLOHMANN_PRIVATE_INCLUDE`), `JSON_Install` + `find_dependency` + `CAUDIO_JSON_EXTRA_INCLUDE` gone.
- Verified: dev 143/143, modules preset 205/205, install tree has no `nlohmann/` and no `find_dependency(nlohmann_json)`, downstream smoke (parse/track-roundtrip/IPC-wire, no JSON package visible) passes, `caudio --version` prints `v1.0.0-rc1`.
- Design notes: `JsonRef` proxy keeps builder lines source-identical; throwing `get<T>` preserves try/catch-to-`Corrupt` behavior; iteration is index loops (`at()`/`keyAt()`); CMake lesson: even PRIVATE links to an imported target force it into `install(EXPORT)` — hence include-dirs, not links.
- Behavior delta (intentional, for §6.2 goldens to lock): `toJson(Result)` track payloads now use the full `db::trackToJson` field set (superset of the old `ipc::detail` subset); all readers are contains-guarded. `dirty` bool still defaults on import (pre-existing `is_number()` guard, unchanged).
- `db.hpp` umbrella docstring claims `:detail/:fingerprint/:fts/:stmt_helpers` are "available separately but not included" — yet `db_core.hpp` includes `detail.hpp` anyway. Make the separation real or drop the claim.
- CI guard (new): fail if the installed tree contains `detail`/`_impl`/`internal`/`stmt_helpers` (allowlist exceptions only).

Verified LIVE — do NOT "remove" (user-suspected, symbol-checked 2026-09-28):
- `db/json.hpp`: `trackToJson/trackFromJson/exportJson/importJson` all called (`src/db/json.cpp`, CLI `library export/import`, `test_db_json`). The duplication was in `ipc::detail` (deleted 2026-09-28, unified on `db::`).
- `utils/{math,ring,mpsc_queue,log,error,print,thread,result}.hpp`: all symbols used (`SpscRing` by engine+tests, hex helpers by fingerprint, `Logger` by service+tests). Legit SDK surface — keep.
- `version.hpp`, `ipc` wire fns (`serialize/deserialize/frame/deframe/toJsonString`): keep.

### 6.2 CLI polish (surface: `main.cpp`, `app/core.{hpp,cpp}`, `app/parse.hpp`)

- Help-text audit: consistent verbs/units across subcommands (time args accept `s`/`mm:ss`/`hh:mm:ss`; volume `0-100`/`+n`/`-n`/`mute`).
- `parse.hpp` edges: `mm:ss` with `ss>=60` currently tolerated (parse.hpp:75-77) — decide strict vs lenient + document; `hh:mm:ss` range checks; empty-sign rejects.
- Exit-code contract: document (0 ok / 2 usage / 1 runtime?) and assert in tests; unify user-error output (one path — `main.cpp:19` `std::cerr` scatter is the start, audit `core.cpp`).
- `--version`/`--help` + per-command golden tests (new `tests/cli_golden.cpp`) — also unblocks deferred KISS-4.
- Daemon UX: `start`/`--foreground`, stale socket/pid handling, `pidPathForConfig` edge cases.
- JSON output consistency: `writePlaylistJson` vs `toJsonString` paths must agree field-for-field with the IPC wire format.
- Docs sync: man page + README command list/examples regenerated from `--help`, not hand-maintained.

### 6.3 Release mechanics

- rc2 tag covers the de-noise break (CHANGELOG `### Removed` + migration note: `internal::`/`detail` gone from install, `_impl` merged).
- rc3: freeze — only bugfixes, full matrix + sanitizers + install-smoke green.

---

## 7. Header de-noise inventory (audited 2026-09-28: 52 headers, include+symbol use)

Install rule: `install(DIRECTORY include/...)` has NO excludes — all 52
headers ship, plus generated `version_config.hpp`, plus
`vendor/sqlite3.h` + `vendor/blake3.h` (no longer installed either), plus 44 `.cppm` under `<prefix>/modules/` when modules are ON.

### 7.1 Vendor headers/libs (question answered 2026-09-28)

- install() ships `libsqlite3.a` (sqlite amalgamation, target `sqlite3`,
  export `caudio::sqlite3`) + `libblake3.a` AND `sqlite3.h` + `blake3.h`
  into `caudio/vendor/`. `miniaudio.h` is correctly NOT installed. Zero
  `#include <sqlite3.h|blake3.h|miniaudio.h>` under `include/` (comments
  + `struct sqlite3;` fwd-decls only). (Renamed 2026-09-28: was target
  `caudio` → `libcaudio.a`, which falsely implied the whole library and
  collided with the shared umbrella stem; content-named like `blake3`.
  The `3`s stay: they are part of the upstream project names.)
- Libs: INTENDED + REQUIRED — static transitive deps (`db` links them
  PRIVATE → `LINK_ONLY` in export); downstream needs the archives present.
- Headers: LEFTOVER — nothing installed references them, and shipping
  `sqlite3.h` invites ODR violations (consumer links system sqlite3 +
  our static `libcaudio.a` → duplicate `sqlite3_*`). Action: delete the
  one install line, keep the libs. Deeper hazard (unprefixed symbols in
  shipped static libs) is out of rc2 scope — one SDK-docs sentence
  ("don't mix with system sqlite3") covers 1.0.

### 7.2 Per-file verdicts (status: PUBLIC / UMBRELLA / REMOVE→src / MERGE / SPLIT / KEEP-SLIM / DECIDE)

`/` (root)

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| caudio.hpp | UMBRELLA | root re-export + layer-rule docs | keep | doc claims "four modules", omits ipc/service/client/config libs |

`client/`

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| client.hpp (13L) | UMBRELLA | re-exports client/* | keep | DONE 2026-09-28: uniform `_core` — `client/client_core.hpp` holds the class |
| client/client_core.hpp (62L) | PUBLIC (SDK) | `Config`, `Client`, `<poll.h>` | keep (merged) | was `client.hpp`+`client_impl.hpp` split, merged then renamed |
| client/ipc_client.hpp | PUBLIC | `IpcClient` transport | keep | – |
| client/output_formatter.hpp | DECIDE | `OutputFormatter` | default keep; or move to `cli/` (only our CLI uses it) | – |

`config.hpp` (only component without a subdir)

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| config.hpp | PUBLIC | `Config`, load/save, `Raw*` fns, socket/pid/lock path helpers | keep | – |

`db/`

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| db.hpp | UMBRELLA | re-export; `:partition` module syntax in docstring | keep; fix docstring | claims `:detail/:fts` "not included" but `db_core.hpp` drags them in |
| db_core.hpp (709L) | PUBLIC | `DbOpts`, `SqliteCloser`, `Database` (+`WriterThread` member) | KEEP-SLIM (decls only) | – |
| db_types.hpp | PUBLIC | pure data: `Track`, `Playlist`, `Queue(Item)`, `HistoryEntry`, queries | keep | – |
| detail.hpp (14L) | PRIVATE | re-export hub for `internal::` | REMOVE →src | – |
| fingerprint.hpp | PRIVATE | `internal::computeFingerprint`, `kSample` | REMOVE →src | `k`-prefix |
| fts.hpp | PRIVATE | `internal::escapeLike/sanitizeFtsTerm` | REMOVE →src | `search.hpp` declares a public wrapper of the same name |
| json.hpp (92L) | PUBLIC | track↔JSON, export/import | keep | – |
| queue.hpp (195L) | SPLIT | 13 `*Locked(sqlite3*,…)` free fns | REMOVE fns →src (types live in db_types) | locking contract in public names |
| scan.hpp | SPLIT | `ScanMode`, `scan*`, `hasAudioExt` | keep entries; helper →src | – |
| schema.hpp (192L) | PRIVATE (DECIDE) | `kSchema` DDL strings | REMOVE default; keep only if blessed as embedder DDL | `k`-prefix |
| search.hpp | SPLIT | `search*`, `tryFtsQuery`, `fillTrackSearch` | keep `search*`; rest →src | see fts.hpp dup-name |
| statement.hpp | PRIVATE | `SqliteStatement` RAII (already out of umbrella) | REMOVE →src | – |
| stmt_helpers.hpp | PRIVATE | `internal::` guards, row-mapper, col-list | REMOVE →src | `k`-prefix |
| transaction.hpp | PRIVATE | `DbTransaction` (already out of umbrella) | REMOVE →src | – |
| write_thread.hpp | PUBLIC-STRUCTURAL | `WriterThread` (**member** of `Database`) | keep (or pimpl `Database` later) | – |

`engine/`

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| engine.hpp | UMBRELLA | re-exports `:types` + `:core` | keep (split 2026-09-28) | was umbrella+class in one file |
| engine/engine_core.hpp (853L) | PUBLIC | `Engine` (decls only) | keep | uniform `_core` with db/player/client/service |
| engine_types.hpp | PUBLIC | enums, events, `EngineConfig/State` | keep | – |
| history.hpp | SPLIT | `History` + `engine::detail` fns + **`HistoryEntry` duplicating `db::HistoryEntry` field-for-field** | unify entry on `db::`; detail fns →src; `History` class DECIDE | dup type across namespaces |
| shuffle.hpp (56L) | PRIVATE | `engine::detail::shufflePerm` | REMOVE →src | – |

`ipc/`

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| ipc.hpp | UMBRELLA | re-export | keep | – |
| command.hpp (985L) | PUBLIC | ~40 `Command` structs + `@json_example` docs | keep | – |
| protocol.hpp (337L) | PUBLIC | wire types, converters, `detail` (playlist/error/enum = wire SDK) | keep | – |
| result.hpp (573L) | PUBLIC | ~14 `Result` structs | keep | – |

`player/`

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| player.hpp | UMBRELLA | re-export | keep | – |
| decoder.hpp | PUBLIC | `Decoder`, `TrackMetadata`, `extractMetadata` | keep | – |
| output.hpp (285L) | SPLIT | `AudioOutput` (**`unique_ptr` member** of `Player`) + `enumerateDevices`/`DeviceInfo`/`DeviceList` + `ma_device` fwd-decl | keep device API; fwd-declare `AudioOutput` (dtor out-of-line) or keep | opaque fwd-decl fine |
| player_core.hpp (416L) | PUBLIC | `State`, `PlayerOpts`, `Player` | KEEP-SLIM | `X_core` vs `engine.hpp`/`service_impl.hpp` — pick one convention |
| reader.hpp | SPLIT | `Reader`/`FileReader`/`MemoryReader` + `fseek64/ftell64/fileSizeInner` shims | keep types; shims →src | snake_case free fns in `caudio::player` |

`service/`

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| service.hpp | UMBRELLA | re-export | keep | DONE 2026-09-28: uniform `_core` |
| ipc_channel.hpp | PUBLIC-ISH | `IpcChannel`, `frame/deframeMessage` | keep | **layering wart**: client lib includes a service header |
| ipc_server.hpp | PUBLIC (daemon SDK) | `IpcServer` + Win32 fallback decls | keep | – |
| service/service_core.hpp (296L) | PUBLIC (daemon SDK) | `ServiceConfig`, `Service`, windows.h dance | keep (merged) | was `service.hpp`+`service_impl.hpp` split, merged then renamed |
| shm_status.hpp | PUBLIC | `ShmStatus` ABI + atomic helpers + Win32 decls | keep | – |

`utils/` — all PUBLIC, all symbol-checked live earlier — keep all 9 + `version.hpp`

| file | status | contains | remove? | naming |
|---|---|---|---|---|
| error.hpp / result.hpp / log.hpp / math.hpp / ring.hpp / mpsc_queue.hpp / thread.hpp / json.hpp / version.hpp | PUBLIC | as named | keep | `print.hpp` lives in `namespace caudio` not `utils` (intentional `std::print` parity, inconsistent); `thread.hpp` has 4th `detail` convention (file `detail.hpp` vs inline `engine::detail` vs `ipc::detail` vs `utils::detail`) |

Net rc2 cut list: ~10 files →`src/`, 2 merges, 3 slim-downs, 2 splits, 1 vendor-install deletion, 4 naming decisions (`HistoryEntry` dup, `_impl`/`_core` convention, `detail` convention, `output_formatter` home).

Executed 2026-09-28 (uncommitted): all of the above EXCEPT deliberate deviations —
- `scan()` generator KEPT in `scan.hpp` (cross-component: service playlist-import calls `caudio::db::scan`; earlier single-TU audit missed the qualified call).
- `hasAudioExt` KEPT in `scan.hpp` (`db::detail::`, called from 2 service TUs — legit shared helper, still exported via `scan.cppm`).
- `db::sanitizeFtsTerm` wrapper KEPT (exercised by `test_db_search` — tested public API); `fillTrackSearch`/`tryFtsQuery` went file-static.
- `History` class + `nowMs`/`shouldMarkPlayed`/`shufflePerm` → `src/engine/` (engine-internal confirmed: zero cli/tests/examples users); `db::HistoryEntry` extended to 12 fields, `engine::HistoryEntry` deleted; `ipc::HistoryEntry` wire twin untouched.
- New `WriterThread::push(sql, cb)` overload: by-value `unique_ptr<SqliteStatement>` params require completeness even for `nullptr` at every call site (incl. tests) — the 2-arg form is the SDK-usable one.
- `WriteOp`/`Database` ctors moved out-of-line (inline construction with fwd-declared member types breaks unwinding instantiation in every including TU).
- White-box tests (`shufflePerm`, `shouldMarkPlayed`) kept via `src/` on the test include path (build-tree only).
- `service/dispatch_{queue,library}` reach `src/db/` privates (`computeFingerprint`, `hasAudioExt`) through the shared `src/` include root — accepted coupling (pre-existing), not installed.
- Verified: dev 143/143, modules preset green, install trees (header + module) contain no `detail/_impl/internal/stmt_helpers/schema/queue/statement/transaction/shuffle/history/nlohmann`, no `caudio/vendor/`, downstream smoke green on the de-noised prefix.
- Naming unification (2026-09-28, committed `a21ad37`): uniform `component.hpp` umbrella + `component/component_core.hpp` primary class — `client/client.hpp`→`client_core.hpp`, `service/service.hpp`→`service_core.hpp`, `Engine` split out of `engine.hpp` into `engine/engine_core.hpp` (all `engine.hpp` includers unchanged); module partitions `:impl`→`:core` + new `engine:core`. Verified dev 143/143 + modules green.

---

## 8. Documentation audit (2026-09-28)

Audited all 52 public headers + cli/src/modules/cmake docs; fixed and verified (dev 143/143, modules green).
- Umbrella `caudio.hpp` now includes all 8 libraries (was 4) + full layer rule.
- Module sources install to `<prefix>/modules/` (same level as `include/`), not `include/caudio/modules/` — FILE_SET `BASE_DIRS`, DIRECTORY rule, CI verify paths, README layout all updated; module-mode downstream smoke green on the new layout.
- Doxygen was documenting `src/`+`cli/src` and never `include/` (patterns missed `.hpp` too): INPUT is now `include/`, project renamed `caudio-cpp`→`caudio`, privates no longer extracted.
- Groups: every component umbrella defines its `@defgroup` (added db/ipc/client/service/config); duplicate `caudio_player` defgroup removed from `player.cppm`; `caudio_engine`/`caudio_utils` anchors moved to their umbrellas.
- Missing file/API docs added: `player.hpp`, `service/ipc_channel.hpp` (+`IpcChannel`/framing fns), `service/ipc_server.hpp` (+methods, incl. the windows.h-avoidance pattern note), `service/shm_status.hpp` (all public API), undocumented ctors/dtors (`Decoder`, `Player`, `Client`×4, `IpcServer`, `ShmStatusHandle`), CLI `detail::` helpers, `OutputFormatter` helpers.
- Rot removed: `Decoder registry` (single decoder; header+`.cppm`), TUI-subcommand references in code comments (capability docs in man/README kept), `fallback legacy` pid comment, `9.0.1+` FFmpeg floor (FindFFmpeg enforces none; CI uses system FFmpeg), CPack summary missing ipc/client/service, `ports of test_utils_*` test comment, `combined.cmake` `# //` + stale KISS-1 bare ref, `SetupFFmpeg` pinned-version comment vs rolling URL, missing component file-headers (`db/engine/player/utils.cmake`), stale `nlohmann` CMake comment (links→private `-I`), `db_types.hpp` pointer at non-installed `fingerprint.hpp`, misplaced "legacy raw-pointer" note in `db_core.hpp`, README missing `caudio::engine` + stale modules path, CONTRIBUTING FFmpeg row + vendor note.
- New `modules/utils/print.cppm` (`:print` partition; every other utils header had one).
- Full tree clang-formatted (`*.cpp/*.hpp/*.cppm`, excluding `build/`).

---

## Notes — checked and found clean

- **Module gating core:** `CaudioHelpers.cmake` gates `FILE_SET CXX_MODULES` on `CAUDIO_ENABLE_MODULES`; `CMAKE_CXX_SCAN_FOR_MODULES` toggles; `*.cppm` install is gated. Verified by full builds in both modes.
- **Public-header vendor includes:** verified zero `sqlite3.h`/`blake3.h`/`miniaudio.h` includes under `include/`. (Stale note removed 2026-09-28: nlohmann/json is private since 0.36.0 — opaque `Json` facade, no `find_dependency`, nothing installed.)
- **Namespace rename:** `caudio::cli` fully gone from code (`caudio::ipc` + `caudio::config`); zero `^import` in tests/cli/examples; `RepeatMode::Queue` → `All` incl. JSON wire string.
- **Install verified both modes:** header-only install ships headers + `FindFFmpeg.cmake`, no `.cppm`; modules-ON install ships 44 `.cppm` files under `<prefix>/modules/` (moved out of `include/` 2026-09-28; was 56 under `include/caudio/modules/`).
- **`version_config.hpp` flow:** `cmake/version.hpp.in → configure_file → BINARY_DIR/include/caudio/version_config.hpp → install(FILES …)` is coherent.
- **Sanitizer helper:** target-scoped with WIN32/MSVC guards; per-test repetition removed via single `caudio_add_catch_test` helper.
- **Test skip mechanism:** `CAUDIO_TEST_NOAUDIO` compile def + env fallback + `CAUDIO_SKIP_IF_NOAUDIO()` macro, consistently used; ctest 143/143 green in default config.
- **Umbrella layering docs:** `include/caudio.hpp` layer rule matches `cmake/components/*.cmake` DEPS; no public-header→`src/` private-header include remains.
- **No commented-out code blocks** of significance in sampled CMake/sources.
- **`.clang-format` / `.clang-tidy` / `.editorconfig`** exist and are referenced; `CAUDIO_ENABLE_CLANG_TIDY` wires correctly when the binary exists.
- **Windows declarations:** `service_paths.cpp`, `core.cpp`, `ipc_client.cpp` include real `<windows.h>` first with `clang-format off` guards; hand-rolled declarations removed.
- **Engine header slimmed twice:** 21 → 12 includes, then the `Engine` class split into `engine/engine_core.hpp` (fwd-decls + decls) leaving a 10-line `engine.hpp` umbrella; impl needs moved to `src/engine/*.cpp`.
- **Decoder collapsed:** single public `Decoder` + private `ffmpeg_impl.hpp`; deleted `IDecoder`, `DecoderRegistry`, `decoder_common.hpp`, `ffmpeg.cppm`, `decoder_interface.cppm`.
- **Service split:** `service_impl.cpp` (1521 → ~300 lines) → 4 `dispatch_*.cpp` + `service_paths`/`service_status`/`service_audio`; 5 forwarding wrappers deleted.
- **Utils consolidated:** `clampVolume` + `toHex`/`fromHex` in `utils::`; removed from `db`/`player`.
- **Deprecated shims removed:** `ShuffleMode`, `getState`/`getPosition`, `attachDb`, `getCached*`, `bindBlob` C-ptr, C-string `Error`, `service::socketPathFor`, hex wrappers.
- **nlohmann/json privatized (0.36.0):** opaque `Json` facade; no installed headers, no `find_dependency`; downstream consumer compiles without a JSON package.
- **Shared variants gated:** `*_shared` + `combined` behind `CAUDIO_BUILD_SHARED=OFF` (default); static + header-only is canonical. Verified OFF (143/143 tests) and ON both configure + build.
- **Version story hardened:** tag-less configure emits a loud `WARNING` (fallback is NOT a release build); `CAUDIO_REQUIRE_GIT_VERSION=ON` fails fast with a clear message. Stale `e.g. "v0.25.4"` doc strings removed from `version.hpp` / `version.hpp.in` / `ipc/result.hpp` — surrounding words already convey the format, so nothing rots on release.
- **FetchContent lazy:** `find_package(nlohmann_json/CLI11)` first, fetch only as fallback; CLI11 + the `caudio` executable skipped entirely with `CAUDIO_BUILD_CLI=OFF` (verified configure + build). nlohmann reaches components as plain PRIVATE `-I` (`CAUDIO_NLOHMANN_PRIVATE_INCLUDE`); `CAUDIO_CLI11_EXTRA_INCLUDE` covers the exe (empty when system package provides the target).
- **AI comments:** stale `import`-wording narration in `tests/common.hpp` deleted; per-method `@par Thread safety` blocks verified as concise one-line locking contracts (kept); canonical locking section lives in `engine/engine_core.hpp` file docs (moved out of the `engine.hpp` umbrella 2026-09-28).

---

*Method note: findings derive from glob/grep/read sampling per the task brief (not a full per-file audit). Line numbers are as observed at review time; verify with grep before editing. Items addressed in batches B1–B9, C1–C5, shared-lib gating, version/fetch/comment cleanup plus the namespace rename were removed in the 2026-09-27 pass. KISS-4 deferred and NAME-7 declined by user decision. No files other than this report were modified.*