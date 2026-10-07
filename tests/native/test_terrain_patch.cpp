// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/terrain_patch.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

using namespace fable2::native::capture;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// The heightmap fetch constant seen in gameplay (frame-map section 9): tf16 of
// every Bowerstone terrain draw.
static const uint32_t kTf16[6] = {0x84C04802, 0x1BD0C058, 0x00480240, 0x01001400, 0x00000000, 0x00000218};

int main() {
  // --- Fetch constant decode ---
  HeightMap m;
  if (!DecodeHeightMap(kTf16, &m)) return 1;
  if (m.phys_addr != 0x1BD0C000 || m.width != 577 || m.height != 577 || m.pitch != 608) return 2;
  if (!m.tiled || m.endian != 1 || m.clamp_x != 2 || m.clamp_y != 2) return 3;
  // Tiled extent: origin of the last 32x32 tile (576, 576) plus 0xC00 for 2-byte texels.
  if (m.size != uint32_t(TiledOffset2D(576, 576, 608, 1)) + 0xC00) return 4;
  if (m.size > 608u * 608u * 2u + 0xC00u) return 5;

  uint32_t fc[6];
  std::memcpy(fc, kTf16, sizeof(fc));
  fc[0] &= ~3u;  // not a texture fetch constant (type 2)
  if (DecodeHeightMap(fc, &m)) return 6;
  std::memcpy(fc, kTf16, sizeof(fc));
  fc[1] = (fc[1] & ~0x3Fu) | 30;  // k_16_FLOAT: not handled
  if (DecodeHeightMap(fc, &m)) return 7;
  std::memcpy(fc, kTf16, sizeof(fc));
  fc[3] |= 1u << 19;  // linear mag filter: not handled
  if (DecodeHeightMap(fc, &m)) return 8;
  std::memcpy(fc, kTf16, sizeof(fc));
  fc[0] |= 1u << 2;  // signed x
  if (DecodeHeightMap(fc, &m)) return 9;
  std::memcpy(fc, kTf16, sizeof(fc));
  fc[5] = (fc[5] & ~(3u << 9)) | (2u << 9);  // 3D
  if (DecodeHeightMap(fc, &m)) return 10;
  std::memcpy(fc, kTf16, sizeof(fc));
  fc[0] &= ~0x80000000u;  // linear (untiled): rows of the fetch pitch, 608 texels of 2 bytes (the SDK's base-level rule)
  if (!DecodeHeightMap(fc, &m) || m.tiled || m.size != 1216u * 576u + 577u * 2u) return 11;

  // --- Tiled addressing (xenia GetTiledOffset2D) ---
  if (TiledOffset2D(0, 0, 608, 1) != 0) return 20;
  if (TiledOffset2D(1, 0, 608, 1) != 2) return 21;
  if (TiledOffset2D(0, 1, 608, 1) != 16) return 22;  // micro rows of 8 texels
  if (TiledOffset2D(8, 0, 608, 1) != 64) return 23;  // ((x >> 3) & 3) << 6
  {
    // Over whole 32x32 tiles the mapping is a bijection onto 2-byte slots.
    std::vector<uint8_t> seen(64 * 64 * 2, 0);
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 64; ++x) {
        const int32_t o = TiledOffset2D(x, y, 64, 1);
        if (o < 0 || o % 2 || o >= int32_t(seen.size()) || seen[size_t(o)]) return 24;
        seen[size_t(o)] = 1;
      }
    }
  }

  // --- Sampling: 64x64 tiled k_16, 8in16 (big-endian words in memory) ---
  HeightMap s;
  s.width = s.height = 64;
  s.pitch = 64;
  s.tiled = true;
  s.endian = 1;
  s.clamp_x = s.clamp_y = 2;
  s.size = uint32_t(TiledOffset2D(32, 32, 64, 1)) + 0xC00;
  std::vector<uint8_t> tex(s.size);
  for (uint32_t y = 0; y < 64; ++y) {
    for (uint32_t x = 0; x < 64; ++x) {
      const uint32_t v = (x * 1000 + y * 7) & 0xFFFF;
      const uint32_t o = uint32_t(TiledOffset2D(int32_t(x), int32_t(y), 64, 1));
      tex[o] = uint8_t(v >> 8);
      tex[o + 1] = uint8_t(v);
    }
  }
  float h = 0;
  if (!HeightTexel(tex.data(), tex.size(), s, 3, 5, &h) || !Near(h, (3000 + 35) / 65535.0f)) return 30;
  // Point sampling: texel floor(u * width); clamp to edge outside [0, 1).
  if (!SampleHeight(tex.data(), tex.size(), s, 10.5f / 64, 20.9f / 64, &h) ||
      !Near(h, (10000 + 140) / 65535.0f)) return 31;
  if (!SampleHeight(tex.data(), tex.size(), s, 1.5f, -0.25f, &h) || !Near(h, (63000 + 0) / 65535.0f)) return 32;
  // Repeat wraps.
  s.clamp_x = 0;
  if (!SampleHeight(tex.data(), tex.size(), s, 1.0f + 2.5f / 64, 0.0f, &h) || !Near(h, 2000 / 65535.0f)) return 33;
  s.clamp_x = 2;
  // Little-endian words (endian 0).
  HeightMap le = s;
  le.endian = 0;
  if (!HeightTexel(tex.data(), tex.size(), le, 3, 5, &h)) return 34;
  {
    const uint32_t v = 3000 + 35;
    const uint32_t swapped = ((v & 0xFF) << 8) | (v >> 8);
    if (!Near(h, swapped / 65535.0f)) return 35;
  }
  // Out-of-range data is a failure, not a read past the buffer.
  if (HeightTexel(tex.data(), 16, s, 40, 40, &h)) return 36;

  // --- Patch positions (shader C30A97D946FA2BE4 math) ---
  TerrainPatch t;
  t.active = true;
  t.patch = 3;  // cols 2: row 1, col 1
  t.patches = 1;
  t.cols = 2;
  t.inv_cols = 0.5f;
  t.cell[0] = t.cell[1] = 8;
  t.height_scale = 100;
  t.origin[0] = 0;
  t.origin[1] = 16;
  t.tex_offset[0] = 0;
  t.tex_offset[1] = 16;
  t.tex_scale[0] = t.tex_scale[1] = 1.0f / 64;  // one texel per world unit
  t.map = s;
  std::vector<Float4> pos;
  if (!BuildTerrainPositions(t, tex.data(), tex.size(), 4, &pos)) return 40;
  if (pos.size() != 25) return 41;
  // Grid point (i, j) = (2, 1): u 0.5, v 0.25 -> world (12, 16 + 10) and texel (12, 10).
  const Float4 p = pos[1 * 5 + 2];
  if (!Near(p.x, 12) || !Near(p.y, 26) || !Near(p.w, 1)) return 42;
  if (!Near(p.z, 100.0f * ((12000 + 70) / 65535.0f), 1e-3f)) return 43;
  // First and last corners.
  if (!Near(pos[0].x, 8) || !Near(pos[0].y, 24) || !Near(pos[24].x, 16) || !Near(pos[24].y, 32)) return 44;
  // Several consecutive patches (82207C30 draws a run): patch 3 then 4 (row 2, col 0).
  t.patches = 2;
  if (!BuildTerrainPositions(t, tex.data(), tex.size(), 4, &pos) || pos.size() != 50) return 45;
  if (!Near(pos[25].x, 0) || !Near(pos[25].y, 32)) return 46;
  // A bad map fails.
  if (BuildTerrainPositions(t, tex.data(), 8, 4, &pos)) return 47;
  t.patches = 0;
  if (BuildTerrainPositions(t, tex.data(), tex.size(), 4, &pos)) return 48;

  // --- Grid indices ---
  std::vector<uint32_t> idx;
  BuildGridIndices(2, 2, &idx);
  // 2x2 quads per patch, 2 triangles each, 2 patches; patch 1 offset by 9 points.
  if (idx.size() != 2 * 4 * 6) return 50;
  const uint32_t first[6] = {0, 1, 4, 0, 4, 3};
  for (int i = 0; i < 6; ++i) {
    if (idx[i] != first[i]) return 51;
  }
  if (idx[24] != 9 || *std::max_element(idx.begin(), idx.end()) != 17) return 52;

  // --- Patch from the shader's registers (vs-transforms.json "terrain") ---
  {
    static float bank[256 * 4] = {};
    auto set = [](uint32_t reg, float x, float y, float z, float w) {
      bank[4 * reg] = x; bank[4 * reg + 1] = y; bank[4 * reg + 2] = z; bank[4 * reg + 3] = w;
    };
    // Gameplay values of a C30A97D946FA2BE4 draw ([vtess] GPU register file).
    set(11, 2, 0.5f, 0.25f, 0);
    set(46, 8, 8, 88.9755249f, 1);
    set(47, 1.0f / 288, 1.0f / 288, 0.5f, 0.25f);
    set(113, 96, 144, 0, 0);
    // C30A97D946FA2BE4: grid c11.xy, cell c46.xy, height c46.z, origin c113.xy,
    // tex offset c47.zw, tex scale c47.xy, no patch offset, heightmap tf16.
    const TerrainSpec c30a{11 * 4, 46 * 4, 46 * 4 + 2, 113 * 4, 47 * 4 + 2, 47 * 4, -1, 16};
    TerrainPatch tp;
    if (!MakeTerrainPatch(c30a, bank, kTf16, 3, 1, &tp)) return 60;
    if (!tp.active || tp.patch != 3 || tp.patches != 1 || tp.cols != 2 || tp.inv_cols != 0.5f) return 61;
    if (tp.cell[0] != 8 || tp.cell[1] != 8 || tp.height_scale != 88.9755249f) return 62;
    if (tp.origin[0] != 96 || tp.origin[1] != 144 || tp.tex_offset[0] != 0.5f || tp.tex_offset[1] != 0.25f) return 63;
    if (tp.tex_scale[0] != 1.0f / 288 || tp.map.width != 577) return 64;
    // 5003700B7C9B1C16 adds c113.x to the patch index (instr 5).
    set(113, 18, 0, 0, 0);
    const TerrainSpec s5003{11 * 4, 8 * 4, 8 * 4 + 2, 47 * 4, 46 * 4 + 2, 46 * 4, 113 * 4, 16};
    if (!MakeTerrainPatch(s5003, bank, kTf16, 0, 20, &tp) || tp.patch != 18 || tp.patches != 20) return 65;
    // An unsupported heightmap or no patches: no terrain.
    uint32_t bad[6];
    std::memcpy(bad, kTf16, sizeof(bad));
    bad[1] = (bad[1] & ~0x3Fu) | 30;
    if (MakeTerrainPatch(c30a, bank, bad, 0, 1, &tp) || tp.active) return 66;
    if (MakeTerrainPatch(c30a, bank, kTf16, 0, 0, &tp)) return 67;
    // A register past c255 (or a pair crossing a register) is rejected.
    const TerrainSpec past{255 * 4 + 3, 46 * 4, 46 * 4 + 2, 113 * 4, 47 * 4 + 2, 47 * 4, -1, 16};
    if (MakeTerrainPatch(past, bank, kTf16, 0, 1, &tp)) return 68;
  }

  // --- Texel index of non-finite coordinates (garbage constants) ---
  {
    volatile float inf = INFINITY;  // volatile: no constant folding
    volatile float nan = NAN;
    for (uint8_t clamp : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
      if (detail::TexelIndex(inf, 8, clamp) != 0) return 70 + clamp;
      if (detail::TexelIndex(-inf, 8, clamp) != 0) return 73 + clamp;
      if (detail::TexelIndex(nan, 8, clamp) != 0) return 76 + clamp;
    }
    // Finite coordinates are unchanged: wrap, mirror, clamp.
    if (detail::TexelIndex(1.25f, 8, 0) != 2 || detail::TexelIndex(-0.125f, 8, 0) != 7) return 79;
    if (detail::TexelIndex(1.25f, 8, 1) != 5 || detail::TexelIndex(2.0f, 8, 2) != 7) return 80;
  }

  // --- Terrain material inputs (frame-map section 13, "Terrain") ---
  // The synthetic layer draw of tests/test_terrain_replay.py (LAYER_CONSTS, PCONST): the same
  // constants, and the numbers pinned here are the ones pinned there (LAYER_WANT and
  // test_layer_inputs_and_values), so the replay tool and the capture's inputs agree on every
  // intermediate value: the three coordinate transforms, the weight of a texel value, the cull
  // test, the fade and the distance.
  {
    static const uint32_t kTf0[6] = {0x88000002, 0x17360052, 0x007FE3FF, 0x00A80D10, 0x000000C3, 0x173E0A18};
    static const uint32_t kTf3[6] = {0x86000002, 0x1A000052, 0x005FE2FF, 0x00A80D10, 0x00000000, 0x00000218};
    // tf18 of every layer draw of the bridge captures: 1650 x 1815 k_DXT3A, tiled.
    static const uint32_t kTf18[6] = {0x8D004802, 0x1BA0207A, 0x00E2C671, 0x01000D10, 0x00000000, 0x00000218};
    static float vs[1024], ps[1024];
    std::fill(std::begin(vs), std::end(vs), 0.0f);
    std::fill(std::begin(ps), std::end(ps), 0.0f);
    auto set = [](float* bank, int reg, float x, float y, float z, float w) {
      bank[4 * reg] = x; bank[4 * reg + 1] = y; bank[4 * reg + 2] = z; bank[4 * reg + 3] = w;
    };
    set(vs, 4, 1, 0, 0, 0);
    set(vs, 5, 0, 1, 0, 0);
    set(vs, 6, 0, 0, 1, 0);
    set(vs, 8, 0.0625f, 0.125f, 1, 0.125f);
    set(vs, 11, 2, 0.5f, 0.25f, 0);
    set(vs, 14, 1.0f / 384, 1.0f / 384, 1, 1);
    set(vs, 46, 8, 8, 90, 1);
    set(vs, 47, 1.0f / 384, 1.0f / 384, 16, 32);
    set(vs, 113, 80, 144, 0, 0);
    set(vs, 114, 1.0f / 255, 0, 0, 0);
    set(vs, 115, 16.0f / 66, 16.0f / 33, 33.5f / 66, 0.5f / 33);
    set(ps, 19, 100, 160, 30, 1);
    set(ps, 21, 1.5f, 1.0f, 0.5f, 2.0f);
    set(ps, 31, 0.0005f, -1.25f, 0, 0);
    const TerrainSpec layer_spec{11 * 4, 46 * 4, 46 * 4 + 2, 113 * 4, 47 * 4 + 2, 47 * 4, -1, 16};
    TerrainPatch tp;
    if (!MakeTerrainPatch(layer_spec, vs, kTf16, 3, 1, &tp)) return 100;
    TerrainFetches f;
    f.detail = kTf0;
    f.base = kTf3;
    f.weight = kTf18;
    TerrainMaterialInputs in;
    if (!MakeTerrainMaterialInputs(tp, false, vs, ps, f, &in) || !in.valid || in.adaptive) return 101;
    if (std::memcmp(in.detail_fetch, kTf0, 24) || std::memcmp(in.base_fetch, kTf3, 24) ||
        std::memcmp(in.weight_fetch, kTf18, 24)) return 102;
    if (!Near(in.detail_uv[0], 0.125f, 1e-6f) || !Near(in.detail_uv[1], -0.125f, 1e-6f) ||
        in.detail_uv[2] != 0.0f || in.detail_uv[3] != 0.0f) return 103;
    if (!Near(in.base_uv[0], 1.0f / 384, 1e-7f) || !Near(in.base_uv[1], 1.0f / 384, 1e-7f) ||
        !Near(in.base_uv[2], -16.0f / 384, 1e-6f) || !Near(in.base_uv[3], -32.0f / 384, 1e-6f)) return 104;
    if (!Near(in.weight_uv[0], 2.0f / 66, 1e-6f) || !Near(in.weight_uv[1], 2.0f / 33, 1e-6f) ||
        !Near(in.weight_uv[2], -126.5f / 66, 1e-5f) || !Near(in.weight_uv[3], -287.5f / 33, 1e-5f)) return 105;
    if (in.base_pow != 2.0f || in.base_tint[0] != 1.5f || in.base_tint[1] != 1.0f || in.base_tint[2] != 0.5f) return 106;
    if (!Near(in.weight_scale, 15.0f / 14, 1e-6f) || !Near(in.weight_bias, 1.0f / 15, 1e-7f)) return 107;
    if (in.fade[0] != 0.0005f || in.fade[1] != -1.25f || !Near(in.hole_ref, 1.0f / 255, 1e-8f)) return 108;
    if (in.eye[0] != 100.0f || in.eye[1] != 160.0f || in.eye[2] != 30.0f) return 109;
    const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    if (std::memcmp(in.world, identity, sizeof(identity))) return 110;
    // Sub-patch 3 is row 1, column 1: its corner (88, 152) is texel 16 of the second 33-texel
    // cell (49.5 of 66) and row 16 (16.5 of 33).
    float uv[2];
    TerrainUv(in.weight_uv, 88.0f, 152.0f, uv);
    if (!Near(uv[0], 0.75f, 1e-5f) || !Near(uv[1], 0.5f, 1e-5f)) return 111;
    TerrainUv(in.base_uv, 88.0f, 152.0f, uv);
    if (!Near(uv[0], 0.1875f, 1e-6f) || !Near(uv[1], 0.3125f, 1e-6f)) return 112;
    TerrainUv(in.detail_uv, 88.0f, 152.0f, uv);
    if (uv[0] != 11.0f || uv[1] != -19.0f) return 113;
    // The weight of the 4-bit value n is (n - 1) / 14; n = 0 is below the reference 1/255.
    if (!Near(TerrainLayerWeight(in, 8.0f / 15), 0.5f, 1e-6f) || !Near(TerrainLayerWeight(in, 1.0f), 1.0f, 1e-6f) ||
        !Near(TerrainLayerWeight(in, 1.0f / 15), 0.0f, 1e-6f)) return 114;
    if (!TerrainLayerCulled(in, 0.0f) || TerrainLayerCulled(in, 1.0f / 15)) return 115;
    // The game compares with greater-than (sgt): a value equal to the reference is drawn.
    if (TerrainLayerCulled(in, in.hole_ref)) return 140;
    // Fade 1 - sat(d2 * 0.0005 - 1.25).
    if (TerrainFade(in, 2500.0f) != 1.0f || !Near(TerrainFade(in, 3000.0f), 0.75f, 1e-5f) ||
        TerrainFade(in, 4500.0f) != 0.0f) return 116;
    if (!Near(TerrainEyeDistance2(in, 88.0f, 152.0f, 20.0f), 308.0f, 1e-3f)) return 117;
    // The reference of a base layer is -1: nothing is culled, and the value 0 has the weight -1/14.
    set(vs, 114, -1, 0, 0, 0);
    TerrainMaterialInputs base_layer;
    if (!MakeTerrainMaterialInputs(tp, false, vs, ps, f, &base_layer)) return 118;
    if (TerrainLayerCulled(base_layer, 0.0f) || !Near(TerrainLayerWeight(base_layer, 0.0f), -1.0f / 14, 1e-6f)) return 119;
    set(vs, 114, 1.0f / 255, 0, 0, 0);
    // A world matrix other than identity reaches the distance: c4 = (0, 1, 0, 5) puts y + 5 in x.
    set(vs, 4, 0, 1, 0, 5);
    TerrainMaterialInputs turned;
    if (!MakeTerrainMaterialInputs(tp, false, vs, ps, f, &turned)) return 120;
    // p = (152 + 5, 152, 20): d2 = 57^2 + 8^2 + 10^2.
    if (!Near(TerrainEyeDistance2(turned, 88.0f, 152.0f, 20.0f), 3249.0f + 64.0f + 100.0f, 1e-2f)) return 121;
    set(vs, 4, 1, 0, 0, 0);

    // The base map (tf3) and its constants are light, which the material does not read (the unlit
    // rule): they are kept when they can be read (base_valid) and never decide the material.
    if (!in.base_valid) return 170;
    TerrainFetches nb = f;
    nb.base = nullptr;  // no texture in the base map's slot
    TerrainMaterialInputs no_base;
    if (!MakeTerrainMaterialInputs(tp, false, vs, ps, nb, &no_base) || !no_base.valid || no_base.base_valid) return 171;
    if (no_base.base_fetch[0] != 0 || std::memcmp(no_base.detail_fetch, kTf0, 24) ||
        no_base.fade[0] != 0.0005f || !Near(no_base.weight_uv[0], 2.0f / 66, 1e-6f)) return 172;
    {
      volatile float not_finite = NAN;  // volatile: no constant folding
      uint32_t not_texture3[6];
      std::memcpy(not_texture3, kTf3, sizeof(not_texture3));
      not_texture3[0] &= ~3u;
      nb.base = not_texture3;
      if (!MakeTerrainMaterialInputs(tp, false, vs, ps, nb, &no_base) || !no_base.valid || no_base.base_valid) return 173;
      ps[21 * 4 + 1] = not_finite;  // ps c21.y, the tint
      TerrainMaterialInputs no_tint;
      if (!MakeTerrainMaterialInputs(tp, false, vs, ps, f, &no_tint) || !no_tint.valid || no_tint.base_valid) return 174;
      ps[21 * 4 + 1] = 1.0f;
      ps[21 * 4 + 3] = not_finite;  // ps c21.w, the exponent
      if (!MakeTerrainMaterialInputs(tp, false, vs, ps, f, &no_tint) || !no_tint.valid || no_tint.base_valid) return 175;
      ps[21 * 4 + 3] = 2.0f;
      vs[14 * 4] = not_finite;  // vs c14.x, the base map's scale
      if (!MakeTerrainMaterialInputs(tp, false, vs, ps, f, &no_tint) || !no_tint.valid || no_tint.base_valid) return 176;
      vs[14 * 4] = 1.0f / 384;
      // Everything the material does read still invalidates: the eye, the fade, the cull reference,
      // the detail scale, the weight cell, the matrix.
      TerrainMaterialInputs gone = in;
      const int ps_read[] = {19 * 4, 19 * 4 + 2, 31 * 4, 31 * 4 + 1};
      for (int at : ps_read) {
        const float keep = ps[at];
        ps[at] = not_finite;
        gone = in;
        if (MakeTerrainMaterialInputs(tp, false, vs, ps, f, &gone) || gone.valid) return 177;
        ps[at] = keep;
      }
      const int vs_read[] = {114 * 4, 8 * 4 + 1, 115 * 4, 115 * 4 + 3, 4 * 4, 6 * 4 + 3};
      for (int at : vs_read) {
        const float keep = vs[at];
        vs[at] = not_finite;
        gone = in;
        if (MakeTerrainMaterialInputs(tp, false, vs, ps, f, &gone) || gone.valid) return 178;
        vs[at] = keep;
      }
      if (!MakeTerrainMaterialInputs(tp, false, vs, ps, f, &gone) || !gone.valid || !gone.base_valid) return 179;
    }

    // Nothing is made from missing or garbage data: every failure leaves the inputs invalid.
    TerrainMaterialInputs bad = in;
    if (MakeTerrainMaterialInputs(tp, false, nullptr, ps, f, &bad) || bad.valid) return 122;
    bad = in;
    if (MakeTerrainMaterialInputs(tp, false, vs, nullptr, f, &bad) || bad.valid) return 123;
    TerrainFetches g = f;
    g.weight = nullptr;  // the weight fetch constant could not be read
    bad = in;
    if (MakeTerrainMaterialInputs(tp, false, vs, ps, g, &bad) || bad.valid) return 124;
    uint32_t not_texture[6];
    std::memcpy(not_texture, kTf0, sizeof(not_texture));
    not_texture[0] &= ~3u;  // not a texture fetch constant
    g = f;
    g.detail = not_texture;
    bad = in;
    if (MakeTerrainMaterialInputs(tp, false, vs, ps, g, &bad) || bad.valid) return 125;
    TerrainPatch inactive;
    bad = in;
    if (MakeTerrainMaterialInputs(inactive, false, vs, ps, f, &bad) || bad.valid) return 126;
    volatile float nan = NAN;  // volatile: no constant folding
    ps[31 * 4] = nan;
    bad = in;
    if (MakeTerrainMaterialInputs(tp, false, vs, ps, f, &bad) || bad.valid) return 127;
    ps[31 * 4] = 0.0005f;
    TerrainPatch flat = tp;
    flat.cell[0] = 0.0f;  // the weight coordinate divides by the cell
    bad = in;
    if (MakeTerrainMaterialInputs(flat, false, vs, ps, f, &bad) || bad.valid) return 128;

    // --- A draw of the adaptive builder (FAR_CONSTS / FAR_WANT of the Python test) ---
    static const uint32_t kTf13[6] = {0x88000002, 0x1A800052, 0x007FE3FF, 0x00A80D10, 0x00000000, 0x00000218};
    // tf18 / tf19 of the adaptive draws of the bridge captures: 816 x 48 k_16 tables, point sampled.
    static const uint32_t kTableU[6] = {0x07004802, 0x1B9D6058, 0x0005E32F, 0x01001400, 0x00000000, 0x00000218};
    static const uint32_t kTableV[6] = {0x07004802, 0x1B9EC058, 0x0005E32F, 0x01001400, 0x00000000, 0x00000218};
    set(vs, 8, 8, 8, 90, 1);
    set(vs, 11, 6, 1.0f / 6, 1.0f / 12, 0);
    set(vs, 46, 1.0f / 384, 1.0f / 384, 32, 64);
    set(vs, 47, 96, 144, 0, 0);
    set(vs, 72, 16.0f / 17, 0.5f / 17, 0, 0);
    set(vs, 113, 3, 0, 0, 0);
    set(vs, 114, 0.5f, 0.5f, 0, 0);
    set(vs, 115, 0, 0, 0, 0);
    const TerrainSpec far_spec{11 * 4, 8 * 4, 8 * 4 + 2, 47 * 4, 46 * 4 + 2, 46 * 4, 113 * 4, 16};
    TerrainPatch fp;
    if (!MakeTerrainPatch(far_spec, vs, kTf16, 0, 6, &fp)) return 130;
    TerrainFetches ff;
    ff.detail = kTf13;
    ff.base = kTf3;
    ff.far_u = kTableU;
    ff.far_v = kTableV;
    TerrainMaterialInputs fm;
    if (!MakeTerrainMaterialInputs(fp, true, vs, ps, ff, &fm) || !fm.valid || !fm.adaptive) return 131;
    if (std::memcmp(fm.detail_fetch, kTf13, 24) || std::memcmp(fm.far_u_fetch, kTableU, 24) ||
        std::memcmp(fm.far_v_fetch, kTableV, 24) || fm.weight_fetch[0] != 0) return 132;
    if (!Near(fm.base_uv[0], 1.0f / 384, 1e-7f) || !Near(fm.base_uv[2], -32.0f / 384, 1e-6f) ||
        !Near(fm.base_uv[3], -64.0f / 384, 1e-6f)) return 133;
    if (!Near(fm.far_table[0], 16.0f / 17, 1e-6f) || !Near(fm.far_table[1], 0.5f / 17, 1e-7f) ||
        fm.far_table[2] != 0.5f || fm.far_table[3] != 0.5f || fm.far_patch[0] != 0.0f || fm.far_patch[1] != 0.0f)
      return 134;
    if (fm.base_pow != 2.0f || fm.fade[0] != 0.0005f || fm.eye[2] != 30.0f) return 135;
    // Knot k of patch column 1, row 1 is texel 17 + k of row 1 of a 34 x 2 table.
    float knot[2];
    TerrainFarKnot(fm, 1.0f, 1.0f, 0, knot);
    if (uint32_t(knot[0] * 34) != 17 || uint32_t(knot[1] * 2) != 1) return 136;
    // The knot is the centre of its texel: (1 + 0.5 / 17) / 2 across, (1 + 0.5) / 2 down.
    if (!Near(knot[0], (1.0f + 0.5f / 17) / 2, 1e-6f) || !Near(knot[1], 0.75f, 1e-6f)) return 141;
    TerrainFarKnot(fm, 1.0f, 1.0f, 16, knot);
    if (uint32_t(knot[0] * 34) != 33 || uint32_t(knot[1] * 2) != 1) return 137;
    // The far kind does not need the base map either.
    {
      TerrainFetches fb = ff;
      fb.base = nullptr;
      TerrainMaterialInputs far_no_base;
      if (!MakeTerrainMaterialInputs(fp, true, vs, ps, fb, &far_no_base) || !far_no_base.valid ||
          far_no_base.base_valid || !fm.base_valid) return 180;
    }
    // The far kind needs both tables and no weight map.
    TerrainFetches fg = ff;
    fg.far_v = nullptr;
    bad = fm;
    if (MakeTerrainMaterialInputs(fp, true, vs, ps, fg, &bad) || bad.valid) return 138;
    // The distance of a far draw is to the patch position itself (its pixel shader reads no matrix).
    if (!Near(TerrainEyeDistance2(fm, 88.0f, 152.0f, 20.0f), 308.0f, 1e-3f)) return 139;

    // --- The far colour coordinate per grid point (test_far_patch of the Python test: the same two
    // tables, the same patch, the same numbers) ---
    // Tables 34 x 2, linear k_16 with big-endian words, pitch 64 texels: rows of 128 bytes. Patch
    // column 1, row 1 holds u 0.25 to 0.75 over its 17 knots and v 0.6 to 0.9; everything else is 0.
    static const uint32_t kSmallU[6] = {0x00800002, 0x1A000058, 0x00002021, 0x00000D10, 0x00000000, 0x00000200};
    static const uint32_t kSmallV[6] = {0x00800002, 0x1A100058, 0x00002021, 0x00000D10, 0x00000000, 0x00000200};
    HeightMap um, vm;
    if (!DecodeHeightMap(kSmallU, &um) || !DecodeHeightMap(kSmallV, &vm)) return 150;
    if (um.tiled || um.width != 34 || um.height != 2 || um.pitch != 64 || um.size != 128u + 68u) return 151;
    std::vector<uint8_t> tu(um.size, 0), tv(vm.size, 0);
    for (uint32_t x = 17; x < 34; ++x) {
      const uint32_t u16 = uint32_t(std::lround(65535.0 * (0.25 + 0.5 * (x - 17) / 16.0)));
      const uint32_t v16 = uint32_t(std::lround(65535.0 * (0.6 + 0.3 * (x - 17) / 16.0)));
      tu[128 + 2 * x] = uint8_t(u16 >> 8);
      tu[128 + 2 * x + 1] = uint8_t(u16);
      tv[128 + 2 * x] = uint8_t(v16 >> 8);
      tv[128 + 2 * x + 1] = uint8_t(v16);
    }
    float texel = 0;
    if (!HeightTexel(tu.data(), tu.size(), um, 21, 1, &texel) || !Near(texel, 0.375f, 1e-4f)) return 152;
    ff.far_u = kSmallU;
    ff.far_v = kSmallV;
    TerrainPatch one;  // patch 4 of the draw: with c113.x = 3 it is patch 7, column 1 of row 1, corner (104, 152)
    if (!MakeTerrainPatch(far_spec, vs, kTf16, 4, 1, &one)) return 153;
    if (!MakeTerrainMaterialInputs(one, true, vs, ps, ff, &one.material)) return 154;
    std::vector<Float2> uvs;
    if (!BuildTerrainFarUvs(one, um, tu.data(), tu.size(), vm, tv.data(), tv.size(), kTerrainGrid, &uvs)) return 155;
    if (uvs.size() != 17 * 17) return 156;
    // Grid point (i, j) reads the u table at knot i and the v table at knot j. The replay's point
    // (U 0.25, V 0.5) is (4, 8): u 0.375, v 0.75; (U 0.75, V 0.5) is (12, 8): u 0.625.
    if (!Near(uvs[8 * 17 + 4].u, 0.375f, 1e-4f) || !Near(uvs[8 * 17 + 4].v, 0.75f, 1e-4f)) return 157;
    if (!Near(uvs[8 * 17 + 12].u, 0.625f, 1e-4f) || !Near(uvs[8 * 17 + 12].v, 0.75f, 1e-4f)) return 158;
    // The corners: knots 0 and 16 of both tables.
    if (!Near(uvs[0].u, 0.25f, 1e-4f) || !Near(uvs[0].v, 0.6f, 1e-4f) || !Near(uvs[16 * 17 + 16].u, 0.75f, 1e-4f) ||
        !Near(uvs[16 * 17 + 16].v, 0.9f, 1e-4f)) return 159;
    // u changes along i only, v along j only (each table is read along its own axis).
    if (uvs[5 * 17 + 4].u != uvs[8 * 17 + 4].u || uvs[8 * 17 + 9].v != uvs[8 * 17 + 4].v) return 160;
    // Two patches in a run: the second (patch 8, column 2) lies past the tables' 34 texels, which
    // wrap (clamp mode 0) to column 0, where the tables hold 0.
    TerrainPatch two = one;
    two.patches = 2;
    if (!BuildTerrainFarUvs(two, um, tu.data(), tu.size(), vm, tv.data(), tv.size(), kTerrainGrid, &uvs) ||
        uvs.size() != 2 * 17 * 17 || uvs[289 + 8 * 17 + 4].u != 0.0f || !Near(uvs[8 * 17 + 4].u, 0.375f, 1e-4f)) return 161;
    // Nothing is made from missing data: a table cut short, a missing table, inputs that are not
    // valid or not those of the adaptive builder, another grid than the 17 knots.
    if (BuildTerrainFarUvs(one, um, tu.data(), 128 + 40, vm, tv.data(), tv.size(), kTerrainGrid, &uvs) || !uvs.empty()) return 162;
    if (BuildTerrainFarUvs(one, um, tu.data(), tu.size(), vm, nullptr, tv.size(), kTerrainGrid, &uvs) || !uvs.empty()) return 163;
    if (BuildTerrainFarUvs(one, um, tu.data(), tu.size(), vm, tv.data(), tv.size(), 8, &uvs)) return 164;
    TerrainPatch invalid = one;
    invalid.material.valid = false;
    if (BuildTerrainFarUvs(invalid, um, tu.data(), tu.size(), vm, tv.data(), tv.size(), kTerrainGrid, &uvs)) return 165;
    TerrainPatch not_far = one;
    not_far.material.adaptive = false;
    if (BuildTerrainFarUvs(not_far, um, tu.data(), tu.size(), vm, tv.data(), tv.size(), kTerrainGrid, &uvs)) return 166;
    // One bound for the patches of a record, for positions and coordinates alike.
    TerrainPatch many = one;
    many.patches = kMaxTerrainPatches + 1;
    if (kMaxTerrainPatches != 4096 ||
        BuildTerrainFarUvs(many, um, tu.data(), tu.size(), vm, tv.data(), tv.size(), kTerrainGrid, &uvs)) return 167;
    // (the 64 x 64 map of the position cases clamps, so every patch of a long run finds its heights)
    TerrainPatch run = t;
    run.patches = kMaxTerrainPatches;
    std::vector<Float4> grid;
    if (!BuildTerrainPositions(run, tex.data(), tex.size(), 4, &grid) || grid.size() != size_t(kMaxTerrainPatches) * 25) return 168;
    run.patches = kMaxTerrainPatches + 1;
    if (BuildTerrainPositions(run, tex.data(), tex.size(), 4, &grid) || !grid.empty()) return 169;
  }

  std::cout << "PASS: terrain patch\n";
  return 0;
}
