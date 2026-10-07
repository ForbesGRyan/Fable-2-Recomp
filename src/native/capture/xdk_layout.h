#pragma once

// Guest XDK D3D object and device layouts for Fable 2, found by disassembly
// and discovery captures (frame-map section 8). Every constant names its evidence: an instruction
// address in the guest image (`sigmatch.py disasm out\xdk\fable2 <addr> <n>`)
// and/or a discovery/[vbind] cross-check.

#include <cstdint>

namespace fable2::native::capture::xdk {

// --- Device (the r3 of every D3DDevice_* hook) ------------------------------
// Vertex fetch constant shadow: slot s is the dword pair at +0x480 + 8*s.
// SetStreamSource 0x821B6DFC stores dword 0 at (0xEF - index) * 8 and
// 0x821B6E00 stores dword 1 at 0x6F4 + (0x11 - index) * 8 = 0x77C - 8*index,
// i.e. slot 95 - index at +0x480. SetPixelShader 0x82208DD8 also uses +0x480.
inline constexpr uint32_t kDeviceVertexFetchOffset = 0x480;
// Texture fetch constant t shares the same shadow: its 6 dwords are vertex
// slots 3t..3t+2, i.e. +0x480 + 24*t (the GPU's register layout, xenos
// xe_gpu_texture_fetch_t). Runtime (frame-map section 9): the 6 dwords of
// t = 16..19 read there at every sampled DrawIndx:8221C9C8 equal the GPU
// register file's texture fetch constants of a draw with the same constants
// and patch index (1275/1275 rows, capture native_discovery_20261002_130248
// with fable_2_108.log).
inline constexpr uint32_t kDeviceTextureFetchStride = 24;
// Stream i is bound to vertex fetch slot 95 - i (same instructions; [vbind]
// shows fetch=95 for stream 0 draws).
inline constexpr uint32_t kStreamFetchSlotBase = 95;
inline constexpr uint32_t kMaxStreams = 16;
// Stream i's vertex-buffer object pointer: SetStreamSource 0x821B6E10..0x821B6E8C
// (`addi r11,r29,0xC2B; slwi r27,r11,2; stwx r30,r27,r31`) = +0x30AC + 4*i.
inline constexpr uint32_t kDeviceStreamObjectOffset = 0x30AC;
// Stream i's stride in dwords (one byte per stream): SetStreamSource
// 0x821B6E90/0x821B6E98 (`rlwinm r9,r26,30,24,31; stb r9,0x30F0(r11)`).
inline constexpr uint32_t kDeviceStreamStrideOffset = 0x30F0;
// Current index-buffer object: SetIndices 0x8219CD5C (`stw r29,0x3094(r31)`),
// read by DrawIndexedVertices 0x8221E330.
inline constexpr uint32_t kDeviceIndexBufferOffset = 0x3094;
// Vertex constant bank (256 x float4): DrawIndexedVertices 0x8221E130 and
// DrawVertices 0x8221C554 pass r6 = device + 0x780 with r5 = 0x4000 to
// SetPending_AluConstants.
inline constexpr uint32_t kDeviceVsConstantsOffset = 0x780;
// Pixel constant bank (256 x float4): DrawIndexedVertices 0x8221E148/0x8221E14C
// and DrawVertices 0x8221C56C/0x8221C570 pass r6 = device + 0x1780 with
// r5 = 0x4400 to SetPending_AluConstants (0x8221DF98), right after the vertex
// bank: the two banks are contiguous (0x780 + 256 * 16 = 0x1780).
inline constexpr uint32_t kDevicePsConstantsOffset = 0x1780;
// Shadow of the bool constants (registers 0x4900..0x4907; vertex b0..b31 are
// bits 0-31 of the first dword), right after the pixel bank. All six draw
// builders flush it with the register-run writer when its dirty bit is set:
// DrawIndexedVertices 0x8221E314..0x8221E324 (`addi r6,r31,0x2780;
// li r5,0x4900; lis r4,-0x100; bl 0x8221C908`: a 40-bit mask, register r5 + n
// from r6 + 4*n, so the 32 loop constants 0x4908.. follow at +0x27A0), and the
// same at 0x8221C738 (DrawVertices), 0x82206304, 0x82207F38, 0x82218164,
// 0x8221CC4C. Runtime (frame-map section 12): bit 0 is set in 32 of 19,256
// draw rows, all of two shaders with a `cexec b0` block. The loop constants
// read 0 in every row, also for the four shaders with a `loop i0`, so that
// part of the shadow is not established. Used as evidence only (discovery
// draw rows "vbool").
inline constexpr uint32_t kDeviceBoolConstantsOffset = 0x2780;
// Register 0x4904, the fifth dword of that shadow: the pixel shaders' bool
// constants b128..b159 as the dumps number them (`cexec b128`), b128 in bit 0.
// Static only: the dword was not compared with the GPU's register. Runtime
// (frame-map section 13, "Alpha and missing shaders"): it reads 0x00000100
// (b136 alone) in every draw row of two captures. Evidence only (discovery
// draw rows "pbool").
inline constexpr uint32_t kDevicePsBoolConstantsOffset = kDeviceBoolConstantsOffset + 4 * 4;

// --- Main-scene bracket (frame-map section 8, "Main-scene bracket") ---------
// Device flag byte +0x2ABC; bit 0x20 = inside BeginTiling/EndTiling (the
// predicated-tiling pass). BeginTiling 0x822A5F80 (only static caller
// 0x821A19B0, engine 0x821A17A8) sets it (0x822A61C0 `lbz`, 0x822A61D0
// `ori r9,r9,0x20`, 0x822A61FC `stb`); EndTiling 0x8227F0B0 clears it
// (0x8227F418 `lbz`, 0x8227F41C `andi. r11,r11,0xDF`, 0x8227F43C `stb`).
// Bit 0x01 of the same byte is the guard the six DRAW_INDX builders test
// before their SET_BIN_MASK_LO header (0x8221E41C `lbz r11,0x2ABC(r31)`,
// 0x8221E420 `clrlwi. r11,r11,31`, 0x8221E424 `bne` to the 0xC0006000 header;
// same test at 0x8221C7B4, 0x822063EC, 0x82208048, 0x822182BC, 0x8221CCA8):
// per-draw bin masks, set by BeginTiling only when device bits 0x08/0x04 and
// byte +0x2F9B are clear (0x822A6204..0x822A626C) and cleared by an explicit
// bin mask (0x822655B0: 0x822656C0 `rlwinm r11,r11,0,0,30`), so it misses part
// of the pass. Runtime (frame rows, frame-map section 8): gameplay capture
// native_discovery_20261002_104938, 120 frames: 1460-1477 draws per frame have
// bit 0x20 (byte 0x20/0x21/0x60/0x61), all of them on pitch 1120 and none of
// the 950 others (pitches 1040, 320, 280, 1280, 560, 640, 80, 160); 274 of the
// 1477 have bit 0x01 clear. One open and one close per frame.
inline constexpr uint32_t kDeviceTilingFlagOffset = 0x2ABC;  // byte
inline constexpr uint8_t kDeviceTilingFlagMask = 0x20;
// Shadow of RB_SURFACE_INFO (register 0x2000; bits 0-13 = color surface pitch
// in pixels): DrawIndexedVertices 0x8221E200..0x8221E210 (`addi r6,r31,0x2880;
// li r5,0x2000; bl 0x8221C908`); 0x8221C908 writes one type-0 packet per run of
// mask bits, register r5 + n taken from r6 + 4*n (0x8221C98C..0x8221C9AC).
// The pitch is evidence only (frame rows "pitch_in"/"pitch_out"). Bits 16-17
// are the surface's sample count (SDK registers.h RB_SURFACE_INFO::msaa_samples,
// xenos.h MsaaSamples: 0 none, 1 two samples, 2 four), which the draw records
// carry for alpha-to-mask (render_state.h; kRegSurfaceInfo below). Runtime
// (frame-map section 13, "Alpha and missing shaders"): 0x11810460, pitch 1120
// and two samples, in all 7,652 and 7,186 main-scene draw and tess rows of the
// captures native_discovery_20261007_044603 and _045335; outside the main scene
// none, and four at pitch 280. The sample field was not compared with the GPU's
// register file; in the depth pre-pass bins the tiling state emitter overrides
// the register (0x82215904..0x8221592C), as it does RB_COLOR_MASK below.
inline constexpr uint32_t kDeviceSurfaceInfoOffset = 0x2880;
inline constexpr uint32_t kSurfacePitchMask = 0x3FFF;
inline constexpr uint32_t kRegSurfaceInfo = 0x2000;    // RB_SURFACE_INFO
// Shadow of VGT_HOS_CNTL (register 0x2285; bits 0-1 = tessellation mode, 2 =
// adaptive). Registers 0x2280.. are shadowed from +0x2964 (0x82207E9C..
// 0x82207EA4: `addi r6,r31,0x2964; li r5,0x2280` to the register-run writer),
// so 0x2285 is +0x2978. DrawIndx:82207C30 tests it (0x82207C78 `lwz
// r10,0x2978(r31)`, 0x82207C7C `clrlwi r10,r10,30`, 0x82207C80 `cmplwi r10,2`)
// and in adaptive mode draws r7 patches with r7 * 4 tessellation factors
// (0x82207FD0 `mullw r8,r27,r21`, r21 = 4). Runtime: every [vtess] GPU draw of
// that builder's shader has VGT_HOS_CNTL = 2 and num_indices = 4 * r7
// (290/290 sampled rows, frame-map section 9).
inline constexpr uint32_t kDeviceHosCntlOffset = 0x2978;
inline constexpr uint32_t kHosCntlAdaptive = 2;

// --- Render-state register shadows (frame-map section 13, "Render state") ----
// The draw builders flush the device's register shadows with the register-run
// writer 0x8221C908 (r5 first register, r6 device + offset, r4 a dirty mask,
// register r5 + n from r6 + 4*n). DrawIndexedVertices:
//   0x8221E228 `addi r6,r31,0x28CC`, 0x8221E22C `li r5,0x2100`, 21-bit mask
//   (0x8221E230 `rldicr r4,r30,22,20`): 0x2100..0x2114;
//   0x8221E1B4 `addi r6,r31,0x2934`, 0x8221E1B8 `li r5,0x2200`, 12-bit mask
//   (0x8221E1BC `rldicr r4,r30,52,11`): 0x2200..0x220B.
// Runtime (frame-map section 13; tools/xdk_sigmatch/render_state_check.py pairs
// each discovery row with the GPU's DRAW_INDX packet by command-buffer address,
// index count and shader or c0): the ten registers of the rows' "rs", read here
// after the builder returned, equal the GPU register file in every execution of
// the draw outside the depth pre-pass. Bridge capture
// native_discovery_20261006_164805: 55,279 rows, all paired, 80,553 executions;
// walking capture native_discovery_20261006_165738: 44,726 rows, all paired,
// 66,761 executions. In the depth pre-pass (bin select 0x80000001 of the tiled
// main scene; 12,294 and 13,688 executions) nine registers are equal and the
// GPU's RB_COLOR_MASK is 0 where the shadow holds 0xF: the library's tiling
// state emitter 0x822155E0 (called by the builders, 0x8221E1A0) writes the
// shadow's value with a type-0 packet (0x822158A8 `li r10,0x2104` ..
// 0x822158C4) and then, under bin mask 0x15555555 (0x822158E8/0x822158F0), a
// predicated SET_CONSTANT of RB_COLOR_MASK = 0 (0x82215938..0x82215948, r20 = 0
// from 0x822158B4). The shadow is the value of the colour passes.
// Second round (captures native_discovery_20261006_174553 and _174802, with
// PA_CL_VPORT_ZSCALE, PA_CL_VPORT_ZOFFSET and PA_CL_VTE_CNTL added): all
// thirteen registers equal in all 80,490 and 66,802 colour-pass executions;
// in the 12,299 and 13,652 pre-pass executions twelve are equal and
// RB_COLOR_MASK is 0. Every row has VTE 0x0000043F (Z scale and offset on),
// scale -1 and offset 1: window depth is 1 - z/w, the depth range is reversed.
inline constexpr uint32_t kDeviceRegShadowBase2100 = 0x28CC;  // register 0x2100
inline constexpr uint32_t kDeviceRegShadowBase2200 = 0x2934;  // register 0x2200
inline constexpr uint32_t kRegColorMask = 0x2104;      // RB_COLOR_MASK
inline constexpr uint32_t kRegBlendRed = 0x2105;       // RB_BLEND_RED (green, blue, alpha follow)
inline constexpr uint32_t kRegAlphaRef = 0x210E;       // RB_ALPHA_REF (float)
inline constexpr uint32_t kRegDepthControl = 0x2200;   // RB_DEPTHCONTROL
inline constexpr uint32_t kRegBlendControl0 = 0x2201;  // RB_BLENDCONTROL0
inline constexpr uint32_t kRegColorControl = 0x2202;   // RB_COLORCONTROL
inline constexpr uint32_t kRegSuScModeCntl = 0x2205;   // PA_SU_SC_MODE_CNTL
inline constexpr uint32_t kRegVportZScale = 0x2113;    // PA_CL_VPORT_ZSCALE (float)
inline constexpr uint32_t kRegVportZOffset = 0x2114;   // PA_CL_VPORT_ZOFFSET (float)
inline constexpr uint32_t kRegVteCntl = 0x2206;        // PA_CL_VTE_CNTL
// RB_COLOR_INFO: the colour target of the draw (SDK registers.h: color_base in
// bits 0-11, color_format in bits 16-19). It is the second word of the 0x2000
// run the builders flush from kDeviceSurfaceInfoOffset (sixteen registers:
// DrawIndexedVertices 0x8221E200..0x8221E210, `rldicr r4,r30,6,15`). Discovery
// evidence only, and from the device shadow alone: unlike the registers above
// it was not compared with the GPU's register file.
inline constexpr uint32_t kRegColorInfo = 0x2001;
// PA_CL_VTE_CNTL bits 4 and 5 (SDK registers.h): the viewport Z scale and
// offset apply; window depth = z/w * scale + offset.
inline constexpr uint32_t kVteZScaleEnable = 0x10;
inline constexpr uint32_t kVteZOffsetEnable = 0x20;
// Device offset of the shadow of `reg` for the two runs above and the 0x2000
// run; 0 for any other register (not shadowed here).
constexpr uint32_t DeviceRegShadowOffset(uint32_t reg) {
  if (reg >= 0x2000 && reg <= 0x200F) return kDeviceSurfaceInfoOffset + 4 * (reg - 0x2000);
  if (reg >= 0x2100 && reg <= 0x2114) return kDeviceRegShadowBase2100 + 4 * (reg - 0x2100);
  if (reg >= 0x2200 && reg <= 0x220B) return kDeviceRegShadowBase2200 + 4 * (reg - 0x2200);
  return 0;
}

// --- Bin mask (frame-map section 13, "Pre-pass draws and the depth range") ---
// The device's explicit bin mask, low and high word. SetPredication 0x822655B0
// (r3 device, r4 mask) stores r4 at +0x31A4 for a non-zero mask (0x822656BC
// `stw r4,12708(r31)`) and 0xFFFFFFFF for r4 = 0 (0x822656A8 `li r10,-1`,
// 0x822656AC), 0 at +0x31A8 (0x822656D4 `stw r9,12712(r31)`), and emits
// SET_BIN_MASK_LO with the stored word (0x822656F0..0x82265700). The tiling
// state emitter 0x822155E0 restores SET_BIN_MASK_LO / _HI from the two words
// (0x8221594C..0x82215960). A predicated draw packet executes in a pass when
// the pass's bin select intersects the mask.
// Runtime (frame-map section 13; render_state_check.py on captures
// native_discovery_20261006_174553 and _174802): the high word is 0 in every
// row; the low word is 0xFFFFFFFF, 0x15555555, 0x2AAAAAAA, 0x0003000A or
// 0x00030002; and "a main-scene draw has a colour-pass execution iff its low
// word has a bit of 0x2AAAAAAA" holds for every sampled main-scene row (28,431
// with and 93 without on the bridge, 28,361 and 107 after walking, none wrong). The
// rows without are adaptive terrain draws (0x82207C30, pixel shader
// 0x2F8A0341E8C09698) under 0x15555555, executed once, depth only. Outside
// the main scene the bin select is all ones and every draw executes whatever
// its mask (121 rows under 0x15555555 / 0x2AAAAAAA at pitch 280).
inline constexpr uint32_t kDeviceBinMaskLoOffset = 0x31A4;
inline constexpr uint32_t kDeviceBinMaskHiOffset = 0x31A8;
// Odd bits are colour bins, even bits depth pre-pass bins: the emitter forces
// RB_COLOR_MASK to 0 under bin mask 0x15555555 (0x822158E8/0x822158F0), and
// draws under 0x15555555 execute only under bin select 0x80000001, draws under
// 0x2AAAAAAA only under 0x2 and 0x8 (same captures).
inline constexpr uint32_t kColourBins = 0x2AAAAAAAu;
inline constexpr uint32_t kPrePassBins = 0x15555555u;
// Whether a draw of the tiled main scene issued under this bin mask (low word)
// is drawn in colour; false: it only fills depth in the pre-pass.
constexpr bool HasColourPass(uint32_t bin_mask_lo) { return (bin_mask_lo & kColourBins) != 0; }

// --- Vertex buffer object (D3DVertexBuffer, SetStreamSource r5) -------------
// Dwords 6-7 hold the vertex fetch constant: dword 6 = CPU virtual address |
// type 3, dword 7 = size in dwords << 2 | endian. SetStreamSource 0x821B6DC4
// (`lwz r10,0x18(r30)`) and 0x821B6DCC (`lwz r5,0x1C(r30)`) write
// fc0 = GpuAddress(dword6 + offset), fc1 = dword7 - offset to the shadow.
// Confirmed against [vbind] fc=0x1A63C4E3 0x10012422 (fable_2_051.log; object
// 0x401288C8, dword6 0xFA63AD03, dword7 0x10012C02, offset 0x7E0) and by
// check_discovery.py: menu native_discovery_20261002_101117 197/197 matched,
// gameplay native_discovery_20261002_101258 9841/9841 matched (fc_match: the
// object's dwords 6/7 plus the stream offset equal the device shadow in all
// 9841 gameplay draw rows).
inline constexpr uint32_t kVbFetchDword = 6;

// --- Index buffer object (D3DIndexBuffer, device + kDeviceIndexBufferOffset) -
// DrawIndexedVertices 0x8221E39C (`lwz r10,0x18(r24)`): index data CPU address
// (start * 2 added, then the GpuAddress conversion, 0x8221E3AC..0x8221E3E4).
// Gameplay capture native_discovery_20261002_101258: GpuAddress(dword 6) is
// 4-byte aligned and below 0x20000000 in all 9335 sampled indexed draws, and
// the indices read there stay inside the position stream (max index + base
// vertex < vertices) in 9318; 6589 use exactly the last vertex. The other 17
// fetch their first element per instance from stream 1 (5 elements), so the
// vertex range does not apply to them.
inline constexpr uint32_t kIbAddressDword = 6;
// Index data size in bytes. Same capture: all 9335 sampled DrawIndexedVertices
// have (start + count) * 2 <= dword 7, 6598 exactly equal (e.g. object
// 0x4062A148 dword 7 = 28 for start 0, count 14).
inline constexpr uint32_t kIbSizeDword = 7;
// DrawIndexedVertices 0x8221E3A8/0x8221E3D8 (`lwz r6,0(r24); rlwinm r6,r6,0,0,0`):
// bit 31 of the resource Common dword selects 32-bit indices (sets 0x800 in
// the DRAW_INDX dword and scales the start offset by 4 instead of 2).
inline constexpr uint32_t kIbFormatDword = 0;
inline constexpr uint32_t kIbFormatMask = 0x80000000u;  // set = 32-bit indices
// Gameplay IB objects have Common 0x20400002 (type 2, endian 1, 16-bit) in all
// 9335 sampled draws; no 32-bit index buffer was sampled, so the mask rests on
// the disassembly. 16-bit strips are cut with the reset index 0xFFFF (8325 of
// the sampled draws contain it).
// Bits 29-30 of the same dword are the index endian swap (0x8221E3D0).
inline constexpr uint32_t kIbEndianShift = 29;

// --- Vertex shader object ----------------------------------------------------
// The hook named "SetPixelShader?" (0x822324E0) is the real SetVertexShader: it
// stores r4 at device + 0x3198 (0x82232580), and the shader flush 0x8221B140
// loads that object (0x8221B184) and emits its microcode with IM_LOAD type 0
// (vertex, 0x8221B7C0) or IM_LOAD_IMMEDIATE (0x821DFDE0). "SetVertexShader?"
// (0x82208CE8) stores device + 0x3194, emitted with IM_LOAD type 1 (pixel,
// 0x8221B354 `ori r11,r11,1`). Draws read the device field (inlined binds
// are covered too). Confirmed: device-field shaders hash to a [vbind] vs= in
// every sampled DrawVertices (menu 197/197, gameplay 610/610).
enum class VsSource : uint8_t { kHookR4, kHookR5, kHookR6, kDeviceField };
inline constexpr VsSource kVsSource = VsSource::kDeviceField;
inline constexpr uint32_t kVsDeviceFieldOffset = 0x3198;
// Fallback when the device field is null: gameplay indexed draws run with
// device + 0x3198 = 0 (every sampled DrawIndexedVertices in the gameplay
// capture) after the engine loads shaders itself through GpuLoadShaders
// 0x82221978 (r3 device, r4 vertex shader, r5 pixel shader). It emits the
// vertex shader's microcode with IM_LOAD type 0 straight from the object's
// record, unpatched (0x82221AA4..0x82221B1C), variant = header flag only when
// r5 is null (0x822219BC..0x822219C8, 0x82221A10). Confirmed: in gameplay
// capture native_discovery_20261002_101258 all 9335 sampled
// DrawIndexedVertices have device + 0x3198 = 0, and the microcode of the last
// GpuLoadShaders r4 on the drawing thread hashes to a [vbind] vs= with the
// same dword count in all 9335 (draw rows "source":"gpu_load").
inline constexpr VsSource kVsFallbackSource = VsSource::kHookR4;
inline constexpr uint32_t kVsFallbackHookAddress = 0x82221978;
// Microcode location: base = object dword kVsUcodeBaseDword (0x8221B7CC
// `lwz r8,0x20(r31)`); the shader header sits at object + kVsHeaderOffset
// (0x8221B4BC `addi r27,r31,0x368`); header + kVsRecordOffsetField + 8*variant
// holds the byte offset (from the header) of the variant's microcode record
// (0x8221B7B0..0x8221B7D4: obj[0x380 + 8*v] + obj + 0x368). Header bit
// kVsVariantFlagMask selects variant 1 (0x8221B20C..0x8221B224).
inline constexpr uint32_t kVsUcodeBaseDword = 8;
inline constexpr uint32_t kVsHeaderOffset = 0x368;
inline constexpr uint32_t kVsRecordOffsetField = 0x18;
inline constexpr uint32_t kVsVariantFlagMask = 0x20;
// Record dwords: microcode offset from the base (0x8221B7E0 `lwz r11,0x368(r11)`)
// and its size in bytes (0x8221B80C/0x8221B810 `lwz r11,0x36C(r11); srwi r11,r11,2`).
inline constexpr uint32_t kVsUcodeAddressDword = 0;
inline constexpr uint32_t kVsUcodeSizeDword = 1;
inline constexpr uint32_t kVsUcodeSizeShift = 0;  // bytes
// Runtime: the immediate copy's dword count (its IM_LOAD_IMMEDIATE header)
// equals record size / 4 in every load, and object microcode at
// GpuAddress(base + offset), size bytes, hashes to the [vbind] vs= of the draw
// with [vbind] dwords = size / 4 (gameplay native_discovery_20261002_101258:
// 9335 gpu_load + 416 object draws). Only variant 0 was seen at runtime
// (header flag 0x20 clear in every sample); variant 1 rests on the disassembly.
// The object's microcode is a template: its vertex fetches are rewritten for
// the bound vertex declaration and stream strides (0x821D3318) before the GPU
// sees them, so in the menu it does not hash to the [vbind] value (gameplay
// DrawVertices shaders read from the object did: 416 of the 610). In the menu (and while
// device byte +0x2ABC has bit 0x80 set, 0x8221B4F0..0x8221B504) the flush never
// patches in place (object dword 10, the patched-declaration id, stays
// 0xFFFFFFFF); 0x821DFDE0 (r3 device, r5 object, r10 variant) instead copies
// the template into the command buffer as IM_LOAD_IMMEDIATE (header
// 0xC0002B00 | (dwords + 1) << 16, then 0, then dwords; 0x821DFE34..0x821DFE6C)
// and patches the copy. On return the device's command write pointer
// (+kDeviceCommandWriteOffset, 0x821DFF30 `stw r11,0x30(r30)`) is the copy's
// last dword. Those patched bytes are what the GPU loads and hashes (menu:
// 197/197 sampled draws; the copy differs from the template in 14 dwords).
inline constexpr uint32_t kDeviceCommandWriteOffset = 0x30;
inline constexpr uint32_t kImLoadImmediateHeader = 0xC0002B00u;  // | (dwords + 1) << 16, bit 0 = predicate

// --- Pixel shader object -----------------------------------------------------
// The real SetPixelShader ("SetVertexShader?" 0x82208CE8) stores r4 at device +
// 0x3194 (0x82208D6C) and sets dirty bit 0x00100000 (0x82208D78); the shader
// flush 0x8221B140 loads the field (0x8221B18C) and, with that bit set
// (0x8221B2AC `rlwinm r8,r20,0,11,11`), emits the microcode with IM_LOAD type 1
// (0x8221B32C..0x8221B36C). GpuLoadShaders 0x82221978 emits its r5 the same way
// (0x82221A24..0x82221A64) and does not store the device field. The flush
// skips everything, the pixel shader included, when device + 0x3198 (vertex)
// is null (0x8221B190 `cmplwi cr6,r31,0`, 0x8221B194 `beq 0x8221B9C4`), so then
// the GPU runs the last GpuLoadShaders pixel shader even if the device field is
// set. A null pixel shader means depth only: GpuLoadShaders writes
// RB_MODECONTROL (SET_CONSTANT 0x00040208, 0x82221B34..0x82221B3C) = 5 (depth)
// without r5 (0x822219C0) and 4 (color + depth) with it (0x82221A18); the flush
// sets the same 5 / 4 in the low bits of device + 0x2954 (0x8221B228..0x8221B248,
// 0x8221B370..0x8221B3A4). Runtime: the draws' (vertex, pixel) hash pairs
// against the emulator's pipeline storage (frame-map section 8).
inline constexpr uint32_t kPsDeviceFieldOffset = 0x3194;
// Same layout as the vertex shader object with a different header and base
// dword and no variants. Header at object + kPsHeaderOffset (0x8221B2F8
// `addi r4,r29,0x28` hands it to the flush's literal-constant upload
// 0x8222BFF8, called at 0x8221B304; 0x82221BEC `addi r4,r30,0x28` hands it to
// GpuLoadShaders' own upload 0x82221CB0, called at 0x82221BF4, which emits
// LOAD_ALU_CONSTANT at 0x82221D30..0x82221D4C; SetPixelShader
// 0x82208D8C/0x82208D90 reads header dword 5 the
// same way); header + kPsRecordOffsetField holds the byte offset (from the
// header) of the single microcode record (0x8221B32C `lwz r11,0x40(r29)`,
// 0x8221B334 `add r11,r11,r29`: record = obj + obj[0x40] + 0x28). Base =
// object dword kPsUcodeBaseDword (0x8221B330 `lwz r10,0x18(r29)`, 0x82221A28
// `lwz r10,0x18(r30)`).
inline constexpr uint32_t kPsHeaderOffset = 0x28;
inline constexpr uint32_t kPsRecordOffsetField = 0x18;
inline constexpr uint32_t kPsUcodeBaseDword = 6;
// Record dwords: microcode offset from the base (0x8221B338 `lwz r11,0x28(r11)`,
// then `add r11,r11,r10` and the inline GpuAddress, 0x8221B340..0x8221B354
// `ori r11,r11,1`) and its size in bytes (0x8221B364/0x8221B368 `lwz
// r11,0x2C(r11); srwi r11,r11,2` into the IM_LOAD size field; same at
// 0x82221A30 / 0x82221A5C..0x82221A60). The microcode is loaded unpatched
// (0x8222BFF8 and 0x82221CB0 only upload the shader's literal constants). Runtime: see
// frame-map section 8, "Pixel shader microcode".
inline constexpr uint32_t kPsUcodeAddressDword = 0;
inline constexpr uint32_t kPsUcodeSizeDword = 1;
inline constexpr uint32_t kPsUcodeSizeShift = 0;  // bytes

// A shader's literal constants (the registers its own code sets: c252..c255 in
// the terrain shaders) are not in the device's constant banks. GpuLoadShaders
// uploads them from a table in the shader object with 0x82221CB0 (r3 device,
// r4 header, r5 base, r7 variant): for the vertex shader with header = object +
// kVsHeaderOffset and base = object dword kVsUcodeBaseDword (0x82221B60 `lwz
// r5,32(r31)`, 0x82221B68 `addi r4,r31,872`, call at 0x82221B70), for the pixel
// shader with header = object + kPsHeaderOffset, base = object dword
// kPsUcodeBaseDword and variant 0 (0x82221BDC..0x82221BF4). The table is at
// header + header[kShaderLiteralTableField + 8 * variant], 0 = none
// (0x82221CB8..0x82221CD8); its dword at kLiteralTableSizeOffset is the byte
// size of the entries, which start at kLiteralTableEntriesOffset
// (0x82221CDC..0x82221CE8). An entry is {u16 first register, u16 dword count,
// u32 offset} (0x82221CF8..0x82221D10); a zero count ends the list (another
// list follows it). The values are `count` big-endian dwords at
// GpuAddress(base + offset), loaded with LOAD_ALU_CONSTANT at dword index
// first * 4 (0x82221D30..0x82221D6C). Runtime: sub-project 5 read seven vertex
// shaders' tables this way with a temporary diagnostic (frame-map section 12,
// "Literals": one entry each, registers 252-255). The pixel side ran in game
// session 1 of sub-project 6: the tess rows' "ps_lit" hold the terrain pixel
// shaders' tables, registers 252 to 255 (frame-map section 13, "Terrain").
inline constexpr uint32_t kShaderLiteralTableField = 0x14;
inline constexpr uint32_t kLiteralTableSizeOffset = 0x10;
inline constexpr uint32_t kLiteralTableEntriesOffset = 0x14;

struct LiteralRun {
  uint16_t first_register;
  uint16_t dwords;
  uint32_t offset;  // of the values, from the shader object's base dword
};

// The entries of a literal table (`size` big-endian bytes at `entries`) as at
// most `max` runs; returns how many. Stops at a zero count or at an entry that
// does not fit in `size`.
inline uint32_t ParseLiteralRuns(const uint8_t* entries, uint32_t size, LiteralRun* out, uint32_t max) {
  uint32_t n = 0;
  if (!entries) return 0;
  for (uint32_t at = 0; n < max && at + 4 <= size;) {
    const uint16_t first = uint16_t(entries[at] << 8 | entries[at + 1]);
    const uint16_t count = uint16_t(entries[at + 2] << 8 | entries[at + 3]);
    if (count == 0 || at + 8 > size) break;
    out[n++] = {first, count,
                uint32_t(entries[at + 4]) << 24 | uint32_t(entries[at + 5]) << 16 |
                    uint32_t(entries[at + 6]) << 8 | uint32_t(entries[at + 7])};
    at += 8;
  }
  return n;
}

// XDK CPU-virtual -> GPU physical address conversion, as inlined at every
// fetch-constant / IM_LOAD site (e.g. SetStreamSource 0x821B6DD8..0x821B6DF4):
// the 0xE0000000 4 KB-page physical view is offset by 0x1000.
inline constexpr uint32_t GpuAddress(uint32_t cpu_virtual) {
  return (cpu_virtual & 0x1FFFFFFFu) + ((((cpu_virtual >> 20) + 0x200u)) & 0x1000u);
}

}  // namespace fable2::native::capture::xdk
