#pragma once

// Pure helpers for the clay pass and its geometry cache (no SDK/GPU deps):
// cache keys, vertex counts, index range checks, draw colors, the root
// constant block, pipeline keys and the alpha rule of a draw's render state,
// texture counters and the F3 status text.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../capture/draw_record.h"
#include "../capture/render_state.h"
#include "../capture/shader_tally.h"
#include "frame_scene.h"
#include "geometry_cache_index.h"

namespace fable2::native::render {

struct ClayStats {
  uint32_t drawn = 0, skipped_bad_index = 0, uploads = 0, hits = 0;
  uint64_t resident_bytes = 0;
  double hash_ms = 0, decode_ms = 0, record_ms = 0;
  // Drawn records whose shader deforms the position (drawn undeformed).
  uint32_t deformed = 0;
  // Renderer-side skips other than bad indices: unreadable guest memory, a
  // failed decode or upload, or an empty triangle list.
  uint32_t skipped_other = 0;
  // Drawn records whose positions came from the instanced (flat stream) and
  // the bone-skinned builders, cache hits included. Counted with `drawn`: a
  // record whose index list fails afterwards is in neither.
  uint32_t instanced = 0, skinned = 0;
  // Render state (DecideDrawState). Of the drawn records: those drawn with the
  // alpha test (or alpha-to-mask) applied, with a blend, with a test or blend
  // the material has no alpha for (drawn without the test, with alpha 1), and
  // with a state that was not read (drawn with the default state) or a depth
  // function the RHI cannot express (the default depth state only; blend,
  // colour mask and cull stay). blend_unknown: records left out because their
  // state blends and their colour is not known, or the RHI cannot express the
  // blend.
  uint32_t alpha_tested = 0, blended = 0, alpha_unknown = 0, blend_unknown = 0, state_invalid = 0;
  // Pipelines created in the pass's cache (a slot whose creation failed is not
  // counted), and drawn records that used the default pipeline because the
  // cache was full or their pipeline could not be created.
  uint32_t pipelines = 0, pipeline_overflow = 0;
  // alpha_unknown and blend_unknown by pixel shader (kTallyAlphaUnknown, kTallyBlendUnknown).
  capture::ShaderTally<32> unknown_by_ps;
};

inline constexpr uint8_t kTallyAlphaUnknown = 0, kTallyBlendUnknown = 1;

// Albedo texture counters of one clay frame (TextureCache and the clay pass).
struct TextureStats {
  uint32_t textured = 0, resident = 0, uploads = 0, mirror = 0, base_only = 0;
  uint64_t resident_bytes = 0, upload_bytes = 0;
  double decode_ms = 0;
  uint32_t status[size_t(capture::MaterialStatus::kCount)] = {};  // final statuses of drawn records
  bool latched = false;  // an RHI failure turned the texture path off
};

enum class ClayColor : uint8_t { kClay, kDraw, kShader };

inline ClayColor ParseClayColor(std::string_view text, bool* recognized) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  std::string lower(text);
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  bool ok = true;
  ClayColor c = ClayColor::kClay;
  if (lower == "draw") c = ClayColor::kDraw;
  else if (lower == "shader") c = ClayColor::kShader;
  else if (lower != "clay") ok = false;
  if (recognized) *recognized = ok;
  return c;
}

// 64 -> 32 bit mix (murmur3 fmix64, folded).
inline uint32_t Hash32(uint64_t v) {
  v ^= v >> 33;
  v *= 0xFF51AFD7ED558CCDull;
  v ^= v >> 33;
  v *= 0xC4CEB9FE1A85EC53ull;
  v ^= v >> 33;
  return uint32_t(v ^ (v >> 32));
}

inline uint32_t HashCombine32(std::initializer_list<uint32_t> values) {
  uint64_t h = 0x9E3779B97F4A7C15ull;
  for (uint32_t v : values) h = (h ^ v) * 0x100000001B3ull + 0x632BE59BD9B4E019ull;
  return Hash32(h);
}

inline constexpr uint32_t kClayBaseColor = 0xB8B0A0u;

inline uint32_t ClayColorValue(ClayColor color, const capture::DrawRecord& r) {
  switch (color) {
    case ClayColor::kDraw: return 0x404040u | (Hash32(uint64_t(r.seq)) & 0xBFBFBFu);
    case ClayColor::kShader: return 0x404040u | (Hash32(r.vs_hash) & 0xBFBFBFu);
    default: return kClayBaseColor;
  }
}

// Positions in a stream of `vb_size` bytes: every vertex whose position lies
// fully inside the stream.
inline uint32_t PositionCount(uint32_t vb_size, const capture::PosLayout& l) {
  const uint32_t bytes = capture::PositionBytes(l.format);
  if (bytes == 0 || l.stride_bytes == 0) return 0;
  const uint64_t need = uint64_t(l.offset_bytes) + bytes;
  if (need > vb_size) return 0;
  return uint32_t((vb_size - need) / l.stride_bytes + 1);
}

// Vertices the record's position buffer holds: the terrain grid points, an
// instanced draw's flat stream, or every position in the stream.
inline uint32_t RecordVertexCount(const capture::DrawRecord& r) {
  if (r.terrain.active) {
    constexpr uint32_t kPoints = (capture::kTerrainGrid + 1) * (capture::kTerrainGrid + 1);
    return r.terrain.patches * kPoints;
  }
  if (r.instances.active) return r.instances.flat_count;
  return PositionCount(r.vb.size, r.pos);
}

inline uint32_t FloatBits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

inline uint32_t LayoutHash(const capture::PosLayout& l) {
  return HashCombine32({l.offset_bytes, uint32_t(l.format), uint32_t(l.exp_adjust), l.swizzle,
                        uint32_t(l.swap16), uint32_t(l.is_signed), uint32_t(l.normalized),
                        l.stride_bytes});
}

// Everything of an instance set that changes the flat stream built from a
// mesh: the instance stream's range and row layouts, the four index constants,
// the offset and the flat count (the content hash covers both streams' bytes).
inline uint32_t InstanceHash(const capture::InstanceSet& s) {
  return HashCombine32({s.rows_addr, s.rows_size, LayoutHash(s.rows[0]), LayoutHash(s.rows[1]),
                        LayoutHash(s.rows[2]), FloatBits(s.inv_count), FloatBits(s.count),
                        FloatBits(s.first), FloatBits(s.bias), FloatBits(s.offset[0]),
                        FloatBits(s.offset[1]), FloatBits(s.offset[2]), s.flat_count});
}

// Key of the record's vertex stream decoded on its own (kind 0): the stream
// plus every layout field that changes the decode, whatever is built from it
// afterwards. An instanced draw's mesh is decoded under this key once per
// frame for all the draws that expand it (FrameMemo).
inline GeoKey MeshPositionKey(const capture::DrawRecord& r) {
  GeoKey k;
  k.addr = r.vb.phys_addr;
  k.size = r.vb.size;
  k.stride = r.pos.stride_bytes;
  k.extra = LayoutHash(r.pos);
  k.kind = 0;
  return k;
}

// Cache key of a decoded position stream: the stream plus every layout field
// that changes the decode (the content hash covers the raw bytes only). A
// skinned stream adds its bone layout and palette range (kind 0); a terrain
// patch run is keyed by its heightmap and every patch parameter (kind 2); an
// instanced draw's flat stream is keyed by its mesh stream and its instance
// set (kind 5).
inline GeoKey PositionKey(const capture::DrawRecord& r) {
  GeoKey k;
  if (r.terrain.active) {
    const capture::TerrainPatch& t = r.terrain;
    const capture::HeightMap& m = t.map;
    k.addr = m.phys_addr;
    k.size = m.size;
    k.stride = capture::kTerrainGrid;
    k.extra = HashCombine32(
        {FloatBits(t.patch), t.patches, FloatBits(t.cols), FloatBits(t.inv_cols),
         FloatBits(t.cell[0]), FloatBits(t.cell[1]), FloatBits(t.height_scale),
         FloatBits(t.origin[0]), FloatBits(t.origin[1]), FloatBits(t.tex_offset[0]),
         FloatBits(t.tex_offset[1]), FloatBits(t.tex_scale[0]), FloatBits(t.tex_scale[1]), m.width,
         m.height, m.pitch, m.endian, uint32_t(m.tiled), uint32_t(m.clamp_x), uint32_t(m.clamp_y)});
    k.kind = 2;
    return k;
  }
  k = MeshPositionKey(r);
  if (r.instances.active) {
    k.extra = HashCombine32({k.extra, InstanceHash(r.instances)});
    k.kind = 5;
    return k;
  }
  if (r.skin.active) {
    const capture::BoneSkin& s = r.skin;
    k.extra = HashCombine32({k.extra, s.bones, s.index_offset_bytes, s.index_shift[0], s.index_shift[1],
                             s.index_shift[2], s.index_shift[3], s.index_endian, uint32_t(s.weighted),
                             s.weight_offset_bytes, s.weight_shift[0], s.weight_shift[1], s.weight_shift[2],
                             s.weight_shift[3], s.palette_addr, s.palette_size, s.bone_stride,
                             LayoutHash(s.rows[0]), LayoutHash(s.rows[1]), LayoutHash(s.rows[2])});
  }
  k.kind = 0;
  return k;
}

inline GeoKey IndexKey(const capture::DrawRecord& r) {
  GeoKey k;
  if (r.terrain.active) {
    // The grid's triangle list depends only on its shape.
    k.stride = capture::kTerrainGrid;
    k.extra = r.terrain.patches;
    k.kind = 3;
    return k;
  }
  k.addr = r.indexed ? r.ib.phys_addr : 0;
  k.size = r.indexed ? r.ib.size : 0;
  k.stride = r.index32 ? 1 : 0;
  k.extra = HashCombine32({r.prim, r.start, r.count, uint32_t(r.indexed)});
  k.kind = 1;
  return k;
}

// Key of the record's UV stream decoded on its own (kind 4; kind 3 is the
// terrain grid's index list): the stream holding the UV element plus every
// layout field that changes the decode. Like MeshPositionKey, also the
// per-frame key of an instanced draw's mesh UVs.
inline GeoKey MeshUvKey(const capture::DrawRecord& r) {
  const capture::Material& m = r.material;
  const capture::UvLayout& l = m.uv;
  GeoKey k;
  k.addr = m.uv_vb.phys_addr;
  k.size = m.uv_vb.size;
  k.stride = l.stride_bytes;
  k.extra = HashCombine32({l.offset_bytes, uint32_t(l.format), uint32_t(l.comp_u),
                           uint32_t(l.comp_v), uint32_t(l.swap16), uint32_t(l.normalized),
                           uint32_t(l.is_signed), uint32_t(l.exp_adjust),
                           uint32_t(l.v_element_delta)});
  k.kind = 4;
  return k;
}

// Everything of an instance set that changes the flat UVs built from a mesh's
// UVs: uvs[i] = mesh_uv[vertex(i)] (ExpandInstanceUvs), and vertex(i) takes
// the index, the bias, inv_count and count. The first copy, the instance
// stream, its row layouts and the offset only place the copies; they stay out,
// so a draw whose instance batch changed keeps its UV buffer.
// ExpandInstanceUvs never reads the instance stream. It writes a zero UV in
// two cases only: InstanceIndex fails (the copy number, which is where the
// first copy enters, is negative, not finite or at least 65,536), or the
// vertex is past the mesh UVs decoded. InstanceRangeSkip records no draw with
// the first case: every index below flat_count maps to a copy below 65,536
// and to a vertex below count. The second depends on count and on the mesh UV
// stream, which are in the key.
inline uint32_t InstanceUvHash(const capture::InstanceSet& s) {
  return HashCombine32({FloatBits(s.inv_count), FloatBits(s.count), FloatBits(s.bias), s.flat_count});
}

// Cache key of a decoded UV stream: MeshUvKey; an instanced draw's flat UVs
// (kind 6) add InstanceUvHash. Their content hash is the mesh UV stream's.
inline GeoKey UvKey(const capture::DrawRecord& r) {
  GeoKey k = MeshUvKey(r);
  if (r.instances.active) {
    k.extra = HashCombine32({k.extra, InstanceUvHash(r.instances)});
    k.kind = 6;
  }
  return k;
}

// Mesh vertices (positions or UVs) to decode for an instanced draw, of the
// `stream_count` its stream holds: every index maps to a vertex below
// s.count (InstanceIndex, checked at capture by InstanceBoundsOk), so one
// copy's vertices are enough when the stream holds more. 0 for a count that
// is not at least 1 (NaN included), no bound from a count past 2^32: the
// capture records neither, and neither reaches the cast.
inline uint32_t InstanceMeshCount(uint32_t stream_count, const capture::InstanceSet& s) {
  if (!(s.count >= 1.0f)) return 0;
  if (s.count >= 4294967296.0f) return stream_count;
  return std::min(stream_count, uint32_t(s.count));
}

// UVs in a stream of `vb_size` bytes: every vertex whose UV element (both
// elements when u and v sit apart) lies fully inside the stream.
inline uint32_t UvCount(uint32_t vb_size, const capture::UvLayout& l) {
  const uint32_t need = capture::UvElementEnd(l);
  if (need == 0 || l.stride_bytes == 0 || need > vb_size) return 0;
  return uint32_t((vb_size - need) / l.stride_bytes + 1);
}

struct IndexBytes {
  uint32_t offset = 0, size = 0;
};

// The index bytes an indexed draw references, within its index buffer.
inline bool ReferencedIndexBytes(const capture::DrawRecord& r, IndexBytes* out) {
  const uint64_t width = r.index32 ? 4 : 2;
  const uint64_t begin = uint64_t(r.start) * width;
  const uint64_t end = (uint64_t(r.start) + r.count) * width;
  if (end > r.ib.size) return false;
  out->offset = uint32_t(begin);
  out->size = uint32_t(end - begin);
  return true;
}

// Vertex count the shader and the kBadIndex check may use: never more than
// the uploaded buffer holds (root SRVs have no hardware bounds check, and a
// key collision could pair a record with a smaller decoded stream).
inline uint32_t DrawVertexCount(uint32_t buffer_count, uint32_t record_count) {
  return std::min(buffer_count, record_count);
}

// The renderer's kBadIndex rule: the highest referenced vertex
// (max_index + base_vertex) must lie inside the position stream. Lower
// vertices below 0 are left to the shader's per-vertex bounds check.
inline bool IndexRangeValid(uint32_t max_index, int32_t base_vertex, uint32_t vertex_count) {
  const int64_t hi = int64_t(max_index) + base_vertex;
  return hi >= 0 && hi < int64_t(vertex_count);
}

// A draw's render state as a pipeline of the clay pass: which pixel shader, and
// blend, colour mask, depth and cull in the values the RHI can express. Factors
// and operations are capture::Factor / BlendOp values, depth_func a
// capture::Compare on the pass's own depth (not reversed), cull a capture::Cull
// in the pass's window space, where clockwise faces are front (ClayCull). The
// alpha test is not pipeline state: it is a discard driven by ClayAlpha.
struct PipelineKey {
  uint8_t textured = 0;      // 0 flat, 1 textured, 2 terrain layer, 3 terrain far
  uint8_t blend = 0, src = 1, dst = 0, op = 0, src_alpha = 1, dst_alpha = 0, op_alpha = 0;
  uint8_t color_mask = 0xF;
  uint8_t depth_test = 1, depth_write = 1, depth_func = 1;   // Compare values
  uint8_t cull = 0;          // Cull values
  bool operator==(const PipelineKey&) const = default;
};

// The game's depth function on a depth buffer of the other direction.
inline capture::Compare MirrorCompare(capture::Compare f) {
  switch (f) {
    case capture::Compare::kLess: return capture::Compare::kGreater;
    case capture::Compare::kLessEqual: return capture::Compare::kGreaterEqual;
    case capture::Compare::kGreater: return capture::Compare::kLess;
    case capture::Compare::kGreaterEqual: return capture::Compare::kLessEqual;
    default: return f;
  }
}

// The depth function the pass draws with: its depth is not reversed.
inline capture::Compare ClayDepthFunc(const capture::RenderState& s) {
  return s.depth_reversed ? MirrorCompare(s.depth_func) : s.depth_func;
}

// What the RHI's enums hold (native_rhi.h: BlendFactor, BlendOp, CompareFunc).
inline bool RhiFactor(capture::Factor f) {
  return f == capture::Factor::kZero || f == capture::Factor::kOne || f == capture::Factor::kSrcAlpha ||
         f == capture::Factor::kInvSrcAlpha;
}
inline bool RhiBlendOp(capture::BlendOp op) {
  return op == capture::BlendOp::kAdd || op == capture::BlendOp::kMin;
}
inline bool BlendSupported(const capture::RenderState& s) {
  return RhiFactor(s.src) && RhiFactor(s.dst) && RhiFactor(s.src_alpha) && RhiFactor(s.dst_alpha) &&
         RhiBlendOp(s.op) && RhiBlendOp(s.op_alpha);
}
// False: the depth test is on with a function the RHI cannot express on the
// pass's depth; the draw then uses the default depth state.
inline bool DepthSupported(const capture::RenderState& s) {
  if (!s.depth_test) return true;
  const capture::Compare f = ClayDepthFunc(s);
  return f == capture::Compare::kAlways || f == capture::Compare::kLess || f == capture::Compare::kLessEqual;
}

// The captured cull mode in the pass's window space, where clockwise faces are
// front (the RHI has no front-face control; D3D12's default). The pass draws the
// game's own clip positions into a viewport of the same orientation, so a
// triangle keeps the winding it has in the game: with clockwise front faces
// (PA_SU_SC_MODE_CNTL bit 2) the mode is the captured one, with
// counter-clockwise ones front and back trade places. Checked against the
// emulated frame for meshes and terrain: frame-map section 13, "Render state".
inline capture::Cull ClayCull(capture::Cull cull, bool front_cw) {
  if (cull == capture::Cull::kNone || front_cw) return cull;
  return cull == capture::Cull::kFront ? capture::Cull::kBack : capture::Cull::kFront;
}

// An invalid state gives the default key of `textured`. A blend the RHI cannot
// express gives no blend (DecideDrawState leaves such a draw out), a depth
// function it cannot express the default depth state. Without the depth test
// nothing is written to depth, as on the console.
inline PipelineKey MakePipelineKey(const capture::RenderState& s, uint8_t textured) {
  PipelineKey k;
  k.textured = textured;
  if (!s.valid) return k;
  if (s.blend && BlendSupported(s)) {
    k.blend = 1;
    k.src = uint8_t(s.src); k.dst = uint8_t(s.dst); k.op = uint8_t(s.op);
    k.src_alpha = uint8_t(s.src_alpha); k.dst_alpha = uint8_t(s.dst_alpha); k.op_alpha = uint8_t(s.op_alpha);
  }
  k.color_mask = s.color_mask;
  if (!s.depth_test) {
    k.depth_test = 0;
    k.depth_write = 0;
  } else if (DepthSupported(s)) {
    k.depth_write = s.depth_write ? 1 : 0;
    k.depth_func = uint8_t(ClayDepthFunc(s));
  }
  k.cull = uint8_t(ClayCull(s.cull, s.front_cw));
  return k;
}

inline constexpr size_t kMaxClayPipelines = 64;
inline constexpr size_t kNoPipelineSlot = ~size_t(0);

// Index of `k` in `keys`, appended if absent and there is room; kNoPipelineSlot when the
// cache is full (the caller then draws with the default pipeline and counts the overflow).
inline size_t FindOrAddPipelineSlot(std::vector<PipelineKey>& keys, const PipelineKey& k,
                                    size_t max = kMaxClayPipelines) {
  for (size_t i = 0; i < keys.size(); ++i) {
    if (keys[i] == k) return i;
  }
  if (keys.size() >= max) return kNoPipelineSlot;
  keys.push_back(k);
  return keys.size() - 1;
}

// ClayConstants::alpha_mode: bits 0-2 the alpha test's compare function
// (capture::Compare), then the flags below; bits 5-6 are the albedo component
// (0-3 = x-w) that is the alpha; bits 16-23 the alpha-to-mask dither offsets
// (RenderState::alpha_to_mask_offsets).
inline constexpr uint32_t kAlphaTest = 1u << 3;        // discard unless alpha <func> alpha_ref
inline constexpr uint32_t kAlphaFromAlbedo = 1u << 4;  // alpha is an albedo component (else the literal 1)
inline constexpr uint32_t kAlphaBlend = 1u << 7;       // write the alpha (else 1)
inline constexpr uint32_t kAlphaToMask = 1u << 8;      // discard unless AlphaToMaskCovers
inline constexpr uint32_t kAlphaDivide = 1u << 9;      // the colour is the albedo's divided by its alpha
inline constexpr uint32_t kAlphaToMaskShift = 16;      // the four 2-bit offsets

// Alpha-to-mask as the GPU does it without multisampling, which is what the
// textured pixel shader does after the alpha test (the emulator's rule, SDK
// dxbc_translator_om.cpp CompletePixelShader_AlphaToMask): the pixel (x, y) is
// covered iff alpha >= 1 - offset / 4, with the offset of its parity,
// index (x & 1) * 2 + (y & 1), two bits each from bit 0 of `offsets`. The alpha
// is not clamped and a NaN covers nothing. 0xAA: 0.5 at every pixel; the dither
// pattern 0x87: 0.25, 0.75, 1.0, 0.5 for indices 0 to 3, so a fading draw keeps
// none, one, two, three or all of each 2x2 block.
inline bool AlphaToMaskCovers(uint8_t offsets, uint32_t x, uint32_t y, float alpha) {
  const uint32_t index = ((x & 1u) << 1) | (y & 1u);
  const uint32_t offset = (uint32_t(offsets) >> (2 * index)) & 3u;
  return alpha >= 1.0f - float(offset) * 0.25f;
}

// The alpha fields of the root constants; the default is an opaque draw.
struct ClayAlpha {
  uint32_t mode = 0;
  float ref = 0.0f;
  float scale = 1.0f;  // Material::alpha_scale
};

// What the pass does with one record.
struct DrawState {
  bool skip = false;  // blend-unknown: the record is not drawn
  PipelineKey key;
  ClayAlpha alpha;
  // The ClayStats counters the drawn record adds to.
  bool alpha_tested = false, blended = false, alpha_unknown = false, state_invalid = false;
};

// `textured`: the PipelineKey value the record is drawn with this frame (0: flat
// clay, its colour is not known). `flat_terrain`: a terrain patch drawn as flat
// clay; its blend is the layer's own, which flat clay does not have, so it is
// drawn opaque and not left out. Rules (DecideAlpha): a blended draw whose
// colour is unknown, or whose blend the RHI cannot express, is left out; a test
// or blend without an alpha the pixel shader can form (no recipe, a flat draw,
// a recipe that needs another fetch) is drawn without the test and with alpha
// 1; an invalid state or an inexpressible depth function draws with the default
// state. A colour mask of 0 writes no colour, so its blend does not matter.
inline DrawState DecideDrawState(const capture::RenderState& rs, const capture::Material& m,
                                 uint8_t textured, bool flat_terrain) {
  DrawState d;
  d.key = MakePipelineKey(rs, textured);
  d.state_invalid = !rs.valid || !DepthSupported(rs);
  if (!rs.valid) return d;
  const bool blend = rs.blend && rs.color_mask != 0 && !flat_terrain;
  if (!blend) {
    const PipelineKey opaque;
    d.key.blend = 0;
    d.key.src = opaque.src; d.key.dst = opaque.dst; d.key.op = opaque.op;
    d.key.src_alpha = opaque.src_alpha; d.key.dst_alpha = opaque.dst_alpha; d.key.op_alpha = opaque.op_alpha;
  } else if (!BlendSupported(rs)) {
    d.skip = true;
    return d;
  }
  const bool colour_known = textured != 0;
  const capture::AlphaSpec& a = m.alpha;
  const bool has_recipe = colour_known && (a.source == capture::AlphaSource::kAlbedo ||
                                           a.source == capture::AlphaSource::kOne);
  // The shader divides its colour by the alpha whatever the state is.
  if (has_recipe && a.divide_colour) d.alpha.mode |= kAlphaDivide;
  const bool test = rs.alpha_test || rs.alpha_to_mask;
  switch (capture::DecideAlpha(test, blend, colour_known, has_recipe)) {
    case capture::AlphaRule::kOpaque:
      break;
    case capture::AlphaRule::kSkipBlendUnknown:
      d.skip = true;
      break;
    case capture::AlphaRule::kDrawAlphaUnknown:
      d.alpha_unknown = true;
      d.blended = blend;
      break;
    case capture::AlphaRule::kApply:
      d.alpha_tested = test;
      d.blended = blend;
      if (rs.alpha_test) d.alpha.mode |= (uint32_t(rs.alpha_func) & 7u) | kAlphaTest;
      if (rs.alpha_to_mask) {
        d.alpha.mode |= kAlphaToMask | (uint32_t(rs.alpha_to_mask_offsets) << kAlphaToMaskShift);
      }
      if (a.source == capture::AlphaSource::kAlbedo) {
        d.alpha.mode |= kAlphaFromAlbedo | (uint32_t(a.component & 3) << 5);
      }
      if (blend) d.alpha.mode |= kAlphaBlend;
      d.alpha.ref = rs.alpha_ref;
      d.alpha.scale = m.alpha_scale;
      break;
  }
  return d;
}

// Root constants, laid out as the clay HLSL cbuffer (36 dwords).
struct ClayConstants {
  float rows[16];
  uint32_t layout;
  int32_t base_vertex;
  uint32_t vertex_count;
  uint32_t color;
  float uv[4];          // u * uv[0] + uv[2], v * uv[1] + uv[3]
  uint32_t textured;    // 1: sample the albedo at t3
  uint32_t sampler;     // static sampler s0-s3 (SamplerIndex)
  uint32_t alpha_mode;  // ClayAlpha::mode
  float alpha_ref;
  float cut[4];         // eye xyz, squared distance (DrawRecord::cut; +inf: no cut)
  float alpha_scale;
  uint32_t pad2[3];
};
static_assert(sizeof(ClayConstants) == 144);

inline ClayConstants MakeClayConstants(const capture::DrawRecord& r, uint32_t vertex_count,
                                       uint32_t color, const float uv[4], bool textured,
                                       uint32_t sampler, const ClayAlpha& alpha = {}) {
  ClayConstants c{};
  std::copy(std::begin(r.rows), std::end(r.rows), std::begin(c.rows));
  c.layout = r.layout == capture::TransformLayout::kCombine ? 1u : 0u;
  c.base_vertex = r.base_vertex;
  c.vertex_count = vertex_count;
  c.color = color;
  std::copy(uv, uv + 4, std::begin(c.uv));
  c.textured = textured ? 1u : 0u;
  c.sampler = sampler;
  c.alpha_mode = alpha.mode;
  c.alpha_ref = alpha.ref;
  std::copy(std::begin(r.cut), std::end(r.cut), std::begin(c.cut));
  c.alpha_scale = alpha.scale;
  return c;
}

// The F3 texture line: textured share of the drawn records and of the drawn
// non-terrain records (spec success criterion 2), cache counters and the top
// three final statuses other than textured, among which the state reasons rank:
// `alpha-unknown` (drawn records whose alpha test or blend had no alpha to
// apply) and `blend-unknown` (records left out, ClayStats). "Textures: off
// (latched)" once an RHI failure turned the texture path off.
inline std::string FormatTextureText(const TextureStats& ts, uint32_t drawn, uint32_t alpha_unknown = 0,
                                     uint32_t blend_unknown = 0) {
  if (ts.latched) return "Textures: off (latched)";
  std::vector<std::pair<const char*, uint32_t>> reasons;
  for (size_t i = 0; i < size_t(capture::MaterialStatus::kCount); ++i) {
    if (i == size_t(capture::MaterialStatus::kTextured) || ts.status[i] == 0) continue;
    reasons.emplace_back(capture::MaterialStatusName(capture::MaterialStatus(i)), ts.status[i]);
  }
  if (alpha_unknown) reasons.emplace_back("alpha-unknown", alpha_unknown);
  if (blend_unknown) reasons.emplace_back("blend-unknown", blend_unknown);
  std::stable_sort(reasons.begin(), reasons.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  const uint32_t share = drawn ? uint32_t(uint64_t(ts.textured) * 100 / drawn) : 0;
  const uint32_t terrain = ts.status[size_t(capture::MaterialStatus::kTerrain)];
  const uint32_t non_terrain = drawn > terrain ? drawn - terrain : 0;
  const uint32_t share_nt = non_terrain ? uint32_t(uint64_t(ts.textured) * 100 / non_terrain) : 0;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "Textures: textured %u of %u drawn (%u%%, %u%% non-terrain), resident %u (%.1f MB), "
                "uploads %u (%.1f MB), decode %.2f ms",
                ts.textured, drawn, share, share_nt, ts.resident,
                double(ts.resident_bytes) / (1024.0 * 1024.0), ts.uploads,
                double(ts.upload_bytes) / (1024.0 * 1024.0), ts.decode_ms);
  std::string text = buf;
  if (!reasons.empty()) {
    text += " | top untextured:";
    for (size_t i = 0; i < reasons.size() && i < 3; ++i) {
      text += std::string(i ? ", " : " ") + reasons[i].first + " " + std::to_string(reasons[i].second);
    }
  }
  return text;
}

// The F3 state line: pipelines in the pass's cache (with the draws that found
// no pipeline there, when there are any) and the drawn records with the alpha
// test applied, with a blend, and with an invalid state.
inline std::string FormatStateText(const ClayStats& st) {
  std::string text = "State: " + std::to_string(st.pipelines) + " pipelines";
  if (st.pipeline_overflow) text += " (overflow " + std::to_string(st.pipeline_overflow) + ")";
  return text + ", alpha test " + std::to_string(st.alpha_tested) + ", blended " +
         std::to_string(st.blended) + ", invalid " + std::to_string(st.state_invalid);
}

// The log's breakdown of alpha-unknown and blend-unknown by pixel shader (top
// eight each); empty when there is neither.
inline std::string FormatUnknownByPs(const ClayStats& st) {
  constexpr size_t kTop = 8;
  std::string text;
  const std::pair<uint8_t, const char*> kinds[] = {{kTallyAlphaUnknown, "alpha-unknown"},
                                                   {kTallyBlendUnknown, "blend-unknown"}};
  for (const auto& [kind, name] : kinds) {
    capture::ShaderTally<32>::Entry top[kTop];
    const size_t n = st.unknown_by_ps.Top(kind, top, kTop);
    if (!n) continue;
    text += std::string(text.empty() ? "" : " ") + name + " by ps {";
    char buf[48];
    for (size_t i = 0; i < n; ++i) {
      std::snprintf(buf, sizeof(buf), "%s0x%016llX: %u", i ? ", " : "",
                    static_cast<unsigned long long>(top[i].hash), top[i].count);
      text += buf;
    }
    text += "}";
  }
  return text;
}

// Five F3 lines: capture/draw counts with the top three skip reasons (records
// the pass left out as blend-unknown among them), the geometry cache counters
// and GPU-thread CPU timings, the state line, the guest-thread capture time of
// the scene's frame, then the texture line (tex nullptr: the texture path is
// off).
inline std::string FormatStatusText(const FrameScene& scene, const ClayStats& st,
                                    const TextureStats* tex) {
  std::vector<std::pair<std::string, uint64_t>> reasons;
  for (size_t i = 0; i < size_t(capture::SkipReason::kCount); ++i) {
    uint64_t n = scene.skipped[i];
    if (i == size_t(capture::SkipReason::kBadIndex)) n += st.skipped_bad_index;
    if (n) reasons.emplace_back(capture::SkipReasonName(capture::SkipReason(i)), n);
  }
  if (st.skipped_other) reasons.emplace_back("render-other", st.skipped_other);
  if (st.blend_unknown) reasons.emplace_back("blend-unknown", st.blend_unknown);
  uint64_t skipped = 0;
  for (const auto& r : reasons) skipped += r.second;
  std::stable_sort(reasons.begin(), reasons.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string text = "Native: captured " + std::to_string(scene.captured) + ", drawn " +
                     std::to_string(st.drawn) + " (deformed " + std::to_string(st.deformed) +
                     "), skipped " + std::to_string(skipped);
  if (!reasons.empty()) {
    text += " (top:";
    for (size_t i = 0; i < reasons.size() && i < 3; ++i) {
      text += (i ? ", " : " ") + reasons[i].first + " " + std::to_string(reasons[i].second);
    }
    text += ")";
  }
  char geo[256];
  std::snprintf(geo, sizeof(geo),
                "\nGeometry: %u uploads, %u hits, %.1f MB resident, instanced %u, skinned %u | "
                "hash %.2f ms, decode %.2f ms, record %.2f ms",
                st.uploads, st.hits, double(st.resident_bytes) / (1024.0 * 1024.0), st.instanced,
                st.skinned, st.hash_ms, st.decode_ms, st.record_ms);
  char cap[64];
  std::snprintf(cap, sizeof(cap), "\nCapture: %.2f ms guest time per frame\n", scene.capture_ms);
  return text + geo + "\n" + FormatStateText(st) + cap +
         (tex ? FormatTextureText(*tex, st.drawn, st.alpha_unknown, st.blend_unknown) : std::string("Textures: off"));
}

}  // namespace fable2::native::render
