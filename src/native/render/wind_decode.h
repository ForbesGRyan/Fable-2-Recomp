#pragma once

// The per-vertex wind words of a mesh (pure: no SDK/GPU deps). Foliage sub-project, phase 11a:
// spec Design 6, rulings 11 and 21; the ledger's rulings 30 and 32; frame-map section 18, "Wind
// words". The formulas that read them are docs/native-renderer/lighting-formulas.md, "Wind".
//
// What a family's wind formula reads of a vertex besides its position, as 32-bit floats, one run a
// vertex. Mesh-intrinsic: no register and no literal is in the words, so the wind (the clock, the
// direction) rebuilds nothing.
//   a trunk vertex, 4 words: the four values of its 8_8_8_8 weights, in the element's source order
//     (light_formulas.wind_trunk's `w4`);
//   a leaf vertex, 10 words: its pivot's three coordinates (read as the position is), the four
//     weights, the three flutter values (wind_leaf's `q`, `w4`, `v3`);
//   an instanced plant's flat position, 2 words: A3 + B3 of its copy (the fourth components of the
//     copy's first two rows, whose sum with the draw's c7.x + c7.y is the copy's seed) and z of its
//     mesh vertex (wind_plant's `a3`, `b3`, `z`).
// All or nothing: a builder that returns anything but kNone has produced no usable words.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../capture/instance_expand.h"
#include "../capture/position_decode.h"
#include "../capture/wind_inputs.h"
#include "normal_decode.h"

namespace fable2::native::render {

constexpr uint32_t WindWordsPerVertex(capture::WindFamily f) {   // trunk 4, leaf 10, plant 2, else 0
  return f == capture::WindFamily::kTrunk ? 4u : f == capture::WindFamily::kLeaf ? 10u : f == capture::WindFamily::kPlant ? 2u : 0u;
}

// The words a draw of `count` vertices reads; 0 when the product does not fit 32 bits (and for no
// family or no vertex): a count of words is a 32-bit number wherever it goes.
constexpr uint32_t WindWordsNeeded(capture::WindFamily f, uint32_t count) {
  const uint64_t words = uint64_t(WindWordsPerVertex(f)) * count;
  return words > 0xFFFFFFFFull ? 0u : uint32_t(words);
}

// Why a mesh has no wind words (kNone: it has them).
enum class WindBuild : uint8_t {
  kNone,
  kElement,  // an element is not one a reader decodes as the family's, or the record is not the family's kind
  kStream,   // a stream is unreadable or holds fewer vertices than asked for; a copy past the instance stream
  kValue,    // a value that is no number in a vertex the words cover
  kUpload,   // the buffer could not be made (the geometry cache)
  kCount
};

inline const char* WindBuildName(WindBuild b) {
  switch (b) {
    case WindBuild::kNone: return "none";
    case WindBuild::kElement: return "element";
    case WindBuild::kStream: return "stream";
    case WindBuild::kValue: return "value";
    case WindBuild::kUpload: return "upload";
    default: return "?";
  }
}

// What a tree mesh's words say about how far the wind can move its vertices: the inputs of the
// reach that no register holds (ledger ruling 32; light_formulas.wind_reach_tree's `w4_max` and
// wind_reach_leaf's `length_max`). weight_max: the largest of the four weights over the vertices
// built (at most 1: they are bytes over 255). length_max: a leaf's largest distance of a vertex
// from its pivot, |P - Q|; 0 for a trunk. A plant has none: its reach is its registers' alone
// (wind_reach_plant).
struct WindExtent {
  float weight_max = 0.0f;
  float length_max = 0.0f;
};

namespace wind_detail {
// The format and the number of source components each element of a tree has (the table's:
// wind_inputs.h, WindTableIsWellFormed), and which a family reads.
inline constexpr uint8_t kFormat[capture::kWindElements] = {capture::kVertexFormat_16_16_16_16_FLOAT, capture::kVertexFormat_8_8_8_8,
                                                            capture::kVertexFormat_8_8_8_8};
inline constexpr uint8_t kCount[capture::kWindElements] = {3, 4, 3};
inline bool Reads(capture::WindFamily f, uint32_t element) {
  return f == capture::WindFamily::kLeaf || (f == capture::WindFamily::kTrunk && element == capture::kWindWeights);
}
// Whether a reader decodes `e` as element `element` of a tree: the element's format, read as
// ElementReadable reads it (inside its vertex, the guest's big-endian dwords, the bytes unsigned
// and normalised), through as many source components as the family's formula takes.
inline bool Supported(const capture::LightElement& e, uint32_t element) {
  return element < capture::kWindElements && e.format == kFormat[element] && e.source_count == kCount[element] && ElementReadable(e);
}
}  // namespace wind_detail

// The vertices a tree's wind elements all hold whole, each in its own stream as the capture bound
// it (LightElement::vb); 0 for a family that is no tree and for an element no reader decodes.
inline uint32_t TreeWindVertices(const capture::WindInputs& wind) {
  uint32_t count = 0xFFFFFFFFu;
  bool any = false;
  for (uint32_t e = 0; e < capture::kWindElements; ++e) {
    if (!wind_detail::Reads(wind.family, e)) continue;
    const capture::LightElement& el = wind.element[e];
    if (!wind_detail::Supported(el, e)) return 0;
    const uint32_t need = uint32_t(el.offset_bytes) + capture::LightElementBytes(el.format);
    if (need > el.vb.size) return 0;
    const uint32_t n = (el.vb.size - need) / el.stride_bytes + 1;
    count = n < count ? n : count;
    any = true;
  }
  return any ? count : 0;
}

// A tree's words for its first `count` vertices into `out` (WindWordsNeeded floats): a trunk
// (w.x, w.y, w.z, w.w); a leaf (q.x, q.y, q.z, w.x, w.y, w.z, w.w, v.x, v.y, v.z), each value as
// the fetch reads it (the element's format, sign, normalisation and endian) through the element's
// source components. `streams[e]`, `sizes[e]`: the bytes of element e's stream from its bound
// offset on (only those the family reads are looked at). `positions`: a leaf's decoded positions,
// `count` of them, as the record's position buffer has them (for the extent; a trunk's are not
// read). `extent` (optional): receives the mesh's extent.
// kNone, or: kElement for a family that is no tree or an element that is not the family's;
// kStream without a stream, a vertex, `out`, or a leaf's positions, and for a vertex whose element
// lies past its stream; kValue for a pivot or a position that is not finite. Nothing is written
// from the failing vertex's first word on; what lies before it is no result either.
inline WindBuild BuildTreeWindWords(const capture::WindInputs& wind, const uint8_t* const streams[capture::kWindElements],
                                    const size_t sizes[capture::kWindElements], const capture::Float4* positions, uint32_t count,
                                    float* out, WindExtent* extent) {
  const bool leaf = wind.family == capture::WindFamily::kLeaf;
  if (!leaf && wind.family != capture::WindFamily::kTrunk) return WindBuild::kElement;
  for (uint32_t e = 0; e < capture::kWindElements; ++e) {
    if (wind_detail::Reads(wind.family, e) && !wind_detail::Supported(wind.element[e], e)) return WindBuild::kElement;
  }
  if (!streams || !sizes || !out || count == 0 || (leaf && !positions)) return WindBuild::kStream;
  for (uint32_t e = 0; e < capture::kWindElements; ++e) {
    if (wind_detail::Reads(wind.family, e) && !streams[e]) return WindBuild::kStream;
  }
  const uint32_t words = WindWordsPerVertex(wind.family);
  WindExtent ext;
  for (uint32_t v = 0; v < count; ++v) {
    float value[capture::kWindElements][4] = {};
    for (uint32_t e = 0; e < capture::kWindElements; ++e) {
      if (!wind_detail::Reads(wind.family, e)) continue;
      const capture::LightElement& el = wind.element[e];
      const uint64_t at = uint64_t(v) * el.stride_bytes + el.offset_bytes;
      if (at + capture::LightElementBytes(el.format) > sizes[e]) return WindBuild::kStream;
      if (DecodeElement(el, streams[e] + at, size_t(sizes[e] - at), value[e]) != wind_detail::kCount[e]) return WindBuild::kElement;
      for (uint32_t k = 0; k < wind_detail::kCount[e]; ++k) {
        if (!std::isfinite(value[e][k])) return WindBuild::kValue;
      }
    }
    const float* w = value[capture::kWindWeights];
    for (int k = 0; k < 4; ++k) ext.weight_max = w[k] > ext.weight_max ? w[k] : ext.weight_max;
    float* o = out + size_t(v) * words;
    if (leaf) {
      const float* q = value[capture::kWindPivot];
      const capture::Float4& p = positions[v];
      const double d[3] = {double(p.x) - q[0], double(p.y) - q[1], double(p.z) - q[2]};
      const float length = float(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
      if (!std::isfinite(length)) return WindBuild::kValue;   // (a position that is no number)
      ext.length_max = length > ext.length_max ? length : ext.length_max;
      const float* f = value[capture::kWindFlutter];
      o[0] = q[0]; o[1] = q[1]; o[2] = q[2];
      o[3] = w[0]; o[4] = w[1]; o[5] = w[2]; o[6] = w[3];
      o[7] = f[0]; o[8] = f[1]; o[9] = f[2];
    } else {
      o[0] = w[0]; o[1] = w[1]; o[2] = w[2]; o[3] = w[3];
    }
  }
  if (extent) *extent = ext;
  return WindBuild::kNone;
}

// A plant's words, one pair a flat position: out[2 i] = A3 + B3 of flat index i's copy (the fourth
// components of the copy's first two rows as ExpandInstances decodes them: the row's translation),
// out[2 i + 1] = z of its mesh vertex (the decoded position's third component, in the mesh's own
// space). Index for index the order of ExpandInstances. Both zero where that has no position: an
// index the constants map to no copy, a vertex past the mesh positions. kNone, or, with nothing
// usable in `out`: kStream without a mesh, rows, `out` or a flat position, and for a copy the draw
// names whose rows lie past the instance stream; kValue for a sum or a z that is not finite in a
// position the draw names (none is made up, and none that is no number reaches a buffer).
inline WindBuild ExpandPlantWindWords(const capture::Float4* mesh, uint32_t mesh_count, const uint8_t* rows, size_t rows_size,
                                      const capture::InstanceSet& s, uint32_t flat_count, float* out) {
  if (!mesh || !rows || !out || flat_count == 0) return WindBuild::kStream;
  std::vector<float> seed;     // A3 + B3 per copy, made on first use
  std::vector<uint8_t> state;  // 0 unknown, 1 made
  for (uint32_t i = 0; i < flat_count; ++i) {
    uint32_t copy = 0, vertex = 0;
    out[2 * size_t(i)] = out[2 * size_t(i) + 1] = 0.0f;
    if (!capture::InstanceIndex(s, i, &copy, &vertex) || vertex >= mesh_count) continue;
    if (copy >= state.size()) {
      state.resize(size_t(copy) + 1, 0);
      seed.resize(size_t(copy) + 1, 0.0f);
    }
    if (state[copy] == 0) {
      capture::Float4 r[2];
      for (int k = 0; k < 2; ++k) {
        if (!capture::DecodePositions(rows, rows_size, s.rows[k], copy, 1, &r[k])) return WindBuild::kStream;
      }
      // (the third row is the position's too: a copy whose third row lies past the stream has no position)
      capture::Float4 third;
      if (!capture::DecodePositions(rows, rows_size, s.rows[2], copy, 1, &third)) return WindBuild::kStream;
      const float sum = r[0].w + r[1].w;
      if (!std::isfinite(sum)) return WindBuild::kValue;
      seed[copy] = sum;
      state[copy] = 1;
    }
    const float z = mesh[vertex].z;
    if (!std::isfinite(z)) return WindBuild::kValue;
    out[2 * size_t(i)] = seed[copy];
    out[2 * size_t(i) + 1] = z;
  }
  return WindBuild::kNone;
}

}  // namespace fable2::native::render
