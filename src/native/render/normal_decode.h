#pragma once

// Vertex normals and the vertex colour stream for the light of the clay pass
// (pure: no SDK/GPU deps): the element decodes, the transform that keeps a
// normal perpendicular, and what the geometry cache builds from them.
//
// A normal buffer holds one unit normal per vertex of the draw's position
// buffer, in the space its positions are in (geometry_cache.h):
//   - a plain mesh: the stream's own space. The draw's world rotation
//     (LightInputs::world_rot) is applied when the draw is drawn, as the draw's
//     rows are to its positions: RotateNormal is the rule.
//   - a bone-skinned mesh: after the bones, through each vertex's blended bone
//     matrix as it is (bone_skin.h, BlendBones: the weights of the position),
//     which is what the game's shaders compute.
//   - an instanced draw: after the instance's rows, one normal per flat
//     position (instance_expand.h).
// A vertex whose normal has no direction (a zero or not finite element, a
// vertex that gets no position, a matrix that flattens it) holds (0, 0, 0):
// no direction is made up, and whoever lights with the buffer treats a zero
// normal as unlit by the sun, which is what the game's saturating dot
// products make of it (lighting-formulas.md, "Method", "Limits").
//
// Which components are the normal's x, y, z is NormalSourceComponents: source
// components by the element's format, never the fetch's destination swizzle
// (LightElement::swizzle says where the fetch puts a component in the shader's
// register, and the shaders differ in that: xyz_ and _xyz for the 10_11_11
// element, yxwz and yzxw for the halves).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../capture/bone_skin.h"
#include "../capture/instance_expand.h"
#include "../capture/material.h"
#include "../capture/position_decode.h"

namespace fable2::native::render {

struct Float3 {
  float x, y, z;
};
static_assert(sizeof(Float3) == 12);

namespace detail {
// v / |v| in `out`; false, with out zero, when v has no direction (zero length or not finite).
inline bool Normalize(const double v[3], float out[3]) {
  const double len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (!(len2 > 0.0) || !std::isfinite(len2)) {
    out[0] = out[1] = out[2] = 0.0f;
    return false;
  }
  const double inv = 1.0 / std::sqrt(len2);
  for (int k = 0; k < 3; ++k) out[k] = float(v[k] * inv);
  return true;
}
// The fetch's endian of an element (xenos::Endian): 2 is 8in32, the guest's big-endian dword.
inline uint32_t ElementEndian(const capture::LightElement& e) { return (e.flags >> capture::kLightElementEndianShift) & 3u; }
// Bit c: the fetch writes source component c into its register (destination swizzle, 3 bits a component).
inline uint32_t FetchedComponents(uint32_t swizzle) {
  uint32_t mask = 0;
  for (int c = 0; c < 4; ++c) {
    const uint32_t sel = (swizzle >> (3 * c)) & 7u;
    if (sel < 4) mask |= 1u << sel;
  }
  return mask;
}
}  // namespace detail

// The source components of a normal element (0 to 3: x to w of the GPU's
// fetch, before any destination swizzle) that are the normal's x, y, z; false
// for a format that holds no normal a reader decodes.
//   FMT_10_11_11 (16), the object, trunk and skinned meshes: source x, y, z.
//   FMT_16_16_16_16_FLOAT (32), the instanced plants' meshes: source y, x, w,
//     which under the stream's 8in32 pair swap are the first three halves in
//     memory.
// That is how every vertex shader with a light entry uses its element,
// whatever its fetch's swizzle: checked against the microcode by
// tools/xdk_sigmatch/light_check.py (its vertex drivers take the normal in
// this order) and, for the two skinned shaders, by skinned_normal_check.py of
// the lighting evidence; and on the streams of two captures, where the vector
// taken this way has unit length in every sampled vertex of all twelve shaders
// and agrees with its triangles' face normals, which no other order does
// (frame-map section 14, "Normals"; tests/native/test_normal_decode.cpp,
// FABLE2_NORMALS_BIN). The one place that says which components a normal is:
// a table entry that names them replaces the format here.
inline bool NormalSourceComponents(uint32_t format, uint8_t comp[3]) {
  if (format == capture::kVertexFormat_10_11_11) {
    comp[0] = 0; comp[1] = 1; comp[2] = 2;
    return true;
  }
  if (format == capture::kVertexFormat_16_16_16_16_FLOAT) {
    comp[0] = 1; comp[1] = 0; comp[2] = 3;
    return true;
  }
  return false;
}

// One normal element as the guest stores it (big-endian words: the fetch's
// endian 8in32), normalised. The element's source components:
//   FMT_10_11_11 (16): x = bits 0 to 10, y = bits 11 to 21, z = bits 22 to 31 of
//     the dword, each signed and divided by its positive maximum (1023, 1023,
//     511), at least -1: the SDK's k_10_11_11 with a signed, normalised fetch
//     (thirdparty/rexglue-sdk/src/graphics/pipeline/shader/interpreter.cpp,
//     ExecuteVertexFetchInstruction: widths 11, 11, 10 at offsets 0, 11, 22,
//     max(-1, v / (2^(w - 1) - 1)); dxbc_translator_fetch.cpp has the same).
//   FMT_16_16_16_16_FLOAT (32): x and y the low and the high half of the first
//     dword, z and w of the second (the same two files).
// of which the normal is the three NormalSourceComponents names. False, with
// nothing usable in `out`, for another format, fewer bytes than the element
// has, or a vector without a direction.
inline bool DecodeNormal(uint32_t format, const uint8_t* p, size_t avail, float out[3]) {
  uint8_t comp[3];
  if (!p || !NormalSourceComponents(format, comp)) return false;
  double source[4] = {0.0, 0.0, 0.0, 0.0};
  if (format == capture::kVertexFormat_10_11_11) {
    if (avail < 4) return false;
    const uint32_t w = capture::detail::Be32(p);
    auto field = [w](uint32_t shift, uint32_t bits) {
      const int32_t raw = int32_t(w << (32 - shift - bits)) >> (32 - bits);  // sign-extended
      return std::max(double(raw) / double((1u << (bits - 1)) - 1u), -1.0);
    };
    source[0] = field(0, 11);
    source[1] = field(11, 11);
    source[2] = field(22, 10);
  } else {
    if (avail < 8) return false;
    for (int k = 0; k < 4; ++k) source[k ^ 1] = capture::HalfToFloat(capture::detail::Be16(p + 2 * k));
  }
  const double v[3] = {source[comp[0]], source[comp[1]], source[comp[2]]};
  return detail::Normalize(v, out);
}

// Whether a reader decodes this element as a normal: a format
// NormalSourceComponents knows, inside its vertex, read as the guest's
// big-endian dwords (endian 8in32), the 10_11_11 signed and normalised, and
// fetched whole: the fetch delivers each of the three source components
// somewhere in its register (a shader that fetches two and derives the third
// has another normal than the stored one). Anything else is
// `normal-unsupported`: never the bytes read another way.
inline bool NormalElementSupported(const capture::LightElement& e) {
  uint8_t comp[3];
  const uint32_t bytes = capture::LightElementBytes(e.format);
  if (!NormalSourceComponents(e.format, comp) || bytes == 0) return false;
  if (e.stride_bytes == 0 || uint32_t(e.offset_bytes) + bytes > e.stride_bytes) return false;
  if (detail::ElementEndian(e) != 2) return false;
  if (e.format == capture::kVertexFormat_10_11_11 &&
      !((e.flags & capture::kLightElementSigned) && (e.flags & capture::kLightElementNormalized))) {
    return false;
  }
  const uint32_t need = (1u << comp[0]) | (1u << comp[1]) | (1u << comp[2]);
  return (detail::FetchedComponents(e.swizzle) & need) == need;
}

// Normals in the element's stream: every vertex whose element lies fully
// inside it; 0 for an element that is not read as a normal.
inline uint32_t NormalElementCount(const capture::LightElement& e) {
  if (!NormalElementSupported(e)) return 0;
  const uint32_t need = uint32_t(e.offset_bytes) + capture::LightElementBytes(e.format);
  if (need > e.vb.size) return 0;
  return (e.vb.size - need) / e.stride_bytes + 1;
}

// Decodes `count` normals from vertex `first_vertex` on. A vertex whose
// element has no direction gets (0, 0, 0). Fails (the buffer is then not
// usable) for an element NormalElementSupported refuses or a read past
// src_size.
inline bool DecodeNormals(const uint8_t* src, size_t src_size, const capture::LightElement& e, uint32_t first_vertex,
                          uint32_t count, Float3* out) {
  if (!src || !NormalElementSupported(e)) return false;
  const uint32_t bytes = capture::LightElementBytes(e.format);
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t at = (uint64_t(first_vertex) + i) * e.stride_bytes + e.offset_bytes;
    if (at + bytes > src_size) return false;
    float n[3];
    DecodeNormal(e.format, src + at, bytes, n);  // (zero without a direction)
    out[i] = {n[0], n[1], n[2]};
  }
  return true;
}

// The matrix that takes a normal through `rot` so that it stays perpendicular
// to what `rot` makes of its surface: the inverse transpose, up to a positive
// factor (the cofactor matrix times the determinant's sign, so a mirror does
// not turn the normal into the surface). For a rotation times one scale it is
// `rot` up to a factor. When the determinant is within 1e-12 of zero there is
// no inverse and `rot` itself is used.
inline void NormalMatrix(const float rot[3][3], float out[3][3]) {
  const double a = rot[0][0], b = rot[0][1], c = rot[0][2];
  const double d = rot[1][0], e = rot[1][1], f = rot[1][2];
  const double g = rot[2][0], h = rot[2][1], i = rot[2][2];
  const double cof[3][3] = {{e * i - f * h, f * g - d * i, d * h - e * g},
                            {c * h - b * i, a * i - c * g, b * g - a * h},
                            {b * f - c * e, c * d - a * f, a * e - b * d}};
  const double det = a * cof[0][0] + b * cof[0][1] + c * cof[0][2];
  if (!(std::fabs(det) > 1e-12)) {  // (a determinant that is not a number too)
    for (int r = 0; r < 3; ++r) {
      for (int k = 0; k < 3; ++k) out[r][k] = rot[r][k];
    }
    return;
  }
  const double sign = det < 0.0 ? -1.0 : 1.0;
  for (int r = 0; r < 3; ++r) {
    for (int k = 0; k < 3; ++k) out[r][k] = float(sign * cof[r][k]);
  }
}

// out = normalize(m * n). False, with out (0, 0, 0), when the result has no
// direction: a zero normal, a matrix that flattens it, values that are not finite.
inline bool TransformNormal(const float m[3][3], const float n[3], float out[3]) {
  double v[3];
  for (int r = 0; r < 3; ++r) v[r] = double(m[r][0]) * n[0] + double(m[r][1]) * n[1] + double(m[r][2]) * n[2];
  return detail::Normalize(v, out);
}

// A normal through the 3 x 3 `rot` (rows, as a draw's world rotation c4..c6
// holds them): multiplied by the inverse transpose of `rot` and normalised, so
// that it stays perpendicular under a scale that is not uniform and under a
// mirror (NormalMatrix); a result without a direction gives (0, 0, 1).
inline void RotateNormal(const float rot[3][3], const float n[3], float out[3]) {
  float m[3][3];
  NormalMatrix(rot, m);
  if (!TransformNormal(m, n, out)) {
    out[0] = out[1] = 0.0f;
    out[2] = 1.0f;
  }
}

// What a draw's world rotation is, as the object shaders use it on the normal:
// the rows c4..c6 times the constant c7.w (LightInputs::world_rot and
// world_scale). In the captured main-scene rows the rows carry the object's
// uniform scale and c7.w is its reciprocal, so the product is a rotation
// (orthonormal to 5e-7; frame-map section 14); nothing guarantees that for
// every draw, so the kind is found per draw and counted, never assumed.
//   kRotation: orthonormal after the scale (each entry of M M^T within 1e-3 of
//     the identity's) and right-handed: the normal is M n.
//   kMirrored: orthonormal after the scale, left-handed (a mirrored object, or
//     a negative c7.w): RotateNormal keeps the normal on the surface's side,
//     which the game's own M n * c7.w does only up to its sign.
//   kSkewed: invertible, not orthonormal after the scale (a scale that is not
//     uniform, a shear, a c7.w that is not the rows' reciprocal): the normal
//     RotateNormal makes is perpendicular, the game's is not.
//   kDegenerate: no direction comes out of it (a zero or singular matrix, a
//     zero scale, a value that is not finite).
enum class RotationKind : uint8_t { kRotation, kMirrored, kSkewed, kDegenerate, kCount };

inline const char* RotationKindName(RotationKind k) {
  switch (k) {
    case RotationKind::kRotation: return "rotation";
    case RotationKind::kMirrored: return "mirrored";
    case RotationKind::kSkewed: return "skewed";
    case RotationKind::kDegenerate: return "degenerate";
    default: return "?";
  }
}

inline RotationKind ClassifyRotation(const float rot[3][3], float scale) {
  double m[3][3];
  bool finite = true;
  for (int r = 0; r < 3; ++r) {
    for (int k = 0; k < 3; ++k) {
      m[r][k] = double(rot[r][k]) * scale;
      finite = finite && std::isfinite(m[r][k]);
    }
  }
  if (!finite) return RotationKind::kDegenerate;
  const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                     m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  if (!(std::fabs(det) > 1e-12)) return RotationKind::kDegenerate;
  for (int a = 0; a < 3; ++a) {
    for (int b = a; b < 3; ++b) {
      const double d = m[a][0] * m[b][0] + m[a][1] * m[b][1] + m[a][2] * m[b][2];
      if (std::fabs(d - (a == b ? 1.0 : 0.0)) > 1e-3) return RotationKind::kSkewed;
    }
  }
  return det > 0.0 ? RotationKind::kRotation : RotationKind::kMirrored;
}

// Replaces normals[0, count) of vertices first_vertex.. (decoded, in the
// stream's space) by their normals after the bones: normalize(B3 n), B3 the
// 3 x 3 of the vertex's blended bone matrix, the one SkinPositions puts its
// position through. The matrix as it is, not its inverse transpose: that is
// the game's own arithmetic (0xD4D558DA6A82BDC8 instr 47 to 49,
// 0xA1F7E9885EC466DF instr 19 to 21: Nb = B3 n), so under a bone that scales,
// or a blend of bones that is no rotation, the normal tilts as the game's
// does. The game's shaders then make the normal orthogonal to the tangent
// they transform the same way; that step is not built (the light inputs name
// no tangent; frame-map section 14, "Normals", follow-ups). A vertex that gets
// no position there (a bone past the palette, every weight zero) gets
// (0, 0, 0), as does one whose normal has no direction. Fails where
// SkinPositions fails: an index or weight word that cannot be read, a palette
// without a whole bone.
inline bool SkinNormals(const uint8_t* vb, size_t vb_size, const uint8_t* palette, size_t palette_size,
                        const capture::BoneSkin& s, uint32_t stride, uint32_t first_vertex, uint32_t count,
                        Float3* normals) {
  if (!s.active || stride == 0 || s.bone_stride == 0 || s.bones < 1 || s.bones > 4) return false;
  std::vector<capture::Float4> bones;
  capture::DecodeBones(palette, palette_size, s, &bones);
  if (bones.empty()) return false;
  for (uint32_t i = 0; i < count; ++i) {
    float m[3][4];
    const capture::BoneBlend blend = capture::BlendBones(vb, vb_size, uint64_t(first_vertex + i) * stride, s, bones, m);
    if (blend == capture::BoneBlend::kUnreadable) return false;
    if (blend == capture::BoneBlend::kNone) {
      normals[i] = {0.0f, 0.0f, 0.0f};
      continue;
    }
    const float rot[3][3] = {{m[0][0], m[0][1], m[0][2]}, {m[1][0], m[1][1], m[1][2]}, {m[2][0], m[2][1], m[2][2]}};
    const float n[3] = {normals[i].x, normals[i].y, normals[i].z};
    float out[3];
    TransformNormal(rot, n, out);  // (the matrix as it is; zero without a direction)
    normals[i] = {out[0], out[1], out[2]};
  }
  return true;
}

// Instanced draws: out[i] = the mesh normal of flat index i's vertex through
// the normal matrix of its copy's rows (the 3 x 3 that ExpandInstances puts
// the position through; the fourth components and the offset move positions
// only). Index for index the order of ExpandInstances. (0, 0, 0) where that
// has no position (an index the constants do not map, a vertex past the mesh
// normals, a copy past the stream) and where the normal has no direction.
// The game's shaders normalise M n (lighting-formulas.md, "Instanced
// plants"), which is the same for the rows of a rotation times one scale.
inline bool ExpandInstanceNormals(const Float3* mesh, uint32_t mesh_count, const uint8_t* rows, size_t rows_size,
                                  const capture::InstanceSet& s, uint32_t flat_count, Float3* out) {
  if (!mesh || !rows || !out || flat_count == 0) return false;
  struct Matrix {
    float m[3][3];
  };
  std::vector<Matrix> matrices;  // the normal matrix per copy, made on first use
  std::vector<uint8_t> state;    // 0 unknown, 1 made, 2 past the stream
  for (uint32_t i = 0; i < flat_count; ++i) {
    uint32_t copy = 0, vertex = 0;
    out[i] = {0.0f, 0.0f, 0.0f};
    if (!capture::InstanceIndex(s, i, &copy, &vertex) || vertex >= mesh_count) continue;
    if (copy >= state.size()) {
      state.resize(size_t(copy) + 1, 0);
      matrices.resize(size_t(copy) + 1);
    }
    if (state[copy] == 0) {
      capture::Float4 r[3];
      bool ok = true;
      for (int k = 0; k < 3 && ok; ++k) ok = capture::DecodePositions(rows, rows_size, s.rows[k], copy, 1, &r[k]);
      if (ok) {
        const float rot[3][3] = {{r[0].x, r[0].y, r[0].z}, {r[1].x, r[1].y, r[1].z}, {r[2].x, r[2].y, r[2].z}};
        NormalMatrix(rot, matrices[copy].m);
      }
      state[copy] = ok ? 1 : 2;
    }
    if (state[copy] != 1) continue;
    const float n[3] = {mesh[vertex].x, mesh[vertex].y, mesh[vertex].z};
    float v[3];
    TransformNormal(matrices[copy].m, n, v);  // (zero without a direction)
    out[i] = {v[0], v[1], v[2]};
  }
  return true;
}

// --- The colour stream of 0xECD66A10092E6562 (lighting-formulas.md, "Objects") ---
// An 8_8_8_8 element of a stream of its own, read at the vertex index plus the
// draw's offset (vertex c20.x, LightInputs::colour_first). The buffer holds,
// per vertex, red, green, blue and alpha in bytes 0 to 3 of an entry, 0 to 255
// (the fetch is unsigned and normalised). The shader raises the first three to
// its literal (LightInputs::colour_power); the buffer holds them as stored.

// The source components (0 to 3: x to w of the GPU's fetch) that are the
// colour's red, green, blue and alpha: z, y, x, w. A D3DCOLOR in guest memory
// is the bytes A, R, G, B, of which the fetch's source x is the last; the
// shader raises the fetched z, y, x to its literal as red, green, blue
// (0xECD6... instr 14, 21 to 35; checked by light_check.py's vertex_colour).
// Like NormalSourceComponents, by what the element is and not by the fetch's
// destination swizzle, and the one place that says so.
inline void ColourSourceComponents(uint8_t comp[4]) {
  comp[0] = 2; comp[1] = 1; comp[2] = 0; comp[3] = 3;
}

// Whether a reader decodes this element as that colour: 8_8_8_8, unsigned,
// normalised, inside its vertex, read as the guest's big-endian dword, and
// fetched whole (the fetch delivers all four source components).
inline bool ColourElementSupported(const capture::LightElement& e) {
  if (e.format != capture::kVertexFormat_8_8_8_8 || e.stride_bytes == 0 || uint32_t(e.offset_bytes) + 4 > e.stride_bytes) {
    return false;
  }
  if ((e.flags & capture::kLightElementSigned) || !(e.flags & capture::kLightElementNormalized)) return false;
  if (detail::ElementEndian(e) != 2) return false;
  return detail::FetchedComponents(e.swizzle) == 0xFu;
}

// Colours the stream holds from element `first` on, at most `limit` (the
// vertices of the draw's position buffer); 0 for an element that is not read
// as a colour or a `first` past the stream.
inline uint32_t ColourCount(const capture::LightElement& e, uint32_t first, uint32_t limit) {
  if (!ColourElementSupported(e)) return 0;
  const uint32_t need = uint32_t(e.offset_bytes) + 4;
  if (need > e.vb.size) return 0;
  const uint32_t total = (e.vb.size - need) / e.stride_bytes + 1;
  return first >= total ? 0 : std::min(total - first, limit);
}

// out[i] = the colour of element first + i, for i < count. Fails, with nothing
// usable in `out`, for an element ColourElementSupported refuses or a read
// past src_size: a draw whose offset leads outside its colour stream has no
// colours.
inline bool DecodeColours(const uint8_t* src, size_t src_size, const capture::LightElement& e, uint32_t first,
                          uint32_t count, uint32_t* out) {
  if (!src || !ColourElementSupported(e)) return false;
  uint8_t comp[4];
  ColourSourceComponents(comp);
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t at = (uint64_t(first) + i) * e.stride_bytes + e.offset_bytes;
    if (at + 4 > src_size) return false;
    // Source x in bits 0 to 7, then y, z, w (the SDK's k_8_8_8_8: widths 8 at offsets 0, 8, 16, 24).
    const uint32_t w = capture::detail::Be32(src + at);
    uint32_t packed = 0;
    for (uint32_t c = 0; c < 4; ++c) packed |= ((w >> (8 * comp[c])) & 0xFFu) << (8 * c);
    out[i] = packed;
  }
  return true;
}

}  // namespace fable2::native::render
