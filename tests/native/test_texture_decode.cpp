// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/texture_decode.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace fable2::native;
using namespace fable2::native::render;

struct FcArgs {
  uint32_t format = 18, w = 256, h = 128, pitch = 256, endian = 1, base = 0x1A000000, mip = 0x1A010000;
  uint32_t mip_max = 8, swizzle = 0x688, filter = 1, clamp_x = 0, clamp_y = 2, dim = 1, type = 2;
  bool tiled = true, packed = true;
};
static void MakeFc(const FcArgs& a, uint32_t fc[6]) {
  fc[0] = a.type | (a.clamp_x << 10) | (a.clamp_y << 13) | ((a.pitch >> 5) << 22) | (uint32_t(a.tiled) << 31);
  fc[1] = a.format | (a.endian << 6) | a.base;
  fc[2] = (a.w - 1) | ((a.h - 1) << 13);
  fc[3] = (a.swizzle << 1) | (a.filter << 19);
  fc[4] = (a.mip_max << 6);
  fc[5] = (a.dim << 9) | (uint32_t(a.packed) << 11) | a.mip;
}

int main() {
  uint32_t fc[6];
  TextureFetch t;
  // --- decode ---
  MakeFc({}, fc);
  if (DecodeTextureFetch(fc, &t) != FetchError::kNone) return 1;
  if (t.format != TexFormat::kDxt1 || t.width != 256 || t.height != 128 || t.pitch_texels != 256) return 2;
  if (t.base_phys != 0x1A000000 || t.mip_phys != 0x1A010000 || !t.tiled || t.endian != 1) return 3;
  if (t.mip_max != 8 || !t.packed_mips || t.clamp_x != 0 || t.clamp_y != 2 || !t.linear_filter) return 4;
  if (t.swizzle[0] != 0 || t.swizzle[1] != 1 || t.swizzle[2] != 2 || t.swizzle[3] != 3) return 5;
  FcArgs a;
  a.type = 0; MakeFc(a, fc);
  if (DecodeTextureFetch(fc, &t) != FetchError::kNotTexture) return 6;
  a = {}; a.dim = 3; MakeFc(a, fc);  // cube
  if (DecodeTextureFetch(fc, &t) != FetchError::kNot2D) return 7;
  a = {}; a.format = 7; MakeFc(a, fc);  // k_2_10_10_10: no decoder
  if (DecodeTextureFetch(fc, &t) != FetchError::kFormat) return 8;
  a = {}; a.pitch = 128; MakeFc(a, fc);  // pitch below width
  if (DecodeTextureFetch(fc, &t) != FetchError::kSize) return 9;
  a = {}; a.base = 0; MakeFc(a, fc);  // no base level
  if (DecodeTextureFetch(fc, &t) != FetchError::kSize) return 10;
  // The terrain heightmap (k_16) has no albedo decoder.
  const uint32_t tf16[6] = {0x84C04802, 0x1BD0C058, 0x00480240, 0x01001400, 0x00000000, 0x00000218};
  if (DecodeTextureFetch(tf16, &t) != FetchError::kFormat) return 11;

  // --- format info, levels, pitches ---
  if (FormatInfo(TexFormat::kDxt1).bytes_per_block != 8 || FormatInfo(TexFormat::kDxt45).bpb_log2 != 4) return 12;
  if (FormatInfo(TexFormat::k8888).block != 1 || FormatInfo(TexFormat::k8888).bytes_per_block != 4) return 13;
  a = {}; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (LevelCount(t) != 9) return 14;  // min(8, log2(256)) + 1
  a.mip = 0; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (LevelCount(t) != 1) return 15;  // no mip address: base only
  if (LevelExtent(256, 3) != 32 || LevelExtent(5, 4) != 1) return 16;
  if (UploadRowPitch(3, 8) != 256 || UploadRowPitch(64, 8) != 512) return 17;
  a = {}; a.tiled = false; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  const LevelLayout base = BaseLevelLinear(t);
  if (base.pitch_blocks != 64 || base.width_blocks != 64 || base.height_blocks != 32 || base.row_pitch_bytes != 512) return 18;
  // SDK rule (GetGuestTextureLayout): base row pitch = fetch pitch in blocks
  // aligned to 32 blocks, no 256-byte row alignment.
  a = {}; a.tiled = false; a.format = 6; a.w = 96; a.pitch = 96; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (BaseLevelLinear(t).row_pitch_bytes != 384) return 35;  // 96 texels * 4 bytes, not 512
  a = {}; a.tiled = false; a.format = 20; a.w = 32; a.pitch = 32; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (BaseLevelLinear(t).row_pitch_bytes != 512) return 36;  // 8 blocks -> 32 blocks * 16 bytes, not 256

  // --- endian swaps ---
  const uint8_t s[4] = {1, 2, 3, 4};
  uint8_t d[4];
  CopySwapped(d, s, 4, 0); if (std::memcmp(d, "\x01\x02\x03\x04", 4)) return 19;
  CopySwapped(d, s, 4, 1); if (std::memcmp(d, "\x02\x01\x04\x03", 4)) return 20;
  CopySwapped(d, s, 4, 2); if (std::memcmp(d, "\x04\x03\x02\x01", 4)) return 21;
  CopySwapped(d, s, 4, 3); if (std::memcmp(d, "\x03\x04\x01\x02", 4)) return 22;

  // --- linear 8888 untile: 4x2 texels, guest rows of 256 bytes (pitch 64 texels), 8in32 ---
  {
    std::vector<uint8_t> src(256 + 16, 0);
    for (uint32_t y = 0; y < 2; ++y)
      for (uint32_t x = 0; x < 4; ++x) {
        uint8_t* p = src.data() + y * 256 + x * 4;
        p[0] = 0xAA; p[1] = uint8_t(y); p[2] = uint8_t(x); p[3] = 0x55;  // big-endian dword
      }
    LevelLayout l;
    l.pitch_blocks = 64; l.width_blocks = 4; l.height_blocks = 2; l.row_pitch_bytes = 256;
    std::vector<uint8_t> dst(2 * 256, 0);
    if (!UntileLevel(src.data(), src.size(), l, FormatInfo(TexFormat::k8888), false, 2, dst.data(), 256)) return 23;
    const uint8_t* q = dst.data() + 1 * 256 + 3 * 4;  // texel (3, 1), swapped to little-endian
    if (q[0] != 0x55 || q[1] != 3 || q[2] != 1 || q[3] != 0xAA) return 24;
    if (UntileLevel(src.data(), 256 + 8, l, FormatInfo(TexFormat::k8888), false, 2, dst.data(), 256)) return 25;
  }
  // --- tiled 8888: 64x64 texels, pitch 64; each texel's bytes name its (x, y) ---
  {
    uint32_t extent = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) extent = std::max(extent, uint32_t(capture::TiledOffset2D(x, y, 64, 2)) + 4);
    std::vector<uint8_t> src(extent, 0);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        uint8_t* p = src.data() + capture::TiledOffset2D(x, y, 64, 2);
        p[0] = uint8_t(x); p[1] = uint8_t(y); p[2] = 7; p[3] = 9;
      }
    LevelLayout l;
    l.pitch_blocks = 64; l.width_blocks = 64; l.height_blocks = 64;
    std::vector<uint8_t> dst(64 * 256, 0);
    if (!UntileLevel(src.data(), src.size(), l, FormatInfo(TexFormat::k8888), true, 0, dst.data(), 256)) return 26;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        const uint8_t* q = dst.data() + y * 256 + x * 4;
        if (q[0] != x || q[1] != y) return 27;
      }
  }
  // --- tiled DXT1 packed-mip origin: a 4x4-block level read at block offset (16, 8) ---
  {
    uint32_t extent = 0;
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) extent = std::max(extent, uint32_t(capture::TiledOffset2D(x, y, 32, 3)) + 8);
    std::vector<uint8_t> src(extent, 0);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) {
        uint8_t* p = src.data() + capture::TiledOffset2D(x, y, 32, 3);
        p[0] = uint8_t(x); p[1] = uint8_t(y);  // 8in16 swaps these into d[1], d[0]
      }
    LevelLayout l;
    l.pitch_blocks = 32; l.width_blocks = 4; l.height_blocks = 4; l.x_blocks = 16; l.y_blocks = 8;
    std::vector<uint8_t> dst(4 * 256, 0);
    if (!UntileLevel(src.data(), src.size(), l, FormatInfo(TexFormat::kDxt1), true, 1, dst.data(), 256)) return 28;
    const uint8_t* q = dst.data() + 2 * 256 + 3 * 8;  // block (3, 2) of the level = guest (19, 10)
    if (q[1] != 19 || q[0] != 10) return 29;
  }

  // --- sampler choice and identity ---
  a = {}; a.filter = 0; a.clamp_x = 0; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (SamplerIndex(t) != 0) return 30;  // point, wrap
  a.filter = 1; a.clamp_x = 2; a.clamp_y = 2; MakeFc(a, fc); DecodeTextureFetch(fc, &t);
  if (SamplerIndex(t) != 3) return 31;  // linear, clamp
  if (!IsMirror(1) || IsMirror(2) || IsMirror(0)) return 32;
  // Identity ignores sampler-only fields (clamps, filters) but not the address or format.
  uint32_t f1[6], f2[6];
  a = {}; MakeFc(a, f1);
  a.clamp_x = 2; a.filter = 0; MakeFc(a, f2);
  if (TextureIdentity(f1) != TextureIdentity(f2)) return 33;
  a = {}; a.base = 0x1B000000; MakeFc(a, f2);
  if (TextureIdentity(f1) == TextureIdentity(f2)) return 34;
  // --- k_DXT3A (format 58): the explicit-alpha half of a DXT3 block, one 4-bit channel -> R8 ---
  // Layout from the SDK: ConvertTexelDXT3AToDXT3 (conversion.cpp:80-85) swaps the 8-byte block like any
  // other and the texture_load_dxt3a shader reads four 16-bit rows, texel i in nibble i, low first.
  {
    // The terrain's weight map: tf18 of the terrain vertex shaders, fetch dword 1 0x1BA0207A, 1650 x 1815, tiled.
    const uint32_t tf18[6] = {2u | (52u << 22) | (1u << 31), 0x1BA0207A, 1649u | (1814u << 13), 0x00000000, 0, 1u << 9};
    if (DecodeTextureFetch(tf18, &t) != FetchError::kNone) return 37;
    if (t.format != TexFormat::kDxt3a || t.xenos_format != 58 || t.width != 1650 || t.height != 1815) return 38;
    if (t.endian != 1 || t.base_phys != 0x1BA02000 || !t.tiled) return 39;
    if (FormatInfo(TexFormat::kDxt3a).block != 4 || FormatInfo(TexFormat::kDxt3a).bytes_per_block != 8 ||
        FormatInfo(TexFormat::kDxt3a).bpb_log2 != 3) return 40;

    std::vector<uint8_t> out(3, 0x77);
    // Block A rows (texels 0..3): 0 1 2 3 / 4 5 6 7 / 8 9 10 11 / 12 13 14 15. Little-endian row words 0x3210
    // 0x7654 0xBA98 0xFEDC; guest bytes under 8in16 (endian 1) are those with each byte pair swapped.
    // Block B rows: 15 15 0 0 / 1 2 3 4 / 10 11 12 13 / 0 15 0 15 (words 0x00FF 0x4321 0xDCBA 0xF0F0).
    const uint8_t guest_a[8] = {0x32, 0x10, 0x76, 0x54, 0xBA, 0x98, 0xFE, 0xDC};
    const uint8_t guest_b[8] = {0x00, 0xFF, 0x43, 0x21, 0xDC, 0xBA, 0xF0, 0xF0};
    std::vector<uint8_t> want(8 * 4);
    {
      const int rows_a[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
      const int rows_b[16] = {15, 15, 0, 0, 1, 2, 3, 4, 10, 11, 12, 13, 0, 15, 0, 15};
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
          want[y * 8 + x] = uint8_t(rows_a[y * 4 + x] * 17);
          want[y * 8 + 4 + x] = uint8_t(rows_b[y * 4 + x] * 17);
        }
    }
    // Linear, pitch 32 blocks (256-byte rows), blocks at 0 and 8.
    std::vector<uint8_t> lin(16);
    std::memcpy(lin.data(), guest_a, 8);
    std::memcpy(lin.data() + 8, guest_b, 8);
    if (!DecodeDxt3aLevel(lin.data(), lin.size(), 8, 4, 32, false, 1, &out)) return 41;
    if (out != want) return 42;
    // One byte short: refused, output empty (the pre-filled contents are cleared).
    out.assign(3, 0x77);
    if (DecodeDxt3aLevel(lin.data(), lin.size() - 1, 8, 4, 32, false, 1, &out) || !out.empty()) return 43;
    if (DecodeDxt3aLevel(nullptr, 16, 8, 4, 32, false, 1, &out) || !out.empty()) return 44;
    if (DecodeDxt3aLevel(lin.data(), lin.size(), 0, 4, 32, false, 1, &out) || !out.empty()) return 45;
    // Tiled: blocks (0,0) and (1,0) sit at 0 and 8 as well. Both endians the game uses (8in16 above; 8in32 and
    // none below, bytes re-swapped from the post-swap form).
    for (uint32_t endian : {0u, 1u, 2u, 3u}) {
      uint8_t post[16];
      CopySwapped(post, guest_a, 8, 1);  // guest 8in16 -> post-swap little-endian form
      CopySwapped(post + 8, guest_b, 8, 1);
      std::vector<uint8_t> g(16);
      CopySwapped(g.data(), post, 16, endian);  // the swaps are their own inverses
      if (!DecodeDxt3aLevel(g.data(), g.size(), 8, 4, 32, true, endian, &out) || out != want) return 46 + int(endian);
    }
  }
  // A 70 x 45 image (edge blocks cropped), blocks placed by the tiled / linear rule, must decode to the rule and to
  // the bytes tests/test_texture_thumb.py pins with the same rule (FNV-1a 64 below equals its DXT3A_FNV).
  {
    const uint32_t W = 70, H = 45, PITCH = 32, WB = 18, HB = 12;
    auto value = [](uint32_t x, uint32_t y) { return (x * 5 + y * 3 + ((x * y) >> 2)) & 15; };
    std::vector<uint8_t> want(W * H);
    for (uint32_t y = 0; y < H; ++y)
      for (uint32_t x = 0; x < W; ++x) want[y * W + x] = uint8_t(value(x, y) * 17);
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint8_t b : want) h = (h ^ b) * 0x100000001B3ull;
    if (h != 0x8382B24A4858EC9Eull) return 50;
    for (bool tiled : {false, true}) {
      for (uint32_t endian : {1u, 2u, 0u, 3u}) {
        uint32_t size = 0;
        for (uint32_t by = 0; by < HB; ++by)
          for (uint32_t bx = 0; bx < WB; ++bx) {
            const uint32_t o = tiled ? uint32_t(capture::TiledOffset2D(int(bx), int(by), PITCH, 3))
                                     : by * PITCH * 8 + bx * 8;
            size = std::max(size, o + 8);
          }
        std::vector<uint8_t> g(size, 0);
        for (uint32_t by = 0; by < HB; ++by)
          for (uint32_t bx = 0; bx < WB; ++bx) {
            uint8_t post[8];
            for (uint32_t r = 0; r < 4; ++r) {
              uint32_t row = 0;
              for (uint32_t i = 0; i < 4; ++i) row |= value(bx * 4 + i, by * 4 + r) << (4 * i);
              post[2 * r] = uint8_t(row & 255);
              post[2 * r + 1] = uint8_t(row >> 8);
            }
            const uint32_t o = tiled ? uint32_t(capture::TiledOffset2D(int(bx), int(by), PITCH, 3))
                                     : by * PITCH * 8 + bx * 8;
            CopySwapped(g.data() + o, post, 8, endian);
          }
        std::vector<uint8_t> out;
        if (!DecodeDxt3aLevel(g.data(), g.size(), W, H, PITCH, tiled, endian, &out)) return 51;
        if (out != want) return 52;
        // The decoder must not read past the source: any shorter source is refused (the last block is needed).
        if (DecodeDxt3aLevel(g.data(), g.size() - 1, W, H, PITCH, tiled, endian, &out) || !out.empty()) return 53;
        // An unaligned pitch behaves as its 32-block alignment (both the SDK layout and TiledOffset2D align).
        if (!DecodeDxt3aLevel(g.data(), g.size(), W, H, 20, tiled, endian, &out) || out != want) return 54;
      }
    }
  }
  // --- BaseLevelDumpBytes: how much of a base level a discovery dump has to hold ---
  // The fetch constants are the terrain rows' own (native_discovery_20261006_211654.jsonl); the
  // expected sizes are counted by hand from the tile layout, not by the function's own code.
  {
    // tf16, the near heightmap: 769 x 769 k_16, tiled, pitch 800. 25 tiles of 32 x 32 texels per
    // row, 2048 bytes each; the last texel (768, 768) is in tile (24, 24), whose origin is
    // (24 * 25 + 24) * 2048 = 1,277,952; a 2-byte format extends 0xC00 past that origin.
    const uint32_t tf16[6] = {0x86404802, 0x1B770058, 0x00600300, 0x01001400, 0x00000000, 0x00000218};
    if (BaseLevelDumpBytes(tf16) != 1277952u + 0xC00u) return 70;
    // tf17, its normal map: 769 x 769 k_DXN (4 x 4 blocks of 16 bytes), tiled, pitch 896 texels =
    // 224 blocks: 7 tiles of 32 x 32 blocks per row, 16384 bytes each; the last block (192, 192)
    // is in tile (6, 6): (6 * 7 + 6) * 16384 = 786,432, plus 0x400 << 4.
    const uint32_t tf17[6] = {0x87004802, 0x1B8AA071, 0x00600300, 0x01000D10, 0x00000000, 0x00000218};
    if (BaseLevelDumpBytes(tf17) != 786432u + 0x4000u) return 71;
    // The far tables (tf18 / tf19 of the adaptive draws): 816 x 48 k_16, linear, pitch 896: rows
    // of 1792 bytes, 47 full rows and 816 texels of the last.
    const uint32_t table[6] = {0x07004802, 0x1B9D6058, 0x0005E32F, 0x01001400, 0x00000000, 0x00000218};
    if (BaseLevelDumpBytes(table) != 47u * 1792u + 816u * 2u || BaseLevelDumpBytes(table) != 85856u) return 72;
    // tf18 of the layer draws, the weight atlas: 1650 x 1815 k_DXT3A, tiled, pitch 1664 texels =
    // 416 blocks: 13 tiles per row of 8192 bytes; the last block (412, 453) is in tile (12, 14):
    // (14 * 13 + 12) * 8192 = 1,589,248, plus 0x400 << 3. The same number the SDK's bound gives
    // for a format the renderer decodes.
    const uint32_t tf18[6] = {0x8D004802, 0x1BA0207A, 0x00E2C671, 0x01000D10, 0x00000000, 0x00000218};
    if (BaseLevelDumpBytes(tf18) != 1589248u + 0x2000u) return 73;
    // Linear k_DXN and tiled k_16 of other shapes: 70 x 45 texels of DXN, linear, pitch 96 texels
    // = 24 blocks aligned to 32: rows of 512 bytes, 12 block rows, 18 blocks in the last.
    FcArgs a;
    a.format = 49; a.w = 70; a.h = 45; a.pitch = 96; a.tiled = false;
    uint32_t fc[6];
    MakeFc(a, fc);
    if (BaseLevelDumpBytes(fc) != 11u * 512u + 18u * 16u) return 74;
    // 40 x 20 k_16, tiled, pitch 64: one row of two tiles of 2048 bytes. Two-byte formats
    // interleave a pair of tiles in one 64-texel portion (the odd tile starts 0x400 in), so the
    // level spans exactly the two tiles: 4096 bytes.
    a.format = 24; a.w = 40; a.h = 20; a.pitch = 64; a.tiled = true;
    MakeFc(a, fc);
    if (BaseLevelDumpBytes(fc) != 4096u) return 75;
    // The same k_16 linear: rows of 128 bytes.
    a.tiled = false;
    MakeFc(a, fc);
    if (BaseLevelDumpBytes(fc) != 19u * 128u + 40u * 2u) return 76;
    // Not dumped in full: a format without a layout here (k_8, 2), a fetch constant that is not a
    // 2D texture, no base address, a pitch below the width.
    a.format = 2;
    MakeFc(a, fc);
    if (BaseLevelDumpBytes(fc) != 0) return 77;
    a.format = 24; a.type = 3;
    MakeFc(a, fc);
    if (BaseLevelDumpBytes(fc) != 0) return 78;
    a.type = 2; a.dim = 2;
    MakeFc(a, fc);
    if (BaseLevelDumpBytes(fc) != 0) return 79;
    a.dim = 1; a.base = 0;
    MakeFc(a, fc);
    if (BaseLevelDumpBytes(fc) != 0) return 80;
    a.base = 0x1A000000; a.pitch = 32;  // 40 texels wide
    MakeFc(a, fc);
    if (BaseLevelDumpBytes(fc) != 0) return 81;
    // The decoded formats keep their layout: DXT1 256 x 128 linear, pitch 256 = 64 blocks.
    FcArgs d;
    d.tiled = false;
    MakeFc(d, fc);
    if (BaseLevelDumpBytes(fc) != 31u * 64u * 8u + 64u * 8u) return 82;
  }
  // --- Fog discovery: 1D tables, and the targets a dump writes in full and keeps by content ---
  // The fetch constants are the game's own (native_discovery_20261009_065904.jsonl and the oracle
  // capture native_discovery_20261009_034735.jsonl); the sizes are counted by hand.
  {
    // tf4, the in-scatter table: 1D, 64 texels of k_16_16_16_16_EXPAND (8 bytes each).
    const uint32_t tf4[6] = {0x00804956, 0x1FAB105D, 0x0000003F, 0x00280D11, 0x00000003, 0x00000018};
    if (!Is1DFetch(tf4) || Fetch1DWidth(tf4) != 64 || Fetch1DDumpBytes(tf4) != 512) return 120;
    if (BaseLevelDumpBytes(tf4) != 0 || !DumpKeyedByContent(tf4)) return 121;
    // tf5, the transmittance table: 1D, 256 texels of k_8_8_8_8.
    const uint32_t tf5[6] = {0x02004802, 0x1FAB0086, 0x000000FF, 0x00280C14, 0x00000003, 0x00000018};
    if (!Is1DFetch(tf5) || Fetch1DWidth(tf5) != 256 || Fetch1DDumpBytes(tf5) != 1024) return 122;
    // A 1D k_32_FLOAT table of 512 texels (the composite's luminance table has this shape).
    uint32_t t1[6] = {0x04000002, 0x1FAA0000u | 36u | (2u << 6), 511, 0, 0, 0};
    if (!Is1DFetch(t1) || Fetch1DDumpBytes(t1) != 2048 || !DumpKeyedByContent(t1)) return 123;
    // The width of a 1D texture is 24 bits wide (xe_gpu_texture_fetch_t size_1d), not the 2D field's 13.
    t1[1] = 0x1FAA0000u | 2u;  // k_8
    t1[2] = 0x4000 - 1;
    if (Fetch1DWidth(t1) != 0x4000 || Fetch1DDumpBytes(t1) != 0x4000) return 124;
    // No size: a compressed format (no texel size), no base address, not a texture, a 2D texture.
    t1[1] = 0x1FAA0000u | 18u;
    if (Fetch1DDumpBytes(t1) != 0 || !Is1DFetch(t1) || !DumpKeyedByContent(t1)) return 125;
    t1[1] = 36u;
    if (Fetch1DDumpBytes(t1) != 0) return 126;
    t1[1] = 0x1FAA0000u | 36u;
    t1[0] = 0x04000003;
    if (Is1DFetch(t1) || Fetch1DDumpBytes(t1) != 0 || DumpKeyedByContent(t1)) return 127;
    FcArgs a;
    a.format = 6;
    uint32_t fc2[6];
    MakeFc(a, fc2);
    if (Is1DFetch(fc2) || Fetch1DDumpBytes(fc2) != 0 || DumpKeyedByContent(fc2)) return 128;
    // Texel sizes of the uncompressed formats the tables and targets use; 0 for a block format.
    if (TexelBytes(6) != 4 || TexelBytes(29) != 8 || TexelBytes(36) != 4 || TexelBytes(2) != 1 ||
        TexelBytes(24) != 2 || TexelBytes(23) != 4 || TexelBytes(38) != 16 || TexelBytes(18) != 0 ||
        TexelBytes(49) != 0 || TexelBytes(63) != 0) {
      return 129;
    }
    // The scene colour the post chain reads: 1120 x 720 k_16_16_16_16_EXPAND, tiled, pitch 1120 =
    // 35 tiles of 32 x 32 texels a row, 8192 bytes each; the last texel (1119, 719) is in tile
    // (34, 22): (22 * 35 + 34) * 8192 = 6,586,368, plus 0x400 << 3. Below the dump's 8 MB cap.
    const uint32_t scene[6] = {0x88C24956, 0x199E005D, 0x0059E45F, 0x01000D11, 0x00000000, 0x00000218};
    if (BaseLevelDumpBytes(scene) != 6586368u + 8192u || BaseLevelDumpBytes(scene) > (8u << 20)) return 130;
    if (Is1DFetch(scene) || !DumpKeyedByContent(scene)) return 131;
    // The scene depth of the same pass: 1120 x 720 k_24_8_FLOAT, tiled: tiles of 4096 bytes.
    a = FcArgs{};
    a.format = 23; a.w = 1120; a.h = 720; a.pitch = 1120;
    MakeFc(a, fc2);
    if (BaseLevelDumpBytes(fc2) != (22u * 35u + 34u) * 4096u + 0x1000u || !DumpKeyedByContent(fc2)) return 132;
    a.format = 22;
    MakeFc(a, fc2);
    if (BaseLevelDumpBytes(fc2) != (22u * 35u + 34u) * 4096u + 0x1000u || !DumpKeyedByContent(fc2)) return 133;
    // An ordinary texture is kept once per address and identity, as before.
    a = FcArgs{};
    MakeFc(a, fc2);
    if (DumpKeyedByContent(fc2)) return 134;
    a.format = 24;
    MakeFc(a, fc2);
    if (DumpKeyedByContent(fc2)) return 135;
  }
  // --- Post discovery (frame-map section 17): the post chain's own buffers are kept by content ---
  // The fetch constants are the game's own (native_discovery_20261009_092959.jsonl, frame 1: the
  // composite's row and the front buffer's); the sizes are counted by hand.
  {
    // The front buffer and the block the small post buffers live in; nothing beside them.
    if (!PostBufferBase(0x1ECFF000) || !PostBufferBase(0x12A00000) || !PostBufferBase(0x12D9F000) ||
        !PostBufferBase(0x12DFF000)) return 140;
    if (PostBufferBase(0x12E00000) || PostBufferBase(0x129FF000) || PostBufferBase(0x12704000) ||
        PostBufferBase(0x1ECFE000) || PostBufferBase(0x1ED00000) || PostBufferBase(0) || PostBufferBase(0x199E0000)) return 141;
    // The blurred bloom the composite samples (tf2): 280 x 180 k_2_10_10_10_AS_16_16_16_16 (54), tiled,
    // pitch 288 = 9 tiles a row of 4096 bytes; the last texel (279, 179) is in tile (8, 5).
    const uint32_t bloom[6] = {0x82424802, 0x12D9F0B6, 0x00166117, 0x01280C14, 0x00000003, 0x00000218};
    if (!DumpKeyedByContent(bloom) || Is1DFetch(bloom)) return 142;
    if (BaseLevelDumpBytes(bloom) != (5u * 9u + 8u) * 4096u + 0x1000u) return 143;
    // The same buffer as a k_2_10_10_10 texture (7: what the blurs sample): the same bytes.
    uint32_t b7[6] = {0x82424802, 0x12D9F000u | 7u | (2u << 6), 0x00166117, 0x01280C14, 0x00000003, 0x00000218};
    if (!DumpKeyedByContent(b7) || BaseLevelDumpBytes(b7) != (5u * 9u + 8u) * 4096u + 0x1000u) return 144;
    // The front buffer pass F samples: 1280 x 720 k_8_8_8_8 (a decoded format: kept by content for its address).
    FcArgs a;
    a.format = 6; a.w = 1280; a.h = 720; a.pitch = 1280; a.base = 0x1ECFF000; a.mip = 0; a.tiled = true;
    uint32_t fc2[6];
    MakeFc(a, fc2);
    if (!DumpKeyedByContent(fc2)) return 145;
    // The same texture anywhere else is kept once per address, and so are 7 and 54 outside the block.
    a.base = 0x1A000000;
    MakeFc(a, fc2);
    if (DumpKeyedByContent(fc2)) return 146;
    b7[1] = 0x1A000000u | 54u | (2u << 6);
    if (DumpKeyedByContent(b7) || BaseLevelDumpBytes(b7) != (5u * 9u + 8u) * 4096u + 0x1000u) return 147;
    // A cube, and a constant that is no texture: never by content, whatever the address.
    a = FcArgs{};
    a.base = 0x12D00000; a.dim = 3;
    MakeFc(a, fc2);
    if (DumpKeyedByContent(fc2)) return 148;
    a.dim = 1; a.type = 0;
    MakeFc(a, fc2);
    if (DumpKeyedByContent(fc2)) return 149;
  }
  // --- Format 3 (k_1_5_5_5): the impostors' pictures in a discovery dump (plan Task 1) ---
  {
    TexFormatInfo fi;
    if (!DumpFormatInfo(3, &fi) || fi.block != 1 || fi.bytes_per_block != 2 || fi.bpb_log2 != 1) return 150;
    if (TexFormatFromXenos(3) != TexFormat::kUnsupported) return 151;   // spec ruling 14: no record's material changes
    FcArgs a;
    a.format = 3; a.w = 256; a.h = 256; a.pitch = 256; a.endian = 1; a.base = 0x12704000; a.mip = 0x12724000;
    a.mip_max = 3; a.packed = false; a.clamp_x = 6; a.clamp_y = 6;
    uint32_t p[6];
    MakeFc(a, p);
    if (BaseLevelDumpBytes(p) != 131072u) return 153;                   // whole: it was cut at 65,536 before
    if (!DumpKeyedByContent(p)) return 154;                             // its memory holds another picture each frame
    TextureFetch t;
    if (DecodeTextureFetch(p, &t) != FetchError::kFormat) return 155;   // the texture cache still refuses it
    TexFormatInfo dfi;
    if (!DumpFetch(p, &t, &dfi) || t.width != 256 || t.height != 256 || t.base_phys != 0x12704000u ||
        t.mip_phys != 0x12724000u || !t.tiled || t.endian != 1 || t.mip_max != 3 ||
        t.format != TexFormat::kUnsupported || dfi.bytes_per_block != 2) return 156;
    a.format = 4;                                                       // k_5_6_5: no layout, as before
    MakeFc(a, p);
    if (DumpFetch(p, &t, &dfi) || BaseLevelDumpBytes(p) != 0 || DumpKeyedByContent(p)) return 157;
    a.format = 3; a.dim = 0;                                            // a 1D constant: no 2D layout
    MakeFc(a, p);
    if (DumpFetch(p, &t, &dfi)) return 158;
    // DumpFetch reads a constant as DecodeTextureFetch does: every field equal for a format both take, and
    // the same refusals (no texture, no base, a pitch below the width), with `out` untouched.
    FcArgs b;
    uint32_t q[6];
    MakeFc(b, q);
    TextureFetch d1, d2;
    if (DecodeTextureFetch(q, &d1) != FetchError::kNone || !DumpFetch(q, &d2, &dfi) || dfi.bytes_per_block != 8) return 159;
    if (d1.base_phys != d2.base_phys || d1.mip_phys != d2.mip_phys || d1.width != d2.width || d1.height != d2.height ||
        d1.pitch_texels != d2.pitch_texels || d1.xenos_format != d2.xenos_format || d1.format != d2.format ||
        d1.endian != d2.endian || d1.tiled != d2.tiled || d1.packed_mips != d2.packed_mips || d1.mip_min != d2.mip_min ||
        d1.mip_max != d2.mip_max || std::memcmp(d1.swizzle, d2.swizzle, 4) != 0 || d1.clamp_x != d2.clamp_x ||
        d1.clamp_y != d2.clamp_y || d1.linear_filter != d2.linear_filter) return 159;
    TextureFetch keep;
    keep.width = 77;
    b = FcArgs{}; b.format = 3; b.type = 0; MakeFc(b, q);
    if (DumpFetch(q, &keep, &dfi) || keep.width != 77) return 159;
    b = FcArgs{}; b.format = 3; b.base = 0; MakeFc(b, q);
    if (DumpFetch(q, &keep, &dfi) || keep.width != 77) return 159;
    b = FcArgs{}; b.format = 3; b.pitch = 128; MakeFc(b, q);
    if (DumpFetch(q, &keep, &dfi) || keep.width != 77) return 159;
  }
  // --- Decode1555Level (plan Task 7): 16-bit texels, tiled, 8-in-16, alpha in bit 15 ---
  {
    std::vector<uint8_t> src(131072, 0);
    auto put = [&](uint32_t offset, uint16_t word) { src[offset] = uint8_t(word >> 8); src[offset + 1] = uint8_t(word); };   // guest order: high byte first
    put(0, 0x8000 | 31);                    // (0, 0): alpha, the low field full
    put(2, 31 << 5);                        // (1, 0): the middle field, no alpha
    put(16, 0x8000 | (31 << 10));           // (0, 1): the high field
    put(64, 0x8000 | (16 << 5) | 1);        // (8, 0): rounding: 16 -> 132, 1 -> 8
    put(130942, 0xFFFF);                    // (255, 255)
    std::vector<uint8_t> out;
    if (!Decode1555Level(src.data(), src.size(), 256, 256, 256, true, 1, &out) || out.size() != 256u * 256u * 4u) return 160;
    auto px = [&](uint32_t x, uint32_t y) { return &out[(size_t(y) * 256 + x) * 4]; };
    const uint32_t lo = k1555LowFieldChannel, hi = 2 - k1555LowFieldChannel;
    if (lo != 0 && lo != 2) return 161;
    if (px(0, 0)[lo] != 255 || px(0, 0)[1] != 0 || px(0, 0)[hi] != 0 || px(0, 0)[3] != 255) return 162;
    if (px(1, 0)[lo] != 0 || px(1, 0)[1] != 255 || px(1, 0)[hi] != 0 || px(1, 0)[3] != 0) return 163;
    if (px(0, 1)[lo] != 0 || px(0, 1)[1] != 0 || px(0, 1)[hi] != 255 || px(0, 1)[3] != 255) return 164;
    if (px(8, 0)[lo] != 8 || px(8, 0)[1] != 132 || px(8, 0)[hi] != 0) return 165;
    if (px(255, 255)[0] != 255 || px(255, 255)[1] != 255 || px(255, 255)[2] != 255 || px(255, 255)[3] != 255) return 166;
    if (px(2, 0)[0] != 0 || px(2, 0)[3] != 0) return 167;                               // an untouched texel
    if (Decode1555Level(src.data(), 130943, 256, 256, 256, true, 1, &out) || !out.empty()) return 168;   // one byte short: nothing
    if (Decode1555Level(nullptr, 131072, 256, 256, 256, true, 1, &out) || Decode1555Level(src.data(), src.size(), 0, 256, 256, true, 1, &out)) return 169;
    // linear, as stored (endian 0: the low byte first): texel (x, y) at y * 512 + 2 * x for a pitch of 256
    std::vector<uint8_t> lin(4 * 512, 0);
    lin[512 * 1 + 2 * 3] = 31;              // (3, 1): the low field
    lin[512 * 1 + 2 * 3 + 1] = 0x80;        // and alpha
    if (!Decode1555Level(lin.data(), lin.size(), 4, 4, 256, false, 0, &out) || out.size() != 64) return 170;
    if (out[(1 * 4 + 3) * 4 + lo] != 255 || out[(1 * 4 + 3) * 4 + 3] != 255 || out[0] != 0) return 171;
    // The field order is the SDK's (frame-map section 19, "The pictures' format": its load shader moves bits 0 to
    // 4 of the swapped word into the host format's red): the low field is channel 0.
    if (k1555LowFieldChannel != 0) return 172;
    // Only endian 1 exchanges the two bytes of a texel, as in that shader: 2 and 3 (32-bit swaps) read it as stored.
    if (!Decode1555Level(lin.data(), lin.size(), 4, 4, 256, false, 2, &out) || out[(1 * 4 + 3) * 4 + lo] != 255 || out[(1 * 4 + 3) * 4 + 3] != 255) return 173;
    if (!Decode1555Level(lin.data(), lin.size(), 4, 4, 256, false, 1, &out) || out[(1 * 4 + 3) * 4 + lo] != 0 || out[(1 * 4 + 3) * 4 + 3] != 0 ||
        out[(1 * 4 + 3) * 4 + 1] != 230 || out[(1 * 4 + 3) * 4 + hi] != 58) return 174;   // the word 0x1F80: no alpha, low 0, middle 28, high 7
    // Fail closed: a pitch below the width (no fetch constant the decoders take has one), an image beyond the
    // 8,192 texels a fetch constant can name, a missing output; the last texel of a linear level one byte short.
    out.assign(3, 9);
    if (Decode1555Level(src.data(), src.size(), 256, 256, 128, true, 1, &out) || !out.empty()) return 175;
    // (a size no source of this length can hold is refused before the image is allocated: review of phase 12a, Minor 5)
    if (Decode1555Level(src.data(), src.size(), 8192, 8192, 8192, true, 1, &out) || !out.empty()) return 175;
    if (Decode1555Level(src.data(), src.size(), 8193, 1, 8224, true, 1, &out) || Decode1555Level(src.data(), src.size(), 256, 256, 256, true, 1, nullptr)) return 176;
    if (!Decode1555Level(lin.data(), 3 * 512 + 8, 4, 4, 256, false, 0, &out) || Decode1555Level(lin.data(), 3 * 512 + 7, 4, 4, 256, false, 0, &out) || !out.empty()) return 177;
  }
  // --- DumpWrittenAtRow: an impostor's picture is written at the row of the draw that shows it, nowhere else ---
  {
    FcArgs a;
    a.format = 3; a.w = 256; a.h = 256; a.pitch = 256; a.endian = 1; a.base = 0x12704000; a.mip = 0x12724000; a.mip_max = 3;
    uint32_t p[6];
    MakeFc(a, p);
    if (DumpWrittenAtRow(p, false) || !DumpWrittenAtRow(p, true)) return 190;   // the copy passes that make its levels sample it first
    a.format = 29;                                                              // every other texture: at any row, as before
    MakeFc(a, p);
    if (!DumpWrittenAtRow(p, false) || !DumpWrittenAtRow(p, true)) return 191;
    a.format = 3; a.dim = 0;                                                    // a 1D table of format 3 is no picture
    MakeFc(a, p);
    if (!DumpWrittenAtRow(p, false)) return 192;
    a = FcArgs{}; a.format = 3; a.type = 0;                                     // no texture: DumpTexture's own rules refuse it
    MakeFc(a, p);
    if (!DumpWrittenAtRow(p, false)) return 193;
  }
  // --- The content-kept files' budgets (review of the whole branch, Minor 7; ledger ruling 54): the impostors'
  // pictures, fourteen a frame, have a count and a byte budget of their own, so that a long capture's pictures
  // cannot use up the files of the 1D fog tables or the bytes of the game's targets ---
  {
    FcArgs a;
    a.format = 3; a.w = 256; a.h = 256; a.pitch = 256; a.endian = 1; a.base = 0x12704000; a.mip = 0x12724000; a.mip_max = 3;
    uint32_t picture[6], table[6], target[6];
    MakeFc(a, picture);
    a = FcArgs{}; a.format = 26; a.dim = 0; a.w = 256; a.h = 1; a.base = 0x1A001000;
    MakeFc(a, table);
    a = FcArgs{}; a.format = 29; a.w = 1280; a.h = 720; a.pitch = 1280; a.base = 0x1B000000;
    MakeFc(a, target);
    if (ContentDumpKindOf(picture) != ContentDumpKind::kPicture || ContentDumpKindOf(table) != ContentDumpKind::kTable ||
        ContentDumpKindOf(target) != ContentDumpKind::kTarget) return 194;
    a = FcArgs{}; a.format = 3; a.dim = 0; a.w = 256; a.h = 1; a.base = 0x1A001000;      // a 1D table of format 3 is a table
    MakeFc(a, table);
    if (ContentDumpKindOf(table) != ContentDumpKind::kTable || !Is1DFetch(table)) return 194;
    static_assert(kContentDumpLimits.files == 1024 && kContentDumpLimits.bytes == (512ull << 20));   // the budgets of before, for tables and targets
    static_assert(kContentDumpLimits.picture_files == 448 && kContentDumpLimits.picture_bytes == 448ull * 131072ull);   // 32 frames of fourteen
    const ContentDumpLimits& lim = kContentDumpLimits;
    // A long armed capture: every frame fourteen pictures, two fog tables and a scene colour target, each asked
    // and counted as DumpTexture does. The tables are written in every one of 300 frames (before: none after the
    // 68th frame, 1,024 / 15), the pictures stop at their own count, and the targets at their own bytes.
    ContentDumpUse use;
    uint32_t tables = 0, pictures = 0, targets = 0, first_frame_without_table = 0, first_frame_without_picture = 0;
    const uint64_t target_bytes = 1280ull * 720ull * 8ull;
    for (uint32_t frame = 1; frame <= 300; ++frame) {
      uint32_t frame_tables = 0, frame_pictures = 0;
      for (int i = 0; i < 14; ++i) {
        if (!ContentDumpHasRoom(ContentDumpKind::kPicture, 131072, use, lim)) continue;
        use = ContentDumpCounted(ContentDumpKind::kPicture, 131072, use);
        ++pictures;
        ++frame_pictures;
      }
      for (int i = 0; i < 2; ++i) {
        if (!ContentDumpHasRoom(ContentDumpKind::kTable, 1536, use, lim)) continue;
        use = ContentDumpCounted(ContentDumpKind::kTable, 1536, use);
        ++tables;
        ++frame_tables;
      }
      if (ContentDumpHasRoom(ContentDumpKind::kTarget, target_bytes, use, lim)) {
        use = ContentDumpCounted(ContentDumpKind::kTarget, target_bytes, use);
        ++targets;
      }
      if (frame_tables != 2 && !first_frame_without_table) first_frame_without_table = frame;
      if (frame_pictures != 14 && !first_frame_without_picture) first_frame_without_picture = frame;
    }
    if (tables != 600 || first_frame_without_table != 0) return 195;                       // every frame's tables
    if (pictures != 448 || first_frame_without_picture != 33) return 196;                  // 32 whole frames of pictures
    if (targets != 72 || uint64_t(targets) * target_bytes > lim.bytes || uint64_t(targets + 1) * target_bytes <= lim.bytes) return 197;   // the targets' bytes alone
    if (use.files != 672 || use.picture_files != 448 || use.picture_bytes != 448ull * 131072ull || use.bytes != 72ull * target_bytes) return 198;
    // Each budget's edges; a table counts against no byte budget; no sum wraps.
    ContentDumpUse u;
    u.files = lim.files - 1;
    if (!ContentDumpHasRoom(ContentDumpKind::kTable, 1536, u, lim) || !ContentDumpHasRoom(ContentDumpKind::kTarget, 4096, u, lim)) return 199;
    u.files = lim.files;
    if (ContentDumpHasRoom(ContentDumpKind::kTable, 1536, u, lim) || ContentDumpHasRoom(ContentDumpKind::kTarget, 4096, u, lim)) return 199;
    if (!ContentDumpHasRoom(ContentDumpKind::kPicture, 131072, u, lim)) return 200;       // the tables' and targets' files full: the pictures go on
    u = ContentDumpUse{};
    u.picture_files = lim.picture_files;
    if (ContentDumpHasRoom(ContentDumpKind::kPicture, 131072, u, lim) || !ContentDumpHasRoom(ContentDumpKind::kTable, 1536, u, lim) ||
        !ContentDumpHasRoom(ContentDumpKind::kTarget, 4096, u, lim)) return 201;          // the pictures full: the others go on
    u = ContentDumpUse{};
    u.picture_bytes = lim.picture_bytes - 131071;
    if (ContentDumpHasRoom(ContentDumpKind::kPicture, 131072, u, lim)) return 202;
    u.picture_bytes = lim.picture_bytes - 131072;
    if (!ContentDumpHasRoom(ContentDumpKind::kPicture, 131072, u, lim)) return 202;
    u = ContentDumpUse{};
    u.bytes = lim.bytes;
    if (ContentDumpHasRoom(ContentDumpKind::kTarget, 1, u, lim) || !ContentDumpHasRoom(ContentDumpKind::kTable, 8u << 20, u, lim) ||
        !ContentDumpHasRoom(ContentDumpKind::kPicture, 131072, u, lim)) return 203;
    if (ContentDumpHasRoom(ContentDumpKind::kTarget, 0xFFFFFFFFFFFFFFFFull, ContentDumpUse{0, 1, 0, 0}, lim) ||
        ContentDumpHasRoom(ContentDumpKind::kPicture, 0xFFFFFFFFFFFFFFFFull, ContentDumpUse{0, 0, 0, 1}, lim)) return 204;   // (no wrap)
    if (ContentDumpHasRoom(ContentDumpKind::kTarget, 0, ContentDumpUse{}, lim) || ContentDumpHasRoom(ContentDumpKind::kPicture, 0, ContentDumpUse{}, lim) ||
        ContentDumpHasRoom(ContentDumpKind::kTable, 0, ContentDumpUse{}, lim)) return 205;                                   // (no bytes: nothing to write)
    // The file count alone (asked before a texture's bytes are known).
    u = ContentDumpUse{};
    u.files = lim.files;
    if (ContentDumpFileRoom(ContentDumpKind::kTable, u, lim) || ContentDumpFileRoom(ContentDumpKind::kTarget, u, lim) ||
        !ContentDumpFileRoom(ContentDumpKind::kPicture, u, lim)) return 206;
    u = ContentDumpUse{};
    u.picture_files = lim.picture_files;
    if (!ContentDumpFileRoom(ContentDumpKind::kTable, u, lim) || !ContentDumpFileRoom(ContentDumpKind::kTarget, u, lim) ||
        ContentDumpFileRoom(ContentDumpKind::kPicture, u, lim)) return 206;
  }
  // --- Water discovery (plan Task 1): the reflection pictures by content, the normal map's levels ---
  {
    FcArgs a;
    a.format = 6; a.w = 256; a.h = 256; a.pitch = 256; a.endian = 2; a.base = 0x13401000; a.mip = 0;
    a.mip_max = 0; a.packed = false; a.clamp_x = 0; a.clamp_y = 0;
    uint32_t p[6];
    MakeFc(a, p);
    if (DumpKeyedByContent(p)) return 210;                                       // by its constant alone: once an address, as before
    if (!WaterPictureFetch(p)) return 211;
    if (!DumpKeyedByContentAt(p, true, 13) || !DumpKeyedByContentAt(p, true, 14)) return 212;
    if (DumpKeyedByContentAt(p, false, 13) || DumpKeyedByContentAt(p, true, 0) || DumpKeyedByContentAt(p, true, 2)) return 213;   // another row, another slot
    if (ContentDumpKindOf(p, true) != ContentDumpKind::kReflection || ContentDumpKindOf(p) != ContentDumpKind::kTarget) return 214;
    a.w = 128; a.h = 128; a.pitch = 128;                                         // the 128 x 128 passes before the views: not a picture
    MakeFc(a, p);
    if (WaterPictureFetch(p) || DumpKeyedByContentAt(p, true, 13)) return 215;
    a.w = 256; a.h = 256; a.pitch = 256; a.format = 3;                           // an impostor's picture keeps its own rule and kind
    MakeFc(a, p);
    if (!DumpKeyedByContentAt(p, false, 0) || ContentDumpKindOf(p) != ContentDumpKind::kPicture || WaterPictureFetch(p)) return 216;
    // (a constant the old rule takes keeps its kind at a water row's slot 13 too: `reflection` is only for the new rule's own)
    if (!DumpKeyedByContentAt(p, true, 13) || WaterPictureFetch(p)) return 216;
    // the reflection budget is its own: full, it refuses a reflection and nothing else; the others full, it still has room
    ContentDumpUse use;
    use.reflection_files = kContentDumpLimits.reflection_files;
    if (ContentDumpFileRoom(ContentDumpKind::kReflection, use, kContentDumpLimits) ||
        !ContentDumpFileRoom(ContentDumpKind::kTable, use, kContentDumpLimits) ||
        !ContentDumpFileRoom(ContentDumpKind::kPicture, use, kContentDumpLimits)) return 217;
    if (ContentDumpHasRoom(ContentDumpKind::kReflection, 262144, use, kContentDumpLimits) ||
        !ContentDumpHasRoom(ContentDumpKind::kTarget, 262144, use, kContentDumpLimits) ||
        !ContentDumpHasRoom(ContentDumpKind::kPicture, 131072, use, kContentDumpLimits)) return 217;
    use = ContentDumpUse{};
    use.files = kContentDumpLimits.files;
    use.picture_files = kContentDumpLimits.picture_files;
    use.bytes = kContentDumpLimits.bytes;
    use.picture_bytes = kContentDumpLimits.picture_bytes;
    if (!ContentDumpHasRoom(ContentDumpKind::kReflection, 262144, use, kContentDumpLimits)) return 218;
    // the reflection bytes' edge, and no wrap
    use = ContentDumpUse{};
    use.reflection_bytes = kContentDumpLimits.reflection_bytes - 262143;
    if (ContentDumpHasRoom(ContentDumpKind::kReflection, 262144, use, kContentDumpLimits)) return 218;
    use.reflection_bytes = kContentDumpLimits.reflection_bytes - 262144;
    if (!ContentDumpHasRoom(ContentDumpKind::kReflection, 262144, use, kContentDumpLimits) ||
        ContentDumpHasRoom(ContentDumpKind::kReflection, 0xFFFFFFFFFFFFFFFFull, use, kContentDumpLimits) ||
        ContentDumpHasRoom(ContentDumpKind::kReflection, 0, ContentDumpUse{}, kContentDumpLimits)) return 218;
    use = ContentDumpCounted(ContentDumpKind::kReflection, 262144, ContentDumpUse{});
    if (use.reflection_files != 1 || use.reflection_bytes != 262144 || use.files != 0 || use.bytes != 0 || use.picture_files != 0) return 219;
    // (the other kinds count nothing against the reflections)
    use = ContentDumpCounted(ContentDumpKind::kTarget, 4096, ContentDumpCounted(ContentDumpKind::kPicture, 131072, ContentDumpUse{}));
    if (use.reflection_files != 0 || use.reflection_bytes != 0 || use.files != 1 || use.picture_files != 1) return 219;
    if (kContentDumpLimits.reflection_files != 256 || kContentDumpLimits.reflection_bytes != 256ull * 262144ull) return 207;
    // (the budgets of sub-project 12 stand)
    if (kContentDumpLimits.files != 1024 || kContentDumpLimits.bytes != (512ull << 20) || kContentDumpLimits.picture_files != 448 ||
        kContentDumpLimits.picture_bytes != 448ull * 131072ull) return 207;
    // the normal map's levels: slot 2 of a water row alone
    a.format = 49; a.mip = 0x14D50000; a.mip_max = 8; a.base = 0x14D40000;
    MakeFc(a, p);
    if (!DumpMipsAt(p, false, true, 2) || DumpMipsAt(p, false, false, 2) || DumpMipsAt(p, false, true, 1)) return 208;
    if (!DumpMipsAt(p, true, false, 0)) return 209;                              // a decoded texture: as before, whatever the row
    a.format = 24;                                                               // (another undecoded format at the slot: no levels)
    MakeFc(a, p);
    if (DumpMipsAt(p, false, true, 2)) return 208;
    a.format = 49; a.dim = 3;                                                    // (not a 2D texture)
    MakeFc(a, p);
    if (DumpMipsAt(p, false, true, 2)) return 208;
    // The captured constants themselves (CAP 155547, a water row's slots 13, 14 and 2; the 128 x 128 pass at the same base)
    const uint32_t cap13[6] = {0x82000002u, 0x13401086u, 0x001FE0FFu, 0x00A80C14u, 0x00000003u, 0x00000218u};
    const uint32_t cap14[6] = {0x82000002u, 0x13441086u, 0x001FE0FFu, 0x00A80C14u, 0x00000003u, 0x00000218u};
    const uint32_t cap2[6] = {0x82000002u, 0x14D40071u, 0x001FE0FFu, 0x00A80D10u, 0x00000203u, 0x14D50A18u};
    const uint32_t cap128[6] = {0x81000002u, 0x13401086u, 0x000FE07Fu, 0x00A80C14u, 0x00000003u, 0x00000218u};
    if (!DumpKeyedByContentAt(cap13, true, 13) || !DumpKeyedByContentAt(cap14, true, 14) || DumpKeyedByContent(cap13) ||
        DumpKeyedByContentAt(cap128, true, 13) || DumpKeyedByContentAt(cap2, true, 2)) return 209;
    if (!DumpMipsAt(cap2, false, true, 2) || DumpMipsAt(cap13, false, true, 13)) return 209;
  }
  // --- DecodeK8Level, DecodeDxnLevelRg8, WaterTextureFetch (plan Task 5) ---
  {
    if (TexFormatFromXenos(kXenosK8) != TexFormat::kUnsupported) return 220;      // (Review Focus 4: no record's material changes)
    if (TexFormatFromXenos(kXenosDxn) != TexFormat::kUnsupported) return 221;
    if (kXenosK8 != 2 || kXenosDxn != 49) return 221;
    const uint32_t w = 64, h = 64, pitch = 64;
    std::vector<uint8_t> src(16384, 0);                                           // more than the level spans in any tiling
    auto at = [&](uint32_t x, uint32_t y) { return size_t(uint32_t(fable2::native::capture::TiledOffset2D(int32_t(x), int32_t(y), pitch, 0))); };
    if (at(0, 0) != 0 || at(1, 0) != 1) return 222;                               // the two places that need no rule
    src[at(0, 0)] = 7; src[at(1, 0)] = 255; src[at(0, 1)] = 31; src[at(33, 2)] = 99; src[at(63, 63)] = 200;
    std::vector<uint8_t> out;
    if (!DecodeK8Level(src.data(), src.size(), w, h, pitch, true, &out) || out.size() != size_t(w) * h) return 223;
    if (out[0] != 7 || out[1] != 255 || out[w] != 31 || out[2 * w + 33] != 99 || out[63 * w + 63] != 200 || out[2] != 0) return 224;
    // every texel at its own place: a source that holds each tiled place's low byte and one marker beside it
    {
      std::vector<uint8_t> all(16384, 0);
      size_t top = 0;
      for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
          if (at(x, y) >= all.size() || all[at(x, y)] != 0) return 224;           // inside the source; no two texels at one place
          all[at(x, y)] = uint8_t(1 + (x * 7 + y * 13) % 255);
          top = std::max(top, at(x, y));
        }
      }
      if (!DecodeK8Level(all.data(), all.size(), w, h, pitch, true, &out)) return 224;
      for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
          if (out[size_t(y) * w + x] != uint8_t(1 + (x * 7 + y * 13) % 255)) return 224;
        }
      }
      // the source cut at the highest place fails, one byte more holds (at this pitch the 4,096 texels fill
      // bytes 0 to 4,095 exactly)
      if (top + 1 != size_t(w) * h) return 225;
      if (DecodeK8Level(all.data(), top, w, h, pitch, true, &out) || !out.empty()) return 225;
      if (!DecodeK8Level(all.data(), top + 1, w, h, pitch, true, &out) || out.size() != size_t(w) * h) return 225;
      // a wider pitch moves the second row of tiles (the pitch is the addressing's, not only a bound), and there
      // the bound is each texel's own: a source that holds the texels' number of bytes and not their places fails
      std::vector<uint8_t> wide;
      if (!DecodeK8Level(all.data(), all.size(), w, h, 128, true, &wide) || wide == out) return 225;
      size_t top_wide = 0;
      for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
          top_wide = std::max(top_wide, size_t(uint32_t(fable2::native::capture::TiledOffset2D(int32_t(x), int32_t(y), 128, 0))));
        }
      }
      if (top_wide < size_t(w) * h || top_wide >= all.size()) return 225;
      if (DecodeK8Level(all.data(), top_wide, w, h, 128, true, &wide) || !wide.empty()) return 225;
      if (!DecodeK8Level(all.data(), top_wide + 1, w, h, 128, true, &wide)) return 225;
    }
    if (DecodeK8Level(src.data(), at(63, 63), w, h, pitch, true, &out) || !out.empty()) return 225;       // the last texel outside: nothing
    if (DecodeK8Level(nullptr, src.size(), w, h, pitch, true, &out) || DecodeK8Level(src.data(), src.size(), 0, h, pitch, true, &out) ||
        DecodeK8Level(src.data(), src.size(), w, h, 32, true, &out) || DecodeK8Level(src.data(), src.size(), 8193, 1, 8224, false, &out)) return 226;
    if (DecodeK8Level(src.data(), src.size(), w, 0, pitch, true, &out) || DecodeK8Level(src.data(), src.size(), 1, 8193, 32, false, &out) ||
        DecodeK8Level(src.data(), src.size(), w, h, pitch, true, nullptr)) return 226;
    // linear: rows of the pitch aligned to 32 texels, one byte a texel (the masks of 8 x 16 are such, if their constant says so)
    std::vector<uint8_t> lin(32 * 16, 0);
    lin[32 * 3 + 5] = 77;
    if (!DecodeK8Level(lin.data(), lin.size(), 8, 16, 8, false, &out) || out.size() != 128 || out[3 * 8 + 5] != 77 || out[0] != 0) return 227;
    lin[32 * 15 + 7] = 5;                                                         // the last texel: 32 * 15 + 8 bytes are enough, one fewer is not
    if (!DecodeK8Level(lin.data(), 32 * 15 + 8, 8, 16, 8, false, &out) || out[15 * 8 + 7] != 5) return 227;
    if (DecodeK8Level(lin.data(), 32 * 15 + 7, 8, 16, 8, false, &out) || !out.empty()) return 227;
    // the normal map's level as two bytes a texel: DecodeDxnLevel's values, rounded to nearest.
    // The block is packed here from the format's definition (two end values, then sixteen 3-bit indices from the
    // low end of the next 48 bits, texel i at bit 3 * i) and held to the plan's literal bytes: index 1 everywhere
    // is the bit pattern 001 repeated, 0x49, 0x92, 0x24 twice.
    auto bc4 = [](uint8_t a0, uint8_t a1, uint32_t (*index)(uint32_t), uint8_t o[8]) {
      uint64_t bits = 0;
      for (uint32_t i = 0; i < 16; ++i) bits |= uint64_t(index(i) & 7) << (3 * i);
      o[0] = a0;
      o[1] = a1;
      for (int b = 0; b < 6; ++b) o[2 + b] = uint8_t(bits >> (8 * b));
    };
    uint8_t blk[16];
    bc4(255, 0, [](uint32_t) { return 0u; }, blk);                                // x: all a0 = 255
    bc4(0, 128, [](uint32_t) { return 1u; }, blk + 8);                            // y: all a1 = 128
    const uint8_t plan_blk[16] = {255, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24};
    if (std::memcmp(blk, plan_blk, 16) != 0) return 228;
    std::vector<float> xy;
    std::vector<uint8_t> rg;
    if (!DecodeDxnLevel(blk, sizeof(blk), 4, 4, 1, false, 0, &xy) || !DecodeDxnLevelRg8(blk, sizeof(blk), 4, 4, 1, false, 0, &rg)) return 228;
    if (rg.size() != 32) return 229;
    for (size_t i = 0; i < 32; ++i) {
      if (rg[i] != uint8_t(xy[i] * 255.0f + 0.5f)) return 230;
    }
    if (rg[0] != 255 || rg[1] != 128) return 231;
    if (DecodeDxnLevelRg8(blk, 15, 4, 4, 1, false, 0, &rg) || !rg.empty()) return 232;
    if (DecodeDxnLevelRg8(blk, sizeof(blk), 4, 4, 1, false, 0, nullptr) || DecodeDxnLevelRg8(nullptr, 16, 4, 4, 1, false, 0, &rg) ||
        DecodeDxnLevelRg8(blk, sizeof(blk), 0, 4, 1, false, 0, &rg)) return 232;
    // rounded to NEAREST, both palettes, every index, by whole-number arithmetic that shares nothing with the
    // decoder's floats: eight values 200 > 61 (sevenths), six values 10 <= 251 (fifths), then 0 and 255.
    {
      uint8_t two[16];
      bc4(200, 61, [](uint32_t i) { return i % 8; }, two);
      bc4(10, 251, [](uint32_t i) { return i % 8; }, two + 8);
      if (!DecodeDxnLevelRg8(two, sizeof(two), 4, 4, 1, false, 0, &rg) || rg.size() != 32) return 240;
      bool down = false, up = false;
      for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t k = i % 8;
        // (2 * n + d) / (2 * d): n / d rounded to nearest; no value here is an exact half (7 and 5 are odd)
        const uint32_t nx = k == 0 ? 200u * 7 : k == 1 ? 61u * 7 : (8 - k) * 200 + (k - 1) * 61;
        const uint32_t ny = k == 0 ? 10u * 5 : k == 1 ? 251u * 5 : k < 6 ? (6 - k) * 10 + (k - 1) * 251 : k == 6 ? 0u : 255u * 5;
        if (rg[2 * i] != (2 * nx + 7) / 14 || rg[2 * i + 1] != (2 * ny + 5) / 10) return 240;
        down = down || (nx % 7 != 0 && nx % 7 < 4);                               // a value rounded down and one rounded up are both met:
        up = up || nx % 7 >= 4;                                                   // truncation (the SDK's decompress path) would fail here
      }
      if (!down || !up) return 240;
      // (the texels are row-major inside the block: texel (1, 0) is index 1, texel (0, 1) index 4)
      if (rg[2 * 1] != 61 || rg[2 * 4] != (2 * (4u * 200 + 3 * 61) + 7) / 14) return 240;
    }
    // The normal map as the pass uploads it (ledger ruling 32; plan Task 11): WaterDxnUploadLevel makes the rows of a
    // host BC5 level from the guest's blocks, nothing decoded, and what a GPU reads of those rows (DecodeBc5Rows:
    // red the block's first eight bytes, green its last, the palette entries as fractions) is DecodeDxnLevel's
    // float decode of the guest bytes, float for float: for a tiled level with the 8-in-16 swap whose blocks hold
    // both palettes and every index, and for a level inside a packed tail.
    {
      const uint32_t w = 16, h = 8, pitch = 32;
      std::vector<uint8_t> guest(TiledUpperBound2D(w / 4, h / 4, pitch, 4), 0);
      uint32_t seed = 12345;
      auto next = [&] { seed = seed * 1664525u + 1013904223u; return uint8_t(seed >> 24); };
      for (uint32_t by = 0; by < h / 4; ++by) {
        for (uint32_t bx = 0; bx < w / 4; ++bx) {
          uint8_t b[16];
          for (uint8_t& v : b) v = next();
          if ((bx + by) & 1) std::swap(b[0], b[1]);                                   // both palettes (a0 above a1 and not) in each channel
          CopySwapped(guest.data() + size_t(uint32_t(fable2::native::capture::TiledOffset2D(int32_t(bx), int32_t(by), pitch, 4))), b, 16, 1);
        }
      }
      LevelLayout l;
      l.pitch_blocks = pitch; l.width_blocks = w / 4; l.height_blocks = h / 4;
      const uint32_t row = UploadRowPitch(l.width_blocks, 16);
      std::vector<uint8_t> rows(size_t(row) * l.height_blocks, 0xEE);
      std::vector<float> host, want;
      if (WaterGuestFormat(WaterDecode::kDxn).block != 4 || WaterGuestFormat(WaterDecode::kDxn).bytes_per_block != 16 ||
          WaterGuestFormat(WaterDecode::kDxn).bpb_log2 != 4 || WaterGuestFormat(WaterDecode::kK8).block != 1 ||
          WaterGuestFormat(WaterDecode::kK8).bytes_per_block != 1 || WaterGuestFormat(WaterDecode::kK8).bpb_log2 != 0) return 257;
      if (!WaterDxnUploadLevel(guest.data(), guest.size(), l, true, 1, rows.data(), row)) return 257;
      if (!DecodeBc5Rows(rows.data(), row, w, h, &host) || !DecodeDxnLevel(guest.data(), guest.size(), w, h, pitch, true, 1, &want)) return 257;
      if (host.size() != size_t(w) * h * 2 || host != want) return 257;
      bool fractions = false;
      for (float v : host) fractions = fractions || (v * 255.0f != std::floor(v * 255.0f));
      if (!fractions) return 257;                                                     // (a palette entry that is no whole 8-bit step is among them)
      // Controls: without the swap, and with the two channels' blocks exchanged, the GPU would read another map.
      std::vector<uint8_t> unswapped(rows.size(), 0);
      std::vector<float> other;
      if (!UntileLevel(guest.data(), guest.size(), l, WaterGuestFormat(WaterDecode::kDxn), true, 0, unswapped.data(), row) ||
          !DecodeBc5Rows(unswapped.data(), row, w, h, &other) || other == want) return 257;
      std::vector<uint8_t> exchanged = rows;
      for (size_t at = 0; at + 16 <= exchanged.size(); at += 16) std::swap_ranges(exchanged.begin() + at, exchanged.begin() + at + 8, exchanged.begin() + at + 8);
      if (!DecodeBc5Rows(exchanged.data(), row, w, h, &other) || other == want) return 257;
      // A level that reaches beyond the source is refused whole; a null argument and an empty level decode nothing.
      LevelLayout beyond = l;
      beyond.width_blocks = 64;
      std::vector<uint8_t> wide(size_t(UploadRowPitch(64, 16)) * l.height_blocks, 0);
      if (WaterDxnUploadLevel(guest.data(), guest.size(), beyond, true, 1, wide.data(), UploadRowPitch(64, 16))) return 257;
      if (DecodeBc5Rows(rows.data(), row, w, h, nullptr) || DecodeBc5Rows(nullptr, row, w, h, &other) || !other.empty() ||
          DecodeBc5Rows(rows.data(), 48, w, h, &other) || DecodeBc5Rows(rows.data(), row, 0, h, &other)) return 257;
    }
    // The host channel a shader component reads (the program picks it; the views have no swizzle): the mask's R8
    // gives its one byte for every source channel, the map's BC5 red for channel 0 and green for the others (the
    // SDK's host swizzles RRRR and RGGG); a constant selector stays a constant.
    {
      for (uint8_t s = 0; s < 4; ++s) {
        if (WaterHostChannel(WaterDecode::kK8, s) != 0 || WaterHostChannel(WaterDecode::kDxn, s) != (s == 0 ? 0u : 1u)) return 258;
      }
      for (const WaterDecode kind : {WaterDecode::kK8, WaterDecode::kDxn}) {
        if (WaterHostChannel(kind, 4) != 4 || WaterHostChannel(kind, 5) != 5 || WaterHostChannel(kind, 6) != 4 || WaterHostChannel(kind, 7) != 4) return 258;
      }
    }
    // A stored level inside a packed tail, as a cache reads one: UntileLevel lifts the level's blocks out of the
    // tail (tiled, endian swapped, at its block origin) into linear rows, and the decoder reads those rows with
    // the level's own width as the pitch, linear, endian 0.
    {
      const TexFormatInfo fi = {4, 16, 4};
      LevelLayout l;
      l.pitch_blocks = 32; l.width_blocks = 2; l.height_blocks = 2; l.x_blocks = 4; l.y_blocks = 1;
      std::vector<uint8_t> tail(TiledUpperBound2D(8, 4, 32, 4), 0);
      for (uint32_t by = 0; by < 2; ++by) {
        for (uint32_t bx = 0; bx < 2; ++bx) {
          uint8_t b[16];
          const uint8_t vx = uint8_t(10 + 20 * bx + 40 * by), vy = uint8_t(200 - 3 * bx - 5 * by);
          b[0] = vx; b[1] = vx; b[8] = vy; b[9] = vy;
          std::memset(b + 2, 0, 6);
          std::memset(b + 10, 0, 6);
          CopySwapped(tail.data() + size_t(uint32_t(fable2::native::capture::TiledOffset2D(int32_t(4 + bx), int32_t(1 + by), 32, 4))), b, 16, 1);
        }
      }
      const uint32_t row = ((l.width_blocks + 31) & ~31u) * 16;
      std::vector<uint8_t> rows(size_t(row) * l.height_blocks, 0);
      if (!UntileLevel(tail.data(), tail.size(), l, fi, true, 1, rows.data(), row)) return 241;
      if (!DecodeDxnLevelRg8(rows.data(), rows.size(), 8, 8, l.width_blocks, false, 0, &rg) || rg.size() != 128) return 241;
      {
        // (the same level as the pass uploads it: the host rows' values are the 8-bit decode's before its rounding)
        std::vector<uint8_t> up(rows.size(), 0);
        std::vector<float> host;
        if (!WaterDxnUploadLevel(tail.data(), tail.size(), l, true, 1, up.data(), row) || up != rows || !DecodeBc5Rows(up.data(), row, 8, 8, &host)) return 241;
        for (size_t i = 0; i < host.size(); ++i) {
          if (uint8_t(host[i] * 255.0f + 0.5f) != rg[i]) return 241;
        }
      }
      for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
          if (rg[2 * (y * 8 + x)] != 10 + 20 * (x / 4) + 40 * (y / 4) || rg[2 * (y * 8 + x) + 1] != 200 - 3 * (x / 4) - 5 * (y / 4)) return 241;
        }
      }
    }
    // the fetch: the kind's format alone, the cache's own reader still refusing both
    auto make_water_fc = [](const FcArgs& a, uint32_t fc[6]) {
      MakeFc(a, fc);
      fc[3] |= (a.filter << 21) | (a.filter << 23);  // water fixtures use the same min/mag/mip filter
    };
    FcArgs a;
    a.format = kXenosK8; a.w = 64; a.h = 64; a.pitch = 64; a.endian = 0; a.base = 0x1BD03000; a.mip = 0; a.mip_max = 0;
    a.packed = false; a.clamp_x = 2; a.clamp_y = 2;
    uint32_t p[6];
    make_water_fc(a, p);
    TextureFetch t;
    if (DecodeTextureFetch(p, &t) != FetchError::kFormat) return 233;
    if (WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kNone || t.width != 64 || t.height != 64 || t.base_phys != 0x1BD03000u ||
        t.format != TexFormat::kUnsupported || t.xenos_format != kXenosK8 || t.clamp_x != 2) return 234;
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kFormat) return 235;
    a.w = 512; a.h = 512; a.pitch = 512;
    make_water_fc(a, p);
    // (ledger ruling 42: no size a 2D constant can name is refused for being large; the largest is 8,192 a side)
    if (WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kNone || t.width != 512) return 236;
    a.w = 256; a.h = 256; a.pitch = 256;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kNone) return 236;
    a.w = 8; a.h = 257; a.pitch = 32;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kNone || t.height != 257) return 236;
    a.w = 8192; a.h = 8192; a.pitch = 8192;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kNone || t.width != 8192 || t.height != 8192) return 236;
    a.w = 64; a.h = 64; a.pitch = 32;                                                       // (a pitch below the width is still no texture of the water)
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kSize) return 236;
    a.format = kXenosDxn; a.w = 256; a.h = 256; a.pitch = 256; a.base = 0x14D40000; a.mip = 0x14D50000; a.mip_max = 8;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kNone || t.mip_max != 8 || t.mip_phys != 0x14D50000u || LevelCount(t) != 9) return 237;
    if (t.format != TexFormat::kUnsupported || t.xenos_format != kXenosDxn || DecodeTextureFetch(p, &t) != FetchError::kFormat ||
        WaterTextureFetch(p, WaterDecode::kK8, &t) != FetchError::kFormat) return 237;
    a.w = 2048; a.h = 2048; a.pitch = 2048;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kNone || t.width != 2048) return 238;      // (ruling 42)
    a.w = 1024; a.h = 1024; a.pitch = 1024;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kNone) return 238;
    a.pitch = 512;                                                                          // (a pitch below the width; no base)
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kSize) return 238;
    a.pitch = 1024; a.base = 0;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kSize) return 238;
    a.base = 0x14D40000; a.dim = 0; a.w = 256; a.h = 256; a.pitch = 256;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kNot2D) return 239;
    a.dim = 1; a.type = 0;
    make_water_fc(a, p);
    if (WaterTextureFetch(p, WaterDecode::kDxn, &t) != FetchError::kNotTexture) return 239;
    // Ledger ruling 33 (sub-project 13; the review of Tasks 1 to 5, I1): a constant that does not deliver its
    // components as unsigned fractions is refused (kSign), so that a mask's sample is a number in [0, 1] whenever
    // the rule admits the draw: a sign field other than 0 on a delivered component (x for the mask; x and y for the
    // normal map), the integer number format (dword 3 bit 0), an exponent adjust (dword 3 bits 13 to 18). The same
    // cases as tests/test_water_textures.py holds water_textures.fetch to.
    {
      const uint32_t mask[6] = {0x80804802u, 0x1BD03002u, 0x0007E03Fu, 0x00A81400u, 0x00000003u, 0x00000218u};   // CAP 155547, slot 1
      const uint32_t map[6] = {0x82000002u, 0x14D40071u, 0x001FE0FFu, 0x00A80D10u, 0x00000203u, 0x14D50A18u};    // slot 2
      uint32_t q[6];
      TextureFetch keep;
      keep.width = 77;
      auto with = [&](const uint32_t* from, uint32_t dword, uint32_t bits) {
        std::memcpy(q, from, sizeof(q));
        q[dword] |= bits;
        return q;
      };
      for (uint32_t sign = 1; sign <= 3; ++sign) {                                          // signed, biased, gamma
        if (WaterTextureFetch(with(mask, 0, sign << 2), WaterDecode::kK8, &keep) != FetchError::kSign || keep.width != 77) return 249;
        if (WaterTextureFetch(with(map, 0, sign << 2), WaterDecode::kDxn, &keep) != FetchError::kSign) return 249;
        if (WaterTextureFetch(with(map, 0, sign << 4), WaterDecode::kDxn, &keep) != FetchError::kSign || keep.width != 77) return 249;
        // a component the shader is not given is not judged: the mask's y, z, w; the map's z, w
        if (WaterTextureFetch(with(mask, 0, sign << 4), WaterDecode::kK8, &t) != FetchError::kNone) return 250;
        if (WaterTextureFetch(with(mask, 0, sign << 8), WaterDecode::kK8, &t) != FetchError::kNone) return 250;
        if (WaterTextureFetch(with(map, 0, sign << 6), WaterDecode::kDxn, &t) != FetchError::kNone) return 250;
      }
      if (WaterTextureFetch(with(mask, 3, 1u), WaterDecode::kK8, &keep) != FetchError::kSign ||
          WaterTextureFetch(with(map, 3, 1u), WaterDecode::kDxn, &keep) != FetchError::kSign) return 251;      // the integer number format
      for (uint32_t adjust : {1u, 0x20u, 0x3Fu}) {                                          // +1, -32, -1
        if (WaterTextureFetch(with(mask, 3, adjust << 13), WaterDecode::kK8, &keep) != FetchError::kSign ||
            WaterTextureFetch(with(map, 3, adjust << 13), WaterDecode::kDxn, &keep) != FetchError::kSign || keep.width != 77) return 252;
      }
      // Ledger ruling 41 (the review of Tasks 4 to 15, I1): the sign is the SOURCE CHANNEL's that the component's
      // swizzle names, as the SDK takes it (texture_util: sign = (dword 0 >> (2 + 2 * swizzle)) & 3); a constant
      // selector (4, 5) has none. The reviewer's two constants: the mask's x from channel 1 with channel 1 biased,
      // the map's x from channel 2 with channel 2 biased, are refused; the sign of a channel no delivered
      // component names is not judged, channel 0's among them.
      {
        auto moved = [&](const uint32_t* from, uint32_t component, uint32_t channel, uint32_t sign_channel, uint32_t sign) {
          std::memcpy(q, from, sizeof(q));
          q[3] = (q[3] & ~(7u << (1 + 3 * component))) | (channel << (1 + 3 * component));
          q[0] = (q[0] & ~(3u << (2 + 2 * sign_channel))) | (sign << (2 + 2 * sign_channel));
          return q;
        };
        for (uint32_t sign = 1; sign <= 3; ++sign) {
          if (WaterTextureFetch(moved(mask, 0, 1, 1, sign), WaterDecode::kK8, &keep) != FetchError::kSign || keep.width != 77) return 254;
          if (WaterTextureFetch(moved(map, 0, 2, 2, sign), WaterDecode::kDxn, &keep) != FetchError::kSign) return 254;
          if (WaterTextureFetch(moved(map, 1, 3, 3, sign), WaterDecode::kDxn, &keep) != FetchError::kSign || keep.width != 77) return 254;
          // (x from channel 1: channel 0's sign is no delivered component's)
          if (WaterTextureFetch(moved(mask, 0, 1, 0, sign), WaterDecode::kK8, &t) != FetchError::kNone || t.swizzle[0] != 1) return 255;
          if (WaterTextureFetch(moved(map, 0, 2, 0, sign), WaterDecode::kDxn, &t) != FetchError::kNone) return 255;
          // (a constant selector has no sign: x the constant 1, every channel's sign set)
          std::memcpy(q, mask, sizeof(q));
          q[3] = (q[3] & ~(7u << 1)) | (5u << 1);
          q[0] |= (sign << 2) | (sign << 4) | (sign << 6) | (sign << 8);
          if (WaterTextureFetch(q, WaterDecode::kK8, &t) != FetchError::kNone) return 255;
        }
      }
      // Ledger ruling 42 (I3): the size is bounded by what a 2D fetch constant can name (8,192 a side:
      // kWaterFetchMaxSide), not by a multiple of one region's textures: a 512 x 512 mask and a 4,096 x 4,096
      // normal map are the water's.
      {
        std::memcpy(q, mask, sizeof(q));
        q[2] = 511u | (511u << 13);
        q[0] = (q[0] & ~(0x1FFu << 22)) | (16u << 22);
        if (kWaterFetchMaxSide != 8192u || WaterTextureFetch(q, WaterDecode::kK8, &t) != FetchError::kNone || t.width != 512 || t.height != 512) return 256;
        std::memcpy(q, map, sizeof(q));
        q[2] = 4095u | (4095u << 13);
        q[0] = (q[0] & ~(0x1FFu << 22)) | (128u << 22);
        if (WaterTextureFetch(q, WaterDecode::kDxn, &t) != FetchError::kNone || t.width != 4096) return 256;
      }
      // The review of Tasks 11 and 12, Minor 3 (ledger ruling 46): the normal map's two fetches take the level the
      // GPU computes from the coordinates' derivatives, where the emulator multiplies those by 2 ^ (the constant's
      // LOD bias / 32) first (dword 4 bits 12 to 21, signed). The surface's program applies no bias, so a map
      // whose constant has one would be drawn a level off: refused with its own name. The mask is sampled at
      // level 0 and its bias is not judged. The captured constant's field is 0.
      {
        if (((map[4] >> 12) & 0x3FFu) != 0) return 260;
        for (uint32_t bias : {1u, 32u, 0x1FFu, 0x200u, 0x3FFu}) {                         // 1/32, +1, the largest, -16, -1/32
          if (WaterTextureFetch(with(map, 4, bias << 12), WaterDecode::kDxn, &keep) != FetchError::kLodBias || keep.width != 77) return 260;
          if (WaterTextureFetch(with(mask, 4, bias << 12), WaterDecode::kK8, &t) != FetchError::kNone) return 260;
        }
        // (the bits beside the field are not it: the levels below, the two above)
        if (WaterTextureFetch(with(map, 4, 0xFC3u), WaterDecode::kDxn, &t) != FetchError::kNone) return 260;
        if (WaterTextureFetch(with(map, 4, 0xFFC00000u), WaterDecode::kDxn, &t) != FetchError::kNone) return 260;
        // (a sign is said before a bias: an old case keeps its name)
        std::memcpy(q, map, sizeof(q));
        q[0] |= 1u << 2;
        q[4] |= 32u << 12;
        if (WaterTextureFetch(q, WaterDecode::kDxn, &keep) != FetchError::kSign) return 260;
        if (std::string(FetchErrorName(FetchError::kLodBias)) != "lod-bias") return 260;
      }
      // (the size and the format are judged first, as before: a refusal's name does not change for an old case)
      if (WaterTextureFetch(with(mask, 1, 49u), WaterDecode::kK8, &keep) != FetchError::kFormat) return 253;
      if (std::string(FetchErrorName(FetchError::kSign)) != "sign") return 253;
    }
    // every field as DecodeTextureFetch reads it: one constant of a decoded format, its format bits exchanged for
    // the kind's, gives the same fetch but for the two format members
    {
      FcArgs d;                                                                             // the defaults: DXT1, 256 x 128, mips, swizzle, filter, clamps
      d.w = 256; d.h = 128;
      d.clamp_y = d.clamp_x;
      uint32_t q[6];
      make_water_fc(d, q);
      TextureFetch ref, got;
      if (DecodeTextureFetch(q, &ref) != FetchError::kNone) return 242;
      q[1] = (q[1] & ~0x3Fu) | kXenosDxn;
      if (WaterTextureFetch(q, WaterDecode::kDxn, &got) != FetchError::kNone) return 242;
      if (got.base_phys != ref.base_phys || got.mip_phys != ref.mip_phys || got.width != ref.width || got.height != ref.height ||
          got.pitch_texels != ref.pitch_texels || got.endian != ref.endian || got.tiled != ref.tiled || got.packed_mips != ref.packed_mips ||
          got.mip_min != ref.mip_min || got.mip_max != ref.mip_max || std::memcmp(got.swizzle, ref.swizzle, 4) != 0 || got.clamp_x != ref.clamp_x ||
          got.clamp_y != ref.clamp_y || got.linear_filter != ref.linear_filter) return 242;
      // a refusal leaves the output as it was
      TextureFetch keep;
      keep.width = 77;
      q[1] = (q[1] & ~0x3Fu) | 18;
      if (WaterTextureFetch(q, WaterDecode::kDxn, &keep) != FetchError::kFormat || keep.width != 77) return 242;
    }
    // Unsupported samplers fail before producing an upload or static sampler index (same grid as Python).
    {
      uint32_t q[6] = {0x82000002u, 0x14D40071u, 0x001FE0FFu, 0x00A80D10u, 0x00000203u, 0x14D50A18u};
      TextureFetch keep;
      keep.width = 77;
      for (uint32_t x = 0; x < 8; ++x) for (uint32_t y = 0; y < 8; ++y) {
        q[0] = (q[0] & ~(0x3Fu << 10)) | (x << 10) | (y << 13);
        const bool supported = x == y && (x == 0 || x == 2);
        if (WaterSamplerSupported(q) != supported) return 261;
        if (!supported && (WaterTextureFetch(q, WaterDecode::kDxn, &keep) != FetchError::kSampler || keep.width != 77)) return 261;
      }
      q[0] &= ~(0x3Fu << 10);
      for (uint32_t mag = 0; mag < 4; ++mag) for (uint32_t min = 0; min < 4; ++min) for (uint32_t mip = 0; mip < 4; ++mip) {
        q[3] = (q[3] & ~(0x3Fu << 19)) | (mag << 19) | (min << 21) | (mip << 23);
        const bool supported = mag <= 1 && mag == min && mag == mip;
        if (WaterSamplerSupported(q) != supported) return 261;
        if (!supported && (WaterTextureFetch(q, WaterDecode::kDxn, &keep) != FetchError::kSampler || keep.width != 77)) return 261;
      }
      q[3] = 0x00A80D10u;
      for (uint32_t minimum = 1; minimum < 16; ++minimum) {
        q[4] = 0x203u | (minimum << 2);
        if (WaterTextureFetch(q, WaterDecode::kDxn, &keep) != FetchError::kSampler || keep.width != 77) return 261;
      }
      q[4] = 0x203u;
      for (uint32_t aniso = 1; aniso < 8; ++aniso) {
        q[3] = 0x00A80D10u | (aniso << 25);
        if (WaterTextureFetch(q, WaterDecode::kDxn, &keep) != FetchError::kSampler || keep.width != 77) return 261;
      }
      if (std::string(FetchErrorName(FetchError::kSampler)) != "sampler") return 261;
    }
    // The captured constants (CAP 155547, a water row's slots 1 and 2): what the two readers make of them
    const uint32_t cap1[6] = {0x80804802u, 0x1BD03002u, 0x0007E03Fu, 0x00A81400u, 0x00000003u, 0x00000218u};
    const uint32_t cap1s[6] = {0x80404802u, 0x1B6D0002u, 0x0001E007u, 0x00A81400u, 0x00000003u, 0x00000218u};
    const uint32_t cap2[6] = {0x82000002u, 0x14D40071u, 0x001FE0FFu, 0x00A80D10u, 0x00000203u, 0x14D50A18u};
    if (WaterTextureFetch(cap1, WaterDecode::kK8, &t) != FetchError::kNone || t.width != 64 || t.height != 64 || t.pitch_texels != 64 || !t.tiled ||
        t.endian != 0 || t.clamp_x != 2 || t.clamp_y != 2 || t.mip_phys != 0 || LevelCount(t) != 1) return 243;
    if (t.swizzle[0] != 0 || t.swizzle[1] != 0 || t.swizzle[2] != 0 || t.swizzle[3] != 5 || !t.linear_filter) return 243;   // the mask: (x, x, x, one)
    if (WaterTextureFetch(cap1s, WaterDecode::kK8, &t) != FetchError::kNone || t.width != 8 || t.height != 16 || t.pitch_texels != 32 || !t.tiled) return 243;
    if (WaterTextureFetch(cap2, WaterDecode::kDxn, &t) != FetchError::kNone || t.width != 256 || t.height != 256 || t.pitch_texels != 256 || !t.tiled ||
        t.endian != 1 || t.clamp_x != 0 || t.clamp_y != 0 || t.mip_phys != 0x14D50000u || t.mip_max != 8 || !t.packed_mips || LevelCount(t) != 9) return 243;
    if (t.swizzle[0] != 0 || t.swizzle[1] != 1 || t.swizzle[2] != 2 || t.swizzle[3] != 3 || !t.linear_filter) return 243;   // the map: x the first block, y the second
  }
  // --- k_DXN (format 49): two BC4 blocks per 4 x 4 block, the terrain's normal map (tf17) ---
  // The same blocks and numbers as test_decode_dxn of tests/test_terrain_replay.py.
  {
    auto near = [](float a, float b) { return std::fabs(a - b) <= 1e-6f; };
    // 8 bytes of a BC4 block as the GPU reads them: two end values, then texel i's 3-bit index from the low end.
    auto bc4 = [](uint8_t a0, uint8_t a1, uint32_t (*index)(uint32_t), uint8_t out[8]) {
      uint64_t bits = 0;
      for (uint32_t i = 0; i < 16; ++i) bits |= uint64_t(index(i) & 7) << (3 * i);
      out[0] = a0;
      out[1] = a1;
      for (int b = 0; b < 6; ++b) out[2 + b] = uint8_t(bits >> (8 * b));
    };
    auto by_i = [](uint32_t i) { return i % 8; };
    auto zero = [](uint32_t) { return 0u; };
    // Block 0: x has end values 200 > 60 (eight values), y 10 <= 250 (six values, then 0 and 255), texel i
    // with index i % 8. Block 1: x 255 everywhere, y 0.
    uint8_t post[32];
    bc4(200, 60, by_i, post);
    bc4(10, 250, by_i, post + 8);
    bc4(255, 255, zero, post + 16);
    bc4(0, 0, zero, post + 24);
    const float x_want[8] = {200, 60, 180, 160, 140, 120, 100, 80};
    const float y_want[8] = {10, 250, 58, 106, 154, 202, 0, 255};
    for (uint32_t i = 0; i < 16; ++i) {
      if (!near(Bc4Value(post, i), x_want[i % 8]) || !near(Bc4Value(post + 8, i), y_want[i % 8])) return 90;
    }
    // A value that is no whole number: 200 and 61 give (6 * 200 + 61) / 7 at index 2. Not rounded.
    {
      uint8_t odd[8];
      bc4(200, 61, [](uint32_t) { return 2u; }, odd);
      if (!near(Bc4Value(odd, 5), 1261.0f / 7.0f) || Bc4Value(odd, 5) == 180.0f) return 91;
    }
    for (bool tiled : {false, true}) {
      for (uint32_t endian : {1u, 0u, 2u, 3u}) {
        // Linear: blocks 0 and 1 of a row of 32 blocks, at 0 and 16; tiled: block (1, 0) at 32 (16-byte
        // blocks sit two to a 32-byte column of the tile; (0, 1) lies between them).
        const uint32_t second = tiled ? uint32_t(capture::TiledOffset2D(1, 0, 8, 4)) : 16u;
        if (second != (tiled ? 32u : 16u)) return 89;
        std::vector<uint8_t> g(second + 16, 0);
        CopySwapped(g.data(), post, 16, endian);  // the swaps are their own inverses
        CopySwapped(g.data() + second, post + 16, 16, endian);
        float xy[2] = {9, 9};
        for (uint32_t i = 0; i < 16; ++i) {
          if (!DecodeDxnTexel(g.data(), g.size(), 8, 4, 8, tiled, endian, i % 4, i / 4, xy)) return 92;
          // x is the first eight bytes of the block, y the second.
          if (!near(xy[0], x_want[i % 8] / 255.0f) || !near(xy[1], y_want[i % 8] / 255.0f)) return 93;
        }
        if (!DecodeDxnTexel(g.data(), g.size(), 8, 4, 8, tiled, endian, 5, 2, xy) || xy[0] != 1.0f || xy[1] != 0.0f) return 94;
        // Nothing is read out of range: a block cut by one byte, a texel outside the level, no source.
        xy[0] = xy[1] = 9;
        if (DecodeDxnTexel(g.data(), g.size() - 1, 8, 4, 8, tiled, endian, 5, 2, xy) || xy[0] != 9 || xy[1] != 9) return 95;
        if (!DecodeDxnTexel(g.data(), g.size() - 1, 8, 4, 8, tiled, endian, 3, 3, xy)) return 96;  // block 0 is whole
        if (DecodeDxnTexel(g.data(), g.size(), 8, 4, 8, tiled, endian, 8, 0, xy)) return 97;
        if (DecodeDxnTexel(g.data(), g.size(), 8, 4, 8, tiled, endian, 0, 4, xy)) return 98;
        if (DecodeDxnTexel(nullptr, g.size(), 8, 4, 8, tiled, endian, 0, 0, xy)) return 99;
        // The whole level: width * height pairs, row-major.
        std::vector<float> level(3, 7.0f);
        if (!DecodeDxnLevel(g.data(), g.size(), 8, 4, 8, tiled, endian, &level) || level.size() != 64) return 100;
        if (!near(level[2 * (1 * 8 + 2)], x_want[6] / 255.0f) || !near(level[2 * (1 * 8 + 2) + 1], y_want[6] / 255.0f) ||
            level[2 * (3 * 8 + 7)] != 1.0f || level[2 * (3 * 8 + 7) + 1] != 0.0f) return 101;
        if (DecodeDxnLevel(g.data(), g.size() - 1, 8, 4, 8, tiled, endian, &level) || !level.empty()) return 102;
      }
    }
    // The byte order is the fetch constant's: the 8in16 guest bytes read without the swap have the end
    // values exchanged (60 <= 200: the six-value mode, whose index 0 is 60).
    {
      uint8_t one[16], guest[16];
      bc4(200, 60, zero, one);
      bc4(0, 0, zero, one + 8);
      CopySwapped(guest, one, 16, 1);
      float xy[2];
      if (!DecodeDxnTexel(guest, 16, 4, 4, 1, false, 1, 0, 0, xy) || !near(xy[0], 200.0f / 255.0f)) return 103;
      if (!DecodeDxnTexel(guest, 16, 4, 4, 1, false, 0, 0, 0, xy) || !near(xy[0], 60.0f / 255.0f)) return 104;
    }
    // A 70 x 45 level (edge blocks cropped), linear with a pitch of 24 blocks (rows of 32 blocks of 16
    // bytes) and tiled: block (bx, by) holds x = 3 * bx + by and y = 250 - bx - 2 * by everywhere.
    for (bool tiled : {false, true}) {
      const uint32_t W = 70, H = 45, WB = 18, HB = 12, PITCH = 24;
      uint32_t size = 0, last_x = 0, last_y = 0;  // the block that ends the level's bytes
      auto at = [&](uint32_t bx, uint32_t by) {
        return tiled ? uint32_t(capture::TiledOffset2D(int(bx), int(by), PITCH, 4)) : by * 32u * 16u + bx * 16u;
      };
      for (uint32_t by = 0; by < HB; ++by)
        for (uint32_t bx = 0; bx < WB; ++bx) {
          if (at(bx, by) + 16 > size) {
            size = at(bx, by) + 16;
            last_x = bx;
            last_y = by;
          }
        }
      std::vector<uint8_t> g(size, 0);
      for (uint32_t by = 0; by < HB; ++by)
        for (uint32_t bx = 0; bx < WB; ++bx) {
          uint8_t blk[16];
          bc4(uint8_t(3 * bx + by), uint8_t(3 * bx + by), zero, blk);
          bc4(uint8_t(250 - bx - 2 * by), uint8_t(250 - bx - 2 * by), zero, blk + 8);
          CopySwapped(g.data() + at(bx, by), blk, 16, 1);
        }
      float xy[2];
      for (uint32_t y = 0; y < H; y += 7)
        for (uint32_t x = 0; x < W; x += 3) {
          if (!DecodeDxnTexel(g.data(), g.size(), W, H, PITCH, tiled, 1, x, y, xy)) return 105;
          if (!near(xy[0], float(3 * (x / 4) + y / 4) / 255.0f) || !near(xy[1], float(250 - x / 4 - 2 * (y / 4)) / 255.0f)) return 106;
        }
      // The block that ends the bytes is needed whole for its own texels, and for no other.
      if (DecodeDxnTexel(g.data(), g.size() - 1, W, H, PITCH, tiled, 1, last_x * 4, last_y * 4, xy)) return 107;
      if (!DecodeDxnTexel(g.data(), g.size(), W, H, PITCH, tiled, 1, last_x * 4, last_y * 4, xy)) return 109;
      if (!DecodeDxnTexel(g.data(), g.size() - 1, W, H, PITCH, tiled, 1, 0, 0, xy)) return 115;
      // The size the level spans by the dump rule (pitch 96 texels) holds every block the decode reads.
      const uint32_t dump_fc[6] = {2u | ((96u >> 5) << 22) | (uint32_t(tiled) << 31), 49u | (1u << 6) | 0x1A000000u,
                                   (W - 1) | ((H - 1) << 13), 0, 0, 1u << 9};
      if (BaseLevelDumpBytes(dump_fc) < size || (!tiled && BaseLevelDumpBytes(dump_fc) != size)) return 108;
    }
  }
  // --- the real terrain weight map, when a discovery dump of tf18 is on disk (else SKIPPED, loudly) ---
  // FABLE2_TF18_BIN: the dumped base-level bytes (native_tex_<stamp>/<BASE>_<IDENTITY>.bin of the tess row's tf18);
  // FABLE2_TF18_FC: its six fetch dwords as hex, comma separated (the "texture" row's "fc"). The decoded R8 image is
  // written to FABLE2_TF18_BIN + ".cpp.r8" for tests/test_texture_thumb.py's real-dump test to compare with.
  {
    const char* bin = std::getenv("FABLE2_TF18_BIN");
    const char* fcs = std::getenv("FABLE2_TF18_FC");
    if (!bin || !fcs) {
      std::printf("SKIPPED: real tf18 dump test (set FABLE2_TF18_BIN and FABLE2_TF18_FC after a discovery capture)\n");
    } else {
      uint32_t rfc[6] = {};
      if (std::sscanf(fcs, "%x,%x,%x,%x,%x,%x", &rfc[0], &rfc[1], &rfc[2], &rfc[3], &rfc[4], &rfc[5]) != 6) return 60;
      TextureFetch rt;
      if (DecodeTextureFetch(rfc, &rt) != FetchError::kNone || rt.format != TexFormat::kDxt3a) return 61;
      std::FILE* f = std::fopen(bin, "rb");
      if (!f) return 62;
      std::vector<uint8_t> bytes;
      uint8_t chunk[65536];
      for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) bytes.insert(bytes.end(), chunk, chunk + n);
      std::fclose(f);
      std::vector<uint8_t> r8;
      if (!DecodeDxt3aLevel(bytes.data(), bytes.size(), rt.width, rt.height, rt.pitch_texels / 4, rt.tiled,
                            rt.endian, &r8) || r8.size() != size_t(rt.width) * rt.height) return 63;
      const std::string outp = std::string(bin) + ".cpp.r8";
      std::FILE* o = std::fopen(outp.c_str(), "wb");
      if (!o || std::fwrite(r8.data(), 1, r8.size(), o) != r8.size()) return 64;
      std::fclose(o);
      std::printf("tf18 dump decoded: %ux%u, wrote %s\n", rt.width, rt.height, outp.c_str());
    }
  }
  // --- the real terrain normal map, when a discovery dump of tf17 is on disk (else SKIPPED, loudly) ---
  // FABLE2_TF17_BIN / FABLE2_TF17_FC as above, for the tess rows' tf17 (k_DXN). The decoded level is written to
  // The shadow mask's rotation tile (plan Task 5 of the shadows): 32 rows of 256 guest bytes, two bytes a
  // texel, exchanged by the fetch's 8-in-16 swap: the shader's x is the SECOND stored byte, y the first.
  {
    std::vector<uint8_t> guest(kRotationTileGuestBytes, 0);
    static_assert(kRotationTileGuestBytes == 31 * 256 + 64 && kRotationTileBytes == 2048 && kRotationTilePhys == 0x1A53B000u);
    // Every texel the vector (1, 0): x byte 255 (a = 1), y byte 128 (b = 0.004): stored as (128, 255).
    for (uint32_t y = 0; y < 32; ++y) {
      for (uint32_t x = 0; x < 32; ++x) {
        guest[y * 256 + x * 2] = 128;
        guest[y * 256 + x * 2 + 1] = 255;
      }
    }
    // One hand-made texel, (5, 3): the vector (0.6, -0.8): x byte 204 (0.6), y byte 26 (-0.796), stored
    // y first: bytes (26, 204).
    guest[3 * 256 + 5 * 2] = 26;
    guest[3 * 256 + 5 * 2 + 1] = 204;
    // (bytes between a row's 64 and the next row are not the tile's: garbage there changes nothing)
    guest[3 * 256 + 64] = 7;
    guest[3 * 256 + 255] = 9;
    uint8_t tile[kRotationTileBytes];
    std::memset(tile, 0xEE, sizeof(tile));
    if (!DecodeRotationTile(guest.data(), guest.size(), tile)) return 120;
    if (tile[2 * (3 * 32 + 5)] != 204 || tile[2 * (3 * 32 + 5) + 1] != 26) return 121;  // x = second stored byte, y = first
    if (tile[0] != 255 || tile[1] != 128 || tile[2 * (31 * 32 + 31)] != 255 || tile[2 * (31 * 32 + 31) + 1] != 128) return 122;
    // The rotation it gives: (a, b) = (0.6, -0.796); the other order would be (-0.796, 0.6), a mirror.
    const float a = 2.0f * tile[2 * (3 * 32 + 5)] / 255.0f - 1.0f, b = 2.0f * tile[2 * (3 * 32 + 5) + 1] / 255.0f - 1.0f;
    if (std::fabs(a - 0.6f) > 1e-6f || std::fabs(b + 0.7960784f) > 1e-6f) return 123;
    // Fail closed: too few bytes, no source, a texel that is no unit vector (zeros, or a vector too
    // long): false, and the output is not touched.
    uint8_t untouched[kRotationTileBytes];
    std::memset(untouched, 0xEE, sizeof(untouched));
    uint8_t before[kRotationTileBytes];
    std::memcpy(before, untouched, sizeof(before));
    if (DecodeRotationTile(guest.data(), guest.size() - 1, untouched) || DecodeRotationTile(nullptr, guest.size(), untouched)) return 124;
    std::vector<uint8_t> bad = guest;
    bad[31 * 256 + 62] = 128;  // the last texel: (0, 0)
    bad[31 * 256 + 63] = 128;
    if (DecodeRotationTile(bad.data(), bad.size(), untouched)) return 125;
    bad = guest;
    bad[10 * 256 + 4] = 255;   // (1, 1): length 1.41
    if (DecodeRotationTile(bad.data(), bad.size(), untouched)) return 125;
    if (std::memcmp(untouched, before, sizeof(before)) != 0) return 126;
    std::vector<uint8_t> zeros(kRotationTileGuestBytes, 0);  // unmapped or cleared memory: (-1, -1), length 1.41
    if (DecodeRotationTile(zeros.data(), zeros.size(), untouched)) return 127;
    // The edge of the tolerance: a length of 0.96 passes, 0.94 does not.
    bad = guest;
    bad[0] = 128;
    bad[1] = uint8_t(127.5f + 127.5f * 0.96f);
    if (!DecodeRotationTile(bad.data(), bad.size(), tile)) return 128;
    bad[1] = uint8_t(127.5f + 127.5f * 0.94f);
    if (DecodeRotationTile(bad.data(), bad.size(), tile)) return 128;
    // The game's own tile, when its dump is there (out/native-evidence/shadows; git-ignored): it passes.
    if (std::FILE* f = std::fopen("out/native-evidence/shadows/task1_bridge_0F99_tf14_1A53B000_FB036F07EB514FF4.bin", "rb")) {
      std::vector<uint8_t> dump(65536);
      const size_t n = std::fread(dump.data(), 1, dump.size(), f);
      std::fclose(f);
      if (n < kRotationTileGuestBytes || !DecodeRotationTile(dump.data(), n, tile)) return 129;
      if (tile[0] != dump[1] || tile[1] != dump[0]) return 129;
      std::printf("rotation tile: the game's dump passes the check\n");
    }
  }
  // FABLE2_TF17_BIN + ".cpp.xy" (two little-endian floats per texel) for tests/test_terrain_replay.py's real-dump
  // test to compare with.
  {
    const char* bin = std::getenv("FABLE2_TF17_BIN");
    const char* fcs = std::getenv("FABLE2_TF17_FC");
    if (!bin || !fcs) {
      std::printf("SKIPPED: real tf17 dump test (set FABLE2_TF17_BIN and FABLE2_TF17_FC after a discovery capture)\n");
    } else {
      uint32_t rfc[6] = {};
      if (std::sscanf(fcs, "%x,%x,%x,%x,%x,%x", &rfc[0], &rfc[1], &rfc[2], &rfc[3], &rfc[4], &rfc[5]) != 6) return 110;
      if ((rfc[1] & 0x3F) != 49) return 111;
      std::FILE* f = std::fopen(bin, "rb");
      if (!f) return 112;
      std::vector<uint8_t> bytes;
      uint8_t chunk[65536];
      for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) bytes.insert(bytes.end(), chunk, chunk + n);
      std::fclose(f);
      const uint32_t w = (rfc[2] & 0x1FFF) + 1, h = ((rfc[2] >> 13) & 0x1FFF) + 1;
      std::vector<float> xy;
      if (!DecodeDxnLevel(bytes.data(), bytes.size(), w, h, (((rfc[0] >> 22) & 0x1FF) << 5) / 4, (rfc[0] >> 31) != 0,
                          (rfc[1] >> 6) & 3, &xy) || xy.size() != size_t(w) * h * 2) return 113;
      const std::string outp = std::string(bin) + ".cpp.xy";
      std::FILE* o = std::fopen(outp.c_str(), "wb");
      if (!o || std::fwrite(xy.data(), sizeof(float), xy.size(), o) != xy.size()) return 114;
      std::fclose(o);
      std::printf("tf17 dump decoded: %ux%u, wrote %s\n", w, h, outp.c_str());
    }
  }
  // FABLE2_1555_BIN + ".cpp.rgba" for tests/test_impostor_pictures.py's real-dump test (as FABLE2_TF17_BIN above):
  // a dumped base level of an impostor's picture (format 3) and its six fetch dwords in FABLE2_1555_FC, decoded by
  // the constant's own width, height, pitch, tiling and endian.
  {
    const char* bin = std::getenv("FABLE2_1555_BIN");
    const char* fcs = std::getenv("FABLE2_1555_FC");
    if (!bin || !fcs) {
      std::printf("SKIPPED: real format-3 dump test (set FABLE2_1555_BIN and FABLE2_1555_FC after a discovery capture)\n");
    } else {
      uint32_t rfc[6] = {};
      if (std::sscanf(fcs, "%x,%x,%x,%x,%x,%x", &rfc[0], &rfc[1], &rfc[2], &rfc[3], &rfc[4], &rfc[5]) != 6) return 180;
      TextureFetch pt;
      TexFormatInfo pfi;
      if ((rfc[1] & 0x3F) != 3 || !DumpFetch(rfc, &pt, &pfi)) return 181;
      std::FILE* f = std::fopen(bin, "rb");
      if (!f) return 182;
      std::vector<uint8_t> bytes;
      uint8_t chunk[65536];
      for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) bytes.insert(bytes.end(), chunk, chunk + n);
      std::fclose(f);
      std::vector<uint8_t> rgba;
      if (!Decode1555Level(bytes.data(), bytes.size(), pt.width, pt.height, pt.pitch_texels, pt.tiled, pt.endian, &rgba) ||
          rgba.size() != size_t(pt.width) * pt.height * 4) return 183;
      const std::string outp = std::string(bin) + ".cpp.rgba";
      std::FILE* o = std::fopen(outp.c_str(), "wb");
      if (!o || std::fwrite(rgba.data(), 1, rgba.size(), o) != rgba.size()) return 184;
      std::fclose(o);
      std::printf("format-3 dump decoded: %ux%u, wrote %s\n", pt.width, pt.height, outp.c_str());
    }
  }
  // FABLE2_K8_BIN / FABLE2_K8_FC and FABLE2_WDXN_BIN / FABLE2_WDXN_FC: a dumped mask and the dumped normal map's
  // base level through the two decoders (plan Task 5), written to <bin>.cpp.r8 and <bin>.cpp.rg8 for
  // tools/xdk_sigmatch/water_textures.py compare and tests/test_water_textures.py (as FABLE2_TF17_BIN above).
  // Unset: one SKIPPED line each.
  for (const WaterDecode kind : {WaterDecode::kK8, WaterDecode::kDxn}) {
    const bool k8 = kind == WaterDecode::kK8;
    const char* bin = std::getenv(k8 ? "FABLE2_K8_BIN" : "FABLE2_WDXN_BIN");
    const char* fcs = std::getenv(k8 ? "FABLE2_K8_FC" : "FABLE2_WDXN_FC");
    if (!bin || !fcs) {
      std::printf("SKIPPED: real %s dump test (set %s and %s after a discovery capture)\n", k8 ? "shore mask" : "water normal map",
                  k8 ? "FABLE2_K8_BIN" : "FABLE2_WDXN_BIN", k8 ? "FABLE2_K8_FC" : "FABLE2_WDXN_FC");
      continue;
    }
    uint32_t rfc[6] = {};
    if (std::sscanf(fcs, "%x,%x,%x,%x,%x,%x", &rfc[0], &rfc[1], &rfc[2], &rfc[3], &rfc[4], &rfc[5]) != 6) return 244;
    TextureFetch wt;
    if (WaterTextureFetch(rfc, kind, &wt) != FetchError::kNone) return 245;
    std::FILE* f = std::fopen(bin, "rb");
    if (!f) return 246;
    std::vector<uint8_t> bytes;
    uint8_t chunk[65536];
    for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(f);
    std::vector<uint8_t> decoded;
    const bool ok = k8 ? DecodeK8Level(bytes.data(), bytes.size(), wt.width, wt.height, wt.pitch_texels, wt.tiled, &decoded)
                       : DecodeDxnLevelRg8(bytes.data(), bytes.size(), wt.width, wt.height, wt.pitch_texels / 4, wt.tiled, wt.endian, &decoded);
    if (!ok || decoded.size() != size_t(wt.width) * wt.height * (k8 ? 1 : 2)) return 247;
    if (!k8) {
      // The real map's base level as the pass uploads it (ledger ruling 32): the BC5 rows a GPU reads are the float
      // decode of the dump, float for float, and rounded to nearest they are the bytes written below.
      LevelLayout l;
      l.pitch_blocks = wt.pitch_texels / 4;
      l.row_pitch_bytes = ((l.pitch_blocks + 31) & ~31u) * 16;
      l.width_blocks = (wt.width + 3) / 4;
      l.height_blocks = (wt.height + 3) / 4;
      const uint32_t row = UploadRowPitch(l.width_blocks, 16);
      std::vector<uint8_t> rows(size_t(row) * l.height_blocks, 0);
      std::vector<float> host, want;
      if (!WaterDxnUploadLevel(bytes.data(), bytes.size(), l, wt.tiled, wt.endian, rows.data(), row) ||
          !DecodeBc5Rows(rows.data(), row, wt.width, wt.height, &host) ||
          !DecodeDxnLevel(bytes.data(), bytes.size(), wt.width, wt.height, wt.pitch_texels / 4, wt.tiled, wt.endian, &want) || host != want) return 259;
      size_t fractions = 0;
      for (size_t i = 0; i < host.size(); ++i) {
        if (uint8_t(host[i] * 255.0f + 0.5f) != decoded[i]) return 259;
        fractions += host[i] * 255.0f != std::floor(host[i] * 255.0f);
      }
      std::printf("water normal map as uploaded (BC5 blocks, untiled, endian %u): %zu values equal to the float decode, float for float; %zu of them (%.1f%%) no whole 8-bit step\n",
                  wt.endian, host.size(), fractions, 100.0 * double(fractions) / double(host.size()));
    }
    const std::string outp = std::string(bin) + (k8 ? ".cpp.r8" : ".cpp.rg8");
    std::FILE* o = std::fopen(outp.c_str(), "wb");
    if (!o || std::fwrite(decoded.data(), 1, decoded.size(), o) != decoded.size()) return 248;
    std::fclose(o);
    std::printf("%s dump decoded: %ux%u, wrote %s\n", k8 ? "shore mask" : "water normal map", wt.width, wt.height, outp.c_str());
  }
  return 0;
}
