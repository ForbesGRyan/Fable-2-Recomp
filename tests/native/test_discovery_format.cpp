#include "../../src/native/capture/discovery_format.h"
#include "../../src/native/capture/render_state.h"
#include <iostream>
#include <iterator>
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
  // A row's "rs" members (frame-map section 15, "Bias"): one "<REG>":"0x<word>" per register the
  // reader gives, in the list's order; the list ends with the stencil reference words (0x210C back
  // faces, 0x210D) and the four polygon offset registers (0x2380..0x2383).
  {
    using namespace fable2::native::capture;
    const size_t n = std::size(kRowStateRegisters);
    const std::string all = RegisterWords(kRowStateRegisters, n, [](uint32_t reg, uint32_t* word) {
      *word = 0xA0000000u | reg;
      return true;
    });
    const std::string head = "\"2104\":\"0xA0002104\",\"210E\":\"0xA000210E\",\"2200\":\"0xA0002200\",";
    const std::string tail = ",\"2001\":\"0xA0002001\",\"210C\":\"0xA000210C\",\"210D\":\"0xA000210D\","
                             "\"2380\":\"0xA0002380\",\"2381\":\"0xA0002381\",\"2382\":\"0xA0002382\","
                             "\"2383\":\"0xA0002383\"";
    if (all.compare(0, head.size(), head) != 0) return 12;
    if (all.size() < tail.size() || all.compare(all.size() - tail.size(), tail.size(), tail) != 0) return 13;
    if (all.size() != n * 20 - 1) return 14;  // twenty members of 19 characters and the commas between
    // A register the reader does not give is left out, with no stray comma at either end.
    const uint32_t three[] = {0x2380, 0x2381, 0x210D};
    auto but = [](uint32_t skip) {
      return [skip](uint32_t reg, uint32_t* word) {
        *word = reg == 0x210D ? 0x00FFFF2Au : 0x3F000000u;
        return reg != skip;
      };
    };
    if (RegisterWords(three, 3, but(0x2380)) != "\"2381\":\"0x3F000000\",\"210D\":\"0x00FFFF2A\"") return 15;
    if (RegisterWords(three, 3, but(0x2381)) != "\"2380\":\"0x3F000000\",\"210D\":\"0x00FFFF2A\"") return 16;
    if (RegisterWords(three, 3, but(0x210D)) != "\"2380\":\"0x3F000000\",\"2381\":\"0x3F000000\"") return 17;
    if (RegisterWords(three, 3, [](uint32_t, uint32_t*) { return false; }) != "") return 18;
    if (RegisterWords(three, 0, but(0)) != "") return 19;
  }
  // The head of a "verts" row (a BeginVertices draw: r4 the primitive type, r5 the vertex count,
  // r6 the vertex size in bytes): the kind, the frame, the decoded primitive and count, the four
  // argument registers as the other decoded rows have them, the device and the bracket.
  {
    using namespace fable2::native::capture;
    const uint32_t args[4] = {8, 3, 16, 8};
    if (VertsRowHead(7, "D3DDevice_BeginVertices?", args, 0x4410B500, true) !=
        "{\"kind\":\"verts\",\"frame\":7,\"func\":\"D3DDevice_BeginVertices?\",\"prim\":8,\"count\":3,"
        "\"args\":[\"0x00000008\",\"0x00000003\",\"0x00000010\",\"0x00000008\"],\"device\":\"0x4410B500\","
        "\"in_scene\":true") {
      return 20;
    }
    // The primitive type is the low six bits of r4, as the builder packs it (0x8220641C `clrlwi
    // r10,r22,26`).
    const uint32_t quads[4] = {0x4D, 0xE0, 4, 0x110};
    const std::string q = VertsRowHead(12, "f", quads, 0, false);
    if (q.find("\"prim\":13,\"count\":224,") == std::string::npos) return 21;
    const std::string end = "\"in_scene\":false";
    if (q.size() < end.size() || q.compare(q.size() - end.size(), end.size(), end) != 0) return 22;
  }
  std::cout << "PASS: discovery format\n";
  return 0;
}
