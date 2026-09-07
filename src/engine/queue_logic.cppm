module;
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

export module caudio.engine:queue_logic;

import :types;

export namespace caudio::engine::detail {

inline void shufflePerm(std::vector<int64_t>& perm, std::mt19937& rng) {
    if (perm.size() <= 1)
        return;
    for (size_t i = perm.size() - 1; i > 0; --i) {
        std::uniform_int_distribution<size_t> dist(0, i);
        size_t j = dist(rng);
        std::swap(perm[i], perm[j]);
    }
}

inline void shufflePerm(std::vector<int64_t>& perm) {
    std::random_device rd;
    std::mt19937 gen(rd());
    shufflePerm(perm, gen);
}

} // namespace caudio::engine::detail
