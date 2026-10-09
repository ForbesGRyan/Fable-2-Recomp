#pragma once

// The frame's fog block and sky block (pure): what the native pass needs of the
// game's distance fog and of its sky draw (frame-map section 16, "Fog and sky";
// docs/native-renderer/lighting-formulas.md, "Fog and sky"). The game fogs in
// its scene pixel shaders: six pixel registers that hold one value a frame and
// two 1D tables it rewrites in guest memory every frame, the in-scatter colour
// by the angle to an axis (fetch slot 4, "tf4") and the transmittance by
// distance (slot 5, "tf5"). The sky is one draw whose pixel shader reads three
// registers more and two textures.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "position_decode.h"

namespace fable2::native::capture {

// amount: pixel c28.xy: how much of the fog is applied, and the in-scatter's scale.
// axis: c66.xyz, the unit axis the in-scatter's angle is measured to (sun-like).
// range: c67 = (the distance the fog begins at, the sky's path height, the slope
//   of the blend from near_colour to the in-scatter table, the transmittance
//   table's distance scale); (19.9, 1900, 1, 0.0005) at the bridge.
// near_colour: c68.rgb, the in-scatter colour at the fog's begin.
// ext: c64.rgb and c65.rgb, the two extinctions of the sky's
//   exp(-(l1 * c64 + l2 * c65)); the fog itself does not read them.
// scatter: tf4 decoded, 64 texels r g b (texel i's centre at (i + 0.5) / 64).
// transmit: tf5 decoded after its swizzle, 256 texels r g b.
// The trees' vertex form (ruling B: T = tf5, add = (1 - T) * tf4(angle) * c8.x,
// colour * T * c49 + add) reads the same two tables (the game binds tf5 and tf4
// themselves as its tf16 and tf17: 28 of 28 captured frames) and the same axis
// and range (vertex c110, c111 are the twins of pixel c66, c67); what it has
// beyond them is per draw and not here: vertex c8.x (1 in every captured row),
// which is the tree draw's own vertex register, and c49, which the light inputs
// carry already. Its eye is the light block's.
// An invalid block is all zero: nothing of it may be used.
struct FogBlock {
  float amount[2];        // c28.xy
  float axis[3];          // c66.xyz
  float range[4];         // c67
  float near_colour[3];   // c68.rgb
  float ext[2][3];        // c64.rgb, c65.rgb (the sky's)
  float scatter[64][3];   // tf4 decoded
  float transmit[256][3]; // tf5 decoded, swizzled
  bool valid;
};
// 978 floats and the flag: one is copied into each frame's scene (FrameScene::fog).
static_assert(sizeof(FogBlock) == 3916, "FogBlock grew or shrank: the documents state its size");

// c31x, c46x: pixel c31.x and c46.x at the sky draw, the two path lengths at the
//   zenith (150 and 100 in every captured row). c47x: c47.x, the blend from the
//   first sky texture to the second (0 in every captured row).
// fetch: the fetch constants of slots 13 and 14 at the sky draw, as the device
//   held them (host-order dwords), whatever they are: a texture the pass cannot
//   decode is the pass's matter ("sky-texture"), not the block's.
// The rest of the sky's arithmetic is the fog block's (ext, axis, range[1],
// scatter). An invalid block is all zero.
struct SkyBlock {
  float c31x, c46x, c47x;
  uint32_t fetch[2][6];   // tf13, tf14
  bool valid;
};
static_assert(sizeof(SkyBlock) == 64, "SkyBlock grew or shrank: the documents state its size");

// The pixel registers MakeFogBlock reads, in the order of its `regs`, and the
// sky's three. None of them is a register a LoadAluConstants call can name
// (light_block.h, kBufferLoadRanges: pixel c0..c7 and c80..c115): the device's
// bank holds their value at any scene draw.
inline constexpr uint32_t kFogBlockRegisters[6] = {28, 64, 65, 66, 67, 68};
inline constexpr uint32_t kSkyBlockRegisters[3] = {31, 46, 47};
// The texture fetch slots of the two tables (tf4, tf5) and of the sky's two textures.
inline constexpr uint32_t kFogTableSlots[2] = {4, 5};
inline constexpr uint32_t kSkyTextureSlots[2] = {13, 14};
// The bytes of the two tables from their fetch constants' base addresses:
// 64 texels of four half floats, 256 texels of four bytes.
inline constexpr size_t kFogScatterBytes = 512;
inline constexpr size_t kFogTransmitBytes = 1024;
// The sky's pixel shader (light_formulas.SKY_SHADER): the sky draw is the call of
// the indexed builder 0x82217EE8 inside the main scene that runs it.
inline constexpr uint64_t kSkyPixelShader = 0xF7601FBE485936FDull;

// Reason strings of the fog and the sky (the plan's): a frame with the fog on
// that has no fog block; one that has no sky block.
inline constexpr const char* kFogBlockReason = "fog-block";
inline constexpr const char* kSkyBlockReason = "sky-block";

// What a table's fetch constant must be for its bytes to be read as MakeFogBlock
// reads them. Constant over the 28 frames of the four captures of frame-map
// section 16, but for the address.
struct FogTablePin {
  uint32_t width;    // texels (dword 2, the 24 bits of a 1D texture's size)
  uint32_t format;   // xenos::TextureFormat (dword 1 bits 0 to 5)
  uint32_t endian;   // xenos::Endian (dword 1 bits 6, 7)
  uint32_t swizzle;  // dword 3 bits 1 to 12, three bits a component
  uint32_t signs;    // dword 0 bits 2 to 9, two bits a component
  uint32_t num_format;  // dword 3 bit 0 (fraction or integer), judged with the format
};
// tf4: 64 texels of k_16_16_16_16_EXPAND (29; half floats as the SDK presents the
// format), endian 8-in-16, swizzle 0x688 (r g b a as stored), all four sign
// fields 1. tf5: 256 texels of k_8_8_8_8 (6), endian 8-in-32, swizzle 0x60A (a
// texel is stored a r g b), sign fields 0. The number format bit is 1 in tf4's
// constant and 0 in tf5's (dword 3 = 0x00280D11 and 0x00280C14 in every captured
// row); whether the emulator reads the bit was not established: it is pinned
// because the decode was only ever seen with these values.
inline constexpr FogTablePin kFogScatterPin = {64, 29, 1, 0x688, 0x55, 1};
inline constexpr FogTablePin kFogTransmitPin = {256, 6, 2, 0x60A, 0x00, 0};

// What is wrong with one table, in the order the rules are tried.
enum class FogTableFault : uint8_t {
  kNone,
  kDimension,  // not a texture constant, or not a 1D texture
  kBase,       // no address
  kWidth,
  kFormat,
  kEndian,
  kSwizzle,
  kSign,
  kExpAdjust,  // an exponent adjust other than 0 (dword 3 bits 13 to 18)
  kClamp,      // not clamped to the edge along the table (dword 0 bits 10 to 12; 2 = clamp to edge)
  kFilter,     // magnification or minification not linear (dword 3 bits 19, 20 and 21, 22; 1 = linear)
  kMip,        // a highest mip level other than 0 (dword 4 bits 6 to 9): a table has one level
  kBytes,      // the bytes are missing or not the table's size (MakeFogBlock)
  kCount
};

// The fault of a table's fetch constant against its pin (never kBytes). A 1D
// texture has one axis: the clamps of the other two address nothing and are not
// pinned. A null constant is no texture.
inline FogTableFault FogTableFetchFault(const uint32_t fc[6], const FogTablePin& pin) {
  if (!fc || (fc[0] & 3) != 2 || ((fc[5] >> 9) & 3) != 0) return FogTableFault::kDimension;
  if ((fc[1] & 0xFFFFF000u) == 0) return FogTableFault::kBase;
  if ((fc[2] & 0xFFFFFFu) + 1 != pin.width) return FogTableFault::kWidth;
  if ((fc[1] & 0x3F) != pin.format || (fc[3] & 1) != pin.num_format) return FogTableFault::kFormat;
  if (((fc[1] >> 6) & 3) != pin.endian) return FogTableFault::kEndian;
  if (((fc[3] >> 1) & 0xFFF) != pin.swizzle) return FogTableFault::kSwizzle;
  if (((fc[0] >> 2) & 0xFF) != pin.signs) return FogTableFault::kSign;
  if (((fc[3] >> 13) & 0x3F) != 0) return FogTableFault::kExpAdjust;
  if (((fc[0] >> 10) & 7) != 2) return FogTableFault::kClamp;
  if (((fc[3] >> 19) & 3) != 1 || ((fc[3] >> 21) & 3) != 1) return FogTableFault::kFilter;
  if (((fc[4] >> 6) & 0xF) != 0) return FogTableFault::kMip;
  return FogTableFault::kNone;
}

// Why a frame has no fog block (FrameScene::fog_reason); kNone: it has one.
enum class FogBlockReason : uint8_t {
  kNone,
  kOff,        // the fog was not wanted this frame (the view, the light or the fog switch off): nothing was read
  kNoDraw,     // no main-scene colour draw of the frame ran a fogged pixel shader (a menu, a loading screen)
  kUnread,     // a register, a fetch constant or a table's memory could not be read
  kRegister,   // a register is not finite
  kAmount,     // c28.xy outside [0, 4]
  kAxis,       // c66.xyz is not a unit vector within 1e-3
  kMagnitude,  // a component the fog reads beyond kFogRegisterLimit
  kRange,      // c67: a start or a near slope below 0, or a distance scale not above 0
  // tf4's faults (FogTableFault, in its order), then tf5's.
  kScatterDimension, kScatterBase, kScatterWidth, kScatterFormat, kScatterEndian, kScatterSwizzle, kScatterSign,
  kScatterExpAdjust, kScatterClamp, kScatterFilter, kScatterMip, kScatterBytes,
  kTransmitDimension, kTransmitBase, kTransmitWidth, kTransmitFormat, kTransmitEndian, kTransmitSwizzle, kTransmitSign,
  kTransmitExpAdjust, kTransmitClamp, kTransmitFilter, kTransmitMip, kTransmitBytes,
  kScatterTexel,  // a texel of tf4 is not finite (tf5's are bytes: always numbers)
  kCount
};

// The block's reason for a fault of table 0 (tf4) or 1 (tf5).
inline constexpr FogBlockReason FogTableReason(int table, FogTableFault fault) {
  if (fault == FogTableFault::kNone || fault >= FogTableFault::kCount) return FogBlockReason::kNone;
  const int first = int(table == 0 ? FogBlockReason::kScatterDimension : FogBlockReason::kTransmitDimension);
  return FogBlockReason(first + int(fault) - 1);
}
static_assert(FogTableReason(0, FogTableFault::kBytes) == FogBlockReason::kScatterBytes &&
              FogTableReason(1, FogTableFault::kDimension) == FogBlockReason::kTransmitDimension &&
              FogTableReason(1, FogTableFault::kBytes) == FogBlockReason::kTransmitBytes);

inline const char* FogBlockReasonName(FogBlockReason r) {
  static constexpr const char* kNames[] = {
      "none", "off", "no-draw", "unread", "register", "amount", "axis", "magnitude", "range",
      "tf4-dimension", "tf4-base", "tf4-width", "tf4-format", "tf4-endian", "tf4-swizzle", "tf4-sign",
      "tf4-exp-adjust", "tf4-clamp", "tf4-filter", "tf4-mip", "tf4-bytes",
      "tf5-dimension", "tf5-base", "tf5-width", "tf5-format", "tf5-endian", "tf5-swizzle", "tf5-sign",
      "tf5-exp-adjust", "tf5-clamp", "tf5-filter", "tf5-mip", "tf5-bytes",
      "tf4-texel"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(FogBlockReason::kCount));
  return r < FogBlockReason::kCount ? kNames[size_t(r)] : "?";
}

// The largest magnitude a component of the six registers may have (review of
// plan Task 3, M2): of the fog's c67.x, .z, .w and c68.rgb here, and of the
// sky's numbers where the sky is decided (clay_logic.h, SkyRules). Generous for
// anything the game holds there (a start of 19.9, a path height of 1,900,
// colours that may be HDR), and small enough that no product of the fog's
// arithmetic overflows single precision: with every component the fog reads
// within it, a table texel within a half float's 65,504 and c28 within
// 4, the largest term is (1 - T) * c28.y * F <= 4 * 1e6, and a table coordinate
// c67.w * dD stays finite for any distance below 3e32. So nothing downstream can
// meet 0 * infinity from the block alone; the pixel's own position is the
// shader's guard (clay_logic.h, FogMirror).
inline constexpr float kFogRegisterLimit = 1e6f;

// The frame's fog block from what was read at a fogged main-scene draw.
// regs: pixel c28, c64, c65, c66, c67, c68 (kFogBlockRegisters), as host floats.
// fetch4, fetch5: the fetch constants of slots 4 and 5, host-order dwords.
// tf4_bytes, n4: the n4 bytes read at fetch4's base address; tf5_bytes, n5 likewise.
// Valid only when, tried in this order (`why`, optional, names the first that fails):
//   - nothing is null (kUnread);
//   - every component of the six registers is finite (kRegister);
//   - c28.x and c28.y are in [0, 4] (kAmount);
//   - |c66.xyz| is 1 within 1e-3 (kAxis);
//   - no component the fog reads is beyond kFogRegisterLimit in magnitude: c67.x,
//     .z, .w and c68.rgb (kMagnitude; c28.xy and the axis are bounded by their own
//     rules; review of plan Task 4, M2: the sky's c64, c65, c67.y and the unread
//     components are not judged, the sky's magnitudes are the sky's decision's);
//   - c67.x >= 0, c67.z >= 0 and c67.w > 0 (kRange);
//   - each fetch constant is its table's pin (FogTableFetchFault: 1D, an address,
//     width, format with its number format bit, endian, swizzle, sign fields,
//     exponent adjust 0, clamp to edge, linear filter, highest mip level 0),
//     tf4's first;
//   - n4 is 512 and n5 is 1,024, with the bytes there (kScatterBytes, kTransmitBytes);
//   - every component of every texel of tf4 is finite, its alpha too (kScatterTexel).
// The decode (frame-map section 16, "The two tables"; tools/xdk_sigmatch/
// fog_tables.py, decode_tf4 and decode_tf5): a texel of tf4 is four half floats,
// each stored high byte first, r g b a; a texel of tf5 is four bytes a r g b,
// each over 255. A table's values are not judged beyond that (tf4 is not held
// below 1: half floats can carry more). An invalid block is all zero.
// Not judged here, because the fog reads none of them (review of plan Task 3,
// I1): the sign of c67.y (the sky's path height) and of c64, c65 (the sky's
// extinctions), nor their magnitudes (review of plan Task 4, M2). They are
// finite like every component; whether they make a sky is the sky's decision
// (clay_logic.h, SkyRules: a negative or oversized range[1] or ext component
// gives no sky, counted "sky-block"), and must not cost a night or an interior
// its fog.
inline FogBlock MakeFogBlock(const float regs[6][4], const uint32_t fetch4[6], const uint32_t fetch5[6],
                             const uint8_t* tf4_bytes, size_t n4, const uint8_t* tf5_bytes, size_t n5,
                             FogBlockReason* why) {
  auto fail = [&](FogBlockReason r) {
    if (why) *why = r;
    return FogBlock{};
  };
  if (!regs || !fetch4 || !fetch5) return fail(FogBlockReason::kUnread);
  for (int r = 0; r < 6; ++r) {
    for (int c = 0; c < 4; ++c) {
      if (!std::isfinite(regs[r][c])) return fail(FogBlockReason::kRegister);
    }
  }
  const float* c28 = regs[0];
  const float* c64 = regs[1];
  const float* c65 = regs[2];
  const float* c66 = regs[3];
  const float* c67 = regs[4];
  const float* c68 = regs[5];
  for (int c = 0; c < 2; ++c) {
    if (!(c28[c] >= 0.0f && c28[c] <= 4.0f)) return fail(FogBlockReason::kAmount);
  }
  const double length = std::sqrt(double(c66[0]) * c66[0] + double(c66[1]) * c66[1] + double(c66[2]) * c66[2]);
  if (!(std::fabs(length - 1.0) <= 1e-3)) return fail(FogBlockReason::kAxis);
  // (only what the fog reads: c28.xy and the axis are bounded above; the sky's c64, c65 and c67.y
  // and the components no shader reads have no say here)
  const float read[6] = {c67[0], c67[2], c67[3], c68[0], c68[1], c68[2]};
  for (float v : read) {
    if (!(std::fabs(v) <= kFogRegisterLimit)) return fail(FogBlockReason::kMagnitude);
  }
  if (!(c67[0] >= 0.0f && c67[2] >= 0.0f && c67[3] > 0.0f)) return fail(FogBlockReason::kRange);
  if (const FogTableFault f = FogTableFetchFault(fetch4, kFogScatterPin); f != FogTableFault::kNone) {
    return fail(FogTableReason(0, f));
  }
  if (const FogTableFault f = FogTableFetchFault(fetch5, kFogTransmitPin); f != FogTableFault::kNone) {
    return fail(FogTableReason(1, f));
  }
  if (!tf4_bytes || n4 != kFogScatterBytes) return fail(FogBlockReason::kScatterBytes);
  if (!tf5_bytes || n5 != kFogTransmitBytes) return fail(FogBlockReason::kTransmitBytes);
  FogBlock b{};
  for (int i = 0; i < 64; ++i) {
    float texel[4];
    for (int c = 0; c < 4; ++c) {
      const uint8_t* p = tf4_bytes + 8 * i + 2 * c;
      texel[c] = HalfToFloat(uint16_t((uint16_t(p[0]) << 8) | p[1]));
      if (!std::isfinite(texel[c])) return fail(FogBlockReason::kScatterTexel);
    }
    std::memcpy(b.scatter[i], texel, sizeof(b.scatter[i]));
  }
  for (int i = 0; i < 256; ++i) {
    const uint8_t* p = tf5_bytes + 4 * i;
    for (int c = 0; c < 3; ++c) b.transmit[i][c] = float(p[1 + c]) / 255.0f;
  }
  std::memcpy(b.amount, c28, sizeof(b.amount));
  std::memcpy(b.axis, c66, sizeof(b.axis));
  std::memcpy(b.range, c67, sizeof(b.range));
  std::memcpy(b.near_colour, c68, sizeof(b.near_colour));
  std::memcpy(b.ext[0], c64, sizeof(b.ext[0]));
  std::memcpy(b.ext[1], c65, sizeof(b.ext[1]));
  b.valid = true;
  if (why) *why = FogBlockReason::kNone;
  return b;
}
inline FogBlock MakeFogBlock(const float regs[6][4], const uint32_t fetch4[6], const uint32_t fetch5[6],
                             const uint8_t* tf4_bytes, size_t n4, const uint8_t* tf5_bytes, size_t n5) {
  return MakeFogBlock(regs, fetch4, fetch5, tf4_bytes, n4, tf5_bytes, n5, nullptr);
}

// Where a frame's fog block was read from, for evidence only (the log and the
// discovery frame row; no shader input): per table (0 tf4, 1 tf5) the base
// address of its fetch constant and the XXH3 hash of the bytes that were
// decoded, which is the "content" a discovery capture names the same table by
// in its rows' "tfc". All zero without a block.
struct FogSource {
  uint32_t base[2] = {};
  uint64_t content[2] = {};
};

// Why a frame has no sky block (FrameScene::sky_reason); kNone: it has one.
enum class SkyBlockReason : uint8_t {
  kNone,
  kOff,       // the fog (and with it the sky) was not wanted this frame: nothing was read
  kNoDraw,    // the frame's main scene has no sky draw (an interior, a menu)
  kUnread,    // a register or a fetch constant could not be read
  kRegister,  // c31.x, c46.x or c47.x is not finite
  kRange,     // a path length below 0, or the textures' blend outside [0, 1]
  kCount
};

inline const char* SkyBlockReasonName(SkyBlockReason r) {
  switch (r) {
    case SkyBlockReason::kNone: return "none";
    case SkyBlockReason::kOff: return "off";
    case SkyBlockReason::kNoDraw: return "no-draw";
    case SkyBlockReason::kUnread: return "unread";
    case SkyBlockReason::kRegister: return "register";
    case SkyBlockReason::kRange: return "range";
    default: return "?";
  }
}

// The frame's sky block from what was read at the sky draw: pixel c31, c46, c47
// (only the x of each is the block's: the other components are not judged) and
// the fetch constants of slots 13 and 14. Valid only when nothing is null
// (kUnread), the three x are finite (kRegister), c31.x >= 0, c46.x >= 0 and
// c47.x is in [0, 1] (kRange). The fetch constants are copied as they are. An
// invalid block is all zero; `why` (optional) names the first rule that fails.
inline SkyBlock MakeSkyBlock(const float c31[4], const float c46[4], const float c47[4], const uint32_t fetch13[6],
                             const uint32_t fetch14[6], SkyBlockReason* why) {
  auto fail = [&](SkyBlockReason r) {
    if (why) *why = r;
    return SkyBlock{};
  };
  if (!c31 || !c46 || !c47 || !fetch13 || !fetch14) return fail(SkyBlockReason::kUnread);
  if (!std::isfinite(c31[0]) || !std::isfinite(c46[0]) || !std::isfinite(c47[0])) return fail(SkyBlockReason::kRegister);
  if (!(c31[0] >= 0.0f && c46[0] >= 0.0f && c47[0] >= 0.0f && c47[0] <= 1.0f)) return fail(SkyBlockReason::kRange);
  SkyBlock b{};
  b.c31x = c31[0];
  b.c46x = c46[0];
  b.c47x = c47[0];
  std::memcpy(b.fetch[0], fetch13, sizeof(b.fetch[0]));
  std::memcpy(b.fetch[1], fetch14, sizeof(b.fetch[1]));
  b.valid = true;
  if (why) *why = SkyBlockReason::kNone;
  return b;
}
inline SkyBlock MakeSkyBlock(const float c31[4], const float c46[4], const float c47[4], const uint32_t fetch13[6],
                             const uint32_t fetch14[6]) {
  return MakeSkyBlock(c31, c46, c47, fetch13, fetch14, nullptr);
}

// The pixel shaders with the game's fog block (tools/xdk_sigmatch/light_formulas.py,
// FOG_SHADERS, through gen_fog_table.py): a main-scene colour draw of one of them
// has the frame's six registers in the bank and the two tables in slots 4 and 5,
// which is where the frame's block is read. `alpha`: the blended form, whose
// in-scatter is scaled by the pixel's alpha (for the pass that draws the fog).
// The generated line's second number, the interpolator that is the pixel's
// world position in the game's shader, is not carried: the pass rebuilds the
// position from the pixel's depth (ruling 14) and nothing reads it (final
// review, M7; the generator and its tests still hold it). The lake's
// surface (not of the form), the sky and the trees' pixel shaders (which read no
// fog register: their fog comes from the vertex shader) are not in the table.
struct FogShaderEntry {
  uint64_t ps_hash;
  bool alpha;
};
inline constexpr FogShaderEntry kFogShaderTable[] = {
#define FABLE2_FOG_SHADER(PS, WORLD, ALPHA) {PS, (ALPHA) != 0},
#include "fog_shader_table.inc"
#undef FABLE2_FOG_SHADER
};
// What the table may hold: hashes that are not zero, in rising order (so none
// twice), none of them the sky's.
constexpr bool FogShaderTableIsWellFormed() {
  uint64_t last = 0;
  for (const FogShaderEntry& e : kFogShaderTable) {
    if (e.ps_hash <= last || e.ps_hash == kSkyPixelShader) return false;
    last = e.ps_hash;
  }
  return true;
}
static_assert(FogShaderTableIsWellFormed(), "fog_shader_table.inc holds an entry the capture cannot read");

// The fog entry of a pixel shader, or null.
inline const FogShaderEntry* FindFogShader(uint64_t ps_hash) {
  for (const FogShaderEntry& e : kFogShaderTable) {
    if (e.ps_hash == ps_hash) return &e;
  }
  return nullptr;
}

}  // namespace fable2::native::capture
