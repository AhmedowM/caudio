#pragma once

/**
 * @file caudio.hpp
 * @brief Umbrella header for caudio -- modern C++23 music player library.
 * @defgroup caudio caudio
 * @ingroup caudio
 *
 * @details caudio is a cross-platform, offline-first music player library
 * written in modern C++23. It provides layered libraries (this header
 * includes all of them):
 *
 * - @ref caudio_types "caudio.types" -- Dependency-free shared value types:
 *   wire enums (RepeatMode, PlaybackState) and plain-data records (Track,
 *   Playlist, QueueItem, HistoryEntry, Bookmark) used across all layers.
 * - @ref caudio_utils "caudio.utils" -- Core utilities: error handling (`std::expected`),
 *   lock-free SPSC ring, thread helpers, logging, opaque JSON value.
 * - @ref caudio_media "caudio.media" -- Media input: reader abstractions,
 *   FFmpeg-based decoder, metadata extraction, audio-extension helpers,
 *   library scan discovery (filesystem walk, fingerprint, tags).
 * - @ref caudio_player "caudio.player" -- Audio playback: miniaudio output,
 *   gapless playback. Reader/decoder are re-exported from `caudio.media`.
 * - @ref caudio_db "caudio.db" -- Database layer: SQLite with WAL, track/playlist/queue
 *   management, full-text search (FTS5), JSON import/export, library scan
 *   persistence (upsert of the tracks discovered by `caudio.media`).
 * - @ref caudio_engine "caudio.engine" -- Playback engine: state machine, shuffle/repeat,
 *   gapless transition, history marking, persistence, event queue.
 * - @ref caudio_ipc "caudio.ipc" -- Daemon wire protocol: Command/Result
 *   variants, JSON serialization, length-prefixed framing.
 * - @ref caudio_client "caudio.client" -- Daemon SDK: Client, IPC transport,
 *   output formatting (used by the CLI and third-party frontends).
 * - @ref caudio_service "caudio.service" -- Daemon runtime: Service owning
 *   Engine, Database and the IPC server.
 * - config -- JSON config file, socket/pid/lock path derivation.
 *
 * Layer rule (enforced by link dependencies; each line lists its direct
 * caudio dependencies):
 * @code
 * caudio.types   (no deps)
 * caudio.utils   (types)
 * caudio.media   (types, utils)
 * caudio.player  (types, utils, media)
 * caudio.db      (types, utils, media)
 * caudio.engine  (types, utils, media, player, db)
 * caudio.ipc     (utils)
 * caudio.client  (utils, ipc)
 * caudio.service (utils, ipc, engine, db, media, player)
 * caudio.app     (utils, ipc, client, media)
 * @endcode
 *
 * Threading model:
 * - `utils::SpscRing` -- lock-free SPSC (decode thread ->' audio callback)
 * - `Engine` -- `queueMutex_` (try_lock) for queue state, `decodeMtx_` for
 *   decoder/ring vs seek, `dbMutex_` (shared_mutex) for DB, `cbMutex_` for callbacks.
 * - `Database` -- `dbMutex_` (shared_mutex) serializes all SQLite access;
 *   `cacheMutex_` protects prepared-statement cache.
 *   Lock order: `dbMutex_` ->' `cacheMutex_`.
 * - Background writer -- single consumer, bounded MPSC queue,
 *   `condition_variable` + `stop_token`.
 *
 * Error handling: All fallible public APIs return `std::expected<T, Error>`
 * with `StatusCode` enum (Ok, InvalidArg, NotFound, Unsupported, Io, Device,
 * State, NoMem, Internal, AlreadyExists, Busy, Corrupt, NoSpace).
 *
 * Build: CMake 3.28+, C++23 modules, Ninja. Vendored deps: SQLite (FTS5),
 * miniaudio, BLAKE3, FFmpeg (required). JSON (nlohmann backend) is private
 * to the build -- downstream needs no JSON package.
 *
 * @see caudio.types
 * @see caudio.utils
 * @see caudio.media
 * @see caudio.player
 * @see caudio.db
 * @see caudio.engine
 * @see caudio.ipc
 * @see caudio.client
 * @see caudio.service
 * @see caudio_config
 */

#include <caudio/app.hpp>
#include <caudio/client.hpp>
#include <caudio/config.hpp>
#include <caudio/db.hpp>
#include <caudio/engine.hpp>
#include <caudio/ipc.hpp>
#include <caudio/media.hpp>
#include <caudio/player.hpp>
#include <caudio/service.hpp>
#include <caudio/types.hpp>
#include <caudio/utils.hpp>
#include <caudio/version.hpp>
