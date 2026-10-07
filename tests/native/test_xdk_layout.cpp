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
// The bool constants (registers 0x4900..0x4907) follow the pixel bank.
static_assert(xdk::kDeviceBoolConstantsOffset == 0x2780);
static_assert(xdk::kDeviceBoolConstantsOffset == xdk::kDevicePsConstantsOffset + 256 * 16);
// Pixel bools b128..b159 are register 0x4904, the fifth dword of that shadow.
static_assert(xdk::kDevicePsBoolConstantsOffset == 0x2790);
static_assert(xdk::kDevicePsBoolConstantsOffset == xdk::kDeviceBoolConstantsOffset + 4 * (0x4904 - 0x4900));
// Render-state register shadows (frame-map section 13, "Render state"): the
// 0x2100 run (21 registers) from +0x28CC and the 0x2200 run (12) from +0x2934.
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegColorMask) == 0x28DC);
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegBlendRed) == 0x28E0);
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegAlphaRef) == 0x2904);
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegDepthControl) == 0x2934);
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegBlendControl0) == 0x2938);
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegColorControl) == 0x293C);
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegSuScModeCntl) == 0x2948);
// RB_MODECONTROL: the shader flush's device + 0x2954 (0x8221B228..0x8221B248).
static_assert(xdk::DeviceRegShadowOffset(0x2208) == 0x2954);
// First and last register of each run, and the registers just outside them.
static_assert(xdk::DeviceRegShadowOffset(0x2100) == xdk::kDeviceRegShadowBase2100);
static_assert(xdk::DeviceRegShadowOffset(0x2114) == 0x28CC + 4 * 20);
static_assert(xdk::DeviceRegShadowOffset(0x2115) == 0);
static_assert(xdk::DeviceRegShadowOffset(0x20FF) == 0);
// The 0x2000 run: sixteen registers from device + 0x2880 (DrawIndexedVertices 0x8221E200..0x8221E210,
// mask width 16). RB_SURFACE_INFO is its first word, RB_COLOR_INFO (the colour target's format) the second.
static_assert(xdk::DeviceRegShadowOffset(0x2000) == xdk::kDeviceSurfaceInfoOffset);
static_assert(xdk::kRegColorInfo == 0x2001 && xdk::DeviceRegShadowOffset(xdk::kRegColorInfo) == 0x2884);
static_assert(xdk::DeviceRegShadowOffset(0x200F) == 0x2880 + 4 * 15);
static_assert(xdk::DeviceRegShadowOffset(0x2010) == 0);
static_assert(xdk::DeviceRegShadowOffset(0x1FFF) == 0);
static_assert(xdk::DeviceRegShadowOffset(0x2200) == xdk::kDeviceRegShadowBase2200);
static_assert(xdk::DeviceRegShadowOffset(0x220B) == 0x2934 + 4 * 11);
static_assert(xdk::DeviceRegShadowOffset(0x220C) == 0);
static_assert(xdk::DeviceRegShadowOffset(0x21FF) == 0);
// Other runs are not covered by this function (VGT_HOS_CNTL has its own constant).
static_assert(xdk::DeviceRegShadowOffset(0x2285) == 0);
// The 0x2100 run ends where the 0x2180 run's shadow starts (+0x2920), and the
// 0x2200 run ends where the 0x2280 run's starts (+0x2964).
static_assert(xdk::kDeviceRegShadowBase2100 + 4 * 21 == 0x2920);
static_assert(xdk::kDeviceRegShadowBase2200 + 4 * 12 == 0x2964);
// Viewport Z scale and offset and the viewport transform control, in the same
// two runs (frame-map section 13, "Pre-pass draws and the depth range").
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegVportZScale) == 0x2918);
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegVportZOffset) == 0x291C);
static_assert(xdk::DeviceRegShadowOffset(xdk::kRegVteCntl) == 0x294C);
static_assert(xdk::kVteZScaleEnable == 0x10 && xdk::kVteZOffsetEnable == 0x20);
// The explicit bin mask: SetPredication 0x822655B0 stores r4 at 12708(r31) and
// 0 at 12712(r31); the tiling emitter restores SET_BIN_MASK_LO / _HI from them.
static_assert(xdk::kDeviceBinMaskLoOffset == 12708 && xdk::kDeviceBinMaskHiOffset == 12712);
static_assert(xdk::kDeviceBinMaskHiOffset == xdk::kDeviceBinMaskLoOffset + 4);
// Colour bins are the odd bits, depth pre-pass bins the even bits.
static_assert(xdk::kColourBins == 0x2AAAAAAAu && xdk::kPrePassBins == 0x15555555u);
static_assert((xdk::kColourBins & xdk::kPrePassBins) == 0);
static_assert(xdk::HasColourPass(0xFFFFFFFFu));   // no explicit predication
static_assert(xdk::HasColourPass(0x2AAAAAAAu));   // colour passes only
static_assert(!xdk::HasColourPass(0x15555555u));  // depth pre-pass only
static_assert(!xdk::HasColourPass(0));
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
  // A shader object's literal-constant table (GpuLoadShaders' upload 0x82221CB0): entries of
  // {u16 first register, u16 dword count, u32 offset}, big-endian, ended by a zero count. The
  // one-entry shape is what sub-project 5 found in seven vertex shaders: registers 252-255,
  // sixteen dwords (frame-map section 12, "Literals").
  {
    const uint8_t one[] = {0x00, 0xFC, 0x00, 0x10, 0x00, 0x00, 0x01, 0x40,   // c252, 16 dwords, offset 0x140
                           0x00, 0x00, 0x00, 0x00,                           // zero count: end of the list
                           0x00, 0x07, 0x00, 0x02, 0xAA, 0xBB, 0xCC, 0xDD};  // the next list: not literals
    xdk::LiteralRun runs[4] = {};
    if (xdk::ParseLiteralRuns(one, sizeof(one), runs, 4) != 1) return 4;
    if (runs[0].first_register != 252 || runs[0].dwords != 16 || runs[0].offset != 0x140) return 5;
    // Two runs that fill the table exactly (no terminator): a pixel shader's c254..c255 at 510.
    const uint8_t two[] = {0x00, 0xFC, 0x00, 0x04, 0x00, 0x00, 0x02, 0x00,
                           0x01, 0xFE, 0x00, 0x08, 0x12, 0x34, 0x56, 0x78};
    if (xdk::ParseLiteralRuns(two, sizeof(two), runs, 4) != 2) return 6;
    if (runs[1].first_register != 510 || runs[1].dwords != 8 || runs[1].offset != 0x12345678u) return 7;
    // No more than `max`; an entry whose offset lies past the size is not reported; no table, none.
    if (xdk::ParseLiteralRuns(two, sizeof(two), runs, 1) != 1) return 8;
    if (xdk::ParseLiteralRuns(two, 12, runs, 4) != 1) return 9;
    if (xdk::ParseLiteralRuns(two, 6, runs, 4) != 0 || xdk::ParseLiteralRuns(two, 3, runs, 4) != 0) return 10;
    if (xdk::ParseLiteralRuns(nullptr, 16, runs, 4) != 0 || xdk::ParseLiteralRuns(two, 0, runs, 4) != 0) return 11;
    const uint8_t none[] = {0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40};  // zero count first
    if (xdk::ParseLiteralRuns(none, sizeof(none), runs, 4) != 0) return 12;
  }
  std::cout << "PASS: xdk layout\n";
  return 0;
}
