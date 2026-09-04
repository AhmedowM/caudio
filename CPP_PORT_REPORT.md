# C++ Port Report — caudio (C17 → Modern C++17/20/23)

**Generated:** 2026-09-04  
**Source repo:** `C:\Users\Secondary\Projects\caudio`  
**Current version:** 0.1.0 (`CMakeLists.txt:20`, `include/caudio/ca_version.h.in:5`) — `git describe --tags` fallback  
**Intended audience:** AI implementer producing a pure modern C++ rewrite using STL + C++17/20/23 features  
**Build-verified via:** `CMakeLists.txt:1`, `README.md:1`, full source read (include/src/tests/examples/vendor/cmake)

---

## 1. Project Overview

### 1.1 Purpose

`caudio` is an **offline music player library** in **strict C17** (`-std=c17`, `STANDARD_REQUIRED ON`, `EXTENSIONS OFF` per `CMakeLists.txt:39-41`). It provides four focused **static libraries** for allocation, audio playback, persistence, and orchestration that can be linked independently. No network at build time; all vendor deps are vendored. README tagline at `README.md:9`.

Key properties: cross-platform (Windows MSVC, Linux/macOS GCC/Clang), offline-first, leak-tracked debug mode, sanitizer-clean, 37 CTest tests.

### 1.2 Current Tech Stack

| Aspect | Current | Reference |
|--------|---------|-----------|
| Language | Strict C17, warnings `/W4` or `-Wall -Wextra -Wpedantic` | `CMakeLists.txt:71-76`, `README.md:14` |
| Build | CMake 3.20+ (`cmake_minimum_required` at `CMakeLists.txt:1`), GNUInstallDirs, CPack TGZ/ZIP, `CMAKE_EXPORT_COMPILE_COMMANDS ON` | `CMakeLists.txt:42`, `334` |
| Compiler support | MSVC 19+, GCC 11+, Clang 14+ | `README.md:54` |
| Threading | POSIX pthreads (`Threads::Threads`) or Win32 `CreateThread`/`CRITICAL_SECTION`; `<sched.h>` priority, `nanosleep`/`Sleep` | `src/utils/ca_thread.c:1`, `CMakeLists.txt:67` |
| Atomics | `<stdatomic.h>` (`_Atomic`, `memory_order_*`) | `src/utils/ca_ring.c:5`, `src/player/ca_player.c:12` |
| Alloc | Pluggable `ca_alloc` vtable (`malloc/calloc/realloc/free` + user ptr) | `include/caudio/utils/ca_types.h:38` |
| Audio output | `vendor/miniaudio.h` (single header) via `ma_device` F32 playback | `src/player/ca_output.c:6`, `vendor/miniaudio.h` |
| Decoding | Bundled `dr_wav`/`dr_flac`/`dr_mp3` + `stb_vorbis.c` (single-header/C); optional `FFmpeg` via `dlopen`/`LoadLibrary` | `vendor/dr_*.h`, `src/player/decoders/*` |
| DB | `vendor/sqlite3.c` 3.46.1 amalgamation with `SQLITE_ENABLE_FTS5=1`; WAL + FTS5 | `CMakeLists.txt:168`, `src/db/ca_schema.c:4` |
| Crypto/hash | Hand-rolled public-domain SHA-256 (64 KiB prefix hash) | `src/db/ca_scan.c:22` |
| Versioning | `git describe --tags` → `PROJECT_VERSION` → `configure_file` → `ca_version.h` | `CMakeLists.txt:4-49` |
| Install | `find_package(caudio CONFIG)` providing `caudio::utils`, `caudio::player`, `caudio::db`, `caudio::engine` + `_shared` variants | `CMakeLists.txt:335-367`, `cmake/caudioConfig.cmake.in:1` |
| Tests | CTest 37 tests, `ca_add_test` helper | `CMakeLists.txt:388-416`, `README.md:454` |
| Formatting/Lint | `.clang-format:1` (LLVM, 4-space, 100 col), `.clang-tidy:1` (`clang-analyzer`, `readability`, `performance`, `portability`) |  |
| CI artifacts | `.gitignore:1` ignores `build/`, `out/`, `compile_commands.json` etc. |  |

### 1.3 Repository Layout (tree)

```
caudio/
├── .clang-format / .clang-tidy / .gitignore
├── CMakeLists.txt               # 553 lines, all targets + options
├── README.md                    # 496 lines
├── cmake/caudioConfig.cmake.in  # package config template
├── include/caudio/
│   ├── caudio.h                 # umbrella (re-exports all four libs)
│   ├── ca_version.h.in          # version template → build/include/caudio/ca_version.h
│   ├── utils/
│   │   ├── ca_utils.h           # umbrella
│   │   ├── ca_types.h           # ca_result, ca_state, ca_alloc, ca_init_opts, ca_player_opts
│   │   ├── ca_export.h          # CA_API, CA_NODISCARD
│   │   ├── ca_alloc.h           # debug alloc macros
│   │   ├── ca_arena.h           # bump allocator API
│   │   ├── ca_thread.h          # ca_thread, sleep, priority, name
│   │   ├── ca_ring.h            # SPSC float ring
│   │   ├── ca_cmd.h             # ca_cmd_queue (fixed enum + union)
│   │   ├── ca_result.h          # ca_result_to_string
│   │   ├── ca_error.h           # ca_error struct
│   │   └── ca_log.h             # ca_log_level + callback
│   ├── player/
│   │   ├── ca_player.h          # ca_player lifecycle + transport
│   │   └── ca_reader.h          # file/mem reader, 64-bit tell/seek
│   ├── db/
│   │   ├── ca_db.h              # full DB API surface
│   │   └── ca_db_types.h        # ca_track, ca_queue_item, ca_history_entry etc.
│   └── engine/
│       ├── ca_engine.h          # engine orchestrator API
│       └── ca_engine_types.h    # ca_engine_event, ca_engine_callbacks, ca_engine_state
├── src/
│   ├── utils/
│   │   ├── ca_alloc.c           # 310 lines, CA_DEBUG tracking impl
│   │   ├── ca_arena.c           # 201 lines, 64-byte aligned bump allocator
│   │   ├── ca_thread.c          # 261 lines, Win32/POSIX dual impl
│   │   ├── ca_ring.c            # 171 lines, atomic SPSC float ring
│   │   ├── ca_cmd.c             # 153 lines, MPSC cmd queue (atomic + mutex)
│   │   ├── ca_result.c          # 34 lines
│   │   ├── ca_error.c           # 31 lines
│   │   └── ca_log.c             # 29 lines
│   ├── player/
│   │   ├── ca_reader.c          # 247 lines, FILE* vs mem, 64-bit ftello
│   │   ├── ca_decode.h/.c       # 60/279 lines, vt registry + 64 KiB arena
│   │   ├── ca_output.h/.c       # 37/204 lines, miniaudio device, no-alloc callback
│   │   ├── ca_player.c          # 961 lines, central state machine + decode thread
│   │   ├── ca_player_internal.h # 21 lines, RING_CAP/CMD_CAP constants
│   │   ├── decoder_common.h     # 117 lines, helpers + sine synthesis fallback
│   │   └── decoders/
│   │       ├── dr_wav.c         # 166 lines, probe "RIFF"
│   │       ├── dr_flac.c        # 164 lines, probe "fLaC"
│   │       ├── dr_mp3.c         # 193 lines, probe "ID3"/0xFFE0
│   │       ├── stb_vorbis.c     # 99 lines, synthetic-only stub
│   │       └── ffmpeg.c         # 216 lines, dlopen fallback (stub reports UNSUPPORTED)
│   ├── db/
│   │   ├── ca_db.c              # 2314 lines, all DB operations
│   │   ├── ca_db_internal.h     # 71 lines, struct ca_db + db_lock helpers
│   │   ├── ca_schema.c/.h       # 147/208 lines, WAL/FTS5 + triggers
│   │   ├── ca_write_thread.c/.h # 291/31 lines, MPSC via ca_ring carrying ca_write_op
│   │   ├── ca_scan.c            # 441 lines, recursive scan + SHA-256 fingerprint
│   │   └── ca_search.c          # 240 lines, FTS5 + LIKE fallback
│   └── engine/
│       ├── ca_engine.c          # 1012 lines, monitor thread + queue/history
│       ├── ca_engine_internal.h # 47 lines, struct ca_engine + ca_queue_state
│       ├── ca_queue_logic.h/.c  # 23/335 lines, shuffle perm + next/prev
│       └── ca_history_policy.h/.c # 8/20 lines, 60%/90s rule
├── vendor/
│   ├── miniaudio.h / dr_wav.h / dr_flac.h / dr_mp3.h / stb_vorbis.c / sqlite3.c/.h / sqlite3ext.h
├── tests/                       # 37 .c tests + helpers.h + fixtures/sample.wav (176,444 B)
│   ├── helpers.h               # shared helpers (fingerprint, sample finding, asserts)
│   ├── test_utils_*.c (5), test_*.c (32 including race/flush)
│   └── fixtures/sample.wav
├── examples/
│   ├── common.h                 # inline demos helpers (fallback :memory:, queue seeding)
│   ├── mini.c                   # 88 lines
│   ├── player_db_demo.c         # 143 lines
│   └── engine_demo.c            # 140 lines
├── docs/superpowers/
│   ├── plans/ (core, db, utils-extraction, bugfix)
│   └── specs/ (core-design, db-design, engine-design)
├── build*/ .cache/ .superpowers/ cmake/ Testing/
└── CPP_PORT_REPORT.md           # this file
```

### 1.4 Build System

- **CMake options** at `CMakeLists.txt:62-64`: `CAUDIO_WITH_FFMPEG` OFF, `CAUDIO_BUILD_TESTS` ON, `CAUDIO_BUILD_EXAMPLES` ON.
- **Targets**: `caudio_utils`/`_shlib`, `caudio_player`/`_shlib`, `caudio_db`/`_shlib`, `caudio_engine`/`_shlib`, combined `caudio_shlib` at `CMakeLists.txt:298-333`.
- **Warnings helper** `ca_set_warnings` at `CMakeLists.txt:74`.
- **Vendor SQLite XOR FetchContent** at `CMakeLists.txt:155-186` (`CAUDIO_HAS_VENDOR_SQLITE` flag, SHA256-verified tarball).
- **FFmpeg conditional** at `CMakeLists.txt:292` appends `decoders/ffmpeg.c`.

### 1.5 How to Run / Test

```sh
# Prerequisites: CMake ≥3.20, C17 compiler, pthreads on POSIX
cmake -B build -DCMAKE_BUILD_TYPE=Release        # or Debug
cmake --build build -j
./build/mini tests/fixtures/sample.wav           # Windows: build/Release/mini.exe
ctest --test-dir build -V                        # 37/37 expected
ctest --test-dir build -R test_db -V

# Debug leak tracking
cmake -B build -DCMAKE_C_FLAGS="-DCA_DEBUG" && cmake --build build && ctest --test-dir build -V
# ASan+UBSan
cmake -B build-asan -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g -O1 -DCA_DEBUG"
cmake --build build-asan && ctest --test-dir build-asan -V

# Optional FFmpeg dlopen fallback
cmake -B build -DCAUDIO_WITH_FFMPEG=ON && cmake --build build

# Install
cmake --install build --prefix /usr/local
```

Usage snippets documented in `README.md:151-291` for player/utils/db/engine.

---

## 2. Architecture

### 2.1 High-Level Diagram (text)

```
                    ┌─────────────────────┐
                    │  caudio::engine     │  monitor thread (poll_ms 10), gapless_ms 300,
                    │  orchestrator       │  queue/shuffle/repeat, history 60%/90s,
                    │  src/engine/*       │  callbacks + lock-free event ring (64)
                    └──────────┬──────────┘
                               │ uses (owns or borrows)
                ┌──────────────┴──────────────┐
                │                             │
     ┌──────────▼─────────┐       ┌───────────▼─────────┐
     │  caudio::player    │◄──────│   caudio::db        │  writer thread + MPSC via ca_ring,
     │  reader→decode→    │ uses  │  SQLite 3.46.1+FTS5 │  64 KiB SHA-256 fingerprint scan,
     │  ring→output→      │ fp    │  10 tables + FTS5,  │  WAL, JSON import/export, stats
     │  decode thread     │ path  │  tracks/playlists/  │
     │  src/player/*      │       │  queue/history +    │
     └──────────┬─────────┘       │  engine_state       │
                │                └───────────┬─────────┘
                └───────────────┬────────────┘
                                │ uses
                    ┌───────────▼───────────┐
                    │  caudio::utils        │  ca_alloc/CA_DEBUG, ca_arena (64B align),
                    │  foundation           │  ca_thread (Win32/POSIX), ca_ring (SPSC),
                    │  src/utils/*          │  ca_cmd_queue, ca_result/error/log
                    └───────────────────────┘

  Vendor layer: vendor/miniaudio.h, dr_*.h, stb_vorbis.c, sqlite3.c
  Platform: Windows (HANDLE/CRITICAL_SECTION) vs POSIX (pthread/mutex/sched/nanosleep/dlopen)
```

**Layer rule:** `utils` is standalone; `player` depends on `utils`; `db` depends on `utils`; `engine` depends on `db+player+utils`. Found in `CMakeLists.txt:143,210,257,277`.

### 2.2 Module-by-Module Breakdown

#### 2.2.1 caudio::utils — foundation (`include/caudio/utils/*`, `src/utils/*`)

| Component | Public API (`include/...`) | Impl (`src/utils/...`) | Key symbols (file:line) |
|-----------|----------------------------|------------------------|-------------------------|
| Result/Error | `ca_types.h:15`, `ca_result.h:12`, `ca_error.h:13` | `ca_result.c:3`, `ca_error.c:6` | `ca_result_to_string:ca_result.c:3`, `ca_error_set:ca_error.c:6` (printf-style, 256B buf), 13 `CA_ERR_*` codes |
| Alloc | `ca_types.h:38`, `ca_alloc.h:16` | `ca_alloc.c:256` | `ca__alloc_default:ca_alloc.c:273`, `ca__malloc/ca__free:ca_alloc.c:284`, `CA_DEBUG` record `struct ca_dbg_rec:ca_alloc.c:20`, `ca_dbg_add:ca_alloc.c:35`, `ca__debug_shutdown:ca_alloc.c:125` (qsort by file:line, aggregate report) |
| Arena | `ca_arena.h:13` | `ca_arena.c:24` | `ca_arena_create:ca_arena.c:24` (cap+64 alignment), `ca_arena_alloc:ca_arena.c:124` (power-of-two fast path `ca__align_up:ca_arena.c:116`), `ca_arena_reset/destroy:ca_arena.c:155` |
| Thread | `ca_thread.h:13` | `ca_thread.c:25` (Win32) / `140` (POSIX) | `ca_thread_create:ca_thread.c:25`, `ca_thread_join:ca_thread.c:45`, `ca_thread_set_priority:ca_thread.c:62` (maps ±2 to BELOW/HIGHEST), `ca_thread_set_name:ca_thread.c:88` (SetThreadDescription / pthread_setname_np), `ca_thread_sleep_ms:ca_thread.c:116` |
| Ring (SPSC audio) | `ca_ring.h:14` | `ca_ring.c:10` | `struct ca_ring:ca_ring.c:10` (`_Atomic wr/rd`, `float* buf`), `ca_ring_create:ca_ring.c:20` (cap*channels overflow check), `ca_ring_write:ca_ring.c:65` / `ca_ring_read:ca_ring.c:97` (wrap memcpy, `memory_order_acquire/release`), `ca_ring_available_read/write:ca_ring.c:127` |
| Cmd Queue (MPSC) | `ca_cmd.h:13` | `ca_cmd.c:19` | `struct ca_cmd_queue:ca_cmd.c:19` (atomic wr/rd + CRITICAL_SECTION/pthread_mutex), `ca_cmd_push:ca_cmd.c:79` (CAS + `CA_ERR_BUSY` on full, truncates path[511]), `ca_cmd_pop:ca_cmd.c:110` |
| Log | `ca_log.h:12` | `ca_log.c:6` | `g_log_cb:ca_log.c:6`, `ca_log:ca_log.c:12` (vsnprintf 1024) — global, not thread-safe |

#### 2.2.2 caudio::player — decode → ring → output (`include/caudio/player/*`, `src/player/*`)

| Component | Public API | Impl | Key symbols |
|-----------|-----------|------|-------------|
| Reader | `ca_reader.h:14` | `ca_reader.c:18` | `struct ca_reader:ca_reader.c:18` (FILE vs mem kind), `ca__ftell64/ca__fseek64:ca_reader.c:28` (`_ftelli64` vs `ftello`), `ca_reader_open_file:ca_reader.c:46`, `ca_reader_open_memory:ca_reader.c:75`, `ca_reader_seek:ca_reader.c:143` (clamps 0..size/file_size_inner), `ca_reader_file_size_inner:ca_reader.c:129` (save/restore) |
| Decode registry | `ca_decode.h:18` | `ca_decode.c:17` | `struct ca_decoder:ca_decode.c:17` (vt + arena 64 KiB at `ca_decode.c:42`), `g_vts[16]:ca_decode.c:29`, `ca_register_decoder:ca_decode.c:32`, `ca_decode_open:ca_decode.c:48` (probe 32B from `CA_DECODE_PROBE_BYTES:ca_decode.c:14`, restore orig offset at `ca_decode.c:67`, choose first matching vt), `ca_decode_register_builtins:ca_decode.c:235` (order: dr_wav, dr_flac, dr_mp3, stb_vorbis, ffmpeg) |
| Decoder vt contract | `ca_decode.h:20` | — | `ca_decoder_vt:ca_decode.h:20` (`probe`, `open`, `decode`, `seek`, `close`), per-decoder priv via `ca_decoder_set_priv:ca_decode.c:149`; sample rate/channels/total_frames on decoder object |
| Wav | `ca_decode.h:50` | `decoders/dr_wav.c:15` | `ca_wav_probe:decoders/dr_wav.c:28` ("RIFF"), `ca_wav_open:decoders/dr_wav.c:69` (`drwav_init_ex` with reader callbacks), synthetic fallback 8000 Hz mono 8000 frames |
| Flac | `ca_decode.h:51` | `decoders/dr_flac.c:15` | `ca_flac_probe:decoders/dr_flac.c:27` ("fLaC"), similar `drflac_open` pattern, synthetic 44100 Hz stereo |
| Mp3 | `ca_decode.h:52` | `decoders/dr_mp3.c:15` | `ca_mp3_probe:decoders/dr_mp3.c:28` ("ID3"/0xFFE0), `drmp3_init` then `mp3.sampleRate/channels` at `decoders/dr_mp3.c:113`, total placeholder 48000*300 if unknown |
| Vorbis | `ca_decode.h:53` | `decoders/stb_vorbis.c:14` | `ca_vorbis_probe:stb_vorbis.c:24` ("OggS"), **stub-only** synthetic 22050 Hz mono |
| FFmpeg | `ca_decode.h:54` | `decoders/ffmpeg.c:1` | `ca_ffmpeg_try_load:decoders/ffmpeg.c:33/73` (`LoadLibraryA("avcodec-60.dll")` vs `dlopen("libavcodec.so.60")`), `ca_ffmpeg_probe:decoders/ffmpeg.c:107` (ASF GUID `30 26 B2...`), open returns `CA_ERR_UNSUPPORTED` in stub |
| Output | `ca_output.h:16` | `ca_output.c:14` | `struct ca_output:ca_output.c:14` (ring + `_Atomic float volume` + `ma_device`), `ca_output_data_callback:ca_output.c:27` (no alloc, vol clamp NaN, `ca_ring_read` + `memset` zero-fill), `ca_output_create:ca_output.c:72` (default 48000/2, channel ≤32, auto-start, fallback dummy if `ma_device_start` fails), `ca_output_test_fill:ca_output.c:195` |
| Player core | `ca_player.h:12` | `ca_player.c:40` | `struct ca_player:ca_player.c:40` (arena, cmdq `CA_PLAYER_CMD_CAP 64:ca_player_internal.h:13`, ring `8192:ca_player_internal.h:12`, decode thread, atomics `state/should_exit/open_gate/decode_busy/frames_decoded/pos_base/pos_start_ms/is_playing/volume`), `g_tls_last_error:ca_player.c:38` (`__declspec(thread)`/`_Thread_local`), `ca__now_ms:ca_player.c:91`, `ca_player_thread_fn:ca_player.c:156` (cmd dispatch + decode pump chunk ≤1024, time-based EOF), `ca_player_create:ca_player.c:655`, `ca_player_do_open_path:ca_player.c:351`, `ca_player_do_open_reader:ca_player.c:502`, `ca_player_play/pause/resume/stop/seek/set_volume:ca_player.c:742/787/799/811/860/910` |

**Player data flow (per `src/player/ca_player.c`):**

```
ca_player_open(path)  [main thread, synchronous, gates decode thread]
  → ca_reader_open_file
  → ca_decode_open (probe 32B → choose vt → arena 64KiB → vt->open)
  → ca_ring_create/recreate (8192 frames, channels from decoder)
  → ca_output_create (ma_device F32, auto-start)
  → preroll CA_PLAYER_RING_CAP/2 (≤2048) via ca_decoder_decode → ca_ring_write
  → state READY

ca_player_play  → pos_start_ms = now, is_playing=1, state PLAYING

decode thread (ca_player_thread_fn):
  loop every ~2ms:
    drain cmd queue (CA_CMD_PLAY/PAUSE/RESUME/STOP/SEEK/VOLUME)
    if open_gate → sleep 2ms (synchronization with main thread seek/stop)
    if is_playing && time-based pos < total && ring has space:
         chunk = min(avail, 1024, 2048/ch)
         decode_busy=1 → ca_decoder_decode → decode_busy=0 → ca_ring_write
    if got==0 && ring drain==0 → is_playing=0, state STOPPED

output callback (miniaudio thread, RT, no alloc):
  ca_ring_read(dst) → multiply by atomic volume → zero-fill remainder

seek/stop: pause_decode (open_gate=1, wait ≤500ms for decode_busy==0) → ring reset → vt->seek → pos_base update → preroll 1024 → gate 0
position: time-based (ca_player_position:ca_player.c:933) — base + (now-start)*sr/1000, clamped to total_frames
```

#### 2.2.3 caudio::db — SQLite persistence (`include/caudio/db/*`, `src/db/*`)

| Component | Public API | Impl | Key symbols |
|-----------|-----------|------|-------------|
| Types | `ca_db_types.h:15` | — | `ca_track:ca_db_types.h:27` (26 fields, 1024 path, 32B fingerprint, 256 title/artist/album, 512 cover, play_count, library_id), `ca_playlist:ca_db_types.h:56`, `ca_queue_item:ca_db_types.h:66`, `ca_history_entry:ca_db_types.h:74`, `ca_library:ca_db_types.h:92`, `ca_db_stats:ca_db_types.h:103`, callbacks `ca_track_cb` etc at `ca_db_types.h:145` |
| Handle/Locking | `ca_db.h:12` | `ca_db_internal.h:21` | `struct ca_db:ca_db_internal.h:21` (handle, alloc, writer, batch_size, path, db_lock), `ca__db_lock_*:ca_db_internal.h:35` (CRITICAL_SECTION vs recursive pthread_mutexattr), used pervasively in `ca_db.c` |
| Schema | `ca_db.h:12` | `ca_schema.c:4` | `ca__schema_sql:ca_schema.c:4` (WAL+NORMAL+32768+FK ON, 10 tables + FTS5 + 3 triggers + engine_state default row), `ca__schema_init:ca_schema.c:131` |
| Open/Close | `ca_db.h:12` | `ca_db.c:109` | `ca_db_open:ca_db.c:109` (malloc db, copy path, sqlite3_open, schema init, write thread create/start), `ca_db_close:ca_db.c:171` (join writer, sqlite3_close under lock) |
| Tracks | `ca_db.h:18` | `ca_db.c:216` | `ca_db_track_insert:ca_db.c:216` (bind 24 cols, auto timestamp NULL→CURRENT), `ca_db_track_update:ca_db.c:273` (UPDATE + deleted_at nullable), `ca_db_track_get:ca_db.c:366`, `find_by_fingerprint/path:ca_db.c:396/431`, `ca_db_track_list:ca_db.c:479` (dynamic WHERE with LIKE+ESCAPE, limit/offset, order id), `set_dirty:ca_db.c:586` |
| Playlists | `ca_db.h:31` | `ca_db.c:621` | CRUD at `ca_db.c:621/657/704/745/777`, `add_track:ca_db.c:820` (append via MAX(position), else shift +1), `reorder:ca_db.c:908` (two UPDATE shifts + final SET), `get_tracks:ca_db.c:973` (JOIN tracks→playlist_items) |
| Queue (multi) | `ca_db.h:47` | `ca_db.c:1010` | `ca_db_queue_enqueue:ca_db.c:1010` (queue_id 0→1, same shift/append logic), `dequeue:ca_db.c:1065` (SELECT ORDER BY position LIMIT 1, DELETE, shift -1), `remove/clear/list:ca_db.c:1120/1163/1192` |
| History | `ca_db.h:57` | `ca_db.c:1233` | `ca_db_history_add:ca_db.c:1233`, `list:ca_db.c:1266` (WHERE track_id/queue_id, ORDER BY started_at DESC) |
| Bookmarks/Lyrics/EQ | `ca_db.h:61`, schema | `ca_db.c:1344` | `bookmark_add/list:ca_db.c:1344/1377`; lyrics/eq presets tables exist but have no public API (schema-only) |
| Libraries | `ca_db.h:72` | `ca_db.c:1419` | `library_add/list/update/delete:ca_db.c:1419/1454/1501/1544` |
| Stats | `ca_db.h:81` | `ca_db.c:1578` | `ca_db_get_stats:ca_db.c:1578` (6 COUNT(*) + SUM(duration)*1000) |
| Search | `ca_db.h:66` | `ca_search.c:141` | `ca_db_search:ca_search.c:141` (sanitize FTS5 query at `ca_search.c:64`, MATCH rank→prefix `*` → LIKE fallback via `ca_search.c:172`), `porter unicode61` |
| Scan | `ca_db.h:69` | `ca_scan.c:380` | `ca_db_scan_library:ca_scan.c:380` (resolve library.path, recurse via FindFirstFileA/opendir, per-file stat → skip if path+size+mtime hit, else SHA-256 64KiB at `ca_scan.c:154`, upsert with fingerprint identity + preserve play_count at `ca_scan.c:247/272/284`), `has_audio_ext:ca_scan.c:175` (mp3/flac/ogg/wav/m4a), `ca_sha256_*:ca_scan.c:47/86/111` |
| JSON | `ca_db.h:78` | `ca_db.c:1647/1921` | `ca_db_export_json:ca_db.c:1647` (writes `{"tracks":[...]}` with `json_escape:ca_db.c:1623`), `ca_db_import_json:ca_db.c:1921` (naive hand-rolled parser, FNV fingerprint fallback at `ca_db.c:1899`, BEGIN/COMMIT with corruption detection) |
| Writer thread | `ca_write_thread.h:10` | `ca_write_thread.c:13` | `struct ca_write_thread:ca_write_thread.c:13` (ca_thread + atomics shutdown/running/in_flight + ca_ring*queue + float hijack), `ca_write_op:ca_write_thread.h:12` (sql[1024] + stmt + callback), `writer_loop:ca_write_thread.c:27` (batch up to batch_size, ring→floats→memcpy op → sqlite3_step/exec under db_lock), `ca__write_thread_create:ca_write_thread.c:92` (ring cap = 256 ops × floats_per_op), `ca__write_thread_flush:ca_write_thread.c:167` (poll 200×1ms for avail < fpp && in_flight==0 else CA_ERR_BUSY), `ca_db_write_async:ca_write_thread.c:241` |

**DB tables (per `src/db/ca_schema.c:4-129`):** libraries, tracks (fingerprint BLOB(32) UNIQUE, indexes on fingerprint/path/artist+album/library_id), playlists, playlist_items (PK playlist_id+track_id), queue (queue_id+position), history, bookmarks, lyrics, eq_presets, engine_state (single row id=1), plus `tracks_fts` FTS5 virtual table + triggers ai/ad/au syncing title/artist/album/album_artist/genre.

#### 2.2.4 caudio::engine — orchestration (`include/caudio/engine/*`, `src/engine/*`)

| Component | Public API | Impl | Key symbols |
|-----------|-----------|------|-------------|
| Types | `ca_engine_types.h:12` | — | `ca_repeat_mode:ca_engine_types.h:13`, `ca_engine_event:ca_engine_types.h:22` (type/track_id/queue_id/pos/dur/msg), `ca_engine_callbacks:ca_engine_types.h:30` (4 fns + user), `ca_engine_opts:ca_engine_types.h:37` (alloc, cbs, enable_monitor_thread, poll_ms, gapless_ms, history pct/secs), `ca_engine_state:ca_engine_types.h:46` persisted |
| Internal | `ca_engine_internal.h:9` | — | `struct ca_queue_state:ca_engine_internal.h:9` (shuffle, repeat, perm, n, cursor, queue_id, alloc), `struct ca_engine:ca_engine_internal.h:19` (db/player, owns_*, mon thread, ev_buf 64, atomics, st, q, current track, gapless/queue locks) |
| Engine lifecycle | `ca_engine.h:11` | `ca_engine.c:424` | `engine_alloc:ca_engine.c:424` (default poll 10, gapless 300, pct 60, secs 90), `ca_engine_create:ca_engine.c:510` (no db/player), `ca_engine_attach:ca_engine.c:542` (borrowed), `ca_engine_open:ca_engine.c:588` (owns db via ca_db_open + ca_player_create + attach), `ca_engine_destroy:ca_engine.c:633` (join mon, save state) |
| State persistence | `ca_engine.h:17` | `ca_engine.c:37/128` | `engine_state_load:ca_engine.c:37` (shuffle_perm blob → perm array, cursor bounds clamp), `engine_state_save:ca_engine.c:128` (BEGIN IMMEDIATE → UPDATE 6 cols → COMMIT via sqlite3_exec) |
| Events | `ca_engine.h:33` | `ca_engine.c:190` | `push_event:ca_engine.c:190` (MPSC spin on ev_lock, ring cap 64 drop if full, then dispatch callbacks outside lock: TRACK_STARTED/ENDED/QUEUE_CHANGED/ERROR), `ca_engine_poll_event:ca_engine.c:943` (SPSC read via ev_r/w atomics), `ca_engine_drain_events:ca_engine.c:961` |
| Monitor | — | `ca_engine.c:319/371` | `engine_tick:ca_engine.c:319` (do_history_mark + progress every 500ms → push PROGRESS + gapless look-ahead remaining ≤ gapless_ms/1000 → CAS gapless_armed → ca_engine_next), `monitor_loop:ca_engine.c:371` (sleep poll_ms, atomic mon_run) |
| History | `ca_history_policy.h:5` | `ca_history_policy.c:3` | `ca__should_mark_played_ex:ca_history_policy.c:3` (`pos/dur≥pct` OR `pos≥secs`), `do_history_mark:ca_engine.c:235` (CAS marked_played 0→1 exactly-once, BEGIN → track_get → ++play_count → history_add → COMMIT, reset flag on rollback) |
| Queue logic | `ca_queue_logic.h:14` | `ca_queue_logic.c:11` | `ca__qs_count:ca_queue_logic.c:11` (COUNT queue), `ca__qs_set_shuffle:ca_queue_logic.c:186` (if want=1: COUNT → alloc perm 0..n-1 → Fisher-Yates via `sqlite3_randomness:ca_queue_logic.c:178` → persist_shuffle_blob), `ca__qs_set_repeat:ca_queue_logic.c:236`, `ca__qs_next:ca_queue_logic.c:247` (shuffle: perm[cursor++] else queue dequeue+enqueue on REPEAT_QUEUE), `ca__qs_prev:ca_queue_logic.c:310` (shuffle only, cursor-=2 then perm[cursor++]), `persist_shuffle_blob/persist_cursor:ca_queue_logic.c:69/126` |
| Transport | `ca_engine.h:17` | `ca_engine.c:666` | `ca_engine_play:ca_engine.c:666` (queue_lock CAS → qs_next → engine_do_play_track), `ca_engine_next/prev:ca_engine.c:761/809` (REPEAT_ONE special: seek 0 or re-open path), `ca_engine_set_shuffle:ca_engine.c:833` / `set_repeat:ca_engine.c:865` (both queue_lock, push QUEUE_CHANGED), `pause/resume/stop/seek/volume:ca_engine.c:694/704/714/724/734` (stop/pause delegate to ca_player, volume clamps 0..1 and saves), `ca_engine_get_state:ca_engine.c:896` |
| Sync stubs | `ca_engine.h:38` | `ca_engine.c:998` | `ca_engine_on_track_synced/on_library_synced:ca_engine.c:998` — no-ops (hooks-only sync per README) |

**Engine control flow:**

```
ca_engine_open(path) → ca_db_open → ca_init → ca_player_create → ca_engine_attach → engine_state_load → start monitor thread

ca_engine_play(queue_id):
  lock(queue_lock) → ca__qs_next → ca_player_open(path) → ca_player_play → has_current=1, current_dur, started_ms, save state → push TRACK_STARTED

monitor thread tick (poll_ms):
  do_history_mark (if 60%/90s → CAS → transaction)
  if PLAYING && every 500ms → push PROGRESS
  if PLAYING && remaining ≤ gapless_ms → CAS gapless_armed → ca_engine_next (non-blocking; queue_lock if held → BUSY and reset armed)

ca_engine_next:
  if !shuffle && repeat==ONE && has_current → seek 0 else open+play next perm/dequeue
```

---

## 3. Detailed Functional Specification

### 3.1 Features

| Feature | Status | Location |
|---------|--------|----------|
| Audio decode Wav/Flac/Mp3 | Real via dr_* (probe + fuzzer-tolerant synthetic fallback on drwav_init failures) | `decoders/dr_wav.c`, `dr_flac.c`, `dr_mp3.c` |
| Vorbis (Ogg) | **Stub synthetic only** — probe OggS, always 22050 Hz sine | `decoders/stb_vorbis.c` |
| FFmpeg fallback | **Stub** — dlopen detection, ASF-only probe, always UNSUPPORTED | `decoders/ffmpeg.c` (enabled only with `CAUDIO_WITH_FFMPEG`) |
| 64-bit file I/O | ftello/_ftelli64 avoid 32-bit long truncation | `src/player/ca_reader.c:28` |
| Lock-free audio path | Data callback does no alloc, only ring read + volume * | `src/player/ca_output.c:27` |
| Gapless pre-roll | Half-ring preroll on open, 1024 on seek/play | `src/player/ca_player.c:462` |
| Time-based position | Monotonic clock, not decode count; gapless remaining calculation | `src/player/ca_player.c:91/933` |
| Multi-queue | queue_id 0→1 default, isolated per id | `src/db/ca_db.c:1016` |
| Shuffle / Repeat | Fisher-Yates via sqlite3_randomness, persisted blob; REPEAT_ONE/QUEUE/OFF | `src/engine/ca_queue_logic.c:174`, `ca_engine.c:761` |
| History policy | Configurable pct/secs thresholds, CAS once-per-track, transaction | `src/engine/ca_history_policy.c:3`, `ca_engine.c:235` |
| Persistence | engine_state single row (shuffle, repeat, cursor, track, volume, perm) | `src/db/ca_schema.c:125` |
| Search | FTS5 porter + LIKE fallback, operator stripping | `src/db/ca_search.c:141` |
| Scan | Recursive dir walk, 64 KiB SHA-256 identity, play_count preservation | `src/db/ca_scan.c:380` |
| JSON import/export | Custom escape/unescape + naive parser; corruption → ROLLBACK | `src/db/ca_db.c:1647` |
| Debug diagnostics | CA_DEBUG leak aggregate by site, ASan/UBSan, strict warnings | `src/utils/ca_alloc.c:111`, `CMakeLists.txt:71` |
| Events | MPSC ring 64 + 4 callbacks (started/ended/queue_changed/error) + poll/drain | `src/engine/ca_engine.c:190` |
| Writer thread | Batch size configurable, flush 200ms timeout → BUSY | `src/db/ca_write_thread.c:167` |

### 3.2 CLI / API Surface

#### CLI Examples (built if `CAUDIO_BUILD_EXAMPLES=ON`)

| Binary | Source | Behavior |
|--------|--------|----------|
| `mini` | `examples/mini.c:37` | `mini [path]` — ca_init → ca_player_create → open → play → poll state/pos 100ms until STOPPED. Default `tests/fixtures/sample.wav` |
| `player_db_demo` | `examples/player_db_demo.c:76` | Opens `library.db` (fallback `:memory:` via `common.h:34`), enqueues 2 demo synthetic tracks if empty (`common.h:50`), iterates queue → track_get → player open/play with 10ms*500 timeout |
| `engine_demo` | `examples/engine_demo.c:28` | Opens `build/engine_demo.db` (fallback), seeds queue, ca_engine_attach with poll_ms 10, gapless 300, callbacks started/queue_changed, play queue 1, 20×100ms event poll + mid drain |

#### Public C API (all `CA_NODISCARD CA_API` / `CA_API`, `extern "C"`)

**caudio::utils** — `include/caudio/utils/ca_utils.h:6` umbrella:

- `ca_result_to_string` (`ca_result.h:12`) — maps 13 codes
- `ca_error_set/clear/is_set` (`ca_error.h:18`)
- `ca_log_set_callback`, `ca_log` (`ca_log.h:19`)
- `ca__malloc/calloc/realloc/free` + `CA_DEBUG` `dbg` variants (`ca_alloc.h:16`)
- `ca_arena_create/alloc/reset/destroy` (`ca_arena.h:15`)
- `ca_thread_create/join/set_priority/set_name/sleep_ms` (`ca_thread.h:17`)
- `ca_ring_create/write/read/available/reset/destroy` (`ca_ring.h:16`)
- `ca_cmd_queue_create/push/pop/destroy`, enum `CA_CMD_*` (`ca_cmd.h:13`)

**caudio::player** — `include/caudio/player/ca_player.h:12`:

```c
ca_result ca_init(const ca_init_opts*);
void ca_shutdown(void);
ca_player *ca_player_create(const ca_player_opts*);
void ca_player_destroy(ca_player*);
ca_result ca_player_open(ca_player*, const char* path);
ca_result ca_player_open_reader(ca_player*, ca_reader*);
ca_result ca_player_play/pause/resume/stop(ca_player*);
ca_result ca_player_seek(ca_player*, double seconds);
ca_result ca_player_set_volume(ca_player*, float gain); // clamps 0..1, NaN→0
ca_state  ca_player_state(const ca_player*);
double    ca_player_position(const ca_player*); // seconds, time-based
const char *ca_player_last_error(const ca_player*); // TLS + per-player 256B
```

Reader (`ca_reader.h:14`): `ca_reader_open_file/open_memory`, `read`, `seek/tell/size`, `close` (all 64-bit).

**caudio::db** — `include/caudio/db/ca_db.h:12`:

```c
ca_result ca_db_open(const char* path, const ca_db_opts* opts, ca_db** out);
void ca_db_close(ca_db*);
ca_result ca_db_flush(ca_db*); // 200ms drain → BUSY on timeout
ca_result ca_db_set_write_batch_size(ca_db*, size_t n);

tracks: insert/update/delete/get/find_by_fingerprint/find_by_path/list/set_dirty
playlists: create/get/update/delete/list + add_track/remove_track/reorder/get_tracks
queue: enqueue/dequeue/remove/clear/list  (all queue_id, pos=-1 append, 0→1)
history: add/list (filter has_track_id/has_queue_id, limit/offset, DESC started_at)
bookmarks: add/list (by track_id)
search: ca_db_search(query, limit, cb, user)
scan: ca_db_scan_library(library_id, progress cb, user)
libraries: add/list/update/delete
json: export_json/import_json (path)
stats: get_stats(out: num_tracks etc. + total_duration_ms)
```

Callbacks: `ca_track_cb`, `ca_playlist_cb`, `ca_queue_cb`, `ca_history_cb`, `ca_bookmark_cb`, `ca_library_cb`, `ca_scan_cb` per `ca_db_types.h:145`.

**caudio::engine** — `include/caudio/engine/ca_engine.h:7`:

```c
ca_result ca_engine_create(const ca_engine_opts*, ca_engine**);
ca_result ca_engine_open(const char* db_path, const ca_engine_opts*, ca_engine**); // owns db+player
ca_result ca_engine_attach(ca_db*, ca_player*, const ca_engine_opts*, ca_engine**); // borrows
void ca_engine_destroy(ca_engine*);

ca_result ca_engine_play(ca_engine*, int64_t queue_id);
ca_result ca_engine_pause/resume/stop/seek/set_volume(ca_engine*, ...);
ca_result ca_engine_next/prev(ca_engine*);
ca_result ca_engine_set_shuffle(ca_engine*, int on);
ca_result ca_engine_set_repeat(ca_engine*, ca_repeat_mode);
ca_state ca_engine_get_state(const ca_engine*); // macro alias ca_engine_state
double ca_engine_position(const ca_engine*);
int64_t ca_engine_current_track_id(const ca_engine*);
ca_result ca_engine_get_stats(ca_engine*, ca_db_stats*);
const char* ca_engine_last_error(const ca_engine*);
ca_result ca_engine_poll_event(ca_engine*, ca_engine_event* out); // CA_ERR_NOT_FOUND if empty
ca_result ca_engine_drain_events(ca_engine*, ca_engine_event* buf, size_t cap, size_t* n);
ca_result ca_engine_set_callbacks(ca_engine*, const ca_engine_callbacks*);
ca_result ca_engine_on_track_synced(ca_engine*, int64_t tid, const char* rev); // no-ops
ca_result ca_engine_on_library_synced(ca_engine*, int64_t lid);
```

Events (`ca_engine_types.h:14`): NONE, TRACK_STARTED, TRACK_ENDED, QUEUE_CHANGED, PROGRESS, ERROR (progress pushed every 500ms in monitor).

### 3.3 Config Options

- **CMake**: `CAUDIO_WITH_FFMPEG` (`OFF`, adds `ffmpeg.c`), `CAUDIO_BUILD_TESTS`, `CAUDIO_BUILD_EXAMPLES` (`CMakeLists.txt:62`).
- **CFLAGS**: `-DCA_DEBUG` (leak tracking), `-fsanitize=address,undefined` (ASan+UBSan).
- **ca_init_opts** (`ca_types.h:52`): `ca_alloc alloc`, `ca_log_cb log_cb`, `bool debug_leak_track` (unused; leak tracked via `CA_DEBUG` macro).
- **ca_player_opts** (`ca_types.h:58`): `sample_rate`, `channels`, `on_state`/`on_state_user`, `on_error`/`on_error_user`.
- **ca_db_opts** (`ca_db_types.h:113`): `ca_alloc alloc`, `enable_wal` (ignored, always WAL), `cache_size` (ignored, always -32768), `write_batch_size` (forwarded to writer), `extensions` (unused; scan hardcodes mp3/flac/ogg/wav/m4a).
- **ca_engine_opts** (`ca_engine_types.h:37`): `ca_alloc`, `cbs`, `enable_monitor_thread` (1), `poll_ms` (10), `gapless_ms` (300), `history_threshold_pct` (60), `history_threshold_secs` (90). Zero → defaults applied at `ca_engine.c:474`.
- **Runtime**: `CAUDIO_SAMPLE` / `CAUDIO_FIXTURE` env for example fixtures (`examples/common.h:109`, `tests/helpers.h`).

### 3.4 File Formats & Protocols

| Format / Storage | Parser / Writer | Details |
|------------------|-----------------|---------|
| WAV/FLAC/MP3/Ogg | dr_* via ca_reader callbacks (`on_read/seek/tell` at `decoders/dr_wav.c:36`) | Probe first 4 bytes: `RIFF/fLaC/ID3|0xFFE0/OggS`; synthetic sine fallback on init failure → tests never fail on corrupt fixture |
| FFmpeg (ASF/WMA) | ffmpeg.c dlopen | Only ASF GUID detected; always reports unsupported (stub) |
| SQLite DB | sqlite3 3.46.1, PRAGMA WAL, page 4KiB | 10 tables + FTS5, triggers keep FTS in sync, FK cascades on playlist_items/queue/history/bookmarks; engine_state single-row; default library id=1 inserted at `ca_schema.c:144` |
| FTS5 index | porter unicode61 tokenizer, 5 columns | Sanitized query (strip operators, quotes doubled) → `MATCH ? ORDER BY rank LIMIT ?` with prefix `*` and LIKE fallback |
| JSON export | `ca_db_export_json:ca_db.c:1647` → `{"tracks":[{id,size,...path/title/artist/album/cover..}]}` | Hand-escaped via `json_escape:ca_db.c:1623` (handles \n\r\t\u00XX) |
| JSON import | `ca_db_import_json:ca_db.c:1921` | Hand-rolled: find `"tracks"`, locate `[`…`]`, scan per object by searching `"id"`, parse fields via `find_json_field:ca_db.c:1793` + `extract_json_*`, naive `{`/`}` scanning with `\"` awareness, corruption → ROLLBACK + `CA_ERR_CORRUPT` |
| Fingerprint | `compute_fingerprint_file:ca_scan.c:154` SHA-256 of first 64 KiB | 32B `tracks.fingerprint` UNIQUE identity; import generates FNV+mix at `ca_db.c:1899` if missing |
| Cover art / paths | Fixed buffers in structs | path 1024, title 256, cover 512, genre 64 — truncation via `strncpy(..., sizeof-1)` + null; format fields bound unconditionally with SQLITE_TRANSIENT |
| Command queue wire | `ca_cmd:ca_cmd.h:24` (type + union 512 path / reader* / seconds / gain) | Passed via `ca_cmd_queue` MPSC; path truncated to 511+'\0' at `ca_cmd.c:97`; callback state changes mirror seek semantics |

---

## 4. Algorithm & Logic Deep-Dive

### 4.1 Core Data Structures

- **SPSC float ring** (`src/utils/ca_ring.c:10`): `cap` frames × `channels` floats, `_Atomic wr/rd` counters (unwrapped monotonic, modulus for index). Invariants: `used = wr-rd`, clamp to cap on wrap (>cap → cap); write drops excess, read clamps to avail. Memory order `acquire` load indices, `release` store after memcpy. No ABA. Single producer (decode thread) + single consumer (audio callback / output fill) — but flush uses main thread `ring_reset` which briefly pauses output to avoid torn memcpy (see `ca__ring_reset_safe:ca_player.c:140`).
- **MPSC cmd queue** (`src/utils/ca_cmd.c:19`): atomic wr/rd + platform mutex serializing push/pop (so behaves like mutex queue despite atomics). Cap fixed at `CA_PLAYER_CMD_CAP 64`. Full → `CA_ERR_BUSY` (caller must retry); empty pop → `CA_ERR_STATE`.
- **Arena** (`src/utils/ca_arena.c:10`): contiguous cap+64 raw, 64B aligned base, `off` bump. `ca__align_up:ca_arena.c:116` fast path `(align & (align-1))==0` → mask else divide. Only reset/destroy, no free-per-alloc. Used for decode working scratch (64 KiB at `ca_decode.c:15`).
- **ca_reader** (`src/player/ca_reader.c:18`): dual kind FILE vs mem slice (pos/len). FILE path uses `ftello/fseeko` posix or `_ftelli64/_fseeki64` win32 (64-bit). Seek clamps to 0..size/file_size_inner; size computed via seek 0→END under save/restore (`ca_reader_file_size_inner:ca_reader.c:129`). Mem reads are `memcpy` bounded by avail.
- **DB queue state** (`src/engine/ca_engine_internal.h:9`): `perm` int64 array (permutation of queue positions 0..n-1), `n`, `cursor`, `shuffle` flag, `queue_id`, `alloc*`. Backed by sqlite `engine_state.shuffle_perm` blob (little-endian host) + `cursor_pos/shuffle_enabled/repeat_mode`.
- **Engine event ring**: 64× `ca_engine_event` heap array, atomic `ev_w/ev_r` (MPSC producers: monitor + main), `ev_lock` CAS spin (drop if ≥64 used). Callback dispatch after unlock (`ca_engine.c:190`).
- **Writer thread op channel** (`src/db/ca_write_thread.h:12`): `ca_write_op` with sql[1024] + stmt* + callback; hijacks `ca_ring` float storage by `floats_per_op = (sizeof(op)+3)/4` (`ca_write_thread.c:113`), transferred via `memcpy` over float buffer. Abuses type punning via memcpy, not union alias.

### 4.2 Core Algorithms (step-by-step for faithful reimpl)

#### A. Decode dispatch & decoder lifecycle

1. **Probe** (`ca_decode.c:48`): snapshot `orig=ca_reader_tell`, seek 0, read 32B (`CA_DECODE_PROBE_BYTES`), restore orig (fallback to 0 on seek fail). Iterate `g_vts[0..g_count)` probe order (registration order: `ca_decode_register_builtins:ca_decode.c:235`). First true → chosen.
2. **Open** (`ca_decode.c:92`): `ca_decoder*dec = ca__malloc(sizeof) + zero`, store `vt`, `reader`, `alloc`, create `arena` 64KiB (`CA_DECODE_ARENA_CAP`). Call `vt->open(dec, reader, alloc, arena)` which allocates per-format ctx and sets `dec->sample_rate/channels/total_frames` via setters. On open failure: destroy arena, free dec, return code.
3. **Per-format open** (e.g., `ca_wav_open:decoders/dr_wav.c:69`): allocate ctx via `ca__malloc`, copy alloc, store reader, call `drwav_init_ex` with `ca_reader_read/seek/tell` trampolines. If init fails → enter `synthetic=1` with hardcoded rate/ch/total (e.g., wav 8000/1/8000) and return OK anyway (tests expect success on probe-match even if file not real WAV). **Stb_vorbis is always synthetic** (`stb_vorbis.c:32` unconditionally creates 22050/1 ctx). FFmpeg: try_load dlopen, check ASF GUID, then always UNSUPPORTED (`ffmpeg.c:126`).
4. **Decode** (`ca_decoder_decode:ca_decode.c:129`): dispatch `vt->decode(dec, out, frames)`. Real: `drwav_read_pcm_frames_f32` etc.; synthetic: `ca_decoder_fill_sine* :decoder_common.h:39` (`sin(2π440t)*0.5 (+ chan_off)`, pos pointer, clamp to `total - pos`).
5. **Seek** (`ca_decoder_seek:ca_decode.c:139`): dispatch `vt->seek(dec, seconds)` which converts `seconds*sampleRate → PCM frame target` clamp to total and calls `dr*_seek_to_pcm_frame` or sets `synth_pos`.
6. **Close** (`ca_decoder_close:ca_decode.c:247`): call `vt->close` (dr* uninit) → arena destroy → free dec via original copy alloc (handles stack `allocCopy` correctly).

#### B. Player thread synchronization & position model

- **Atomics** (10 atomics on `ca_player:ca_player.c:40`): `state`, `should_exit`, `thread_alive`, `open_gate` (1=main is mutating ring/decoder), `decode_busy` (1=inside decode/ring_write), `frames_decoded` (legacy), `pos_base` (frames at last pause/seek), `pos_start_ms`, `is_playing`, `last_error_set`, plus `volume`(float).
- **Position** is **time-based**, not frame-count: `ca__now_ms` via `GetTickCount64` or `CLOCK_MONOTONIC` (`ca_player.c:91`). On play `pos_start_ms=now` (`ca_player.c:781`); `elapsed = (now-start)*sr/1000` (`ca_player.c:101`), `cur = base+elapsed` clamp to `total_frames`. On pause `ca__update_base_from_elapsed:ca_player.c:116` (`base=cur`). Seek sets `base=target` (`ca_player.c:886`). Reported via `ca_player_position:ca_player.c:933`.
- **Gate protocol** (`ca_player.c:122`): main sets `open_gate=1` then spins ≤500×1ms waiting for `decode_busy==0`; decode thread polls `open_gate` at loop head and sleeps if set. Reset helpers `ca__ring_reset_safe:ca_player.c:140` (pause output device around `ca_ring_reset`). Prone to deadlock if gate held >500ms (see weaknesses).
- **Decode pump** (`ca_player_thread_fn:ca_player.c:156`): handle queued cmds (PLAY/PAUSE/RESUME/STOP/SEEK/VOLUME — note OPEN cases are no-ops as sync path already did work), then gap: if `open_gate` skip, elif `is_playing` && time pos < total && ring `avail>0`, chunk `min(avail,1024, 2048/ch) → tmp[2048]` → set `decode_busy=1` → decode → clear → `ca_ring_write`. If decode `got==0` and `ring_read==0` → auto `is_playing=0`, `state STOPPED`.
- **EOF model**: per-frame `total_frames` determines time EOF (`cur>=total && ring drain==0 → STOPPED` at `ca_player.c:263`).

#### C. DB: track identity & scan

- **Identity**: `fingerprint SHA-256(first 64KiB)`. Compute via `ca_sha256_init/update/final:ca_scan.c:86` (in-file portable impl). **Limitation** documented at `ca_scan.c:148`: two files with identical first 64KiB collide.
- **Scan upsert** (`scan_file:ca_scan.c:201`): acquire lock → `SELECT id FROM tracks WHERE path=? AND size=? AND mtime=?` — if hit → count scanned+progress and return (skip hash). Else compute fp; lock → `SELECT id WHERE fingerprint=?` → if found: `UPDATE path,size,mtime,last_scanned,deleted_at=NULL,library_id=? WHERE id=?` + dedup `DELETE FROM tracks WHERE path=? AND id!=? AND fingerprint=?` (`ca_scan.c:263`). Else if `SELECT id WHERE path=?` found (same path, content changed): `UPDATE` with fingerprint+size/mtime+clear title/artist/album/album_artist/genre/year/track_num/disc_num/cover/duration/samplerate/ch/bitrate+dirty=0 (`ca_scan.c:288`). Else INSERT `(fingerprint,path,size,mtime,library_id, CURRENT_TIMESTAMP)` (`ca_scan.c:305`). Unlock and bump scanned/progress.
- **Preservation**: library scan preserves `play_count/rating/date_added` on fingerprint move; clears stale metadata on content change.

#### D. DB: queue / shuffle logic

- **Count query** (`ca__qs_count:ca_queue_logic.c:11`) single COUNT with SHIFT. Enqueue/dequeue multi-queue via `queue_id` param normalized 0→1.
- **Shuffle** (`ca__qs_set_shuffle:ca_queue_logic.c:186`): 
  - Want=1, count=0 → set flag, empty perm, persist and return.
  - Want=1, count>0 → free old, `malloc cnt*8` → fill 0..cnt-1 → `shuffle_perm` Fisher-Yates (`sqlite3_randomness(&r,4):ca_queue_logic.c:178`, r% (i+1) swap) → set `perm,n, cursor=0, shuffle=1` → `persist_shuffle_blob:ca_queue_logic.c:69` (BEGIN IMMEDIATE → UPDATE engine_state.shuffle_perm=blob(cursor,shuffle_enabled) → COMMIT).
  - Want=0 → free, n=0, cursor 0, shuffle 0 → persist null.
- **Next** (`ca__qs_next:ca_queue_logic.c:247`): if shuffle: if n==0 rebuild via `set_shuffle`; if cursor≥n and `REPEAT_QUEUE` → cursor=0; if `REPEAT_ONE` → stay at perm[cursor-1] (or n-1). Else pos=perm[cursor++], persist cursor, `fetch_track_by_pos:ca_queue_logic.c:149` (`SELECT track_id FROM queue WHERE queue_id=? ORDER BY position LIMIT 1 OFFSET pos` → `ca_db_track_get`). If non-shuffle: if `REPEAT_QUEUE` → dequeue then enqueue back → track; else dequeue → track (consumes queue rows).
- **Prev** (`ca__qs_prev:ca_queue_logic.c:310`): shuffle only. If cursor≤1 → reset to 0 or NOT_FOUND; else cursor-=2 then perm[cursor++] (so next/prev are inverses). Return via `fetch_track_by_pos`.
- **Edge**: non-shuffle `REPEAT_ONE` is handled not here but in `ca_engine_next:ca_engine.c:768` (seek 0 / reopen path) — bypasses queue consumption.

#### E. Engine: tick, history, gapless, state

- **History rule** (`ca__should_mark_played_ex:ca_history_policy.c:3`): `marked==1 →0`, else `(duration>0 && pos/duration >= pct_thr/100)` OR `pos >= secs_thr`. Defaults pct 60 secs 90 at `ca_engine.c:474`.
- **do_history_mark** (`ca_engine.c:235`): early-outs (no has_current or already marked), checks policy, CAS `marked_played 0→1` exactly-once (retry-safe). Then transaction `BEGIN IMMEDIATE` → `ca_db_track_get` → `++play_count; last_played=now_sec` → `ca_db_track_update` → `ca_db_history_add` (completion_pct = pos/dur*100 clamped 0..100, position_ms=pos*1000, queue_id) → COMMIT else ROLLBACK and reset CAS flag for retry.
- **Gapless** (`engine_tick:ca_engine.c:319`): checks only when `PLAYING && current_dur>0` and `remaining = dur - pos` within `[0, gapless_ms/1000]`. CAS `gapless_armed 0→1` (single shot per track) then calls `ca_engine_next` directly (without holding `queue_lock` — previous deadlock comment at `ca_engine.c:355`). On BUSY failure, reset armed to 0.
- **Progress event** (`engine_tick:ca_engine.c:319`): every ≥500ms while PLAYING, push `PROGRESS` event with track_id/queue_id/pos/dur.
- **Engine state persistence** (`ca_engine.c:37/128`): load on attach (also last_scanned volume/shuffle etc. under db_lock), save on `play/next/prev/shuffle/repeat/volume` and at destroy (cursor saved). Shard blob size guard `n*sizeof(int64) ≤ INT_MAX`.

### 4.3 Invariants & Edge Cases

| Invariant / Edge | Guarantee | Location |
|------------------|-----------|----------|
| Allocation fallbacks | All CA_* alloc paths handle NULL alloc → default `malloc/calloc/realloc/free`; overflow checks `cap > SIZE_MAX/sizeof` before mul | `ca_arena.c:35`, `ca_ring.c:29`, `ca_cmd.c:41`, `ca_db.c:122` |
| Null guards | Public C APIs start with `if(!db\|!out\|!path\|\|id==0) return INVALID_ARG` consistently | e.g., `ca_db.c:110,216,334` |
| Path truncation | `path[512]` union truncates to 511+'\0' on push; DB path column 1024 via strncpy sizeof-1 | `ca_cmd.c:97`, `ca_db_types.h:30` |
| Channels clamp | Player: 0→2, >32 → INVALID_ARG; Ring cap accounted per channel | `ca_output.c:76`, `ca_ring.c:29` |
| Volume clamp | NaN/Inf/neg/>1 all clamped to 0..1 in output callback and set_volume | `ca_output.c:48,144`, `ca_player.c:910` |
| SQLite busy semantics | `sqlite_to_ca_ex:ca_db.c:12` maps BUSY→CA_ERR_BUSY, CONSTRAINT→ALREADY_EXISTS/CORRUPT, generic → INTERNAL | `ca_db.c:12` |
| WAL / batch flush | Flush polls 200ms for drain, else BUSY (caller must retry) | `ca_write_thread.c:167` |
| Synthetic decode | Any dr* init failure → sine at fixed rate/chan/total rather than error (test-friendly but masks real errors) | `decoders/dr_wav.c:82` |
| Thread safety of reportedsync | position/state are release/relaxed atomic loads; `pos_base` update is under gate but reads are lock-free (may see torn during gate transition — mitigated by gate wait) | `ca_player.c:101,933` |
| Queue position holes | Enqueue with pos>=0 shifts `position+1` before insert; dequeue shifts `position-1` after delete — maintains dense 0..n-1 if all goes through API, but concurrent direct SQL or mid-transaction abort could leave holes (not validated) | `ca_db.c:1033,1108` |
| Scan fingerprint collision | Duplicate first-64KiB files share fingerprint; dedup DELETE constrained to `AND fingerprint=?` mitigates, but still one row lost on collision | `ca_scan.c:148/263` |
| Engine shuffle cursor OOB | Load clamps `cursor < n ? cursor : 0` at `ca_engine.c:88`; save validates `n*sizeof ≤ INT_MAX` | |

---

## 5. Dependencies → C++ STL / Modern-C++ Equivalents

| C / C lib Dependency | Version / Use | C++ STL Equivalent (C++17/20/23) | Notes for Port |
|----------------------|---------------|-----------------------------------|----------------|
| `libcaudio_utils` own alloc | `ca_alloc` vtable + CA_DEBUG | `std::pmr::memory_resource` + `std::pmr::polymorphic_allocator`, custom `tracking_resource` for leak sites; `std::pmr::monotonic_buffer_resource` for arena | Preserve pluggable alloc via `std::pmr::memory_resource*` threaded through all types; map `CA_DEBUG` to resource wrapper counting `bytes` + `unordered_map<void*, site>` |
| `ca_arena` bump allocator | 64-byte aligned, 64 KiB cap | `std::pmr::monotonic_buffer_resource` over `std::vector<std::byte>` or `std::aligned_storage`; or `boost`-free custom `Arena { std::byte* buf; size_t cap, off; }` with `std::align` | Provide `reset()`; no per-free. Replacement in decoders can be `std::pmr::vector<float>` backed by arena resource |
| `ca_thread` | pthreads / Win32 | `std::jthread` (C++20) + `std::stop_token`, `std::thread` + `std::condition_variable`, `std::this_thread::sleep_for` | Replace `ca_thread_sleep_ms` with `std::this_thread::sleep_for(std::chrono::milliseconds)`; priority/name maps to `std::thread::native_handle()` + platform `pthread_setname_np`/`SetThreadDescription` fallback |
| `ca_ring` SPSC ring | `_Atomic wr/rd` + float buffer | `boost::lockfree::spsc_queue<float>` or hand-rolled `std::atomic<size_t>` + `std::vector<float>` with `std::atomic_ref` (C++20); **preferred**: write own template `SpscRing<T>` with `memory_order_acquire/release` mirroring `ca_ring.c` | Use `std::span<float>` for read/write slices; add `capacity()` and `size()` helpers; use `std::atomic<size_t>` correctly (not _Atomic macro) |
| `ca_cmd_queue` MPSC queue | atomic+mutex | `std::queue<Cmd>` + `std::mutex` + `std::condition_variable` or `moodycamel::ConcurrentQueue`; for simplicity: `std::deque<Cmd>` guarded by `std::mutex` | Cmd `std::variant<Open{string}, OpenReader, Play, Pause, Resume, Stop, Seek{double}, Volume{float}>` replaces union 512. Preserve `CA_ERR_BUSY` on full via bounded `std::vector<Cmd>` ring rather than unbounded queue |
| `ca_log` global callback | vsnprintf 1024 | `spdlog` / `std::format` (C++20) / `fmt`; callback stays `std::function<void(LogLevel,string_view)>` | Make thread-safe with `std::mutex`; preserve level enum `LogLevel::Debug/Info/Warn/Error` |
| `ca_result` + `ca_error` | 13 codes + message 256 | `std::expected<T, CaError>` (C++23) or `tl::expected`; enum class `CaResult { Ok, InvalidArg, ... }` + `CaError { CaResult code; std::string message; }` | Keep `to_string` via `std::string_view`; update call sites to `if(auto r = foo(); !r) return r.error()` pattern |
| `miniaudio` device | ma_device F32 callback | Keep `miniaudio` C header in C++ (extern "C") or replace with `RtAudio` / `cubeb` / `SDL3 audio`; or wrap in RAII `AudioOutput { ma_device dev; std::atomic<float> volume; SpscRing<float>* ring; }` | Device init/start logic unchanged; ensure callback remains `noexcept` and `[[nodiscard]]` on setters |
| `dr_wav/dr_flac/dr_mp3` | single-header decoders via ca_reader | Nest `miniaudio` decoders or keep dr_* under `extern "C"`; or use `libsndfile` / `dr_libs` C++ wrappers; alternatives: `minimp3`, `dr_flac` as before | Easiest: keep headers in `extern "C"` and wrap in RAII `WavDecoder { drwav wav; }` with destructor `drwav_uninit` |
| `stb_vorbis` | stb_vorbis.c (stub synthetic) | Real `stb_vorbis` fetch or `libvorbis` / `minivorbis`; replace synthetic stub with true decoder (`stb_vorbis_open_memory` via custom read via memory slice) | Ensure OggS probe preserved; supply seek via `stb_vorbis_seek` |
| `FFmpeg` dlopen fallback | dlopen/LoadLibrary ASF probe | Same dynamic load but via `std::filesystem` + `dlopen` wrapper class `FfmpegLoader { void *avformat; void *avcodec; }` using `std::expected`; or link `libav*` via `find_package(FFmpeg)` when option ON | Consider `extern "C"` FFI types and `std::span<const uint8_t>` probe |
| `sqlite3` 3.46.1 + FTS5 | vendor sqlite3.c, WAL 32768, triggers | `sqlite_orm` / `sqlpp11` / raw `sqlite3*` with RAII wrappers (`SqliteDb`, `Statement`, `Transaction`); `SQLiteCpp` is also suitable | Wrap `sqlite3*` in `unique_ptr` with deleter; `Statement` caches via prepared stmt recycling; keep WAL pragma and triggers intact |
| SHA-256 (first 64KiB) | hand-rolled `ca_sha256_*` | `OpenSSL::SHA256`, `Botan`, or `std::` + `<openssl/sha.h>`; or `picosha2` header-only | Port must preserve same collision contract (optionally upgrade to full-file streaming via `std::ifstream` + incremental update) |
| Filesystem recursion | `opendir/readdir/stat` vs `FindFirstFileA` | `std::filesystem::recursive_directory_iterator` + `std::filesystem::file_size/last_write_time` | Use `std::filesystem::path::extension()` case-insensitive check for `mp3/flac/ogg/wav/m4a` |
| Build | CMake + GNUInstallDirs + CPack | Keep CMake but require C++20 (`CMAKE_CXX_STANDARD 20`, `REQUIRED ON`, `EXTENSIONS OFF`); `target_compile_features`, `find_package(Threads)` | Add `FetchContent` for Catch2/GoogleTest, `spdlog` optional |
| Version header | `ca_version.h.in` configure_file | Same mechanism; reuse `git describe` at `CMakeLists.txt:4-35` unchanged |  |
| TLS error | `__declspec(thread)/_Thread_local char[256]` | `thread_local std::string`, `thread_local CaError`; or `std::error_code` per thread | Avoid global mutable unless required for ABI compat |
| Clang tooling | `.clang-format/.clang-tidy` | Reuse identical config; add `clang-tidy` checks: `modernize-*, bugprone-*, concurrency-*`, `cppcoreguidelines-*` |  |

### Mapping table for C idioms → Modern C++

| C idiom | Modern C++ replacement |
|---------|------------------------|
| `ca_alloc` vtable + `has_alloc` flag | `std::pmr::memory_resource* resource = nullptr` (null → `std::pmr::get_default_resource()`); store as `std::pmr::polymorphic_allocator<std::byte>`; pass by `std::pmr::memory_resource&` |
| Fixed buffers `char path[1024]` | `std::string` (with `reserve`), or `std::array<char,1024>` if ABI constraint; DB columns become `std::string` fields via `sqlite3_column_text` → `std::string_view` copy |
| `strncpy(..., sizeof-1)` + null | `std::string::assign` + `.substr(0,N)`; for fixed buffer use `std::string_view` then copy with truncation check |
| `memset(&t,0,sizeof(t))` | Value-init `CaTrack t{}` or designated `CaTrack{ .path = ..., .title = ... }` (C++20 designated init) |
| `out` param + `CA_NODISCARD ca_result` | `std::expected<T, CaError>` or `std::expected<void, CaError>`; keep `[[nodiscard]]` |
| `CA_TLS char[256]` error | `thread_local CaError` + `std::string last_error()`; or `std::expected` carries error payload |
| `_Atomic int` + `memory_order` | `std::atomic<int>` / `std::atomic<uint64_t>` / `std::atomic<float>` (note `std::atomic<float>` is valid C++20 `is_always_lock_free` check) with same `memory_order_*` |
| `qsort` leak sort | `std::sort` + `std::ranges::sort` with comparator `file<=>line<=>seq` |
| `pthread_mutex_t` recursive vs `CRITICAL_SECTION` | `std::recursive_mutex` (or `std::mutex` + `std::lock_guard` — DB really needs recursion per `ca_db_internal.h:41`, consider refactoring to non-recursive via `std::mutex` + `std::scoped_lock` and eliminating re-entrance: current `ca_db_write_stmt_async` re-enters indirectly — mark as tech debt) |
| `sqlite3_stmt` prepare/step/finalize | RAII `Statement` wrapper (move-only, destructor finalize), plus `[[maybe_unused]]` nodiscard |
| `char sql[2048]` dynamic WHERE | `std::string sql; sql += " AND artist=?";` + `std::vector<std::any>` binds or `std::format` |
| `snprintf(path, sizeof, "%lld")` | `std::to_string(id)` or `std::format("{}")` (C++20) |
| `uint8_t fp[32]` | `std::array<std::uint8_t, 32>` + `std::span<const uint8_t,32>` |
| Union `ca_cmd` path/gain/seconds | `std::variant<OpenCmd{std::string path;}, OpenReader, PlayCmd, PauseCmd, ...>` |
| `ca_reader::stat` + `FILE*` | `std::filesystem::directory_entry::file_size/last_write_time` + `std::ifstream` over `std::span<const std::byte>` for mem, or use `FILE*` behind unique_ptr with custom deleter if keep C I/O for 64-bit portability |
| `sqlite3_randomness` Fisher-Yates | `std::shuffle` with `std::mt19937` seeded from `std::random_device` or `std::chrono` + QRNG; avoid global `srand/rand` note already respected |

---

## 6. C++ Mapping Guidance (for the implementer)

### 6.1 Suggested Project Structure

```
caudio-cpp/
├── CMakeLists.txt               # CXX 20, 4 libs + tests, same options
├── include/caudio/
│   ├── version.hpp              # generated from version.hpp.in via configure_file
│   ├── caudio.hpp               # umbrella
│   ├── utils/
│   │   ├── result.hpp           # CaResult enum class + to_string_view, CaError
│   │   ├── alloc.hpp            # TrackingResource : pmr::memory_resource
│   │   ├── arena.hpp            # class Arena { pmr::monotonic_buffer_resource mbr; }
│   │   ├── thread.hpp           # wrappers around jthread + sleep_for, priority
│   │   ├── ring.hpp             # template<SpscRing<T>> or SpscAudioRing
│   │   ├── cmd_queue.hpp        # BoundedMpscQueue<Cmd> with std::mutex or lockfree
│   │   ├── log.hpp              # enum class LogLevel, thread-safe callback
│   │   └── types.hpp            # CaState, init/player opts (use std::function)
│   ├── player/
│   │   ├── reader.hpp           # class Reader { virtual read/seek/tell/size; FileReader, MemoryReader }
│   │   ├── decoder.hpp          # IDecoder interface + registry, struct DecoderInfo
│   │   └── player.hpp           # class Player (RAII, movable non-copyable)
│   ├── db/
│   │   ├── types.hpp            # structs Track/Playlist/QueueItem/HistoryEntry/Library/Stats/Queries
│   │   ├── database.hpp         # class Database { SqliteHandle, recursive_mutex, writer }
│   │   └── schema.hpp           # constexpr string_view schema_sql
│   └── engine/
│       ├── engine.hpp           # class Engine (owns/borrows DB+Player, jthread monitor)
│       └── types.hpp            # RepeatMode, EngineEvent, EngineCallbacks, EngineOpts/State
├── src/
│   ├── utils/*.cpp
│   ├── player/*.cpp + decoders/
│   ├── db/*.cpp
│   └── engine/*.cpp
├── vendor/ (keep miniaudio/dr_*, or add via FetchContent: Catch2, sqlite amalgamation optional)
├── tests/ (Catch2 TEST_CASE; reuse helpers.h port → test_helpers.hpp)
└── examples/mini.cpp etc.
```

### 6.2 Namespace & Class Design

```cpp
namespace caudio {
namespace utils {
  enum class CaResult : int { Ok=0, InvalidArg, NotFound, Unsupported, Io, Device, State, NoMem, Internal, AlreadyExists, Busy, Corrupt, NoSpace };
  std::string_view to_string(CaResult) noexcept;
  struct CaError { CaResult code{ CaResult::Ok }; std::string message; };
  template<class T> using Expected = std::expected<T, CaError>; // C++23 (or tl::expected)

  class TrackingResource final : public std::pmr::memory_resource { /* counts sites */ };
  class Arena { // monotonic bump, 64B align
  public:
    explicit Arena(std::pmr::memory_resource* upstream, std::size_t cap);
    void* allocate(std::size_t n, std::size_t align);
    void reset() noexcept; ~Arena();
  };
  template<typename T> class SpscRing { // float specialization alias AudioRing
  public:
    SpscRing(std::pmr::memory_resource*, size_t cap_frames, uint32_t channels);
    size_t write(std::span<const T>); size_t read(std::span<T>);
  private:
    std::pmr::vector<T> buf; // or raw with pmr
    std::atomic<size_t> wr_{0}, rd_{0}; size_t cap_; uint32_t channels_;
  };
  struct Cmd { std::variant<Open, OpenReader, Play, Pause, Resume, Stop, Seek, Volume> payload; };
  class MpscCmdQueue { // bounded, returns Expected<void>
  public:
    Expected<void> push(Cmd); Expected<Cmd> pop();
  };
} // utils
namespace player {
  class Reader { public: virtual ~Reader(); virtual size_t read(std::span<std::byte>) = 0; virtual Expected<void> seek(int64_t, int whence) = 0; };
  class FileReader final : public Reader { /* FILE* or std::ifstream behind */ };
  class MemoryReader final : public Reader { std::span<const std::byte> data; size_t pos; };

  struct IDecoder { virtual ~IDecoder()=default; virtual size_t decode(std::span<float>) = 0; virtual Expected<void> seek(double) = 0; uint32_t sample_rate{}, channels{}; uint64_t total_frames{}; };
  // registry: inline std::vector<const DecoderVt*> &registry(); + register_builtins()

  class Player { // movable, non-copyable, RAII
  public:
    static Expected<std::unique_ptr<Player>> create(const PlayerOpts&, std::pmr::memory_resource*);
    Expected<void> open(std::string_view path);
    Expected<void> open_reader(std::unique_ptr<Reader>);
    Expected<void> play(); Expected<void> pause(); Expected<void> resume(); Expected<void> stop();
    Expected<void> seek(std::chrono::duration<double>);
    Expected<void> set_volume(float); // clamps
    CaState state() const noexcept; // atomic load
    std::chrono::duration<double> position() const noexcept;
    std::string_view last_error() const noexcept; // thread_local
  private:
    struct Impl; std::unique_ptr<Impl> p; // PImpl hides atomics/thread/ring/output/decoder
  };
} // player
namespace db {
  struct Track { int64_t id{}; std::array<uint8_t,32> fingerprint{}; std::string path; /* ... */ };
  class Database { // RAII sqlite3* + recursive_mutex + WriterThread
  public:
    static Expected<std::unique_ptr<Database>> open(std::filesystem::path, const DbOpts&);
    Expected<int64_t> track_insert(const Track&); // etc. overloads returning Expected
    void for_each_track(const TrackQuery&, std::function<void(const Track&)>) const;
    Expected<void> flush(); // wait ≤200ms or Busy
  };
} // db
namespace engine {
  enum class RepeatMode { Off, Queue, One };
  struct EngineEvent { EngineEventType type; int64_t track_id{}, queue_id{}; double pos{}, dur{}; std::string msg; };
  struct EngineCallbacks { std::function<void(int64_t)> on_track_started; /* ... */ void* user{}; };
  class Engine { // owns or borrows DB+Player, jthread monitor
  public:
    static Expected<std::unique_ptr<Engine>> open(std::filesystem::path db, const EngineOpts&);
    static Expected<std::unique_ptr<Engine>> attach(Database&, player::Player&, const EngineOpts&);
    Expected<void> play(int64_t queue_id); Expected<void> next(); Expected<void> prev();
    Expected<void> set_shuffle(bool); Expected<void> set_repeat(RepeatMode);
    Expected<EngineEvent> poll_event(); size_t drain_events(std::span<EngineEvent>);
  };
} // engine
} // caudio
```

- Use `[[nodiscard]]` on every `Expected`, `std::string_view` for non-owning strings, `std::span<const float>` for decode buffers, `std::chrono` for durations, `std::filesystem::path` for every path argument, `std::jthread` stop_token for the decode/monitor loops, `std::stop_source` for cancellation.
- Use `concepts` for decoder vt: `template<DecoderConcept T> void register_decoder()` ensuring `probe(span) -> bool`, `open → Expected<unique_ptr<IDecoder>>` etc., or keep classic vt table with `std::function`.
- Use `std::ranges` for leak sort, fingerprint hex conversion, `std::views` for timeline filtering in tests.

### 6.3 Error Handling

- Replace `ca_result` integer returns with `std::expected<T, CaError>` (C++23; fallback `tl::expected` or `std::variant<T, CaError>`). Keep `CaResult` enum for compatibility with `CaError::code`.
- Thread-local errors become `thread_local CaError tl_last;` plus per-object `CaError last_;` protected by `mutable std::mutex` if accessed cross-thread.
- `CA_NODISCARD` → `[[nodiscard]]`.
- Exceptions: **do not throw across ABI** if preserving C ABI shim; inside C++ internals either use `expected` exclusively (no throw) or define exception type `CaException : std::runtime_error { CaResult r; }` and translate to error before crossing module boundary. Prefer `expected` per performance critical audio path (no exception in `ca_output_data_callback` — mark `noexcept`).

### 6.4 Smart Pointers & Ownership

- `ca_player_create/destroy` → `std::unique_ptr<Player>` with custom deleter; internal members (`ring`, `output`, `decoder`, `reader`) all `std::unique_ptr` with pmr deleters; writer/monitor threads owned via `std::jthread`.
- `ca_reader*` ownership after `open_reader`: player takes `unique_ptr<Reader>` (move-in), matching doc "player takes ownership" at `src/player/ca_player.c:544`.
- `ca_db*`: `Database` owns `sqlite3*` via `unique_ptr<sqlite3, SqliteDeleter>` + `recursive_mutex` + `std::unique_ptr<WriterThread>`; copied paths as `std::string path_`. Writer holds raw `sqlite3*` borrowed.
- `ca_engine`: optionally owns (`owns_db/owns_player` bools at `ca_engine_internal.h:22`) → in C++ `bool owns_db_` + `std::unique_ptr<Database>` vs `Database* borrow_` pattern; prefer `std::variant<unique_ptr, raw pointer>` or two constructors.

### 6.5 Containers, Ranges, Concepts, Coroutines

- **Containers**: `std::vector<float>` for ring buffers (with pmr), `std::string` for TEXT cols, `std::array<uint8_t,32>` for fingerprint, `std::unordered_map<int64_t, Track>` cache, `std::deque<Cmd>` or `std::vector<Cmd>` ring for mpsc.
- **Ranges/Concepts**: `template<std::ranges::range R> void for_each_track(R&&)` / use `std::views::filter` for query helpers; `concept Decoder = requires(T t){ {t.probe(std::span<const uint8_t>{})} -> std::same_as<bool>; }`.
- **Spans**: decode uses `std::span<float> out` with `out.size()` bounds; reader uses `std::span<const std::byte>`; ring uses `std::span<const float>`. Avoid raw `float* + size` split.
- **Coroutines**: not required for hot paths, but consider `co_yield` generator for `track_list` (`std::generator<const Track&>`) or async scan via `co_await` on filesystem traversal. If used, keep decode/output poll loop as `jthread` + blocking, not coroutine, to avoid allocation in RT thread.

### 6.6 Build System (CMake)

```cmake
cmake_minimum_required(VERSION 3.28)
project(caudio VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)  # or 23 if std::expected needed without TL
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Threads REQUIRED)
add_library(caudio_utils STATIC src/utils/...)
target_compile_features(caudio_utils PUBLIC cxx_std_20)
target_compile_options(caudio_utils PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)
# similarly for player/db/engine + alias caudio::*
option(CAUDIO_WITH_FFMPEG "..." OFF)
# SQLite: keep vendor/sqlite3.c as OBJECT lib or use find_package(SQLite3)
include(FetchContent); FetchContent_Declare(Catch2 URL ...); FetchContent_MakeAvailable(Catch2)
enable_testing(); include(CTest); include(Catch)
```

Preserve `CAUDIO_BUILD_TESTS/EXAMPLES` pattern and `caudio::utils_shared` shared variants with export macros adapted for C++ (`CA_API` as `__declspec(dllexport)` unchanged).

### 6.7 Testing

- **Framework**: **Catch2 v3** (or GoogleTest). Port 37 tests 1:1: `TEST_CASE("ca_ring write/read")` etc. Use `SECTION`, `REQUIRE`, and `TEMPLATE_TEST_CASE` for typed ring/cmd tests.
- **Leak check**: replace `CA_DEBUG` qsort with Catch2 `SECTION` + `TrackingResource::live_count() == 0` assertion (`REQUIRE(tracking.live_bytes()==0)` after each test).
- **Sanitizers**: same `CMAKE_CXX_FLAGS="-fsanitize=address,undefined"`; compile with `-DCA_DEBUG` via `target_compile_definitions`.
- **Race tests**: port `test_race_*` to `std::jthread` spawn harness, keep `ctest -V` naming.

### 6.8 Performance Considerations

- Keep audio callback `noexcept`, no allocation, `std::atomic<float> volume` relaxed load, `SpscRing` acquire/release identical to C. Verify with `clang -Rpass` that no hidden alloc (e.g., `std::string`) enters hot path.
- Decode thread: preserve chunk ≤1024 + 2048/ch bound (`ca_player_internal.h:15`) to cap latency at ~21 ms at 48 kHz. Keep gate wait bounded (500×1ms) but add metric/logs (see weaknesses).
- DB: batch writer already exists; in C++ consider `PRAGMA busy_timeout` and `sqlite3_busy_handler`, or use `WAL+synchronous=NORMAL` unchanged. Keep `recursive_mutex` only if needed — else switch to `std::mutex` to avoid priority inversion.
- Filesystem scan: parallelize via `std::execution::par` / `std::for_each` on file list (collect first via `recursive_directory_iterator`) plus per-file SHA streaming via `std::ifstream` read 64 KiB chunks — keep current 64 KiB prefix semantics unless user opts full-hash.
- Use `std::pmr` to avoid per-frame alloc chatter; keep decode `tmp[2048]` stack array (or `std::array<float,2048>`) unchanged.

---

## 7. Porting Checklist (step-by-step, AI-followable)

1. **Scaffold** `caudio-cpp` repo, copy `include/caudio/ca_version.h.in` → `version.hpp.in`, replicate `CMakeLists.txt` skeleton with `CXX 20`, alias targets, vendored headers.
2. **Port `caudio::utils` first** (no external deps):
   - `result.hpp`: `enum class CaResult` + `constexpr std::string_view to_string(CaResult)` table; `struct CaError` with `CaResult+string`.
   - `alloc.hpp`: implement `TrackingResource : pmr::memory_resource` storing `unordered_map<void*, Site{string_view file; int line; size_t sz; uint64_t seq;}>`; expose `live_count()/live_bytes()/dump_sorted()`. Wrap every `allocate/deallocate` with record.
   - `arena.hpp`: `Arena(pmr::memory_resource*, size_t cap)` allocating `cap+64` via upstream, aligning with `std::align(64, ...)`, bump.
   - `thread.hpp`: `class JThread { std::jthread jt; public: static Expected<JThread> create(auto fn); }`, map priority `int` → platform via native_handle; `set_name(string_view)`; free func `sleep_for(milliseconds)`.
   - `ring.hpp`: template `SpscRing<T>` porting `ca_ring.c:65` memcpy wrap; `static_assert(std::atomic<size_t>::is_always_lock_free)`.
   - `cmd_queue.hpp`: bounded 64 Mpsc using `std::mutex` + `std::condition_variable` + atomic indices; variant payload.
   - `log.hpp`: thread-safe `std::function<void(LogLevel,string)>` + `std::mutex`.
   - Validate with ported `test_utils_*` (Catch2) 1:1 line coverage.
3. **Port `ca_reader`** (`reader.hpp/cpp`): abstract `Reader`, `FileReader(path, pmr*)` using `std::filesystem` + `std::ifstream` binary (keep _ftelli64 path for large-file compat via `fseeko64` fallback behind `__cpp_lib_filesystem`), `MemoryReader(span, pmr*)`. Preserve 64-bit `tell/size` and seek clamp semantics (`seek(invalid)=InvalidArg`).
4. **Port `ca_decode` infra** (`decoder.hpp`): `IDecoder` pure virtual, registry `vector<DecoderDesc>` with `probe(span)->bool`; `Expected<unique_ptr<IDecoder>> decode_open(Reader&, pmr*, Arena&)` mimicking `ca_decode.c:48` (probe 32B, restore offset). Add `set_priv/get_priv`-style type-erased slot if needed, but prefer typed ctx per decoder.
5. **Port decoders** one by one (`decoders/*.cpp`): keep `extern "C" #include "dr_wav.h"` etc. inside `detail` namespace; wrap contexts in RAII classes (`WavDecoder { drwav wav; bool inited; unordered_map ctx; }`), implement synthetic fallback with same rates (Wav 8000/1, Flac 44100/2, Mp3 48000/2, Vorbis 22050/1) and `fill_sine` helper (`decoder_common.h:42` → `detail::fill_sine(span<float>, ...)`). Implement FFmpeg stub loader class (dlopen probe ASF) returning `CaResult::Unsupported`. Add unit tests `test_decode_registry`, `test_decoder` with real sample wav.
6. **Port `ca_output`** (`output.hpp/cpp`): RAII `AudioOutput { SpscRing<float>* ring; std::atomic<float> volume; ma_device dev; }`; callback `noexcept`, `std::atomic<float> volume` relaxed, zero-fill via `std::ranges::fill`. Provide `test_fill(span<float>)` helper for tests. Preserve `≤32 channel` cap, default sr 48000 ch 2, dummy fallback if `ma_device_start` fails.
7. **Port `ca_player`** (`player.hpp/cpp`, PImpl): replicate `ca_player.c:40` state atomics (`std::atomic<CaState>`, `atomic<uint64_t> pos_base/pos_start_ms`, `atomic<bool> is_playing`, `atomic<int> open_gate/decode_busy`). Implement `Impl::decode_thread` via `std::jthread` with `stop_token` + `open_gate` polling. Methods `open/Seek/Stop` use `open_gate=1; wait 500×sleep 1ms for decode_busy==0` same as `ca__pause_decode:ca_player.c:122`. Keep preroll `cap/2` and position formula `(now-start)*sr/1000`. File parity: exhaustive `test_player`, `test_seek`, `test_gapless`, `test_race_player_open`.
8. **Port `ca_db` schema + Database** (`db/schema.hpp`, `database.hpp/cpp`): move `ca__schema_sql:ca_schema.c:4` to `constexpr string_view kSchemaSql`; implement `Database::open(path, opts)` via `sqlite3_open`, `exec(schema_sql)`, insert default library 1, start `WriterThread`. Wrap `sqlite3*` in unique_ptr deleter, `RecursiveMutex` (start recursive, later non-recursive once audit passes). Provide RAII `Statement` wrapper for prepare/bind/step/finalize. Port every `ca_db_*` function to `Expected<T>` form, preserving locking placement (`__db_lock` around every prepare/step/finalize per `ca_db.c` pattern), string truncation (`string::substr(0,N)`), null binding for timestamps, and sqlite→CaResult mapping (`sqlite_to_ca_ex:ca_db.c:12`). Keep `track_list` dynamic WHERE builder with `LIKE … ESCAPE '\'` at `ca_db.c:465`.
9. **Port `WriterThread`** (`db/write_thread.hpp/cpp`): keep float-ring hijack OR replace with typed `moodycamel::ConcurrentQueue<WriteOp>` (simpler, less UB). If keeping ring, use `SpscRing<std::byte>` with `sizeof(WriteOp)` stride. Implement `flush()` as 200×1ms poll for `available==0 && in_flight==0` else Busy. Preserve callbacks.
10. **Port `scan`** (`db/scan.cpp`): replace posix/win branches with `recursive_directory_iterator` + `directory_entry`; extension check case-insensitive `path.extension()==".mp3"`; compute SHA via `picosha2` or OpenSSL SHA256 streaming first 64 KiB (`compute_fingerprint_file:ca_scan.c:154`). Replicate upsert branching exactly (skip on path+size+mtime hit, fingerprint conflict branch, same-path content-changed branch clearing metadata — preserve `play_count` etc.). Wrap iteration with `__db_lock`.
11. **Port `search`** (`db/search.cpp`): sanitize FTS query per `sanitize_fts_query:ca_search.c:64` (strip OR/AND/NOT/NEAR via token split, replace `*:-():\^'` with space, double `"`). Try `MATCH ? ORDER BY rank LIMIT ?`, fallback prefix `*`, then LIKE 3-col fallback (title/artist/album prefix) per `ca_search.c:172`. Keep prepared stmt lifecycle.
12. **Port JSON export/import** (`db/json.cpp`) OR **better**: replace hand parser with `nlohmann/json` or `simdjson`+`fmt` (see weaknesses); if retaining hand parser, port `json_escape:ca_db.c:1623` + import state machine (find `"id"`, brace escape-aware scan at `ca_db.c:1999`) faithfully, including `FNV generate fingerprint:ca_db.c:1899` fallback. Ensure export writes same field order for golden tests.
13. **Port engine queue logic** (`engine/queue_logic.cpp`): `QueueState { bool shuffle; RepeatMode repeat; vector<int64_t> perm; size_t cursor; int64_t queue_id; pmr::memory_resource* alloc; }`; implement `shuffle_perm` via `std::shuffle` + `mt19937{random_device{}()}` (or sqlite_randomness wrapper for determinism). Persist `shuffle_perm` blob via `sqlite3_bind_blob` (memcpy little-endian `int64_t` array). Preserve prev cursor semantics (`cursor-=2` etc. at `ca_queue_logic.c:324`).
14. **Port `history_policy`** (`engine/history_policy.cpp`): two overloads `should_mark_played_ex(duration, pos, marked, pct, secs)` returning `bool` per `ca_history_policy.c:3`.
15. **Port `engine`** (`engine/engine.cpp`, PImpl): full `Engine` class preserving `engine_state_load/save:ca_engine.c:37/128` (BEGIN IMMEDIATE, blob ↔ perm), `push_event` MPSC spin lock `atomic<int> ev_lock CAS` + drop-on-full, `engine_tick` (500ms PROGRESS, gapless CAS), `do_history_mark` CAS 0→1 exactly-once + transaction rollback/reset on failure, monitor `jthread` polling `poll_ms`, `engine_do_play_track` (player open→play, set current_dur, has_current, gapless_armed, started_ms, save, push TRACK_STARTED). Keep `queue_lock` CAS BUSY semantics on every `play/next/prev/set_*`. Implement `poll_event/drain_events` via atomic `ev_r/ev_w` acquire/release identical to C.
16. **Port `examples/` to C++** (`examples/mini.cpp` etc.) using `std::chrono`, `std::cout`, same defaults.
17. **Bring `tests/`** over to Catch2 with `helpers/test_helpers.hpp` (fingerprint helper, sample finder). Keep fixture `tests/fixtures/sample.wav`. Achieve 37→37 parity.
18. **Polish**: `clang-format` (run), `clang-tidy` `modernize-*, bugprone-*`, ASan/UBSan CI matrix, `ctest -V`.
19. **Docs/CPack**: update README C++ sections, bump generated `version.hpp` via same `git describe` logic; verify `find_package(caudioCpp CONFIG)` exports four targets + shared aliases.

---

## 8. Weaknesses & Recommendations for C++ Version

### 8.0 Global / Cross-Cutting

*All weaknesses are load-bearing for the C++ port: the C version is functional and passes sanitizer/ctest, but each item below induces risk (data-lost, UB, deadlock, leak, or test-gaming) that the rewrite should remove.*

#### W1 — Global mutable state + C-style “god mutex” in DB

**What:** `struct ca_db { sqlite3 *handle; …; db_lock; path; }` (`ca_db_internal.h:21`) uses a **single recursive `pthread_mutex`/CRITICAL_SECTION** (`ca__db_lock_init:ca_db_internal.h:35`) held around *every* sqlite prepare/step/finalize in `ca_db.c` (dozens of sites) and also by `writer_loop` (`ca_write_thread.c:59`) and `scan_file`/`engine_state_*`. Writer thread contends the same lock.

**Why it matters:** coarse-grained; masks logical races but causes starvation/“BUSY” false positives, and recursive mutex hides re-entrancy bugs (SCAN calls into DB while holding lock across external I/O at `ca_scan.c:209-229`). Also inhibits concurrent readers after WAL mode (SQLite allows concurrent reads).

**C++ remedy:** replace with fine-grained `std::shared_mutex` (readers concurrent, writers exclusive) or `std::mutex` + `std::scoped_lock` + **no hold across I/O** (snapshot size/mtime/fingerprint before lock, then lock only for SQL). Audit and remove recursion: refactor any re-entrant call (e.g., `ca_db_track_get` inside `do_history_mark` holding the outer BEGIN lock) to use a single transaction handle object (`Transaction { sqlite3* ; bool committed; }`). Provide `Database::with_lock<R>(func)` scoped wrapper.

#### W2 — Type-punning `ca_write_op` over `float` ring (UB)

**What:** `struct ca_write_thread { ca_ring* queue; float *tmp; size_t floats_per_op=(sizeof(ca_write_op)+3)/4; }` (`ca_write_thread.c:13/113`) and `enqueue_op:ca_write_thread.c:211` / `writer_loop:ca_write_thread.c:42` `memcpy(buf,&op,sizeof(op))` over a `float` buffer. `ca_ring` operates on `float` samples — using it for arbitrary `sql[1024]+stmt*+callback` relies on `memcpy` through `float*` and size rounding not alias-safe, plus `stack_buf[512]` overflow guard is ad-hoc.

**Why it matters:** strict-aliasing UB, silent truncation if `WriteOp` grows beyond `512*4=2048B`, and in-flight lifetime of `stmt*` passed between threads without ownership contract (caller may free stmt after async call).

**C++ remedy:** replace with **typed MPSC queue**: `struct WriteOp { std::string sql; sqlite3_stmt* stmt; std::function<void(Expected<void>)> cb; };` + `moodycamel::ConcurrentQueue<WriteOp>` or `std::queue<WriteOp>` + `std::mutex`/`condition_variable`. Make `stmt` ownership move-only (`unique_ptr<Statement>`), flush by draining to batch `sqlite3_exec` under the writer’s lock. Add `static_assert(sizeof(WriteOp) <= 1024)`.

#### W3 — CA_DEBUG leaks: fixed 4096 cap, non-atomic, no-race

**What:** `CA_DBG_MAX_RECS 4096` (`ca_alloc.c:19`), global arrays `g_recs[4096]` + `g_live/g_live_bytes/g_seq` mutated without atomics (`ca_alloc.c:27`). `ca_dbg_add/remove` (`ca_alloc.c:35/49`) loop-linear scan O(n) swap-remove, no lock, shared between decode/player threads. `ca_arena_create:ca_arena.c:36` duplicates the alloc path under `#ifdef CA_DEBUG` with near-duplicate code (drift risk).

**Why it matters:** with threads creating players concurrently, `CA_DEBUG` races cause miscounts / double removes; fixed cap silently drops records past 4096 (leaks invisible). Not portable to C++ PMR accounting.

**C++ remedy:** implement `TrackingResource : pmr::memory_resource` with `std::mutex` + `std::unordered_map<void*, Site>` + `std::atomic<uint64_t> seq`; use `thread_local` fast path for leaf allocations. No fixed cap; dump sorted via `std::ranges::sort` and aggregate by `(file,line)`. Remove arena `#ifdef CA_DEBUG` duplication — single path: `pmr::polymorphic_allocator<std::byte>{resource}.allocate(...)`.

#### W4 — JSON hand-rolled parser is fragile / UB-prone

**What:** `ca_db_export_json:ca_db.c:1647` hand-escapes via `fprintf("\\u%04x")` for `<0x20`; `ca_db_import_json:ca_db.c:1921` hand-parses by `strstr("\"id\"")`, brace-scan counting `"\""` while honoring `\\` (`ca_db.c:1999`), `find_json_field:ca_db.c:1793` scans every char with `memcmp`, extractors copy into 64B tmp blocks, re-re-allocates import buffer via `malloc(sz+1)`. No schema validation; fingerprint re-generation via FNV+mix (`ca_db.c:1899`) may collide.

**Why it matters:** any unescaped unicode beyond `\uXXXX` path couples to nonstandard `json_escape/unescape` pair; trailing-comma/garbage detection at `ca_db.c:2260` uses heuristics; invalid JSON can pass or corrupt DB (partial txn rollback handles only post-`BEGIN`).
**C++ remedy:** replace with **`nlohmann/json`** (single header, integrates offline via `FetchContent`) or **`simdjson`** for large imports: `json j = json::parse(std::string_view{buf, sz}); for(auto &o: j["tracks"]) { Track t=o.get<Track>(); }`. Preserve field order via `json::object` with ordered iteration for export golden tests. Keep fingerprint re-gen path but use `std::hash<std::string>` + `std::array<uint8_t,32>` via `OpenSSL SHA256` fallback instead of FNV.

#### W5 — Vorbis and FFmpeg stubs lie as successes

**What:** `ca_stb_vorbis_vt:stb_vorbis.c:98` never invokes real `stb_vorbis.c` (`vendor/stb_vorbis.c` exists but is *not linked into the decoder*; the C file only fills sine at `stb_vorbis.c:64`). FFmpeg `ca_ffmpeg_open:decoders/ffmpeg.c:126` always returns `UNSUPPORTED` even if library was successfully `dlopen`’d. Both still pass tests because synthetic fallback returns `CA_OK`, masking missing format coverage.

**Why it matters:** Ogg and WMA paths claim success while returning synthetic audio (silent sine) — user gets wrong content without error. Tests pass despite incomplete feature.

**C++ remedy:** either **wire real libs** (fetch `stb_vorbis.c` decode path via `stb_vorbis_open_pushdata` + `Seek`, and FFmpeg via proper `AVIOContext` + `avformat_open_input` + `avcodec_send_packet`) or **fail probe** (return `Unsupported` on real open failure) so caller knows format unavailable. Add integration tests that decode a real `.ogg` fixture and compare frames to golden PCM.

#### W6 — Thread lifetimes: non-joinable / race-on-destroy

**What:** `ca_player_destroy_internal:ca_player.c:317` joins only if `thread_alive==1`; `ca_thread_join:ca_thread.c:45` on POSIX frees the `pthread_t*` heap allocation while thread may still pointer-dereference trampoline `tr` (`ca_thread.c:133`). `ca_engine_destroy:ca_engine.c:633` saves `engine_state` under `db->handle` after joining monitor, but `db->handle` may be concurrently touched by writer thread (no happens-before). `ca_write_thread_destroy:ca_write_thread.c:125` calls `stop` which joins writer but DB `handle` not cleared.

**Why it matters:** join-free race plus manual heap `pthread_t*`, `ca_tramp` double-free paths, subtle use-after-free on rapid `create/destroy` loops (race tests cover partially but not destructor ordering).

**C++ remedy:** **RAII `std::jthread`** (auto-joins in destructor, cooperates via `stop_token`), no heap `pthread_t*`. Store trampoline as `std::function` + lambda capture (no `free` on thread side). `Engine`/`Player`/`Database` destructors join in dependency order: monitor → decode → writer → sqlite close, with `std::stop_source` signaling before join. Add `[[clang::requires_lock_not_held]]` annotations guiding callers.

#### W7 — 500 ms gate spin / sleep polling is brittle

**What:** `ca__pause_decode:ca_player.c:122` spins `while(decode_busy && waited<500) sleep 1` ; `ca_player_thread_fn:ca_player.c:252` sleeps `2ms` on gate or idle; `writer_loop:ca_write_thread.c:85` + `monitor_loop:ca_engine.c:381` sleep 1–10 ms. No condition variable, no futex.

**Why it matters:** latency tail (gapless may miss `gapless_ms` deadline), power inefficiency, missed wakeups (flush BUSY). 500 ms cap means large decode chunk could remain busy → caller continues unsynchronized and mutates `decoder` dangling.

**C++ remedy:** replace all spins with `std::condition_variable` (+ `std::atomic<bool>` gate) or `std::counting_semaphore`. Writer/monitor become `while(!stop_token.stop_requested()) { queue.wait(stop_token) | sleep_until(next_tick); }`. Decode thread parks via `std::binary_semaphore` until `open_gate==0 && is_playing`.

#### W8 — SQLite finalized under held lock + stmt not cached

**What:** every DB helper (`ca_db_track_insert:ca_db.c:222`, `ca_db_scan_library:ca_scan.c:236`, …) `prepare→step→finalize` inside the same `__db_lock` region. No statement cache; dynamic WHERE builders (`ca_db_track_list:ca_db.c:479`) `snprintf` 2048 SQL each call. SQLite handles WAL journal_mode each `ca__schema_init` exec with multi-statement `ca__schema_sql:ca_schema.c:4` (five pragmas+ DDL in one exec).

**Why it matters:** lock held during finalization + `snprintf` dominates; no reuse of prepared statements → latency. Multi-statement exec hard to error-attribute.

**C++ remedy:** introduce `StatementCache { unordered_map<string, sqlite3_stmt*> pool; }` with LRU, reset & clear bindings via `sqlite3_reset/clear_bindings` (move-only guard). Split schema into discrete `exec` per statement with `std::array<string_view, N>`. Use `sqlite3_prepare_v2` outside lock? Actually keep prepare inside lock if connection shared — correct under mutex.

#### W9 — Fixed buffers + truncation silently

**What:** C structs carry fixed arrays (`path[1024]`, `title[256]`, `cover[512]`, `genre[64]` at `ca_db_types.h:30/103`). Inserts call `sqlite3_bind_text(..., -1, TRANSIENT)` without length truncation check, but fills via `strncpy(..., sizeof-1)` at read side — oversize Title is truncated silently and rounds to FTS inconsistency (index contains truncated string). `ca_cmd.path[512]` at `ca_cmd.h:27` shares the union with `seconds/gain`.

**Why it matters:** silent data loss, FTS/search divergence, path max 1024 insufficient for deep Windows `\\?\` paths (260→32k). Union alias means bit pattern of `double seconds` can masquerade as path bytes in debugger.

**C++ remedy:** replace fixed arrays with `std::string` (owning) in C++ structs; persist with `sqlite3_bind_text(..., str.size(), TRANSIENT)`; for network/FFI path keep a `constexpr size_t kMaxPath=4096` check returning `Corrupt/NoSpace`. Replace union with `std::variant`.

#### W10 — Scan SHA of only first 64 KiB + hand SHA impl

**What:** `compute_fingerprint_file:ca_scan.c:154` `fread 64*1024` then SHA single-block; notes at `ca_scan.c:148` already document collision. SHA impl shadows names `ROTRIGHT` etc. without constant-time; no salt. Dedup delete constrained to `AND fingerprint=?` but collision still collapses tracks.

**Why it matters:** adversarial or accidental same-prefix files coalesce; test fixtures small so not caught. In-file SHA is duplicated elsewhere (import gen uses FNV mix).

**C++ remedy:** stream full file via `std::ifstream` reading 64 KiB chunks + incremental `OpenSSL SHA256` (or `picosha2`) to hash entire file; add `(size, mtime)` into hash state if keep fast path for large libs (optional `--fast` flag). Provide migration path for existing DB (re-hash on next scan when `last_scanned` missing fingerprint for full-file).

#### W11 — Error/message path is mixed

**What:** `ca_result` + TLS `g_tls_last_error[256]` (`ca_player.c:38`) + per-player `last_error[256]` (`ca_player.c:68`) + per-engine `last_err[512]` (`ca_engine_internal.h:41`) + `ca_error_set` printf-style (`ca_error.c:6`) + sqlite TEXT error via `sqlite3_free(err)` inconsistently freed (`ca_engine.c:137` conditional free). Callers must check both `ca_result` and `*_last_error`.

**Why it matters:** multiple sources; `last_error` 256 truncates sqlite message (may be longer), TLS makes tests order-sensitive under threads.

**C++ remedy:** unify on `std::expected<void, CaError>` where `CaError { CaResult code; std::string message; }` carries owning message. Eliminate TLS; keep `last_error()` returning `std::string_view` over per-object `CaError::message` guarded by mutex. Convert sqlite `char *err` to `std::unique_ptr<char, SqliteFree>` RAII wrapper.

#### W12 — Search sanitizer grammar incomplete

**What:** `sanitize_fts_query:ca_search.c:64` replaces `"*/-():\^` with spaces, doubles `"` → `""`, converts `'`→space, strips bare `OR/AND/NOT/NEAR` tokens via `strtok`/`strcspn`. Non-ASCII, column filters (`title:foo`), `NEAR/5`, quoted phrases after stripping behave unexpectedly. LIKE fallback covers only title/artist/album (`ca_search.c:172`) ignoring `album_artist/genre`.

**Why it matters:** user query `"The Beatles - Abbey Road"` loses phrase semantics; search may return 0 rows then prefix `*` retry triggers no ranking.

**C++ remedy:** use SQLite’s own `fts5` escaping: quote terms via `"\""+escaped+"\""` + `fts5:matchinfo` ranking; or integrate `fts5_tokenizer` `unicode61 "remove_diacritics 2"`. Fallback LIKE should cover all 5 FTS columns with `COLLATE NOCASE`. Add property-based tests for query strings.

#### W13 — DB recursive mutex hides logical ordering bugs

**What:** `ca__db_lock_init:ca_db_internal.h:39` sets `PTHREAD_MUTEX_RECURSIVE` (Windows CRITICAL_SECTION is inherently recursive). Numerous paths nest `__db_lock` via `ca_db_track_get` inside `do_history_mark`’s ongoing BEGIN transaction (`ca_engine.c:272`).

**Why it matters:** recursion masks missing lock ordering; a future port to `std::mutex` would deadlock. Also blocks upgrade to `shared_mutex`.

**C++ remedy:** remove recursion: refactor transactional helpers to accept `Transaction&` scope object (RAII `BEGIN→COMMIT/ROLLBACK`) so inner calls reuse the transaction’s connection without re-locking. Annotate functions `[[requires_lock_held(db.mtx)]]` vs public wrappers `[[requires_lock_not_held]]` to enforce discipline via `clang-thread-safety` attributes.

#### W14 — Magic constants & synthetic rates not tested against spec

**What:** `CA_PLAYER_RING_CAP 8192`, `CMD_CAP 64`, `ARENA 64KiB`, `DECODE_CHUNK 1024`, `RING_CAP/2 preroll`, `gapless_ms 300`, history 60%/90s, synthetic total frames `8000/44100/22050/48000` differ per decoder. Cap/floor defaults at `ca_player.c:76-89`, `ca_engine.c:474`, `ca_output.c:76`.

**Why it matters:** dedup to header keeps tuning coupled; synthetic totals cause `gapless_ms` false-trigger vs `total` mismatches in tests (gapless arms but decode is sine). No central config doc.

**C++ remedy:** central `constexpr` config header `Config { static constexpr size_t kRingCap=8192; static constexpr size_t kCmdCap=64; … }`; make `EngineOpts` carry `std::chrono::milliseconds gapless, poll`; make decoder synthetics share a single `kSyntheticTotal = 1s * rate` derived deterministically so arithmetic stays sound. Expose as `engine::kDefaultGapless = 300ms`.

#### W15 — Build/install portability nits

**What:** SQLite vendoring via `caudio_sqlite3_obj OBJECT` linked into multiple shared libs (`caudio_db_shlib`, `caudio_engine_shlib`, `caudio_shlib` at `CMakeLists.txt:199/279/329`) — each gets its own sqlite copy (binary bloat + not singleton journal). Vendored `sqlite3.c` ~8 MB compiled 3×. `FetchContent` URL is 2024 snapshot (`sqlite-autoconf-3460100.tar.gz:CMakeLists.txt:174`) pinned by hash (good). CPack vendor contact hard-coded `caudio@example.com`.

**Why it matters:** object size bloat; two concurrent sqlite connections to same file via separate amalgam instances may conflict on `sqlite3_initialize`. FetchContent path cannot build offline without network cache.

**C++ remedy:** build single `caudio_sqlite` STATIC/OBJECT once and link publicly: `target_link_libraries(caudio_db PUBLIC caudio_sqlite)` and let consumers transitively link (or use system `find_package(SQLite3)` with fallback). Add `FetchContent` option for offline cache (`FETCHCONTENT_SOURCE_DIR_SQLITE3`). Fix install rules to avoid exporting sqlite internal symbols (`PRIVATE`).

#### W16 — Testing / coverage gaps

**What:** 37 tests pass `ctest -V` but: ① gapless/hist edge rely on timing/sleeps (`history_edge`, `shuffle_restore`) → flaky; ② no real `.ogg` decode exercising `stb_vorbis`, no real FFmpeg WMA file; ③ `import_json` corruption checks rely on handcrafted strings, no fuzz; ④ no ASan on MinGW as noted in `docs/superpowers/specs/2026-09-02-caudio-db-design.md:142`; ⑤ no benchmark for ring throughput/jitter.

**Why it matters:** C++ port without tightening would paper over perf/robustness.

**C++ remedy:** Catch2 tags `[@engine, @timing]` plus deterministic virtual clock (`std::chrono::steady_clock` mock) for history/gapless tests; golden PCM files per format (generated via `dr_libs` fixture script); fuzz `import_json` via `libFuzzer`/AFL++ + JSON schema validator; add microbench `bench_ring` using `nanobench`; enable sanitizers in CI (Linux GCC) with `ctest -T memcheck`.

#### W17 — Ownership semantics of `ca_alloc::user` and custom allocator pitfalls

**What:** every func takes `ca_alloc *` which may hold `user` opaque ptr whose lifetime caller must manage; copy-assign via `*alloc` snapshot but original `user` may be freed while DB still holds copy (`ca_db_open:ca_db.c:128` stores alloc). Leak tracker copies `g_debug_alloc` snapshot (`ca_alloc.c:101`) — inconsistent.

**Why it matters:** UAF if user passes stack alloc.

**C++ remedy:** replace with `std::shared_ptr<pmr::memory_resource>` (shared ownership) or `std::pmr::memory_resource&` with documented lifetime (resource must outlive DB); provide `OwningMemoryResource` that owns its buffer. Remove `has_alloc` flag — check for `nullptr` resource → `get_default_resource()`.

#### W18 — Platform ifdefs scattered

**What:** `_WIN32` branches inside `ca_thread.c`, `ca_scan.c`, `ca_cmd.c`, `ca_db_internal.h`. Lengthy, duplicated trampoline struct definitions (`ca_tramp:ca_thread.c:13/128`). No single `platform.hpp` abstraction.

**Why it matters:** adding new platform (Emscripten, Android) touches many files.

**C++ remedy:** central `platform/filesystem.hpp` + `thread_platform.hpp` with `namespace caudio::platform`, single `native_thread_handle` wrapper, and `std::filesystem` for all dir operations (no `FindFirstFileA`/`opendir` duplication). Use `#ifdef` once at abstraction boundary.

---

*End of Weaknesses.*

---

## 9. C API Shim (optional, for parity)

If the C++ port must remain link-compatible with the C headers (`include/caudio/**/*.h` `extern "C"`):

- Keep the C headers unchanged (or regenerate with `CA_EXTERN_C` guarded) and provide `extern "C"` thunks in `src/shim/c_api.cpp` that forward to `caudio::*` classes using `reinterpret_cast` on opaque handles (`struct ca_player` as forward-declared incomplete type holding `std::unique_ptr<PlayerImpl>`). Use `NOLINT` on reinterpret_cast and add `static_assert(sizeof(CHandle)==sizeof(void*))`.
- Version the `.so/.dll` with same `OUTPUT_NAME` but add `SOVERSION` major bump (since ABI now C++ mangled).

---

## 10. Quick Reference: File → C++ Target

| C file | C++ file | Key symbol remapped |
|--------|----------|---------------------|
| `src/utils/ca_alloc.c` | `src/utils/alloc.cpp` | `ca__malloc` → `TrackingResource::do_allocate` |
| `src/utils/ca_arena.c` | `src/utils/arena.cpp` | `ca_arena_alloc` → `Arena::allocate` |
| `src/utils/ca_thread.c` | `src/utils/thread.cpp` | `ca_thread_create` → `std::jthread` factory |
| `src/utils/ca_ring.c` | `src/utils/ring.cpp` | `ca_ring_*` → `SpscRing<float>` |
| `src/utils/ca_cmd.c` | `src/utils/cmd_queue.cpp` | `ca_cmd_queue_*` → `MpscCmdQueue` |
| `src/player/ca_reader.c` | `src/player/reader.cpp` | `ca_reader_open_*` → `FileReader/MemoryReader` |
| `src/player/ca_decode.c` | `src/player/decoder_registry.cpp` | `ca_decode_open` → `DecoderRegistry::open` |
| `src/player/decoders/*` | `src/player/decoders/*.cpp` | `ca_dr_*_vt` → classes `WavDecoder/FlacDecoder/...` |
| `src/player/ca_output.c` | `src/player/output.cpp` | `ca_output_data_callback` → `AudioOutput::callback noexcept` |
| `src/player/ca_player.c` | `src/player/player.cpp` | `struct ca_player` → `class Player::Impl` + `jthread` |
| `src/db/ca_schema.c` | `src/db/schema.cpp` | `ca__schema_sql` → `constexpr std::string_view kSchema` |
| `src/db/ca_write_thread.c` | `src/db/write_thread.cpp` | `ca_write_thread` → `WriterThread` typed queue |
| `src/db/ca_scan.c` | `src/db/scan.cpp` | `ca_sha256_*` → `picosha2::hash256_hex_string(file)` + `filesystem` |
| `src/db/ca_search.c` | `src/db/search.cpp` | `sanitize_fts_query` → `pegtl`/SQLite escaper |
| `src/db/ca_db.c` | `src/db/database.cpp` | `ca_db_*` → `Database::* Expected<T>` |
| `src/engine/ca_queue_logic.c` | `src/engine/queue_logic.cpp` | `ca__qs_*` → `QueueState` methods |
| `src/engine/ca_history_policy.c` | `src/engine/history_policy.cpp` | `ca__should_mark_played_ex` → `HistoryPolicy::should_mark` |
| `src/engine/ca_engine.c` | `src/engine/engine.cpp` | `struct ca_engine` → `class Engine` + `jthread` monitor |

---

## 11. Verification Checklist Before Ship

- [ ] `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build -V` → 37/37 (ported count) plus any new bench
- [ ] `cmake -B build -DCMAKE_CXX_FLAGS="-DCA_DEBUG"` → leak dump 0
- [ ] `clang-tidy --checks='modernize-*,bugprone-*,concurrency-*,cppcoreguidelines-*'` clean (fix or `NOLINT` with rationale)
- [ ] `clang-format --dry-run --Werror src/**/*.{hpp,cpp}` clean (100 col, LLVM base)
- [ ] ASan+UBSan Linux CI passes (`-fsanitize=address,undefined -fno-omit-frame-pointer`)
- [ ] `valgrind --tool=helgrind` on `test_race_*` ports shows no races
- [ ] Real fixtures decode: verify `sample.wav` + `sample.mp3/flac/ogg` PCM frames match golden dumps
- [ ] `ctest -R flush` no `CA_ERR_BUSY` flakes (deterministic virtual clock)
- [ ] `ctest --verbose` on Windows+MinGW+Clang, macOS Clang, Linux GCC all green

---

*Report written without `git add`/`git commit` per brief. File intentionally not tracked. For questions, read cited file_path:line_number before assuming.*
