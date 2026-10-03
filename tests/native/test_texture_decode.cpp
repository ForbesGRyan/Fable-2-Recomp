// Synthetic standalone test; no game or GPU.
#include "../../src/native/render/texture_decode.h"
#include <cstring>
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
  return 0;
}
