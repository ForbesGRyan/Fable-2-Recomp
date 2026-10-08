#pragma once

// Pure helpers for the clay pass and its geometry cache (no SDK/GPU deps):
// cache keys, vertex counts, index range checks, draw colors, the root
// constant block, pipeline keys and the alpha rule of a draw's render state,
// the terrain material (its constants, the fallback of a ground square),
// texture counters and the F3 status text.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../capture/draw_record.h"
#include "../capture/render_state.h"
#include "../capture/shader_tally.h"
#include "frame_scene.h"
#include "geometry_cache_index.h"
#include "normal_decode.h"

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
  // counted), and records that found no pipeline for their state because the
  // cache was full or the pipeline could not be created: drawn records that
  // used the default pipeline, and terrain records that lost their material
  // for it (terrain-inputs; counted once, also when their flat draw finds none).
  uint32_t pipelines = 0, pipeline_overflow = 0;
  // alpha_unknown and blend_unknown by pixel shader (kTallyAlphaUnknown, kTallyBlendUnknown).
  capture::ShaderTally<32> unknown_by_ps;
  // Terrain layer records left out because their ground square is drawn as flat
  // clay by another record (PlanTerrain), and the distinct (colour, weights,
  // side) texture sets bound for the terrain records drawn with their material.
  uint32_t terrain_skipped = 0, terrain_tuples = 0;
  // Terrain records that could not have their material this frame, by their own
  // reason (TerrainMaterialStatus), whatever the plan then did with them: drawn
  // flat under that reason, drawn flat or left out under terrain-square.
  uint32_t terrain_unusable[size_t(capture::MaterialStatus::kCount)] = {};
  // The same for terrain records whose geometry could not be built (their squares
  // fall back too), by what was missing: the positions (the heightmap could not
  // be read, the upload failed) or, with positions, the indices. Counted while the
  // texture path is on, like terrain_unusable.
  uint32_t terrain_no_positions = 0, terrain_no_indices = 0;
  // The light inputs of the prepared records (frame_scene.h, LightOf) as the
  // geometry cache built them. Nothing is drawn from them yet. lit: prepared
  // records with light inputs. light_ready: those whose normals, and for the
  // family with a colour stream its colours, were built. light_missing: the
  // others by reason (index: capture::LightReason; normal-unsupported: a normal
  // element no reader decodes; light-unknown: a stream that could not be read
  // or holds too few elements, a failed upload), and by vertex shader
  // (light_missing_by_vs; the kind is the reason). light_rotation: the world
  // rotation of the object and trunk records among the ready ones, by
  // RotationKind (normal_decode.h), and those that are not a rotation by
  // vertex shader (light_rotation_by_vs; the kind is the RotationKind).
  uint32_t lit = 0, light_ready = 0;
  uint32_t light_missing[size_t(capture::LightReason::kCount)] = {};
  capture::ShaderTally<32> light_missing_by_vs;
  uint32_t light_rotation[size_t(RotationKind::kCount)] = {};
  capture::ShaderTally<32> light_rotation_by_vs;
};

inline constexpr uint8_t kTallyAlphaUnknown = 0, kTallyBlendUnknown = 1;

// Albedo texture counters of one clay frame (TextureCache and the clay pass).
struct TextureStats {
  uint32_t textured = 0, resident = 0, uploads = 0, mirror = 0, base_only = 0;
  uint64_t resident_bytes = 0, upload_bytes = 0;
  double decode_ms = 0;
  uint32_t status[size_t(capture::MaterialStatus::kCount)] = {};  // final statuses of drawn records
  // Drawn terrain records, and those of them drawn with their material (counted in `textured` too).
  uint32_t terrain = 0, terrain_textured = 0;
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
// instanced draw's flat stream, or every position in the stream. `terrain`,
// here and in the keys below: the record's terrain inputs from its scene's
// side array (TerrainOf), null for a record that is not a terrain record.
inline uint32_t RecordVertexCount(const capture::DrawRecord& r, const capture::TerrainPatch* terrain) {
  if (terrain) {
    constexpr uint32_t kPoints = (capture::kTerrainGrid + 1) * (capture::kTerrainGrid + 1);
    return terrain->patches * kPoints;
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

// Everything of a skin that changes what is built from a stream through its
// bones: the bone words' layout, the palette's range and its rows' layouts (the
// content hash covers the palette's bytes).
inline uint32_t SkinHash(const capture::BoneSkin& s) {
  return HashCombine32({s.bones, s.index_offset_bytes, s.index_shift[0], s.index_shift[1], s.index_shift[2],
                        s.index_shift[3], s.index_endian, uint32_t(s.weighted), s.weight_offset_bytes,
                        s.weight_shift[0], s.weight_shift[1], s.weight_shift[2], s.weight_shift[3],
                        s.palette_addr, s.palette_size, s.bone_stride, LayoutHash(s.rows[0]),
                        LayoutHash(s.rows[1]), LayoutHash(s.rows[2])});
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
inline GeoKey PositionKey(const capture::DrawRecord& r, const capture::TerrainPatch* terrain) {
  GeoKey k;
  if (terrain) {
    const capture::TerrainPatch& t = *terrain;
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
  if (r.skin.active) k.extra = HashCombine32({k.extra, SkinHash(r.skin)});
  k.kind = 0;
  return k;
}

// --- Normals and colours (normal_decode.h; frame-map section 14, "Normals") ---

// Key of a normal stream decoded on its own (kind 9): the stream holding the
// element and what changes its decode (where the element lies, its format and
// the source components the reader takes, LightElement::source). Nothing of
// the draw: a plain mesh's normals are in the stream's own space, one buffer
// however many draws place and turn the mesh (the world rotation is the
// draw's, LightInputs::world_rot). The sign and scale flags and the endian
// decide only whether the element is read at all (NormalElementSupported),
// and where a shader's fetch puts the components in its register is not in
// the element, so two shaders that fetch one stream's normal differently
// share its buffer. The content hash is the stream's.
inline GeoKey MeshNormalKey(const capture::LightInputs& l) {
  const capture::LightElement& e = l.normal;
  GeoKey k;
  k.addr = e.vb.phys_addr;
  k.size = e.vb.size;
  k.stride = e.stride_bytes;
  k.extra = HashCombine32({e.offset_bytes, e.format, e.source, e.source_count});
  k.kind = 9;
  return k;
}

// Everything of an instance set that changes the flat normals built from a
// mesh's normals: InstanceHash without the offset, which moves positions only
// (ExpandInstanceNormals reads the rows' 3 x 3; the content hash covers the
// mesh's and the instance stream's bytes).
inline uint32_t InstanceNormalHash(const capture::InstanceSet& s) {
  return HashCombine32({s.rows_addr, s.rows_size, LayoutHash(s.rows[0]), LayoutHash(s.rows[1]),
                        LayoutHash(s.rows[2]), FloatBits(s.inv_count), FloatBits(s.count),
                        FloatBits(s.first), FloatBits(s.bias), s.flat_count});
}

// Cache key of a record's normal buffer, as PositionKey is of its positions:
// MeshNormalKey; a skinned stream adds its bone layout and palette range and
// the position stream its bone words are read from (kind 9; the content hash
// adds that stream's and the palette's bytes); an instanced draw's flat
// normals add InstanceNormalHash (kind 10; the content hash adds the instance
// stream's bytes).
inline GeoKey NormalKey(const capture::DrawRecord& r, const capture::LightInputs& l) {
  GeoKey k = MeshNormalKey(l);
  if (r.instances.active) {
    k.extra = HashCombine32({k.extra, InstanceNormalHash(r.instances)});
    k.kind = 10;
    return k;
  }
  if (r.skin.active) {
    k.extra = HashCombine32({k.extra, SkinHash(r.skin), r.vb.phys_addr, r.vb.size, r.pos.stride_bytes});
  }
  return k;
}

// Cache key of a record's colour buffer (kind 11), which holds `count` colours
// of the stream from element LightInputs::colour_first on (DecodeColours): the
// stream, where the element lies, the source components the reader takes, the
// draw's offset into the stream and the
// count. Meshes that share one colour stream at different offsets are different
// entries; draws of one mesh share theirs. The content hash is the stream's.
inline GeoKey ColourKey(const capture::LightInputs& l, uint32_t count) {
  const capture::LightElement& e = l.second;
  GeoKey k;
  k.addr = e.vb.phys_addr;
  k.size = e.vb.size;
  k.stride = e.stride_bytes;
  k.extra = HashCombine32({e.offset_bytes, e.format, e.source, e.source_count, l.colour_first, count});
  k.kind = 11;
  return k;
}

inline GeoKey IndexKey(const capture::DrawRecord& r, const capture::TerrainPatch* terrain) {
  GeoKey k;
  if (terrain) {
    // The grid's triangle list depends only on its shape.
    k.stride = capture::kTerrainGrid;
    k.extra = terrain->patches;
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

// Cache key of a far terrain draw's colour coordinates (kind 7), which
// BuildTerrainFarUvs reads from the adaptive vertex shader's two tables: the
// tables (fetch dwords 0 to 2 of each: pitch and tiling, address, format and
// endian, size), the draw's patches and the constants that address the tables.
// The content hash covers both tables' bytes. The heightmap, the height scale
// and what changes with the eye (fade, eye) do not enter.
inline GeoKey TerrainFarUvKey(const capture::TerrainPatch& t) {
  const capture::TerrainMaterialInputs& m = t.material;
  GeoKey k;
  k.addr = m.far_u_fetch[1] & 0xFFFFF000u;
  k.size = m.far_v_fetch[1] & 0xFFFFF000u;  // (the second table's address)
  k.stride = capture::kTerrainGrid;
  k.extra = HashCombine32({m.far_u_fetch[0], m.far_u_fetch[1], m.far_u_fetch[2], m.far_v_fetch[0], m.far_v_fetch[1],
                           m.far_v_fetch[2], FloatBits(t.patch), t.patches, FloatBits(t.cols), FloatBits(t.inv_cols),
                           FloatBits(m.far_table[0]), FloatBits(m.far_table[1]), FloatBits(m.far_table[2]),
                           FloatBits(m.far_table[3]), FloatBits(m.far_patch[0]), FloatBits(m.far_patch[1])});
  k.kind = 7;
  return k;
}

// Cache key of a terrain record's vertex normals (kind 8), which
// BuildTerrainNormals reads from the terrain vertex shader's normal map at the
// grid points, for the light and for the steep-ground block: the map (fetch
// dwords 0 to 2: pitch, tiling and clamp modes, address, format and endian,
// size), the draw's patches and what places a grid point on the map (cell,
// origin, the height coordinate's offset and scale). The content hash covers
// the map's bytes. The heightmap, the height scale and everything of the draw
// itself (its textures and constants) do not enter: the layers of a square
// share one buffer.
inline GeoKey TerrainNormalKey(const capture::TerrainPatch& t) {
  const capture::TerrainMaterialInputs& m = t.material;
  GeoKey k;
  k.addr = m.normal_fetch[1] & 0xFFFFF000u;
  k.size = m.normal_fetch[2];
  k.stride = capture::kTerrainGrid;
  k.extra = HashCombine32({m.normal_fetch[0], m.normal_fetch[1], m.normal_fetch[2], FloatBits(t.patch), t.patches,
                           FloatBits(t.cols), FloatBits(t.inv_cols), FloatBits(t.cell[0]), FloatBits(t.cell[1]),
                           FloatBits(t.origin[0]), FloatBits(t.origin[1]), FloatBits(t.tex_offset[0]),
                           FloatBits(t.tex_offset[1]), FloatBits(t.tex_scale[0]), FloatBits(t.tex_scale[1])});
  k.kind = 8;
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
inline constexpr uint8_t kTexturedTerrainLayer = 2, kTexturedTerrainFar = 3;
inline constexpr uint8_t kClayPixelShaders = 4;  // the values of PipelineKey::textured

struct PipelineKey {
  uint8_t textured = 0;      // 0 flat, 1 textured, 2 terrain layer, 3 terrain far (the pass's pixel shaders)
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
// (RenderState::alpha_to_mask_offsets), bits 24-25 the sample count they are
// applied under (RenderState::alpha_to_mask_samples). None of this is pipeline
// state: the pixel shader computes the thresholds.
inline constexpr uint32_t kAlphaTest = 1u << 3;        // discard unless alpha <func> alpha_ref
inline constexpr uint32_t kAlphaFromAlbedo = 1u << 4;  // alpha is an albedo component (else the literal 1)
inline constexpr uint32_t kAlphaBlend = 1u << 7;       // write the alpha (else 1)
inline constexpr uint32_t kAlphaToMask = 1u << 8;      // discard unless AlphaToMaskCovers
inline constexpr uint32_t kAlphaDivide = 1u << 9;      // the colour is the albedo's divided by its alpha
inline constexpr uint32_t kAlphaToMaskShift = 16;      // the four 2-bit offsets
inline constexpr uint32_t kAlphaToMaskSamplesShift = 24;  // the surface's sample count, 2 bits

// Alpha-to-mask, which the textured pixel shader does after the alpha test.
// The GPU covers a sample iff alpha >= base - offset * scale (the emulator's
// rule, SDK dxbc_translator_om.cpp CompletePixelShader_AlphaToMask), with the
// offset of the pixel's parity, index (x & 1) * 2 + (y & 1), two bits each from
// bit 0 of `offsets`, and per sample count of the surface (`samples`,
// RB_SURFACE_INFO bits 16-17):
//   0, no multisampling: one sample, base 1, scale 1/4;
//   1, two samples: bases 0.5 and 1, scale 1/8;
//   2, four samples: bases 0.75, 0.25, 0.5 and 1, scale 1/16.
// The clay target has one sample per pixel: the pixel (x, y) is kept iff at
// least half of the game's samples would be covered (ruling, 2026-10-07):
//   0: alpha >= 1 - offset / 4;
//   1: alpha >= 0.5 - offset / 8;
//   2: alpha >= 0.5 - offset / 16.
// Any other count is taken as 0. The alpha is not clamped and a NaN covers
// nothing. Under two samples, the main scene's count, 0xAA cuts at 0.25 at
// every pixel and the dither pattern 0x87 at 0.125, 0.375, 0.5, 0.25 for
// indices 0 to 3: a texel of alpha 0.5 or more is kept at every pixel, and a
// fading draw keeps none, one, two, three or all of each 2x2 block below that.
// A pixel of which the game covers one sample of two is drawn whole here: more
// solid than the game, never a hole the game does not have.
inline bool AlphaToMaskCovers(uint8_t offsets, uint32_t x, uint32_t y, float alpha, uint32_t samples = 0) {
  const uint32_t index = ((x & 1u) << 1) | (y & 1u);
  const uint32_t offset = (uint32_t(offsets) >> (2 * index)) & 3u;
  const float mask_base = (samples == 1u || samples == 2u) ? 0.5f : 1.0f;
  const float per_offset = samples == 1u ? 0.125f : samples == 2u ? 0.0625f : 0.25f;
  return alpha >= mask_base - float(offset) * per_offset;
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
// drawn opaque and not left out. A terrain record drawn with its material
// (`textured` 2 or 3) needs no recipe: the terrain pixel shaders form their own
// alpha (weight x fade, or 1), so its test and blend always apply, and the
// material's recipe, scale and colour division are not used. Rules
// (DecideAlpha): a blended draw whose colour is unknown, or whose blend the RHI
// cannot express, is left out; a test
// or blend without an alpha the pixel shader can form (no recipe, a flat draw,
// a recipe that needs another fetch) is drawn without the test and with alpha
// 1; an invalid state or an inexpressible depth function draws with the default
// state. A colour mask of 0 writes no colour, so its blend does not matter.
// A flat terrain draw whose depth state cannot be used tests less-equal with
// depth write instead of the default less (TerrainFlatDefaultKey): it has to
// cover the underlay of its square, which lies at the same depth.
inline DrawState DecideDrawState(const capture::RenderState& rs, const capture::Material& m,
                                 uint8_t textured, bool flat_terrain) {
  DrawState d;
  d.key = MakePipelineKey(rs, textured);
  d.state_invalid = !rs.valid || !DepthSupported(rs);
  if (flat_terrain && d.state_invalid) {
    d.key.depth_test = 1;
    d.key.depth_write = 1;
    d.key.depth_func = uint8_t(capture::Compare::kLessEqual);
  }
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
  const bool terrain = textured == kTexturedTerrainLayer || textured == kTexturedTerrainFar;
  const capture::AlphaSpec& a = m.alpha;
  const bool has_recipe = terrain || (colour_known && (a.source == capture::AlphaSource::kAlbedo ||
                                                       a.source == capture::AlphaSource::kOne));
  // The shader divides its colour by the alpha whatever the state is.
  if (has_recipe && !terrain && a.divide_colour) d.alpha.mode |= kAlphaDivide;
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
        d.alpha.mode |= kAlphaToMask | (uint32_t(rs.alpha_to_mask_offsets) << kAlphaToMaskShift) |
                        (uint32_t(rs.alpha_to_mask_samples & 3) << kAlphaToMaskSamplesShift);
      }
      if (!terrain && a.source == capture::AlphaSource::kAlbedo) {
        d.alpha.mode |= kAlphaFromAlbedo | (uint32_t(a.component & 3) << 5);
      }
      if (blend) d.alpha.mode |= kAlphaBlend;
      d.alpha.ref = rs.alpha_ref;
      if (!terrain) d.alpha.scale = m.alpha_scale;
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

// --- Linear target and output mapping (frame-map section 14, "Linear target") ---
// The pass draws linear colour into a float target, as the game does, and one
// full-screen step (shaders::kOutputMapPs) makes the display values of the 8-bit
// target the composite views sample: display = sqrt(saturate(linear * E)). The
// functions below are the HLSL's rules; test_native_shaders.cpp pins the
// statements they mirror.

// The output mapping for one channel. Only a value in [0, 1] comes out: a
// negative value, a NaN (no comparison holds for it) and 0 * infinity give 0,
// +infinity gives 1.
inline float OutputMap(float linear, float exposure) {
  const float v = linear * exposure;
  if (!(v > 0.0f)) return 0.0f;
  return std::sqrt(v < 1.0f ? v : 1.0f);
}

// What a clay pixel shader writes for the display value `display` (albedo or
// clay colour times the facet shade: the value the 8-bit target held, which
// clamped it to [0, 1]): its square, so that the mapping with E = 1 gives the
// value back. The shade therefore multiplies before the square. A NaN gives 0.
inline float ClayLinear(float display) {
  if (!(display > 0.0f)) return 0.0f;
  const float d = display < 1.0f ? display : 1.0f;
  return d * d;
}

// The terrain shaders have the linear value itself (the game's A = tf0^2,
// F = tf13^2, or the steep-ground mix) and the shade of the display value:
// (sqrt(v) * shade)^2 = v * shade^2, clamped as ClayLinear clamps.
inline float ClayLinearShaded(float linear, float shade) {
  const float v = linear * shade * shade;
  if (!(v > 0.0f)) return 0.0f;
  return v < 1.0f ? v : 1.0f;
}

// The pass's clear colour: the 8-bit levels the old target showed where nothing
// is drawn (it was cleared to 0.08, 0.08, 0.1; 20, 20, 25 in the shots), and
// for the linear target the values nearest those levels' squares that its
// channels hold exactly (101 steps of 2^-14 in the 11-bit floats, 39 of 2^-12
// in the 10-bit one), so that the clear does not depend on a GPU's rounding;
// the mapping returns the three levels. The fourth is the alpha of the clear
// call; the target has none.
inline constexpr int kClayClearLevels[3] = {20, 20, 25};
inline constexpr float kClayClearLinear[4] = {101.0f / 16384.0f, 101.0f / 16384.0f, 39.0f / 4096.0f, 1.0f};

// fable2_native_light. Off: every light term is 1 and the exposure is 1, which
// is the materials picture. On: nothing more yet (the lit shaders follow).
enum class ClayLight : uint8_t { kOff, kOn };

inline ClayLight ParseClayLight(std::string_view text, bool* recognized) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  std::string lower(text);
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  if (recognized) *recognized = lower == "off" || lower == "on";
  return lower == "on" ? ClayLight::kOn : ClayLight::kOff;
}

// The output mapping's exposure E: one number for the frame, no draw's own.
// 1 with light off; with light on it stays 1 until the lit shaders exist and E
// is calibrated against the emulated frame (spec, design 4).
inline float ClayExposure(ClayLight light) {
  switch (light) {
    case ClayLight::kOff: return 1.0f;
    case ClayLight::kOn: return 1.0f;
  }
  return 1.0f;
}

// Bytes per texel of the pass's three targets: the linear colour
// (R11G11B10_FLOAT: 11-bit floats for red and green, a 10-bit float for blue,
// no sign and no alpha), the mapped colour (R8G8B8A8_UNORM) and the depth
// (D32_FLOAT). The 32-bit float format was taken for the frame rate (ruling,
// 2026-10-08): R16G16B16A16_FLOAT, 8 bytes, returns every 8-bit level exactly
// but cost 2 to 3 ms a frame more in one set of game runs (later runs did not
// separate the two formats); this one is up to one level off
// in red and green and up to three in blue (test_clay_logic.cpp, cases 397 to
// 400 and 416; frame-map section 14, "Linear target"). The linear format is one
// for every pipeline of the pass, so PipelineKey has no field for it.
inline constexpr uint32_t kClayLinearTexelBytes = 4, kClayMappedTexelBytes = 4, kClayDepthTexelBytes = 4;

inline uint64_t ClayTargetBytes(uint32_t width, uint32_t height, uint32_t texel_bytes) {
  return uint64_t(width) * height * texel_bytes;
}

// The log line of the pass's targets, written when they are created.
inline std::string FormatTargetText(uint32_t width, uint32_t height) {
  const double mb = 1024.0 * 1024.0;
  char buf[192];
  std::snprintf(buf, sizeof(buf),
                "targets %u x %u: linear R11G11B10_FLOAT %.1f MB, mapped R8G8B8A8_UNORM %.1f MB, "
                "depth D32_FLOAT %.1f MB",
                width, height, double(ClayTargetBytes(width, height, kClayLinearTexelBytes)) / mb,
                double(ClayTargetBytes(width, height, kClayMappedTexelBytes)) / mb,
                double(ClayTargetBytes(width, height, kClayDepthTexelBytes)) / mb);
  return buf;
}

// --- Terrain material (frame-map section 13, "Terrain", "Native terrain material") ---
// A terrain record whose inputs and textures are usable is drawn by one of the
// two terrain pixel shaders (fable2_native_shaders.h) with its captured render
// state: a layer of the layer builder (8221C9C8) with kClayTerrainLayerPs, a
// draw of the adaptive builder (82207C30) with kClayTerrainFarPs.

// The terrain shaders' constants (cbuffer Terrain, register b1), laid out as
// shaders::kClayTerrainFields says. They do not fit the root signature (64
// words: 36 are the draw's constants, 6 the three buffers, 2 the two texture
// tables, and the RHI has one constant block per layout), so every terrain
// draw of a frame has a slot of kClayTerrainSlotBytes in one constant buffer.
struct ClayTerrainConstants {
  float detail_uv[4];   // layer: tf0 coordinate of the world position, scaled into the host texture
  float weight_uv[4];   // layer: tf18 coordinate of the world position
  float world[12];      // TerrainMaterialInputs::world
  float eye[3];
  float hole_ref;
  float fade[2];
  float weight_scale, weight_bias;
  uint32_t colour_sampler;  // static sampler of the colour texture (SamplerIndex)
  uint32_t weight_sampler;  // of the weights; the shader reads its clamp bit and filters linearly
  uint32_t flags;           // kTerrainFadeIn, kTerrainSteep
  // The steep-ground block (kTerrainSteep; zero without it).
  uint32_t side_sampler;    // static sampler of the side texture
  float side_uv[2];         // TerrainMaterialInputs::side_uv, scaled into the host texture
  float slope[2];           // TerrainMaterialInputs::slope
  float steep_threshold, steep_eps;
  uint32_t pad[2];
};
static_assert(sizeof(ClayTerrainConstants) == 160);
inline constexpr uint32_t kClayTerrainSlotBytes = 256;  // the RHI's constant buffer offset alignment
// ClayTerrainConstants::flags: the far colour is multiplied by sat(d2 * fade[0] + fade[1]) (the
// kind terrain-far-fade, shader 2F8A: the underlay the layers add to).
inline constexpr uint32_t kTerrainFadeIn = 1u;
// The layer's pixel shader has the steep-ground block and the draw has what it reads: the side
// texture at t6 and the vertex normals in the draw's buffer at t2 (TerrainSteepStatus).
inline constexpr uint32_t kTerrainSteep = 2u;

// The pipeline of a flat terrain draw whose state could not be read: the default
// state with depth less-equal. The pass keeps it as a default pipeline, so that
// a flat terrain draw that finds no pipeline of its own in a full cache still
// passes the depth test over its underlay.
inline PipelineKey TerrainFlatDefaultKey() {
  PipelineKey k;
  k.depth_func = uint8_t(capture::Compare::kLessEqual);
  return k;
}

// Whether a terrain record's captured state can be drawn as captured: it was
// read, its depth function and its blend are ones the RHI has. If not, the
// record is not drawn with its material (reason terrain-inputs): the pass's
// default state is opaque with depth less, under which a layer over its
// underlay fails the depth test at equal depth and a first layer writes its
// colour unweighted, and a layer left out of its square's sum darkens it. The
// square then falls back as a whole (PlanTerrain).
inline bool TerrainStateUsable(const capture::RenderState& rs) {
  return rs.valid && DepthSupported(rs) && !(rs.blend && rs.color_mask != 0 && !BlendSupported(rs));
}

// PipelineKey::textured of a terrain kind.
inline uint8_t TerrainTextured(capture::MaterialKind kind) {
  return kind == capture::MaterialKind::kTerrainLayer ? kTexturedTerrainLayer : kTexturedTerrainFar;
}

// `colour_fix`: the colour texture's (width / host width, height / host height). A far draw's
// colour coordinate is per vertex (TerrainFarUvKey) and is scaled by the draw's uv transform.
// `side_fix` and `side_sampler`: the same of the side texture of a layer with the steep-ground
// block, when the pass has it (nullptr: the block stays off).
inline ClayTerrainConstants MakeClayTerrainConstants(const capture::TerrainMaterialInputs& m,
                                                     capture::MaterialKind kind, const float colour_fix[2],
                                                     uint32_t colour_sampler, uint32_t weight_sampler,
                                                     const float* side_fix = nullptr, uint32_t side_sampler = 0) {
  ClayTerrainConstants c{};
  for (int i = 0; i < 4; ++i) {
    c.detail_uv[i] = m.detail_uv[i] * colour_fix[i & 1];
    c.weight_uv[i] = m.weight_uv[i];
  }
  std::copy(std::begin(m.world), std::end(m.world), std::begin(c.world));
  std::copy(std::begin(m.eye), std::end(m.eye), std::begin(c.eye));
  c.hole_ref = m.hole_ref;
  c.fade[0] = m.fade[0];
  c.fade[1] = m.fade[1];
  c.weight_scale = m.weight_scale;
  c.weight_bias = m.weight_bias;
  c.colour_sampler = colour_sampler;
  c.weight_sampler = weight_sampler;
  c.flags = kind == capture::MaterialKind::kTerrainFarFade ? kTerrainFadeIn : 0u;
  if (m.steep && side_fix && kind == capture::MaterialKind::kTerrainLayer) {
    c.flags |= kTerrainSteep;
    c.side_sampler = side_sampler;
    c.side_uv[0] = m.side_uv[0] * side_fix[0];
    c.side_uv[1] = m.side_uv[1] * side_fix[1];
    c.slope[0] = m.slope[0];
    c.slope[1] = m.slope[1];
    c.steep_threshold = m.steep_threshold;
    c.steep_eps = m.steep_eps;
  }
  return c;
}

// kTextured when a terrain record can be drawn with its material this frame,
// else the reason it is flat clay. `kind` is the record's pixel shader's
// (Material::kind), `adaptive_builder` whether the adaptive builder drew it;
// `colour` and `weight` are the texture cache's statuses of the colour texture
// and (layers) the weight cells, `far_uvs` whether the far colour coordinates
// were built (far draws). Inputs that are not valid, or a pixel shader that is
// not a terrain shader of the record's builder: terrain-inputs, and no texture
// was asked for. A texture that is not usable: the cache's reason, the colour
// texture's first. Far coordinates that could not be built (a table unreadable
// or of a layout that is not handled): uv-unsupported.
inline capture::MaterialStatus TerrainMaterialStatus(const capture::TerrainMaterialInputs& m,
                                                     capture::MaterialKind kind, bool adaptive_builder,
                                                     capture::MaterialStatus colour, capture::MaterialStatus weight,
                                                     bool far_uvs) {
  using capture::MaterialKind;
  using capture::MaterialStatus;
  const bool far_kind = kind == MaterialKind::kTerrainFar || kind == MaterialKind::kTerrainFarFade;
  const bool layer_kind = kind == MaterialKind::kTerrainLayer;
  if (!m.valid || m.adaptive != adaptive_builder || (adaptive_builder ? !far_kind : !layer_kind)) {
    return MaterialStatus::kTerrainInputs;
  }
  if (colour != MaterialStatus::kTextured) return colour;
  if (adaptive_builder) return far_uvs ? MaterialStatus::kTextured : MaterialStatus::kUvUnsupported;
  return weight;
}

// The steep-ground block's part of a layer record's status, asked when
// TerrainMaterialStatus is kTextured: kTextured for a record without the block
// (`side` and `normals` are not looked at); for one with it, the texture
// cache's reason of the side texture when that is not usable, uv-unsupported
// when the vertex normals could not be built (the normal map unreadable or of a
// layout that is not handled), else kTextured. A record that is not kTextured
// here cannot have its material, so its square falls back as a whole
// (PlanTerrain): the block is not left out of a layer that has it.
inline capture::MaterialStatus TerrainSteepStatus(const capture::TerrainMaterialInputs& m,
                                                  capture::MaterialStatus side, bool normals) {
  using capture::MaterialStatus;
  if (!m.steep) return MaterialStatus::kTextured;
  if (side != MaterialStatus::kTextured) return side;
  return normals ? MaterialStatus::kTextured : MaterialStatus::kUvUnsupported;
}

// A patch's rectangle of ground: corner and size in world units.
struct TerrainRect {
  float x0 = 0, y0 = 0, w = 0, h = 0;
  bool operator==(const TerrainRect&) const = default;
};

// Patch `k` of a terrain record, as the vertex shaders place it: row = floor(p / cols),
// col = p - row * cols, corner = origin + (col, row) * cell (the first grid point of
// BuildTerrainPositions, computed the same way, so equal patches give equal rectangles).
inline TerrainRect TerrainPatchRect(const capture::TerrainPatch& t, uint32_t k) {
  const float p = t.patch + float(k);
  const float row = std::floor(p * t.inv_cols);
  const float col = p - row * t.cols;
  return {col * t.cell[0] + t.origin[0], row * t.cell[1] + t.origin[1], t.cell[0], t.cell[1]};
}

enum class TerrainAction : uint8_t {
  kMaterial,  // drawn by its terrain pixel shader with its captured state
  kFlat,      // drawn as flat clay, opaque
  kSkip       // not drawn
};

// One terrain record of a frame for PlanTerrain, in draw order.
struct TerrainPlanItem {
  const capture::TerrainPatch* patch = nullptr;  // the record's terrain inputs (TerrainOf)
  bool layer = false;     // a draw of the layer builder; else of the adaptive builder
  bool usable = false;    // its material can be drawn this frame (TerrainMaterialStatus is kTextured)
  bool drawable = true;   // its positions and indices were built
  TerrainAction action = TerrainAction::kFlat;  // out
};

// What the pass does with each terrain record of a frame. The ground near the
// eye is the sum of the layer draws of a square (one patch rectangle of the
// layer builder: a tile's origin, its sub-patch and the cell size, compared as
// TerrainPatchRect) over an underlay (a draw of the adaptive builder that
// covers the square; found by the square's centre), each layer blended by its
// captured state. A sum with a part missing or replaced by flat clay is wrong
// (too dark, or additive layers over clay), so a square is all or nothing:
//  - every draw usable and drawable: each with its material;
//  - a layer of a square not usable (or without geometry): the square is "bad".
//    Its first drawable layer that is not yet under a flat draw is drawn as flat
//    clay (opaque: it covers the underlay), every other layer of the square is
//    left out;
//  - an underlay that is not usable is itself drawn flat (opaque, all its
//    patches), and every layer of the squares it covers is left out; one without
//    geometry draws nothing and its squares are bad as above;
//  - a layer draw of several patches ties its squares together: when one is bad
//    all are (a square can then lie under two opaque flat draws, never under a
//    material layer over flat clay).
// A draw of the adaptive builder never depends on another draw. A record with
// more than capture::kMaxTerrainPatches patches has no grid (no positions are
// built for it) and is not walked: as a layer it has no squares; as an underlay
// it cannot say which squares it lies under and draws nothing, so every square
// of the frame is bad (the treatment of an underlay without geometry).
//
// The work is bounded (records come from guest memory: 16 layer records of 4096
// patches over an underlay without material took 218 ms). Linking one patch of
// a layer record to its square counts kTerrainPlanLinkCost steps (a table
// lookup), testing one square against one patch of an underlay without its
// material one step. A frame that would take more than kTerrainPlanBudget steps
// is not planned by squares: every square is bad without the walk, which here
// means every layer record that has geometry is flat clay (several opaque flat
// draws of one square show the same clay) and every far draw is what it is by
// itself. A real frame is far below the budget: the bridge (session-1 capture)
// has 492 layer records of one patch each (31,488 steps) and 75 far records of
// 1,606 patches under about 100 squares (about 160,000 steps more if none of
// them had its material).
// `scratch`: storage a caller keeps between frames (nullptr: local).
inline constexpr uint64_t kTerrainPlanBudget = 1u << 20;
inline constexpr uint64_t kTerrainPlanLinkCost = 64;

struct TerrainPlanScratch {
  struct Square {
    TerrainRect rect;
    bool bad = false, covered = false;
  };
  struct RectHash {
    size_t operator()(const TerrainRect& r) const {
      return HashCombine32({FloatBits(r.x0), FloatBits(r.y0), FloatBits(r.w), FloatBits(r.h)});
    }
  };
  std::vector<Square> squares;
  std::unordered_map<TerrainRect, uint32_t, RectHash> index;
  // link[first[i] .. first[i + 1]): the squares of layer item i, one per patch.
  std::vector<uint32_t> link, first;
};

inline void PlanTerrain(std::vector<TerrainPlanItem>& items, TerrainPlanScratch* scratch = nullptr) {
  using Square = TerrainPlanScratch::Square;
  TerrainPlanScratch local;
  TerrainPlanScratch& w = scratch ? *scratch : local;
  std::vector<Square>& squares = w.squares;
  auto& index = w.index;
  std::vector<uint32_t>& link = w.link;
  std::vector<uint32_t>& first = w.first;
  squares.clear();
  index.clear();
  link.clear();
  first.assign(items.size() + 1, 0);
  auto has_grid = [](const TerrainPlanItem& it) {
    return it.patch && it.patch->patches <= capture::kMaxTerrainPatches;
  };
  auto own_action = [](const TerrainPlanItem& it) {
    return !it.drawable ? TerrainAction::kSkip : it.usable ? TerrainAction::kMaterial : TerrainAction::kFlat;
  };
  // Over the budget: every square is bad without the walk.
  auto unplanned = [&] {
    for (TerrainPlanItem& it : items) {
      it.action = !it.layer ? own_action(it) : it.drawable ? TerrainAction::kFlat : TerrainAction::kSkip;
    }
  };
  uint64_t steps = 0;
  for (const TerrainPlanItem& it : items) {
    if (it.layer && has_grid(it)) steps += kTerrainPlanLinkCost * it.patch->patches;
  }
  if (steps > kTerrainPlanBudget) return unplanned();
  bool every_bad = false;
  for (size_t i = 0; i < items.size(); ++i) {
    first[i] = uint32_t(link.size());
    const TerrainPlanItem& it = items[i];
    if (!it.layer || !has_grid(it)) continue;
    for (uint32_t k = 0; k < it.patch->patches; ++k) {
      const TerrainRect rect = TerrainPatchRect(*it.patch, k);
      const auto found = index.try_emplace(rect, uint32_t(squares.size()));
      if (found.second) squares.push_back({rect});
      link.push_back(found.first->second);
    }
  }
  first[items.size()] = uint32_t(link.size());

  for (size_t i = 0; i < items.size(); ++i) {
    TerrainPlanItem& it = items[i];
    const bool whole = it.usable && it.drawable;
    if (it.layer) {
      if (!whole) {
        for (uint32_t l = first[i]; l < first[i + 1]; ++l) squares[link[l]].bad = true;
      }
      continue;
    }
    it.action = own_action(it);
    if (it.patch && !has_grid(it)) {
      every_bad = true;  // over the patch bound: no grid to find its squares by
      continue;
    }
    if (whole || !it.patch) continue;
    // The squares over this draw have no underlay to add to: bad, and under its flat draw if it has one.
    steps += uint64_t(it.patch->patches) * squares.size();
    if (steps > kTerrainPlanBudget) return unplanned();
    for (uint32_t k = 0; k < it.patch->patches; ++k) {
      const TerrainRect f = TerrainPatchRect(*it.patch, k);
      for (Square& s : squares) {
        const float cx = s.rect.x0 + 0.5f * s.rect.w, cy = s.rect.y0 + 0.5f * s.rect.h;
        if (cx >= f.x0 && cx < f.x0 + f.w && cy >= f.y0 && cy < f.y0 + f.h) {
          s.bad = true;
          s.covered = s.covered || it.drawable;
        }
      }
    }
  }
  if (every_bad) {
    for (Square& s : squares) s.bad = true;
  }
  // A layer draw with a bad square makes all its squares bad.
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < items.size(); ++i) {
      bool bad = false;
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) bad = bad || squares[link[l]].bad;
      if (!bad) continue;
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) {
        changed = changed || !squares[link[l]].bad;
        squares[link[l]].bad = true;
      }
    }
  }
  for (size_t i = 0; i < items.size(); ++i) {
    TerrainPlanItem& it = items[i];
    if (!it.layer) continue;
    bool bad = false, open = false;
    for (uint32_t l = first[i]; l < first[i + 1]; ++l) {
      bad = bad || squares[link[l]].bad;
      open = open || !squares[link[l]].covered;
    }
    if (!bad) {
      // (a layer without a patch has no square: it is drawn on its own)
      it.action = own_action(it);
    } else if (it.drawable && open) {
      it.action = TerrainAction::kFlat;
      for (uint32_t l = first[i]; l < first[i + 1]; ++l) squares[link[l]].covered = true;
    } else {
      it.action = TerrainAction::kSkip;
    }
  }
}

// The F3 texture line: textured share of the drawn records (terrain records
// drawn with their material among them) and of the drawn non-terrain records
// (spec success criterion 2), cache counters and the top
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
  const uint32_t non_terrain = drawn > ts.terrain ? drawn - ts.terrain : 0;
  const uint32_t textured_nt = ts.textured > ts.terrain_textured ? ts.textured - ts.terrain_textured : 0;
  const uint32_t share_nt = non_terrain ? uint32_t(uint64_t(textured_nt) * 100 / non_terrain) : 0;
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

// The log's terrain line: the drawn terrain records, those drawn with their
// material and as flat clay, the layer records left out with their square, and
// the distinct texture sets bound (colour, weights, side texture: each takes
// three slots of the RHI's shader-visible view heap until one of its views is
// destroyed).
// Then, when there are any, the records that could not have their material by
// their own reason, most first (ClayStats::terrain_unusable, and no-positions
// and no-indices for records whose geometry could not be built): the cause of a
// square's fallback, which the other lines show only as terrain-square when the
// record is not the square's flat draw.
inline std::string FormatTerrainText(const ClayStats& st, const TextureStats& ts) {
  const uint32_t flat = ts.terrain > ts.terrain_textured ? ts.terrain - ts.terrain_textured : 0;
  std::string text = "Terrain: material " + std::to_string(ts.terrain_textured) + " of " +
                     std::to_string(ts.terrain) + " drawn, flat " + std::to_string(flat) + ", left out " +
                     std::to_string(st.terrain_skipped) + ", texture sets " + std::to_string(st.terrain_tuples);
  std::vector<std::pair<const char*, uint32_t>> reasons;
  for (size_t i = 0; i < size_t(capture::MaterialStatus::kCount); ++i) {
    if (st.terrain_unusable[i]) {
      reasons.emplace_back(capture::MaterialStatusName(capture::MaterialStatus(i)), st.terrain_unusable[i]);
    }
  }
  if (st.terrain_no_positions) reasons.emplace_back("no-positions", st.terrain_no_positions);
  if (st.terrain_no_indices) reasons.emplace_back("no-indices", st.terrain_no_indices);
  std::stable_sort(reasons.begin(), reasons.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  for (size_t i = 0; i < reasons.size(); ++i) {
    text += std::string(i ? ", " : " | without material: ") + reasons[i].first + " " + std::to_string(reasons[i].second);
  }
  return text;
}

// The log's light line: the prepared records with light inputs and those whose
// normals (and colours) the geometry cache built, the world rotations of the
// objects and trunks among them by kind, and, when there are any, the rotations
// that are not one by vertex shader and the records without their buffers by
// reason and vertex shader (top eight each).
inline std::string FormatLightText(const ClayStats& st) {
  constexpr size_t kTop = 8;
  std::string text = "Light inputs: ready " + std::to_string(st.light_ready) + " of " + std::to_string(st.lit) +
                     " lit records | world rotation:";
  for (size_t i = 0; i < size_t(RotationKind::kCount); ++i) {
    text += std::string(i ? ", " : " ") + RotationKindName(RotationKind(i)) + " " + std::to_string(st.light_rotation[i]);
  }
  auto by_vs = [&](const capture::ShaderTally<32>& tally, uint8_t kind) {
    capture::ShaderTally<32>::Entry top[kTop];
    const size_t n = tally.Top(kind, top, kTop);
    std::string list;
    char buf[48];
    for (size_t i = 0; i < n; ++i) {
      std::snprintf(buf, sizeof(buf), "%s0x%016llX: %u", i ? ", " : "", static_cast<unsigned long long>(top[i].hash),
                    top[i].count);
      list += buf;
    }
    return list;
  };
  for (size_t i = 1; i < size_t(RotationKind::kCount); ++i) {
    if (!st.light_rotation[i]) continue;
    text += std::string(" | ") + RotationKindName(RotationKind(i)) + " by vs {" + by_vs(st.light_rotation_by_vs, uint8_t(i)) + "}";
  }
  for (size_t i = 1; i < size_t(capture::LightReason::kCount); ++i) {
    if (!st.light_missing[i]) continue;
    text += std::string(" | ") + capture::LightReasonName(capture::LightReason(i)) + " " +
            std::to_string(st.light_missing[i]) + " by vs {" + by_vs(st.light_missing_by_vs, uint8_t(i)) + "}";
  }
  return text;
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
// the pass left out as blend-unknown, and terrain layers left out with their
// square as terrain-square, among them), the geometry cache counters
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
  if (st.terrain_skipped) reasons.emplace_back("terrain-square", st.terrain_skipped);
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
