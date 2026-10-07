#include "../../src/native/capture/discovery_format.h"
#include <iostream>
int main() {
  const uint32_t v[2] = {0x1, 0xDEADBEEF};
  if (fable2::native::capture::HexDwords(v, 2) != "[\"0x00000001\",\"0xDEADBEEF\"]") return 1;
  if (fable2::native::capture::HexDwords(nullptr, 2) != "null") return 2;
  // Big-endian float32 as the guest stores them (discovery rows "pconst"): shortest text that
  // round-trips, the non-finite values as the bare tokens Python's json reads.
  const uint8_t be[] = {0x3F, 0x80, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x00, 0x7F, 0xC0, 0x00, 0x00,
                        0x7F, 0x80, 0x00, 0x00, 0xFF, 0x80, 0x00, 0x00, 0x3D, 0xCC, 0xCC, 0xCD};
  if (fable2::native::capture::BeFloats(be, 6) != "[1,-0.5,NaN,Infinity,-Infinity,0.100000001]") return 3;
  if (fable2::native::capture::BeFloats(be + 4, 1) != "[-0.5]") return 4;
  if (fable2::native::capture::BeFloats(be, 0) != "[]") return 5;
  if (fable2::native::capture::BeFloats(nullptr, 6) != "null") return 6;
  std::string s = "x";
  fable2::native::capture::AppendFloat(s, 0.25f);
  if (s != "x0.25") return 7;
  // Constant registers by their bits (discovery rows "pfull", "ptiles", "pbank_diff"): 32 hex
  // digits per register, x first; a register that was not read is 32 dashes; no register, "".
  const uint32_t regs[12] = {0x3F800000, 0xBF000000, 0x00000000, 0x7FC00001, 1, 2, 3, 4,
                             0xDEADBEEF, 0x80000000, 0x0000000A, 0xFFFFFFFF};
  const bool have[3] = {true, false, true};
  if (fable2::native::capture::RegistersHex(regs, have, 3) !=
      "3F800000BF000000000000007FC00001--------------------------------DEADBEEF800000000000000AFFFFFFFF") {
    return 8;
  }
  if (fable2::native::capture::RegistersHex(regs + 4, nullptr, 1) != "00000001000000020000000300000004") return 9;
  if (fable2::native::capture::RegistersHex(regs, have, 0) != "") return 10;
  if (fable2::native::capture::RegistersHex(nullptr, have, 2) !=
      "----------------------------------------------------------------") {
    return 11;
  }
  std::cout << "PASS: discovery format\n";
  return 0;
}
