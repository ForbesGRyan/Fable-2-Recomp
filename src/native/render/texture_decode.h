#pragma once

// Xenos 2D texture fetch constants -> decode parameters, and level untiling
// into host upload rows (pure: no SDK/GPU deps). Field layout:
// xenos::xe_gpu_texture_fetch_t, host-order dwords (the capture byte-swaps the
// device shadow). Level offsets for mips and packed tails come from the SDK
// (texture_cache.cpp); this header handles one level at a time.

#include <algorithm>
#include <cmath>
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
// renderer decodes, two it does not but the terrain reads in its vertex
// shaders: k_16 (24: the heightmap and the far colour tables) and k_DXN (49:
// the normal map; 4 x 4 blocks of 16 bytes), and the game's own targets as its
// later passes sample them (fog discovery, frame-map section 16):
// k_16_16_16_16_EXPAND (29: the scene colour before post, 8 bytes a texel) and
// k_24_8 / k_24_8_FLOAT (22, 23: depth), and k_1_5_5_5 (3: the pictures of the far
// woods' impostors, frame-map section 19; 2 bytes a texel). False for any other
// format. The product reaches this for k_DXN (terrain_patch.h,
// DecodeNormalMap) and, through DumpFetch, for k_1_5_5_5 (the impostors'
// pictures: ClayPass::ReadImpostorPicture).
inline bool DumpFormatInfo(uint32_t xenos_format, TexFormatInfo* fi) {
  if (xenos_format == 24 || xenos_format == 3) {
    *fi = {1, 2, 1};
  } else if (xenos_format == 49) {
    *fi = {4, 16, 4};
  } else if (xenos_format == 29) {
    *fi = {1, 8, 3};  // k_16_16_16_16_EXPAND: the scene colour the post chain reads
  } else if (xenos_format == 22 || xenos_format == 23) {
    *fi = {1, 4, 2};  // k_24_8, k_24_8_FLOAT: the scene depth and the shadow maps
  } else if (xenos_format == 7 || xenos_format == 54) {
    // k_2_10_10_10 and k_2_10_10_10_AS_16_16_16_16: the post chain's bright, blur and sun-layer
    // buffers as its passes sample them (post discovery, frame-map section 17): 4 bytes a texel.
    *fi = {1, 4, 2};
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

// Bytes of one texel of an uncompressed Xenos texture format (xenos::TextureFormat), 0 for a
// block-compressed, video or unknown format.
inline uint32_t TexelBytes(uint32_t xenos_format) {
  switch (xenos_format) {
    case 2: case 8: case 9: return 1;                       // k_8, k_8_A, k_8_B
    case 3: case 4: case 5: case 10: case 15: return 2;     // k_1_5_5_5, k_5_6_5, k_6_5_5, k_8_8, k_4_4_4_4
    case 24: case 27: case 30: return 2;                    // k_16, k_16_EXPAND, k_16_FLOAT
    case 6: case 7: case 14: case 16: case 17: return 4;    // k_8_8_8_8, k_2_10_10_10, k_8_8_8_8_A, k_10_11_11, k_11_11_10
    case 22: case 23: return 4;                             // k_24_8, k_24_8_FLOAT
    case 25: case 28: case 31: case 33: case 36: return 4;  // k_16_16 (plain, EXPAND, FLOAT), k_32, k_32_FLOAT
    case 26: case 29: case 32: case 34: case 37: return 8;  // k_16_16_16_16 (plain, EXPAND, FLOAT), k_32_32 (plain, FLOAT)
    case 35: case 38: return 16;                            // k_32_32_32_32, k_32_32_32_32_FLOAT
    default: return 0;
  }
}

// A 1D texture fetch constant (xenos::DataDimension::k1D: the fog tables tf4 and tf5 of
// frame-map section 16). Its width is the 24-bit field of dword 2 (xe_gpu_texture_fetch_t
// size_1d), not the 13 bits of a 2D texture.
inline bool Is1DFetch(const uint32_t fc[6]) { return (fc[0] & 3) == 2 && ((fc[5] >> 9) & 3) == 0; }
inline uint32_t Fetch1DWidth(const uint32_t fc[6]) { return (fc[2] & 0xFFFFFFu) + 1; }

// Bytes a 1D texture spans from its base address: its texels in one row. 0 when the constant is
// not a 1D texture, has no base address or its format has no texel size (TexelBytes).
inline uint64_t Fetch1DDumpBytes(const uint32_t fc[6]) {
  if (!Is1DFetch(fc) || (fc[1] & 0xFFFFF000u) == 0) return 0;
  return uint64_t(Fetch1DWidth(fc)) * TexelBytes(fc[1] & 0x3F);
}

// Whether a texture's base address is one the game's post chain resolves into (post discovery,
// frame-map section 17): the front buffer 0x1ECFF000 (the final picture, which the last pass
// blends over the next frame), and the block [0x12A00000, 0x12E00000) that holds the sun layer's,
// the downsample's, the bright pass's and the blurs' buffers (0x12AE1000 .. 0x12D9F000 in the
// captures of 2026-10-09). Their memory holds another picture each frame, and under
// --readback_resolve=full it is the picture the next pass samples.
constexpr bool PostBufferBase(uint32_t base) {
  return base == 0x1ECFF000u || (base >= 0x12A00000u && base < 0x12E00000u);
}

// Whether a discovery dump keeps a texture once per content and not once per address: the
// textures the game rewrites in place. Every 1D texture (the fog tables sit in a ring of three
// addresses and are rewritten each frame), the 2D targets of the formats added above (scene
// colour and depth, whose memory holds another picture each frame), any 2D texture at a post
// buffer's address (PostBufferBase), whatever its format, and a 2D texture of format 3 (k_1_5_5_5:
// the impostors' pictures, which the game renders again every frame; frame-map section 19).
inline bool DumpKeyedByContent(const uint32_t fc[6]) {
  if ((fc[0] & 3) != 2) return false;
  if (Is1DFetch(fc)) return true;
  if (((fc[5] >> 9) & 3) != 1) return false;
  const uint32_t format = fc[1] & 0x3F;
  return format == 29 || format == 22 || format == 23 || format == 3 || PostBufferBase(fc[1] & 0xFFFFF000u);
}

// Whether a discovery dump writes a texture at the row being written. An impostor's picture (a 2D
// texture of format 3) is written only at the row of the draw that shows it, an in-scene "verts"
// row (`scene_verts_row`): the game's copy passes that make the picture's lower levels sample its
// base first (capture native_discovery_20261011_074119: three "indx" rows a picture, slot 13,
// before the impostor's own row), and a base written at their row would stand beside levels that
// are not yet this picture's. At the impostor's row the base and its levels are read at one
// moment, the one at which a product would read them. Every other texture: at any row.
inline bool DumpWrittenAtRow(const uint32_t fc[6], bool scene_verts_row) {
  const bool picture = (fc[0] & 3) == 2 && ((fc[5] >> 9) & 3) == 1 && (fc[1] & 0x3F) == 3;
  return scene_verts_row || !picture;
}

// The budgets of a discovery capture's content-kept files (DumpKeyedByContent), by kind. A 1D table
// counts against the file count alone (it is 1.5 KB in practice), a 2D target against the same file
// count and the targets' bytes, and an impostor's picture (a 2D texture of format 3: fourteen a
// frame) against a file count and a byte budget OF ITS OWN: counted with the others, the pictures
// used up the 1,024 files in about 68 frames, and no fog table was dumped after that, with no
// impostor switch on (review of the whole branch, Minor 7). The pictures' budget is 32 frames of
// fourteen whole bases.
enum class ContentDumpKind : uint8_t { kTable, kTarget, kPicture };
// For a constant DumpKeyedByContent takes: a 1D texture is a table whatever its format.
inline ContentDumpKind ContentDumpKindOf(const uint32_t fc[6]) {
  if (Is1DFetch(fc)) return ContentDumpKind::kTable;
  return ((fc[5] >> 9) & 3) == 1 && (fc[1] & 0x3F) == 3 ? ContentDumpKind::kPicture : ContentDumpKind::kTarget;
}
struct ContentDumpUse {
  uint32_t files = 0;           // tables and targets written
  uint64_t bytes = 0;           // the targets' bytes
  uint32_t picture_files = 0;   // pictures written
  uint64_t picture_bytes = 0;
};
struct ContentDumpLimits {
  uint32_t files;
  uint64_t bytes;
  uint32_t picture_files;
  uint64_t picture_bytes;
};
inline constexpr ContentDumpLimits kContentDumpLimits = {1024, 512ull << 20, 448, 448ull * 131072ull};
// Whether a file of this kind may still be written (asked before the texture's bytes are known).
constexpr bool ContentDumpFileRoom(ContentDumpKind kind, const ContentDumpUse& use, const ContentDumpLimits& lim) {
  return kind == ContentDumpKind::kPicture ? use.picture_files < lim.picture_files : use.files < lim.files;
}
// Whether `bytes` of this kind may be written: room for the file, some bytes, and (a target, a picture) room in
// the kind's byte budget. No sum wraps.
constexpr bool ContentDumpHasRoom(ContentDumpKind kind, uint64_t bytes, const ContentDumpUse& use, const ContentDumpLimits& lim) {
  if (bytes == 0 || !ContentDumpFileRoom(kind, use, lim)) return false;
  if (kind == ContentDumpKind::kTable) return true;
  const uint64_t used = kind == ContentDumpKind::kPicture ? use.picture_bytes : use.bytes;
  const uint64_t budget = kind == ContentDumpKind::kPicture ? lim.picture_bytes : lim.bytes;
  return used <= budget && bytes <= budget - used;
}
// The use after a file of `bytes` of this kind was written.
constexpr ContentDumpUse ContentDumpCounted(ContentDumpKind kind, uint64_t bytes, ContentDumpUse use) {
  if (kind == ContentDumpKind::kPicture) {
    ++use.picture_files;
    use.picture_bytes += bytes;
  } else {
    ++use.files;
    if (kind == ContentDumpKind::kTarget) use.bytes += bytes;
  }
  return use;
}

// A 2D fetch constant of any format DumpFormatInfo has, as DecodeTextureFetch reads one (the same
// fields from the same bits); `out->format` is TexFormatFromXenos's (kUnsupported for a format the
// renderer does not decode) and `*fi` the format's block layout. False, with `out` and `fi`
// untouched, as DecodeTextureFetch is for a constant that is no 2D texture, has no base or a pitch
// below its width, and for a format without a layout. Discovery, and since sub-project 12 the
// pass's read of an impostor's picture (ClayPass::ReadImpostorPicture: the fetch of a record the
// capture already held to format 3, 256 x 256, tiled); no record or material is made from it, and
// no texture of the texture cache.
inline bool DumpFetch(const uint32_t fc[6], TextureFetch* out, TexFormatInfo* fi) {
  if ((fc[0] & 3) != 2 || ((fc[5] >> 9) & 3) != 1) return false;
  TexFormatInfo info;
  if (!DumpFormatInfo(fc[1] & 0x3F, &info)) return false;
  TextureFetch t;
  t.xenos_format = fc[1] & 0x3F;
  t.format = TexFormatFromXenos(t.xenos_format);
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
  if (t.base_phys == 0 || t.pitch_texels < t.width) return false;
  *out = t;
  *fi = info;
  return true;
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

// k_1_5_5_5 (Xenos format 3: the pictures of the far woods' impostors, frame-map section 19, "The pictures'
// format") -> RGBA8, width * height * 4 bytes row-major. NOT entered in TexFormatFromXenos (spec ruling 14): no
// record's material and no texture of the cache is made from it; its one caller is the impostor step. A texel is
// 16 bits at TiledOffset2D(x, y, pitch_texels, 1), or linear at y * ((pitch_texels + 31) & ~31) * 2 + x * 2 (the
// SDK's base level rule); after the GPU's endian swap (endian 1 exchanges the two bytes; any other value reads
// them as stored, as the SDK's load shader does): alpha in bit 15, three 5-bit fields below it, each field
// v -> (v * 255 + 15) / 31 (255 * v / 31 rounded to nearest), alpha 0 or 255.
// k1555LowFieldChannel: the output channel of bits 0 to 4 (0 red, 2 blue; the middle field is green). It is 0 by
// the SDK's own reading [code]: its load shader for this format (texture_load_r5g5b5a1_b5g5r5a1_cs, disassembled:
// out/native-evidence/distant-trees/t7_load_shader_disasm.txt) moves bits 0 to 4 of the swapped word to bits 10
// to 14 of the host's B5G5R5A1 word, which are that format's red, and bits 10 to 14 to its blue; and its resolve
// (resolve_full_16bpp_cs, case 3) writes red + 32 * green + 1024 * blue + 32768 * alpha. The channels are the
// texture's, before the fetch constant's swizzle (TextureFetch::swizzle), which the sampler applies as for any
// texture: the impostors' fetch names (2, 1, 0, 3) in the captures so far, so the shader's red is channel 2.
// False, with out_rgba left empty, for a null source or output, an empty image, one above the 8,192 texels a
// fetch constant can name, a pitch below the width, or a texel outside src_size (nothing is read past it). One
// level: a caller gives a lower level's own bytes, size and pitch.
inline constexpr uint32_t k1555LowFieldChannel = 0;
inline bool Decode1555Level(const uint8_t* src, size_t src_size, uint32_t width, uint32_t height,
                            uint32_t pitch_texels, bool tiled, uint32_t endian, std::vector<uint8_t>* out_rgba) {
  if (!out_rgba) return false;
  out_rgba->clear();
  if (!src || width == 0 || height == 0 || width > 8192 || height > 8192 || pitch_texels < width) return false;
  const uint64_t row_pitch_bytes = uint64_t((pitch_texels + 31) & ~31u) * 2;
  // Before anything is allocated (review of phase 12a, Minor 5): the image's texels are that many distinct
  // two-byte places of the source in either layout, so a source smaller than them cannot hold it. (The exact
  // bound is each texel's own, below; without this a constant naming 8,192 x 8,192 over a short source would
  // allocate 268 MB and then fail.)
  if (uint64_t(width) * height * 2 > src_size) return false;
  std::vector<uint8_t> out(size_t(width) * height * 4);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const uint64_t o = tiled ? uint64_t(uint32_t(capture::TiledOffset2D(int32_t(x), int32_t(y), pitch_texels, 1)))
                               : uint64_t(y) * row_pitch_bytes + uint64_t(x) * 2;
      if (o + 2 > src_size) return false;
      const uint32_t word = endian == 1 ? uint32_t(src[o + 1]) | (uint32_t(src[o]) << 8)
                                        : uint32_t(src[o]) | (uint32_t(src[o + 1]) << 8);
      uint8_t* p = &out[(size_t(y) * width + x) * 4];
      p[k1555LowFieldChannel] = uint8_t(((word & 31) * 255 + 15) / 31);
      p[1] = uint8_t((((word >> 5) & 31) * 255 + 15) / 31);
      p[2 - k1555LowFieldChannel] = uint8_t((((word >> 10) & 31) * 255 + 15) / 31);
      p[3] = (word & 0x8000) ? 255 : 0;
    }
  }
  *out_rgba = std::move(out);
  return true;
}

// The value (0..255, not rounded) of texel i (row-major, 0..15) of an 8-byte BC4 block as the GPU reads it: two
// end values a0 and a1, then sixteen 3-bit indices from the low end of the following 48 bits. Index 0 is a0 and 1
// is a1; with a0 > a1 the indices 2 to 7 are the six steps of a seventh from a0 to a1, else 2 to 5 are the four
// steps of a fifth, 6 is 0 and 7 is 255 (the alpha block of DXT5).
inline float Bc4Value(const uint8_t blk[8], uint32_t i) {
  const uint32_t a0 = blk[0], a1 = blk[1];
  uint64_t bits = 0;
  for (uint32_t b = 0; b < 6; ++b) bits |= uint64_t(blk[2 + b]) << (8 * b);
  const uint32_t k = uint32_t(bits >> (3 * (i & 15))) & 7;
  if (k == 0) return float(a0);
  if (k == 1) return float(a1);
  if (a0 > a1) return float((8 - k) * a0 + (k - 1) * a1) / 7.0f;
  if (k < 6) return float((6 - k) * a0 + (k - 1) * a1) / 5.0f;
  return k == 6 ? 0.0f : 255.0f;
}

// One texel of a k_DXN level (Xenos format 49; the terrain's normal map, tf17 of the layer vertex shaders) as its
// two values in 0..1. A 4 x 4 block is 16 bytes: after the GPU's endian swap, the BC4 block of x and then that of y.
// Source: the SDK presents k_DXN as BC5 through its plain 128-bits-per-block loader, which only swaps the bytes,
// with the host swizzle RGGG, so the shader's x is the first block and its y the second
// (thirdparty/rexglue-sdk/src/graphics/d3d12/texture_cache.cpp, the k_DXN row of the host format table); checked on
// the dump of the normal map against the heightmap's slopes (frame-map section 13, "Terrain"). The values are the
// palette entries as fractions (Bc4Value), not rounded to 8 bits. Blocks sit like DXT5's: tiled at
// TiledOffset2D(bx, by, pitch_blocks, 4) or linear in rows of pitch_blocks aligned to 32 blocks times 16 bytes
// (pitch_blocks: the fetch pitch in blocks, at least the level's width). False, with out_xy untouched, for a null
// source, a texel outside the level or a block that does not lie inside src_size (nothing is read past it).
inline bool DecodeDxnTexel(const uint8_t* src, size_t src_size, uint32_t width, uint32_t height, uint32_t pitch_blocks,
                           bool tiled, uint32_t endian, uint32_t x, uint32_t y, float out_xy[2]) {
  if (!src || x >= width || y >= height) return false;
  const uint32_t pitch = std::max(pitch_blocks, (width + 3) / 4);
  const uint32_t bx = x / 4, by = y / 4;
  const uint64_t o = tiled ? uint64_t(uint32_t(capture::TiledOffset2D(int32_t(bx), int32_t(by), pitch, 4)))
                           : uint64_t(by) * (uint64_t((pitch + 31) & ~31u) * 16) + uint64_t(bx) * 16;
  if (o + 16 > src_size) return false;
  uint8_t blk[16];
  CopySwapped(blk, src + o, 16, endian);
  const uint32_t i = (y & 3) * 4 + (x & 3);
  out_xy[0] = Bc4Value(blk, i) / 255.0f;
  out_xy[1] = Bc4Value(blk + 8, i) / 255.0f;
  return true;
}

// A whole k_DXN level: width * height pairs (x, y), row-major, each as DecodeDxnTexel gives it. False, with
// out_xy left empty, when any texel cannot be read. The clay pass reads single texels (the terrain grid's
// points); this is for tools and tests.
inline bool DecodeDxnLevel(const uint8_t* src, size_t src_size, uint32_t width, uint32_t height, uint32_t pitch_blocks,
                           bool tiled, uint32_t endian, std::vector<float>* out_xy) {
  if (!out_xy) return false;
  out_xy->clear();
  if (!src || width == 0 || height == 0) return false;
  std::vector<float> out(size_t(width) * height * 2);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      if (!DecodeDxnTexel(src, src_size, width, height, pitch_blocks, tiled, endian, x, y,
                          &out[(size_t(y) * width + x) * 2])) {
        return false;
      }
    }
  }
  *out_xy = std::move(out);
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

// The rotation tile of the game's shadow mask (frame-map section 15, "Mask draws": tf14 of pixel shader
// 0x0F99810F072BF0FC). A k_8_8 texture of 32 x 32 texels, linear, pitch 128 texels (256 bytes a row), point
// filtered and repeated; (a, b) = 2 * texel.xy - 1 is a unit vector, the rotation of the pixel's twelve
// near taps. The mask draws are not captured by the product, so no record carries its fetch constant: the
// address is the one of every mask row of five captures in four runs (fetch constant 01000002 1A53B04A
// 0003E01F 01001690 00000000 00000218; the first 8,192 bytes equal in all five), and the contents are
// checked each time they are read (below): another address in another session shows as a failed check,
// and the taps are then drawn unrotated and the frame counted.
// Channel order, from the SDK's load path of the format (not from the lengths: both orders give unit
// vectors, and the wrong one is a mirrored rotation):
//   - the fetch constant's endian field (dword 1 bits 6-7) is 1, the 8-in-16 swap; the 16-bit load shader
//     exchanges the two bytes of every texel (thirdparty/rexglue-sdk/src/graphics/shaders/bytecode/
//     d3d12_5_1/texture_load_16bpb_cs.h lines 121 to 126: `ieq ..., l(1)`, then << 8 & 0xff00ff00 and
//     >> 8 & 0x00ff00ff), into DXGI_FORMAT_R8G8_UNORM (src/graphics/d3d12/texture_cache.cpp lines 127 to
//     130: k_8_8, kLoadShaderIndex16bpb, host swizzle RGGG), whose R is the first byte in memory: so the
//     host's R is the SECOND byte of the texel as it lies in guest memory and G the first;
//   - the fetch constant's swizzle (dword 3 bits 1-12 = 0xB48: x <- 0, y <- 1, z <- 5, w <- 5) through the
//     host format's swizzle RGGG (src/graphics/pipeline/texture/cache.cpp lines 352 to 366,
//     GuestToHostSwizzle) gives the shader x = R, y = G.
// So tf14.x is the texel's second stored byte and tf14.y its first, each / 255.
inline constexpr uint32_t kRotationTilePhys = 0x1A53B000u;
inline constexpr uint32_t kRotationTileSize = 32;
inline constexpr uint32_t kRotationTileRowBytes = 256;
// Guest bytes the tile's 32 rows span, and the bytes of the decoded tile (two a texel: x, y).
inline constexpr uint32_t kRotationTileGuestBytes = (kRotationTileSize - 1) * kRotationTileRowBytes + kRotationTileSize * 2;
inline constexpr uint32_t kRotationTileBytes = kRotationTileSize * kRotationTileSize * 2;
// How far the length of a texel's vector may be from 1 (the game's tile: 0.989 to 1.010).
inline constexpr float kRotationUnitTolerance = 0.05f;

// The tile as the shader samples it: out[2 * (y * 32 + x)] = tf14.x and the byte after it tf14.y of texel
// (x, y), as bytes (value / 255). False, with `out` untouched, when the source is missing or too small, or
// when a texel is not a unit vector within kRotationUnitTolerance: then the bytes are not the tile.
inline bool DecodeRotationTile(const uint8_t* src, size_t src_size, uint8_t out[kRotationTileBytes]) {
  if (!src || !out || src_size < kRotationTileGuestBytes) return false;
  uint8_t tile[kRotationTileBytes];
  for (uint32_t y = 0; y < kRotationTileSize; ++y) {
    for (uint32_t x = 0; x < kRotationTileSize; ++x) {
      const uint8_t* texel = src + size_t(y) * kRotationTileRowBytes + size_t(x) * 2;
      const uint8_t tx = texel[1], ty = texel[0];  // the 8-in-16 swap: the second stored byte is the host's R
      const float a = 2.0f * float(tx) / 255.0f - 1.0f, b = 2.0f * float(ty) / 255.0f - 1.0f;
      const float length = std::sqrt(a * a + b * b);
      if (!(std::fabs(length - 1.0f) <= kRotationUnitTolerance)) return false;
      tile[2 * (y * kRotationTileSize + x)] = tx;
      tile[2 * (y * kRotationTileSize + x) + 1] = ty;
    }
  }
  std::memcpy(out, tile, sizeof(tile));
  return true;
}

}  // namespace fable2::native::render
