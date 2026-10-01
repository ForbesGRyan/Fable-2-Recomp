#pragma once

// Pure helpers behind guest_read.h: a small page-readability cache that clears
// itself when the global generation changes (bumped once per guest frame, so
// the cache is per-frame), and the physical-window bounds check.

#include <cstdint>

namespace fable2::native::capture {

class PageReadCache {
 public:
  // Returns the cached readability of `page`, calling `probe(page)` on a miss.
  // The cache is dropped whenever `generation` differs from the one it was
  // filled under. (This does not remove the inherent query-then-read race.)
  template <typename Probe>
  bool Query(uintptr_t page, uint32_t generation, Probe&& probe) {
    if (generation != generation_) {
      for (Entry& e : entries_) e.valid = false;
      next_ = 0;
      generation_ = generation;
    }
    for (const Entry& e : entries_) {
      if (e.valid && e.page == page) return e.readable;
    }
    const bool readable = probe(page);
    entries_[next_] = {page, readable, true};
    next_ = (next_ + 1) & 63;
    return readable;
  }

 private:
  struct Entry {
    uintptr_t page = 0;
    bool readable = false;
    bool valid = false;
  };
  Entry entries_[64];
  uint32_t next_ = 0;
  uint32_t generation_ = 0;
};

// True if [addr & 0x1FFFFFFF, +size) stays inside the 512 MB physical window.
inline bool PhysicalRangeInWindow(uint32_t guest_physical, uint32_t size) {
  return uint64_t(guest_physical & 0x1FFFFFFF) + uint64_t(size) <= 0x20000000ull;
}

}  // namespace fable2::native::capture
