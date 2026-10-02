#pragma once

// Xenos shader microcode -> vertex fetch list (pure: no SDK/GPU deps). Bit
// layouts mirror rex/graphics/format/ucode.h (ControlFlowExecInstruction,
// VertexFetchInstruction); see the plan's Task 3 for the field list.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "position_decode.h"

namespace fable2::native::capture {

struct VertexFetch {
  uint32_t instr_index = 0;
  uint32_t fetch_slot = 0;
  uint32_t dst_reg = 0;
  uint32_t dst_swizzle = 0;
  uint32_t format = 0;
  bool is_signed = false;
  bool normalized = true;
  bool mini = false;
  int exp_adjust = 0;
  uint32_t stride_dwords = 0;
  int32_t offset_dwords = 0;
};

namespace detail {
inline bool IsExec(uint32_t op) { return (op >= 1 && op <= 6) || op == 13 || op == 14; }
inline bool IsExecEnd(uint32_t op) { return op == 2 || op == 4 || op == 6 || op == 14; }
inline int32_t SignExtend(uint32_t v, int bits) {
  const uint32_t m = 1u << (bits - 1);
  return int32_t((v ^ m) - m);
}
}  // namespace detail

inline std::vector<VertexFetch> DecodeVertexFetches(const uint32_t* ucode, size_t dword_count) {
  std::vector<VertexFetch> out;
  if (!ucode || dword_count < 3) return out;
  uint32_t instr_index = 0, cur_slot = 0, cur_stride = 0;
  size_t cf_end = dword_count;  // shrinks to the first exec's instruction area
  for (size_t cf = 0; cf + 3 <= cf_end; cf += 3) {
    const uint32_t* dw = ucode + cf;
    const uint64_t pair[2] = {uint64_t(dw[0]) | (uint64_t(dw[1] & 0xFFFF) << 32),
                              uint64_t(dw[1] >> 16) | (uint64_t(dw[2]) << 16)};
    bool ended = false;
    for (uint64_t ins : pair) {
      const uint32_t op = uint32_t(ins >> 44) & 0xF;
      if (!detail::IsExec(op)) continue;
      const uint32_t address = uint32_t(ins) & 0xFFF;
      const uint32_t count = uint32_t(ins >> 12) & 0x7;
      const uint32_t sequence = uint32_t(ins >> 16) & 0xFFF;
      if (size_t(address) * 3 < cf_end) cf_end = size_t(address) * 3;
      for (uint32_t i = 0; i < count; ++i, ++instr_index) {
        if (!((sequence >> (2 * i)) & 1)) continue;
        const size_t at = (size_t(address) + i) * 3;
        if (at + 3 > dword_count) return out;
        const uint32_t d0 = ucode[at], d1 = ucode[at + 1], d2 = ucode[at + 2];
        if ((d0 & 0x1F) != 0) continue;  // texture fetch
        VertexFetch f;
        f.instr_index = instr_index;
        f.dst_reg = (d0 >> 12) & 0x3F;
        f.dst_swizzle = d1 & 0xFFF;
        f.is_signed = (d1 >> 12) & 1;
        f.normalized = ((d1 >> 13) & 1) == 0;
        f.format = (d1 >> 16) & 0x3F;
        f.exp_adjust = detail::SignExtend((d1 >> 24) & 0x3F, 6);
        f.mini = (d1 >> 30) & 1;
        if (!f.mini) {
          cur_slot = ((d0 >> 20) & 0x1F) * 3 + ((d0 >> 25) & 0x3);
          cur_stride = d2 & 0xFF;
        }
        f.fetch_slot = cur_slot;
        f.stride_dwords = cur_stride;
        f.offset_dwords = detail::SignExtend((d2 >> 8) & 0x7FFFFF, 23);
        out.push_back(f);
      }
      if (detail::IsExecEnd(op)) { ended = true; break; }
    }
    if (ended) break;
  }
  return out;
}

inline bool SelectPosition(const std::vector<VertexFetch>& fetches, int override_index, PosLayout* out) {
  const VertexFetch* f = nullptr;
  if (override_index >= 0) {
    if (size_t(override_index) >= fetches.size()) return false;
    f = &fetches[size_t(override_index)];
  } else {
    for (const auto& c : fetches) {
      if (!c.mini) { f = &c; break; }
    }
  }
  if (!f || f->stride_dwords == 0) return false;
  PosLayout l;
  l.format = PosFormatFromXenos(f->format);
  if (l.format == PosFormat::kUnknown || f->offset_dwords < 0) return false;
  l.is_signed = f->is_signed;
  l.normalized = f->normalized;
  l.exp_adjust = f->exp_adjust;
  l.offset_bytes = uint32_t(f->offset_dwords) * 4;
  l.stride_bytes = f->stride_dwords * 4;
  l.fetch_slot = f->fetch_slot;
  l.swizzle = f->dst_swizzle;
  *out = l;
  return true;
}

}  // namespace fable2::native::capture
