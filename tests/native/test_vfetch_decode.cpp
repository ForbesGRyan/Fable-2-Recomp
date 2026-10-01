// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/vfetch_decode.h"
#include <iostream>

using namespace fable2::native::capture;

// 48-bit exec CF instruction.
static uint64_t Exec(uint32_t opcode, uint32_t address, uint32_t count, uint32_t sequence) {
  return uint64_t(address & 0xFFF) | (uint64_t(count & 7) << 12) | (uint64_t(sequence & 0xFFF) << 16) |
         (uint64_t(opcode & 0xF) << 44);
}
static void PackCf(uint64_t a, uint64_t b, uint32_t* dw) {
  dw[0] = uint32_t(a);
  dw[1] = uint32_t((a >> 32) & 0xFFFF) | (uint32_t(b & 0xFFFF) << 16);
  dw[2] = uint32_t(b >> 16);
}
static void Vfetch(uint32_t* dw, uint32_t dst, uint32_t ci, uint32_t sel, uint32_t fmt, bool sgn, bool norm,
                   int exp, bool mini, uint32_t stride, int32_t offset) {
  dw[0] = 0u | (dst << 12) | (1u << 19) | (ci << 20) | (sel << 25);
  dw[1] = 0x688u | (uint32_t(sgn) << 12) | (uint32_t(!norm) << 13) | (fmt << 16) |
          ((uint32_t(exp) & 0x3F) << 24) | (uint32_t(mini) << 30);
  dw[2] = stride | ((uint32_t(offset) & 0x7FFFFF) << 8);
}

int main() {
  // CF pair 0: exec(addr=2, count=3, seq: fetch, fetch, alu) + exec_end(addr=5, count=1, fetch).
  uint32_t ucode[3 * 6] = {};
  PackCf(Exec(1, 2, 3, 0b000101), Exec(2, 5, 1, 0b01), ucode);
  // Instructions start at address 2 (dword 6).
  Vfetch(ucode + 6, 0, 0, 1, 57, true, true, 0, false, 8, 0);   // float3 pos, slot 1, stride 8 dw
  Vfetch(ucode + 9, 1, 0, 1, 26, true, true, 0, true, 0, 3);    // mini: inherits slot/stride
  // ucode + 12 is an ALU instruction (sequence bit 0) - left zero.
  Vfetch(ucode + 15, 2, 1, 0, 32, false, true, -2, false, 4, 1); // half4, slot 3, stride 4
  auto f = DecodeVertexFetches(ucode, 18);
  if (f.size() != 3) return 1;
  if (f[0].fetch_slot != 1 || f[0].format != 57 || f[0].stride_dwords != 8 || f[0].mini) return 2;
  if (!f[1].mini || f[1].fetch_slot != 1 || f[1].stride_dwords != 8 || f[1].offset_dwords != 3) return 3;
  if (f[2].fetch_slot != 3 || f[2].exp_adjust != -2 || f[2].offset_dwords != 1 || f[2].instr_index != 3) return 4;
  PosLayout pos;
  if (!SelectPosition(f, -1, &pos)) return 5;
  if (pos.format != PosFormat::kFloat3 || pos.stride_bytes != 32 || pos.offset_bytes != 0 || pos.fetch_slot != 1) return 6;
  if (!SelectPosition(f, 2, &pos) || pos.format != PosFormat::kHalf4 || pos.offset_bytes != 4) return 7;
  if (SelectPosition(f, 9, &pos)) return 8;
  // A program whose first fetch is a mini fetch, or with no fetches, has no position.
  std::vector<VertexFetch> only_mini = {f[1]};
  only_mini[0].fetch_slot = 0;
  if (SelectPosition(only_mini, -1, &pos)) return 9;
  if (SelectPosition({}, -1, &pos)) return 10;
  // Zero stride full fetch is rejected.
  std::vector<VertexFetch> zero = {f[0]};
  zero[0].stride_dwords = 0;
  if (SelectPosition(zero, -1, &pos)) return 11;
  // Truncated input never reads out of bounds.
  if (!DecodeVertexFetches(ucode, 2).empty()) return 12;
  std::cout << "PASS: vfetch decode\n";
  return 0;
}
