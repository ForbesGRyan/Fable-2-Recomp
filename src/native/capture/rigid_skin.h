#pragma once

// Rigid (one bone per vertex) skinning for the clay pass (pure: no SDK/GPU
// deps). Shader A1F7E9885EC466DF reads a bone index from each vertex and
// three half4 rows of that bone from a second stream, transforms the position
// by the bone (which also carries the object placement) and then applies
// c0..c3 (frame-map section 9). Each output component k is
// dot(row_k, position), with the rows and the position as the shader's
// registers hold them (w = 1).

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "position_decode.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

struct RigidSkin {
  bool active = false;
  // Bone index: an 8_8_8_8 component of the 32-bit word at this byte offset in
  // the position stream's vertex; the GPU reads the word big-endian under
  // fetch endian 8in32 (2) and little-endian under none (0). Component x is
  // bits 0-7 (index_shift 0), y bits 8-15, and so on.
  uint32_t index_offset_bytes = 0;
  uint32_t index_shift = 0;
  uint32_t index_endian = 0;
  // Bone stream (GPU physical) and its rows; rows[k].stride_bytes is the bone size.
  uint32_t palette_addr = 0;
  uint32_t palette_size = 0;
  uint32_t bone_stride = 0;
  PosLayout rows[3];
};

// vs-transforms.json "skin" (FABLE2_VS_SKIN): fetches by their index in
// DecodeVertexFetches order. The bone index is destination component
// `index_component` (0-3 = x-w) of fetch `index_fetch`; the bone's three rows
// are fetches `row_fetch[0..2]`.
struct SkinSpec {
  int index_fetch;
  uint32_t index_component;
  int row_fetch[3];
};

// The skin layout of a shader: the index must be an integer 8_8_8_8 component
// read from the position's stream (same slot and stride, so it is per vertex),
// and the three rows full or mini fetches of one other stream with one stride.
// Endians are applied by the caller from the streams' fetch constants
// (index_endian, ApplyFetchEndian on each row). *bone_slot receives the rows'
// fetch slot.
inline bool SelectSkin(const std::vector<VertexFetch>& fetches, const SkinSpec& spec,
                       const PosLayout& pos, RigidSkin* out, uint32_t* bone_slot) {
  auto valid = [&](int i) { return i >= 0 && size_t(i) < fetches.size(); };
  if (!valid(spec.index_fetch) || spec.index_component > 3) return false;
  const VertexFetch& ix = fetches[size_t(spec.index_fetch)];
  if (ix.fetch_slot != pos.fetch_slot || ix.stride_dwords * 4 != pos.stride_bytes) return false;
  if (ix.format != 6 || ix.normalized || ix.offset_dwords < 0) return false;  // 8_8_8_8 integer
  const uint32_t src = (ix.dst_swizzle >> (3 * spec.index_component)) & 7;
  if (src > 3) return false;  // component not fetched
  RigidSkin s;
  s.index_offset_bytes = uint32_t(ix.offset_dwords) * 4;
  s.index_shift = 8 * src;
  for (int k = 0; k < 3; ++k) {
    if (!valid(spec.row_fetch[k])) return false;
    if (!SelectPosition(fetches, spec.row_fetch[k], &s.rows[k])) return false;
    if (s.rows[k].fetch_slot != s.rows[0].fetch_slot ||
        s.rows[k].stride_bytes != s.rows[0].stride_bytes) {
      return false;
    }
  }
  s.bone_stride = s.rows[0].stride_bytes;
  s.active = true;
  *bone_slot = s.rows[0].fetch_slot;
  *out = s;
  return true;
}

// The bone index of `vertex`, or false if the word lies outside the stream or
// the endian is not handled.
inline bool BoneIndex(const uint8_t* vb, size_t vb_size, uint32_t vertex_byte_offset,
                      const RigidSkin& s, uint32_t* out) {
  const uint64_t at = uint64_t(vertex_byte_offset) + s.index_offset_bytes;
  if (!vb || at + 4 > vb_size || s.index_shift > 24) return false;
  const uint8_t* p = vb + at;
  uint32_t word;
  if (s.index_endian == 2) {
    word = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  } else if (s.index_endian == 0) {
    word = (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
  } else {
    return false;
  }
  *out = (word >> s.index_shift) & 0xFF;
  return true;
}

// Replaces positions[0, count) of vertices first_vertex.. (already decoded
// with w = 1) by their bone-transformed positions. A vertex whose bone lies
// past the palette (another mesh's vertex in a shared stream) becomes NaN,
// which the clay shader culls. Fails if an index word cannot be read or the
// palette holds no whole bone.
inline bool SkinPositions(const uint8_t* vb, size_t vb_size, const uint8_t* palette,
                          size_t palette_size, const RigidSkin& s, uint32_t stride,
                          uint32_t first_vertex, uint32_t count, Float4* positions) {
  if (!s.active || stride == 0 || s.bone_stride == 0) return false;
  // Every bone's rows, decoded once (the index is 8 bits: at most 256 bones).
  std::vector<Float4> bones;
  for (uint32_t b = 0; b < 256; ++b) {
    Float4 r[3];
    bool ok = true;
    for (int k = 0; k < 3 && ok; ++k) {
      ok = DecodePositions(palette, palette_size, s.rows[k], b, 1, &r[k]);
    }
    if (!ok) break;
    bones.insert(bones.end(), {r[0], r[1], r[2]});
  }
  if (bones.empty()) return false;
  const uint32_t n = uint32_t(bones.size() / 3);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t bone = 0;
    if (!BoneIndex(vb, vb_size, (first_vertex + i) * stride, s, &bone)) return false;
    if (bone >= n) {
      positions[i] = {nan, nan, nan, 1.0f};
      continue;
    }
    const Float4* r = &bones[size_t(bone) * 3];
    const Float4 p = positions[i];
    auto dot = [&](const Float4& a) { return a.x * p.x + a.y * p.y + a.z * p.z + a.w * p.w; };
    positions[i] = {dot(r[0]), dot(r[1]), dot(r[2]), 1.0f};
  }
  return true;
}

}  // namespace fable2::native::capture
