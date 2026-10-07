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
#include "xenos_tiling.h"

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
    // Linear rows padded to 256 bytes: unverified (every heightmap observed is
    // tiled, so this branch has never run; the SDK's base-level rule aligns the
    // pitch to 32 texels instead, see render::BaseLevelLinear).
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
  // NaN or infinity (garbage constants): fmod would give NaN, and converting
  // that to an integer is undefined.
  if (!std::isfinite(f)) return 0;
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

// What the terrain shaders' unlit colour needs from one terrain draw besides the
// patch geometry (frame-map section 13, "Terrain": the reading of the three
// vertex and eight pixel shaders, with instruction numbers). Coordinate
// transforms take the world position the patch grid already has:
// u = x * [0] + [2], v = y * [1] + [3] (TerrainUv). Constants are named as the
// shaders read them: "vs cN" the vertex bank, "ps cN" the pixel bank.
//
// Layer draws (builder 8221C9C8, vertex shaders C30A97D946FA2BE4 and
// FB68A7F2301210E1, kind terrain-layer):
//   colour = tf0(detail_uv)^2 * pow(tf3(base_uv), base_pow) * base_tint
//   alpha  = TerrainFade(d2) * TerrainLayerWeight(tf18(weight_uv)), d2 the squared
//            distance from `eye` to `world` * (x, y, height, 1)
//   the draw has no geometry where tf18 < hole_ref (TerrainLayerCulled)
// The game samples tf18 at its vertices; a bilinear sample at weight_uv is that
// weight at 16 segments per patch edge (the cell's texel centres are the patch's
// 17 x 17 grid points). Not covered: the pixel shaders paired with C30A blend a
// second colour texture (tf13, at two coordinates that use the height) into the
// detail colour where the ground is steep; these inputs hold only tf0.
//
// Draws of the adaptive builder (82207C30, vertex shader 5003700B7C9B1C16, kinds
// terrain-far and terrain-far-fade; `adaptive`):
//   colour = tf13(u, v)^2 * pow(tf3(base_uv), base_pow) * base_tint, opaque; the
//   fading kind times sat(d2 * fade[0] + fade[1]) (it is the underlay the
//   layers add to: 0 where the layers are whole)
//   u and v are read from two tables at the patch's 17 knots per edge
//   (TerrainFarKnot) and vary linearly between them.
struct TerrainMaterialInputs {
  bool valid = false;  // false: something could not be read or is not finite; nothing below is usable
  bool adaptive = false;  // a draw of the adaptive builder (the far kinds)
  uint32_t detail_fetch[6] = {};  // layer: tf0, the layer's detail colour; far: tf13, the far colour
  uint32_t base_fetch[6] = {};    // tf3, the base map
  uint32_t weight_fetch[6] = {};  // layer: tf18 of the vertex shader, the weight cells (k_DXT3A); far: zero
  uint32_t far_u_fetch[6] = {};   // far: tf18 of the vertex shader, the table of u (k_16); layer: zero
  uint32_t far_v_fetch[6] = {};   // far: tf19, the table of v
  float detail_uv[4] = {};  // layer: (vs c8.y, -vs c8.y, 0, 0); far: zero (the tables address tf13)
  float base_uv[4] = {};    // (world - heightmap offset) * vs c14.xy; the offset is TerrainPatch::tex_offset
  float weight_uv[4] = {};  // layer: (col + u) * vs c115.x + c115.z and the same in y, as a world transform
  float far_table[4] = {};  // far: vs c72.x, c72.y, c114.x, c114.y
  float far_patch[2] = {};  // far: vs c115.xy, the block's first patch in the tables
  float base_pow = 1.0f;                     // ps c21.w
  float base_tint[3] = {1.0f, 1.0f, 1.0f};   // ps c21.xyz (changes from frame to frame: see the frame map)
  float weight_scale = 0.0f, weight_bias = 0.0f;  // layer: the literals vs c253.w and c253.y
  float fade[2] = {};      // ps c31.xy
  float hole_ref = 0.0f;   // layer: vs c114.x (1/255, or -1 for a layer that is never culled)
  float eye[3] = {};       // ps c19.xyz
  float world[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};  // layer: vs c4..c6, three rows; far: identity
};

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
  // Not part of the geometry and of no cache key; the renderer does not read it
  // yet (terrain is drawn as flat clay).
  TerrainMaterialInputs material;
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

// The layer vertex shaders turn the weight texel into the weight
// c253.w * (tf18 - c253.y) (C30A97D946FA2BE4 instr 48 and 58, FB68A7F2301210E1
// instr 62 and 63). c253 is a shader literal, zero in the device bank; these
// are the GPU register file's values, c253 = (0.5, 1/15, -1, 15/14) in all
// 78,016 logged draws of the two shaders ([vtess] log, frame-map section 13):
// the 4-bit texel value n (sampled as n / 15) has the weight (n - 1) / 14.
inline constexpr float kTerrainWeightScale = 15.0f / 14.0f;
inline constexpr float kTerrainWeightBias = 1.0f / 15.0f;
// c254.x of the adaptive vertex shader (5003700B7C9B1C16 instr 21), the same
// kind of literal from the same log: the centre of the patch's table row.
inline constexpr float kTerrainFarRowBias = 0.5f;

// Registers of the material inputs. Vertex bank: c4..c6 the layers' world
// matrix rows (C30A instr 56, 57, 70; FB68 instr 55, 56, 69), c8.y the detail
// scale (C30A instr 50, 55; FB68 instr 47, 55), c14.xy the base map scale (C30A
// instr 68, 72; FB68 instr 67, 71; 5003 instr 37), c72 and c114 the far tables'
// knot spacing and patch size (5003 instr 22, 24), c114.x the layers' cull
// reference (instr 37 of both), c115 the weight cell (instr 23 of both) or the
// far block's first patch (5003 instr 21, 23). Pixel bank: c19 the eye, c21 the
// base map's exponent and tint, c31 the fade (instruction numbers per pixel
// shader in the frame map).
inline constexpr uint32_t kTerrainVsWorldReg = 4, kTerrainVsDetailReg = 8, kTerrainVsBaseScaleReg = 14,
                          kTerrainVsFarKnotReg = 72, kTerrainVsCullReg = 114, kTerrainVsCellReg = 115;
inline constexpr uint32_t kTerrainPsEyeReg = 19, kTerrainPsBaseReg = 21, kTerrainPsFadeReg = 31;
// Texture fetch constants the vertex shaders sample besides the heightmap: the
// layers' weight cells (C30A and FB68 instr 24), the adaptive shader's tables
// of u and v (5003 instr 26 and 25). The pixel shaders' slots (base map,
// detail or far colour) are in ps-albedo.json.
inline constexpr uint32_t kTerrainWeightFetch = 18, kTerrainFarUFetch = 18, kTerrainFarVFetch = 19;

// The fetch constants of a terrain draw (six host-order dwords each), nullptr
// for one that could not be read. A layer needs detail, base and weight; a far
// draw detail (the far colour), base, far_u and far_v.
struct TerrainFetches {
  const uint32_t* detail = nullptr;
  const uint32_t* base = nullptr;
  const uint32_t* weight = nullptr;
  const uint32_t* far_u = nullptr;
  const uint32_t* far_v = nullptr;
};

// The material inputs of a terrain draw from its patch, the two constant banks
// (256 x float4, host order; only the registers named above are read) and its
// fetch constants. False, with out->valid false, when the patch is not active,
// a bank or a needed fetch constant is missing or not a texture fetch constant,
// a value is not finite, or the patch has no cell size.
inline bool MakeTerrainMaterialInputs(const TerrainPatch& patch, bool adaptive, const float* vs_bank,
                                      const float* ps_bank, const TerrainFetches& f,
                                      TerrainMaterialInputs* out) {
  *out = TerrainMaterialInputs{};
  if (!patch.active || !vs_bank || !ps_bank) return false;
  TerrainMaterialInputs m;
  m.adaptive = adaptive;
  auto fetch = [](const uint32_t* fc, uint32_t* dst) {
    if (!fc || (fc[0] & 3) != 2) return false;  // unread, or not a texture fetch constant
    for (int i = 0; i < 6; ++i) dst[i] = fc[i];
    return true;
  };
  if (!fetch(f.detail, m.detail_fetch) || !fetch(f.base, m.base_fetch)) return false;
  if (adaptive ? !(fetch(f.far_u, m.far_u_fetch) && fetch(f.far_v, m.far_v_fetch)) : !fetch(f.weight, m.weight_fetch)) {
    return false;
  }
  auto vs = [&](uint32_t reg, uint32_t comp) { return vs_bank[4 * reg + comp]; };
  auto ps = [&](uint32_t reg, uint32_t comp) { return ps_bank[4 * reg + comp]; };
  m.base_uv[0] = vs(kTerrainVsBaseScaleReg, 0);
  m.base_uv[1] = vs(kTerrainVsBaseScaleReg, 1);
  m.base_uv[2] = -patch.tex_offset[0] * m.base_uv[0];
  m.base_uv[3] = -patch.tex_offset[1] * m.base_uv[1];
  if (adaptive) {
    m.far_table[0] = vs(kTerrainVsFarKnotReg, 0);
    m.far_table[1] = vs(kTerrainVsFarKnotReg, 1);
    m.far_table[2] = vs(kTerrainVsCullReg, 0);
    m.far_table[3] = vs(kTerrainVsCullReg, 1);
    m.far_patch[0] = vs(kTerrainVsCellReg, 0);
    m.far_patch[1] = vs(kTerrainVsCellReg, 1);
  } else {
    m.detail_uv[0] = vs(kTerrainVsDetailReg, 1);
    m.detail_uv[1] = -vs(kTerrainVsDetailReg, 1);
    // (col + u) = (x - origin) / cell, so the cell coordinate is a transform of the world position
    // (a patch without a cell size gives a value that is not finite, which is refused below).
    m.weight_uv[0] = vs(kTerrainVsCellReg, 0) / patch.cell[0];
    m.weight_uv[1] = vs(kTerrainVsCellReg, 1) / patch.cell[1];
    m.weight_uv[2] = vs(kTerrainVsCellReg, 2) - patch.origin[0] * m.weight_uv[0];
    m.weight_uv[3] = vs(kTerrainVsCellReg, 3) - patch.origin[1] * m.weight_uv[1];
    m.weight_scale = kTerrainWeightScale;
    m.weight_bias = kTerrainWeightBias;
    m.hole_ref = vs(kTerrainVsCullReg, 0);
    for (uint32_t i = 0; i < 12; ++i) m.world[i] = vs_bank[4 * kTerrainVsWorldReg + i];
  }
  m.base_pow = ps(kTerrainPsBaseReg, 3);
  for (uint32_t i = 0; i < 3; ++i) m.base_tint[i] = ps(kTerrainPsBaseReg, i);
  m.fade[0] = ps(kTerrainPsFadeReg, 0);
  m.fade[1] = ps(kTerrainPsFadeReg, 1);
  for (uint32_t i = 0; i < 3; ++i) m.eye[i] = ps(kTerrainPsEyeReg, i);
  bool finite = std::isfinite(m.base_pow) && std::isfinite(m.hole_ref);
  for (float v : m.detail_uv) finite = finite && std::isfinite(v);
  for (float v : m.base_uv) finite = finite && std::isfinite(v);
  for (float v : m.weight_uv) finite = finite && std::isfinite(v);
  for (float v : m.far_table) finite = finite && std::isfinite(v);
  for (float v : m.far_patch) finite = finite && std::isfinite(v);
  for (float v : m.base_tint) finite = finite && std::isfinite(v);
  for (float v : m.fade) finite = finite && std::isfinite(v);
  for (float v : m.eye) finite = finite && std::isfinite(v);
  for (float v : m.world) finite = finite && std::isfinite(v);
  if (!finite) return false;
  m.valid = true;
  *out = m;
  return true;
}

// u = x * xf[0] + xf[2], v = y * xf[1] + xf[3].
inline void TerrainUv(const float xf[4], float x, float y, float out[2]) {
  out[0] = x * xf[0] + xf[2];
  out[1] = y * xf[1] + xf[3];
}

// A layer's weight for the weight map's value (0..1), before any clamp.
inline float TerrainLayerWeight(const TerrainMaterialInputs& m, float value) {
  return m.weight_scale * (value - m.weight_bias);
}

// The layer draws nothing at a vertex whose value is below the reference: the
// vertex shader writes a NaN position (C30A instr 37, 40, 47; FB68 instr 37, 40, 46).
inline bool TerrainLayerCulled(const TerrainMaterialInputs& m, float value) { return value < m.hole_ref; }

// The layers' fade with the squared eye distance; the fading far kind uses 1 - this.
inline float TerrainFade(const TerrainMaterialInputs& m, float d2) {
  const float t = d2 * m.fade[0] + m.fade[1];
  return 1.0f - std::fmin(std::fmax(t, 0.0f), 1.0f);
}

// Squared distance from the eye to the patch point (x, y, height) through `world`.
inline float TerrainEyeDistance2(const TerrainMaterialInputs& m, float x, float y, float height) {
  float d2 = 0.0f;
  for (int r = 0; r < 3; ++r) {
    const float p = m.world[4 * r] * x + m.world[4 * r + 1] * y + m.world[4 * r + 2] * height + m.world[4 * r + 3];
    d2 += (m.eye[r] - p) * (m.eye[r] - p);
  }
  return d2;
}

// Far: the table coordinate of knot k (0..16) of the patch at column `col`, row
// `row` of the draw's block (5003700B7C9B1C16 instr 21 to 26). The u table is
// read with the knot along the patch's u, the v table with the knot along its v;
// the row coordinate is the same for both.
inline void TerrainFarKnot(const TerrainMaterialInputs& m, float col, float row, uint32_t k, float out[2]) {
  out[0] = (m.far_patch[0] + col + (float(k) / 16.0f) * m.far_table[0] + m.far_table[1]) * m.far_table[2];
  out[1] = (m.far_patch[1] + row + kTerrainFarRowBias) * m.far_table[3];
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
