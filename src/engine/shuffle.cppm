module;
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

/**
 * @file shuffle.cppm
 * @brief Shuffle permutation helpers for the playback queue.
 * @ingroup caudio_engine
 * @details Provides the shuffle primitive used by
 * Engine::setShuffleLocked and Engine::queueNextLocked. Permutations
 * are vectors of queue positions `[0, queueCount)` shuffled via
 * `std::ranges::shuffle` with `std::mt19937`.
 *
 * Two overloads are exported under `caudio::engine::detail`:
 * - `shufflePerm(perm, rng)` uses a caller-supplied engine (e.g.
 *   `std::mt19937{42}` for deterministic tests).
 * - `shufflePerm(perm)` seeds `mt19937` from `std::random_device`
 *   for production randomness.
 *
 * Thread safety: helpers are stateless and operate on the caller's
 * vector; Engine holds `queueMutex_` while mutating `QueueState::perm`.
 */

export module caudio.engine:shuffle;

import :types;

export namespace caudio::engine::detail {

/**
 * @brief Shuffles a permutation in-place using the supplied RNG.
 * @ingroup caudio_engine
 * @param perm Vector of positions to shuffle (typically `[0, n)`).
 * @param rng Mersenne Twister engine; caller controls seeding.
 * @details Thin wrapper over `std::ranges::shuffle(perm, rng)`.
 * Used by tests with `std::mt19937{42}` for deterministic ordering.
 * @par Thread safety
 * No internal synchronization; caller must hold `queueMutex_` if
 * `perm` is `QueueState::perm`.
 * @see shufflePerm(std::vector<int64_t>&)
 */
inline void shufflePerm(std::vector<int64_t>& perm, std::mt19937& rng) {
    std::ranges::shuffle(perm, rng);
}

/**
 * @brief Shuffles a permutation using a random_device-seeded engine.
 * @ingroup caudio_engine
 * @param perm Vector of positions to shuffle.
 * @details Seeds `std::mt19937` from `std::random_device` then
 * delegates to `shufflePerm(perm, rng)`. Production path used by
 * Engine::setShuffleLocked.
 * @par Thread safety
 * No internal synchronization; caller must hold `queueMutex_`.
 * @see shufflePerm(std::vector<int64_t>&, std::mt19937&)
 */
inline void shufflePerm(std::vector<int64_t>& perm) {
    std::random_device rd;
    std::mt19937 gen(rd());
    shufflePerm(perm, gen);
}

} // namespace caudio::engine::detail
