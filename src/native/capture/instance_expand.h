#pragma once

// Instanced meshes (pure: no SDK/GPU deps). Fable 2's instancing shaders
// (0x8123C16DBF583F92 and relatives, frame-map section 12) draw many copies
// of a small mesh in one indexed draw. The guest index encodes both the copy
// and the mesh vertex:
//   t      = trunc((index + bias) * inv_count)
//   copy   = t + trunc(first)
//   vertex = index + trunc(-(count * t))
// Each copy has three rows in a per-instance stream; a mesh vertex p (w = 1)
// becomes (dot(row0, p), dot(row1, p), dot(row2, p)) + offset. These helpers
// build that as one flat stream indexed by the guest index, so the index
// buffer and the clay shaders are used unchanged.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "position_decode.h"
#include "uv_decode.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

inline constexpr uint32_t kMaxInstanceCopies = 65536;

struct InstanceSet {
  bool active = false;
  uint32_t rows_addr = 0;  // per-instance stream, GPU physical
  uint32_t rows_size = 0;
  PosLayout rows[3];       // rows[k].stride_bytes = bytes per copy
  float inv_count = 0.0f, count = 0.0f, first = 0.0f, bias = 0.0f;
  float offset[3] = {};
  uint32_t flat_count = 0;  // positions to build: largest index + base vertex + 1
};

// vs-transforms.json "instance" (FABLE2_VS_INSTANCE): row fetches by their
// index in DecodeVertexFetches order, a destination swizzle override per row
// (0 = the fetch's own), vertex-constant references as register * 4 +
// component, the shader's literal rounding bias, and the offset's x reference
// (y and z follow).
struct InstanceSpec {
  int row_fetch[3];
  uint32_t row_swizzle[3];
  int32_t inv_count_ref, count_ref, first_ref;
  float bias;
  int32_t offset_ref;
};

// The three rows must be position-format fetches of one stream that is not
// the mesh's. Endians are applied by the caller from the stream's fetch
// constant (ApplyFetchEndian on each row). *rows_slot receives the stream's
// fetch slot; the constants are filled by the caller.
inline bool SelectInstanceRows(const std::vector<VertexFetch>& fetches, const InstanceSpec& spec,
                               uint32_t mesh_slot, InstanceSet* out, uint32_t* rows_slot) {
  InstanceSet s;
  for (int k = 0; k < 3; ++k) {
    if (spec.row_fetch[k] < 0 || size_t(spec.row_fetch[k]) >= fetches.size()) return false;
    if (!SelectPosition(fetches, spec.row_fetch[k], &s.rows[k], spec.row_swizzle[k])) return false;
    if (s.rows[k].fetch_slot != s.rows[0].fetch_slot || s.rows[k].stride_bytes != s.rows[0].stride_bytes) {
      return false;
    }
  }
  if (s.rows[0].fetch_slot == mesh_slot) return false;
  s.bias = spec.bias;
  s.active = true;
  *rows_slot = s.rows[0].fetch_slot;
  *out = s;
  return true;
}

inline bool InstanceIndex(const InstanceSet& s, uint32_t index, uint32_t* copy, uint32_t* vertex) {
  const float t = std::trunc((float(index) + s.bias) * s.inv_count);
  const float c = t + std::trunc(s.first);
  const float v = float(index) + std::trunc(-(s.count * t));
  if (!std::isfinite(c) || !std::isfinite(v) || c < 0.0f || v < 0.0f ||
      c >= float(kMaxInstanceCopies) || v >= 16777216.0f) {
    return false;
  }
  *copy = uint32_t(c);
  *vertex = uint32_t(v);
  return true;
}

// Whole copies in the attached stream.
inline uint32_t InstanceCopies(const InstanceSet& s) {
  uint32_t end = 0;  // bytes one copy's rows need
  for (const PosLayout& r : s.rows) {
    const uint32_t bytes = PositionBytes(r.format);
    if (bytes == 0 || r.stride_bytes == 0) return 0;
    if (r.offset_bytes + bytes > end) end = r.offset_bytes + bytes;
  }
  if (s.rows_size < end) return 0;
  return (s.rows_size - end) / s.rows[0].stride_bytes + 1;
}

// Capture-side check before a draw is recorded: sane constants, and every
// index up to max_index stays inside the copies and mesh vertices that exist.
inline bool InstanceBoundsOk(const InstanceSet& s, uint32_t max_index, uint32_t copies, uint32_t vertices) {
  if (!std::isfinite(s.count) || !std::isfinite(s.inv_count) || !std::isfinite(s.first)) return false;
  if (s.count < 1.0f || s.count != std::trunc(s.count) || s.first < 0.0f) return false;
  if (std::fabs(s.count * s.inv_count - 1.0f) > 0.01f) return false;
  if (uint64_t(s.count) > vertices) return false;
  uint32_t copy = 0, vertex = 0;
  if (!InstanceIndex(s, max_index, &copy, &vertex)) return false;  // copy grows with the index
  return copy < copies;
}

inline bool ExpandInstances(const Float4* mesh, uint32_t mesh_count, const uint8_t* rows, size_t rows_size,
                            const InstanceSet& s, uint32_t flat_count, Float4* out) {
  if (!mesh || !rows || !out || flat_count == 0) return false;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::vector<Float4> decoded;   // three rows per copy, decoded on first use
  std::vector<uint8_t> state;    // 0 unknown, 1 decoded, 2 past the stream
  for (uint32_t i = 0; i < flat_count; ++i) {
    uint32_t copy = 0, vertex = 0;
    out[i] = {nan, nan, nan, 1.0f};
    if (!InstanceIndex(s, i, &copy, &vertex) || vertex >= mesh_count) continue;
    if (copy >= state.size()) {
      state.resize(size_t(copy) + 1, 0);
      decoded.resize((size_t(copy) + 1) * 3);
    }
    if (state[copy] == 0) {
      bool ok = true;
      for (int k = 0; k < 3 && ok; ++k) {
        ok = DecodePositions(rows, rows_size, s.rows[k], copy, 1, &decoded[size_t(copy) * 3 + k]);
      }
      state[copy] = ok ? 1 : 2;
    }
    if (state[copy] != 1) continue;
    const Float4* r = &decoded[size_t(copy) * 3];
    const Float4 p = mesh[vertex];
    auto dot = [&](const Float4& a) { return a.x * p.x + a.y * p.y + a.z * p.z + a.w * p.w; };
    out[i] = {dot(r[0]) + s.offset[0], dot(r[1]) + s.offset[1], dot(r[2]) + s.offset[2], 1.0f};
  }
  return true;
}

inline void ExpandInstanceUvs(const Float2* mesh_uv, uint32_t mesh_count, const InstanceSet& s,
                              uint32_t flat_count, Float2* out) {
  for (uint32_t i = 0; i < flat_count; ++i) {
    uint32_t copy = 0, vertex = 0;
    out[i] = (InstanceIndex(s, i, &copy, &vertex) && vertex < mesh_count) ? mesh_uv[vertex] : Float2{0.0f, 0.0f};
  }
}

}  // namespace fable2::native::capture
