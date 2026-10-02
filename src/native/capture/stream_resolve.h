#pragma once

// Index scan and vertex stream resolution for draw capture (pure: no SDK/GPU
// deps). capture.cpp reads the guest objects and hands the words here.

#include <algorithm>
#include <cstdint>

#include "draw_record.h"
#include "xdk_layout.h"

namespace fable2::native::capture {

// Index buffer object fields (xdk_layout.h).
struct IbView {
  uint32_t common = 0;
  uint32_t addr = 0;  // GPU physical
  uint32_t size = 0;  // bytes
  uint32_t endian = 0;
  bool index32 = false;
};

struct IndexScan {
  int64_t max_index = -1;  // largest non-reset index, -1 if none
  uint32_t restarts = 0;
  uint32_t n_first = 0;  // first non-reset indices, for discovery positions
  uint32_t first[64] = {};
};

// Indices [start, start + count) are a non-empty, sane-sized range inside the
// buffer.
inline bool IndexRangeFits(const IbView& ib, uint32_t start, uint32_t count) {
  const uint64_t isize = ib.index32 ? 4 : 2;
  return count && count <= kMaxDrawCount && (uint64_t(start) + count) * isize <= ib.size;
}

// Scans `count` index words at `idx` as the GPU fetches them: little-endian
// words with the buffer's endian swap applied (1 8in16, 2 8in32, 3 16in32).
// The all-ones reset index cuts strips and is not a vertex.
inline void ScanIndexWords(const uint8_t* idx, uint32_t count, bool index32, uint32_t endian,
                           IndexScan* out) {
  const uint32_t isize = index32 ? 4 : 2;
  const uint32_t reset = index32 ? 0xFFFFFFFFu : 0xFFFFu;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = idx + isize * i;
    uint32_t v = index32 ? uint32_t(e[0]) | uint32_t(e[1]) << 8 | uint32_t(e[2]) << 16 |
                               uint32_t(e[3]) << 24
                         : uint32_t(e[0]) | uint32_t(e[1]) << 8;
    if (endian == 1) {
      v = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
    } else if (endian == 2) {
      v = (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
    } else if (endian == 3) {
      v = (v >> 16) | (v << 16);
    }
    if (v == reset) {
      ++out->restarts;
      continue;
    }
    out->max_index = std::max<int64_t>(out->max_index, v);
    if (out->n_first < 64) out->first[out->n_first++] = v;
  }
}

// The vertex buffer feeding a fetch slot: stream i feeds slot 95 - i; its
// object's fetch constant (base) plus the stream offset must equal the device
// shadow (fc_match).
struct StreamView {
  uint32_t stream = 0;
  uint32_t obj = 0;
  uint32_t base = 0;  // GPU physical, without the stream offset
  uint32_t size = 0;  // bytes from base
  uint32_t offset = 0;
  uint32_t fc0 = 0, fc1 = 0;
  bool fc_match = false;
};

// The stream bound to vertex fetch slot `fetch_slot`, false if the slot is not
// a stream slot (xdk_layout.h kStreamFetchSlotBase, kMaxStreams).
inline bool StreamForFetchSlot(uint32_t fetch_slot, uint32_t* stream) {
  if (fetch_slot > xdk::kStreamFetchSlotBase ||
      xdk::kStreamFetchSlotBase - fetch_slot >= xdk::kMaxStreams) {
    return false;
  }
  *stream = xdk::kStreamFetchSlotBase - fetch_slot;
  return true;
}

// Fills base, size, offset and fc_match from the vertex buffer object's fetch
// dwords (d0, d1: object dwords xdk::kVbFetchDword, +1) and the device shadow's
// fetch constant (fc0, fc1). SetStreamSource writes fc0 = GpuAddress(d0 +
// offset), fc1 = d1 - offset (xdk_layout.h kVbFetchDword).
inline void StreamFromFetch(uint32_t d0, uint32_t d1, uint32_t fc0, uint32_t fc1, StreamView* v) {
  v->base = xdk::GpuAddress(d0 & ~3u);
  v->size = ((d1 >> 2) & 0xFFFFFF) * 4;
  v->fc0 = fc0;
  v->fc1 = fc1;
  v->offset = (fc0 & ~3u) - v->base;
  v->fc_match = (fc0 & 3) == 3 && v->offset < v->size && fc1 == d1 - v->offset;
}

}  // namespace fable2::native::capture
