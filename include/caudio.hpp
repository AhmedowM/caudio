#pragma once

#include <caudio/db.hpp>
#include <caudio/engine.hpp>
#include <caudio/player.hpp>
#include <caudio/utils.hpp>

/**
 * @file caudio.hpp
 * @brief Umbrella header for caudio â€” modern C++23 music player library.
 * @defgroup caudio caudio
 * @ingroup caudio
 *
 * @details caudio is a cross-platform, offline-first music player library
 * written in modern C++23. It provides four independent, layered modules:
 *
 * - @ref caudio_utils "caudio.utils" â€” Core utilities: error handling (`std::expected`),
 *   lock-free SPSC/MPSC queues, thread helpers, logging.
 * - @ref caudio_player "caudio.player" â€” Audio playback: reader abstractions,
 *   FFmpeg-based decoder registry, miniaudio output, gapless playback.
 * - @ref caudio_db "caudio.db" â€” Database layer: SQLite with WAL, track/playlist/queue
 *   management, full-text search (FTS5), JSON import/export, library scanning.
 * - @ref caudio_engine "caudio.engine" â€” Playback engine: state machine, shuffle/repeat,
 *   gapless transition, history marking, persistence, event queue.
 *
 * Layer rule (enforced by module imports):
 * @code
 * caudio.utils  (no deps)
 *    â†‘
 * caudio.player (imports utils)
 * caudio.db     (imports utils)
 *    â†‘
 * caudio.engine (imports utils, player, db)
 * @endcode
 *
 * Threading model:
 * - `utils::SpscRing` â€” lock-free SPSC (decode thread â†’ audio callback)
 * - `utils::MpscQueue` â€” bounded MPSC (event queue, writer thread)
 * - `Engine` â€” `queueMutex_` (try_lock) for queue state, `decodeMtx_` for
 *   decoder/ring vs seek, `dbMutex_` (shared_mutex) for DB, `cbMutex_` for callbacks.
 * - `Database` â€” `dbMutex_` (shared_mutex) serializes all SQLite access;
 *   `cacheMutex_` protects prepared-statement cache.
 *   Lock order: `dbMutex_` â†’ `cacheMutex_`.
 * - `WriterThread` â€” single consumer, MPSC queue, `condition_variable` + `stop_token`.
 *
 * Error handling: All fallible public APIs return `std::expected<T, Error>`
 * with `StatusCode` enum (Ok, InvalidArg, NotFound, Unsupported, Io, Device,
 * State, NoMem, Internal, AlreadyExists, Busy, Corrupt, NoSpace).
 *
 * Build: CMake 3.28+, C++23 modules, Ninja. Vendored deps: SQLite (FTS5),
 * miniaudio, BLAKE3, nlohmann::ordered_json, FFmpeg (required).
 *
 * @see caudio.utils
 * @see caudio.player
 * @see caudio.db
 * @see caudio.engine
 */
