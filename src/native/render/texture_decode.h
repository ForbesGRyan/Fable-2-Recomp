#pragma once

// Xenos 2D texture fetch constants -> decode parameters, and level untiling
// into host upload rows (pure: no SDK/GPU deps). Field layout:
// xenos::xe_gpu_texture_fetch_t, host-order dwords (the capture byte-swaps the
// device shadow). Level offsets for mips and packed tails come from the SDK
// (texture_cache.cpp); this header handles one level at a time.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "../capture/xenos_tiling.h"

namespace fable2::native::render {

enum class TexFormat : uint8_t { kUnsupported, kDxt1, kDxt23, kDxt45, k8888, kDxt3a };

struct TexFormatInfo {
  uint32_t block = 1;            // texels per block edge
  uint32_t bytes_per_block = 0;
  uint32_t bpb_log2 = 0;
};

inline TexFormat TexFormatFromXenos(uint32_t xenos_format) {
  switch (xenos_format) {
    case 18: return TexFormat::kDxt1;   // k_DXT1
    case 19: return TexFormat::kDxt23;  // k_DXT2_3
    case 20: return TexFormat::kDxt45;  // k_DXT4_5
    case 6: return TexFormat::k8888;    // k_8_8_8_8
    case 58: return TexFormat::kDxt3a;  // k_DXT3A: no BC equivalent, decoded to R8 (DecodeDxt3aLevel)
    default: return TexFormat::kUnsupported;
  }
}

inline TexFormatInfo FormatInfo(TexFormat f) {
  switch (f) {
    case TexFormat::kDxt1:
    case TexFormat::kDxt3a: return {4, 8, 3};
    case TexFormat::kDxt23:
    case TexFormat::kDxt45: return {4, 16, 4};
    case TexFormat::k8888: return {1, 4, 2};
    default: return {};
  }
}

enum class FetchError : uint8_t { kNone, kNotTexture, kNot2D, kFormat, kSize };

struct TextureFetch {
  uint32_t base_phys = 0, mip_phys = 0;  // GPU physical, 4 KB aligned
  uint32_t width = 0, height = 0;
  uint32_t pitch_texels = 0;             // fetch pitch field << 5
  uint32_t xenos_format = 0;
  TexFormat format = TexFormat::kUnsupported;
  uint32_t endian = 0;                   // xenos::Endian
  bool tiled = false;
  bool packed_mips = false;
  uint32_t mip_min = 0, mip_max = 0;
  uint8_t swizzle[4] = {0, 1, 2, 3};     // 0-3 channel, 4 = 0.0, 5 = 1.0
  uint8_t clamp_x = 0, clamp_y = 0;      // xenos::ClampMode
  bool linear_filter = false;            // mag filter linear
};

inline FetchError DecodeTextureFetch(const uint32_t fc[6], TextureFetch* out) {
  if ((fc[0] & 3) != 2) return FetchError::kNotTexture;
  if (((fc[5] >> 9) & 3) != 1) return FetchError::kNot2D;  // xenos::DataDimension::k2DOrStacked
  TextureFetch t;
  t.xenos_format = fc[1] & 0x3F;
  t.format = TexFormatFromXenos(t.xenos_format);
  if (t.format == TexFormat::kUnsupported) return FetchError::kFormat;
  t.endian = (fc[1] >> 6) & 3;
  t.base_phys = fc[1] & 0xFFFFF000u;
  t.mip_phys = fc[5] & 0xFFFFF000u;
  t.width = (fc[2] & 0x1FFF) + 1;
  t.height = ((fc[2] >> 13) & 0x1FFF) + 1;
  t.pitch_texels = ((fc[0] >> 22) & 0x1FF) << 5;
  t.tiled = (fc[0] >> 31) != 0;
  t.packed_mips = ((fc[5] >> 11) & 1) != 0;
  t.mip_min = (fc[4] >> 2) & 0xF;
  t.mip_max = (fc[4] >> 6) & 0xF;
  for (int i = 0; i < 4; ++i) {
    const uint8_t s = uint8_t((fc[3] >> (1 + 3 * i)) & 7);
    t.swizzle[i] = s <= 5 ? s : 4;
  }
  t.clamp_x = uint8_t((fc[0] >> 10) & 7);
  t.clamp_y = uint8_t((fc[0] >> 13) & 7);
  t.linear_filter = ((fc[3] >> 19) & 3) == 1;
  if (t.base_phys == 0 || t.pitch_texels < t.width) return FetchError::kSize;
  *out = t;
  return FetchError::kNone;
}

// One level's storage. Offsets and pitches are in blocks; x/y_blocks is the
// level's origin inside a packed mip tail (0 otherwise).
struct LevelLayout {
  uint32_t offset = 0;           // bytes from the level's base address
  uint32_t pitch_blocks = 0;     // tiled addressing pitch
  uint32_t row_pitch_bytes = 0;  // linear addressing
  uint32_t width_blocks = 0, height_blocks = 0;
  uint32_t x_blocks = 0, y_blocks = 0;
};

inline uint32_t LevelExtent(uint32_t base, uint32_t level) { return std::max(1u, base >> level); }

// Levels to upload: the base plus mips up to mip_max when a mip address exists.
inline uint32_t LevelCount(const TextureFetch& t) {
  if (!t.mip_phys) return 1;
  uint32_t log2 = 0;
  for (uint32_t m = std::max(t.width, t.height); m > 1; m >>= 1) ++log2;
  return std::min(t.mip_max, log2) + 1;
}

inline uint32_t UploadRowPitch(uint32_t width_blocks, uint32_t bytes_per_block) {
  return (width_blocks * bytes_per_block + 255) & ~255u;
}

// The base level of a linear texture. Row pitch as the SDK's
// texture_util::GetGuestTextureLayout computes it for the base level
// (thirdparty/rexglue-sdk/src/graphics/pipeline/texture/util.cpp): the fetch
// pitch in blocks aligned to 32 blocks (xenos::kTextureTileWidthHeight). Only
// mip levels get the extra 256-byte linear row alignment.
inline LevelLayout BaseLevelLinear(const TextureFetch& t) {
  const TexFormatInfo fi = FormatInfo(t.format);
  LevelLayout l;
  l.pitch_blocks = t.pitch_texels / fi.block;
  l.row_pitch_bytes = ((l.pitch_blocks + 31) & ~31u) * fi.bytes_per_block;
  l.width_blocks = (t.width + fi.block - 1) / fi.block;
  l.height_blocks = (t.height + fi.block - 1) / fi.block;
  return l;
}

// The SDK's texture_util::GetTiledAddressUpperBound2D: bytes a tiled level of
// right x bottom blocks spans from its base. The origin of the 32 x 32 tile
// holding the last block, plus that tile's extent (0xA00 for 1-byte blocks,
// 0xC00 for 2-byte ones, 0x400 << log2 above).
inline uint32_t TiledUpperBound2D(uint32_t right, uint32_t bottom, uint32_t pitch_blocks, uint32_t bpb_log2) {
  if (!right || !bottom) return 0;
  const uint32_t origin = uint32_t(capture::TiledOffset2D(int32_t((right - 1) & ~31u), int32_t((bottom - 1) & ~31u),
                                                          pitch_blocks, bpb_log2));
  return origin + (bpb_log2 == 0 ? 0xA00u : bpb_log2 == 1 ? 0xC00u : 0x400u << bpb_log2);
}

// Block layout of the formats a discovery dump writes in full: the ones the
// renderer decodes, and two it does not but the terrain reads in its vertex
// shaders: k_16 (24: the heightmap and the far colour tables) and k_DXN (49:
// the normal map; 4 x 4 blocks of 16 bytes). False for any other format.
inline bool DumpFormatInfo(uint32_t xenos_format, TexFormatInfo* fi) {
  if (xenos_format == 24) {
    *fi = {1, 2, 1};
  } else if (xenos_format == 49) {
    *fi = {4, 16, 4};
  } else {
    *fi = FormatInfo(TexFormatFromXenos(xenos_format));
  }
  return fi->bytes_per_block != 0;
}

// Bytes the base level of a 2D texture fetch constant spans from its base
// address: the tiled upper bound, or the linear rows (row pitch by the SDK's
// base-level rule, as BaseLevelLinear). 0 when the constant is not a 2D
// texture, has no base address or a pitch below its width, or its format has
// no layout here (DumpFormatInfo).
inline uint64_t BaseLevelDumpBytes(const uint32_t fc[6]) {
  if ((fc[0] & 3) != 2 || ((fc[5] >> 9) & 3) != 1) return 0;
  TexFormatInfo fi;
  if (!DumpFormatInfo(fc[1] & 0x3F, &fi)) return 0;
  const uint32_t width = (fc[2] & 0x1FFF) + 1, height = ((fc[2] >> 13) & 0x1FFF) + 1;
  const uint32_t pitch_texels = ((fc[0] >> 22) & 0x1FF) << 5;
  if ((fc[1] & 0xFFFFF000u) == 0 || pitch_texels < width) return 0;
  const uint32_t wb = (width + fi.block - 1) / fi.block, hb = (height + fi.block - 1) / fi.block;
  const uint32_t pitch_blocks = pitch_texels / fi.block;
  if ((fc[0] >> 31) != 0) return TiledUpperBound2D(wb, hb, pitch_blocks, fi.bpb_log2);
  return uint64_t(((pitch_blocks + 31) & ~31u) * fi.bytes_per_block) * (hb - 1) + uint64_t(wb) * fi.bytes_per_block;
}

// Copies n bytes applying the GPU's endian swap (xenos::Endian): the result is
// what the GPU reads, i.e. little-endian DXT words / RGBA8 bytes.
inline void CopySwapped(uint8_t* d, const uint8_t* s, uint32_t n, uint32_t endian) {
  switch (endian) {
    case 1:  // 8in16
      for (uint32_t i = 0; i + 1 < n; i += 2) { d[i] = s[i + 1]; d[i + 1] = s[i]; }
      break;
    case 2:  // 8in32
      for (uint32_t i = 0; i + 3 < n; i += 4) {
        d[i] = s[i + 3]; d[i + 1] = s[i + 2]; d[i + 2] = s[i + 1]; d[i + 3] = s[i];
      }
      break;
    case 3:  // 16in32
      for (uint32_t i = 0; i + 3 < n; i += 4) {
        d[i] = s[i + 2]; d[i + 1] = s[i + 3]; d[i + 2] = s[i]; d[i + 3] = s[i + 1];
      }
      break;
    default:
      std::memcpy(d, s, n);
  }
}

// Writes the level's blocks as rows of dst_row_pitch bytes (one row per block
// row). False if any block lies outside src_size (nothing is read past it).
inline bool UntileLevel(const uint8_t* src, size_t src_size, const LevelLayout& l, const TexFormatInfo& fi,
                        bool tiled, uint32_t endian, uint8_t* dst, uint32_t dst_row_pitch) {
  if (!src || !dst || fi.bytes_per_block == 0) return false;
  for (uint32_t by = 0; by < l.height_blocks; ++by) {
    for (uint32_t bx = 0; bx < l.width_blocks; ++bx) {
      const uint32_t x = bx + l.x_blocks, y = by + l.y_blocks;
      uint64_t o = tiled ? uint64_t(uint32_t(capture::TiledOffset2D(int32_t(x), int32_t(y), l.pitch_blocks,
                                                                    fi.bpb_log2)))
                         : uint64_t(y) * l.row_pitch_bytes + uint64_t(x) * fi.bytes_per_block;
      o += l.offset;
      if (o + fi.bytes_per_block > src_size) return false;
      CopySwapped(dst + size_t(by) * dst_row_pitch + size_t(bx) * fi.bytes_per_block, src + o,
                  fi.bytes_per_block, endian);
    }
  }
  return true;
}

// k_DXT3A (Xenos format 58) -> an 8-bit single-channel image, width * height bytes row-major. The format is the
// explicit-alpha half of a DXT3 block: 8 bytes per 4 x 4 block, four rows of 16 bits, texel i of a row in nibble i
// (low nibble first), after the GPU's endian swap of the 8-byte block (SDK:
// thirdparty/rexglue-sdk/src/graphics/pipeline/texture/conversion.cpp:80-85 ConvertTexelDXT3AToDXT3 and the
// texture_load_dxt3a compute shader). Each 4-bit value v becomes v * 17. Blocks sit like DXT1's: tiled at
// TiledOffset2D(bx, by, pitch_blocks, 3) or linear with rows of pitch_blocks aligned to 32 blocks (the SDK's base
// level rule) times 8 bytes. pitch_blocks is the fetch pitch in blocks (any value; it is aligned to 32 here as
// the SDK aligns it). False, with out_r8 left empty, for a null source, an empty image or when any block the image
// needs lies outside src_size (nothing is read past it). Only the base level is decoded.
inline bool DecodeDxt3aLevel(const uint8_t* src, size_t src_size, uint32_t width, uint32_t height,
                             uint32_t pitch_blocks, bool tiled, uint32_t endian, std::vector<uint8_t>* out_r8) {
  if (!out_r8) return false;
  out_r8->clear();
  if (!src || width == 0 || height == 0) return false;
  const uint32_t wb = (width + 3) / 4, hb = (height + 3) / 4;
  const uint64_t row_pitch_bytes = uint64_t((std::max(pitch_blocks, wb) + 31) & ~31u) * 8;
  std::vector<uint8_t> out(size_t(width) * height);
  for (uint32_t by = 0; by < hb; ++by) {
    for (uint32_t bx = 0; bx < wb; ++bx) {
      const uint64_t o = tiled ? uint64_t(uint32_t(capture::TiledOffset2D(int32_t(bx), int32_t(by),
                                                                          std::max(pitch_blocks, wb), 3)))
                               : uint64_t(by) * row_pitch_bytes + uint64_t(bx) * 8;
      if (o + 8 > src_size) return false;
      uint8_t blk[8];
      CopySwapped(blk, src + o, 8, endian);
      for (uint32_t r = 0; r < 4 && by * 4 + r < height; ++r) {
        const uint32_t row = uint32_t(blk[2 * r]) | (uint32_t(blk[2 * r + 1]) << 8);
        for (uint32_t i = 0; i < 4 && bx * 4 + i < width; ++i) {
          out[size_t(by * 4 + r) * width + bx * 4 + i] = uint8_t(((row >> (4 * i)) & 15) * 17);
        }
      }
    }
  }
  *out_r8 = std::move(out);
  return true;
}

inline bool IsMirror(uint8_t clamp) { return clamp == 1 || clamp == 3 || clamp == 5 || clamp == 7; }

// Static sampler s<i>: bit 0 linear (else point), bit 1 clamp (else wrap).
// Mirror modes use wrap (counted by the caller).
inline uint32_t SamplerIndex(const TextureFetch& t) {
  const bool wrap = t.clamp_x <= 1;
  return (t.linear_filter ? 1u : 0u) | (wrap ? 0u : 2u);
}

// Hash of the fields that define the texture's contents and layout; clamp,
// filter and LOD fields (sampler state) are masked out.
inline uint64_t TextureIdentity(const uint32_t fc[6]) {
  const uint32_t masked[6] = {fc[0] & ~(0x1FFu << 10), fc[1], fc[2], fc[3] & 0x7FFFFu, fc[4] & (0xFFu << 2),
                              fc[5] & ~0x1FFu};
  uint64_t h = 1469598103934665603ull;
  for (uint32_t v : masked) h = (h ^ v) * 1099511628211ull;
  return h;
}

}  // namespace fable2::native::render
