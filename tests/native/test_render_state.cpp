// Synthetic standalone test for the render-state decoder; no game or GPU.
// Real words are from the main-scene census of frame-map section 13, "Render state".
#include "../../src/native/capture/render_state.h"
#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace fable2::native::capture;

static uint32_t Bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

// A draw's words as the device holds them: depth 0x00708767, opaque, back-face
// cull with clockwise front faces, colour mask RGBA, VTE 0x43F with Z scale -1.
static StateWords Opaque() {
  StateWords w;
  w.read = true;
  w.depth_control = 0x00708767;
  w.blend_control = 0x00010001;
  w.color_control = 0x00000004;
  w.su_sc_mode = 0x00218006;
  w.color_mask = 0xF;
  w.alpha_ref = 0;
  w.vte_cntl = 0x0000043F;
  w.vport_z_scale = 0xBF800000;
  return w;
}

// Garbage words give the default state, not valid.
static bool GarbageIsDefault(const StateWords& w) {
  const RenderState s = DecodeRenderState(w);
  const RenderState d = DefaultRenderState();
  return !s.valid && !s.blend && !s.alpha_test && !s.alpha_to_mask && s.src == d.src && s.dst == d.dst &&
         s.op == d.op && s.depth_test && s.depth_write && s.depth_func == Compare::kLess &&
         s.cull == Cull::kNone && s.color_mask == 0xF && !s.depth_reversed && !s.front_cw;
}

int main() {
  // --- the default state ---
  {
    const RenderState d = DefaultRenderState();
    if (!d.valid || d.alpha_test || d.alpha_to_mask || d.blend || d.color_mask != 0xF) return 1;
    if (!d.depth_test || !d.depth_write || d.depth_func != Compare::kLess) return 2;
    if (d.cull != Cull::kNone || d.front_cw || d.depth_reversed) return 3;
    if (d.src != Factor::kOne || d.dst != Factor::kZero || d.op != BlendOp::kAdd) return 4;
    if (d.src_alpha != Factor::kOne || d.dst_alpha != Factor::kZero || d.op_alpha != BlendOp::kAdd) return 5;
    if (RenderState{}.valid) return 6;  // not valid until a decoder says so
    const StateWords w;
    if (w.read || w.color_mask != 0xF || w.blend_control != 0x00010001 || w.bin_mask_lo != 0x2AAAAAAA) return 7;
  }
  // --- real census words ---
  {  // opaque mesh draw: 0xECD66A10092E6562 / 0x8D900846800943C8
    const RenderState s = DecodeRenderState(Opaque());
    if (!s.valid || s.alpha_test || s.alpha_to_mask || s.blend) return 10;
    if (!s.depth_test || !s.depth_write || s.depth_func != Compare::kGreaterEqual) return 11;
    if (s.cull != Cull::kBack || !s.front_cw || s.color_mask != 0xF) return 12;
    if (!s.depth_reversed) return 13;
    if (s.alpha_func != Compare::kAlways) return 14;
  }
  {  // alpha test greater with alpha-to-mask, reference 128/255: 0x8123C16DBF583F92 / 0xA7E45D07F5CF6627
    StateWords w = Opaque();
    w.color_control = 0x8700001C;
    w.alpha_ref = Bits(128.0f / 255.0f);
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.alpha_test || s.alpha_func != Compare::kGreater || !s.alpha_to_mask) return 20;
    if (s.alpha_ref != 128.0f / 255.0f || s.blend) return 21;
    // Bits 24-31: the four alpha-to-mask dither offsets (the dither pattern 0x87).
    if (s.alpha_to_mask_offsets != 0x87) return 23;
  }
  {  // alpha test, no cull: 0xA5846836C90E1192 / 0x7CD57B81550F19E3
    StateWords w = Opaque();
    w.color_control = 0xAA00001C;
    w.su_sc_mode = 0x00218000;
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.alpha_test || s.cull != Cull::kNone || s.front_cw) return 22;
    if (!s.alpha_to_mask || s.alpha_to_mask_offsets != 0xAA) return 24;  // 0.5 at every pixel
  }
  {  // offsets set, enable clear (0x475EC9F795E5EDBB / 0x00E09D1BC5295D52): the offsets mean nothing
    StateWords w = Opaque();
    w.color_control = 0xAA000004;
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || s.alpha_to_mask || s.alpha_to_mask_offsets != 0) return 25;
    if (DefaultRenderState().alpha_to_mask_offsets != 0 || DecodeRenderState(Opaque()).alpha_to_mask_offsets != 0) {
      return 26;
    }
    // Garbage words give the default state, without offsets.
    w.color_control = 0x8700001C;
    w.alpha_ref = Bits(std::numeric_limits<float>::quiet_NaN());
    if (!GarbageIsDefault(w) || DecodeRenderState(w).alpha_to_mask_offsets != 0) return 27;
  }
  {  // blended with alpha test greater-equal: 0x7C5710DEF3EE33C4 / 0x014F8A02DB7B19CA
    StateWords w = Opaque();
    w.blend_control = 0x07060706;
    w.color_control = 0x0000000E;
    w.alpha_ref = Bits(16.0f / 255.0f);
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.blend || !s.alpha_test || s.alpha_func != Compare::kGreaterEqual) return 30;
    if (s.alpha_to_mask) return 31;
    if (s.src != Factor::kSrcAlpha || s.dst != Factor::kInvSrcAlpha || s.op != BlendOp::kAdd) return 32;
    if (s.src_alpha != Factor::kSrcAlpha || s.dst_alpha != Factor::kInvSrcAlpha || s.op_alpha != BlendOp::kAdd) return 33;
    if (s.alpha_ref != 16.0f / 255.0f) return 34;
  }
  {  // terrain layer, additive: 0xC30A97D946FA2BE4 / 0x9C2144E42E95A76F
    StateWords w = Opaque();
    w.blend_control = 0x01060106;
    w.su_sc_mode = 0x00218002;
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.blend || s.cull != Cull::kBack || s.front_cw) return 40;
    if (s.src != Factor::kSrcAlpha || s.dst != Factor::kOne || s.op != BlendOp::kAdd) return 41;
    if (s.src_alpha != Factor::kSrcAlpha || s.dst_alpha != Factor::kOne) return 42;
  }
  {  // terrain layer, source alpha over nothing
    StateWords w = Opaque();
    w.blend_control = 0x00060006;
    w.su_sc_mode = 0x00218002;
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.blend || s.src != Factor::kSrcAlpha || s.dst != Factor::kZero) return 43;
  }
  {  // one, add, inverse source alpha: 0xD4D558DA6A82BDC8
    StateWords w = Opaque();
    w.blend_control = 0x07010701;
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.blend || s.src != Factor::kOne || s.dst != Factor::kInvSrcAlpha) return 44;
  }
  {  // constant colour factors: 0x0D0C0D0C
    StateWords w = Opaque();
    w.blend_control = 0x0D0C0D0C;
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || s.src != Factor::kConstColor || s.dst != Factor::kInvConstColor) return 45;
  }
  {  // test on, write off, greater (constructed from the census word: bit 2 cleared, function 4)
    StateWords w = Opaque();
    w.depth_control = 0x00708742;
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.depth_test || s.depth_write || s.depth_func != Compare::kGreater) return 46;
  }
  {  // adaptive terrain: 0x5003700B7C9B1C16, depth 0x00708766
    StateWords w = Opaque();
    w.depth_control = 0x00708766;
    w.su_sc_mode = 0x00218002;
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.depth_test || !s.depth_write || s.depth_func != Compare::kGreaterEqual) return 47;
  }
  // --- unread words ---
  {
    StateWords w = Opaque();
    w.read = false;
    const RenderState s = DecodeRenderState(w);
    if (s.valid || s.blend || s.alpha_test || s.depth_func != Compare::kLess || s.cull != Cull::kNone ||
        s.depth_reversed || s.color_mask != 0xF) {
      return 50;
    }
  }
  // --- garbage words ---
  {
    StateWords w = Opaque();
    w.blend_control = 0x00010201;  // colour destination factor 2
    if (!GarbageIsDefault(w)) return 60;
    w.blend_control = 0x00010301;  // colour destination factor 3
    if (!GarbageIsDefault(w)) return 61;
    w.blend_control = 0x00110001;  // alpha source factor 17
    if (!GarbageIsDefault(w)) return 62;
    w.blend_control = 0x00010011;  // colour source factor 17
    if (!GarbageIsDefault(w)) return 63;
    w.blend_control = 0x1F010001;  // alpha destination factor 31
    if (!GarbageIsDefault(w)) return 64;
    w.blend_control = 0x000100A1;  // colour operation 5
    if (!GarbageIsDefault(w)) return 65;
    w.blend_control = 0x00A10001;  // alpha operation 5
    if (!GarbageIsDefault(w)) return 66;
    w.blend_control = 0x00E100E1;  // operation 7 in both
    if (!GarbageIsDefault(w)) return 67;
    w.blend_control = 0x00010001;
    w.alpha_ref = Bits(std::numeric_limits<float>::quiet_NaN());
    if (!GarbageIsDefault(w)) return 68;
    w.alpha_ref = Bits(std::numeric_limits<float>::infinity());
    if (!GarbageIsDefault(w)) return 69;
    w.alpha_ref = Bits(-std::numeric_limits<float>::infinity());
    if (!GarbageIsDefault(w)) return 70;
    w = Opaque();
    w.su_sc_mode = 0x00218003;  // both cull bits
    if (!GarbageIsDefault(w)) return 71;
  }
  // Every valid factor and operation decodes to itself (destination: the factor xor 1 when valid).
  for (uint32_t f : {0u, 1u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u}) {
    const uint32_t d = f ^ 1u;
    if (d != 0 && d != 1 && (d < 4 || d > 16)) continue;
    for (uint32_t op = 0; op <= 4; ++op) {
      StateWords w = Opaque();
      w.blend_control = f | (op << 5) | (d << 8) | (f << 16) | (op << 21) | (f << 24);
      const RenderState s = DecodeRenderState(w);
      if (!s.valid || uint32_t(s.src) != f || uint32_t(s.dst) != d || uint32_t(s.op) != op ||
          uint32_t(s.src_alpha) != f || uint32_t(s.dst_alpha) != f || uint32_t(s.op_alpha) != op) {
        return 72;
      }
    }
  }
  // --- colour mask ---
  {
    StateWords w = Opaque();
    w.color_mask = 0;
    RenderState s = DecodeRenderState(w);
    if (!s.valid || s.color_mask != 0) return 80;
    w.color_mask = 0xFFFFFFF5;  // only target 0's four bits
    s = DecodeRenderState(w);
    if (!s.valid || s.color_mask != 0x5) return 81;
  }
  // --- alpha-to-mask without alpha test: a flag of its own, no test ---
  {
    StateWords w = Opaque();
    w.color_control = 0x00000014;  // bit 4 set, test bit clear, function 4
    const RenderState s = DecodeRenderState(w);
    if (!s.valid || !s.alpha_to_mask || s.alpha_test || s.alpha_func != Compare::kAlways) return 90;
    if (s.alpha_to_mask_offsets != 0) return 91;  // enabled with offsets 0: threshold 1.0 everywhere
    w.color_control = 0x1B000010;                 // each pixel its own offset: 3, 2, 1, 0
    if (DecodeRenderState(w).alpha_to_mask_offsets != 0x1B) return 92;
  }
  // --- depth range: the viewport Z scale enabled by VTE bit 4 ---
  {
    StateWords w = Opaque();
    if (!DecodeRenderState(w).depth_reversed) return 100;  // scale -1, bit 4 set (0x43F)
    w.vte_cntl = 0x0000042F;                                // bit 4 clear
    RenderState s = DecodeRenderState(w);
    if (!s.valid || s.depth_reversed) return 101;
    w = Opaque();
    w.vport_z_scale = Bits(1.0f);
    s = DecodeRenderState(w);
    if (!s.valid || s.depth_reversed) return 102;
    w.vport_z_scale = Bits(std::numeric_limits<float>::quiet_NaN());
    if (!GarbageIsDefault(w)) return 103;  // an enabled NaN scale
    w.vport_z_scale = Bits(-std::numeric_limits<float>::infinity());
    if (!GarbageIsDefault(w)) return 104;
    w.vte_cntl = 0x0000042F;  // a NaN scale that is not enabled is not read
    s = DecodeRenderState(w);
    if (!s.valid || s.depth_reversed) return 105;
    w = Opaque();
    w.vport_z_scale = Bits(0.0f);
    s = DecodeRenderState(w);
    if (!s.valid || s.depth_reversed) return 106;
  }
  std::puts("PASS: render-state decoder");
  return 0;
}
