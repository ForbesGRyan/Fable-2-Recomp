#include "../../src/native/capture/discovery_format.h"
#include "../../src/native/capture/render_state.h"
#include <initializer_list>
#include <iostream>
#include <string>
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
  // The head of an "indx" row (the indexed immediate builder 0x82217EE8: r4 the primitive type,
  // r5 the base vertex, r6 the vertex count, r7 the index count, r8 the index format with bit 2 for
  // 32-bit indices, r9 the vertex size in bytes, r10 where it returns the index storage): the sky's
  // cube is 8 vertices and 36 indices of a triangle list.
  {
    using namespace fable2::native::capture;
    const uint32_t sky[7] = {4, 0, 8, 36, 1, 12, 0x7049CA94};
    if (IndxRowHead(2, "DrawIndx:82217EE8", sky, 0x4450AD80, true) !=
        "{\"kind\":\"indx\",\"frame\":2,\"func\":\"DrawIndx:82217EE8\",\"prim\":4,\"base\":0,\"vertices\":8,"
        "\"indices\":36,\"index32\":false,\"stride\":12,\"args\":[\"0x00000004\",\"0x00000000\",\"0x00000008\","
        "\"0x00000024\",\"0x00000001\",\"0x0000000C\",\"0x7049CA94\"],\"device\":\"0x4450AD80\",\"in_scene\":true") {
      return 23;
    }
    const uint32_t wide[7] = {0x44, 0xFFFFFFFE, 3, 5, 4, 20, 0};
    const std::string w = IndxRowHead(1, "f", wide, 0, false);
    if (w.find("\"prim\":4,\"base\":-2,\"vertices\":3,\"indices\":5,\"index32\":true,\"stride\":20,") ==
        std::string::npos) {
      return 24;
    }
    // The storage the builder reserves, in dwords (0x82217EF8 `mullw r11,r6,r9` then / 4; 16-bit
    // indices are rounded up to whole dwords, 0x82217F24 `addi r11,r23,1` and a shift).
    if (IndxVertexDwords(8, 12) != 24 || IndxVertexDwords(4, 20) != 20 || IndxVertexDwords(3, 6) != 4) return 25;
    if (IndxIndexDwords(36, false) != 18 || IndxIndexDwords(5, false) != 3 || IndxIndexDwords(5, true) != 5) {
      return 26;
    }
    // A product that does not fit 32 bits is the builder's own wrapped value, never more.
    if (IndxVertexDwords(0x10000, 0x10000) != 0) return 27;
    // Bytes as hex, two upper-case digits each, in memory order; nothing for no bytes.
    const uint8_t b[] = {0x00, 0x3F, 0x80, 0xFF, 0x0A};
    if (HexBytes(b, 5) != "003F80FF0A" || HexBytes(b, 0) != "" || HexBytes(nullptr, 5) != "") return 28;
  }
  {
    using namespace fable2::native::capture;
    // Post discovery (frame-map section 17): whether a run of command dwords, as the guest stores them
    // (big-endian), holds a type-0 packet header that writes one of the copy registers 0x2318..0x231B
    // (RB_COPY_CONTROL, _DEST_BASE, _DEST_PITCH, _DEST_INFO): what a resolve is issued with.
    auto be = [](std::initializer_list<uint32_t> dwords) {
      std::string s;
      for (uint32_t d : dwords) {
        s += char(d >> 24); s += char(d >> 16); s += char(d >> 8); s += char(d);
      }
      return s;
    };
    auto names = [](const std::string& s) {
      return NamesCopyRegisters(reinterpret_cast<const uint8_t*>(s.data()), s.size() / 4);
    };
    // Four registers from 0x2318: header (4 - 1) << 16 | 0x2318.
    if (!names(be({0x00002200, 0x00708764, 0x00032318, 0x00300000, 0x12D69000, 0x00B40140, 0x00000102}))) return 30;
    // A run that starts below and reaches them; one that ends just below; one that starts above.
    if (!names(be({0x00082310, 0, 0, 0, 0, 0, 0, 0, 0, 0}))) return 31;
    if (names(be({0x00072310, 0, 0, 0, 0, 0, 0, 0, 0}))) return 32;
    if (names(be({0x0003231C, 0, 0, 0, 0}))) return 33;
    // With the write-one flag (bit 15) every value goes to the base register.
    if (!names(be({0x0005A319, 0, 0, 0, 0, 0, 0})) || names(be({0x0005A310, 0, 0, 0, 0, 0, 0}))) return 34;
    // A type-3 packet (a draw), a type-1 and a type-2 packet are no register runs, whatever their low bits.
    if (names(be({0xC0012318, 0, 0})) || names(be({0x40002318, 0, 0})) || names(be({0x80002318}))) return 35;
    // Nothing for no dwords.
    if (names("") || NamesCopyRegisters(nullptr, 4)) return 36;
  }
  std::cout << "PASS: discovery format\n";
  return 0;
}
