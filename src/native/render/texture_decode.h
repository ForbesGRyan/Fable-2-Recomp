#pragma once

// Xenos 2D texture fetch constants -> decode parameters, and level untiling
// into host upload rows (pure: no SDK/GPU deps). Field layout:
// xenos::xe_gpu_texture_fetch_t, host-order dwords (the capture byte-swaps the
// device shadow). Level offsets for mips and packed tails come from the SDK
// (texture_cache.cpp); this header handles one level at a time.

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "../capture/xenos_tiling.h"

namespace fable2::native::render {

enum class TexFormat : uint8_t { kUnsupported, kDxt1, kDxt23, kDxt45, k8888 };

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
    default: return TexFormat::kUnsupported;
  }
}

inline TexFormatInfo FormatInfo(TexFormat f) {
  switch (f) {
    case TexFormat::kDxt1: return {4, 8, 3};
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

// The base level of a linear texture (rows padded to 256 bytes).
inline LevelLayout BaseLevelLinear(const TextureFetch& t) {
  const TexFormatInfo fi = FormatInfo(t.format);
  LevelLayout l;
  l.pitch_blocks = t.pitch_texels / fi.block;
  l.row_pitch_bytes = (l.pitch_blocks * fi.bytes_per_block + 255) & ~255u;
  l.width_blocks = (t.width + fi.block - 1) / fi.block;
  l.height_blocks = (t.height + fi.block - 1) / fi.block;
  return l;
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
