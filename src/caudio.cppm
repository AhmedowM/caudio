module;
#include <string_view>
#include "caudio/version.hpp"

/**
 * @file caudio.cppm
 * @brief Umbrella module for caudio — modern C++23 music player library.
 * @defgroup caudio caudio
 * @ingroup caudio
 *
 * @details caudio is a cross-platform, offline-first music player library
 * written in modern C++23. It provides four independent, layered modules:
 *
 * - @ref caudio_utils "caudio.utils" — Core utilities: error handling (`std::expected`),
 *   lock-free SPSC/MPSC queues, thread helpers, logging.
 * - @ref caudio_player "caudio.player" — Audio playback: reader abstractions,
 *   FFmpeg-based decoder registry, miniaudio output, gapless playback.
 * - @ref caudio_db "caudio.db" — Database layer: SQLite with WAL, track/playlist/queue
 *   management, full-text search (FTS5), JSON import/export, library scanning.
 * - @ref caudio_engine "caudio.engine" — Playback engine: state machine, shuffle/repeat,
 *   gapless transition, history marking, persistence, event queue.
 *
 * Layer rule (enforced by module imports):
 * @code
 * caudio.utils  (no deps)
 *    ↑
 * caudio.player (imports utils)
 * caudio.db     (imports utils)
 *    ↑
 * caudio.engine (imports utils, player, db)
 * @endcode
 *
 * Threading model:
 * - `utils::SpscRing` — lock-free SPSC (decode thread → audio callback)
 * - `utils::MpscQueue` — bounded MPSC (event queue, writer thread)
 * - `Engine` — `queueMutex_` (try_lock) for queue state, `decodeMtx_` for
 *   decoder/ring vs seek, `dbMutex_` (shared_mutex) for DB, `cbMutex_` for callbacks.
 * - `Database` — `dbMutex_` (shared_mutex) serializes all SQLite access;
 *   `cacheMutex_` protects prepared-statement cache.
 *   Lock order: `dbMutex_` → `cacheMutex_`.
 * - `WriterThread` — single consumer, MPSC queue, `condition_variable` + `stop_token`.
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

export module caudio;

export import caudio.utils;
export import caudio.player;
export import caudio.db;
export import caudio.engine;

export namespace caudio {

/**
 * @brief Library version string (git tag).
 * @ingroup caudio
 */
constexpr std::string_view version() noexcept {
    return CAUDIO_VERSION_FULL;
}

/**
 * @brief Git commit hash.
 * @ingroup caudio
 */
constexpr std::string_view gitHash() noexcept {
    return CAUDIO_VERSION_TWEAK;
}

} // namespace caudio