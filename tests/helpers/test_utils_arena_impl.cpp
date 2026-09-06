#include <cstdint>
import caudio.utils;

namespace caudio::utils::test {

bool arena_basic() {
    Arena ar{4096};
    if (ar.capacity() == 0)
        return false;
    void* p1 = ar.allocate(100, 8);
    if (!p1)
        return false;
    void* p2 = ar.allocate(200, 16);
    if (!p2)
        return false;
    if ((reinterpret_cast<std::uintptr_t>(p2) & 15) != 0)
        return false;
    return true;
}

bool arena_reset() {
    Arena ar{64 * 1024};
    void* p1 = ar.allocate(100, 8);
    if (!p1)
        return false;
    void* p2 = ar.allocate(200, 16);
    if (!p2)
        return false;
    ar.reset();
    void* p3 = ar.allocate(50, 8);
    if (p3 != p1)
        return false;
    return true;
}

bool arena_exhaustion() {
    Arena ar{1024};
    void* p1 = ar.allocate(512, 8);
    if (!p1)
        return false;
    void* p2 = ar.allocate(512, 8);
    if (!p2)
        return false;
    void* p3 = ar.allocate(1, 8);
    if (p3 != nullptr)
        return false;
    void* p4 = ar.allocate(2048, 8);
    if (p4 != nullptr)
        return false;
    ar.reset();
    void* p5 = ar.allocate(1024, 1);
    if (p5 != p1)
        return false;
    return true;
}

bool arena_alignment() {
    std::size_t aligns[] = {1, 2, 4, 8, 16, 32, 64};
    for (auto al : aligns) {
        Arena ar{4096};
        for (int i = 0; i < 10; ++i) {
            void* p = ar.allocate(7, al);
            if (!p)
                return false;
            if ((reinterpret_cast<std::uintptr_t>(p) & (al - 1)) != 0)
                return false;
        }
    }
    return true;
}

bool arena_zero_cap() {
    Arena ar{0};
    void* p = ar.allocate(10, 8);
    if (p != nullptr)
        return false;
    return true;
}

bool arena_64b_align() {
    Arena ar{64 * 1024};
    void* p = ar.allocate(1, 64);
    if (!p)
        return false;
    if ((reinterpret_cast<std::uintptr_t>(p) & 63) != 0)
        return false;
    return true;
}

bool arena_interleaved() {
    Arena ar{4096};
    void* q1 = ar.allocate(100, 8);
    void* q2 = ar.allocate(200, 16);
    if (!q1 || !q2)
        return false;
    if ((reinterpret_cast<std::uintptr_t>(q2) & 15) != 0)
        return false;
    void* q3 = ar.allocate(13, 32);
    if (!q3)
        return false;
    if ((reinterpret_cast<std::uintptr_t>(q3) & 31) != 0)
        return false;
    void* q4 = ar.allocate(1, 1);
    if (!q4)
        return false;
    return true;
}

bool arena_default_64k() {
    Arena ar;
    if (ar.capacity() != 64 * 1024)
        return false;
    if (ar.remaining() != 64 * 1024)
        return false;
    void* p = ar.allocate(64 * 1024, 1);
    if (!p)
        return false;
    if (ar.allocate(1, 1) != nullptr)
        return false;
    return true;
}

bool arena_zero_alloc() {
    Arena ar{1024};
    void* p0 = ar.allocate(0, 8);
    if (!p0)
        return false;
    return true;
}

} // namespace caudio::utils::test
