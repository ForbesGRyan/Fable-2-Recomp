#pragma once

// Draw counts per (vertex shader hash, kind) for the periodic coverage log
// (pure, std only). The kind is caller-defined (the capture uses the skip
// reason). Fixed capacity: pairs past it are counted in Other().

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace fable2::native::capture {

template <size_t N>
class ShaderTally {
 public:
  struct Entry {
    uint64_t hash = 0;
    uint8_t kind = 0;
    uint32_t count = 0;
  };

  void Add(uint64_t hash, uint8_t kind) {
    for (size_t i = 0; i < n_; ++i) {
      if (e_[i].hash == hash && e_[i].kind == kind) {
        ++e_[i].count;
        return;
      }
    }
    if (n_ == N) {
      ++other_;
      return;
    }
    e_[n_++] = Entry{hash, kind, 1};
  }

  // Up to `k` entries of `kind` into `out`, by count descending (ties: lower
  // hash first). Returns how many were written.
  size_t Top(uint8_t kind, Entry* out, size_t k) const {
    size_t m = 0;
    Entry sel[N];
    for (size_t i = 0; i < n_; ++i) {
      if (e_[i].kind == kind) sel[m++] = e_[i];
    }
    std::sort(sel, sel + m, [](const Entry& a, const Entry& b) {
      return a.count != b.count ? a.count > b.count : a.hash < b.hash;
    });
    m = std::min(m, k);
    std::copy(sel, sel + m, out);
    return m;
  }

  // Draws of `kind` in total (pairs past the capacity excluded).
  uint32_t Total(uint8_t kind) const {
    uint32_t t = 0;
    for (size_t i = 0; i < n_; ++i) t += e_[i].kind == kind ? e_[i].count : 0;
    return t;
  }

  uint32_t Other() const { return other_; }
  size_t size() const { return n_; }

  void Clear() {
    n_ = 0;
    other_ = 0;
  }

 private:
  Entry e_[N] = {};
  size_t n_ = 0;
  uint32_t other_ = 0;
};

}  // namespace fable2::native::capture
