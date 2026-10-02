#pragma once

// Pure helpers behind guest_read.h: a small readability cache of guest memory
// regions that clears itself when the global generation changes (bumped once
// per guest frame, so the cache is per-frame), and the physical-window bounds
// check.

#include <cstdint>

namespace fable2::native::capture {

// [begin, end) with uniform readability, as reported by a probe.
struct ReadRegion {
  uint64_t begin = 0;
  uint64_t end = 0;
  bool readable = false;
};

class RegionReadCache {
 public:
  // True if every byte of [addr, addr + size) (at least `addr` for size 0)
  // lies in readable regions. `probe(a)` returns the region containing `a`;
  // one probe covers a whole region, so a long range costs one probe per
  // region it crosses. A probe that does not contain `a` counts as unreadable.
  // The cache is dropped whenever `generation` differs from the one it was
  // filled under. (This does not remove the inherent query-then-read race.)
  template <typename Probe>
  bool RangeReadable(uint64_t addr, uint64_t size, uint32_t generation, Probe&& probe) {
    if (generation != generation_) {
      for (Entry& e : entries_) e.valid = false;
      next_ = 0;
      generation_ = generation;
    }
    const uint64_t last = addr + (size ? size - 1 : 0);
    uint64_t at = addr;
    for (;;) {
      const ReadRegion* r = Find(at);
      if (!r) {
        const ReadRegion got = probe(at);
        if (got.begin > at || got.end <= at) return false;
        entries_[next_] = {got, true};
        r = &entries_[next_].region;
        next_ = (next_ + 1) & 63;
      }
      if (!r->readable) return false;
      if (r->end > last) return true;
      at = r->end;
    }
  }

 private:
  const ReadRegion* Find(uint64_t a) const {
    for (const Entry& e : entries_) {
      if (e.valid && e.region.begin <= a && a < e.region.end) return &e.region;
    }
    return nullptr;
  }

  struct Entry {
    ReadRegion region;
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
