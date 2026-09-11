# Polyglot Integration Strategy — caudio-cpp

> Evaluates strategic delegation of subsystems to other languages per `prompt.md` Part 5. Core C++23 playback engine remains authoritative; polyglot boundaries are strictly out-of-band (never inside real-time audio callback).

## Decision Matrix

| Subsystem Target | Target Language | Binding Mechanism | Justification (Why C++23 Insufficient) | FFI Safety Assessment |
|---|---|---|---|---|
| **Metadata Parser** — ID3/FLAC/Vorbis tag parsing, untrusted file header probing, network stream sniffing | **Rust** | `cxx` crate, C-ABI `extern "C"` | Rust ownership eliminates memory-safety CVEs in adversarial tag buffers; `tokio` async IO outperforms manual `std::jthread` pools for concurrent probing. Ecosystem crates `lofty`/`symphonia`/`metaflac` are mature and fuzz-tested. Manual `detail::hasAudioExt`/`sanitizeFtsTerm` loops in `src/db/detail.cppm:116` are error-prone. | **Zero RT impact.** Parser runs on `WriterThread` / `scan.cppm:46` `std::generator` thread, never in `SpscRing::write` `src/utils/ring.cppm:52` or `AudioOutput::dataCallback` `src/player/output.cppm:142`. Opaque `*mut u8` handle owned by Rust; C++ retains `unique_ptr` RAII boundary. No bidirectional `shared_ptr`. |
| **Cross-Platform GUI** — Queue management, playback controls, spectrum bars, library browser (AIMP/Musicolet-inspired) | **Svelte / TypeScript + Tauri** | Tauri IPC (JSON `cli/src/shared/protocol.cppm:218` already `ordered_json`), optional WASM via Emscripten for lightweight engine components | Web stack delivers rich theming, hot reload, responsive layouts and mobile targets (via Capacitor) without Qt/GTK/WinUI bloat. `output_formatter.cppm` already serializes `Result` → JSON. Native C++ UI toolkits require per-platform widget code and slow compile times. | **Zero RT impact.** UI is separate Tauri process; communicates via `MpscQueue<EngineEvent>` `src/engine/engine.cppm:1392` out-of-band and `IpcChannel` UDS. Audio `SpscRing` + `decoder` remain in isolated C++23 process. No FFI in decode callback. |
| **Queue / Playlist Scripting** — Weighted shuffle prototyping, automated playlist generation, batch tagging tools, test orchestration | **Python** | `nanobind` / `pybind11` around `caudio::engine` + `caudio::db` | Python enables <10-line iteration on algorithms that would be 100+ lines of templated C++23; access to `numpy`/`pandas` for audio feature analysis, `pytest` for suite orchestration. `queue_logic.cppm:13` Fisher-Yates tuning benefits from rapid experimentation. | **Zero RT impact.** Bindings invoked from `cli/src/app/app.cppm:334` CLI handlers and offline scripts, never from audio thread. Opaque handles; Python holds non-owning view, C++ retains ownership. GC pauses isolated to scripting process. |
| **Network Services** — Local media streaming (DLNA/UPnP, WebDAV, HTTP media server), remote-control IPC, companion mobile sync | **Go** | Unix Domain Sockets, gRPC, or C-ABI shared library | Go `net/http` stdlib + goroutines provide ~1 KB/stack concurrency vs OS threads; ideal for many concurrent stream clients. `ipc_channel_unix.cpp` already UDS-based, so Go sidecar is natural sidecar pattern. | **Zero RT impact.** Go service is separate OS process; IPC via `protocol::frame` `cli/src/shared/protocol.cppm:755` over UDS. No language runtime embedded in headless `caudio` binary. Serialization delay isolated to network thread. |

## Guardrails Enforced

1. **FFI Boundary Overhead (STRICT):** No language binding or IPC inside `src/player/output.cppm:142` `dataCallback` or `src/utils/ring.cppm:52` per-sample loop. All polyglot calls are out-of-band (UI, metadata, network, scripting).
2. **Embedded Runtime Bloat:** No full Python/Go runtime embedded in headless executable. Core operations use `std::filesystem`, `std::expected`, `std::print` natively; external runtimes are sidecar processes or optional `pip`/`go run` tooling.
3. **Dual Ownership:** No bidirectional `std::shared_ptr` across FFI. C++ retains explicit ownership of native resources; foreign language holds opaque raw pointer wrapped in native drop/deleter (`cxx` `UniquePtr`, `nanobind` capsule, Tauri handle).

## Recommended Incremental Rollout

1. **Phase 1 — Python bindings** (`nanobind`): lowest risk, immediate velocity for queue algorithm experimentation; no deployment change.
2. **Phase 2 — Tauri GUI** (`Svelte`): replaces CLI-only frontend; reuses existing `cli/src/shared/protocol.cppm` JSON framing.
3. **Phase 3 — Rust metadata crate** (`cxx`): harden untrusted input surface; gated behind `CAUDIO_WITH_RUST_META` CMake option.
4. **Phase 4 — Go sidecar** (UDS/gRPC): optional feature for network streaming; separate binary `caudio-serve`.
