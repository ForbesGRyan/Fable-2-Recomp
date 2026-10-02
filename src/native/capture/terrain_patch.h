#pragma once

// Heightmap terrain patches (pure: no SDK/GPU deps). Fable 2 draws its ground
// as tessellated quad patches (DrawIndx:8221C9C8 and DrawIndx:82207C30,
// frame-map sections 8 and 9). Their vertex shaders fetch no vertices: each
// domain point (u, v) of patch p is placed on a grid and lifted by a height
// read from a 2D texture. These helpers rebuild that surface on a regular grid
// for the clay pass.
//
// Shader math (C30A97D946FA2BE4 / FB68A7F2301210E1 instr 8-22, 5003700B7C9B1C16
// instr 5-20), with the registers named per shader in vs-transforms.json:
//   row = floor(p * inv_cols); col = p - row * cols
//   world.xy = (col + u, row + v) * cell + origin
//   height = tex((world.xy - tex_offset) * tex_scale).x * height_scale
//   position = (world.x, world.y, height, 1), transformed by c0..c3 (dot).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "position_decode.h"

namespace fable2::native::capture {

// Mip 0 of a Xenos 2D texture holding 16-bit unsigned normalized heights.
struct HeightMap {
  uint32_t phys_addr = 0;  // GPU physical
  uint32_t size = 0;       // bytes the texels span from phys_addr
  uint32_t width = 0, height = 0;
  uint32_t pitch = 0;   // texels per row (fetch constant pitch field << 5)
  uint32_t endian = 0;  // 0 none, 1 8in16 (the only endians handled)
  bool tiled = false;
  uint8_t clamp_x = 0, clamp_y = 0;  // xenos::ClampMode (0 repeat, 1 mirror, else clamp)
};

// xenia's texture_util::GetTiledOffset2D (UModel's Xbox 360 untiling).
inline int32_t TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2) {
  pitch = (pitch + 31) & ~31u;
  const int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bytes_per_block_log2 + 7);
  const int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  const int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

// Decodes a texture fetch constant (xenos::xe_gpu_texture_fetch_t, host-order
// dwords). Only what the terrain uses is accepted: a 2D texture (type 2,
// dimension 1) in k_16 (24), unsigned x, exp adjust 0, x read from channel 0,
// point magnification filter, endian none or 8in16. Anything else returns
// false (the draw stays skipped).
inline bool DecodeHeightMap(const uint32_t fc[6], HeightMap* out) {
  if ((fc[0] & 3) != 2) return false;                // not a texture fetch constant
  if (((fc[0] >> 2) & 3) != 0) return false;         // x signed
  if ((fc[1] & 0x3F) != 24) return false;            // k_16
  if (((fc[5] >> 9) & 3) != 1) return false;         // not 2D
  if (((fc[3] >> 1) & 7) != 0) return false;         // swizzle x != channel 0
  if (((fc[3] >> 13) & 0x3F) != 0) return false;     // exp adjust
  if (((fc[3] >> 19) & 3) != 0) return false;        // mag filter not point
  HeightMap m;
  m.endian = (fc[1] >> 6) & 3;
  if (m.endian > 1) return false;
  m.clamp_x = uint8_t((fc[0] >> 10) & 7);
  m.clamp_y = uint8_t((fc[0] >> 13) & 7);
  m.pitch = ((fc[0] >> 22) & 0x1FF) << 5;
  m.tiled = (fc[0] >> 31) != 0;
  m.phys_addr = fc[1] & 0xFFFFF000u;
  m.width = (fc[2] & 0x1FFF) + 1;
  m.height = ((fc[2] >> 13) & 0x1FFF) + 1;
  if (m.pitch < m.width) return false;
  if (m.tiled) {
    // xenia's GetTiledAddressUpperBound2D: the last 32x32 tile's origin plus
    // 0xC00 for 2-byte texels.
    m.size = uint32_t(TiledOffset2D(int32_t((m.width - 1) & ~31u), int32_t((m.height - 1) & ~31u),
                                    m.pitch, 1)) + 0xC00;
  } else {
    // Linear rows are padded to 256 bytes.
    const uint32_t row = (m.pitch * 2 + 255) & ~255u;
    m.size = row * (m.height - 1) + m.width * 2;
  }
  *out = m;
  return true;
}

// Texel (x, y) as a [0, 1] height, or false if it lies outside `size`.
inline bool HeightTexel(const uint8_t* data, size_t size, const HeightMap& m, uint32_t x, uint32_t y,
                        float* out) {
  uint64_t o;
  if (m.tiled) {
    o = uint64_t(uint32_t(TiledOffset2D(int32_t(x), int32_t(y), m.pitch, 1)));
  } else {
    o = uint64_t(y) * ((m.pitch * 2 + 255) & ~255u) + uint64_t(x) * 2;
  }
  if (!data || o + 2 > size) return false;
  // 8in16 swaps the bytes of each 16-bit word: the GPU reads memory order.
  const uint32_t v = m.endian == 1 ? (uint32_t(data[o]) << 8) | data[o + 1]
                                   : (uint32_t(data[o + 1]) << 8) | data[o];
  *out = float(v) / 65535.0f;
  return true;
}

namespace detail {
// Point-filter texel index for normalized coordinate t on an axis of n texels.
inline uint32_t TexelIndex(float t, uint32_t n, uint8_t clamp) {
  const double f = std::floor(double(t) * n);
  if (!(f == f)) return 0;  // NaN
  if (clamp == 0 || clamp == 1) {
    const double period = clamp == 0 ? double(n) : 2.0 * n;
    double r = std::fmod(f, period);
    if (r < 0) r += period;
    if (clamp == 1 && r >= n) r = period - 1 - r;  // mirrored repeat
    return uint32_t(r);
  }
  return uint32_t(std::fmin(std::fmax(f, 0.0), double(n - 1)));
}
}  // namespace detail

// Point sample at normalized (u, v): texel floor(u * width), floor(v * height)
// with the map's clamp modes.
inline bool SampleHeight(const uint8_t* data, size_t size, const HeightMap& m, float u, float v,
                         float* out) {
  if (m.width == 0 || m.height == 0) return false;
  return HeightTexel(data, size, m, detail::TexelIndex(u, m.width, m.clamp_x),
                     detail::TexelIndex(v, m.height, m.clamp_y), out);
}

struct TerrainPatch {
  bool active = false;
  float patch = 0;       // first patch index as the shader sees it
  uint32_t patches = 1;  // consecutive patches in the draw
  float cols = 0, inv_cols = 0;
  float cell[2] = {};
  float height_scale = 0;
  float origin[2] = {};
  float tex_offset[2] = {};
  float tex_scale[2] = {};
  HeightMap map;
};

// vs-transforms.json "terrain" (FABLE2_VS_TERRAIN): where a shader keeps each
// parameter, as constant register * 4 + component. Pairs (cell, origin,
// tex_offset, tex_scale and grid = cols, 1 / cols) use two consecutive
// components of one register.
struct TerrainSpec {
  uint16_t grid;
  uint16_t cell;
  uint16_t height_scale;
  uint16_t origin;
  uint16_t tex_offset;
  uint16_t tex_scale;
  int16_t patch_offset;  // added to the patch index; -1 = none
  uint8_t height_fetch;  // texture fetch constant of the heightmap
};

// The draw's patch(es) from the vertex constants (`bank`, 256 x float4, host
// order) and the heightmap's texture fetch constant. False (and inactive) if a
// register is out of range or the heightmap is not handled.
inline bool MakeTerrainPatch(const TerrainSpec& s, const float* bank, const uint32_t fc[6],
                             uint32_t first_patch, uint32_t patches, TerrainPatch* out) {
  *out = TerrainPatch{};
  auto pair_ok = [](uint32_t c) { return c < 1024 && (c & 3) != 3; };
  if (!bank || patches == 0 || !pair_ok(s.grid) || !pair_ok(s.cell) || s.height_scale >= 1024 ||
      !pair_ok(s.origin) || !pair_ok(s.tex_offset) || !pair_ok(s.tex_scale) ||
      s.patch_offset >= 1024) {
    return false;
  }
  TerrainPatch t;
  if (!DecodeHeightMap(fc, &t.map)) return false;
  t.patch = float(first_patch) + (s.patch_offset >= 0 ? bank[s.patch_offset] : 0.0f);
  t.patches = patches;
  t.cols = bank[s.grid];
  t.inv_cols = bank[s.grid + 1];
  t.cell[0] = bank[s.cell];
  t.cell[1] = bank[s.cell + 1];
  t.height_scale = bank[s.height_scale];
  t.origin[0] = bank[s.origin];
  t.origin[1] = bank[s.origin + 1];
  t.tex_offset[0] = bank[s.tex_offset];
  t.tex_offset[1] = bank[s.tex_offset + 1];
  t.tex_scale[0] = bank[s.tex_scale];
  t.tex_scale[1] = bank[s.tex_scale + 1];
  t.active = true;
  *out = t;
  return true;
}

// Quads per patch edge. A patch spans 16 heightmap texels in gameplay (cell 8,
// 0.5 world units per texel) and the GPU tessellates it at most 15 times.
inline constexpr uint32_t kTerrainGrid = 16;

// (n + 1)^2 grid points per patch, v outer, as (world.x, world.y, height, 1).
inline bool BuildTerrainPositions(const TerrainPatch& t, const uint8_t* data, size_t size,
                                  uint32_t n, std::vector<Float4>* out) {
  out->clear();
  if (n == 0 || t.patches == 0 || t.patches > 4096) return false;
  out->reserve(size_t(t.patches) * (n + 1) * (n + 1));
  for (uint32_t k = 0; k < t.patches; ++k) {
    const float p = t.patch + float(k);
    const float row = std::floor(p * t.inv_cols);
    const float col = p - row * t.cols;
    for (uint32_t j = 0; j <= n; ++j) {
      for (uint32_t i = 0; i <= n; ++i) {
        const float u = float(i) / float(n), v = float(j) / float(n);
        const float wx = (col + u) * t.cell[0] + t.origin[0];
        const float wy = (row + v) * t.cell[1] + t.origin[1];
        float h = 0;
        if (!SampleHeight(data, size, t.map, (wx - t.tex_offset[0]) * t.tex_scale[0],
                          (wy - t.tex_offset[1]) * t.tex_scale[1], &h)) {
          out->clear();
          return false;
        }
        out->push_back({wx, wy, h * t.height_scale, 1.0f});
      }
    }
  }
  return true;
}

// Two triangles per grid quad, `patches` grids of (n + 1)^2 points each.
inline void BuildGridIndices(uint32_t n, uint32_t patches, std::vector<uint32_t>* out) {
  out->clear();
  out->reserve(size_t(patches) * n * n * 6);
  const uint32_t points = (n + 1) * (n + 1);
  for (uint32_t k = 0; k < patches; ++k) {
    for (uint32_t j = 0; j < n; ++j) {
      for (uint32_t i = 0; i < n; ++i) {
        const uint32_t a = k * points + j * (n + 1) + i;
        const uint32_t b = a + 1, c = a + n + 2, d = a + n + 1;
        out->insert(out->end(), {a, b, c, a, c, d});
      }
    }
  }
}

}  // namespace fable2::native::capture
