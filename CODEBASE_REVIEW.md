# caudio C++23 Codebase Review

**Date:** 2026-10-02 (UTC) — all addressed entries removed; only open (deferred/undone/decided) items remain. Declined NAME-7 row removed 2026-10-02; shared-library (§5) and archive-bloat (§6) audits added same day.
**Scope:** `CMakeLists.txt`, `cmake/*.cmake`, `cmake/components/*.cmake`, `include/caudio/**/*.hpp` (+ `include/caudio.hpp`), `src/**/*.cpp` (+ `src/**/*.cppm` sampled), `cli/src/**/*`, `tests/common.hpp` + sample of `tests/*.cpp`, `examples/*.cpp`, `README.md`, `CONTRIBUTING.md`, `.github/workflows/ci.yml`, `.gitignore`, `cmake/version.hpp.in`, `cmake/caudioConfig.cmake.in`, `docs/man/caudio.1` + `docs/polyglot-integration.md` (sampled). Headers treated as canonical; modules (`*.cppm`) opt-in via `CAUDIO_ENABLE_MODULES=OFF` default. Large directories sampled via glob/grep + targeted reads, not every file fully read.

## Executive summary

Fifty-one of the original 53 findings were addressed across twelve batches (header detox, shim deletion, service split, utils consolidation, cmake/docs cleanup, `cli` → `ipc`/`config` namespace rename, module gating, decoder collapse, Windows-declaration fix, engine header slim, shared-lib gating, version/fetch/comment cleanup). Since then the dependency bumps (rc1), the full library de-noise + naming unification, the documentation audit, and the include-hygiene/module-export pass were all executed and committed — those sections were removed in the 2026-09-30 pass. One item remains open by explicit decision (deferred KISS-4) plus two tracked third-party issues; the declined NAME-7 row was removed 2026-10-02. A shared-library audit added 2026-10-02 contributes five findings (§5) — no critical blockers outside the MSVC shared path. An MSVC archive-bloat audit added the same day contributes four findings (§6): SDK-only size overhead, explicitly not an RC item.

**Counts by severity (13 open findings):**

| Severity | Count |
|---|---|
| Critical | 0 |
| Major | 2 |
| Minor | 11 |
| Nit | 0 |
| **Total** | **13** |

---

## 1. KISS violations (remaining)

| ID | Severity | Location | Finding | Suggested fix |
|---|---|---|---|---|
| KISS-4 | Minor | `src/client/output_formatter.cpp:48-~260` | `OutputFormatter::print` is a long `if constexpr (is_same_v<T, …>)` chain over ~10 `Result` alternatives. | Use `std::visit(overloaded{…})` or a table of formatters; one case per small function. **Deferred by user decision:** formatting logic is behavior-sensitive; needs output-golden tests first (§4.1). |

---

## 2. Known third-party issues (tracked, not ours)

| ID | Severity | Location | Finding | Status |
|---|---|---|---|---|
| EXT-1 | Minor | `src/player/decoders/ffmpeg.cpp` (`Decoder::init`, `extractMetadata`) + `tests/sanitizers/lsan.supp` | System FFmpeg (Ubuntu 24.04 libavutil, 6.x) leaks one 8192-byte probe buffer per `avformat_open_input` via `av_realloc_f`, on success and failure paths. Our lifecycle (alloc/open/close, AVIO setup/teardown, codec/swr alloc+free, packet/frame/layout RAII) is balanced; all affected tests pass functionally; zero UBSan/ASan-OOB reports. Suppressed narrowly (`leak:av_realloc_f` — caudio never calls it, so nothing of ours is masked). | Revisit with a Linux+ASan environment or newer system FFmpeg. |
| EXT-2 | Minor | `include/caudio/utils/print.hpp`, `src/**/dispatch_*.cpp`, `cli/src/app/core.cpp` | msys2 MinGW GCC 16.x `libstdc++` lacks `std::__open_terminal`/`std::__write_to_terminal`, so every `std::print` stream/`FILE*` overload fails at link. Worked around with the `caudio::print`/`println` facade (format+insert on MinGW, plain forward elsewhere). | Drop the MinGW branch if a future msys2 build ships the symbols. |

---

## 3. FFmpeg trimming plan (post-1.0 packaging track, approved)

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

## 4. rc2/rc3 goals — CLI polish + release mechanics (marked 2026-09-28)

Phase: rc1 feature-complete; de-noise executed and committed 2026-09-28
(CHANGELOG `### Removed` + migration note cover the API break). Remaining
for rc2: CLI polish. rc3: freeze.

### 4.1 CLI polish (surface: `main.cpp`, `app/core.{hpp,cpp}`, `app/parse.hpp`)

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

## 5. Shared libraries (`*_shared`, `combined`) — audited 2026-10-02

Design: static twins compile once; shared variants link the static objects
via `$<LINK_LIBRARY:WHOLE_ARCHIVE,…>` behind a stub source (single-BMI fix
`bbc1f47`), gated behind `CAUDIO_BUILD_SHARED=OFF`
(`CMakeLists.txt:116-120`, `d077cb4`). `combined` whole-archives
utils/player/db/engine into `libcaudio`
(`cmake/components/combined.cmake:4-16`). Duplicate symbols are masked
with `-Wl,--allow-multiple-definition`, which exists only on GNU ld
(`CaudioHelpers.cmake:65`, `combined.cmake:15`, `cli.cmake:21` — all guard
out Darwin; MSVC has no equivalent in-tree).

| ID | Severity | Location | Finding | Fix (§5.1) |
|---|---|---|---|---|
| SH-1 | Major | `cmake/CaudioHelpers.cmake:57-66` (`OUTPUT_NAME ${NAME}`) | Static and shared flavors share one basename in one dir. On MSVC the static archive (`utils.lib`) and the shared import library (`utils.lib`) are the same filename: last-writer-wins with no warning (reproduced 2026-10-02 with a 2-target VS-generator project — static `foo.lib` silently overwrote the import `foo.lib`). Any MSVC `shared`-preset build ships ambiguous link inputs. Linux/macOS/MinGW unaffected (`.a` vs `.so`/`.dylib`/`*.dll.a` differ). | (a) Suffix shared `OUTPUT_NAME`s (`utils_shared` → `libutils_shared.so`, `utils_shared.dll`). |
| SH-2 | Major | `cmake/`, `CMakeLists.txt` (no export macros anywhere in project code) | MSVC DLLs export nothing: zero `__declspec(dllexport)` / export-macro story, `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` unset (remaining `dllexport` hits are vendor/shipped-OS imports only). Even with SH-1 fixed, `*_shared` on the MSVC ABI is an empty surface. | (b) `GenerateExportHeader`-style `CAUDIO_<COMP>_EXPORT` macros (or equivalent), wired per component. |
| SH-3 | Minor | `cmake/components/combined.cmake:1-16` | `combined` is named/sold as `libcaudio` but whole-archives only utils/player/db/engine — `ipc`/`service`/`client` excluded ("no shared ABI"). A downstream linking `combined` for the full stack silently gets a partial lib. | (c) Complete it to all 7 components (true `libcaudio`) and document mutual exclusivity: link `combined` XOR `*_shared`, never both. |
| SH-4 | Minor | `cmake/components/cli.cmake:54-82` (no `ipc_shared`) | No `ipc_shared` variant: static `ipc` objects bake into *both* `service_shared` and `client_shared` (MinGW build: `libservice.dll` ~65 MB + `libclient.dll` ~64 MB, ipc inside each). Linking both into one binary duplicates every ipc symbol — masked on GNU only. | (d) Add `ipc_shared`; `service_shared`/`client_shared` link it instead of static `ipc`. |
| SH-5 | Minor | `.github/workflows/` (no `shared` string anywhere) | Zero CI coverage for the `shared` preset; release presets never enable it; tests link static targets only. Darwin link status unknown (`--allow-multiple-definition` excluded, no macOS equivalent in-tree). Bitrot by design. | (e) CI `shared`-preset jobs (Linux GCC, Windows MinGW, Windows MSVC, macOS Clang). |

### 5.1 Fix design (cross-platform, no clashes)

Constraints: one scheme for Linux/macOS/Windows-MinGW/Windows-MSVC;
no two artifacts may share a filename in one dir; downstream selection
must be explicit; `combined` keeps `OUTPUT_NAME caudio` (no static
competitor exists — constraint: no static target may ever take
`OUTPUT_NAME caudio`, which is also why the module umbrella stays
`caudio_umbrella`).

1. **Names.** Shared variants take suffixed outputs: target
   `utils_shared` → `OUTPUT_NAME utils_shared` (`libutils_shared.so`,
   `libutils_shared.dylib`, `utils_shared.dll` + `utils_shared.lib`).
   Static keeps canonical names. Uniform across OSes (no per-platform
   `if()` naming — that trades one confusion for another). Breaking
   artifact rename; pre-1.0 + CHANGELOG note covers it.
2. **`ipc_shared`.** `caudio_add_shared_variant(ipc)` with no extra deps;
   `service_shared`/`client_shared` `EXTRA_DEPS caudio::ipc_shared`
   (drop `caudio::ipc`). Ends static baking; one ipc symbol source.
3. **`combined` → full stack.** Whole-archive all 7 components
   (add service/client/ipc + their link deps, e.g. `rt`) so `libcaudio`
   means the whole library. Document in `combined.cmake` + install note:
   `combined` and `*_shared` are mutually exclusive — linking both
   re-duplicates symbols by construction (whole-archive merging, not a
   bug to chase per-link).
4. **Exports.** Component export headers (`CAUDIO_UTILS_EXPORT`, …)
   default-hidden visibility + explicit exports; required for MSVC,
   harmless elsewhere. `GenerateExportHeader` or a hand-rolled macro
   header per component (header-only macro, installed).
5. **Keep the workarounds, document their limits.** WHOLE_ARCHIVE +
   GNU-only `--allow-multiple-definition` stay; comment the platform
   matrix (GNU masked / Darwin unproven / MSVC LNK2005-risk) until
   SH-5 CI proves each cell. Visibility hardening beyond (4) only if
   ODR bites after that.
6. **Do not remove.** OFF-by-default costs the default build nothing;
   distro packagers are the constituency that needs `.so`s, and removal
   would be breaking with no requester. Revisit removal only if SH-5 CI
   shows rot beyond cheap repair.

Phasing: (1)+(2)+(3)+CI in one batch (cmake-only, verifiable per-OS);
(4) with the first MSVC-shared green; (5) never (docs only).
Verified baseline 2026-10-02: `shared` preset (MinGW GCC) configures +
builds 66/66 incl. `libcaudio.dll` + all `lib*.dll`; MSVC collision
reproduced as above.

---

## 6. MSVC archive bloat (SDK-only) — audited 2026-10-02

Question: release `.lib`s total ~63 MB (MSVC) vs ~11 MB (Clang) and
~12 MB-class (GCC) for identical sources — bug, flags, or MSVC design?
Verdict: **MSVC object-format design meeting template-heavy C++23**;
flags are clean Release (`/O2 /Ob2 /DNDEBUG`, `release-msvc/CMakeCache`,
zero `.pdb` files, `.debug$S` 160 bytes in the fattest object). The
shipped binaries are unaffected (MSVC `.exe` is the smallest at 3.1 MB)
because the linker GCs unreferenced COMDATs (`/OPT:REF`) while the
librarian preserves all of them.

Evidence (`protocol.cpp`: 7,841,427-byte MSVC obj vs 1,261,569-byte
Clang obj; `llvm-size dec` code+data 321,952 vs 202,753 — only 1.6x
codegen delta, the rest is symbol/string table):

| Object (MSVC) | Defined syms (MSVC / Clang) | Dominant family |
|---|---|---|
| `protocol.obj` 7.8 MB | 8,896 / 1,307 | `variant` 6,004 + `visit` 455 |
| `client_impl.obj` 7.7 MB | 12,364 / — | `variant` 8,367 + `visit` 354 (construct/copy/destroy/get — no `std::visit` in this TU) |
| `ipc_client.obj` 5.2 MB | 8,956 / — | `variant` 5,797 + `visit` 197 |
| `json.obj` 2.9 MB | 6,349 / — | `nlohmann` 3,330 + `basic_string` 3,488 |
| `dispatch_library.obj` 2.1 MB | 7,665 / — | `format` 915 + `filesystem` 739 + `basic_string` 998 |
| `ipc_server.obj` 2.1 MB | 4,697 / — | `variant` 1,794 + `visit` 202 + `function` 236 |
| `service_impl.obj` 1.7 MB | 4,265 / — | `variant` 1,050 + `visit` 497 + `expected` 571 |
| `output_formatter.obj` 1.4 MB | 4,720 / — | `format` 2,032 + `basic_string` 906 |
| `config.obj` 1.2 MB | 4,870 / — | `format` 874 + `filesystem` 539 + `basic_string` 876 |

(Symbol families via `llvm-nm --defined-only` substring counts;
`protocol.obj` also holds 8,064 COMDAT sections per `dumpbin`.)
Two C objects are table/string-heavy, not template-heavy
(`sqlite3.obj` 4.0 MB, `miniaudio_impl.obj` 2.4 MB) — covered by the
existing `SQLITE_OMIT_*` / `MA_NO_*` trim items (§3 leftovers), not by
anything below. The single biggest object overall is the CLI TU
(`core.obj` 7.2 MB / 20,951 syms), out of library scope.
Method reproduces per-TU: compare `llvm-size -B dec` (code) against
file size (code+symbols), then family-count the `llvm-nm` output.

| ID | Severity | Location | Finding | Fix |
|---|---|---|---|---|
| BIN-1 | Minor | `src/ipc/protocol.cpp:209,717`, `src/client/*.cpp`, `src/service/service_impl.cpp:317` + `include/caudio/ipc/command.hpp` (~20-alternative `Command` variant) | `std::variant` machinery is ~70% of MSVC symbols in IPC/client objects. Already source-local (4 `std::visit` sites, all `.cpp`); the header holds only the variant *definition*, which is the API vocabulary and must stay. Header-stripping cannot move this. | None structural (see RC verdict below). |
| BIN-2 | Minor | `src/utils/json.cpp` (+ vendored nlohmann) | JSON machine is source-local and private; MSVC emits it ~3.5x fatter than Clang. Compiler tax, no code smell. | None. |
| BIN-3 | Minor | `include/` (`<format>` in `utils/{result,error,log,print}.hpp`; `<filesystem>` in 8 headers; `<functional>` in engine/scan/service headers; ~34 small template definitions inventoried, none dominant) | Heavy-STL includes cost parse time in every including TU; only *used* templates instantiate, and the inventory shows no movable monster — the heaviest bodies (`print`/`log` variadics, `get<T>`, formatters) cannot leave headers (variadic/explicit-specialization must be visible at call sites). Non-template inline bodies (e.g. `detail::hasAudioExt`) could move to `.cpp` without breaking API, but contribute marginally. | Opportunistic IWYU only; no campaign. |
| BIN-4 | Minor | `CMakeLists.txt` `install()` (no `COMPONENT`s), CPack TGZ/ZIP ~103 MB | Dev artifacts (`.lib`s) ship inside the runtime archive. The size complaint is really about distribution, not compilation. | Split `COMPONENT`s: runtime (`caudio.exe` + FFmpeg DLLs) vs dev (headers + `.lib`s); ship runtime zips as the release artifact. Optional: `minsize` preset for the exe. |
| BIN-5 | Minor | `src/client/output_formatter.cpp:46` (KISS-4 chain), `src/service/config` paths | `format` is 43% of `output_formatter.obj` symbols (2,032/4,720): each `if constexpr` branch instantiates the full `<format>` machinery for its own arg list. Same pattern smaller in `config.obj` (874). | Fold into the deferred KISS-4 `std::visit` refactor: each visitor returns `std::string`, single `print("{}", …)` at the sink — collapses N instantiations to ~1. Not an RC item (same freeze as KISS-4). |

### Should template-stripping go into the next RCs? No.

- **Too much work?** Yes, relative to payoff. The safe subset (IWYU pruning, moving a handful of non-template inline bodies) is days of audit + re-measure for single-digit-percent archive shrinkage. The structural subset (variant redesign, `std::function` members → leaner erasure, `<format>` constraints) touches the IPC wire type and callback structs: behavior-sensitive, KISS-4-class risk, weeks with review.
- **API-breaking?** Safe subset: no. Structural subset: yes (`EngineCallbacks` member types, `Command` shape, formatter surface) — permissible pre-1.0 with CHANGELOG notes, but churn during freeze.
- **Pros:** smaller SDK artifacts, faster MSVC builds, marginally leaner archives on all compilers.
- **Cons:** zero effect on shipped binaries (linker already GCs); perf risk (less cross-TU inlining without LTO); review burden; violates rc3 freeze (bugfixes only, §4).
- **Verdict:** keep the MSVC default (the `.exe` story is unchanged), do BIN-4 packaging split (cheap, solves the actual complaint), defer template hygiene to a post-1.0 measured project using the `llvm-nm` method above — never as an RC item. The one technique worth that project is `extern template` (§6.1).

### 6.1 Stripping `std::variant` via `extern template` (recipe, post-1.0)

The `Command` variant has 50 alternatives
(`command.hpp:937-945`; `Result` has 16, `result.hpp:559-563`), and the
sweep shows the *machinery* (copy/move/destroy/`get`, thousands of
symbols per TU) outweighs the *visitation* (hundreds per TU). That
machinery is exactly what explicit instantiation deduplicates:

1. In `command.hpp`, after the alias: `extern template class`
   `std::variant<Play, Pause, …>` (full 50-name list, mechanical) plus
   `extern template` declarations for the used free/member templates
   (`get<I>`/`get_if`/`holds_alternative`/`operator==` as used —
   enumerate via `llvm-nm`, iterate on link errors, which are loud).
2. In ONE defining TU (`src/ipc/protocol.cpp`): the matching
   `template class …` + explicit instantiation definitions.
3. Same recipe extends to `Result`, `std::expected<void, Error>`
   (100–500 syms/TU), `std::function<…>` callback signatures, and our
   own `MoveOnlyFunction`/`Generator` instantiations.
4. Legality boundary (standard rule): explicit instantiation is
   permitted only where at least one template argument is
   user-defined — so `variant<Command…>`, `expected<void, Error>`,
   `vector<Track>` qualify, while `std::string`, `std::vector<int>`,
   and per-call-site `std::format` instantiations (variadic, distinct
   types each) can never be covered. TU-local lambda visitors stay
   per-TU by construction.
5. Proof order: single-TU pilot (`protocol.cpp`), re-measure with the
   §6 method, then extend; verify all three compilers × both module
   modes (macro/flags must match within a config — true here).
   Expected: non-defining TUs lose most variant symbols (e.g.
   `client_impl` toward its visit-specific ~400 + own code); binaries
   unchanged (linker already folds).

Effort: days (enumerate → pilot → verify → extend), non-breaking (no
signature changes; misses fail loudly at link). Still not an RC item
(freeze, §4; zero user-visible gain) — first substantive project
after 1.0 if SDK size matters.

---

---

## Notes — checked and found clean

- **Module gating core:** `CaudioHelpers.cmake` gates `FILE_SET CXX_MODULES` on `CAUDIO_ENABLE_MODULES`; `CMAKE_CXX_SCAN_FOR_MODULES` toggles; `*.cppm` install is gated. Verified by full builds in both modes.
- **Public-header vendor includes:** verified zero `sqlite3.h`/`blake3.h`/`miniaudio.h` includes under `include/`. (Stale note removed 2026-09-28: nlohmann/json is private since 0.36.0 — opaque `Json` facade, no `find_dependency`, nothing installed.)
- **Namespace rename:** `caudio::cli` fully gone from code (`caudio::ipc` + `caudio::config`); zero `^import` in tests/cli/examples; `RepeatMode::Queue` → `All` incl. JSON wire string.
- **Install verified both modes:** header-only install ships headers + `FindFFmpeg.cmake`, no `.cppm`; modules-ON install ships 43 `.cppm` files under `<prefix>/modules/` (moved out of `include/` 2026-09-28; was 56 under `include/caudio/modules/`, 44 before the `:paths`/`:status`/`:audio` partitions were deleted 2026-09-30 as wrappers of non-installed `src/service/*` internals, 41 before `:function`/`:generator` were added 2026-10-02). Downstream full-module smoke (all 7 modules imported, live symbol per module) green.
- **`version_config.hpp` flow:** `cmake/version.hpp.in → configure_file → BINARY_DIR/include/caudio/version_config.hpp → install(FILES …)` is coherent.
- **Sanitizer helper:** target-scoped with WIN32/MSVC guards; per-test repetition removed via single `caudio_add_catch_test` helper.
- **Test skip mechanism:** `CAUDIO_TEST_NOAUDIO` compile def + env fallback + `CAUDIO_SKIP_IF_NOAUDIO()` macro, consistently used; ctest 158/158 green in default config (GCC + Clang + MSVC, 2026-10-02; one pre-existing m4a fixture skip).
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
- **Shared variants gated:** `*_shared` + `combined` behind `CAUDIO_BUILD_SHARED=OFF` (default); static + header-only is canonical. Verified OFF (144/144 tests) and ON configure + MinGW-GCC build 66/66 (2026-10-02). MSVC/macOS shared-link status unverified — full audit + cross-platform fix design in §5, which supersedes this note.
- **Version story hardened:** tag-less configure emits a loud `WARNING` (fallback is NOT a release build); `CAUDIO_REQUIRE_GIT_VERSION=ON` fails fast with a clear message. Stale `e.g. "v0.25.4"` doc strings removed from `version.hpp` / `version.hpp.in` / `ipc/result.hpp` — surrounding words already convey the format, so nothing rots on release.
- **FetchContent lazy:** `find_package(CLI11/Catch2)` first, fetch only as fallback (nlohmann_json is vendored in `vendor/nlohmann/`, no fetch at all — see `vendor/README.md`); CLI11 + the `caudio` executable skipped entirely with `CAUDIO_BUILD_CLI=OFF` (verified configure + build). nlohmann reaches components as plain PRIVATE `-I` (`CAUDIO_NLOHMANN_PRIVATE_INCLUDE` = `vendor/`); `CAUDIO_CLI11_EXTRA_INCLUDE` covers the exe (empty when system package provides the target).
- **AI comments:** stale `import`-wording narration in `tests/common.hpp` deleted; per-method `@par Thread safety` blocks verified as concise one-line locking contracts (kept); canonical locking section lives in `engine/engine_core.hpp` file docs (moved out of the `engine.hpp` umbrella 2026-09-28).

## Deferred post-1.0 (CLI behavior audit, 2026-10-03)

- **Playback-policy config options:** behaviors like whether `queue clear` stops playback,
  whether natural queue end stops or loops, and similar policy choices should become
  config-file options. Deferred: 1.0 keeps current behavior (`clear` leaves playback running)

---

*Method note: findings derive from glob/grep/read sampling per the task brief (not a full per-file audit). Line numbers are as observed at review time; verify with grep before editing. Items addressed in batches B1–B9, C1–C5, shared-lib gating, version/fetch/comment cleanup plus the namespace rename were removed in the 2026-09-27 pass; dependency bumps, library de-noise + naming unification, the documentation audit, and the include-hygiene/module-export pass were removed in the 2026-09-30 pass. KISS-4 deferred by user decision. Shared-library audit (§5, with MSVC repro + MinGW shared build evidence) added 2026-10-02; declined NAME-7 row removed same day. MSVC archive-bloat audit (§6, with `llvm-size`/`llvm-nm` per-TU evidence across all library objects + `extern template` recipe) added same day. No files other than this report were modified.*