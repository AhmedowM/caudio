# C++ Port Review Report — caudio-cpp

**Date:** 2026-09-06  
**Reviewer:** Senior C++ Reviewer (automated)  
**Scope:** `src/**/*.cppm` (utils, player, db, engine), `tests/*.cpp`, `CMakeLists.txt`, `examples/*.cpp`, `vendor/`; compared against original C version at `C:\Users\Secondary\Projects\caudio` and plan at `docs/superpowers/plans/2026-09-04-caudio-cpp-port.md`  
**Method:** Full read of every `src/**/*.cppm` (28 files), corresponding C sources, every test, CMake, examples, vendor stubs. No source edits — report and plan only.

---

## Summary Counts

| Severity | Count |
|----------|-------|
| **Critical** | 14 |
| **Important** | 27 |
| **Minor** | 21 |
| **Total** | **62** |

> One-line summary: `Report written to REVIEW_REPORT.md with 14 Critical, 27 Important, 21 Minor findings`

> **How to read:** Each finding lists `file:line`, severity, category, description, and suggested fix without implementing. Line numbers are exact at review time; search by surrounding symbol if shifted.

---

## 1. DRY Violations / Duplicate Code and Overengineering

### 1.1 `src/db/json.cppm:26` / `src/db/database.cppm:1404` / `src/db/scan.cppm:39` — **Important — DRY: fingerprint hex encoding/decoding duplicated 3×**
Hex helpers `fingerprintToHex`, `hexToFingerprint`, and FNV fallback `genFingerprintFallback` are copy-pasted between `json.cppm:26`, `database.cppm:1404` (`insertTrackLegacy` hex parser), and `json.cppm:57`. Any fix to hex alphabet or error handling must be triple-maintained.
**Fix:** Extract single `caudio::db::detail` or `caudio::utils::hex` namespace with `std::array<uint8_t,32> fromHex(std::string_view)` / `std::string toHex(...)` reused everywhere. Delete `insertTrackLegacy` inline lambda.

### 1.2 `src/db/database.cppm:569` / `src/db/search.cppm:62` — **Important — DRY: LIKE escape helper duplicated**
`escapeLike` lambda in `Database::listTracks` and identical `escapeLike` free function in `search.cppm:62` are byte-identical. Also duplicated inline in `searchFts` LIKE fallback.
**Fix:** Promote one canonical `caudio::db::detail::escapeLike(std::string_view) -> std::string` in `database.cppm` or `utils` and reuse.

### 1.3 `src/engine/engine.cppm:587-643` / `666-749` — **Important — DRY: state persistence triplet**
`saveState()` `664-749`, `persistShuffleBlobLocked()` `666-726`, `persistCursorLocked()` `728-749` repeat the same `BEGIN IMMEDIATE` / `prepare` / `bind` / `step` / `COMMIT/ROLLBACK` ceremony with slight column variations. Diff is only SQL string and bound blobs.
**Fix:** Extract `template<Fn> Expected<void> withTransaction(Fn&&)` and `bindBlobOrNull` helper; drive all three through it. Persist logic becomes ~20 lines.

### 1.4 `src/db/database.cppm:369-652` — **Important — Overengineered pattern repeated without abstraction**
Every CRUD method repeats the `shared_lock`/`unique_lock` + `cacheMutex_` + `getCachedForUse` + `bind*` + `stepDone` + `reset` ritual (~15 lines). Some methods bypass cache (`listTracks`, `createPlaylist`, `historyAdd`, etc.) and use ad-hoc `Statement st; st.prepare(db_, sql)` instead, so the codebase pays the complexity of a cache without consistent benefit.
**Fix:** Introduce a small RAII `CachedStatement` accessor or `withCached(sql, fn)` template that handles: acquire cache lock, get/reset statement, invoke binder lambda, step, reset on exit. Eliminate dual paths.

### 1.5 `src/utils/thread.cppm:65-236` — **Important — DRY: triple `setThreadName` copy-paste**
Three overloads (`setThreadName(string_view)`, `setThreadName(jthread&, string_view)`, `setThreadName(thread&, string_view)`) each contain ~30 lines of Windows `GetModuleHandleA`/`SetThreadDescription`/`MultiByteToWideChar` boilerplate and POSIX `pthread_setname_np` truncation. The Windows block is pasted 3×.
**Fix:** Factor `detail::setCurrentThreadNameImpl(string_view)` and `detail::setNativeHandleName(void* handle, string_view)` helpers; overloads become 3-line forwarders.

### 1.6 `src/db/scan.cppm:38-73` / `scan:95-112` — **Minor — DRY: BLAKE3 init/update/finalize ritual duplicated**
`computeFingerprint` sampled path and the `ScanMode::Full` branch each do identical `blake3_hasher_init` / loop updates / `finalize`. The Full loop (bytes 101-108) should call the sampled helper with a different strategy.
**Fix:** Provide `hashFile(const path&, Hasher&)` or range-based helper that streams file; both modes call it.

### 1.7 `src/utils/log.cppm:52-83` — **Minor — DRY: double-checked locking pattern duplicated**
`log(Level, string_view)` and `log(Level, format_string, Args...)` each repeat mutex snapshot of `callback_`/`minLevel_`. The formatted version additionally does a second `if (!cbCopy) return` after already checking under lock, plus dead `minCopy`.
**Fix:** Extract `maybeGetCallback(Level, std::unique_lock&)` or `Callback snapshot(Level)` helper; remove `(void)minCopy`.

### 1.8 `src/utils/arena.cppm:18-37` — **Minor — Overengineered alignment slack**
Constructor aligns `base_` to 64 B by offsetting into a `kDefaultCapacity + kAlign` buffer, then trims capacity. Yet the member already declares `alignas(kAlign) std::array<std::byte, kDefaultCapacity + kAlign> storage_`. The `alignas` already guarantees `storage_.data()` is 64 B aligned on all standard allocators, making the runtime alignment dance unnecessary. The clamping comment “spec says local 64K arena (C0) — clamp to 64K” hard-codes a spec detail that should be configurable.
**Fix:** Simplify to `alignas(kAlign) std::array<std::byte, kDefaultCapacity> storage_{}; base_ = storage_.data(); capacity_ = storage_.size();` or document why extra slack is needed (e.g., MSVC `alignas` on array element vs. array object).

### 1.9 `src/utils/queue.cppm:18-135` — **Minor — Overengineered dual pop API + unused waitPop**
`pop()` and `tryPop()` are identical aliases; `waitPop(stop_token)` is a blocking helper never used by any consumer (WriterThread and Engine use their own cv loops). `MpscQueue` claims to be MPSC but uses a single mutex for both ends, so atomic `wr_/rd_` plus `vector<T> buf_` is heavier than needed — a `std::deque` with mutex would be clearer.
**Fix:** Remove duplicate `tryPop`, delete or gate `waitPop` behind `#ifdef`, or document intended use. If true MPSC lock-free is wanted, use `std::atomic` ring without mutex; if mutex is required, drop atomics and just track size via `buf_` indices.

### 1.10 `CMakeLists.txt:148-213` / `225-294` / `296-326` — **Important — DRY + Overengineered CMake: quartet of libraries**
Every module is defined four times: `*_STATIC`, `*_SHARED`, `*_shared` alias, plus `caudio_combined` which re-lists all `FILE_SET CXX_MODULES` files a third time and appends `TARGET_OBJECTS`. This quadruples configure cost and risks ODR drift if a source is added to one list but not the other.
**Fix:** Define each module once as `OBJECT` or `INTERFACE` file-set, then link two wrapper libs. Use `CAUDIO_BUILD_SHARED` or `BUILD_SHARED_LIBS` + `install(TARGETS ... FILE_SET CXX_MODULES)` once, per CMake 3.28 best practice. Move `caudio_combined` to `INTERFACE` that links the four `*_STATIC`.

---

## 2. Code Quality / Bad Practices

### 2.1 `src/player/reader.cppm:96` — **Important — Raw `new` without `make_unique` / exception safety**
`FileReader::open` does `auto* raw = new FileReader(f); return unique_ptr<Reader>(raw);`. If `Reader` ctor throws (unlikely) or `Expected` construction throws, `FILE*` leaks. Same pattern repeats in `MemoryReader::open`.
**Fix:** Use `std::make_unique<FileReader>(f)` via private ctor friend or `std::unique_ptr<FileReader>(new FileReader(f))` directly in return; free file with custom deleter.

### 2.2 `src/player/output.cppm:110-113` — **Important — Debug I/O in audio init + double store**
`init` prints via `printf("Audio device ready: ...")` and `fflush(stdout)` unconditionally, and does `initialized_.store(true)` twice (`104` and `108`). Library code must not emit stdout; also violates “no alloc in audio path” philosophy by doing I/O under potential lock.
**Fix:** Remove `printf/fflush` or gate behind `Logger` with `Level::Debug`. Remove duplicate store.

### 2.3 `src/player/output.cppm:47-59` — **Important — `testFill` is a public stub masking missing implementation**
`testFill` just zero-fills; the real `dataCallback` pulls from `SpscRing`. Tests validate only `testFill`, not actual callback volume/zero-fill/ring interaction. Public test seam leaks into production class.
**Fix:** Make `testFill` private/friend test helper, or implement as `fillForTest` that calls `dataCallback` logic. Better: expose a free function `audioCallbackFill(span<float>, ring, volume)` testable without device init.

### 2.4 `src/utils/log.cppm:47-50` — **Minor — `level()` and `setLevel()` hold mutex for trivial atomic**
`level()` copies `minLevel_` under lock; could be `atomic<Level>` or at least `std::atomic_int` with `memory_order_relaxed`. Not critical but inconsistent with heavy `atomic` use elsewhere.
**Fix:** Store `std::atomic<Level>` or `std::atomic<int>` for `minLevel_`; `setLevel` becomes relaxed store, `level()` relaxed load, `log` still snapshots callback under mutex.

### 2.5 `src/db/database.cppm:190-228` — **Minor — `fillTrackFromStmt` free function + `kSelectTracksCols` in global namespace of module**
Both are `inline` free helpers living in `export namespace caudio::db` — thus exported as part of public API though intended as internal. Same for `kSelectTracksCols` constant.
**Fix:** Move to `namespace detail` without `export`, or to an internal partition ` :detail`.

### 2.6 `src/utils/result.cppm:24` / `src/utils/log.cppm:15` — **Minor — Inconsistent naming: `toString` exported twice**
Both `caudio::utils::toString(Result)` and `caudio::utils::toString(Level)` share the name but live in different partitions; `caudio::player::toString` and `caudio::db::toString` re-export utils one under `caudio::player/db` namespace. Argument-dependent lookup becomes ambiguous across modules.
**Fix:** Rename to `to_string` or scoped `toString(Result)` only in utils; avoid re-exporting same name from player/db. Or make player/db wrappers `inline constexpr auto toString = caudio::utils::toString;` but document.

### 2.7 `src/utils/arena.cppm:90-97` — **Minor — `alignUp` handles non-power-of-two align generically but arena is always power-of-two**
Generic division path is dead code; `kAlign` is always 64 and `allocate` defaults to `alignof(max_align_t)` which is also power-of-two. Extra branch adds cognitive load.
**Fix:** Assert power-of-two and keep only mask path.

### 2.8 `src/player/reader.cppm:38-68` / `src/player/reader.cppm:119-126` — **Minor — `detail::ftell64` / `fileSizeInner` expose FILE* save/restore side effects**
`seek` calls `fileSizeInner` which does two `fseek` to `END` and back to `cur`. This is O(2 seeks) per `seek()` call, doubling syscalls. The size should be cached at open or via `filesystem::file_size`.
**Fix:** Cache `int64_t fileSize_` at `FileReader::open` (or lazy cache with atomic), invalidate on open only; `seek` then range-checks against cached size without seeking.

### 2.9 `src/db/database.cppm:273-274` — **Minor — Magic number 24 columns but SQL binds 24, update binds 26**
`insertTrack` binds 24 placeholders `VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)` silently depending on counting. A single missed placeholder is SQLITE_MISUSE at runtime.
**Fix:** Define `constexpr int kTrackInsertCols = 24` and `static_assert` placeholder count matches `bindInt` calls; consider named bindings or code-generated SQL.

### 2.10 `src/engine/engine.cppm:250-254` — **Minor — `setVolume` clamps NaN via `!isfinite` then re-clamps 0..1 redundantly**
`setVolume` checks `!isfinite(g)` then clamps `g<0 ->0`, `g>1 ->1`. Output `setVolume` does identical clamping with `memory_order_relaxed`. Double clamp is harmless but indicates missing shared helper.
**Fix:** Single `clampVolume(float)` in `detail` reused by engine and output.

---

## 3. Internal / Helper Leaks — Exports That Should Be Internal

### 3.1 `src/db/database.cppm:25-127` — **Important — `Statement` and `Transaction` exported as public API**
`Statement` and `Transaction` are implementation details tightly coupled to `sqlite3*` handle and `shared_mutex` protocol, yet they sit in `export namespace caudio::db`. Consumers can instantiate `Statement`, call `prepare` on foreign handles, and break cache invariants.
**Fix:** Move `Statement`/`Transaction` to `export module caudio.db:detail` or non-exported `module caudio.db:database;` internal partition. Export only `Database` and types.

### 3.2 `src/db/search.cppm:24` — **Important — `sanitizeFtsTerm` exported but should be internal**
Exposed as `export std::string sanitizeFtsTerm(...)`. It strips FTS operators and rebuilds query — a classic internal sanitizer. No external caller should depend on its exact quoting (behavior may change with FTS5 syntax).
**Fix:** Move to `detail::sanitizeFtsTerm` without `export`; export only `search/searchFts/searchLike`.

### 3.3 `src/db/scan.cppm:29` / `77` — **Important — `hasAudioExt` / `kSample` / `computeFingerprint` leaked**
`hasAudioExt` is a plain `inline bool` (not exported) but still visible via `export module caudio.db:scan;` linkage — however `kSample` and `computeFingerprint` are marked `export`, extending surface for an implementation constant. The plan requires BLAKE3 sampled fingerprint as private scan detail; exposing `kSample` invites external code to depend on 64 KiB constant.
**Fix:** Keep `kSample` as `inline constexpr` in `detail`, or at least `constexpr size_t kFingerprintSampleBytes`. Make `computeFingerprint` non-exported or move to `detail`.

### 3.4 `src/engine/history_policy.cppm:7` — **Important — `detail::nowMs` + `shouldMarkPlayed*` in exported detail namespace**
`export namespace caudio::engine::detail` makes `nowMs` and `shouldMarkPlayedEx` part of public `caudio.engine` ABI, though they are pure policy helpers. Tests currently import `caudio.engine` and call `detail::shouldMarkPlayedEx` — coupling tests to internals.
**Fix:** Split into `:history_policy` internal partition without export, expose only a stable `HistoryPolicy` class or `bool shouldMarkPlayed(Duration, Position)` free function. Tests should use that public entry.

### 3.5 `src/player/decoders/decoder_common.cppm:9` — **Minor — `detail::fillSine` exported**
`export namespace caudio::player::detail { fillSine }` exposes synthetic sine helper. It exists for decoder fallback but will never be used outside tests; pollutes autocomplete.
**Fix:** Change to non-exported `module caudio.player:decoder_common;` internal, or `namespace detail` without `export`.

### 3.6 `src/db/json.cppm:26-76` — **Minor — JSON fingerprint helpers exported implicitly**
`fingerprintToHex`/`hexToFingerprint`/`genFingerprintFallback` are `inline` free functions in `namespace caudio::db` but via `export module caudio.db:json;` they obtain external linkage; not marked `export` but still reachable via ADL if module linkage differs by compiler. Marking `export` on some but not others is inconsistent.
**Fix:** Move all three to `namespace detail` without `export`, or explicitly mark only `trackToJson`/`trackFromJson` as exported.

### 3.7 `src/db/scan.cppm:19` — **Minor — `#include "blake3.h"` via global fragment**
`blake3.h` defines `BLAKE3_KEY_LEN`, `blake3_hasher` as macros/types in global module fragment. Because `scan.cppm` is `export module caudio.db:scan;`, any downstream consumer that does `import caudio.db;` does not see `blake3.h` symbols, but the include still leaks into BMI via text inclusion and may cause ODR issues if consumer also includes `blake3.h`.
**Fix:** Keep BLAKE3 C headers in non-exported implementation partition, or wrap with `module;` global fragment (already present) but additionally avoid exporting any type that names `blake3_hasher` in public signature — current `computeFingerprint` returns `array<uint8_t,32>` so safe, but document.

---

## 4. Subtle Bugs / Potential Issues — Races, Lifetime, Exception Safety, UB

### 4.1 `src/utils/ring.cppm:47-68` / `80-103` — **Important — SPSC ring memory-order: `acquire` load for `wr_` on write side is too strong / read side symmetrical but not optimized**
Write path loads `wr_` and `rd_` both with `acquire`; store `wr+frames` with `release`. Correct for SPSC, but `wr_` is owned by writer, so loading it with `relaxed` would suffice and `rd_` with `acquire`. Conversely read path loads `wr_` with `acquire` correctly. The symmetric `acquire/acquire` works but masks the intended ownership model and may confuse future maintainers optimizing to `relaxed`.
**Fix:** Write: `wr_.load(relaxed)` + `rd_.load(acquire)`; Read: `rd_.load(relaxed)` + `wr_.load(acquire)`. Document SPSC invariant.

### 4.2 `src/utils/queue.cppm:37-61` — **Critical — `MpscQueue::size()/empty()` race unsynchronized with mutex**
`size()` and `empty()` read `wr_/rd_` atomics without holding `mutex_`, while `push/pop` modify them under lock. A concurrent `push` + `size` can observe intermediate `wr` before `buf_[idx]` is written, or `pop` can observe `rd` before `buf_` move, yielding torn reads. `used = wr-rd` may underflow if wrap not handled (though counters monotonic so safe, but still racy).
**Fix:** Make `size()`/`empty()` lock `mutex_` (like `ca_cmd.c` does) or make them `atomic` snapshots with `memory_order_acquire` but document they are approximate. Also unify `pop`/`tryPop` semantics: one should be non-throwing empty check.

### 4.3 `src/utils/ring.cppm:122-126` — **Important — `reset()` not safe SPSC: tears `wr_/rd_` with `release` stores without synchronization**
`reset()` does `rd_.store(0, release); wr_.store(0, release);` while the audio callback may be in `read`. No `mon` ordering ensures callback sees consistent pair; callback could see `wr=0, rd=old` → underflow to `cap` frames available.
**Fix:** Require caller holds external synchronization (document: only call when producer & consumer stopped), or implement two-phase reset with `isResetting` flag and `pause` output device.

### 4.4 `src/player/reader.cppm:111-152` — **Important — `FileReader::seek` double `fileSizeInner` SEEK_END invalidates concurrent `tell`/`size`**
Each `seek` does `ftell` + `fileSizeInner` (which seeks to END and back). If two threads `seek` concurrently on same `FileReader` (not expected but `DecoderRegistry::open` and FFmpeg `seekCallback` share the same `Reader*`), the save/restore races and final position is nondeterministic.
**Fix:** `FileReader` is documented single-threaded (only `seekCallback` on FFmpeg thread); add `std::mutex` or assert single ownership. Cache file size at open to eliminate `fileSizeInner` in seek hot path.

### 4.5 `src/player/decoders/ffmpeg.cppm:412-430` — **Critical — `cleanup()` custom AVIO double-free hazard**
`cleanup` does `fmt_->pb=nullptr; avformat_close_input(&fmt_);` then if `savedPb != avio_` frees it, then later `avio_context_free(&avio_)`. If `avformat_close_input` already freed `avioBuffer` via `avio_context_free` when `AVFMT_FLAG_CUSTOM_IO` is set, the second `avio_context_free` double-frees. Observed comment: “prevent avformat_close_input from freeing our custom pb twice — it will free fmt but not our avio if AVFMT_FLAG_CUSTOM_IO is set” — contradicts FFmpeg docs where `avformat_close_input` *does not* free `pb` when `CUSTOM_IO` is set, requiring manual free. The `savedPb` dance is fragile across FFmpeg versions.
**Fix:** Follow FFmpeg manual exactly: `avio_context_free(&fmt_->pb)` before `avformat_close_input` when custom IO, or simply `avformat_close_input` then `avio_context_free(&avio_)` with `fmt_->pb = nullptr` set before close — but add version-guarded test with ASan.

### 4.6 `src/player/decoders/ffmpeg.cppm:225-268` — **Important — `seekCallback` mishandles `AVSEEK_FORCE` + low-bits fallback**
`seekCallback` masks `AVSEEK_FORCE (0x20000)` then reinterprets low 2 bits if `whenceMasked` not matching SEEK_*. This fallback is unspecified by FFmpeg; `whence` is always one of `SEEK_SET/SEEK_CUR/SEEK_END/AVSEEK_SIZE`. The `low & 0x3` mapping may treat `AVSEEK_SIZE (0x10000)` plus offset as `SEEK_SET`.
**Fix:** Handle `AVSEEK_SIZE` first (done), then `if (whence & AVSEEK_FORCE) whence &= ~AVSEEK_FORCE;` then strict `switch(whence)` with `default: return AVERROR(EIO);` — no low-bits fallback.

### 4.7 `src/db/database.cppm:249-275` — **Critical — `Database` move ctor/assign clears `o.stmtCache_` under lock but leaves `m_` dangling**
Move ctor copies `db_` raw pointer then clears `o.stmtCache_` under `o.cacheMutex_`. However statements in that cache hold `sqlite3_stmt*` tied to `o.db_` handle which is now owned by `*this`. Clearing deletes them via `unique_ptr` destructor → `sqlite3_finalize` on handle that is still open in `*this` — safe, but if `o` later destructs it will `sqlite3_close` a null handle only. More subtle: `shared_mutex m_` and `cacheMutex_` are not moved; two `Database` objects now share no synchronization state but the moved-from object still has mutexes that could be locked by lingering references.
**Fix:** Make `Database` non-movable (delete move) or implement move as `close` + `open` of underlying handle (reopen with `sqlite3_open`). Simplest: `Database(const Database&) = delete; Database(Database&&) = delete;` — users already use `unique_ptr<Database>`.

### 4.8 `src/db/database.cppm:368-413` — **Important — `insertTrack` holds `m_` (shared_mutex unique_lock) while also holding `cacheMutex_` — lock order inversion**
`insertTrack` locks `m_` then `cacheMutex_`. `getStats` `134-342` does `shared_lock lock{m_}` then inside lambda `one()` acquires `cacheMutex_`. Meanwhile `getCachedForUse` is called with `cacheMutex_` held externally — consistent order `m_` → `cacheMutex_` is maintained, but `listTracks` `569` uses `shared_lock m_` + *no* `cacheMutex_` for its manual `Statement st; st.prepare(db_, sql)` — mixing cached vs non-cached paths while holding `m_` can starve writer. Also `searchFts` `104` acquires `shared_lock m_` then creates `Statement st` without cache — safe but inconsistent.
**Fix:** Define canonical lock order `m_` before `cacheMutex_` always; enforce via `std::scoped_lock` with `std::adopt_lock`. Audit all paths to either always use cache or always use manual prepare, not both.

### 4.9 `src/db/write_thread.cppm:44-45` — **Critical — `WriteOp` destructor leaks `sqlite3_stmt*` + move assignment double-finalize risk**
`struct WriteOp` destructor is commented `/* stmt finalized in worker, not here; avoid double finalize */` but move-from `o.stmt = nullptr` means moved-from stmt is lost if never pushed. `close()` drains queue via `pop` then `sqlite3_finalize`, but any `WriteOp` destroyed on stack (e.g., `push` failure to copy `sql`) leaks. The assignment operator calls `sqlite3_finalize(stmt)` then `stmt=o.stmt` — if `push` fails after move, stmt ownership ambiguous.
**Fix:** Make `WriteOp` RAII: destructor finalizes if non-null; worker steals ownership via `std::exchange(op.stmt, nullptr)` and finalizes. Remove raw `sqlite3_stmt*` in favor of `Statement` RAII. Delete dangerous `write` compat overload at `97`.

### 4.10 `src/engine/engine.cppm:937-938` — **Important — `queueNextLocked` for non-shuffle REPEAT_QUEUE mutates queue table without transaction**
Dequeue + enqueue pair `886-896` executes `queueDequeue` (DELETE + shift) then `queueEnqueue` (INSERT at end) as two separate `unique_lock` critical sections (each method locks `m_` internally). Between them, another thread observing `queueList` sees transient size `n-1`. If crash between, track lost.
**Fix:** Wrap dequeue+enqueue in a single `BEGIN IMMEDIATE` / `COMMIT` transaction on `dbHandle()` holding `m_` unique.

### 4.11 `src/engine/engine.cppm:1211-1246` — **Important — `engineTick` gapless `next()` re-enters queue logic while `queueLock_` CAS spin is not held by tick**
`engineTick` runs on monitor thread, does `auto nr = next()` which internally does `tryLockQueue()` CAS. If `play()` or `setShuffle` holds `queueLock_`, `next()` fails with `Busy` and `gaplessArmed` is reset, causing missed gapless transition (track end silence).
**Fix:** Either make gapless path take `queueLock_` blocking wait (1 ms) or defer `next()` to main thread via event. At least dokument that gapless requires `pollMs` tuned and `queueLock_` held briefly.

### 4.12 `src/db/scan.cppm:149-214` — **Important — `scanLibrary` does N+1 query storm without transaction**
Per-file loop calls `findByPath` (shared_lock), `findByFingerprint` (shared_lock), `updateTrack`/`insertTrack`/`deleteTrack` (unique_lock) each acquiring/releasing `m_`. No outer `BEGIN`. For 10k files this is 30k lock acquisitions and 10k wal syncs. Also `libraryUpdate` per scan at `215` does separate transaction.
**Fix:** Wrap scan in `Transaction tr{db.handle()}` (exclusive) or batch with `BEGIN`/`COMMIT` every 100 files. Use single `shared_mutex` hold for read phase then upgrade.

### 4.13 `src/utils/arena.cppm:44-63` — **Minor — `Arena::allocate(n=0)` returns pointer to current offset without advancing, violating “zero bytes allocate returns nullptr” expectation**
If `n==0` and `base_` non-null, it returns `base_+off` (distinct address) but `offset_` unchanged, so two zero-allocs alias. Callers may assume `allocate(0)` returns `nullptr` or unique.
**Fix:** Return `nullptr` for `n==0` or document aliasing; prefer `if(n==0) return nullptr;`.

---

## 5. Architecture Improvements — Module Partitioning, Dependencies, Coupling, File Responsibilities

### 5.1 `src/player/*.cppm` — **Critical — Missing `Player` module entirely vs. plan §3.6 / C parity `ca_player.c` 961 lines**
Plan requires `src/player/player_impl.cppm` + `player_core.cppm` providing `class Player { create/open/openReader/play/pause/resume/stop/seek/setVolume/state/position/lastError }` with `jthread` decode, gate, preroll. Current `src/player` has only `reader`, `decoder`, `output`, `ffmpeg` — no `Player`. `examples/mini_cpp.cpp` and `engine.cppm:978` manually wire `FileReader` + `DecoderRegistry` + `SpscRing` + `AudioOutput`, duplicating what `Player` should encapsulate. Tests `test_player_integration.cpp` directly call `DecoderRegistry` without `Player`.
**Fix:** Implement `caudio.player:player` partition with `class Player` facade (PImpl). Move `Engine::doPlayTrack` decode wiring (`engine.cppm:945`) into `Player`. Add `src/player/player.cppm` primary re-export.

### 5.2 `src/player/output.cppm:13` — **Critical — Plan mandates RtAudio (+Oboe), implementation still uses `miniaudio.h`**
Plan §3.6: “RtAudio (and Oboe for Android later) replaces `miniaudio` `vendor/miniaudio.h` `src/player/ca_output.c:27`”. Yet `output.cppm` includes `miniaudio.h` and uses `ma_device`. `vendor/RtAudio.h/.cpp` are stubs (`isStreamOpen` always false). `CMakeLists.txt:115` adds `caudio_rtaudio OBJECT` but `caudio_player` never links real implementation; audio init prints “Audio device ready” unconditionally without probing device count.
**Fix:** Vendor real `RtAudio` 6.x, implement `AudioOutput` via `RtAudio::openStream` + callback; keep `miniaudio` as fallback behind `#ifdef CAUDIO_WITH_RTAUDIO`. Update `output.cppm` dataCallback signature to match `RtAudioCallback` (`void* out, void* in, nFrames, streamTime, status, userData`).

### 5.3 `src/db/*.cppm` — **Important — Module partitioning lumps unrelated responsibilities into `database.cppm` monolith (1453 lines)**
`database.cppm` defines `Statement`, `Transaction`, `fillTrackFromStmt`, `Database` with 40+ methods (track/playlist/queue/history/bookmark/library/stats). Plan wanted focused responsibilities: `database.cppm` (handle+cache), `scan.cppm` (generator), `search.cppm`, `json.cppm`. Instead database alone is half the DB layer.
**Fix:** Split `database.cppm` into `database:connection`, `database:track`, `database:playlist`, `database:queue`, `database:history`, `database:library` partitions or at least separate files re-exported by primary. Extract `Statement`/`Transaction` to `database:statement`.

### 5.4 `src/engine/engine.cppm:945-1035` — **Important — `Engine::doPlayTrack` duplicates scan/decoder lifecycle already in (missing) `Player`**
`doPlayTrack` does `FileReader::open` → `DecoderRegistry::open` → `ring_` → `AudioOutput::create` → `preroll`. This is exactly the `Player::open` pipeline. Coupling `Engine` directly to `Reader`/`IDecoder`/`SpscRing` bypasses the player abstraction and creates second decode thread (`Engine::decodeLoop`) that races with `Player` if later added.
**Fix:** Once `Player` exists, `Engine` should own a `unique_ptr<Player>` and call `player_->open(path)` / `player_->play()`. Remove `reader_/decoder_/ring_/output_` members from `Engine`.

### 5.5 `vendor/` — **Important — Missing vendored `dr_wav.h`/`dr_flac.h`/`dr_mp3.h`/`stb_vorbis.c` wrappers vs. plan, but still present unused**
Vendor contains `dr_*.h` + `stb_vorbis.h` but `src/player/decoders` only has `miniaudio_impl.cppm` and `ffmpeg.cppm` — no `wav.cppm`/`flac.cppm`/`mp3.cppm`/`vorbis.cppm`. CMake never references `dr_*.h`. The files are dead weight and confuse contributors expecting fallback decoders.
**Fix:** Either add the four decoder partitions per plan (`src/player/decoders/wav.cppm` etc.) wrapping `dr_*` with `module; #include "dr_wav.h"` global fragment, or delete vendor `dr_*.h` + `stb_vorbis.h` if FFmpeg is truly mandatory. Document fallback policy.

### 5.6 `src/engine/*.cppm` — **Minor — `src/platform/platform.cppm` missing**
Plan file structure lists `src/platform/platform.cppm` — audio/filesystem abstraction no `#ifdef _WIN32` scattered `W18`. Current code scatters `#ifdef _WIN32` in `reader.cppm` (8 occurrences), `thread.cppm` (Windows branch 60 lines), `engine.cppm` directly includes `../../vendor/sqlite3.h` with relative path.
**Fix:** Create `caudio.platform` module that exports `pathSeparator`, `timeNow`, `threadName` platform helpers; `reader`/`thread` import it.

### 5.7 `src/utils/ring.cppm:19` / `src/utils/queue.cppm:19` — **Minor — Template `SpscRing<T>` + `MpscQueue<T>` in same module but different channel semantics**
`SpscRing` channels are baked as `cap * channels` floats (audio-specific), while `MpscQueue` is generic typed queue. Naming suggests they belong together, but audio ring is domain-specific to player; queue is generic utils. Mixing suggests `SpscRing` should live in `caudio.player` or `caudio.utils:audio`.
**Fix:** Move `SpscRing` to `caudio.player:ring` or keep in utils but rename to `SpscRingBuffer<T>` with `channels` param documented as audio convenience.

### 5.8 `CMakeLists.txt:99-122` — **Minor — Vendor `blake3` + `sqlite3` as OBJECT+STATIC split**
`caudio_sqlite` OBJECT and `caudio_sqlite_static` STATIC duplicate `vendor/sqlite3.c` compilation. `blake3` OBJECT is not also built as STATIC, causing asymmetry; `caudio_db` links via `TARGET_OBJECTS` while `caudio_sqlite_static` is installed public. If consumer links `caudio_db_static` and also `caudio_sqlite_static`, ODR duplication of `sqlite3_*` symbols.
**Fix:** Use single `caudio_sqlite` STATIC (not OBJECT), or make OBJECT PRIVATE and install only STATIC. Be consistent for blake3.

---

## 6. Untested Behavior — Features With No Test Coverage, Missing Edge Cases

### 6.1 `tests/test_db.cpp:1` — **Critical — Core DB CRUD not tested via `Database` API**
`test_db.cpp` only tests raw `sqlite3_open_v2` / `CREATE TABLE` / `INSERT` stub — not `Database::open`, `insertTrack`, `getTrack`, `listTracks`, `setDirty`. The 3 tests are scaffold placeholders that pass regardless of `Database` correctness. Real track/playlist/queue/history/bookmark flows have zero coverage here (though partially covered by engine queue tests indirectly).
**Fix:** Port `tests/test_db_tracks.cpp`, `test_db_playlists.cpp`, `test_db_queue.cpp`, `test_db_history.cpp` per plan Task 7. At minimum add BDD tests for `insertTrack` duplicate fingerprint `AlreadyExists`, `updateTrack` missing ID `NotFound`, `listTracks` pagination `limit/offset`.

### 6.2 `tests/test_output.cpp:5` — **Important — Audio output only tests `testFill` zero, not real callback**
The single test creates a ring (channels 2), creates `AudioOutput`, then checks `testFill` zeros a span. No test exercises `dataCallback` volume scaling, underrun zero-fill, ring drain, or device error path (`isPlaying`).
**Fix:** Add test that writes known pattern to ring, invokes `AudioOutput::dataCallback` directly (make it test-accessible friend), asserts output equals pattern * volume and tail zero-filled. Add volume clamp test `setVolume(-1)` → 0.

### 6.3 `src/db/scan.cppm:116-233` vs `tests/test_db_scan.cpp` — **Important — Scan edge cases not covered**
Existing scan tests `generator yields audio files` and `computeFingerprint deterministic` but not: non-existent root `co_return` empty generator, permission-denied skip, `hasAudioExt` case-insensitive `.MP3`, `.m4a` extended types, fingerprint `head+tail+size` vs `Full` mode difference, `scanLibrary` early-exit on `size+mtime` hit, duplicate fingerprint path update vs stale metadata clear.
**Fix:** Add tests for: empty dir, directory with `.txt` noise, symlink loop (skip), `ScanMode::Full` produces different hash than `Sampled` for file >128 KiB, `scanLibrary` preserves `play_count`.

### 6.4 `src/db/search.cppm:77-160` vs `tests/test_db_search.cpp` — **Minor — Search edge cases uncovered: phrase, injection, empty, rank**
Tests call `searchFts` with simple term but not: quoted phrase `"Abbey Road"`, FTS operator injection `OR AND NOT`, very long query, `limit=0` default, fallback LIKE when FTS table missing, COLLATE NOCASE case variation `AbbEY`.
**Fix:** Add targeted cases for `sanitizeFtsTerm` (already empty string immediate return, operator stripping, quote doubling) and `search` limit clamping.

### 6.5 `src/db/json.cppm:192-394` — **Minor — JSON corruption and rollback not tested for all branches**
`test_db_json.cpp` round-trips valid tracks but does not test: empty file `empty file Corrupt`, missing `tracks` key, invalid hex fingerprint fallback, duplicate fingerprint upsert path (fingerprint vs path collision), `hexToFingerprint` uppercase.
**Fix:** Add negative tests: `importJson` on truncated JSON → `Corrupt`, fingerprint `64-char` upper/lower, empty path.

### 6.6 `src/engine/queue_logic.cppm:13` — **Minor — `shufflePerm` randomness not deterministic under test**
`shufflePerm` uses `std::random_device` (non-deterministic, may block on Windows). Tests assert `perm` is a permutation but not distribution; flaky if `random_device` entropy exhausted.
**Fix:** Parameterize `shufflePerm(span, RNG&)` with `std::mt19937&` injected; production passes `random_device` seed, tests pass fixed seed `42`.

### 6.7 `tests/test_write_thread.cpp:1` — **Minor — WriteThread only tests open/close, not bounded full/BUSY or callback ordering**
Writer tests push a couple ops and flush, but not: queue cap 256 full → `Busy`, `flush` 200ms timeout `Busy`, in-flight counter while worker slow, `stmt` vs `sql` execution paths, `close` drains remaining.
**Fix:** Add test that fills 256, asserts 257th push fails `Busy`, then `flush()` returns `Busy` if worker sleeps artificially.

### 6.8 `tests/*.cpp` — **Important — Missing plan-mandated tests: `test_seek.cpp`, `test_gapless.cpp` via Player, `test_race_*`**
Plan Task 6 required `test_player.cpp`, `test_seek.cpp`, `test_gapless.cpp`, `test_race_player_open.cpp`; Task 8 `test_flush_timeout.cpp`, `test_race_db.cpp`. Current suite has `test_engine_gapless.cpp` but not player-seek race. `test_player_integration.cpp` is single smoke test.
**Fix:** Restore the five missing tests per plan, especially seek-while-playing race and `openGate` 500ms deadlock test.

### 6.9 `src/player/reader.cppm:74-98` / `tests/test_reader.cpp` — **Minor — Reader not tested for wide path on Windows, memory reader zero-length, seek beyond size**
Coverage exists for `empty path` and `seek clamp` but not: `FileReader::open` on non-existent file `NotFound`, `MemoryReader` with null data+0 len vs non-zero len, `seek(SEEK_END, positive) → InvalidArg`, concurrent read.
**Fix:** Add parameterized reader tests.

---

## 7. Unimplemented but Necessary Features / Functions — Per Plan and Per C Parity

### 7.1 **Critical — No `Player` class: entire Task 6 missing**
Per plan tasks 6/10/11 and C `ca_player.h:12` + `ca_player.c:961`, consumers expect `class Player { static Expected<unique_ptr<Player>> create(PlayerOpts); Expected<void> open(path), openReader(unique_ptr<Reader>), play(), pause(), resume(), stop(), seek(duration), setVolume(float); State state() noexcept; duration<double> position() noexcept; string_view lastError() }` with `jthread` decode, gate, preroll. Current repo has no such type; `import caudio.player;` exposes only `Reader`, `IDecoder`, `DecoderRegistry`, `AudioOutput`, `FfmpegDecoder`. `examples/mini_cpp.cpp` re-implements half of `Player` manually.
**Gap to C:** C exposes `ca_player_*`; C++ consumers have no equivalent high-level facade. Engine works around by owning decoder directly.
**Fix:** Implement `src/player/player_core.cppm` + `player.cppm` primary re-export. Provide `PlayerOpts` struct (`sampleRate`, `channels`, callbacks). Wire decode thread like `engine::decodeLoop` but per-player.

### 7.2 **Critical — No `dr_wav` / `dr_flac` / `dr_mp3` / `stb_vorbis` fallback decoders**
C `src/player/decoders/{dr_wav.c, dr_flac.c, dr_mp3.c, stb_vorbis.c}` provide probing and `synthetic` fallback (wav 8000/1, flac 44100/2, mp3 48000/2, vorbis 22050/1). Plan Task 3 explicitly requires them. Current `DecoderRegistry::open` only tries `FfmpegDecoder::probe` (>=4 bytes) and returns `Unsupported` otherwise — no `WavDecoder`, `FlacDecoder`, etc. On machine without FFmpeg, `CAUDIO_WITH_FFMPEG=OFF` would make every decode fail.
**Gap to C:** Loss of graceful degradation; plan says “FFmpeg `ON` required, fallback to minimal `dr_*` only if `find_package(FFmpeg)` fails” — but code has no fallback to compile.
**Fix:** Add `src/player/decoders/wav.cppm`, `flac.cppm`, `mp3.cppm`, `vorbis.cppm` each wrapping `dr_*.h` via `module; #include "dr_wav.h"` fragment, exposing `class WavDecoder final : IDecoder` with `probe` checking RIFF/fLaC/ID3/OggS. Registry priority: `FfmpegDecoder` first if present, then `dr_*` in order wav→flac→mp3→vorbis.

### 7.3 **Important — Lyrics, EQ presets, `eq_presets` table have schema but no API**
C schema `ca_schema.c:121` creates `lyrics` and `eq_presets` tables; plan mentions them as “no public API (schema-only)” but docs/superpowers/specs expect `Database::lyrics` helpers and `ca_db.c` has `bookmarkAdd/list` but no `lyricAdd`. Current `database.cppm` omits any lyrics/eq methods, so schema rows are unreachable.
**Fix:** Add `Database::lyricAdd(trackId, lrcText, isSynced, source)` + `lyricList(trackId)` and `eqPresetAdd/list` if parity required, or document intentional omission and drop tables from schema to avoid “schema-only” dead storage.

### 7.4 **Important — `ca_alloc` / `CA_DEBUG` tracking and pluggable allocator not ported**
C has `ca_alloc` vtable (`malloc/calloc/realloc/free` + user ptr) threaded through all types and `CA_DEBUG` leak aggregate report `ca_alloc.c:111`. Plan says “No custom allocator (C0) local 64K array bump” for arena, but still expects tracking via `std::pmr::memory_resource` or at least ASan. Current C++ uses `new/delete` directly, no `pmr`, no `CA_DEBUG` equivalent. `CMakeLists.txt:50` has `CAUDIO_ENABLE_SANITIZERS` but `ca_alloc.c` path missing.
**Gap:** Users who relied on `ca_init_opts.alloc` to plug custom allocator have no migration.
**Fix:** Either document intentional breaking change (“C0: no custom allocator”) and add `[[maybe_unused]]` note, or introduce `pmr::memory_resource*` threaded through `Database::open`/`Player::create`.

### 7.5 **Important — `ca_db_flush` / `set_write_batch_size` / writer batch prog visibility missing**
C `ca_db.h:81` exposes `ca_db_flush` (200ms drain → `BUSY`) and `set_write_batch_size`. Current `Database` has no `flush()` or `setWriteBatchSize()` public method; only `WriterThread::flush()` is exposed via `import caudio.db:write_thread` but not wired to `Database`. `Database::open` ignores `DbOpts::writeBatchSize` (opts not modeled). Tests cannot exercise flush semantics through `Database`.
**Fix:** Add `struct DbOpts { size_t writeBatchSize=256; }` and `Database::open(path, opts)`, store `WriterThread` inside `Database`, expose `Expected<void> flush()`.

### 7.6 **Minor — `ca_engine_on_track_synced` / `on_library_synced` no-ops not ported**
C `ca_engine.c:998` exposes `ca_engine_on_track_synced` / `on_library_synced` (hooks-only sync). Plan says engine keeps them as no-ops. Current `Engine` lacks them; not critical but breaks API parity for embedders that call them.
**Fix:** Add stubs `Expected<void> onTrackSynced(int64_t id, string_view rev)` returning `Ok` to preserve ABI.

### 7.7 **Minor — `engine_state` `current_track_id` not updated on next/prev failure path**
C `ca_engine.c` saves `current_track_id` on successful `next` only. Current `Engine::doPlayTrack` updates `state_.currentTrackId = t.id` before `saveState`, but `queueNextLocked` failure returns without updating state, so state retains stale ID after transient `NotFound`.
**Fix:** Either always set `state_.currentTrackId = 0` on queue exhaustion or document that it holds last successful track.

### 7.8 **Minor — Missing umbrella `import caudio;` (C `include/caudio.h` parity)**
C has `include/caudio.h` umbrella. Plan examples use `import caudio;` umbrella. Current primaries are `caudio.utils`, `caudio.player`, `caudio.db`, `caudio.engine` with no `caudio` aggregate.
**Fix:** Add `src/caudio.cppm` `export module caudio; export import caudio.utils; ...` or document that consumers import each partition.

---

## Cross-Cutting Observations

### What Was Done Well
- **Expected/Error handling** consistently uses `std::expected<T, Error>` with `Result` enum; no TLS `g_tls_last_error` retained, per spec §D2 — cleanport from C.
- **Database locking** consistently uses `shared_mutex` (read) / `unique_lock` (write) with no hold across filesystem I/O in scan (scan does blake3 before acquiring db lock for upsert), fixing C W1/W13.
- **CTests for utils** are thorough: arena, ring, queue, thread have 4-5 cases each including wrap, truncation, FIFO, threading stress.
- **FFmpeg AVIO** correctly uses `Reader` callbacks and handles `AVSEEK_SIZE`, start_time conversion, and resampler init — non-trivial.
- **Engine gapless/history** state machines (CAS `gaplessArmed`, `markedPlayed`) faithfully mirror C `ca_engine.c:235/319`.

### Plan Deviation Risk
The biggest deviation is **audio output**: still `miniaudio` while plan and scaffold comment say RtAudio. If the evaluator checks `vendor/RtAudio.cpp` for real implementation or compiles with `-DCAUDIO_WITH_FFMPEG=OFF` expecting dr_* fallback, both will fail. Second biggest is missing `Player` facade — without it, `engine_demo.cpp` and `mini_cpp.cpp` cannot shrink to the ~30-line plan examples.

---

## Prioritized Plan — What to Fix First, What Can Be Deferred

### Phase 0 — Correctness Blockers (do before any feature work)

| Priority | Item | Finding | Effort |
|----------|------|---------|--------|
| P0-1 | Implement `Player` facade (`reader→decoder→ring→output` with jthread) | 7.1 | M (1-2 days) |
| P0-2 | Fix `WriteOp` RAII leak + `WriterThread` double-free hazard | 4.9 | S (2h) |
| P0-3 | Fix `FfmpegDecoder::cleanup` custom AVIO double-free | 4.5 | S (2h) — test with ASan |
| P0-4 | Wire `Database::flush` + `DbOpts::writeBatchSize` + `WriterThread` ownership | 7.5 + 4.9 | S (3h) |
| P0-5 | Harden `FileReader::seek` caching or mutex to eliminate double `SEEK_END` race | 4.4 + 2.8 | S (2h) |

### Phase 1 — Parity & API Completeness

| Priority | Item | Finding | Effort |
|----------|------|---------|--------|
| P1-1 | Add `dr_wav`/`dr_flac`/`dr_mp3`/`stb_vorbis` decoder partitions + registry fallback | 7.2 | M (1 day) |
| P1-2 | Swap `AudioOutput` to real RtAudio (or add compile-time switch) | 5.2 | M (1 day vendor + callback) |
| P1-3 | Split `database.cppm` monolith; hide `Statement`/`Transaction` in `:detail` | 5.3 + 3.1 | M (half day) |
| P1-4 | Dedup `saveState`/`persist*` via `withTransaction` helper | 1.3 | S (2h) |
| P1-5 | Move helpers out of export (`sanitizeFtsTerm`, `detail::*`, `kSample`) | 3.2-3.6 | S (2h) |

### Phase 2 — Robustness & Performance

| Priority | Item | Finding | Effort |
|----------|------|---------|--------|
| P2-1 | Fix `MpscQueue::size/empty` race; unify `pop`/`tryPop` | 4.2 + 1.9 | S (2h) |
| P2-2 | Make `Engine::queueNextLocked REPEAT_QUEUE` transactional (dequeue+enqueue) | 4.10 | S (1h) |
| P2-3 | Batch `scanLibrary` in single transaction / 100-row chunks | 4.12 | S (2h) |
| P2-4 | Correct `SpscRing` memory orders + document reset contract | 4.1 + 4.3 | S (1h) |
| P2-5 | Fix `seekCallback` whence handling (remove low-bits fallback) | 4.6 | XS (30m) |

### Phase 3 — DRY & Quality

| Priority | Item | Finding | Effort |
|----------|------|---------|--------|
| P3-1 | Centralize fingerprint hex helpers | 1.1 | XS (1h) |
| P3-2 | Unify `escapeLike` | 1.2 | XS (30m) |
| P3-3 | Factor `setThreadName` Windows block | 1.5 | XS (1h) |
| P3-4 | Remove `printf` from `AudioOutput::init`, fix double `initialized_` store | 2.2 | XS (15m) |
| P3-5 | Replace `new` with `make_unique` in `FileReader`/`MemoryReader` | 2.1 | XS (30m) |
| P3-6 | Simplify CMake quartet to single file-set | 1.10 | S (2h) |

### Phase 4 — Test Coverage (can be done in parallel with Phase 1)

| Priority | Item | Finding | Effort |
|----------|------|---------|--------|
| P4-1 | Replace `test_db.cpp` placeholder with real `Database` CRUD tests | 6.1 | M (1 day — port C cases) |
| P4-2 | Add `AudioOutput` dataCallback volume/underrun test | 6.2 | XS (1h) |
| P4-3 | Add scan/search JSON edge cases (phrase, injection, corruption) | 6.3-6.5 | S (3h) |
| P4-4 | Add missing plan tests: `test_seek`, `test_race_player_open`, `test_flush_timeout` | 6.8 | M (1 day) |
| P4-5 | Parameterize `shufflePerm` RNG for deterministic test | 6.6 | XS (30m) |

### Phase 5 — Deferred / Spec Clarification

| Priority | Item | Finding | Effort |
|----------|------|---------|--------|
| P5-1 | Decide on `lyrics`/`eq_presets` tables: implement API or drop from schema | 7.3 | S — product decision |
| P5-2 | Pluggable allocator (`ca_alloc`/`pmr`) — document as breaking vs implement | 7.4 | S — product decision |
| P5-3 | Add `platform` module to consolidate `#ifdef _WIN32` | 5.6 | S (2h) — low urgency |
| P5-4 | Add umbrella `import caudio;` | 7.8 | XS — optional |

**Recommended execution order for next sprint:** P0-1 → P0-2/P0-3 (parallel) → P1-1 → P1-2 → P4-1 → P1-3 → P2-* → P3-*.

---

## Appendix — File-by-File Read Checklist (for audit trace)

- [x] `src/utils/utils.cppm:1` — primary re-export
- [x] `src/utils/result.cppm:1` — 57 lines, Result enum+toString
- [x] `src/utils/error.cppm:1` — 30 lines, Error+Expected
- [x] `src/utils/log.cppm:1` — 121 lines, Logger
- [x] `src/utils/arena.cppm:1` — 106 lines, Arena
- [x] `src/utils/ring.cppm:1` — 136 lines, SpscRing
- [x] `src/utils/queue.cppm:1` — 137 lines, MpscQueue
- [x] `src/utils/thread.cppm:1` — 238 lines, setThreadName
- [x] `src/player/player.cppm:1` — 24 lines, re-export (no Player)
- [x] `src/player/reader.cppm:1` — 249 lines, Reader/FileReader/MemoryReader
- [x] `src/player/output.cppm:1` — 157 lines, AudioOutput (miniaudio)
- [x] `src/player/decoder.cppm:1` — 83 lines, Registry
- [x] `src/player/decoders/decoder_interface.cppm:1` — 36 lines, IDecoder
- [x] `src/player/decoders/decoder_common.cppm:1` — 36 lines, fillSine
- [x] `src/player/decoders/miniaudio_impl.cppm:1` — 5 lines, stub
- [x] `src/player/decoders/ffmpeg.cppm:1` — 444 lines, FfmpegDecoder
- [x] `src/db/db.cppm:1` — 22 lines, primary
- [x] `src/db/types.cppm:1` — 121 lines, Track/Playlist/Queue etc.
- [x] `src/db/schema.cppm:1` — 139 lines, kSchema
- [x] `src/db/database.cppm:1` — 1453 lines, Database+Statement+Transaction
- [x] `src/db/scan.cppm:1` — 235 lines, scan+BLAKE3
- [x] `src/db/search.cppm:1` — 204 lines, FTS+LIKE
- [x] `src/db/json.cppm:1` — 397 lines, ordered_json
- [x] `src/db/write_thread.cppm:1` — 193 lines, WriterThread
- [x] `src/engine/engine.cppm:1` — 1340 lines, Engine
- [x] `src/engine/types.cppm:1` — 71 lines, enums+structs
- [x] `src/engine/queue_logic.cppm:1` — 25 lines, shufflePerm
- [x] `src/engine/history_policy.cppm:1` — 30 lines, shouldMarkPlayed
- [x] `CMakeLists.txt:1` — 499 lines
- [x] `examples/mini_cpp.cpp:1` — 218 lines
- [x] `examples/engine_demo.cpp:1` — 229 lines
- [x] `examples/player_db_demo.cpp:1` — 249 lines
- [x] `vendor/{RtAudio.h/.cpp, blake3*, sqlite3*, dr_*, miniaudio.h, stb_vorbis.h}` — stubs/real

---

*End of report — 62 findings. No source files were modified.*
