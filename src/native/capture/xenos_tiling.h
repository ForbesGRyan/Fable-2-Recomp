#pragma once

// Xenos tiled-texture addressing (pure: no SDK/GPU deps).

#include <cstdint>

namespace fable2::native::capture {

// xenia's texture_util::GetTiledOffset2D (UModel's Xbox 360 untiling). x, y and
// pitch are in blocks (texels for uncompressed formats, 4x4 blocks for DXT);
// returns the byte offset of block (x, y) from the level's base.
inline int32_t TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2) {
  pitch = (pitch + 31) & ~31u;
  const int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bytes_per_block_log2 + 7);
  const int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  const int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

}  // namespace fable2::native::capture
