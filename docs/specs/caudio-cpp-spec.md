# caudio-cpp Spec — Modern C++23 Port

**Status:** Draft (incremental, living document)  
**Date:** 2026-09-04  
**Source baseline:** `C:\Users\Secondary\Projects\caudio` strict C17 (`CMakeLists.txt:39-41`, `README.md:1`) — report `CPP_PORT_REPORT.md:1-989`  
**Target:** `caudio-cpp` — pure C++23 named modules, STL-only, no C++26, no `CA_` prefixes  
**Toolchain decided:** CMake 3.28+ + Ninja, `CMAKE_CXX_STANDARD 23` `REQUIRED ON` `EXTENSIONS OFF`, MSVC 19.4+ / GCC 14+ / Clang 17+, `find_package(Threads)`  
**Spec style:** C baseline → decided paths → pending options (each with pros/cons in simple language) → architecture

---

## 1. C Baseline (what we are porting from)

### 1.1 Purpose
Offline music player library — 4 static libs linkable independently (`CMakeLists.txt:143,210,257,277`), no network at build, vendored deps, cross-platform Win32/POSIX.

### 1.2 Four Libraries & Key Files

| Lib | Public headers | Core impl | Key invariants |
|-----|----------------|-----------|----------------|
| `utils` | `include/caudio/utils/*` (`ca_types.h:38`, `ca_ring.h:14`, `ca_cmd.h:13`) | `src/utils/ca_alloc.c:256` (CA_DEBUG), `ca_arena.c:24`, `ca_thread.c:25`, `ca_ring.c:10` SPSC, `ca_cmd.c:19` MPSC, `ca_log.c:6` | `ca_alloc` vtable+user ptr; arena 64B aligned cap+64; ring `_Atomic wr/rd` acquire/release, `float*` buf, cap*channels overflow check `ca_ring.c:29`; cmd cap 64 `ca_player_internal.h:13`, CAS+BUSY `ca_cmd.c:79` |
| `player` | `include/caudio/player/ca_player.h:12`, `ca_reader.h:14` | `src/player/ca_reader.c:18` (FILE vs mem, `_ftelli64`/`ftello`), `ca_decode.c:17` registry `g_vts[16]`, `ca_output.c:14` (miniaudio `ma_device` F32), `ca_player.c:40` (961 lines, `Impl` atomics, decode thread `ca_player_thread_fn:ca_player.c:156`) | Probe 32B `CA_DECODE_PROBE_BYTES:ca_decode.c:14`, arena 64KiB `ca_decode.c:42`, synthetic fallbacks (wav 8000/1, flac 44100/2, mp3 48000/2, vorbis 22050/1 `decoders/stb_vorbis.c:32`), preroll `RING_CAP/2` `ca_player.c:462`, position time-based `ca__now_ms:ca_player.c:91` via `GetTickCount64`/`CLOCK_MONOTONIC`, gate `open_gate`+`decode_busy` 500×1ms spin `ca_player.c:122` |
| `db` | `include/caudio/db/ca_db.h:12`, `ca_db_types.h:15` | `src/db/ca_db.c:2314` all ops, `ca_schema.c:4` (WAL+NORMAL+32768+FK, 10 tables+FTS5+3 triggers), `ca_write_thread.c:13` (float-ring hijack `floats_per_op=(sizeof(op)+3)/4`), `ca_scan.c:380` (SHA-256 first 64KiB `ca_scan.c:154`), `ca_search.c:141` (sanitize FTS `ca_search.c:64`) | Single recursive `db_lock` (`ca_db_internal.h:35` `PTHREAD_MUTEX_RECURSIVE`/CRITICAL_SECTION) around every prepare/step/finalize; writer batch + `flush` 200×1ms → BUSY `ca_write_thread.c:167`; fingerprint `BLOB(32) UNIQUE`; queue `queue_id` 0→1, dense `position` shift logic `ca_db.c:1033/1108`; FTS `porter unicode61`, fallback LIKE `ca_search.c:172`; JSON hand-escaped `ca_db.c:1623` + naive parser `ca_db.c:1921` + FNV fallback `ca_db.c:1899` |
| `engine` | `include/caudio/engine/ca_engine.h:7`, `ca_engine_types.h:12` | `src/engine/ca_engine.c:1012` (monitor `poll_ms 10`, gapless 300, event ring 64 `ca_engine.c:190`), `ca_queue_logic.c:11` (perm `vector<int64>` Fisher-Yates via `sqlite3_randomness:ca_queue_logic.c:178`, `shuffle_perm` blob), `ca_history_policy.c:3` (60%/90s) | `engine_state` single row `src/db/ca_schema.c:125` (shuffle blob+cursor+repeat+volume), `do_history_mark` CAS 0→1 `ca_engine.c:235` + `BEGIN IMMEDIATE` txn, `engine_tick` `ca_engine.c:319` (PROGRESS every 500ms, gapless `remaining≤gapless_ms` CAS `gapless_armed`), `ca__qs_next/prev` `ca_queue_logic.c:247/310` cursor semantics, `queue_lock` CAS BUSY |

Build: `CMakeLists.txt:62` options `CAUDIO_WITH_FFMPEG OFF`, `CAUDIO_BUILD_TESTS ON`, `CAUDIO_BUILD_EXAMPLES ON`; warnings `/W4` or `-Wall -Wextra -Wpedantic` `CMakeLists.txt:71`; vendor `sqlite3.c` 3.46.1 FTS5 `CMakeLists.txt:168`, `miniaudio.h`, `dr_*.h`, `stb_vorbis.c`; tests 37 CTest `CMakeLists.txt:388`.

### 1.3 C Behaviours to Preserve Exactly
- Probe order: wav→flac→mp3→vorbis→ffmpeg `ca_decode.c:235`; read 32B then restore offset `ca_decode.c:48`; synthetic fallback on `dr*` init fail still returns OK.
- Reader 64-bit seek clamp 0..size `ca_reader.c:143`; size via save/restore `ca_reader.c:129`.
- Ring wrap `memcpy` + `memory_order_acquire/release` `ca_ring.c:65/97`; output callback no-alloc, vol clamp NaN→0 `ca_output.c:48`.
- Player preroll `cap/2` on open, `1024` on seek, chunk `min(avail,1024,2048/ch)` `ca_player.c:156`.
- Scan identity: skip if `path+size+mtime` hit else hash first 64KiB SHA-256, then fingerprint branch (UPDATE+Dedup DELETE `ca_scan.c:263` vs same-path UPDATE clearing metadata `ca_scan.c:288` vs INSERT `ca_scan.c:305`).
- Search: sanitize → `MATCH ? ORDER BY rank` → prefix `*` → LIKE fallback 3-col `ca_search.c:141`.
- Queue shuffle: perm 0..n-1 Fisher-Yates `sqlite3_randomness` `ca_queue_logic.c:178`, persist blob+CAS cursor, `REPEAT_ONE` handled in `ca_engine.c:768` not `queue_logic`.

Known C weaknesses to fix in C++ (W1-W18 `CPP_PORT_REPORT.md:788`): recursive god mutex, float-ring type punning, CA_DEBUG fixed 4096 race, hand JSON fragility, stub vorbis/ffmpeg lying as success, thread join race, 500ms gate spin, no stmt cache, fixed buffers, 64KiB-only hash, mixed TLS error, incomplete FTS sanitizer, magic constants, sqlite OBJECT bloat, flaky timing tests, alloc `user` UAF, scattered `#ifdef _WIN32`.

---

## 2. Global Constraints & Decisions Already Made

### 2.1 Modern C++23 Mandate
- **Language:** `CMAKE_CXX_STANDARD 23` only; any C++26 feature ( `std::execution` P2300 senders/receivers, reflection `reflexpr`, `hazard_pointer`, `std::flat_map` improvements beyond C++23) is forbidden. `std::expected` (C++23), `std::print`/`std::format`, `std::generator` (C++23 `<generator>`), `std::mdspan` allowed if beneficial.
- **Modules, not headers:** Every component is a C++23 named module; no `hpp/cpp` split. Partitions for subcomponents. No `#pragma once` headers for public API. Vendor C headers wrapped via global module fragment `module; #include "miniaudio.h"` then `export module`.
- **Features to use where beneficial:** `concepts` + `requires`, `ranges`/`views`, `span`/`string_view`, `filesystem`, `chrono`, `jthread`/`stop_token`, `atomic` with `memory_order`, `pmr`, `variant`/`optional`, `constexpr`/`consteval`, `deducing this` (C++23), `[[nodiscard]]`, `[[maybe_unused]]`, `noexcept`.
- **Style:** No `CA_` prefixes. Namespaces `caudio::utils|player|db|engine` (+ `caudio::platform` for platform abstraction). Types `PascalCase` (`Player`, `Track`, `SpscRing`), methods `camelCase` (`create`, `openReader`, `setVolume`), free functions `snake_case` where appropriate, constants `kSnake` or `kPascal`. Files `snake_case` (`arena.cppm`, `scan.cppm` but as module partitions).

### 2.2 Decided Paths (from prior discussion)

#### D1 — Module Partitioning — `Partitioned` (chosen)
Partitions per logical unit:
```
export module caudio.utils;            // primary
export import :result; :error; :alloc; :arena; :thread; :ring; :queue; :log; :types;
export module caudio.player;           // import caudio.utils;
export import :reader; :decoder; :output; :player;
export module caudio.db;               // import caudio.utils;
export import :types; :schema; :database; :write_thread; :scan; :search; :json;
export module caudio.engine;           // import caudio.db; import caudio.player; import caudio.utils;
export import :types; :queue_logic; :history_policy; :engine;
export module caudio;                  // umbrella re-export
```
Pros vs flat/single: introduces logical grouping, single `import caudio.utils;` instead of N imports, still allows `import caudio.utils:ring;` for fine-grained tests; easier to enforce layer rule `utils < player,db < engine`. Cons: partition syntax slightly more complex for CMake FILE_SET. Accepted.

#### D2 — Error Handling — `Pure std::expected` (chosen)
All fallible public APIs: `[[nodiscard]] expected<T, Error>`. `Error { Result code; std::string message; }`, `enum class Result : int { Ok, InvalidArg, NotFound, Unsupported, Io, Device, State, NoMem, Internal, AlreadyExists, Busy, Corrupt, NoSpace }`. Factories: `static expected<unique_ptr<T>, Error> create(...)` (ctors never throw). Audio callback `noexcept`, DB internals no exceptions, helper `SqliteError` → `Error` mapping. TLS `g_tls_last_error` removed; per-object `Error lastError_` guarded by mutex, plus `expected` carries payload.

#### D3 — Async — `A + limited C` (chosen)
- Hot paths (decode thread, writer, engine monitor, output): `std::jthread` + `std::condition_variable` + typed `MpscQueue<T>` + `std::stop_token` + `std::atomic` gates. No sleeping spin; `cv.wait(lock, pred)` with timeout for monitor tick.
- Cold lazy path (scan): `std::generator<const Track&>` (C++23) or `generator<Track>` coroutine for `for (auto &t : db.scan(path))` — optional, built on top of `jthread`+queue if added.
- Rationale: keeps RT guarantees, deterministic joins, no coroutine frame allocation in audio path.

---

## 3. Pending Decisions — Each Path with Pros/Cons (simple language)

> User asked: explain every path with pros/cons before asking questions. Below is exhaustive. Questions will follow after you review.

### 3.1 Memory / Allocator — DECIDED: No custom allocator (Option C0)

Decision: **C0 — No custom allocator (your pick).** Delete `ca_alloc` `ca_types.h:38` pervasive plumbing. All factories are `static Expected<unique_ptr<Player>> create(PlayerOpts)` with no `memory_resource` param; internals use `make_unique`/`vector`/`string` default heap. Decode arena is private `array<byte,64K>`+bump or local `pmr::monotonic_buffer_resource` not exposed. Leak via ASan/LSan, not hand map `ca_alloc.c:19` `W3/W17` removed.

### 3.2 DB Concurrency & Transactions — DECIDED: `shared_mutex` + `Transaction` (Option B)

Decision: **B — `std::shared_mutex` + RAII `Transaction` + no hold across I/O (your pick).**
`mutable shared_mutex m_;` readers `shared_lock`, writers `unique_lock`; I/O (`fileSize`, `hash`) done before lock; inner helpers take `Transaction&` (no re-lock), fixing `W1/W13` recursion and enabling WAL concurrent reads.

### 3.3 Writer Thread — DECIDED: Non-blocking typed queue (Option B)

Decision: **B — Keep non-blocking architecture (your pick) — Typed `MpscQueue<WriteOp>` + `jthread` + `cv` (not float-ring `W2`).** `WriteOp { string sql; unique_ptr<Statement> stmt; function<void(Expected<void>)> cb; }`, `deque+mutex+cv` cap 256 `Busy` (`ca_cmd.c:79` parity), `flush()` `wait_for(200ms)` `ca_write_thread.c:167`, `inFlight` atomic, callback outside lock. D (direct sync) would be simpler but you chose non-blocking for UI not to block on burst inserts.

### 3.4 JSON Import/Export — DECIDED: `nlohmann/ordered_json` (Option B)

**C today:** Hand `json_escape:ca_db.c:1623` + naive `find_json_field:ca_db.c:1793` parser, FNV fallback `ca_db.c:1899`, fragile `W4`.

Decision: **B — `nlohmann/json` `ordered_json` via vendored single header (FetchContent fallback).** Export via `ordered_json j; j["tracks"]=tracks; out << j.dump(2);` Import via `auto j=json::parse(buf); for(auto &o: j.at("tracks")) o.get_to(t);` Robust unicode, fuzzable, offline vendored. Field order preserved via `ordered_json` for C golden parity.

### 3.5 Decoders — LOCKED: FFmpeg required for all formats, fallback to minimal `dr_*` (your final Q)

Decision: **FFmpeg required to process all formats; only if FFmpeg dev libs absent at `find_package` time does code fall back to built-in minimal `dr_*` decoders.** Stick with current `dr_*`+real `stb_vorbis` only as fallback (not alternatives OpenMedia/Nuclex — low trust). `CAUDIO_WITH_FFMPEG=ON` **required** in C++ port (vs OFF `CMakeLists.txt:62` in C); `find_package(FFmpeg)` via `C:\Users\Secondary\ffmpeg\include`/`lib` `8.1.2-full_build` now present. Decoder registry `g_vts[16]` `ca_decode.c:235` probed as FFmpeg first, then `dr_wav`/`dr_flac`/`dr_mp3`/`stb_vorbis` fallback only if `FFmpeg::avcodec` not found — `DecoderFactory::create(Reader&)` returns `Expected<unique_ptr<IDecoder>>` trying FFmpeg then fallback; `probe 32B` `ca_decode.c:14` kept for fallback order.

- `dr_libs`+`stb_vorbis` kept vendored `vendor/dr_*.h` + `stb_vorbis.c` via `module; #include` fragment + RAII wrappers, but **secondary**.
- Broader C++ libs (OpenMedia/Nuclex/avioflow/musac) rejected — low stars/trust, wrap `dr_*` anyway.

**System FFmpeg check (this host `C:\Users\Secondary\ffmpeg`):** `ffmpeg 8.1.1-essentials_build` present (`C:\Users\Secondary\ffmpeg\bin\ffmpeg.exe`), but **essentials_build = runtime only** (`bin/ffmpeg.exe` 101MB, `doc/` only, no `lib/`/`include/`). No `avcodec.h`/`libavcodec.dll.a` found — can't link.

**How to install FFmpeg dev libs on this Windows (MinGW GCC 14.2 + CMake 3.31 + Ninja 1.12, no vcpkg/pkg-config):**

| Method | Steps for this host | Result |
|--------|---------------------|--------|
| **A — Full build with dev (gyan.dev)** — simplest, no tooling | Download `ffmpeg-8.1.1-full_build.7z` from gyan.dev, extract to `C:\Users\Secondary\ffmpeg-dev` → `include/libavcodec/*.h` + `lib/avcodec.lib` + `bin/avcodec-62.dll` (or `.a` for MinGW). Then `set(FFMPEG_ROOT C:/Users/Secondary/ffmpeg-dev)` ; CMake `find_path`/`find_library` picks it. | Headers + import libs + dlls for both runtime + link |
| **B — vcpkg** (needs bootstrap) | `git clone https://github.com/microsoft/vcpkg C:\vcpkg && C:\vcpkg\bootstrap-vcpkg.bat && C:\vcpkg\vcpkg install ffmpeg[core,avcodec,avformat,avutil,swresample] --triplet x64-mingw-static` then `cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake` | Managed libs, CMake auto `FFMPEG_*` |
| **C — MSYS2** (if MSYS2 installed) | `pacman -S mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-ffmpeg-devel` + `pacman -S mingw-w64-x86_64-pkg-config` | `pkg-config --modversion libavcodec` works |
| **D — Conda** | `conda install -c conda-forge ffmpeg libavcodec-devel` | Less ideal for system-wide |

For this host (**no MSYS2/vcpkg/pkg-config, but `D:\mingw64\bin\g++.exe` 14.2 present**): **A is fastest** — replace essentials with full_build. After install test: `cmake -B build -DCAUDIO_WITH_FFMPEG=ON` + `ffprobe -version` should show dev libs.

**dlopen replacement:** No `std::shared_library` in C++23 (`P0275R2` never adopted, C++26 still no `std::dll`). C++ still uses `dlopen`/`dlsym` vs `LoadLibrary`/`GetProcAddress` via RAII wrapper (`dylib::library`, `boost::dll`). We will **not use dlopen** for FFmpeg — link at build via `find_package(FFmpeg CONFIG)` → `target_link_libraries(caudio_player PRIVATE FFmpeg::avcodec ...)`. `dlopen` only for true runtime plugin hot-reload.

### 3.6 Audio Output Backend — DECIDED: Switch to RtAudio (+ Oboe later) + `std::generator`

Decision: **Switch to RtAudio (and Oboe for Android) + confirm `std::generator` (your pick).**
- **RtAudio:** C++ `RtAudio` class RAII, desktop YES (Linux ALSA/JACK/Pulse/OSS, macOS CoreAudio+JACK, Windows DS/ASIO/WASAPI), single `RtAudio.h/.cpp`, maintained (push 2026-02-27, 6.0.1). Replaces `miniaudio.h` `src/player/ca_output.c:27`; `AudioOutput` becomes `class AudioOutput { RtAudio dac; RtAudio::StreamParameters oParams; atomic<float> volume; SpscRing<float>* ring; }` callback `noexcept`.
- **Oboe (Android later):** `Oboe` wraps `AAudio` (27+) + `OpenSL ES` fallback, builder pattern, `AudioStreamCallback`. RtAudio has no Android `Api` enum — need Oboe for mobile; behind `#ifdef __ANDROID__` `platform/` abstraction.
- **Do we need miniaudio still?** No — replaced. Keep as fallback option `CAUDIO_WITH_MINIAUDIO` if needed but default is RtAudio.
- **`std::generator` confirmed:** `import std;` `<generator>` (C++23) for `db::scan` (`generator<const Track&> scan(path)` `co_yield`) and cold `trackList` generator. Hot paths stay `jthread+cv`.

External deps C vs C++ after switch:
| Dep | Language | **C tool?** |
|-----|----------|-------------|
| `RtAudio` | C++ | No |
| `Oboe` (Android) | C++ (AAudio/OpenSL) | No |
| `dr_wav.h`/`dr_flac.h`/`dr_mp3.h` | C single header | **Yes** |
| `stb_vorbis.c` (now real) | C | **Yes** |
| `sqlite3.c` | C | **Yes** |
| `FFmpeg` (`libavcodec`) | C | **Yes** |
| `BLAKE3` C core (3.8) | C | **Yes** (C core) |
| `nlohmann/ordered_json` (3.4) | C++ header | No |
| `Catch2` v3 (3.11) | C++ | No |
| `OpenMedia`/`avioflow` (opt broad decoders) | C++20 | No |

`CMake`/`Ninja`/`clang-tidy`/`clang-format` remain tools.

### 3.7 Logging — DECIDED: Injected callback (Option B)

Decision: **B — Injected `std::function<void(LogLevel,string_view)>` via opts, `std::format`/`std::print` (C++23), per-object with `mutex`, no global.** Hot path never logs. `spdlog` remains opt-in via `find_package`.

### 3.8 Filesystem & Fingerprinting — DECIDED: Sampled Fast + BLAKE3 (Option C)

Decision: **C sampled `64K head + 64K tail + fileSize` via BLAKE3 (your pick) — Fast sweet spot.** `std::filesystem::recursive_directory_iterator` (`W18` fix), `BLAKE3_hasher` streaming first/last 64K + `size` mixed in, `fingerprint_version` column for migration from C prefix. I/O ~1.2GB for 10k×50MB vs 500GB full, collision near-zero. `Full` mode remains optional via `ScanMode::Full`.

### 3.9 Search — DECIDED: Proper FTS5 quoting (Option B)

Decision: **B — Proper FTS5 quoting per term `"\""+escaped+"\""` + `rank`, LIKE fallback on all 5 cols (`title/artist/album/album_artist/genre`) `COLLATE NOCASE`, `unicode61 "remove_diacritics 2"`.** Fixes `W12` incomplete sanitizer.

### 3.10 Build & SQLite Vendoring — DECIDED: Single STATIC (Option B)

Decision: **B — Single `caudio_sqlite` STATIC built once from `vendor/sqlite3.c` (FTS5), `target_link_libraries(caudio_db PUBLIC caudio_sqlite)` transitive; fallback `find_package(SQLite3)` via FetchContent.** Fixes `W15` 3× bloat.

### 3.11 Testing & Sanitizers — DECIDED: Catch2 + Mock Clock (Option B)

Decision: **B — Catch2 v3, deterministic `Clock` concept mocked via `steady_clock` injection for Player/Engine, golden PCM per format, `importJson` fuzz via `libFuzzer`, `ctest -T memcheck` ASan+UBSan, `bench_ring` microbench.** Fixes `W16` flaky sleeps.

---

## 4. Proposed C++ Architecture (sketch)

### 4.1 Namespaces & Concepts
```cpp
namespace caudio {
namespace utils {
  enum class Result : int { Ok, InvalidArg, NotFound, Unsupported, Io, Device, State, NoMem, Internal, AlreadyExists, Busy, Corrupt, NoSpace };
  std::string_view toString(Result) noexcept;
  struct Error { Result code; std::string message; };
  template<class T> using Expected = std::expected<T, Error>;
  concept DecoderConcept = requires(T t, std::span<const uint8_t> probe) { { t.probe(probe) } -> std::same_as<bool>; };
}
namespace player {
  class Reader { public: virtual ~Reader(); virtual size_t read(std::span<std::byte>) = 0; virtual Expected<void> seek(int64_t, int whence) = 0; };
  class FileReader final : public Reader { /* FILE* or ifstream behind unique_ptr */ };
  class MemoryReader final : public Reader { std::span<const std::byte> data; size_t pos; };
  struct IDecoder { virtual ~IDecoder()=default; virtual size_t decode(std::span<float>) = 0; virtual Expected<void> seek(double) = 0; uint32_t sample_rate{}, channels{}; uint64_t total_frames{}; };
  class Player { public: static Expected<std::unique_ptr<Player>> create(const PlayerOpts&, shared_ptr<pmr::memory_resource>); Expected<void> open(std::filesystem::path); /* ... */ private: struct Impl; unique_ptr<Impl> impl_; };
} // player
namespace db {
  struct Track { int64_t id{}; std::array<uint8_t,32> fingerprint{}; std::string path; std::string title, artist, album, albumArtist, genre; /* ... */ };
  class Database { public: static Expected<unique_ptr<Database>> open(std::filesystem::path, DbOpts, shared_ptr<pmr::memory_resource>); Expected<int64_t> insert(const Track&); generator<const Track&> list(const Query&) const; Expected<void> flush(); private: unique_ptr<sqlite3, SqliteDeleter> handle_; mutable shared_mutex mutex_; unique_ptr<WriterThread> writer_; };
}
namespace engine {
  enum class RepeatMode { Off, Queue, One };
  struct EngineEvent { EngineEventType type; int64_t trackId{}, queueId{}; double pos{}, dur{}; std::string message; };
  class Engine { public: static Expected<unique_ptr<Engine>> open(std::filesystem::path db, EngineOpts); static Expected<unique_ptr<Engine>> attach(Database&, player::Player&, EngineOpts); Expected<void> play(int64_t queueId); Expected<void> next(); Expected<void> prev(); Expected<EngineEvent> pollEvent(); private: struct Impl; unique_ptr<Impl> impl_; };
}
} // caudio
```

### 4.2 Module Layout (partitioned)
```
caudio-cpp/
├── CMakeLists.txt               # CXX 23, FILE_SET CXX_MODULES
├── include/caudio/  (no longer, replaced by modules/)
├── src/
│   ├── utils/
│   │   ├── utils.cppm           // export module caudio.utils;
│   │   ├── result.cppm          // partition :result
│   │   ├── error.cppm
│   │   ├── alloc.cppm           // TrackingResource
│   │   ├── arena.cppm           // 64K array bump (no pmr plumbing, 3.1 C0)
│   │   ├── thread.cppm          // jthread + this_thread::sleep_for
│   │   ├── ring.cppm            // SpscRing<T>
│   │   ├── queue.cppm           // MpscQueue<T>
│   │   └── log.cppm
│   ├── player/
│   │   ├── player.cppm          // export module caudio.player;
│   │   ├── reader.cppm          // :reader (FileReader/MemoryReader)
│   │   ├── decoder.cppm         // IDecoder + DecoderRegistry (OpenMedia/avioflow optional)
│   │   ├── output.cppm          // AudioOutput RtAudio wrapper + Oboe for Android
│   │   ├── player_impl.cppm     // Player::Impl + jthread
│   │   └── decoders/ (wav.cppm, flac.cppm, mp3.cppm, vorbis.cppm real stb_vorbis, ffmpeg.cppm)
│   ├── db/
│   │   ├── db.cppm              // export module caudio.db;
│   │   ├── types.cppm
│   │   ├── schema.cppm          // constexpr string_view kSchema
│   │   ├── database.cppm        // shared_mutex + Transaction
│   │   ├── write_thread.cppm    // non-blocking MpscQueue (3.3 B)
│   │   ├── scan.cppm            // generator<const Track&> + BLAKE3 sampled (3.8 C)
│   │   ├── search.cppm
│   │   └── json.cppm            // nlohmann::ordered_json
│   └── engine/
│       ├── engine.cppm
│       ├── types.cppm
│       ├── queue_logic.cppm
│       ├── history_policy.cppm
│       └── engine_impl.cppm
├── platform/ (audio_backend.cppm, filesystem abstraction)
├── vendor/ (RtAudio.h/.cpp, sqlite3.c/h, dr_*.h kept for fallback, stb_vorbis.c, BLAKE3, nlohmann_json)
├── tests/ (Catch2 TEST_CASE, helpers)
├── examples/ (mini.cpp etc. import caudio;)
└── docs/specs/caudio-cpp-spec.md (this file)
```

### 4.3 Key Design Rules
- Layer rule preserved: `utils` has no deps; `player` imports `utils`; `db` imports `utils`; `engine` imports all three.
- Audio callback: `noexcept`, no alloc, `atomic<float> volume` relaxed, `SpscRing::read` acquire/release identical to `ca_ring.c`.
- PIMPL: `Player::Impl`, `Engine::Impl`, `Database::Impl` hide `jthread`, `mutex`, `sqlite3*`.
- Concepts: `DecoderConcept`, `AudioBackendConcept`, `ClockConcept` for mock time.
- Ranges: `views::filter`, `ranges::sort` for leak dump.

---

## 5. Checklist (incremental)

- [ ] Scaffold repo with modules + CMake 3.28 FILE_SET CXX_MODULES, alias `caudio::utils` etc.
- [ ] Port `utils` partitions, Catch2 `test_utils_*` parity
- [ ] Port `reader` + `decoder` registry + 4 decoders (real vorbis) + `output`
- [ ] Port `Player` with `jthread`+`cv`+`SpscRing`, no spin
- [ ] Port `schema` + `Database` with `shared_mutex`+`Transaction`+`Statement` cache
- [ ] Replace writer float ring with typed `MpscQueue<WriteOp>`
- [ ] Port `scan` via `filesystem`+`generator`+full SHA, `search` with proper FTS
- [ ] Port JSON via `nlohmann::ordered_json`
- [ ] Port `engine` queue/history/monitor with `jthread`+`pollEvent`
- [ ] Port `examples/*` to `import caudio;`
- [ ] Bring 37 tests to Catch2, add golden PCM + fuzz + bench
- [ ] `clang-tidy` `modernize-*,bugprone-*,concurrency-*,cppcoreguidelines-*` clean, `clang-format` 100col
- [ ] ASan+UBSan + `helgrind` on race tests

---

## 6. Decisions Locked (all resolved)

All §3 now decided per your confirms:
- **3.1 C0** no custom alloc (local 64K arena, ASan)
- **3.2 B** `shared_mutex`+`Transaction` + no hold across I/O
- **3.3 B** non-blocking `MpscQueue<WriteOp>`+`jthread`
- **3.4 B** `nlohmann/ordered_json`
- **3.5** Vorbis B real `stb_vorbis`, FFmpeg C→B behind `CAUDIO_WITH_FFMPEG`; optional broad C++ decoders **OpenMedia/avioflow** for AAC/Opus/M4A vs keep `dr_*` fallback
- **3.6** Switch to **RtAudio** (+ Oboe for Android later) + **`std::generator` confirmed** `import std` `<generator>` (your picks)
- **3.7 B** injected log
- **3.8 C** sampled `64K+64K+size` BLAKE3 + `fingerprint_version`
- **3.9 B** FTS5 proper quoting
- **3.10 B** single `caudio_sqlite` STATIC
- **3.11 B** Catch2 + mock clock

### Build + Packaging Workflow (reference: `C:\Users\Secondary\Projects\caudio\CMakeLists.txt:1`)

C workflow mapped to C++23 modules:
| C (`CMakeLists.txt:37`) | C++23 port |
|------------------------|------------|
| `cmake_minimum_required(3.20)` + `project(caudio LANGUAGES C)` `CMAKE_C_STANDARD 17` `EXTENSIONS OFF` `EXPORT_COMPILE_COMMANDS ON` `CMakeLists.txt:39-42` | `cmake_minimum_required(3.28)` (modules require 3.28 FILE_SET CXX_MODULES) + `project(caudio VERSION ${PROJECT_VERSION} LANGUAGES CXX)` `CMAKE_CXX_STANDARD 23` `REQUIRED ON` `EXTENSIONS OFF` `EXPORT_COMPILE_COMMANDS ON` |
| `git describe --tags`→`PROJECT_VERSION`→`configure_file(ca_version.h.in→ca_version.h)` + copy headers `CMakeLists.txt:4-49` | Same git logic `CMakeLists.txt:4-35` reused, but template `ca_version.hpp.in` → `version.cppm`? Keep `configure_file` to `build/include/caudio/version.hpp` then `FILE_SET` exposes as module `caudio:version` |
| `option(CAUDIO_WITH_FFMPEG OFF)`, `BUILD_TESTS`, `BUILD_EXAMPLES` `CMakeLists.txt:62` | Same + `option(CAUDIO_WITH_BROAD_DECODERS OFF)` for OpenMedia/avioflow |
| `find_package(Threads)` `ca_set_warnings(/W4/-Wall)` `CMakeLists.txt:66` | Same, plus `find_package(RtAudio)` or vendored `RtAudio.cpp` as OBJECT |
| 4 libs `caudio_utils/player/db/engine` STATIC+SHARED via `add_library` + ALIAS `caudio::` `CMakeLists.txt:79-277` | Same pattern but `add_library(caudio_utils STATIC)` with `target_sources(FILE_SET CXX_MODULES TYPE CXX_MODULES FILES src/utils/utils.cppm ...)` + ALIAS `caudio::utils` etc.; vendor `sqlite3.c` as **single** `caudio_sqlite STATIC` PUBLIC link (fixes `W15` 3× OBJECT) `CMakeLists.txt:167-199` |
| `target_link_libraries` chain utils<player,db<engine `CMakeLists.txt:143,210,257` | Same layer rule `utils < player,db < engine` via `import` but CMake links as before |
| `add_library(caudio_shlib SHARED combined)` `OUTPUT_NAME caudio` `CMakeLists.txt:298` | `add_library(caudio SHARED` combined modules) `OUTPUT_NAME caudio` — still provides dll/so |
| `install(TARGETS ... EXPORT caudioTargets)` + `install(DIRECTORY include/)` + `configure_package_config_file` + `write_basic_package_version_file` + `CPack TGZ/ZIP` `CMakeLists.txt:334-382` | Same `GNUInstallDirs` + `install(TARGETS ... FILE_SET CXX_MODULES DESTINATION ...)` + `install(EXPORT caudioTargets NAMESPACE caudio::)` + BMI not installed (consumers via `find_package` need CMake 3.28); `CPack` TGZ/ZIP vendor `caudio@example.com` as before |
| `ca_add_test/helpers` `enable_testing()` `EXTRA_LIBS m/dl/winmm` `CMakeLists.txt:384` | `Catch2` via `FetchContent` `catch_discover_tests`, same `ca_add_*_test` wrappers but `target_link_libraries` to `Catch2::Catch2WithMain` |
| Packaging: `CPACK_PACKAGE_NAME caudio`, `CPACK_GENERATOR TGZ;ZIP` | Same, version from git tag |

**Do we have to provide a DLL?** No mandatory DLL. C `CMakeLists.txt:93-305` builds both STATIC (`caudio_utils`) and SHARED (`caudio_utils_shlib` OUTPUT_NAME `caudio_utils` + `caudio_shlib` combined) to support both link modes + `find_package(caudio CONFIG)`. In C++ modules you can provide **only STATIC** — modules are compiled to BMI + .o and archived; consumers `import caudio;` works without dll. We will keep samedual build (STATIC + SHARED) for compatibility and `CAUDIO_BUILD_SHARED` export macro, but SHARED dll is optional (`BUILD_SHARED_LIBS` OFF → archives only). Distribution is possible as static-only via `CPack`.

### C++ Decoder Broader Coverage — Summary from research
See §3.5 table; `dlopen` remains `dlopen/LoadLibrary` wrapped in `dylib`/RAII (`boost::dll` style) — no `std::shared_library` in C++23 (proposal `P0275R2` not adopted). Prefer CMake `find_package(FFmpeg)` link over dlopen when `WITH_FFMPEG=ON`.

Next: scaffold C++23 modules skeleton per this locked spec.

---

### 7. C++ Shared Library Management (your Q)

C `CMakeLists.txt:79-305` builds both STATIC (`caudio_utils`) + SHARED (`caudio_utils_shlib` `OUTPUT_NAME caudio_utils` `CMakeLists.txt:93`) + combined `caudio_shlib` (`OUTPUT_NAME caudio` `CMakeLists.txt:306`) + `install(TARGETS ... EXPORT)` + `CAUDIO_*_EXPORTS`/`CAUDIO_BUILD_SHARED` `CMakeLists.txt:109`.

In C++23 modules:
- **STATIC vs SHARED still both work.** `add_library(caudio_utils STATIC)` with `FILE_SET CXX_MODULES` produces `.a`/`.lib`; `SHARED` produces `.dll`+`.dll.a` (MinGW import lib) / `.so` / `.dylib`. BMI (`.bmi` / `gcm`) is not part of dll — it's build-time only via `CMake 3.28 FILE_SET CXX_MODULES`.
- **Do we need a DLL?** No. Modules can be static-only (`BUILD_SHARED_LIBS OFF` → archives). Shared needed only if you want runtime `LoadLibrary`/`dlopen` or ABI-stable `find_package(caudio CONFIG)` with `caudio::engine_SHARED`.
- **Windows specifics (MinGW `D:\mingw64\bin\g++.exe` 14.2):** SHARED builds need `__declspec(dllexport)` on API classes (`CAUDIO_EXPORT` → `__declspec(dllexport)` when `CAUDIO_BUILD_SHARED` defined, `dllimport` for consumers). MinGW generates import lib `libcaudio_utils.dll.a` alongside `caudio_utils.dll`. Runtime needs dll beside `.exe` or on `PATH`; no `SOVERSION` like Linux. Use `set_target_properties(... OUTPUT_NAME caudio_utils)` keeps C naming.
- **Linux/macOS:** `SOVERSION`/`VERSION` (`set_target_properties(... SOVERSION 0 VERSION 0.1.0)`), `GNUInstallDirs`, `RPATH` via `$ORIGIN`. `find_package` handles `caudioTargets.cmake` `NAMESPACE caudio::`.
- **CMake usage:** `find_package(caudio CONFIG)` → `target_link_libraries(app PRIVATE caudio::engine)` pulls transitive `caudio_db` + `caudio::sqlite` + `RtAudio` via `caudioTargets.cmake` `CMakeLists.txt:349`. `CPack TGZ/ZIP` bundles `lib/`+`bin/`+`cmake/`+BMI if needed.
- **FFmpeg shared:** if `find_package(FFmpeg)` links `FFmpeg::avcodec` SHARED, you ship `avcodec-62.dll`/`libavcodec.so.62` beside your `caudio_*.dll` or set `FFMPEG_ROOT` `bin/`.

---

*Locked — all §3 decided: 3.1 C0, 3.2 B, 3.3 B, 3.4 B, 3.5 dr_*+FFmpeg default (dev install pending), 3.6 RtAudio+Oboe, 3.7 B, 3.8 C sampled BLAKE3, 3.9 B, 3.10 B, 3.11 B, generator confirmed. Next: install FFmpeg dev (method A) then scaffold.*

*End of draft — update in place as decisions land. For C behavior references see `CPP_PORT_REPORT.md:1-989` file_path:line_number citations above.*
