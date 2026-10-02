#pragma once

// Guest XDK D3D object and device layouts for Fable 2, found in Task 9
// (frame-map section 8). Every constant names its evidence: an instruction
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

// XDK CPU-virtual -> GPU physical address conversion, as inlined at every
// fetch-constant / IM_LOAD site (e.g. SetStreamSource 0x821B6DD8..0x821B6DF4):
// the 0xE0000000 4 KB-page physical view is offset by 0x1000.
inline constexpr uint32_t GpuAddress(uint32_t cpu_virtual) {
  return (cpu_virtual & 0x1FFFFFFFu) + ((((cpu_virtual >> 20) + 0x200u)) & 0x1000u);
}

}  // namespace fable2::native::capture::xdk
