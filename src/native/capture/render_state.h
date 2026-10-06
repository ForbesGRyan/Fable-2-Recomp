#pragma once

// GPU render-state words of one draw and their decoder (pure: no SDK/GPU deps).
// Registers and bit layouts: frame-map section 13, "Render state".

#include <cmath>
#include <cstdint>
#include <cstring>

namespace fable2::native::capture {

// Raw GPU register words of one draw's render state, as read from the device shadow.
struct StateWords {
  uint32_t color_control = 0;   // RB_COLORCONTROL
  uint32_t alpha_ref = 0;       // RB_ALPHA_REF, a float's bits
  uint32_t blend_control = 0x00010001;  // RB_BLENDCONTROL0: One, Zero, Add for colour and alpha
  uint32_t color_mask = 0xF;    // RB_COLOR_MASK
  uint32_t depth_control = 0;   // RB_DEPTHCONTROL
  uint32_t su_sc_mode = 0;      // PA_SU_SC_MODE_CNTL
  uint32_t vport_z_scale = 0x3F800000;  // PA_CL_VPORT_ZSCALE (register 0x2113), a float's bits
  uint32_t vte_cntl = 0;        // PA_CL_VTE_CNTL (register 0x2206); bit 4 enables the viewport Z scale
  uint32_t bin_mask_lo = 0x2AAAAAAA;  // device +0x31A4, low word of the tiling bin mask (HasColourPass)
  bool read = false;            // false: the words could not be read
};

enum class Compare : uint8_t { kNever, kLess, kEqual, kLessEqual, kGreater, kNotEqual, kGreaterEqual, kAlways };
enum class Factor : uint8_t { kZero = 0, kOne = 1, kSrcColor = 4, kInvSrcColor = 5, kSrcAlpha = 6, kInvSrcAlpha = 7,
                              kDstColor = 8, kInvDstColor = 9, kDstAlpha = 10, kInvDstAlpha = 11, kConstColor = 12,
                              kInvConstColor = 13, kConstAlpha = 14, kInvConstAlpha = 15, kSrcAlphaSat = 16 };
enum class BlendOp : uint8_t { kAdd = 0, kSubtract = 1, kMin = 2, kMax = 3, kRevSubtract = 4 };
enum class Cull : uint8_t { kNone, kFront, kBack };

struct RenderState {
  bool valid = false;
  bool alpha_test = false; Compare alpha_func = Compare::kAlways; float alpha_ref = 0.0f; bool alpha_to_mask = false;
  bool blend = false; Factor src = Factor::kOne, dst = Factor::kZero; BlendOp op = BlendOp::kAdd;
  Factor src_alpha = Factor::kOne, dst_alpha = Factor::kZero; BlendOp op_alpha = BlendOp::kAdd;
  uint8_t color_mask = 0xF;
  bool depth_test = true, depth_write = true; Compare depth_func = Compare::kLess;
  Cull cull = Cull::kNone;
  bool front_cw = false;         // PA_SU_SC_MODE_CNTL bit 2: 1 = clockwise faces are front
  bool depth_reversed = false;   // viewport Z scale negative: depth_func is mirrored when drawing
};

// Valid, opaque, depth Less with write, no cull, mask 0xF.
inline RenderState DefaultRenderState() {
  RenderState s;
  s.valid = true;
  return s;
}

namespace detail {

inline bool ValidFactor(uint32_t f) { return f == 0 || f == 1 || (f >= 4 && f <= 16); }

inline float BitsToFloat(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

inline RenderState InvalidRenderState() {
  RenderState s = DefaultRenderState();
  s.valid = false;
  return s;
}

}  // namespace detail

// Garbage (an unknown factor or operation, a non-finite alpha reference, both
// cull bits, an enabled non-finite Z scale) or unread words give the default
// state with valid = false: nothing is guessed from them.
inline RenderState DecodeRenderState(const StateWords& w) {
  if (!w.read) return detail::InvalidRenderState();
  RenderState s = DefaultRenderState();

  // RB_COLORCONTROL: bits 0-2 alpha function, 3 test enable, 4 alpha-to-mask.
  const float ref = detail::BitsToFloat(w.alpha_ref);
  if (!std::isfinite(ref)) return detail::InvalidRenderState();
  s.alpha_test = (w.color_control & 0x8) != 0;
  s.alpha_func = s.alpha_test ? Compare(w.color_control & 7) : Compare::kAlways;
  s.alpha_ref = ref;
  s.alpha_to_mask = (w.color_control & 0x10) != 0;

  // RB_BLENDCONTROL0: colour source 0-4, operation 5-7, destination 8-12;
  // alpha source 16-20, operation 21-23, destination 24-28.
  const uint32_t b = w.blend_control;
  const uint32_t src = b & 0x1F, op = (b >> 5) & 7, dst = (b >> 8) & 0x1F;
  const uint32_t asrc = (b >> 16) & 0x1F, aop = (b >> 21) & 7, adst = (b >> 24) & 0x1F;
  if (!detail::ValidFactor(src) || !detail::ValidFactor(dst) || !detail::ValidFactor(asrc) ||
      !detail::ValidFactor(adst) || op > 4 || aop > 4) {
    return detail::InvalidRenderState();
  }
  s.src = Factor(src); s.dst = Factor(dst); s.op = BlendOp(op);
  s.src_alpha = Factor(asrc); s.dst_alpha = Factor(adst); s.op_alpha = BlendOp(aop);
  const bool opaque = src == 1 && dst == 0 && op == 0 && asrc == 1 && adst == 0 && aop == 0;
  s.blend = !opaque;

  s.color_mask = uint8_t(w.color_mask & 0xF);

  // RB_DEPTHCONTROL: bit 1 depth test, 2 depth write, 4-6 depth function.
  s.depth_test = (w.depth_control & 0x2) != 0;
  s.depth_write = (w.depth_control & 0x4) != 0;
  s.depth_func = Compare((w.depth_control >> 4) & 7);

  // PA_SU_SC_MODE_CNTL: bit 0 cull front, 1 cull back, 2 front face clockwise.
  const bool cull_front = (w.su_sc_mode & 1) != 0, cull_back = (w.su_sc_mode & 2) != 0;
  if (cull_front && cull_back) return detail::InvalidRenderState();
  s.cull = cull_front ? Cull::kFront : cull_back ? Cull::kBack : Cull::kNone;
  s.front_cw = (w.su_sc_mode & 4) != 0;

  // PA_CL_VTE_CNTL bit 4: the viewport Z scale applies (window depth = z/w * scale + offset).
  if (w.vte_cntl & 0x10) {
    const float zs = detail::BitsToFloat(w.vport_z_scale);
    if (!std::isfinite(zs)) return detail::InvalidRenderState();
    s.depth_reversed = zs < 0.0f;
  }
  return s;
}

}  // namespace fable2::native::capture
