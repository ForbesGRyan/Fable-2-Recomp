#include "../../src/native/capture/xdk_layout.h"
#include <iostream>
namespace xdk = fable2::native::capture::xdk;
static_assert(xdk::GpuAddress(0xE0000000u) == 0x00001000u);  // 4 KB view: +0x1000
static_assert(xdk::GpuAddress(0xDFFFF000u) == 0x1FFFF000u);  // 16 MB view: no offset
static_assert(xdk::GpuAddress(0xA0001000u) == 0x00001000u);  // 64 KB view: no offset
// Texture fetch constant 16 occupies vertex fetch slots 48-50 of the shadow.
static_assert(xdk::kDeviceVertexFetchOffset + xdk::kDeviceTextureFetchStride * 16 ==
              xdk::kDeviceVertexFetchOffset + 8 * 48);
// VGT_HOS_CNTL (0x2285) in the 0x2280 run shadowed from +0x2964.
static_assert(xdk::kDeviceHosCntlOffset == 0x2964 + 4 * (0x2285 - 0x2280));
// Pixel constant bank: r6 = device + 0x1780 for r5 = 0x4400 (0x8221E148), right
// after the 256 x float4 vertex bank.
static_assert(xdk::kDevicePsConstantsOffset == 0x1780);
static_assert(xdk::kDevicePsConstantsOffset == xdk::kDeviceVsConstantsOffset + 256 * 16);
// Pixel shader object (frame-map section 8, "Pixel shader microcode"): the
// shader flush reads obj[0x40] (record offset, 0x8221B32C), obj[0x18] (base,
// 0x8221B330), record dword 0 at obj + rec + 0x28 (0x8221B338) and the byte size
// at obj + rec + 0x2C (0x8221B364).
static_assert(xdk::kPsDeviceFieldOffset == 0x3194);
static_assert(xdk::kPsHeaderOffset == 0x28 && xdk::kPsRecordOffsetField == 0x18);
static_assert(xdk::kPsHeaderOffset + xdk::kPsRecordOffsetField == 0x40);
static_assert(4 * xdk::kPsUcodeBaseDword == 0x18);
static_assert(xdk::kPsHeaderOffset + 4 * xdk::kPsUcodeAddressDword == 0x28);
static_assert(xdk::kPsHeaderOffset + 4 * xdk::kPsUcodeSizeDword == 0x2C);
static_assert(xdk::kPsUcodeSizeShift == 0);  // bytes (srwi 2 makes the IM_LOAD dword count)
// The vertex shader object, for comparison: same record shape, other header and base.
static_assert(xdk::kVsHeaderOffset + xdk::kVsRecordOffsetField == 0x380);
static_assert(4 * xdk::kVsUcodeBaseDword == 0x20);
static_assert(xdk::kPsUcodeAddressDword == xdk::kVsUcodeAddressDword &&
              xdk::kPsUcodeSizeDword == xdk::kVsUcodeSizeDword);
int main() {
  // Real evidence (frame-map section 8): VB object 0x401288C8 dword 6/7 =
  // 0xFA63AD03 / 0x10012C02, stream offset 0x7E0 -> [vbind] fc=0x1A63C4E3 0x10012422.
  const uint32_t d6 = 0xFA63AD03u, d7 = 0x10012C02u, offset = 0x7E0u;
  if (xdk::GpuAddress(d6 + offset) != 0x1A63C4E3u) return 1;
  if (d7 - offset != 0x10012422u) return 2;
  if (xdk::GpuAddress(d6 & ~3u) + offset != (0x1A63C4E3u & ~3u)) return 3;
  std::cout << "PASS: xdk layout\n";
  return 0;
}
