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

#include "../render/texture_decode.h"
#include "position_decode.h"
#include "uv_decode.h"
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
    // Linear rows by the SDK's base-level rule (texture_util::GetGuestTextureLayout,
    // as render::BaseLevelLinear): the fetch pitch, a multiple of 32 texels, with no
    // further row alignment. Every heightmap observed is tiled; the far colour
    // tables of the adaptive vertex shader (TerrainFarKnot) are linear.
    m.size = m.pitch * 2 * (m.height - 1) + m.width * 2;
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
    o = uint64_t(y) * (uint64_t(m.pitch) * 2) + uint64_t(x) * 2;
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

// Mip 0 of the layer vertex shaders' normal map (tf17): a Xenos 2D texture in
// k_DXN, two values per texel (render::DecodeDxnTexel has the block layout and
// its source).
struct NormalMap {
  uint32_t phys_addr = 0;  // GPU physical
  uint32_t size = 0;       // bytes the level spans from phys_addr
  uint32_t width = 0, height = 0;
  uint32_t pitch_blocks = 0;  // 4 x 4 blocks per row (fetch constant pitch field << 5, in texels, / 4)
  uint32_t endian = 0;        // xenos::Endian of the 16-byte blocks
  bool tiled = false;
  uint8_t clamp_x = 0, clamp_y = 0;  // xenos::ClampMode
};

// Decodes the normal map's texture fetch constant (host-order dwords). Accepted:
// a 2D texture in k_DXN (49), unsigned x and y read from channels 0 and 1, exp
// adjust 0, point magnification filter (a vertex shader's fetch has no computed
// level; C30A97D946FA2BE4 instr 25). Anything else returns false (the draw has
// no steep-ground block to draw with, so its material is not usable).
inline bool DecodeNormalMap(const uint32_t fc[6], NormalMap* out) {
  if ((fc[0] & 3) != 2) return false;                // not a texture fetch constant
  if (((fc[0] >> 2) & 0xF) != 0) return false;       // x or y signed
  if ((fc[1] & 0x3F) != 49) return false;            // k_DXN
  if (((fc[5] >> 9) & 3) != 1) return false;         // not 2D
  if (((fc[3] >> 1) & 0x3F) != 0x08) return false;   // swizzle: x from channel 0, y from channel 1
  if (((fc[3] >> 13) & 0x3F) != 0) return false;     // exp adjust
  if (((fc[3] >> 19) & 3) != 0) return false;        // mag filter not point
  NormalMap m;
  m.endian = (fc[1] >> 6) & 3;
  m.clamp_x = uint8_t((fc[0] >> 10) & 7);
  m.clamp_y = uint8_t((fc[0] >> 13) & 7);
  m.pitch_blocks = (((fc[0] >> 22) & 0x1FF) << 5) / 4;
  m.tiled = (fc[0] >> 31) != 0;
  m.phys_addr = fc[1] & 0xFFFFF000u;
  m.width = (fc[2] & 0x1FFF) + 1;
  m.height = ((fc[2] >> 13) & 0x1FFF) + 1;
  // The SDK's bound of a tiled level, or the linear rows (0: no base address, or a pitch below the width).
  const uint64_t bytes = render::BaseLevelDumpBytes(fc);
  if (bytes == 0 || bytes > 0xFFFFFFFFull) return false;
  m.size = uint32_t(bytes);
  *out = m;
  return true;
}

// Point sample of the normal map at normalized (u, v), as SampleHeight: the two
// values (0..1) of texel floor(u * width), floor(v * height) under the map's
// clamp modes. False when the texel's block lies outside `size`.
inline bool SampleNormal(const uint8_t* data, size_t size, const NormalMap& m, float u, float v, float out_xy[2]) {
  if (m.width == 0 || m.height == 0) return false;
  return render::DecodeDxnTexel(data, size, m.width, m.height, m.pitch_blocks, m.tiled, m.endian,
                                detail::TexelIndex(u, m.width, m.clamp_x), detail::TexelIndex(v, m.height, m.clamp_y),
                                out_xy);
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
// The game samples tf18 at its vertices (component w in both vertex shaders;
// k_DXT3A is presented with its one channel in every component, so x would
// read the same); a bilinear sample at weight_uv approximates that weight: it
// is the weight at 16 segments per patch edge (the cell's texel centres are the
// patch's 17 x 17 grid points), while the game's tessellator tops out at 15 and
// removes whole triangles at a culled vertex, not pixels.
//
// The steep-ground block (`steep`: the three layer pixel shaders paired with
// C30A, 9C2144E42E95A76F, 45243B758F9272AB and 04F23292B2F44077; the table's
// "steep" object). Where the ground is steep they blend the layer's side
// texture tf13 into the detail colour by two side projections that use the
// height (9C21 instr 12 to 37; frame-map section 13, "Terrain", item 1):
//   n = the vertex shader's normal: tf17 at the height coordinate, * 2 - 1,
//       z = sqrt(|1 - (x^2 + y^2)|) (TerrainNormalFromTexel), interpolated
//   s = sat(|n.z| * slope[1] + slope[0])                      (TerrainSteepSlope)
//   colour^2 = tf0^2 where s >= steep_threshold, else
//     s * tf0^2 + (1 - s) * (wy * tf13(o1)^2 + wx * tf13(o0)^2)  (TerrainSteepMix)
//   wx, wy = |n.x|, |n.y| / (|n.x| + |n.y| + steep_eps)       (TerrainSteepWeights)
//   o0 = (y, height) * side_uv, o1 = (x, height) * side_uv    (TerrainSideUvs)
// The clay pass shows the square root of that mix (the unlit rule: the display
// value of the game's linear colour).
//
// Draws of the adaptive builder (82207C30, vertex shader 5003700B7C9B1C16, kinds
// terrain-far and terrain-far-fade; `adaptive`):
//   colour = tf13(u, v)^2 * pow(tf3(base_uv), base_pow) * base_tint, opaque; the
//   fading kind times sat(d2 * fade[0] + fade[1]) (it is the underlay the
//   layers add to: 0 where the layers are whole)
//   u and v are read from two tables at the patch's 17 knots per edge
//   (TerrainFarKnot) and vary linearly between them.
//
// The clay pass draws the colour texture alone (the unlit rule of the frame
// map: tf3 is baked light and base_tint an ambient tint, both lighting): it
// does not read base_fetch, base_uv, base_pow or base_tint. They are kept for
// the lighting sub-project when they could be read (base_valid) and are no part
// of `valid`: a draw without a texture in the base map's slot, or with a ps c21
// that is not finite, still has its material.
struct TerrainMaterialInputs {
  // false: something the material reads could not be read or is not finite; nothing below is usable
  bool valid = false;
  // base_fetch, base_uv, base_pow and base_tint were read and are finite (else they hold their defaults)
  bool base_valid = false;
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
  // The steep-ground block. false: the draw's pixel shader has none, and the fields below but
  // normal_fetch are zero.
  bool steep = false;
  uint32_t side_fetch[6] = {};    // tf13 of the layer draw: the layer's side texture (not the far colour)
  // tf17 of the vertex shader: the normal map (k_DXN, DecodeNormalMap). The three terrain vertex
  // shaders read it at the height coordinate for the normal their pixel shaders light with (C30A
  // and FB68 instr 25, 5003 instr 27), and the block reads the same normal: kept for every terrain
  // draw whose slot holds a texture fetch constant, zero otherwise (then the draw has no normals;
  // with the block it is not valid).
  uint32_t normal_fetch[6] = {};
  float side_uv[2] = {};          // (vs c8.x, -vs c8.x)
  float slope[2] = {};            // ps c45.xy
  float steep_threshold = 0.0f;   // the block runs where s is below it (a literal of the pixel shader)
  float steep_eps = 0.0f;         // the small term of the weights (a literal of the pixel shader)
};

// The inputs of one terrain draw: an entry of its frame's side array
// (FrameScene::terrain), which the record names by DrawRecord::terrain_index.
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
  // Not part of the positions and of no position or index key. The clay pass
  // draws the record with it (clay_logic.h, "Terrain material"); the far colour
  // coordinates built from it have their own key (TerrainFarUvKey).
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
// The steep-ground block: ps c45.xy is the slope's offset and scale (9C21 instr
// 12, 4524 instr 15, 04F2 instr 16); the side scale is vs c8.x, in the detail
// scale's register (C30A instr 53, 63, 64).
inline constexpr uint32_t kTerrainPsSlopeReg = 45;
// Texture fetch constants the vertex shaders sample besides the heightmap: the
// layers' weight cells (C30A and FB68 instr 24), the adaptive shader's tables
// of u and v (5003 instr 26 and 25), the layers' normal map (C30A and FB68
// instr 25). The pixel shaders' slots (base map, detail or far colour, the
// side texture) are in ps-albedo.json.
inline constexpr uint32_t kTerrainWeightFetch = 18, kTerrainFarUFetch = 18, kTerrainFarVFetch = 19,
                          kTerrainNormalFetch = 17;

// ps-albedo.json "steep" (FABLE2_PS_TERRAIN): the steep-ground block of a layer
// pixel shader. The two numbers are literals of the shader (zero in the device
// bank), entered from the shaders' own literal tables as captured in game
// session 1 ("ps_lit": c254.y, in 45243B758F9272AB c254.z, and c255.w).
struct TerrainSteepSpec {
  uint8_t side_slot;  // the pixel shader's fetch constant of the layer's side texture
  float threshold;    // 255/256: the block runs where the slope value is below it
  float epsilon;      // 0.0001: the small term of the weights
};
// A small term of this much or more is not the block's literal: the bound of the
// table generator and of the replay (STEEP_EPSILON_MAX in gen_albedo_table.py).
inline constexpr float kTerrainSteepEpsMax = 0.01f;

// The fetch constants of a terrain draw (six host-order dwords each), nullptr
// for one that could not be read. A layer needs detail and weight; a far draw
// detail (the far colour), far_u and far_v. The base map is optional (base_valid).
// A layer whose shader has the steep-ground block also needs side and normal.
// The normal map is given for every terrain draw (the light reads it); without
// the block it is optional, like the base map.
struct TerrainFetches {
  const uint32_t* detail = nullptr;
  const uint32_t* base = nullptr;
  const uint32_t* weight = nullptr;
  const uint32_t* far_u = nullptr;
  const uint32_t* far_v = nullptr;
  const uint32_t* side = nullptr;
  const uint32_t* normal = nullptr;
};

// The material inputs of a terrain draw from its patch, the two constant banks
// (256 x float4, host order; only the registers named above are read) and its
// fetch constants. False, with out->valid false, when the patch is not active,
// a bank or a needed fetch constant is missing or not a texture fetch constant,
// a value the material reads is not finite, or the patch has no cell size. The
// base map's fetch constant, vs c14 and ps c21 decide base_valid only (a caller
// that could not read one of the two registers passes values that are not finite).
// The vertex shader's normal map (f.normal) is kept in normal_fetch for any
// terrain draw when it is a texture fetch constant; without the block it
// decides nothing else.
// `steep`: the steep-ground block of the draw's pixel shader, or nullptr when it
// has none. With it the inputs are valid only if the block's can be read too:
// the side texture's and the normal map's fetch constants are texture fetch
// constants, vs c8.x and ps c45.xy are finite, and the two literals can be the
// block's (a threshold in (0, 1], a positive small term below
// kTerrainSteepEpsMax); the block is the
// layers', so with a draw of the adaptive builder it is refused.
inline bool MakeTerrainMaterialInputs(const TerrainPatch& patch, bool adaptive, const float* vs_bank,
                                      const float* ps_bank, const TerrainFetches& f,
                                      TerrainMaterialInputs* out, const TerrainSteepSpec* steep = nullptr) {
  *out = TerrainMaterialInputs{};
  if (!patch.active || !vs_bank || !ps_bank) return false;
  if (steep && adaptive) return false;
  TerrainMaterialInputs m;
  m.adaptive = adaptive;
  auto fetch = [](const uint32_t* fc, uint32_t* dst) {
    if (!fc || (fc[0] & 3) != 2) return false;  // unread, or not a texture fetch constant
    for (int i = 0; i < 6; ++i) dst[i] = fc[i];
    return true;
  };
  if (!fetch(f.detail, m.detail_fetch)) return false;
  if (adaptive ? !(fetch(f.far_u, m.far_u_fetch) && fetch(f.far_v, m.far_v_fetch)) : !fetch(f.weight, m.weight_fetch)) {
    return false;
  }
  auto vs = [&](uint32_t reg, uint32_t comp) { return vs_bank[4 * reg + comp]; };
  auto ps = [&](uint32_t reg, uint32_t comp) { return ps_bank[4 * reg + comp]; };
  {
    // The base map's part: all of it or none.
    TerrainMaterialInputs b;
    b.base_uv[0] = vs(kTerrainVsBaseScaleReg, 0);
    b.base_uv[1] = vs(kTerrainVsBaseScaleReg, 1);
    b.base_uv[2] = -patch.tex_offset[0] * b.base_uv[0];
    b.base_uv[3] = -patch.tex_offset[1] * b.base_uv[1];
    b.base_pow = ps(kTerrainPsBaseReg, 3);
    for (uint32_t i = 0; i < 3; ++i) b.base_tint[i] = ps(kTerrainPsBaseReg, i);
    bool ok = fetch(f.base, b.base_fetch) && std::isfinite(b.base_pow);
    for (float v : b.base_uv) ok = ok && std::isfinite(v);
    for (float v : b.base_tint) ok = ok && std::isfinite(v);
    if (ok) {
      for (int i = 0; i < 6; ++i) m.base_fetch[i] = b.base_fetch[i];
      for (int i = 0; i < 4; ++i) m.base_uv[i] = b.base_uv[i];
      for (int i = 0; i < 3; ++i) m.base_tint[i] = b.base_tint[i];
      m.base_pow = b.base_pow;
      m.base_valid = true;
    }
  }
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
  // The vertex shader's normal map, for the light of every terrain draw: kept when it is a
  // texture fetch constant, else left zero (no normals; the material does not depend on it).
  const bool has_normal = fetch(f.normal, m.normal_fetch);
  if (steep) {
    if (!fetch(f.side, m.side_fetch) || !has_normal) return false;
    m.side_uv[0] = vs(kTerrainVsDetailReg, 0);
    m.side_uv[1] = -vs(kTerrainVsDetailReg, 0);
    m.slope[0] = ps(kTerrainPsSlopeReg, 0);
    m.slope[1] = ps(kTerrainPsSlopeReg, 1);
    m.steep_threshold = steep->threshold;
    m.steep_eps = steep->epsilon;
    // (comparisons with a value that is not finite are false)
    if (!std::isfinite(m.side_uv[0]) || !std::isfinite(m.slope[0]) || !std::isfinite(m.slope[1]) ||
        !(m.steep_threshold > 0.0f && m.steep_threshold <= 1.0f) ||
        !(m.steep_eps > 0.0f && m.steep_eps < kTerrainSteepEpsMax)) {
      return false;
    }
    m.steep = true;
  }
  m.fade[0] = ps(kTerrainPsFadeReg, 0);
  m.fade[1] = ps(kTerrainPsFadeReg, 1);
  for (uint32_t i = 0; i < 3; ++i) m.eye[i] = ps(kTerrainPsEyeReg, i);
  bool finite = std::isfinite(m.hole_ref);
  for (float v : m.detail_uv) finite = finite && std::isfinite(v);
  for (float v : m.weight_uv) finite = finite && std::isfinite(v);
  for (float v : m.far_table) finite = finite && std::isfinite(v);
  for (float v : m.far_patch) finite = finite && std::isfinite(v);
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

// The layer vertex shader's normal from the normal map's two values (0..1):
// (x, y) * 2 - 1 and z = sqrt(|1 - (x^2 + y^2)|) (C30A instr 26 to 31, o3).
inline void TerrainNormalFromTexel(float x, float y, float n[3]) {
  n[0] = 2.0f * x - 1.0f;
  n[1] = 2.0f * y - 1.0f;
  n[2] = std::sqrt(std::fabs(1.0f - (n[0] * n[0] + n[1] * n[1])));
}

// Steep-ground block: the slope value of a normal's z, 1 on flat ground and 0 on
// steep (9C21 instr 12: mad_sat of |n.z| with c45.y and c45.x).
inline float TerrainSteepSlope(const TerrainMaterialInputs& m, float nz) {
  const float s = std::fabs(nz) * m.slope[1] + m.slope[0];
  return std::fmin(std::fmax(s, 0.0f), 1.0f);
}

// The weights of the two side projections: out[0] = wx goes with o0 (faces along
// x), out[1] = wy with o1 (9C21 instr 22 to 28 and 36).
inline void TerrainSteepWeights(const TerrainMaterialInputs& m, float nx, float ny, float out[2]) {
  const float inv = 1.0f / (std::fabs(nx) + m.steep_eps + std::fabs(ny));
  out[0] = std::fabs(nx) * inv;
  out[1] = std::fabs(ny) * inv;
}

// The side texture's two coordinates at the patch point (x, y, height):
// o0 = (c8.x * y, -c8.x * height), o1 = (c8.x * x, -c8.x * height) (C30A instr
// 52, 53, 63, 64, 74 to 76).
inline void TerrainSideUvs(const TerrainMaterialInputs& m, float x, float y, float height, float o0[2], float o1[2]) {
  o0[0] = y * m.side_uv[0];
  o0[1] = height * m.side_uv[1];
  o1[0] = x * m.side_uv[0];
  o1[1] = height * m.side_uv[1];
}

// The layer's colour of a steep-block shader in the game's linear space, per
// channel: tf0^2 where the slope value reaches the threshold (the game skips the
// block when not threshold > s), else the mix with side0 = tf13(o0) and
// side1 = tf13(o1), all three as stored. The clay pass's pixel shader
// (kClayTerrainLayerPs) shows its square root.
inline void TerrainSteepMix(const TerrainMaterialInputs& m, const float n[3], const float tf0[3], const float side0[3],
                            const float side1[3], float out[3]) {
  const float s = TerrainSteepSlope(m, n[2]);
  if (!(s < m.steep_threshold)) {
    for (int k = 0; k < 3; ++k) out[k] = tf0[k] * tf0[k];
    return;
  }
  float w[2];
  TerrainSteepWeights(m, n[0], n[1], w);
  for (int k = 0; k < 3; ++k) {
    out[k] = s * tf0[k] * tf0[k] + (1.0f - s) * (w[1] * side1[k] * side1[k] + w[0] * side0[k] * side0[k]);
  }
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
// Patches of one record a grid is built for (the largest draw seen has 36; the
// capture admits any count up to kMaxDrawCount). A record with more has no
// positions, no far coordinates and, in the clay pass's plan, no squares.
inline constexpr uint32_t kMaxTerrainPatches = 4096;

// Far: the far colour (tf13) coordinate of every grid point of the draw's
// patches, in the order of BuildTerrainPositions: (n + 1)^2 per patch, v outer.
// The adaptive vertex shader reads u from one table and v from the other at its
// vertices, point sampled (5003700B7C9B1C16 instr 21 to 26); grid point (i, j)
// of a 16 x 16 grid is knot i of the u table and knot j of the v table
// (TerrainFarKnot), so the coordinate varies linearly between the 17 knots of a
// patch edge, as in the replay tool (terrain_replay.py, far_uv). `um` / `vm` are
// the decoded fetch constants material.far_u_fetch / far_v_fetch
// (DecodeHeightMap: the tables are k_16 like the heightmap) with their bytes.
// False, with nothing in `out`, unless the inputs are valid ones of the
// adaptive builder, n is 16 and every texel read lies inside its table.
inline bool BuildTerrainFarUvs(const TerrainPatch& t, const HeightMap& um, const uint8_t* u_data, size_t u_size,
                               const HeightMap& vm, const uint8_t* v_data, size_t v_size, uint32_t n,
                               std::vector<Float2>* out) {
  out->clear();
  const TerrainMaterialInputs& m = t.material;
  if (!t.active || !m.valid || !m.adaptive || n != 16 || t.patches == 0 || t.patches > kMaxTerrainPatches) {
    return false;
  }
  out->reserve(size_t(t.patches) * (n + 1) * (n + 1));
  for (uint32_t k = 0; k < t.patches; ++k) {
    const float p = t.patch + float(k);
    const float row = std::floor(p * t.inv_cols);
    const float col = p - row * t.cols;
    float u[17], v[17];
    for (uint32_t i = 0; i <= n; ++i) {
      float at[2];
      TerrainFarKnot(m, col, row, i, at);
      if (!SampleHeight(u_data, u_size, um, at[0], at[1], &u[i]) ||
          !SampleHeight(v_data, v_size, vm, at[0], at[1], &v[i])) {
        out->clear();
        return false;
      }
    }
    for (uint32_t j = 0; j <= n; ++j) {
      for (uint32_t i = 0; i <= n; ++i) out->push_back({u[i], v[j]});
    }
  }
  return true;
}

// The terrain vertex shader's (n.x, n.y) at every grid point of the draw's
// patches, in the order of BuildTerrainPositions: the normal the terrain pixel
// shaders light with, and the one the steep-ground block reads. Every terrain
// draw has it, a layer with or without the block and a draw of the adaptive
// builder: the three vertex shaders read the normal map at the height
// coordinate of their vertex, point sampled (C30A and FB68 instr 25: the
// coordinate of the heightmap fetch of instr 20; 5003 instr 27: that of
// instr 18), and the grid's points are the points a layer's would be evaluated
// at with 16 segments per patch edge, each on a texel of its own in the game's
// maps (two texels per world unit, as the heightmap); a far draw's patches are
// larger, and its grid samples the map as it samples the heights. The clay
// vertex shader forms z from the pair as the game's does, and the three are
// interpolated over the grid's triangles.
// `nm` is the decoded fetch constant material.normal_fetch with its bytes.
// False, with nothing in `out`, unless the patch is active, its inputs are
// valid and every texel read lies inside the map's bytes.
inline bool BuildTerrainNormals(const TerrainPatch& t, const NormalMap& nm, const uint8_t* data, size_t size, uint32_t n,
                                std::vector<Float2>* out) {
  out->clear();
  const TerrainMaterialInputs& m = t.material;
  if (!t.active || !m.valid || n == 0 || t.patches == 0 || t.patches > kMaxTerrainPatches) return false;
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
        float xy[2], normal[3];
        if (!SampleNormal(data, size, nm, (wx - t.tex_offset[0]) * t.tex_scale[0],
                          (wy - t.tex_offset[1]) * t.tex_scale[1], xy)) {
          out->clear();
          return false;
        }
        TerrainNormalFromTexel(xy[0], xy[1], normal);
        out->push_back({normal[0], normal[1]});
      }
    }
  }
  return true;
}

// (n + 1)^2 grid points per patch, v outer, as (world.x, world.y, height, 1).
inline bool BuildTerrainPositions(const TerrainPatch& t, const uint8_t* data, size_t size,
                                  uint32_t n, std::vector<Float4>* out) {
  out->clear();
  if (n == 0 || t.patches == 0 || t.patches > kMaxTerrainPatches) return false;
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
