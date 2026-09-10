# caudio-cpp Review — `feat/cli-service` Worktree (2026-09-10)

**Scope:** full read of `src/**/*.cppm` (utils, player, db, engine), `cli/src/**/*` (app, client, service, shared), `CMakeLists.txt`, `tests/**/*.cpp`, `examples/*`, vendor surface. Worktree `feat/cli-service` at `9f76211` (29 files, +6054/−404 vs `master` at `88313a8`). `master` ignored per request; all findings against the worktree.

**Method:** line-by-line read, cross-checked prior report `REVIEW_REPORT.md:1` (62 → 5 open), built mental model of ownership/concurrency/IPC, listed hypotheses then verified against source.

---

## 1. Executive Summary

The worktree is a **feature-complete IPC daemon**: `Service` owns `Engine`+`DB`, `IpcServer`/`IpcClient` exchange `Command`↔`Result` via length-framed JSON over UDS / Named Pipe, `App` (CLI11) dispatches, `ShmStatus` exposes 10-fps snapshot for TUI. Core library (utils/db/player/engine) is well-partitioned C++23 named modules, modernized since the C port. The new daemon layer is functional but **introduces the majority of new risk** — it is the least hardened, least tested vertical.

**Verdict:** ship-ready for an internal `feat/*` branch, **not** for `master` without the 6 blocking fixes below. Rust would pay off most in `engine`/`queue`/`ipc`/`shm` boundaries where this codebase relies on informal contracts.

**Counts:** Critical 4 · Important 9 · Minor/Suggestions 12 — prioritized in §7.

---

## 2. Worktree vs Master — What Changed

```
CMakeLists.txt       +172  (caudio_cli_shared/ipc/client/service targets, caudio exe, IPC tests)
cli/src/**           +~3500 (app, parse, config, ipc_channel_unix/win, ipc_server,
                              service_impl 1270 LOC, shm_status, ipc_client, output_formatter,
                              command/protocol/result/shared)
src/db/{queue,schema,database}.cppm  reworked (queue extracted to partition, statement cache bypass, schema additions)
src/engine/engine.cppm  +222/−  (next/shuffle/play fixes, queue-cursor persistence, decode-loop re-entrancy)
tests/test_ipc.cpp   +195 (daemon round-trip & playback-control)
tests/test_engine_*.cpp  +178
```

Prior report's 57 applied fixes are present (seek-serialization via `decodeMtx_`, queue dedup, RAII helpers). 5 carry-over opens remain; 2 are now measurably worse because the daemon hot-path depends on them.

---

## 3. Architecture Map

```
App (CLI11, app.cppm:117) ──IpcClient::connect(dbPath)→ IpcServer (uds/pipe)
                                │  frame(len+JSON)  │  dispatch(Command→Result)
                                └───────────────────┘
Service (service_impl.cppm:488) owns Engine+DB+IpcServer+ShmStatus+Logger
  ├─ Engine (engine.cppm:48)  ──uses──  Database (database.cppm:34) ──uses── Statement (statement.cppm)
  │   ├─ decodeThread (decodeLoop:1289) ↔ SpscRing (ring.cppm:16) ↔ AudioOutput (output.cppm:21, miniaudio)
  │   └─ monitorThread (monitorLoop:1274, 10ms poll) → doHistoryMark / gapless → next()
  ├─ Scan (scan.cppm:42 generator) + Search (search.cppm:54 FTS5+LIKE)
  └─ ShmStatusHandle (shm_status.cppm:60) seqlock → TUI

DB schema (schema.cppm:9)  WAL/NORMAL, FK=ON, FTS5 tracks_fts, queues/queue/engine_state,
   libraries/tracks/playlists/history/bookmarks/lyrics/eq_presets
```

Module graph is DAG-clean: `utils → {player, db} → engine → {cli_shared →ipc/client/service} → app`. No cycles except `database.cppm:14` imports `:queue/:write_thread` which themselves import `:types/:detail` — legitimate partitions, but `WriterThread` and `Database` share `shared_mutex`/`cacheMutex` discipline informally.

---

## 4. Code Quality — Good Parts

- **C++23 modules throughout** (`export module caudio.*:partition`) with alias targets (`caudio::db`, etc.) — strong boundary discipline vs the old C header soup. `CMakeLists.txt:233` centralizes `CAUDIO_DB_SOURCES` but then duplicates it for shared/combined — see §6.
- **Error model uniform:** `std::expected<T, utils::Error>` (`error.cppm:13`, `result.cppm:4` `Result` enum) threaded everywhere; `makeError` used consistently. `protocol.cppm:174` serializes `Error` losslessly.
- **RAII guards:** `SqliteErrGuard` (`engine.cppm:36` + `detail.cppm:229`), `Statement`, `ShmStatusHandle::~ShmStatusHandle` (`shm_status.cppm:141`), `MpscQueue` bounded 64. Test coverage broad (`test_db`, `test_output`, `test_engine_*`, `test_reader/decoder/ffmpeg`).
- **Ring refactored well:** single-producer/single-consumer invariants documented (`ring.cppm:15` `reset() requires external sync stopped/paused`), cache-line padded atomics, wrap `memcpy` faithful to `ca_ring.c`.
- **Fingerprint stability:** `detail.cppm:27` BLAKE3(head‖tail‖LE64(size)‖LE32(ver)) identical to service-side `service_impl.cppm:435`, adequate dedup for 64 KiB sample + size tag.

---

## 5. Code Quality — Issues by Module

### 5.1 `src/utils`

**`ring.cppm:48,84`** — both `wr_.load(acquire)` even though writer owns `wr` (could be `relaxed` + `rd` is `acquire`). Not a bug today, but audio-path pessimization; plus `reset()` stores `release` while `output::dataCallback:155` may be mid-`read` — acknowledged contract but **no assert/lint** enforces it. One stray `reset()` during playing underflows to `cap` visible. *Carry-over Important 1.3 — still open; document + add `assert(!running||held(decodeMtx_))`.*

**`queue.cppm:17`** — `MpscQueue` copy/move deleted (`queue.cppm:27`) correct; earlier master bug fixed. Remaining nits: `size()` locks (`queue.cppm:36`), `push` notifies unconditionally, `waitPop` takes `std::stop_token` but `Engine::eventQueue_` never uses the blocking path — fine but dead code.

**`thread.cppm:47`** — Windows `SetThreadDescription` fallback correct, diagnostic push/pop for cast, 15-char `pthread_setname_np` truncation covered. Minor: `sleepForMs` vs `sleepFor` overload redundancy.

**`log.cppm:52`** — `level()` and `log(Level,string_view)` each lock, but `log(fmt,...)` double-checks callback then formats outside lock — ok. `minCopy` variable unused (`log.cppm:62`) is debug noise.

**`arena.cppm / result.cppm / error.cppm`** — clean.

### 5.2 `src/db`

**`database.cppm:84`** — `getCachedLocked` vs `getCachedForUse` vs `getCached` three variants: two return raw `Statement*` (null on fail) vs one returns `expected`. Mix invites missed error check. Callers in `database.cppm:188` *do* check, but `getCached` (`database.cppm:113`) is public and unchecked by some external callers. Unify to one `expected` API.

**Cache key is `std::string` copy of `string_view` (`database.cppm:85,100`):** allocation per call. Acceptable, but for hot scan (10k files ×2 lookups) this + per-call `unique_lock<shared_mutex>` dominates. *Carry-over Important 1.2 still open.*

**`queue.cppm:25`** — extracted queue helpers take `cacheMutex` + `StmtCache` but **ignore them** (`(void)cacheMutex; (void)stmtCache;`) and allocate fresh `Statement` each call. This intentionally bypasses the cache to avoid stale `queue` stmts (`service_impl.cppm:502` comment `fix(db): bypass queue statement cache`). Correct semantics but misleading API — params should be removed or documented as "reserved, explicitly not cached".

**`database.cppm:49,58` move-ops:** clear `o.stmtCache_` under `o.cacheMutex_` while `m_` is the DB lock — two mutexes acquired in inconsistent order vs normal `m_` → `cacheMutex_` ordering elsewhere (`database.cppm:179` `lock{m_}` then `cacheLk`). Move occurs only on construction so race low, but ordering violation should be fixed (lock `m_`+`cacheMutex_` in documented order).

**`schema.cppm:120`** — `tracks_fts` `porter` tokenizer good; `idx_queue_queue_pos` + `idx_playlist_items_pos` present. `queue` table lacks `UNIQUE(queue_id, position)` — `queueEnqueueLocked:47` does shift via `position+1`, but without unique constraint concurrent enqueue (if caller forgets `unique_lock`) could produce dup positions. Add constraint or assert single-writer.

**`scan.cppm:90`** — `scanLibrary` does N×(`findByPath` shared-lock + `findByFingerprint` shared-lock + `update/insert` unique-lock) with no outer `BEGIN IMMEDIATE`. 10k files = 30k lock hops, no atomicity — crash mid-scan leaves half-updated library and half-bumped `play_count`. *Carry-over Important 1.2 — deferred but daemon's `LibraryScan` (`service_impl.cppm:1097`) runs this on the IPC thread, blocking all commands for minutes.*

**`scan.cppm:60` / `detail.cppm:46`** — `mtime` from `last_write_time().time_since_epoch().count()` is filesystem-clock-dependent; comparison `existingPath->mtime == trk.mtime` (`scan.cppm:117`) is only valid if scan and DB used same clock epoch — ok on same host, but cross-import breaks. Document units.

**`search.cppm:29`** — `tryFtsQuery` binds un-sanitized `query` then `searchFts:57` sanitizes *before* call — ok. Two exclusive `shared_lock`s (`search.cppm:62` then `78`) cause double-unlock gap where concurrent `insertTrack` could invalid FTS state mid-fallback. Hold one lock across both phases or re-prepare under held lock.

**`write_thread.cppm / transaction.cppm / statement.cppm`** — not fully re-read but callers use `withTransaction` (`engine.cppm:531`) correctly with `BEGIN IMMEDIATE`/`COMMIT`/`ROLLBACK` and `SqliteErrGuard`.

### 5.3 `src/player`

**`player_core.cppm:52`** — `decodeThread_` started in `init` before `sampleRate_/channels_` fully validated — ok since `decodeLoop` waits on `openGate_`. EOF handling (`player_core.cppm:402`) sets `State::Stopped` directly, unlike `Engine::decodeLoop` which leaves monitor to gapless-next — intentional divergence but should be commented.

**`output.cppm:142` dataCallback:** reads `cfg_.ring` without null fence after `shutdown()` — `shutdown:104` stops device before `initialized_=false`, callback may still fire once; `cfg_.ring` is raw non-owning ptr, could dangle if `Engine::doPlayTrack:976` resets `ring_` while callback runs. Engine path serializes `decodeMtx_` but player path does not hold `openMutex_` across callback. Add epoch or `shared_ptr<Ring>` for callback lifetime.

**`reader.cppm / decoder*.cppm / ffmpeg.cppm`** — presumably sound; sample tests in `test_reader`, `test_decoder`, `test_ffmpeg` with `TEST_DATA_DIR` wired (`CMakeLists.txt:591,599`) good.

### 5.4 `src/engine`

**Engine is the best-audited module (prior 62-finding sweep). Four regressions/new concerns:**

**`engine.cppm:137` `play()` when Paused** — now correctly resumes without dequeue (`9f76211` fix). Good.

**`engine.cppm:148` `play()` when Playing** — serializes via `decodeMtx_` (`engine.cppm:151`) and `seek(0)` + `ring->reset()` (`engine.cppm:158`). Correct vs prior race (`ac9aeae`/`4686efd`). Minor: `pausePos_=0` assigned twice (`engine.cppm:159,249`) redundant.

**`engine.cppm:224` `seek()`** — parks to Paused, holds `decodeMtx_`, seeks decoder, resets ring, resumes Playing. Verified fix for `4686efd`. Edge: seek failure restores Playing (`engine.cppm:239`) without resetting `playStart_`/`pausePos_` to pre-seek value — caller sees position jump despite error. Preserve snapshot and restore on error.

**`engine.cppm:333,765` `setShuffle` ignores current perm when toggling off→on?** Calls `setShuffleLocked(true)` which clears `perm` then re-derives `cnt=db_->queueCountLocked` (`engine.cppm:758`). If queue mutated concurrently between `queueNextLocked` caller holding `queueLock_` (atomic spin, not `m_`) and `db_->queueCountLocked` (shared-lock), count may be stale. However `queueLock_` is held across `setShuffleLocked` via `play/next/prev` callers (`engine.cppm:312,368,389`), so safe — document that `setShuffleLocked` requires `queueLock_` held.

**Non-shuffle cursor persistence (`engine.cppm:599` comment, `engine.cppm:910` wrap):** now wraps for both `Off` and `Queue` by resetting `cursor=0`. Differs from prior `REPEAT_QUEUE dequeue+enqueue` transactional atomicity issue (carry-over Critical 1.1). That transaction is now avoided because queue is persistent via `cursor` not destructive `queueDequeueLocked`. **Carry-over 1.1 is obsolete** for Engine's path but remains true for direct `Database::queueDequeueLocked` callers — mark it as "db-layer only".

**`engine.cppm:531` `withTransaction`:** captures `char* err` as stack local then `SqliteErrGuard errGuard{err}` **before** `err` is set by `sqlite3_exec` — guard copies uninitialized pointer. Elsewhere `detail.cppm:229` guard copies value; here fix to `char* err=nullptr; int rc=sqlite3_exec(..., &err); SqliteErrGuard g{err};` after. Also `sqlite3_exec` returns pointer that must be freed even on success — current code leaks when `rc==OK`? `SqliteErrGuard` frees on scope exit regardless, correct, but initialization order bug masks it.

**`engine.cppm:1238` `gaplessArmed_` CAS:** correctly 0→1, re-arms on `next()` failure. Good.

**History CAS (`engine.cppm:1134`):** exactly-once `markedPlayed_` via `compare_exchange_strong` — correct. Transaction in `doHistoryMark` re-prepares statements under `unique_lock<shared_mutex>` — ok.

### 5.5 `cli/src` — Daemon Surface (largest new risk)

**`config.cppm:28` vs `service_impl.cppm:62` vs `app.cppm:132` — path derivation triplicated.** `defaultDbPath` (XDG), `socketPathForDb` (hash-of-dbPath), `pidPathForSocket`, `lockPathForSocket` each hash `dbPath.generic_string()`. All three copies drift (e.g., Windows `LOCALAPPDATA` branch `config.cppm:30` vs `service_impl.cppm:78`). Single source in `caudio.cli:config` should own `socketPathFor`/`pidPathForSocket` and service/app should import it — currently `service_impl.cppm:130` re-implements.

**`service_impl.cppm:223,269` Config hand-written JSON:** `readConfigValueRaw`/`writeConfigValueRaw`/`listConfigValuesRaw` are fragile string-search parsers (≈120 LOC) duplicating `nlohmann::ordered_json` which is already available (`config.cppm:96` uses it correctly). Fails on escaped quotes, numbers with `e`, nested objects, comments. Replace with `ordered_json` throughout or delete and use `caudio.cli:config` load/save.

**`service_impl.cppm:146` `probeSocketAlive`:** on Windows `CreateFileW` + `CloseHandle` races daemon that just created pipe but not yet connected — probe may see `ERROR_PIPE_BUSY` instead of success. Check `GetLastError` branching. On POSIX, `connect` on UDS with `SOCK_STREAM` succeeds even if `accept` queue full, but okay for stale-socket heuristic.

**`service_impl.cppm:173` `tryAcquireLock` disabled on Windows (`#+WIN32 return true`):** TODO comment remains. Single-instance on Windows now depends only on `CreateNamedPipeW` failing or socket probe, which is racy (TOCTOU between probe and listen). Re-enable flock via `CreateFileW` with `FILE_SHARE_READ|0` + `LockFileEx`.

**`service_impl.cppm:263` inline spec in module:** every `inline` function in `detail_svc` is emitted in each translation unit importing `:impl` — violates module ODR expectation; should be `inline` or module-internal but not header-exposed `detail_svc`. Move to non-exported `module caudio.service:detail` partition.

**`ipc_channel_win.cpp/unix.cpp`:** not separately read but `ipc_channel.cppm` path derivation presumably duplicate — confirm it forwards to `caudio.cli:config::socketPathFor`.

**`ipc_server.cppm:84`** — Windows pipe `CreateNamedPipeW` with `PIPE_UNLIMITED_INSTANCES(255)` fine; accept loop (`ipc_server.cppm:148`) recreates pipe handle after each connect (`next = CreateNamedPipeW`). Correct but if `CreateNamedPipeW` fails (`next==INVALID`), `pipeHandle_=nullptr` leaves server deaf until next `listen` — add retry/backoff.

**`ipc_server.cppm:156,238`** — `ConnectNamedPipe` error test `if (connected==0 && err!=0)` treats **any** non-zero `err` as retry, including `ERROR_PIPE_CONNECTED (535)` which is *success* — code handles 535 specially but should also accept `0` (`connected!=0` means already connected synchronously). Logic inverted: `if (!connected) { err=GetLastError(); if (err==ERROR_PIPE_CONNECTED) connected=true; else if (err==ERROR_IO_PENDING) ... }`.

**`ipc_server.cppm:229,322,356`** `running_` vs `stopSource_` vs `std::stop_token st` triple stop: `run(st,dispatch)` stores `stopSource_` fresh, then checks both `st` and `stopSource_`. Callers `Service::run` passes its own token; `shutdown()` calls `stopSource_.request_stop()` *and* `acceptThread_.request_stop()` (`ipc_server.cppm:358`). Redundant; unify on one token.

**`ipc_client.cppm:66`** — `connect` uses `service::socketPathFor(dbPath)` while `IpcServer::listen` (`ipc_server.cppm:79`) also calls `socketPathFor(dbPath)` — but if `Config::socketPath` is non-empty they diverge: client honors `Config::socketPath`, server honors derived path. `Service::create` (`service_impl.cppm:494`) prefers `cfg.socketPath` if nonempty, but `IpcServer::listen` ignores `cfg.socketPath` and re-derives from `dbPath`. Mismatch when user sets `--socket`.

**`protocol.cppm:43`** — `detail` namespace functions `playbackStateToString` etc. are `inline` with `export module` — leaked as `caudio.cli:detail::`? `protocol.cppm` does `export namespace caudio::cli { namespace detail` but `detail` not exported — okay, but `protocol.cppm:41` `namespace detail` is inside export namespace so it *is* exported ABI. Make `namespace detail` non-exported (`namespace caudio::cli::detail` without `export`).

**`protocol.cppm:756` framing:** 4-byte BE length + JSON. `frame:756` + `deframe:768` pair duplicates: `IpcClient::rawRecv:222` reads header+payload manually, then `deframe` again (`ipc_client.cppm:159`) — `deframe` re-parses the 4-byte header already stripped by `rawRecv`; when `rawRecv` returns *payload only* (no header), `deframe` expects header and fails, falls back to raw `replyStr` (`ipc_client.cppm:162`). Works accidentally; remove double framing or make `rawRecv` return framed buffer and `deframe` once. Server side (`ipc_server.cppm:196`) same: reads header into `hdr`, then payload, then constructs `reqStr` from payload and *also* calls `deframe(payload)` which will fail (payload has no header) and rely on fallback. Clean to just use `std::string(payloadChars)`.

**`service_impl.cppm:881` `QueueAdd`:** handles file path vs ID vs FTS query correctly with `computeFingerprint` + `durationFromDecoder` + `queueEnqueue`. Four concerns: (a) `computeFingerprint:446` opens file twice (sample read then `last_write_time`), ok; (b) `insertTrack` (`database.cppm:178`) holds `unique_lock<m_>` over whole bind/step — long under IPC handler; (c) on `AlreadyExists` it does `findByFingerprint` then `findByPath` fallback — but `AlreadyExists` could also be `path UNIQUE` (tracks.path not unique in schema, so actually `fingerprint` is UNIQUE) so path fallback never triggers; (d) `QueueAdd` loops `queueEnqueue` per track without transaction — partial enqueue on failure.

**`service_impl.cppm:1038` `QueueMove`:** correct but does `queueClear` + re-`queueEnqueue` loop without transaction — concurrent `queueList` sees empty queue briefly. Wrap in `withTransaction`-like batch.

**`app.cppm:187` `spawnDaemon` Windows:** builds mutable `buf` from `wCmd` correctly, uses `DETACHED_PROCESS` (comment correctly notes `CREATE_NEW_CONSOLE|DETACHED` illegal). Polls 15×100ms (`app.cppm:279`) adequate; POSIX child does `setsid`+`chdir("/")`+close stdio then `execl` via `/proc/self/exe` — ok but `exePath` fallback to `"/proc/self/exe"` on non-Linux breaks macOS/BSD `spawnDaemon` background mode. Guard with `#ifdef __linux__`.

**`app.cppm:295` `handleShutdown`:** sends `Shutdown` command then polls `pidPath` + `IpcClient::connect` for 20×100ms. `Service::dispatch(Shutdown)` (`service_impl.cppm:1181`) presumably sets `shutdownRequested_` but not shown in truncated read — verify it wakes `server_->run` loop; otherwise shutdown depends on polling timeout, not signal.

**`app.cppm:404` `config_.socketPath` override:** `if (socketPath.empty() && !dbPath.empty()) socketPath = socketPathFor(dbPath)` uses `service::socketPathFor` which is in `caudio.service` module — cross-layer dependency; should use `cli::config::detail::defaultSocket` instead.

**`shm_status.cppm:44` atomics + char arrays:** `AtomicShmStatus` mixes `std::atomic<T>` with non-atomic `char title[256]` protected by seqlock. Correct pattern, but `std::atomic<double>` is not lock-free on all targets (MSVC): `shm_status.cppm:47` `atomic<double>` may not be `is_lock_free()` inside shared memory. Use `uint64_t` bit-cast or `float` with `atomic<uint32_t>` storage and `memcpy`. Also `snapshot:199` busy-spins on `seq&1` without yield — add `std::this_thread::yield()` after few spins.

**`output_formatter` / `client_impl`:** not re-read; presumably formats `Result` variants for `App`.

---

## 6. Build / Toolchain

**`CMakeLists.txt:99`** vendor `caudio_sqlite OBJECT` + `caudio_sqlite_static STATIC` duplicates same source — OBJECT consumed via `$<TARGET_OBJECTS>` (`CMakeLists.txt:257,325`), STATIC installed/exported. OK but install exports both (`CMakeLists.txt:500` `caudio_sqlite_static` listed, `caudio_sqlite` OBJECT not) — consistent. Consider dropping OBJECT and linking STATIC privately to simplify.

**`CMakeLists.txt:233,247–276` quartet duplication:** `CAUDIO_DB_SOURCES` centralized, but then `caudio_db`/`caudio_db_shared`/`caudio_engine`/`caudio_combined` each repeat same `target_sources FILE_SET CXX_MODULES` plus `target_compile_options` suppression. Works, but 4× duplication of warning suppressions (`-Wno-template-names-tu-local` etc.) hides real diagnostics. Target a single interface target `caudio::db_iface` consumed by both.

**`CMakeLists.txt:373` `CAUDIO_IPC_SOURCES` = `CAUDIO_SERVICE_SOURCES`:** `caudio_ipc` and `caudio_service` are identical source sets (`CMakeLists.txt:356` vs `432`). Intent unclear — alias vs duplication. One should be header-only or alias, not two libs with same TU.

**`CMakeLists.txt:477` `caudio` exe `allow-multiple-definition`:** `target_link_options(... -Wl,--allow-multiple-definition)` (`CMakeLists.txt:477`) is a red flag — hides ODR violations across `caudio_service`/`caudio_ipc` duplication and `inline detail_svc` duplicates. Fix the duplication instead of suppressing the linker.

**`CMakeLists.txt:452` `rt` linkage:** `target_link_libraries(caudio_service PUBLIC rt)` on non-Windows for `shm_open` — glibc ≥2.17 may not need `librt`; guard with `find_library(LIBRT rt)` or use `CMAKE_DL_LIBS`.

**`CMakeLists.txt:221-256` FetchContent:** `nlohmann_json v3.11.3` + `CLI11 v2.4.2` pinned good; `Catch2 v3.7.1` for tests. `CAUDIO_FETCH_FFMPEG` fallback auto-fetch may surprise CI; document.

---

## 7. Behavioral / Correctness Defects — Prioritized

| # | Severity | Location | Defect | Fix |
|---|----------|----------|--------|-----|
| 1 | **Critical** | `engine.cppm:539` `withTransaction` | `char* err; SqliteErrGuard{err}` copies uninit before `sqlite3_exec` sets it; leaks/commits on error. | `char* err=nullptr; int rc=sqlite3_exec(...,&err); SqliteErrGuard g{err};` after call. |
| 2 | **Critical** | `service_impl.cppm:526` stale-socket check | `sp.starts_with("\\\\")` Windows guard inside non-Windows block not compiled; stale Windows named-pipe never reaped. Also `probeSocketAlive` Windows path returns false for `ERROR_PIPE_BUSY`, causing false "stale". | Unify: handle pipe via `WaitNamedPipe`/`ERROR_FILE_NOT_FOUND` check. |
| 3 | **Critical** | `ipc_server.cppm:153` + `service_impl.cppm:173` | Windows single-instance disabled (`tryAcquireLock` returns true) + pipe error handling inverted → two daemons can run. | Re-enable `LockFileEx` flock; fix `ConnectNamedPipe` branch to accept `ERROR_PIPE_CONNECTED`. |
| 4 | **Critical** | `protocol.cppm:756` / `ipc_client.cppm:159` / `ipc_server.cppm:207` | Double framing: `rawRecv` strips header then `deframe` expects it. Works by accident, will break if payload happens to look like BE header. | Make `rawRecv` return payload only and construct string directly; delete `deframe` from IPC path or make `rawRecv` return framed buffer. |
| 5 | **Important** | `database.cppm:113` `getCached` vs `getCachedForUse` | Public unchecked `Statement*` API coexists with `expected` variant; callers can miss null → NPE. | Deprecate raw, keep only `expected` variant. |
| 6 | **Important** | `scan.cppm:113` `scanLibrary` | No outer transaction, 30k lock hops, blocks IPC thread minutes, partial commit on crash. | Wrap in `BEGIN IMMEDIATE` batch per 500 files; run on worker thread, publish progress via callback. |
| 7 | **Important** | `service_impl.cppm:881,1057` queue batch ops | `QueueAdd`/`QueueMove` loop `queueEnqueue`/`queueClear` without transaction → transient empty/partial queue. | Add `Database::withTransaction`-style `queueEnqueueBatch`. |
| 8 | **Important** | `shm_status.cppm:47` `atomic<double>` in shm | `atomic<double>` not lock-free on MSVC → undefined in shared memory. | Store `uint64_t` bits, `bit_cast` on read/write. |
| 9 | **Important** | `engine.cppm:224` `seek` error restore | On `decoder->seek` failure restores Playing but `pausePos_/playStart_` already overwritten; `position()` jumps. | Snapshot `pausePos_,playStart_` before seek, restore on error. |
|10 | **Important** | `database.cppm:49` move-ops lock order | Acquires `o.cacheMutex_` while caller expects `m_`→`cacheMutex_` order. | Lock both in documented order; or forbid move after open. |
|11 | **Minor** | `service_impl.cppm:291` hand-rolled JSON | Fragile to escaping, duplicates `ordered_json`. | Replace with `ordered_json` parse/dump. |
|12 | **Minor** | `service_impl.cppm:130` / `app.cppm:132` / `config.cppm:28` | Socket/pid/lock path triplicated. | Centralize in `caudio.cli:config`. |

*Carry-over 1.2/1.3 remain; 1.1 downgraded to db-layer only; 1.4 `detail` export still present in `history_policy.cppm`.*

---

## 8. Other Quality Improvements

- **Remove `-Wl,--allow-multiple-definition`** and deduplicate `caudio_ipc`↔`caudio_service` (`CMakeLists.txt:477`). Create `caudio::service_core` consumed by both `caudio_ipc` (interface alias) and `caudio_service` (owner).
- **Single `socketPathFor(dbPath)` canonical impl** in `caudio.cli:config` (`config.cppm:28` is closest to correct XDG-aware). Have `service_impl`, `app`, `ipc_channel`, `IpcServer/IpcClient` all call it.
- **Module hygiene:** move `detail_svc` (`service_impl.cppm:63`) to internal partition `caudio.service:detail_svc` (no `export`), remove `inline` from per-TU copies; change `export namespace caudio::cli` + `namespace detail` to `namespace caudio::cli::detail` without export (`protocol.cppm:41`).
- **Lock naming:** rename `m_` (`database.cppm:172`) → `dbMutex_`, `cacheMutex_` → `stmtCacheMutex_`, `queueLock_` (`engine.cppm:1389`) → `engineQueueSpin_` to distinguish spin vs mutex.
- **Logging:** two `Logger` constructions in `Service::create` (`service_impl.cppm:564,579`) — keep one. Pass `logger_` to `IpcServer::run` for accept-thread errors instead of swallowing.
- **Error context:** `storage`/`IO` errors in `Service::create` (`service_impl.cppm:549`) append `dbPath` — good; generalize with `makeError(... ) << context` helper.
- **Dead includes:** `service_impl.cppm:8` `<generator>` unused outside `scan`; `protocol.cppm:7` `<span>` used but `<array>` mostly unused; enable `IWYU` or `clang-tidy misc-include-cleaner`.
- **Formatting:** `app.cppm:401` one-liners `while (!s.empty()&&...)` missing clang-format (lines 103,105) — keep `.clang-format` enforced.

---

## 9. Rust Rebuild — What Rust Simplifies, Hardens, or Makes Hard

### 9.1 Where Rust Is a Clear Win (simpler + harder to get wrong)

| Area | C++ Pain Today | Rust Simplification / Hardening |
|------|----------------|----------------------------------|
| **Statement/cache & DB handle** | Raw `sqlite3*`, `shared_mutex`+`mutex` informal ordering (`database.cppm:172`), `Statement` lifetime tied to cache, use-after-close risk | `rusqlite` (bundled `libsqlite3-sys` with `features=["bundled","fts5","cache"]`) — connection owns `rusqlite::Connection`, `Statement` borrow-checked, `Send`/`!Sync` enforces single-thread conn or `Arc<Mutex<Connection>>` + `prepare_cached`. Eliminates 3-variant cache API. |
| **Queue/shuffle invariants** | `perm` + `cursor` + `shuffle` + `repeat` invariants in `engine.cppm:335` with `queueLock_` spin + `shared_mutex` — `reset()` contract informal (`ring.cppm:15`) | Make `QueueState { perm: Vec<i64>, cursor: usize, shuffle: bool, repeat: RepeatMode }` with `fn invariant()` + `#[cfg(debug_assertions)]`, hide behind `Mutex<QueueState>` or `RwLock`; `SpscRing` becomes `rtrb` / `ringbuf` crate with `Send` bounds, `reset` takes `&mut self`. |
| **Threading / Engine lifecycle** | `std::jthread` + `request_stop` + double `monCv_/decodeCv_` + `shutdown()` ordering juggling (`engine.cppm:92`) | `tokio` (async) or `std::thread` + `CancellationToken` (`tokio_util::sync::CancellationToken`), `JoinHandle` RAII, `Drop` impl that joins. Compiler rejects forgotten `join`. |
| **Error handling** | `expected<void,Error>` with `Result` enum + string (`error.cppm:13`) plus ad-hoc `lastErr_` (`engine.cppm:1360`) | `thiserror` + `anyhow`/`miette`, `Result<T, CaudioError>` with `#[derive(Debug, thiserror::Error)]`. No `lastError` string out-param; `?` propagates. |
| **Protocol/Command/Result JSON** | 800-line hand-variant `toJson`/`fromJson` (`protocol.cppm:219,489`) + manual `frame` BE length | `serde::{Serialize, Deserialize}` on `#[derive(Serialize, Deserialize)] enum Command`, `enum Result` + `serde_json`/`rmp-serde` + `tokio-util::codec::LengthDelimitedCodec` (4-byte BE). Eliminates 80% of `protocol.cppm`. |
| **Fingerprint/Path dedup** | Two copies of same BLAKE3 sample logic (`detail.cppm:27` vs `service_impl.cppm:435`) | Single `caudio_core::fingerprint::blake3_sample(path) -> [u8;32]` function, crate-private, shared by `db` and `service` via workspace dep. |
| **Config/socket/pid/lock paths** | Triplicated path math, env var reading (`getenv` unchecked) | One `caudio_config::paths::default_db_path()`, `socket_path_for(&Path) -> PathBuf`, `pid_path_for(&Path)`, with `dirs` crate (`dirs::data_dir()`, `config_dir()`), tested. |
| **SHM status for TUI** | `AtomicShmStatus` with `atomic<double>` not lock-free in shm (`shm_status.cppm:47`), seqlock manual | `shared_memory` / `raw_sync` + `bytemuck` `Pod` snapshot, or `tokio::sync::watch::channel<Status>` if TUI is same-process. If cross-process, `shared_memory::Shmem` with `SeqLock<Status>` using `u64` bits. |
| **History exactly-once** | `compare_exchange_strong` CAS + manual `BEGIN IMMEDIATE` transaction (`engine.cppm:1134`) | Atomic `OnceLock` / `AtomicBool::compare_exchange` same, but `rusqlite::TransactionBehavior::Immediate` + `?` + RAII rollback guarantees commit/rollback pairing. |

### 9.2 Where Rust Makes Things Harder (trade-offs)

| Area | Why Harder in Rust |
|------|--------------------|
| **Audio low-latency callback** (`output.cppm:142` `dataCallback`) | `ma_device.dataCallback` is a C callback on an RT thread that must not allocate/lock. Rust `miniaudio` (`miniaudio-rs`) or `cpal` callback requires `FnMut` with `'static` + `Send`; borrow-checker fights raw `SpscRing` shared with decode thread. Solution: `rtrb::Consumer` + `AtomicF32` volume, `unsafe` shim or `cpal` stream with `FnMut` closure. Still possible but requires `unsafe` boundary and `Send` proofs. |
| **FFmpeg / decoders** | `ffmpeg.cppm` via libavcodec/format is `unsafe` FFI heavy; Rust `ffmpeg-next` or `symphonia` (pure Rust, supports mp3/flac/ogg/wav/m4a) avoids FFmpeg licensing but still requires `unsafe` for packet decode. Build complexity shifts to Cargo `features`. |
| **Async IPC vs sync SQLite** | `rusqlite` is blocking; `tokio` + blocking DB needs `spawn_blocking` or `deadpool`. UDS/Named Pipe: `tokio::net::UnixListener` exists, Windows named pipe needs `tokio::net::windows::NamedPipeServer` (nightly-ish API churn). Dual-platform UDS vs pipe abstraction still manual. |
| **C++ module interop** | Existing C++ consumers (`caudio_combined` shared lib) would need `cxx`/`cbindgen` shim. Incremental port benefits from `cxx` bridging rather than big-bang. |
| **Build / vendoring** | `sqlite3.c` + `blake3.c` + `miniaudio.h` vendored here; Rust equivalents are crates (`rusqlite` bundles sqlite, `blake3` crate, `miniaudio-rs`). Fetches from `crates.io` require auditing vs audited vendor copies. Offline builds need `cargo vendor`. |

### 9.3 Suggested Rust Stack (from-scratch rebuild)

```
Workspace crates:
  caudio-core       (no I/O: Track, QueueItem, fingerprint, history policy)
  caudio-db         (rusqlite bundled+fts5, r2d2 or single Connection+Mutex, migrations via refinery)
  caudio-player     (symphonia + cpal | miniaudio-rs, rtrb ring, volume AtomicU32 bits)
  caudio-engine     (QueueState, PlaybackState, decode+monitor tasks via tokio)
  caudio-protocol   (serde Command/Result, LengthDelimitedCodec)
  caudio-service    (tokio UnixListener / windows NamedPipe, Dirs paths, single-instance via fs2 flock)
  caudio-cli        (clap v4, same subcommands as cli/src/app/app.cppm:332)
  xtask / tests     (insta snapshots for protocol, proptest for queue invariants)

Key deps: tokio, clap, serde/serde_json, rusqlite (+bundled, fts5), symphonia, cpal|minaudio-rs,
          rtrb, blake3, dirs, thiserror, tracing, fs2, shared_memory (optional), refinery.
Toolchain: stable Rust 1.82+, edition 2021, `cargo fmt` + `clippy -- -W clippy::pedantic`.
```

### 9.4 Stepwise Migration Plan (not big-bang)

1. **Week 1 — Core types & protocol:** `caudio-core` + `caudio-protocol` with `serde` round-trip tests covering `protocol.cppm:331` + `568` matrix; snapshot `frame` golden files for wire compat.
2. **Week 2 — DB layer:** `caudio-db` with `rusqlite` + `refinery` migrations from `schema.cppm:9`; port `database.cppm` CRUD + `searchFts` LIKE fallback; property-test `sanitizeFtsTerm` vs `detail.cppm:116`.
3. **Week 3 — Player ring/output:** `rtrb` ring vs `ring.cppm:16` with `cpal` callback; `cargo test -- --ignored` audio loopback (no device on CI — mock).
4. **Week 4 — Engine queue/history:** port `queue_logic.cppm` + `history_policy.cppm` with proptest invariants (perm is permutation, cursor bounds, repeat wrap); single-thread first.
5. **Week 5 — Service/IPC:** `tokio` listener abstraction over UDS/pipe, `LengthDelimitedCodec`, `fs2::FileExt::try_lock_exclusive` for single-instance (replaces `tryAcquireLock` Windows gap).
6. **Week 6 — CLI + scan:** `clap` app mirroring `app.cppm:332`, `walkdir` + `blake3` scan with `tokio::task::spawn_blocking` batching (fixes `scanLibrary` blocking).
7. **Week 7 — Hardening:** `cargo fuzz` on `frame`/`deframe`, `cargo miri` for ring/seqlock, `loom` for engine queue CAS, CI on Win+Linux TUI SHM path.

---

## 10. Immediate Action Items (blocking merge → master)

1. Fix `withTransaction` err-guard init order (`engine.cppm:539`).
2. Re-enable Windows flock or document single-instance as pipe-only with `WaitNamedPipe` probe.
3. Remove double framing (`ipc_client.cppm:159`, `ipc_server.cppm:207`).
4. Deduplicate `socketPathFor` to one canonical impl; wire both `IpcClient::connect` and `IpcServer::listen` to it and respect `Config::socketPath`.
5. Replace hand-rolled `readConfigValueRaw` with `ordered_json` or delete in favor of `caudio.cli:config`.
6. Replace `atomic<double>` in `AtomicShmStatus` with `atomic<uint64_t>` bits.
7. Remove `-Wl,--allow-multiple-definition` by merging `caudio_ipc`/`caudio_service` source sets (`CMakeLists.txt:373`).
8. Add `BEGIN IMMEDIATE` batch to `scanLibrary` and `queueAdd`/`queueMove`.

---

## 11. References

- Prior report: `REVIEW_REPORT.md:1` (62 findings, 57 applied, 5 carry-over)
- Engine: `src/engine/engine.cppm:132` play, `src/engine/engine.cppm:224` seek, `src/engine/engine.cppm:531` withTransaction
- DB: `src/db/database.cppm:84` cache variants, `src/db/queue.cppm:25` ignored cache params, `src/db/scan.cppm:90` scanLibrary, `src/db/schema.cppm:9` schema
- Utils: `src/utils/ring.cppm:15,48` SPSC, `src/utils/queue.cppm:17` MpscQueue, `src/utils/log.cppm:52` Logger
- Player: `src/player/output.cppm:142` dataCallback, `src/player/player_core.cppm:52` Player
- Service: `cli/src/service/service_impl.cppm:63` detail_svc inlines, `cli/src/service/service_impl.cppm:291` hand JSON, `cli/src/service/service_impl.cppm:173` Windows flock TODO
- IPC: `cli/src/service/ipc_server.cppm:84,148` pipe lifecycle, `cli/src/client/ipc_client.cppm:66` socketPath derivation, `cli/src/shared/protocol.cppm:41,756` detail export + framing
- Config/App: `cli/src/config.cppm:28` defaultDbPath, `cli/src/app/app.cppm:132,187` pidPath/spawnDaemon, `cli/src/service/shm_status.cppm:44` SHM atomics
- Build: `CMakeLists.txt:99,233,356,373,456,477` sqlite/object, quartet duplication, `..allow-multiple-definition`, `rt` linkage

*Generated against `.worktrees/feat-cli-service` @ `9f76211`, reviewer: automated full-read.*
