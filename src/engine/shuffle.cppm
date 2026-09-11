module;
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

export module caudio.engine:shuffle;

import :types;

export namespace caudio::engine::detail {

inline void shufflePerm(std::vector<int64_t>& perm, std::mt19937& rng) {
    std::ranges::shuffle(perm, rng);
}

inline void shufflePerm(std::vector<int64_t>& perm) {
    std::random_device rd;
    std::mt19937 gen(rd());
    shufflePerm(perm, gen);
}

} // namespace caudio::engine::detail




