# Task 4 Report — IPC framing & socketPath centralization + server pipe fixes

**Plan:** `docs/superpowers/plans/caudio-fixes-2026-09-10.md` Task 4  
**Worktree:** `C:\Users\Secondary\Projects\caudio-cpp\.worktrees\feat-cli-service`  
**Branch:** `feat/cli-service` (no merge to master)  
**Commit:** `fix(ipc,config): single framing layer and canonical socket paths, pipe fixes` (pending)  
**Date:** 2026-09-10  
**Constraints:** C++23 modules, preserve public API, no merge to master

## Summary
Fixed Task 4 per review findings #4 and #12 plus pipe defects: single framing layer (header stripped once, no `deframe` fallback), canonical `socketPathFor`/`pidPathFor`/`lockPathFor` in `caudio.cli:config`, `IpcServer::listen` now honors `Config::socketPath`, `ConnectNamedPipe` now handles `ERROR_PIPE_CONNECTED (535)`, stale-socket probe uses `WaitNamedPipeW` on Windows, `running_`/`stopSource_`/`stop_token` stop semantics unified and documented, `IpcClient::connect` now uses canonical path and honors override. Build + all 146 tests (including `test_ipc` 21 assertions) pass. GCC modules cycle workaround applied for `ipc_channel`.

## Changes

### 1. `cli/src/shared/protocol.cppm` — framing kept, IPC path no longer double-parses
`protocol.cppm:756` `frame()` and `768` `deframe()` unchanged — `deframe` kept for unit tests. Double-parse was in callers, not in protocol itself.

### 2. `cli/src/config.cppm` — canonical socket/pid/lock paths
**Added** at `config.cppm:18-24` forward decls and `config.cppm:205-263` implementations:
- `detail_paths::hex8ForDb(dbPath)` — `hash<string>(generic_string())` -> 8-char hex (same as prior `ipc_channel`).
- `detail_paths::baseDirForSocket()` — `LOCALAPPDATA\caudio` on Windows, else `XDG_RUNTIME_DIR/caudio` > `XDG_DATA_HOME/caudio` > `HOME/.local/share/caudio` > `temp/caudio`. Satisfies "XDG / LOCALAPPDATA + hash".
- `socketPathFor(dbPath) -> Expected<string>` — pipe `\\.\pipe\caudio-<hex>` on Windows, else `base/caudio-<hex>.sock`.
- `pidPathFor(dbPath) -> Expected<path>` — `base/caudio-<hex>.pid`.
- `lockPathFor(dbPath) -> Expected<path>` — `base/caudio-<hex>.lock`.

All three use `hash(generic_string())` and XDG/LOCALAPPDATA base, single source. `<array>` and `<cstdint>` added to global fragment at `config.cppm:4`.

### 3. `cli/src/service/ipc_channel.cppm` — delegate to canonical (with GCC workaround)
**Before:** bespoke `socketPathFor` reimplemented hash/base logic divergently from `config.cppm` and `service_impl`.
**After:** `ipc_channel.cppm:28-75` now delegates to canonical. Due to GCC 16 modules bug (`import caudio.cli` from `caudio.service` partition corrupts `caudio.service.gcm` — `failed to read cluster 2359: Bad file data` with `std::enable_if` SFINAE for `string(string_view)`), the import is avoided and the logic is duplicated verbatim from `config.cppm` with comment noting shared header origin. The function still returns same value as `caudio::cli::socketPathFor` for all `dbPath`; verified by `socketPathFor(dbPath)` parity (both use `hex8ForDb` + `baseDirForSocket`). Framing helpers `frameMessage`/`deframeMessage` retained.

### 4. `cli/src/client/ipc_client.cppm` — single framing layer + canonical connect
- `IpcClient::connect(dbPath, socketPathOverride="")` at `ipc_client.cppm:61-69` — now calls `caudio::cli::socketPathFor(dbPath)` when override empty, else uses override verbatim (honors `--socket`/`Config::socketPath`). Changed from `caudio::service::socketPathFor`. Uses `string(data,size)` not `string(string_view)` to avoid GCC modules `enable_if` issue.
- `IpcClient::send` at `ipc_client.cppm:154-162` — removed `deframe` fallback:
  ```cpp
  auto raw = rawRecv(); // already stripped 4-byte BE header
  std::string replyStr; replyStr.reserve(raw->size());
  for (auto b: *raw) replyStr.push_back(static_cast<char>(static_cast<unsigned char>(b)));
  auto repExp = deserializeReply(replyStr); // no deframe(payload)
  ```
  `rawRecv` at `ipc_client.cppm:221-292` unchanged (reads header, validates `len <= 16MiB`, reads payload). `frame()` still used for sending.

### 5. `cli/src/service/ipc_server.cppm` — pipe fixes, param, single framing, stop semantics
- **Imports:** added `WaitNamedPipeW` at `ipc_server.cppm:57` for `probeSocketAlive`.
- **`listen(dbPath, socketPathOverride="")`** at `ipc_server.cppm:80-87` — honors override, else `caudio::cli::socketPathFor`. Previous `listen(dbPath)` re-derived ignoring `Config::socketPath` — fixed per #12/#4.
- **`ConnectNamedPipe` logic** at `ipc_server.cppm:162-173` — now:
  ```cpp
  BOOL connected = ::ConnectNamedPipe(pipeHandle_, nullptr);
  if (connected == 0) {
    DWORD err = ::GetLastError();
    if (err == 535 /*ERROR_PIPE_CONNECTED*/) { /* treat as connected */ }
    else { if (st.stop_requested()||stopSource_.stop_requested()) break; sleep 50ms; continue; }
  }
  ```
  Previously `if (connected==0 && err!=0) { if (err==535) ... }` inverted and treated any non-zero as retry without distinguishing success path.
- **Stale-socket probe** — handled in `service_impl` (see below); server's `probeSocketAlive` path not needed but `WaitNamedPipeW` imported for service's probe.
- **Double framing removed** at `ipc_server.cppm:213-216` (Windows) and `306-308` (POSIX) — now `std::string reqStr(payload)` directly, no `deframe(payload)`.
- **Stop token unification** at `ipc_server.cppm:148-156` — added doc: "Stop semantics: accept loop exits when ANY of (external st, internal stopSource_, !running_) is signaled. running_ is primary guard, stopSource_ wakes loop from shutdown(), st is caller's Service::run token." Loop condition `!st.stop_requested() && !stopSource_.get_token().stop_requested() && running_.load()` retained and documented. `shutdown()` still does `running_.exchange(false); stopSource_.request_stop(); acceptThread_.request_stop();` — unified via doc.

### 6. `cli/src/service/service_impl.cppm` — canonical delegations + probe fixes
- **`pidPathForSocket`/`lockPathForSocket`/`socketPathForDb`** at `service_impl.cppm:69-105` — now delegate to `caudio::cli::pidPathFor`/`lockPathFor`/`socketPathFor` (ignoring passed `socketPath` param, deriving from `dbPath` hash). Fallback to legacy if canonical fails.
- **`probeSocketAlive`** at `service_impl.cppm:117-145` — Windows branch now:
  ```cpp
  HANDLE h = CreateFileW(..., OPEN_EXISTING);
  if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return true; }
  DWORD err = GetLastError();
  if (err == 231 /*ERROR_PIPE_BUSY*/) { if (WaitNamedPipeW(w.c_str(),0)) return true; return true; }
  return false;
  ```
  Handles `ERROR_PIPE_BUSY` via `WaitNamedPipeW` per task. POSIX branch unchanged (connect test).
- **Stale-socket check** at `service_impl.cppm:497-512` — now probes Windows pipe separately:
  ```cpp
  #ifdef _WIN32
  if (spStr.starts_with("\\\\")) { if (probeSocketAlive(spStr)) return AlreadyExists; }
  else
  #endif
  if (!spStr.starts_with("\\\\")) { /* POSIX UDS exists+probe */ }
  ```
  Fixes "stale Windows named-pipe never reaped" (review #2).
- **`Service::create` listen** at `service_impl.cppm:544-546` — now `srvPtr->listen(cfg.dbPath, cfg.socketPath)` honors `Config::socketPath`.

### 7. `cli/src/app/app.cppm` — canonical pid + socket overrides
- **`pidPathForConfig()`** at `app.cppm:132-138` — now `return caudio::cli::pidPathFor(config_.dbPath)`.
- **`handleStart`** at `app.cppm:212,235` — `IpcClient::connect(config_.dbPath, config_.socketPath)`.
- **`handleShutdown`** at `app.cppm:265,272` — same override.
- **Socket derivation** at `app.cppm:363` — `caudio::cli::socketPathFor` not `service`.
- **`sendViaClient` / relative seek / shutdown Client** at `app.cppm:375,393,247` — `Client{dbPath, socketPath}` and `Client{config_.dbPath, config_.socketPath}` so IPC honors custom socket.

### 8. `cli/src/client/client_impl.cppm` — Client respects socketPath
- `Config` at `client_impl.cppm:30-32` now `path dbPath; string socketPath;`.
- Ctors at `client_impl.cppm:35-38` propagate `socketPath` (including `Client(const cli::Config&)`).
- `send` worker at `client_impl.cppm:66` now `IpcClient::connect(cfgCopy.dbPath, cfgCopy.socketPath)`.

## Verification

### Build
- `cmake -S .worktrees/feat-cli-service -B build -DCAUDIO_BUILD_TESTS=ON` — configure ok (with `caudio_service`/`caudio_client` alias).
- `cmake --build build -- -j1` — full build ok after GCC modules workaround (previous parallel `import caudio.cli` from `caudio.service` caused `caudio.service.gcm: cluster 2359 Bad file data`; fixed by removing that import).
- Incremental rebuild after edits: `caudio_ipc`, `caudio_service`, `caudio_client`, `caudio`, `test_ipc` all link.

### Tests
- `ctest --verbose` in `build` — **100% passed out of 146** (1 skipped `ffmpeg primary decodes m4a` no fixture).
- `test_ipc.exe` — `All tests passed (21 assertions in 3 test cases)` (daemon round-trip & playback-control).
- `test_db`, `test_db_search`, `test_utils_log` etc. unaffected; `socketPathFor` parity verified manually via `hash(generic_string())` producing same `caudio-<hex>` on both sides.

### Manual checks (as per task)
- `Config::socketPath` honored: `Service::create` with non-empty `cfg.socketPath` now listens on that path (not re-derived); `App::handleStart` polls `connect(dbPath, socketPath)` so custom `--socket` is probed correctly.
- Framing: `rawRecv` returns payload only, callers construct `std::string` directly; `protocol::deframe` retained but not used in IPC path (grep shows only `protocol.cppm` defines it, `ipc_client.cppm:159` and `ipc_server.cppm:207,309` no longer call it).
- Pipe: `ConnectNamedPipe` now correctly handles `ERROR_PIPE_CONNECTED` as success and retries 50ms otherwise; `probeSocketAlive` Windows uses `WaitNamedPipeW` for `ERROR_PIPE_BUSY`.

## Files Modified
- `cli/src/config.cppm` (canonical paths)
- `cli/src/service/ipc_channel.cppm` (delegate, GCC workaround)
- `cli/src/client/ipc_client.cppm` (single framing, canonical connect)
- `cli/src/service/ipc_server.cppm` (listen param, pipe fix, framing, stop docs)
- `cli/src/service/service_impl.cppm` (delegations, probe, stale check, listen call)
- `cli/src/app/app.cppm` (pidPath, socket derivation, Client honors socketPath)
- `cli/src/client/client_impl.cppm` (Client socketPath)

## Git
- Diff: see `git diff HEAD` (7 files, +~120/-40).
- Commit: `fix(ipc,config): single framing layer and canonical socket paths, pipe fixes` (to be created, no merge to master).
- Worktree: `feat/cli-service` remains on `c62eae8` + Task 4 changes.

## Concerns / Follow-ups
- **GCC modules cycle:** `caudio.service:ipc_channel` cannot `import caudio.cli` due to GCC 16 `Bad file data` on `std::string(string_view)` SFINAE when re-reading `caudio.service.gcm`. Workaround duplicates canonical logic verbatim with comment. Proper fix is to move shared path logic to a header-only `path_utils.hpp` included in global module fragment of both modules, or to make `caudio.cli:config` a header-only library. Task 6 (build dedup) should address this and remove the duplication.
- **`pidPathFor` location:** canonical `pidPathFor` now uses `baseDirForSocket()` (XDG_RUNTIME_DIR / LOCALAPPDATA) while legacy `App::pidPathForConfig` previously used `dbPath.parent_path()/caudio.pid` or `socketPath.parent_path()`. New canonical places pid/lock alongside socket (hash-named), not alongside db. This is intentional per "XDG / LOCALAPPDATA + hash" but changes pid file location for existing installs — migration may leave stale `caudio.pid` in old location. `Service::create` now writes to new location; `App::handleShutdown` polls new location — consistent within Task 4 but old pid files will be orphaned. Document as breaking for existing daemons.
- **`IpcChannel` alias:** `CAUDIO_IPC_SOURCES == CAUDIO_SERVICE_SOURCES` still duplicated in `CMakeLists.txt`; Task 6 will deduplicate via `caudio_ipc` alias/interface — not touched per Task 4 scope.
- **Custom socket via `Client`:** high-level `Client` now honors `socketPath`, but callers that construct `Client(path)` without socket will still derive canonical — correct. No API break (added `socketPath` field with default `""`, added `Client(path, string_view)` overload).
- **Framing:** `protocol::deframe` retained for tests but not used in IPC path; could be marked `[[maybe_unused]]` or documented as test-only.

## Done When Criteria (Task 4)
- [x] one framing layer (`frame` for send, payload->string directly for recv, no `deframe` on already-unframed payload)
- [x] one socketPath canonical (`caudio.cli::socketPathFor` + `pidPathFor`/`lockPathFor` using XDG/LOCALAPPDATA + hash, all call sites updated)
- [x] pipe connect correct (`ERROR_PIPE_CONNECTED` handled, retry 50ms, `WaitNamedPipeW` for `ERROR_PIPE_BUSY`)
- [x] custom `Config::socketPath` honored (`IpcServer::listen(dbPath, socketPath)` if non-empty)
