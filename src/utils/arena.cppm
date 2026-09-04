module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <algorithm>

export module caudio.utils:arena;

export namespace caudio::utils {

class Arena {
 public:
  static constexpr std::size_t kDefaultCapacity = 64 * 1024;
  static constexpr std::size_t kAlign = 64;

  explicit Arena(std::size_t capacity = kDefaultCapacity) : capacity_(capacity) {
    if (capacity_ > storage_.size()) {
      // For capacities larger than default, we still cap storage to default
      // The spec says local 64K arena (C0) — clamp to 64K
      capacity_ = storage_.size();
    }
    // Ensure base is 64B aligned
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(storage_.data());
    std::uintptr_t aligned = (addr + (kAlign - 1)) & ~(std::uintptr_t)(kAlign - 1);
    base_ = reinterpret_cast<std::byte*>(aligned);
    // Adjust capacity if alignment consumed bytes
    std::size_t offset = static_cast<std::size_t>(base_ - storage_.data());
    if (offset + capacity_ > storage_.size()) {
      capacity_ = storage_.size() - offset;
    }
    if (capacity == 0) {
      base_ = nullptr;
      capacity_ = 0;
    }
  }

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  Arena(Arena&&) = delete;
  Arena& operator=(Arena&&) = delete;

  [[nodiscard]] void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t)) noexcept {
    if (n == 0) {
      if (!base_) return nullptr;
      std::size_t off = alignUp(offset_, align);
      if (off > capacity_) return nullptr;
      return static_cast<void*>(base_ + off);
    }
    if (!base_) return nullptr;
    std::size_t off = alignUp(offset_, align);
    if (off > capacity_) return nullptr;
    if (n > capacity_ - off) return nullptr;
    void* p = base_ + off;
    offset_ = off + n;
    return p;
  }

  template <typename T>
  [[nodiscard]] T* allocateArray(std::size_t count) noexcept {
    void* p = allocate(count * sizeof(T), alignof(T));
    return static_cast<T*>(p);
  }

  void reset() noexcept { offset_ = 0; }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::size_t used() const noexcept { return offset_; }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return offset_ <= capacity_ ? capacity_ - offset_ : 0;
  }
  [[nodiscard]] bool empty() const noexcept { return offset_ == 0; }

 private:
  static std::size_t alignUp(std::size_t v, std::size_t align) noexcept {
    if (align == 0) return v;
    if ((align & (align - 1)) == 0) {
      return (v + align - 1) & ~(align - 1);
    }
    return ((v + align - 1) / align) * align;
  }

  // 64K + 64 for alignment slack
  alignas(kAlign) std::array<std::byte, kDefaultCapacity + kAlign> storage_{};
  std::byte* base_{nullptr};
  std::size_t capacity_{0};
  std::size_t offset_{0};
};

} // namespace caudio::utils
