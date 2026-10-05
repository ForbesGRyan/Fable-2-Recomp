#pragma once

// Weighted bone skinning (1-4 influences per vertex) for the clay pass (pure:
// no SDK/GPU deps). A skinned shader reads bone indices (and, for blended
// meshes, weights) from each vertex and three half4 rows per bone from a
// second stream. The blended matrix M_k = sum_j(w_j * row_k(bone_j)) is
// applied to the position (w = 1) and then c0..c3 apply; the bones are near
// identity in gameplay; the object placement is in c0..c3 (frame-map section
// 9). Each output component k is dot(M_k, position), with the rows and the
// position as the shader's registers hold them. Rigid shaders (A1F7E9885EC466DF)
// are the one-bone, weight-1 form.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "position_decode.h"
#include "vfetch_decode.h"

namespace fable2::native::capture {

struct BoneSkin {
  bool active = false;
  uint32_t bones = 1;  // influences per vertex, 1..4
  // Bone indices: 8_8_8_8 components of the 32-bit word at this byte offset in
  // the position stream's vertex (big-endian word under fetch endian 8in32,
  // little-endian under none); index_shift[k] selects influence k's byte.
  uint32_t index_offset_bytes = 0;
  uint32_t index_shift[4] = {};
  uint32_t index_endian = 0;
  // Weights: normalized 8_8_8_8 word at weight_offset_bytes, same endian;
  // weighted == false means one bone with weight 1.
  bool weighted = false;
  uint32_t weight_offset_bytes = 0;
  uint32_t weight_shift[4] = {};
  // Bone stream (GPU physical) and its rows; rows[k].stride_bytes is the bone size.
  uint32_t palette_addr = 0;
  uint32_t palette_size = 0;
  uint32_t bone_stride = 0;
  PosLayout rows[3];
};

// vs-transforms.json "skin" (FABLE2_VS_SKIN). Fetches by DecodeVertexFetches
// index. Influence k uses register component index_component[k] of the index
// fetch and weight_component[k] of the weight fetch; weight_fetch -1 = rigid.
// row_swizzle[k] (0 = the fetch's own) replaces row k's destination swizzle.
struct SkinSpec {
  int index_fetch;
  int weight_fetch;
  int row_fetch[3];
  uint32_t row_swizzle[3];
  uint32_t bones;
  uint32_t index_component[4];
  uint32_t weight_component[4];
};

// The skin layout of a shader: the indices (and weights) must be 8_8_8_8
// components read from the position's stream (same slot and stride, so they
// are per vertex), and the three rows full or mini fetches of one other stream
// with one stride. Endians are applied by the caller from the streams' fetch
// constants (index_endian, ApplyFetchEndian on each row). *bone_slot receives
// the rows' fetch slot.
inline bool SelectSkin(const std::vector<VertexFetch>& fetches, const SkinSpec& spec, const PosLayout& pos,
                       BoneSkin* out, uint32_t* bone_slot) {
  auto valid = [&](int i) { return i >= 0 && size_t(i) < fetches.size(); };
  if (!valid(spec.index_fetch) || spec.bones < 1 || spec.bones > 4) return false;
  const bool weighted = spec.weight_fetch >= 0;
  if (!weighted && spec.bones != 1) return false;
  // A per-vertex 8_8_8_8 word of the position's stream; returns its byte
  // offset and the shift of register component `comp`.
  auto word = [&](const VertexFetch& f, bool normalized, uint32_t comp, uint32_t* offset, uint32_t* shift) {
    if (f.fetch_slot != pos.fetch_slot || f.stride_dwords * 4 != pos.stride_bytes) return false;
    if (f.format != 6 || f.normalized != normalized || f.offset_dwords < 0 || comp > 3) return false;
    const uint32_t src = (f.dst_swizzle >> (3 * comp)) & 7;
    if (src > 3) return false;  // component not fetched
    *offset = uint32_t(f.offset_dwords) * 4;
    *shift = 8 * src;
    return true;
  };
  BoneSkin s;
  s.bones = spec.bones;
  s.weighted = weighted;
  for (uint32_t k = 0; k < spec.bones; ++k) {
    if (!word(fetches[size_t(spec.index_fetch)], false, spec.index_component[k], &s.index_offset_bytes,
              &s.index_shift[k])) {
      return false;
    }
    if (weighted && (!valid(spec.weight_fetch) ||
                     !word(fetches[size_t(spec.weight_fetch)], true, spec.weight_component[k],
                           &s.weight_offset_bytes, &s.weight_shift[k]))) {
      return false;
    }
  }
  for (int k = 0; k < 3; ++k) {
    if (!valid(spec.row_fetch[k])) return false;
    if (!SelectPosition(fetches, spec.row_fetch[k], &s.rows[k], spec.row_swizzle[k])) return false;
    if (s.rows[k].fetch_slot != s.rows[0].fetch_slot || s.rows[k].stride_bytes != s.rows[0].stride_bytes) {
      return false;
    }
  }
  s.bone_stride = s.rows[0].stride_bytes;
  s.active = true;
  *bone_slot = s.rows[0].fetch_slot;
  *out = s;
  return true;
}

namespace detail {
// The 32-bit word at `at` as the GPU reads it, or false if outside the stream
// or the endian is not handled.
inline bool SkinWord(const uint8_t* vb, size_t vb_size, uint64_t at, uint32_t endian, uint32_t* out) {
  if (!vb || at + 4 > vb_size) return false;
  const uint8_t* p = vb + at;
  if (endian == 2) {
    *out = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  } else if (endian == 0) {
    *out = (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
  } else {
    return false;
  }
  return true;
}
}  // namespace detail

// The first influence's bone index of `vertex`, or false if the word lies
// outside the stream or the endian is not handled.
inline bool BoneIndex(const uint8_t* vb, size_t vb_size, uint32_t vertex_byte_offset, const BoneSkin& s,
                      uint32_t* out) {
  uint32_t word = 0;
  if (s.index_shift[0] > 24 ||
      !detail::SkinWord(vb, vb_size, uint64_t(vertex_byte_offset) + s.index_offset_bytes, s.index_endian, &word)) {
    return false;
  }
  *out = (word >> s.index_shift[0]) & 0xFF;
  return true;
}

// Replaces positions[0, count) of vertices first_vertex.. (already decoded
// with w = 1) by their skinned positions. A vertex with a nonzero-weight bone
// past the palette (another mesh's vertex in a shared stream), or with every
// weight zero, becomes NaN, which the clay shader culls; an influence of
// weight zero may name any bone. Fails if an index or weight word cannot be
// read or the palette holds no whole bone.
inline bool SkinPositions(const uint8_t* vb, size_t vb_size, const uint8_t* palette, size_t palette_size,
                          const BoneSkin& s, uint32_t stride, uint32_t first_vertex, uint32_t count,
                          Float4* positions) {
  if (!s.active || stride == 0 || s.bone_stride == 0 || s.bones < 1 || s.bones > 4) return false;
  // Every bone's rows, decoded once (the index is 8 bits: at most 256 bones).
  std::vector<Float4> bones;
  for (uint32_t b = 0; b < 256; ++b) {
    Float4 r[3];
    bool ok = true;
    for (int k = 0; k < 3 && ok; ++k) ok = DecodePositions(palette, palette_size, s.rows[k], b, 1, &r[k]);
    if (!ok) break;
    bones.insert(bones.end(), {r[0], r[1], r[2]});
  }
  if (bones.empty()) return false;
  const uint32_t n = uint32_t(bones.size() / 3);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t at = uint64_t(first_vertex + i) * stride;
    uint32_t iw = 0, ww = 0;
    if (!detail::SkinWord(vb, vb_size, at + s.index_offset_bytes, s.index_endian, &iw)) return false;
    if (s.weighted && !detail::SkinWord(vb, vb_size, at + s.weight_offset_bytes, s.index_endian, &ww)) return false;
    float m[3][4] = {};
    bool any = false, bad = false;
    for (uint32_t k = 0; k < s.bones; ++k) {
      const float w = s.weighted ? float((ww >> s.weight_shift[k]) & 0xFF) / 255.0f : 1.0f;
      if (w == 0.0f) continue;  // an unused influence may name any bone
      const uint32_t bone = (iw >> s.index_shift[k]) & 0xFF;
      if (bone >= n) { bad = true; break; }
      const Float4* r = &bones[size_t(bone) * 3];
      for (int row = 0; row < 3; ++row) {
        m[row][0] += w * r[row].x; m[row][1] += w * r[row].y; m[row][2] += w * r[row].z; m[row][3] += w * r[row].w;
      }
      any = true;
    }
    if (bad || !any) {
      positions[i] = {nan, nan, nan, 1.0f};
      continue;
    }
    const Float4 p = positions[i];
    auto dot = [&](const float* a) { return a[0] * p.x + a[1] * p.y + a[2] * p.z + a[3] * p.w; };
    positions[i] = {dot(m[0]), dot(m[1]), dot(m[2]), 1.0f};
  }
  return true;
}

}  // namespace fable2::native::capture
