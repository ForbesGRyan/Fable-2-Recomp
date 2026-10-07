// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/texture_decode.h"
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
  return 0;
}
