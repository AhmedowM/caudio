#pragma once

#include <cstdint>

/**
 * @file wire.hpp
 * @brief Dep-free wire vocabulary shared by the engine and the IPC layer.
 * @defgroup caudio_types caudio wire value types
 * @details First inhabitant of the `caudio::types` layer: plain scalar types
 * that both `caudio::engine` and `caudio::ipc` depend on, so neither component
 * has to include the other. `RepeatMode` and `PlaybackState` cross the
 * daemon/client boundary (fields of `caudio::ipc::Status` and of several
 * commands) and have a fixed `int` underlying type matching the JSON wire
 * encoding (the wire text lives in `types/format.hpp`, its inverse in
 * `types/parse.hpp`).
 *
 * `caudio::engine` re-exports these as aliases (see `engine/types.hpp`). This
 * layer is deliberately dependency-free (no `fwd.hpp`, no std headers) so it
 * can sit at the bottom of the component graph. Later U-3 work moves the
 * plain-data structs (`Track`, `Playlist`, ...) here as well.
 */

namespace caudio::types {

/**
 * @brief Repeat mode for queue advancement.
 * @ingroup caudio_types
 * @details Controls wrap behaviour in Engine::queueNextLocked and
 * Engine::queuePrevLocked when the cursor reaches the end or start.
 * - `Off`: linear; shuffle wrappers reshuffle and non-shuffle wraps to 0.
 * - `All`: same wrap as Off; single-track repeat is only via `One`.
 * - `One`: repeats the current track without advancing the cursor.
 * @see caudio::ipc::Status
 */
enum class RepeatMode : std::uint8_t { Off = 0, All = 1, One = 2 };

/**
 * @brief Playback state machine (wire value).
 * @ingroup caudio_types
 * @details State transitions (Engine):
 * - `Stopped -> Playing` via Engine::play().
 * - `Playing -> Paused` via Engine::pause().
 * - `Paused -> Playing` via Engine::resume() / play() resume path.
 * - `Playing -> Playing` (restart) via play() when already playing (seek 0).
 * - Any -> `Stopped` via Engine::stop().
 * @see caudio::ipc::Status
 */
enum class PlaybackState : std::uint8_t { Stopped = 0, Ready = 1, Playing = 2, Paused = 3 };

} // namespace caudio::types
