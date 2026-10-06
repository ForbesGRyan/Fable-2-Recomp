// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/uv_decode.h"
#include <cmath>
#include <vector>

using namespace fable2::native::capture;

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
static void PutBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
static void PutBeF(uint8_t* p, float f) { uint32_t u; std::memcpy(&u, &f, 4); p[0] = u >> 24; p[1] = u >> 16; p[2] = u >> 8; p[3] = u; }

int main() {
  if (UvFormatFromXenos(31) != UvFormat::kHalf2 || UvFormatFromXenos(37) != UvFormat::kFloat2) return 1;
  if (UvFormatFromXenos(25) != UvFormat::kShort2 || UvFormatFromXenos(26) != UvFormat::kShort4) return 2;
  if (UvFormatFromXenos(32) != UvFormat::kHalf4 || UvFormatFromXenos(57) != UvFormat::kFloat3) return 3;
  if (UvFormatFromXenos(38) != UvFormat::kFloat4 || UvFormatFromXenos(6) != UvFormat::kUnknown) return 4;

  // Shader 0xECD66A10092E6562: vfetch_mini r6.yx, Offset=3, FMT_16_16_FLOAT,
  // stride 5 dwords, endian 8in32; o0.x = r6.x = source y, o0.y = source x.
  VertexFetch f;
  f.format = 31; f.fetch_slot = 95; f.stride_dwords = 5; f.offset_dwords = 3; f.mini = true;
  UvLayout l;
  if (!UvLayoutFromFetch(f, 1, 0, &l)) return 5;
  if (l.format != UvFormat::kHalf2 || l.stride_bytes != 20 || l.offset_bytes != 12 || l.fetch_slot != 95) return 6;
  if (!ApplyUvEndian(&l, 2) || !l.swap16) return 7;
  // Memory halves m0 = 0.25 (0x3400), m1 = 0.75 (0x3A00) at vertex 1.
  std::vector<uint8_t> vb(40, 0);
  PutBe16(vb.data() + 20 + 12, 0x3400);
  PutBe16(vb.data() + 20 + 14, 0x3A00);
  Float2 uv[1];
  if (!DecodeUvs(vb.data(), vb.size(), l, 1, 1, uv)) return 8;
  // GPU source y under 8in32 is memory component 0, source x is memory 1.
  if (!Near(uv[0].u, 0.25f) || !Near(uv[0].v, 0.75f)) return 9;
  // Reading past the buffer fails.
  if (DecodeUvs(vb.data(), 30, l, 1, 1, uv)) return 10;

  // float2 under 8in32 (32-bit components are not pair-swapped).
  VertexFetch g;
  g.format = 37; g.stride_dwords = 2; g.offset_dwords = 0;
  if (!UvLayoutFromFetch(g, 0, 1, &l) || !ApplyUvEndian(&l, 2) || l.swap16) return 11;
  std::vector<uint8_t> fb(8);
  PutBeF(fb.data(), 2.5f); PutBeF(fb.data() + 4, -1.0f);
  if (!DecodeUvs(fb.data(), fb.size(), l, 0, 1, uv) || !Near(uv[0].u, 2.5f) || !Near(uv[0].v, -1.0f)) return 12;
  // 16-bit normalized signed, 8in16 (memory order), with exp adjust -1.
  VertexFetch h;
  h.format = 25; h.stride_dwords = 1; h.is_signed = true; h.normalized = true; h.exp_adjust = -1;
  if (!UvLayoutFromFetch(h, 0, 1, &l) || !ApplyUvEndian(&l, 1) || l.swap16) return 13;
  std::vector<uint8_t> sb(4);
  PutBe16(sb.data(), 32767); PutBe16(sb.data() + 2, uint16_t(-32767));
  if (!DecodeUvs(sb.data(), sb.size(), l, 0, 1, uv) || !Near(uv[0].u, 0.5f) || !Near(uv[0].v, -0.5f)) return 14;
  // Rejections: unknown format, component past the element, bad endian, zero stride.
  VertexFetch bad = g; bad.format = 6;
  if (UvLayoutFromFetch(bad, 0, 1, &l)) return 15;
  if (UvLayoutFromFetch(g, 0, 2, &l)) return 16;  // float2 has no component 2
  if (!UvLayoutFromFetch(g, 0, 1, &l) || ApplyUvEndian(&l, 1)) return 17;  // 32-bit needs 8in32
  VertexFetch zero = g; zero.stride_dwords = 0;
  if (UvLayoutFromFetch(zero, 0, 1, &l)) return 18;

  // u and v in two elements of one vertex: the instancing shaders (0x8123C16DBF583F92
  // and relatives) keep u in the fourth half of the position element (dword 0) and
  // v in the fourth half of the normal element (dword 2); both read source z,
  // stride 6 dwords, endian 8in32. Vertices 0-2 of a grass mesh
  // (native_geo_20261005_103213/1C07BDC0_00000120.bin).
  static const uint8_t grass[72] = {
      0x32, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x3B, 0xFE, 0xA8, 0xF8, 0x3C, 0x00,
      0xBC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBC, 0x00,
      0xA5, 0x9E, 0x24, 0xF8, 0x38, 0x00, 0x34, 0xFD, 0x80, 0x00, 0x3B, 0xFE, 0xA8, 0xF8, 0x00, 0x00,
      0xBC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBC, 0x00,
      0xB2, 0x66, 0x00, 0x00, 0x00, 0x00, 0x38, 0xFD, 0x80, 0x00, 0x3B, 0xFE, 0xA8, 0xF8, 0x3C, 0x00,
      0xBC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBC, 0x00,
  };
  VertexFetch pos;
  pos.format = 32; pos.fetch_slot = 95; pos.stride_dwords = 6; pos.offset_dwords = 0;
  VertexFetch nrm = pos;
  nrm.offset_dwords = 2; nrm.mini = true;
  if (!UvLayoutFromFetches(pos, nrm, 2, 2, &l) || !ApplyUvEndian(&l, 2)) return 19;
  if (l.offset_bytes != 0 || l.v_element_delta != 8 || l.stride_bytes != 24 || l.fetch_slot != 95) return 20;
  if (UvElementEnd(l) != 16) return 21;
  Float2 guv[3];
  if (!DecodeUvs(grass, sizeof(grass), l, 0, 3, guv)) return 22;
  if (!Near(guv[0].u, 0.0f) || !Near(guv[0].v, 1.0f)) return 23;
  if (std::fabs(guv[1].u - 0.3118f) > 1e-3f || !Near(guv[1].v, 0.0f)) return 24;
  if (std::fabs(guv[2].u - 0.6235f) > 1e-3f || !Near(guv[2].v, 1.0f)) return 25;
  // The v element of the last vertex must lie inside the buffer too.
  if (DecodeUvs(grass, 48 + 8, l, 0, 3, guv) || !DecodeUvs(grass, 48 + 16, l, 0, 3, guv)) return 26;
  // The other way round (u from the later element): a negative step.
  if (!UvLayoutFromFetches(nrm, pos, 2, 2, &l) || !ApplyUvEndian(&l, 2)) return 27;
  if (l.offset_bytes != 8 || l.v_element_delta != -8 || UvElementEnd(l) != 16) return 28;
  if (!DecodeUvs(grass, sizeof(grass), l, 1, 1, guv) || !Near(guv[0].u, 0.0f) ||
      std::fabs(guv[0].v - 0.3118f) > 1e-3f) {
    return 29;
  }
  // One fetch for both axes is the single-element layout.
  UvLayout one, two;
  if (!UvLayoutFromFetch(f, 1, 0, &one) || !UvLayoutFromFetches(f, f, 1, 0, &two)) return 30;
  if (two.v_element_delta != 0 || two.offset_bytes != one.offset_bytes || two.comp_u != 1 || two.comp_v != 0 ||
      UvElementEnd(one) != 16) {
    return 31;
  }
  // Two elements must share the stream, the stride and the element format.
  VertexFetch other = nrm; other.fetch_slot = 94;
  if (UvLayoutFromFetches(pos, other, 2, 2, &l)) return 32;
  other = nrm; other.stride_dwords = 7;
  if (UvLayoutFromFetches(pos, other, 2, 2, &l)) return 33;
  other = nrm; other.format = 26;
  if (UvLayoutFromFetches(pos, other, 2, 2, &l)) return 34;
  other = nrm; other.format = 6;
  if (UvLayoutFromFetches(pos, other, 2, 2, &l) || UvLayoutFromFetches(other, pos, 2, 2, &l)) return 35;
  if (UvLayoutFromFetches(pos, nrm, 2, 4, &l) || UvLayoutFromFetches(pos, nrm, 4, 2, &l)) return 36;
  return 0;
}
