#pragma once

// Pure helpers for the clay pass and its geometry cache (no SDK/GPU deps):
// cache keys, vertex counts, index range checks, draw colors, the root
// constant block, texture counters and the F3 status text.

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
  // Records whose positions came from the instanced (flat stream) and the
  // bone-skinned builders this frame, cache hits included.
  uint32_t instanced = 0, skinned = 0;
};

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
                           uint32_t(l.is_signed), uint32_t(l.exp_adjust)});
  k.kind = 4;
  return k;
}

// Cache key of a decoded UV stream: MeshUvKey; an instanced draw's flat UVs
// (kind 6) add its instance set, like its flat positions.
inline GeoKey UvKey(const capture::DrawRecord& r) {
  GeoKey k = MeshUvKey(r);
  if (r.instances.active) {
    k.extra = HashCombine32({k.extra, InstanceHash(r.instances)});
    k.kind = 6;
  }
  return k;
}

// UVs in a stream of `vb_size` bytes: every vertex whose UV element lies
// fully inside the stream.
inline uint32_t UvCount(uint32_t vb_size, const capture::UvLayout& l) {
  const uint32_t n = capture::UvComponents(l.format);
  if (n == 0 || l.stride_bytes == 0) return 0;
  const uint64_t need = uint64_t(l.offset_bytes) + n * (capture::UvSixteen(l.format) ? 2u : 4u);
  if (need > vb_size) return 0;
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

// Root constants, laid out as the clay HLSL cbuffer (28 dwords).
struct ClayConstants {
  float rows[16];
  uint32_t layout;
  int32_t base_vertex;
  uint32_t vertex_count;
  uint32_t color;
  float uv[4];        // u * uv[0] + uv[2], v * uv[1] + uv[3]
  uint32_t textured;  // 1: sample the albedo at t3
  uint32_t sampler;   // static sampler s0-s3 (SamplerIndex)
  uint32_t pad[2];
};
static_assert(sizeof(ClayConstants) == 112);

inline ClayConstants MakeClayConstants(const capture::DrawRecord& r, uint32_t vertex_count,
                                       uint32_t color, const float uv[4], bool textured,
                                       uint32_t sampler) {
  ClayConstants c{};
  std::copy(std::begin(r.rows), std::end(r.rows), std::begin(c.rows));
  c.layout = r.layout == capture::TransformLayout::kCombine ? 1u : 0u;
  c.base_vertex = r.base_vertex;
  c.vertex_count = vertex_count;
  c.color = color;
  std::copy(uv, uv + 4, std::begin(c.uv));
  c.textured = textured ? 1u : 0u;
  c.sampler = sampler;
  return c;
}

// The F3 texture line: textured share of the drawn records and of the drawn
// non-terrain records (spec success criterion 2), cache counters and the top
// three final statuses other than textured. "Textures: off (latched)" once an
// RHI failure turned the texture path off.
inline std::string FormatTextureText(const TextureStats& ts, uint32_t drawn) {
  if (ts.latched) return "Textures: off (latched)";
  std::vector<std::pair<const char*, uint32_t>> reasons;
  for (size_t i = 0; i < size_t(capture::MaterialStatus::kCount); ++i) {
    if (i == size_t(capture::MaterialStatus::kTextured) || ts.status[i] == 0) continue;
    reasons.emplace_back(capture::MaterialStatusName(capture::MaterialStatus(i)), ts.status[i]);
  }
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

// Four F3 lines: capture/draw counts with the top three skip reasons, the
// geometry cache counters and GPU-thread CPU timings, the guest-thread
// capture time of the scene's frame, then the texture line (tex nullptr: the
// texture path is off).
inline std::string FormatStatusText(const FrameScene& scene, const ClayStats& st,
                                    const TextureStats* tex) {
  std::vector<std::pair<std::string, uint64_t>> reasons;
  for (size_t i = 0; i < size_t(capture::SkipReason::kCount); ++i) {
    uint64_t n = scene.skipped[i];
    if (i == size_t(capture::SkipReason::kBadIndex)) n += st.skipped_bad_index;
    if (n) reasons.emplace_back(capture::SkipReasonName(capture::SkipReason(i)), n);
  }
  if (st.skipped_other) reasons.emplace_back("render-other", st.skipped_other);
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
                "hash %.2f ms, decode %.2f ms, record %.2f ms\nCapture: %.2f ms guest time per frame",
                st.uploads, st.hits, double(st.resident_bytes) / (1024.0 * 1024.0), st.instanced,
                st.skinned, st.hash_ms, st.decode_ms, st.record_ms, scene.capture_ms);
  return text + geo + "\n" + (tex ? FormatTextureText(*tex, st.drawn) : std::string("Textures: off"));
}

}  // namespace fable2::native::render
