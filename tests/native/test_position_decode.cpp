// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/position_decode.h"
#include <cmath>
#include <cstring>
#include <iostream>

using namespace fable2::native::capture;

static void PutBe32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = uint8_t(v); }
static void PutBe16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = uint8_t(v); }
static uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

int main() {
  if (PosFormatFromXenos(57) != PosFormat::kFloat3) return 1;
  if (PosFormatFromXenos(38) != PosFormat::kFloat4) return 2;
  if (PosFormatFromXenos(32) != PosFormat::kHalf4) return 3;
  if (PosFormatFromXenos(26) != PosFormat::kShort4) return 4;
  if (PosFormatFromXenos(6) != PosFormat::kUnknown) return 5;

  if (HalfToFloat(0x3C00) != 1.0f || HalfToFloat(0xC000) != -2.0f) return 6;
  if (HalfToFloat(0x0001) <= 0.0f || HalfToFloat(0x0001) > 1e-7f) return 7;  // denormal
  if (!std::isinf(HalfToFloat(0x7C00))) return 8;

  // float3, stride 16, offset 4: vertex1 = (1,2,3)
  uint8_t vb[64] = {};
  PutBe32(vb + 16 + 4, Bits(1.0f)); PutBe32(vb + 16 + 8, Bits(2.0f)); PutBe32(vb + 16 + 12, Bits(3.0f));
  PosLayout f3; f3.format = PosFormat::kFloat3; f3.offset_bytes = 4; f3.stride_bytes = 16;
  Float4 out[2];
  if (!DecodePositions(vb, sizeof(vb), f3, 1, 1, out)) return 9;
  if (out[0].x != 1.0f || out[0].y != 2.0f || out[0].z != 3.0f || out[0].w != 1.0f) return 10;
  // Reading past src_size fails: vertex 3 needs bytes 52..64 (ok), vertex 4 needs 68 (fail).
  if (!DecodePositions(vb, sizeof(vb), f3, 3, 1, out)) return 11;
  if (DecodePositions(vb, sizeof(vb), f3, 3, 2, out)) return 12;
  // NaN passes through unchanged (renderer culls).
  PutBe32(vb + 4, 0x7FC00000);
  if (!DecodePositions(vb, sizeof(vb), f3, 0, 1, out) || !std::isnan(out[0].x)) return 13;

  // short4 signed normalized: -32768 clamps to -1, 32767 -> 1, exp_adjust 1 doubles.
  uint8_t sb[8];
  PutBe16(sb + 0, 0x8000); PutBe16(sb + 2, 0x7FFF); PutBe16(sb + 4, 0); PutBe16(sb + 6, 0x7FFF);
  PosLayout s4; s4.format = PosFormat::kShort4; s4.stride_bytes = 8;
  if (!DecodePositions(sb, sizeof(sb), s4, 0, 1, out)) return 14;
  if (out[0].x != -1.0f || out[0].y != 1.0f || out[0].z != 0.0f || out[0].w != 1.0f) return 15;
  s4.normalized = false; s4.exp_adjust = 1;
  if (!DecodePositions(sb, sizeof(sb), s4, 0, 1, out)) return 16;
  if (out[0].x != -65536.0f || out[0].y != 65534.0f) return 17;

  // half4
  uint8_t hb[8];
  PutBe16(hb + 0, 0x3C00); PutBe16(hb + 2, 0x4000); PutBe16(hb + 4, 0xC000); PutBe16(hb + 6, 0x3C00);
  PosLayout h4; h4.format = PosFormat::kHalf4; h4.stride_bytes = 8;
  if (!DecodePositions(hb, sizeof(hb), h4, 0, 1, out)) return 18;
  if (out[0].x != 1.0f || out[0].y != 2.0f || out[0].z != -2.0f || out[0].w != 1.0f) return 19;

  // Unknown format and zero stride are rejected.
  PosLayout bad; bad.stride_bytes = 16;
  if (DecodePositions(vb, sizeof(vb), bad, 0, 1, out)) return 20;
  f3.stride_bytes = 0;
  if (DecodePositions(vb, sizeof(vb), f3, 0, 1, out)) return 21;

  // Fetch destination swizzle and 8in32 pair swap (frame-map section 9): a
  // half4 position fetched as r.yxw1 from a stream with endian 8in32. The GPU
  // sees each dword's halves swapped, s = (m1, m0, m3, m2); yxw1 restores
  // (m0, m1, m2) and forces w = 1 (the memory w holds other data).
  PutBe16(hb + 6, 0x4800);  // 8.0, not a position w
  h4.swap16 = true;
  h4.swizzle = 1u | (0u << 3) | (3u << 6) | (5u << 9);  // yxw1
  if (!DecodePositions(hb, sizeof(hb), h4, 0, 1, out)) return 22;
  if (out[0].x != 1.0f || out[0].y != 2.0f || out[0].z != -2.0f || out[0].w != 1.0f) return 23;
  // Without the pair swap the same swizzle reads (m1, m0, m3, 1).
  h4.swap16 = false;
  if (!DecodePositions(hb, sizeof(hb), h4, 0, 1, out)) return 24;
  if (out[0].x != 2.0f || out[0].y != 1.0f || out[0].z != 8.0f || out[0].w != 1.0f) return 25;
  // Constant 0 (4) and "keep" (7: 0 for x/y/z, 1 for w). float3 ignores swap16.
  PosLayout g3; g3.format = PosFormat::kFloat3; g3.stride_bytes = 16; g3.swap16 = true;
  g3.swizzle = 2u | (4u << 3) | (7u << 6) | (7u << 9);  // z0__
  if (!DecodePositions(vb + 20, 12, g3, 0, 1, out)) return 26;
  if (out[0].x != 3.0f || out[0].y != 0.0f || out[0].z != 0.0f || out[0].w != 1.0f) return 27;
  // The default swizzle is the identity xyzw.
  if (PosLayout{}.swizzle != 0x688 || PosLayout{}.swap16) return 28;

  // Fetch constant endian -> layout: 16-bit components read big-endian with
  // 8in16 (no pair swap) or 8in32 (pair swap); 32-bit components only with
  // 8in32. Other combinations are rejected.
  PosLayout e = h4;
  if (!ApplyFetchEndian(&e, 1) || e.swap16) return 30;
  if (!ApplyFetchEndian(&e, 2) || !e.swap16) return 31;
  if (ApplyFetchEndian(&e, 0) || ApplyFetchEndian(&e, 3)) return 32;
  e = f3;
  if (!ApplyFetchEndian(&e, 2) || e.swap16) return 33;
  if (ApplyFetchEndian(&e, 1) || ApplyFetchEndian(&e, 0)) return 34;
  std::cout << "PASS: position decode\n";
  return 0;
}
