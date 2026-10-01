# caudio C++23 Codebase Review

**Date:** 2026-09-30 (UTC) — all addressed entries removed; only open (deferred/undone/decided) items remain.
**Scope:** `CMakeLists.txt`, `cmake/*.cmake`, `cmake/components/*.cmake`, `include/caudio/**/*.hpp` (+ `include/caudio.hpp`), `src/**/*.cpp` (+ `src/**/*.cppm` sampled), `cli/src/**/*`, `tests/common.hpp` + sample of `tests/*.cpp`, `examples/*.cpp`, `README.md`, `CONTRIBUTING.md`, `.github/workflows/ci.yml`, `.gitignore`, `cmake/version.hpp.in`, `cmake/caudioConfig.cmake.in`, `docs/man/caudio.1` + `docs/polyglot-integration.md` (sampled). Headers treated as canonical; modules (`*.cppm`) opt-in via `CAUDIO_ENABLE_MODULES=OFF` default. Large directories sampled via glob/grep + targeted reads, not every file fully read.

## Executive summary

Fifty-one of the original 53 findings were addressed across twelve batches (header detox, shim deletion, service split, utils consolidation, cmake/docs cleanup, `cli` → `ipc`/`config` namespace rename, module gating, decoder collapse, Windows-declaration fix, engine header slim, shared-lib gating, version/fetch/comment cleanup). Since then the dependency bumps (rc1), the full library de-noise + naming unification, the documentation audit, and the include-hygiene/module-export pass were all executed and committed — those sections were removed in the 2026-09-30 pass. Two items remain open by explicit decision plus two tracked third-party issues — no critical blockers.

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
| KISS-4 | Minor | `src/client/output_formatter.cpp:48-~260` | `OutputFormatter::print` is a long `if constexpr (is_same_v<T, …>)` chain over ~10 `Result` alternatives. | Use `std::visit(overloaded{…})` or a table of formatters; one case per small function. **Deferred by user decision:** formatting logic is behavior-sensitive; needs output-golden tests first (§5.1). |

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

### Non-FFmpeg trim leftovers (pending, audited 2026-09-27 against actual API use — not yet applied)

- **sqlite3** (27 APIs used; WAL + `foreign_keys=ON` + FTS5 required; zero
  JSON1 SQL uses, no `create_function`/blob-I/O/backup/authorizer/extensions):
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

## 5. rc2/rc3 goals — CLI polish + release mechanics (marked 2026-09-28)

Phase: rc1 feature-complete; de-noise executed and committed 2026-09-28
(CHANGELOG `### Removed` + migration note cover the API break). Remaining
for rc2: CLI polish. rc3: freeze.

### 5.1 CLI polish (surface: `main.cpp`, `app/core.{hpp,cpp}`, `app/parse.hpp`)

- Help-text audit: consistent verbs/units across subcommands (time args accept `s`/`mm:ss`/`hh:mm:ss`; volume `0-100`/`+n`/`-n`/`mute`).
- `parse.hpp` edges: `mm:ss` with `ss>=60` currently tolerated (parse.hpp:75-77) — decide strict vs lenient + document; `hh:mm:ss` range checks; empty-sign rejects.
- Exit-code contract: document (0 ok / 2 usage / 1 runtime?) and assert in tests; unify user-error output (one path — `main.cpp:19` `std::cerr` scatter is the start, audit `core.cpp`).
- `--version`/`--help` + per-command golden tests (new `tests/cli_golden.cpp`) — also unblocks deferred KISS-4.
- Daemon UX: `start`/`--foreground`, stale socket/pid handling, `pidPathForConfig` edge cases.
- JSON output consistency: `writePlaylistJson` vs `toJsonString` paths must agree field-for-field with the IPC wire format.
- Docs sync: man page + README command list/examples regenerated from `--help`, not hand-maintained.
- `client/output_formatter.hpp` home undecided: only our CLI uses `OutputFormatter` — default keep in the SDK, or move to `cli/` (decide at freeze).

### 5.2 Release mechanics

- rc2 tag covers the de-noise break (CHANGELOG `### Removed` + migration note: `internal::`/`detail` gone from install, `_impl` merged).
- rc3: freeze — only bugfixes, full matrix + sanitizers + install-smoke green.

---

## Notes — checked and found clean

- **Module gating core:** `CaudioHelpers.cmake` gates `FILE_SET CXX_MODULES` on `CAUDIO_ENABLE_MODULES`; `CMAKE_CXX_SCAN_FOR_MODULES` toggles; `*.cppm` install is gated. Verified by full builds in both modes.
- **Public-header vendor includes:** verified zero `sqlite3.h`/`blake3.h`/`miniaudio.h` includes under `include/`. (Stale note removed 2026-09-28: nlohmann/json is private since 0.36.0 — opaque `Json` facade, no `find_dependency`, nothing installed.)
- **Namespace rename:** `caudio::cli` fully gone from code (`caudio::ipc` + `caudio::config`); zero `^import` in tests/cli/examples; `RepeatMode::Queue` → `All` incl. JSON wire string.
- **Install verified both modes:** header-only install ships headers + `FindFFmpeg.cmake`, no `.cppm`; modules-ON install ships 41 `.cppm` files under `<prefix>/modules/` (moved out of `include/` 2026-09-28; was 56 under `include/caudio/modules/`, 44 before the `:paths`/`:status`/`:audio` partitions were deleted 2026-09-30 as wrappers of non-installed `src/service/*` internals). Downstream full-module smoke (all 7 modules imported, live symbol per module) green.
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

*Method note: findings derive from glob/grep/read sampling per the task brief (not a full per-file audit). Line numbers are as observed at review time; verify with grep before editing. Items addressed in batches B1–B9, C1–C5, shared-lib gating, version/fetch/comment cleanup plus the namespace rename were removed in the 2026-09-27 pass; dependency bumps, library de-noise + naming unification, the documentation audit, and the include-hygiene/module-export pass were removed in the 2026-09-30 pass. KISS-4 deferred and NAME-7 declined by user decision. No files other than this report were modified.*