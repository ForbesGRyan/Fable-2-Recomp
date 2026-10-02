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
  fc[0] &= ~0x80000000u;  // linear (untiled): rows of 608 texels padded to 256 bytes (1280)
  if (!DecodeHeightMap(fc, &m) || m.tiled || m.size != 1280u * 576u + 577u * 2u) return 11;

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

  std::cout << "PASS: terrain patch\n";
  return 0;
}
