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
//   - an instanced draw: after the instance's rows as they are, one normal per
//     flat position (instance_expand.h), which is what the game's shaders
//     compute.
// And the second per-vertex input of the two vertex kinds without a colour
// stream (at the end): the probe weights with the set index of each vertex,
// and an instance's colour per flat position.
// A vertex whose normal has no direction (a zero or not finite element, a
// vertex that gets no position, a matrix that flattens it) holds (0, 0, 0):
// no direction is made up, and whoever lights with the buffer treats a zero
// normal as unlit by the sun, which is what the game's saturating dot
// products make of it (lighting-formulas.md, "Method", "Limits").
//
// Which components of an element a reader takes is the element's own selector
// (LightElement::source: source components, named per element by the table,
// vs-transforms.json "light"), never the fetch's destination swizzle, which
// says where the fetch puts a component in the shader's register and differs
// between shaders that use one element in one way (xyz_ and _xyz for the
// 10_11_11 normal, yxwz and yzxw for the halves, xyzw and wzxy for the probe
// weights). What this file knows of the formats (NormalSourceComponents,
// ColourSourceComponents) is held against the selector: an element whose
// selector is another one than its format's is not decoded
// (`normal-unsupported`, or no colours: `light-unknown`), never read either way.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

// The source components (x to w of the GPU's fetch) of one element at `p`, which holds the
// format's bytes (LightElementBytes) as the guest stores them: big-endian dwords, the fetch's
// endian 8in32. A component the format does not have is 0.
//   FMT_10_11_11 (16), signed and normalised: x = bits 0 to 10, y = bits 11 to 21, z = bits 22 to
//     31 of the dword, each divided by its positive maximum (1023, 1023, 511), at least -1: the
//     SDK's k_10_11_11 (thirdparty/rexglue-sdk/src/graphics/pipeline/shader/interpreter.cpp,
//     ExecuteVertexFetchInstruction: widths 11, 11, 10 at offsets 0, 11, 22, max(-1, v / (2^(w - 1)
//     - 1)); dxbc_translator_fetch.cpp has the same).
//   FMT_16_16_16_16_FLOAT (32): x and y the low and the high half of the first dword, z and w of
//     the second (the same two files): the halves in memory are source y, x, w, z.
//   FMT_8_8_8_8 (6), unsigned and normalised: x = bits 0 to 7, then y, z, w (widths 8 at offsets
//     0, 8, 16, 24), each divided by 255: the bytes in memory are source w, z, y, x.
//   FMT_32_32_32_32_FLOAT (38): x to w the four dwords in memory order.
inline void ElementSource(uint32_t format, const uint8_t* p, double source[4]) {
  source[0] = source[1] = source[2] = source[3] = 0.0;
  if (format == capture::kVertexFormat_10_11_11) {
    const uint32_t w = capture::detail::Be32(p);
    auto field = [w](uint32_t shift, uint32_t bits) {
      const int32_t raw = int32_t(w << (32 - shift - bits)) >> (32 - bits);  // sign-extended
      return std::max(double(raw) / double((1u << (bits - 1)) - 1u), -1.0);
    };
    source[0] = field(0, 11);
    source[1] = field(11, 11);
    source[2] = field(22, 10);
  } else if (format == capture::kVertexFormat_16_16_16_16_FLOAT) {
    for (int k = 0; k < 4; ++k) source[k ^ 1] = capture::HalfToFloat(capture::detail::Be16(p + 2 * k));
  } else if (format == capture::kVertexFormat_8_8_8_8) {
    const uint32_t w = capture::detail::Be32(p);
    for (int k = 0; k < 4; ++k) source[k] = double((w >> (8 * k)) & 0xFFu) / 255.0;
  } else if (format == capture::kVertexFormat_32_32_32_32_FLOAT) {
    for (int k = 0; k < 4; ++k) {
      const uint32_t w = capture::detail::Be32(p + 4 * k);
      float f;
      std::memcpy(&f, &w, sizeof(f));
      source[k] = f;
    }
  }
}
}  // namespace detail

// Whether a reader decodes this element at all: one of the light's formats, inside its vertex,
// read as the guest's big-endian dwords (endian 8in32), the 10_11_11 signed and normalised and
// the 8_8_8_8 unsigned and normalised (the meanings ElementSource has for them; sign and scale
// mean nothing for the float formats), and with a selector that names three or four source
// components, each one its format has, none twice. The capture makes no other element
// (material.h, LightSource: it also requires that the shader's fetch delivers every component
// named); a reader does not rely on that.
inline bool ElementReadable(const capture::LightElement& e) {
  const uint32_t bytes = capture::LightElementBytes(e.format), components = capture::LightFormatComponents(e.format);
  if (bytes == 0 || components == 0) return false;
  if (e.stride_bytes == 0 || uint32_t(e.offset_bytes) + bytes > e.stride_bytes) return false;
  if (detail::ElementEndian(e) != 2) return false;
  const bool is_signed = (e.flags & capture::kLightElementSigned) != 0;
  const bool normalized = (e.flags & capture::kLightElementNormalized) != 0;
  if (e.format == capture::kVertexFormat_10_11_11 && !(is_signed && normalized)) return false;
  if (e.format == capture::kVertexFormat_8_8_8_8 && !(!is_signed && normalized)) return false;
  if (e.source_count < 3 || e.source_count > 4) return false;
  if (e.source_count == 3 && (e.source >> 6) != 0) return false;
  uint32_t seen = 0;
  for (uint32_t i = 0; i < e.source_count; ++i) {
    const uint32_t c = capture::LightSourceComponent(e, i);
    if (c >= components || ((seen >> c) & 1u)) return false;
    seen |= 1u << c;
  }
  return true;
}

// One element through its selector: out[i] = the source component the selector names i-th, for
// i < e.source_count, which is returned (the normal's x, y, z as stored; a colour's red, green,
// blue and, of the colour stream, alpha, each 0 to 1; the four probe weights). 0, with `out`
// untouched, for an element ElementReadable refuses or fewer than its bytes at `p`.
inline uint32_t DecodeElement(const capture::LightElement& e, const uint8_t* p, size_t avail, float out[4]) {
  if (!p || !ElementReadable(e) || avail < capture::LightElementBytes(e.format)) return 0;
  double source[4];
  detail::ElementSource(e.format, p, source);
  for (uint32_t i = 0; i < e.source_count; ++i) out[i] = float(source[capture::LightSourceComponent(e, i)]);
  return e.source_count;
}

// What this file knows of a normal element by its format: the source
// components (0 to 3: x to w of the GPU's fetch, before any destination
// swizzle) that are the normal's x, y, z; false for a format that holds no
// normal a reader decodes. Not what a reader selects with (that is the
// element's selector, the table's): what the selector is held against
// (NormalElementSupported).
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
// FABLE2_NORMALS_BIN). The table names the same components per element
// (vs-transforms.json "light": "source": "xyz" and "yxw"); the two agree for
// every committed entry (tests/native/test_normal_decode.cpp, the table's rows).
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

// Whether a reader decodes this element as a normal: one ElementReadable
// reads, in a format NormalSourceComponents knows, whose selector names three
// components and the very three, in the order, this file knows for the format.
// That last is the cross-check of the table against the format: an entry that
// named another order (or a format whose order nobody has measured) gives no
// normals rather than plausible wrong ones. Anything else is
// `normal-unsupported` (GeometryCache::Normals, counted by the pass per reason
// and vertex shader): never the bytes read another way. That the shader's
// fetch delivers the three components is the capture's check (material.h,
// LightSource; `normal-unsupported` there too).
inline bool NormalElementSupported(const capture::LightElement& e) {
  uint8_t comp[3];
  if (!NormalSourceComponents(e.format, comp) || !ElementReadable(e) || e.source_count != 3) return false;
  for (uint32_t i = 0; i < 3; ++i) {
    if (capture::LightSourceComponent(e, i) != comp[i]) return false;
  }
  return true;
}

// One normal element as the guest stores it, through the element's selector,
// normalised. False, with nothing usable in `out`, for an element
// NormalElementSupported refuses, fewer bytes than the element has, or a
// vector without a direction.
inline bool DecodeNormal(const capture::LightElement& e, const uint8_t* p, size_t avail, float out[3]) {
  if (!p || !NormalElementSupported(e) || avail < capture::LightElementBytes(e.format)) return false;
  double source[4];
  detail::ElementSource(e.format, p, source);
  const double v[3] = {source[capture::LightSourceComponent(e, 0)], source[capture::LightSourceComponent(e, 1)],
                       source[capture::LightSourceComponent(e, 2)]};
  return detail::Normalize(v, out);
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
  const uint32_t comp[3] = {capture::LightSourceComponent(e, 0), capture::LightSourceComponent(e, 1),
                            capture::LightSourceComponent(e, 2)};
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t at = (uint64_t(first_vertex) + i) * e.stride_bytes + e.offset_bytes;
    if (at + bytes > src_size) return false;
    double source[4];
    detail::ElementSource(e.format, src + at, source);
    const double v[3] = {source[comp[0]], source[comp[1]], source[comp[2]]};
    float n[3];
    detail::Normalize(v, n);  // (zero without a direction)
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
//     a negative c7.w). For an orthonormal M the inverse transpose is M
//     itself, sign included, so the game's M n and RotateNormal's normal are
//     the same vector.
//   kSkewed: invertible, not orthonormal after the scale (a scale that is not
//     uniform, a shear, a c7.w that is not the rows' reciprocal): the normal
//     RotateNormal makes is perpendicular, the game's is not.
//   kDegenerate: no direction comes out of it (a zero or singular matrix, a
//     zero scale, a value that is not finite).
// The clay pass lights with the game's own value for every kind that has a
// direction, N = normalize(M n) with M the rows times the scale
// (clay_logic.h, MakeClayObjectConstants; ruling of 2026-10-08), and draws a
// kDegenerate draw without light. RotateNormal is used by no object draw.
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

// Instanced draws: out[i] = normalize(M n), n the mesh normal of flat index
// i's vertex and M the 3 x 3 of its copy's rows, the one ExpandInstances puts
// the position through (the fourth components and the offset move positions
// only). The matrix as it is, not its inverse transpose: that is the game's
// own arithmetic in all seven plant vertex shaders (0x8123C16DBF583F92 instr
// 22 to 24, 30, 35, 36; 0x48D30ECCF684F488 and 0xFC4F2EF6930768BE 29 to 31, 37,
// 39, 40; 0xB636821F95DC9D8E 24, 29, 30, 34, 35, 37: three dp3 of the normal
// with the rows' xyz, the rows of the position's dp4, then the reciprocal
// length; machine-checked for the seven with rows that are no rotation by
// out/native-evidence/lighting/task5b/plant_normal_check.py), so under a copy
// that is scaled unevenly or sheared the normal tilts as the game's does
// (ruling of 2026-10-08, controller-amendments C8: the game's arithmetic, as
// for the bones and the world rotation). The two forms are one for the rows of
// a rotation times one scale, which every copy the captures' draws name is.
// Index for index the order of ExpandInstances. (0, 0, 0) where that has no
// position (an index the constants do not map, a vertex past the mesh
// normals, a copy past the stream) and where the normal has no direction.
inline bool ExpandInstanceNormals(const Float3* mesh, uint32_t mesh_count, const uint8_t* rows, size_t rows_size,
                                  const capture::InstanceSet& s, uint32_t flat_count, Float3* out) {
  if (!mesh || !rows || !out || flat_count == 0) return false;
  struct Matrix {
    float m[3][3];
  };
  std::vector<Matrix> matrices;  // the rows' 3 x 3 per copy, decoded on first use
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
        for (int k = 0; k < 3; ++k) {
          matrices[copy].m[k][0] = r[k].x;
          matrices[copy].m[k][1] = r[k].y;
          matrices[copy].m[k][2] = r[k].z;
        }
      }
      state[copy] = ok ? 1 : 2;
    }
    if (state[copy] != 1) continue;
    const float n[3] = {mesh[vertex].x, mesh[vertex].y, mesh[vertex].z};
    float v[3];
    TransformNormal(matrices[copy].m, n, v);  // (the matrix as it is; zero without a direction)
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

// What this file knows of that colour: the source components (0 to 3: x to w
// of the GPU's fetch) that are its red, green, blue and alpha: z, y, x, w. A
// D3DCOLOR in guest memory is the bytes A, R, G, B, of which the fetch's
// source x is the last; the shader raises the fetched z, y, x to its literal
// as red, green, blue (0xECD6... instr 14, 21 to 35; checked by
// light_check.py's vertex_colour). Like NormalSourceComponents, by what the
// element is and not by the fetch's destination swizzle, and what the
// element's selector (the table's "zyxw") is held against.
inline void ColourSourceComponents(uint8_t comp[4]) {
  comp[0] = 2; comp[1] = 1; comp[2] = 0; comp[3] = 3;
}

// Whether a reader decodes this element as that colour: 8_8_8_8 that
// ElementReadable reads (unsigned, normalised, inside its vertex, the guest's
// big-endian dword), whose selector names four components and the very four,
// in the order, of ColourSourceComponents (the cross-check: another order
// gives no colours, and with them no light for the draw, `light-unknown`).
inline bool ColourElementSupported(const capture::LightElement& e) {
  if (e.format != capture::kVertexFormat_8_8_8_8 || !ElementReadable(e) || e.source_count != 4) return false;
  uint8_t comp[4];
  ColourSourceComponents(comp);
  for (uint32_t i = 0; i < 4; ++i) {
    if (capture::LightSourceComponent(e, i) != comp[i]) return false;
  }
  return true;
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
  const uint32_t comp[4] = {capture::LightSourceComponent(e, 0), capture::LightSourceComponent(e, 1),
                            capture::LightSourceComponent(e, 2), capture::LightSourceComponent(e, 3)};
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

// --- The second buffer of the two other vertex kinds (plan Task 5b) ---
// kProbe (0x79EA..., 0xD4D5..., 0xA1F7..., 0x475E...): the vertex colour is
// c[28 + a0].xyz + (c[29 + a0] . w, c[30 + a0] . w, c[31 + a0] . w), w the four
// weights of the vertex and a0 the set its position element names
// (lighting-formulas.md, "Objects", probe_colour). The buffer holds w and a0 per
// vertex of the mesh; the constants are the draw's (LightInputs::vs_const) and
// are in no buffer. kInstance (the seven plant shaders): the colour of the
// vertex's copy, one entry per flat position.

// Why such a buffer was not built (counted by the pass: ClayStats::light_second;
// the record is `light-unknown` and keeps its material). kElement: the element,
// or the position's set component, is not one the kind's reader decodes (the
// table's selector held against the format). kStream: a stream is not readable
// or holds fewer elements than asked for. kProbeSet: a vertex whose set index is
// not 0, 4 or 8. kValue: a weight that is not a number of usable size. kUpload:
// the buffer could not be made on the GPU.
enum class LightSecond : uint8_t { kNone, kElement, kStream, kProbeSet, kValue, kUpload, kCount };

inline const char* LightSecondName(LightSecond s) {
  switch (s) {
    case LightSecond::kNone: return "none";
    case LightSecond::kElement: return "element";
    case LightSecond::kStream: return "stream";
    case LightSecond::kProbeSet: return "probe-set";
    case LightSecond::kValue: return "value";
    case LightSecond::kUpload: return "upload";
    default: return "?";
  }
}

// What this file knows of the probe weights: the source components (0 to 3: x
// to w of the GPU's fetch) in the order of the probe constants' components: x,
// y, z, w, the four floats in memory order. Every probe shader dots the fetched
// weights with c[29 + a0]..c[31 + a0] under one swizzle or its exact undoing
// (0x79EA... instr 30 to 32 and 0x475E... 66 to 68: fetched xyzw, `dp4 r.yxzw,
// c.yxzw`; 0xD4D5... 76 to 78 and 0xA1F7... 52 to 54: fetched wzxy, `dp4 r.wzyx,
// c.yxzw`, and register wzyx of a wzxy fetch is source y, x, z, w), so weight
// component k meets constant component k whatever the fetch's destination
// swizzle; light_check.py's probe_colour takes them so. What the element's
// selector (the table's "xyzw") is held against.
inline void WeightsSourceComponents(uint8_t comp[4]) {
  comp[0] = 0; comp[1] = 1; comp[2] = 2; comp[3] = 3;
}

// Whether a reader decodes this element as the probe weights: 32_32_32_32_FLOAT
// that ElementReadable reads (inside its vertex, the guest's big-endian dwords),
// whose selector names four components and the very four, in the order, of
// WeightsSourceComponents (the cross-check: another order gives no weights, and
// with them no light for the draw, `light-unknown`).
inline bool WeightsElementSupported(const capture::LightElement& e) {
  if (e.format != capture::kVertexFormat_32_32_32_32_FLOAT || !ElementReadable(e) || e.source_count != 4) return false;
  uint8_t comp[4];
  WeightsSourceComponents(comp);
  for (uint32_t i = 0; i < 4; ++i) {
    if (capture::LightSourceComponent(e, i) != comp[i]) return false;
  }
  return true;
}

// Weights in the element's stream: every vertex whose element lies fully inside
// it; 0 for an element that is not read as weights.
inline uint32_t WeightsCount(const capture::LightElement& e) {
  if (!WeightsElementSupported(e)) return 0;
  const uint32_t need = uint32_t(e.offset_bytes) + 16;
  if (need > e.vb.size) return 0;
  return (e.vb.size - need) / e.stride_bytes + 1;
}

// The largest magnitude a weight may have to reach a buffer (clay_logic.h holds
// it equal to the bound of the light's constants, kLightValueMax): the captured
// weights are at most 1 in every vertex of every stream (frame-map section 14,
// "Probe weights and instance colours").
inline constexpr float kProbeWeightMax = 1024.0f;

// out[i] = the four weights of vertex first_vertex + i, for i < count. kNone,
// or, with nothing usable in `out`: kElement for an element
// WeightsElementSupported refuses, kStream for a read past src_size, kValue for
// a weight that is not finite or larger than kProbeWeightMax (no such value
// reaches a buffer, and no vertex is given made-up weights instead).
inline LightSecond DecodeWeights(const uint8_t* src, size_t src_size, const capture::LightElement& e, uint32_t first_vertex,
                                 uint32_t count, capture::Float4* out) {
  if (!WeightsElementSupported(e)) return LightSecond::kElement;
  if (!src) return LightSecond::kStream;
  const uint32_t comp[4] = {capture::LightSourceComponent(e, 0), capture::LightSourceComponent(e, 1),
                            capture::LightSourceComponent(e, 2), capture::LightSourceComponent(e, 3)};
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t at = (uint64_t(first_vertex) + i) * e.stride_bytes + e.offset_bytes;
    if (at + 16 > src_size) return LightSecond::kStream;
    float w[4];
    for (uint32_t k = 0; k < 4; ++k) {
      w[k] = capture::detail::BeFloat(src + at + 4 * comp[k]);
      if (!(std::fabs(w[k]) <= kProbeWeightMax)) return LightSecond::kValue;  // (a NaN too)
    }
    out[i] = {w[0], w[1], w[2], w[3]};
  }
  return LightSecond::kNone;
}

// What this file knows of the probe set's index: the source component (0 to 3)
// of the position element that holds it, by the element's layout. A
// 16_16_16_16_FLOAT position read under the stream's endian 8in32 (swap16): the
// fourth half in memory, which under the pair swap is the fetch's source z
// (2). The shaders take it from wherever their fetch's destination swizzle puts
// source z (0x79EA... instr 28, 29: `truncs r2.x, r2.x`, `maxas a0, r2.x` after a
// zxwy fetch; 0x475E... r1.w after wxyz; 0xD4D5... r7.w and 0xA1F7... r1.w after
// yxwz). False for any other layout: none is seen, and where the index would
// lie in one is not known.
inline bool ProbeSetSourceComponent(const capture::PosLayout& pos, uint8_t* comp) {
  if (pos.format != capture::PosFormat::kHalf4 || !pos.swap16) return false;
  *comp = 2;
  return true;
}

// Whether a reader takes the set index from this position element through the
// table's component (LightInputs::a0_comp): the component this file knows for
// the layout, and no other (the cross-check of the table against the format).
inline bool ProbeSetSupported(const capture::PosLayout& pos, int a0_comp) {
  uint8_t comp = 0;
  return pos.stride_bytes != 0 && ProbeSetSourceComponent(pos, &comp) && a0_comp == int(comp);
}

// out[i] = the set index a0 of vertex first_vertex + i (0, 4 or 8: the offset of
// the vertex's probe set from c28), for i < count. kNone, or, with nothing
// usable in `out`: kElement unless ProbeSetSupported, kStream for a read past
// src_size, kProbeSet for a vertex whose component is not exactly 0, 4 or 8.
// The game truncates the component and loads its address register with the
// result; every vertex of every captured stream holds one of the three exactly,
// so anything else is taken for another element than the set index and the
// draw has no buffer, rather than truncated into a set.
inline LightSecond DecodeProbeSets(const uint8_t* src, size_t src_size, const capture::PosLayout& pos, int a0_comp,
                                   uint32_t first_vertex, uint32_t count, uint32_t* out) {
  if (!ProbeSetSupported(pos, a0_comp)) return LightSecond::kElement;
  if (!src) return LightSecond::kStream;
  capture::PosLayout source = pos;
  source.swizzle = 0x688;  // the element's source components as they are, whatever the fetch's destination swizzle
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t vertex = uint64_t(first_vertex) + i;
    capture::Float4 p;
    if (vertex > 0xFFFFFFFFull || !capture::DecodePositions(src, src_size, source, uint32_t(vertex), 1, &p)) {
      return LightSecond::kStream;
    }
    const float v[4] = {p.x, p.y, p.z, p.w};
    const float set = v[a0_comp];
    if (set != 0.0f && set != 4.0f && set != 8.0f) return LightSecond::kProbeSet;  // (a NaN too)
    out[i] = uint32_t(set);
  }
  return LightSecond::kNone;
}

// The probe buffer of a mesh of `count` vertices, as the geometry cache uploads
// it: the weights from byte 0 (four host floats a vertex), the set indices from
// ProbeSetOffset(count) on (one host uint32 a vertex: 0, 4 or 8), the bytes
// between zero. Two regions of one buffer, so that each is read with an
// element no larger than a position's (clay_logic.h, kClayPositionElementBytes:
// the rule for whatever is bound at a light register), the second from a bind
// offset the RHI accepts (a multiple of 256 bytes: nrhi::kBufferOffsetAlignment;
// geometry_cache.cpp asserts the two equal).
inline constexpr uint64_t kProbeRegionAlignment = 256;
inline constexpr uint64_t ProbeSetOffset(uint32_t count) {
  return (uint64_t(count) * 16 + (kProbeRegionAlignment - 1)) & ~(kProbeRegionAlignment - 1);
}
inline constexpr uint64_t ProbeBufferBytes(uint32_t count) { return ProbeSetOffset(count) + uint64_t(count) * 4; }

// Builds that buffer for the first `count` vertices of the two streams into
// `out`. All or nothing: on anything but kNone `out` is empty (the reasons of
// DecodeWeights and DecodeProbeSets; kStream also for no vertex and no `out`).
inline LightSecond BuildProbe(const uint8_t* weights, size_t weights_size, const capture::LightElement& e, const uint8_t* vb,
                              size_t vb_size, const capture::PosLayout& pos, int a0_comp, uint32_t count,
                              std::vector<uint8_t>* out) {
  if (!out) return LightSecond::kStream;
  out->clear();
  if (count == 0) return LightSecond::kStream;
  static_assert(sizeof(capture::Float4) == 16);
  std::vector<capture::Float4> w(count);
  std::vector<uint32_t> sets(count);
  LightSecond why = DecodeWeights(weights, weights_size, e, 0, count, w.data());
  if (why == LightSecond::kNone) why = DecodeProbeSets(vb, vb_size, pos, a0_comp, 0, count, sets.data());
  if (why != LightSecond::kNone) return why;
  out->assign(size_t(ProbeBufferBytes(count)), 0);
  std::memcpy(out->data(), w.data(), size_t(count) * 16);
  std::memcpy(out->data() + ProbeSetOffset(count), sets.data(), size_t(count) * 4);
  return LightSecond::kNone;
}

// What this file knows of an instance's colour: the source components that are
// its red, green and blue: z, y, x, as the colour stream's (ColourSourceComponents)
// without the alpha: the fourth byte of the element is not the light's, and the
// shaders' fetches leave it out (zyx_ and _zyx; it reads 254 in every captured
// copy). The seven plant shaders raise the fetched z, y, x to c18.w as red,
// green, blue (0x8123... instr 17, 26 to 33; light_check.py's _plant_vertex).
// What the element's selector (the table's "zyx") is held against.
inline void InstanceColourSourceComponents(uint8_t comp[3]) {
  comp[0] = 2; comp[1] = 1; comp[2] = 0;
}

// Whether a reader decodes this element as an instance's colour: 8_8_8_8 that
// ElementReadable reads (unsigned, normalised, inside its record, the guest's
// big-endian dword), whose selector names three components and the very three,
// in the order, of InstanceColourSourceComponents.
inline bool InstanceColourElementSupported(const capture::LightElement& e) {
  if (e.format != capture::kVertexFormat_8_8_8_8 || !ElementReadable(e) || e.source_count != 3) return false;
  uint8_t comp[3];
  InstanceColourSourceComponents(comp);
  for (uint32_t i = 0; i < 3; ++i) {
    if (capture::LightSourceComponent(e, i) != comp[i]) return false;
  }
  return true;
}

// Instanced draws: out[i] = the colour of flat index i's copy, the element of
// that copy in the instance stream (`rows`): red, green and blue in bytes 0 to
// 2 of an entry as stored (0 to 255; the shader's power c18.w is the frame's),
// byte 3 zero. Index for index the order of ExpandInstances. 0 for an index the
// constants map to no copy (ExpandInstances gives it no position either).
// False, with nothing usable in `out`, for an element
// InstanceColourElementSupported refuses and for a copy whose colour lies past
// the stream: a draw that names such a copy has no colours (the copy's rows
// can lie inside the stream where its colour, the record's last element, does
// not, and no colour is made up for a vertex that has a position).
inline bool ExpandInstanceColours(const uint8_t* rows, size_t rows_size, const capture::LightElement& e,
                                  const capture::InstanceSet& s, uint32_t flat_count, uint32_t* out) {
  if (!rows || !out || flat_count == 0 || !InstanceColourElementSupported(e)) return false;
  const uint32_t comp[3] = {capture::LightSourceComponent(e, 0), capture::LightSourceComponent(e, 1),
                            capture::LightSourceComponent(e, 2)};
  for (uint32_t i = 0; i < flat_count; ++i) {
    uint32_t copy = 0, vertex = 0;
    out[i] = 0;
    if (!capture::InstanceIndex(s, i, &copy, &vertex)) continue;
    const uint64_t at = uint64_t(copy) * e.stride_bytes + e.offset_bytes;
    if (at + 4 > rows_size) return false;
    // Source x in bits 0 to 7, then y, z, w (as DecodeColours).
    const uint32_t w = capture::detail::Be32(rows + at);
    uint32_t packed = 0;
    for (uint32_t c = 0; c < 3; ++c) packed |= ((w >> (8 * comp[c])) & 0xFFu) << (8 * c);
    out[i] = packed;
  }
  return true;
}

}  // namespace fable2::native::render
