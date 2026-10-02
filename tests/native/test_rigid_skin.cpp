// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/rigid_skin.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include "../../src/native/capture/vfetch_decode.h"

using namespace fable2::native::capture;

static void PutBe16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = uint8_t(v); }
static void PutBe32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = uint8_t(v); }
static bool Near(float a, float b) { return std::fabs(a - b) <= 1e-3f; }

// Half-float bits of small integers and halves (exact).
static uint16_t Half(float f) {
  if (f == 0.0f) return 0;
  const uint16_t sign = f < 0 ? 0x8000 : 0;
  f = std::fabs(f);
  int e = 0;
  float m = std::frexp(f, &e);  // f = m * 2^e, m in [0.5, 1)
  const uint16_t exp = uint16_t(e - 1 + 15);
  const uint16_t man = uint16_t((m * 2.0f - 1.0f) * 1024.0f + 0.5f);
  return uint16_t(sign | (exp << 10) | man);
}

// Writes half4 (a, b, c, d) as the stream stores it: big-endian halves in
// memory order (the GPU's 8in32 pair swap is undone by the fetch swizzle).
static void PutHalf4(uint8_t* p, float a, float b, float c, float d) {
  PutBe16(p, Half(a)); PutBe16(p + 2, Half(b)); PutBe16(p + 4, Half(c)); PutBe16(p + 6, Half(d));
}

int main() {
  // Shader A1F7E9885EC466DF layout: stride 28 bytes, position half4 at 0,
  // bone index = x of the 8_8_8_8 dword at byte 12 (8in32: the dword's last
  // byte in memory); bones of 24 bytes = three half4 rows.
  const uint32_t stride = 28;
  std::vector<uint8_t> vb(3 * stride, 0);
  // Position memory order (p0, p1, p2, garbage); the layout's swizzle "yxw1"
  // with swap16 returns (p0, p1, p2, 1).
  PutHalf4(&vb[0 * stride], 1, 2, 3, 99);
  PutBe32(&vb[0 * stride + 12], 0x00000001);  // bone 1
  PutHalf4(&vb[1 * stride], -1, 0.5f, 4, 99);
  PutBe32(&vb[1 * stride + 12], 0x00000000);  // bone 0
  PutHalf4(&vb[2 * stride], 2, 2, 2, 99);
  PutBe32(&vb[2 * stride + 12], 0x00000005);  // bone 5: past the palette

  std::vector<uint8_t> pal(2 * 24, 0);
  // Bone 0: identity.
  PutHalf4(&pal[0], 1, 0, 0, 0); PutHalf4(&pal[8], 0, 1, 0, 0); PutHalf4(&pal[16], 0, 0, 1, 0);
  // Bone 1: x' = y + 10, y' = -x, z' = 2z + 0.5.
  PutHalf4(&pal[24], 0, 1, 0, 10); PutHalf4(&pal[32], -1, 0, 0, 0); PutHalf4(&pal[40], 0, 0, 2, 0.5f);

  PosLayout pos;
  pos.format = PosFormat::kHalf4;
  pos.stride_bytes = stride;
  pos.swizzle = 0xAC1;  // yxw1
  pos.swap16 = true;
  RigidSkin s;
  s.active = true;
  s.index_offset_bytes = 12;
  s.index_shift = 0;  // component x
  s.index_endian = 2;
  s.bone_stride = 24;
  for (int k = 0; k < 3; ++k) {
    s.rows[k].format = PosFormat::kHalf4;
    s.rows[k].stride_bytes = 24;
    s.rows[k].offset_bytes = 8 * k;
    s.rows[k].swizzle = 0x4C1;  // yxwz: memory order under swap16
    s.rows[k].swap16 = true;
  }

  Float4 out[3];
  if (!DecodePositions(vb.data(), vb.size(), pos, 0, 2, out)) return 1;
  if (!Near(out[0].x, 1) || !Near(out[0].y, 2) || !Near(out[0].z, 3) || !Near(out[0].w, 1)) return 2;
  if (!SkinPositions(vb.data(), vb.size(), pal.data(), pal.size(), s, stride, 0, 2, out)) return 3;
  // Vertex 0, bone 1: (2 + 10, -1, 6.5).
  if (!Near(out[0].x, 12) || !Near(out[0].y, -1) || !Near(out[0].z, 6.5f) || !Near(out[0].w, 1)) return 4;
  // Vertex 1, bone 0: unchanged.
  if (!Near(out[1].x, -1) || !Near(out[1].y, 0.5f) || !Near(out[1].z, 4)) return 5;
  // A bone past the palette (a vertex of another mesh in a shared stream)
  // becomes NaN, which the clay shader culls; the rest of the draw is kept.
  if (!DecodePositions(vb.data(), vb.size(), pos, 0, 3, out)) return 6;
  if (!SkinPositions(vb.data(), vb.size(), pal.data(), pal.size(), s, stride, 0, 3, out)) return 7;
  if (!std::isnan(out[2].x) || !Near(out[0].x, 12) || !Near(out[1].z, 4)) return 13;
  // An unreadable index word or an empty palette fails.
  if (SkinPositions(vb.data(), 2 * stride + 8, pal.data(), pal.size(), s, stride, 0, 3, out)) return 14;
  if (SkinPositions(vb.data(), vb.size(), pal.data(), 10, s, stride, 0, 2, out)) return 15;

  // Index endian: little-endian words read the dword's first byte as x.
  uint8_t word[4] = {7, 0, 0, 0};
  uint32_t b = 0;
  RigidSkin w = s;
  w.index_offset_bytes = 0;
  if (!BoneIndex(word, 4, 0, w, &b) || b != 0) return 8;  // 8in32: last byte
  RigidSkin le = w;
  le.index_endian = 0;
  if (!BoneIndex(word, 4, 0, le, &b) || b != 7) return 9;
  le.index_shift = 8;  // component y
  word[1] = 3;
  if (!BoneIndex(word, 4, 0, le, &b) || b != 3) return 10;
  if (BoneIndex(word, 3, 0, le, &b)) return 11;  // out of range
  RigidSkin bad = w;
  bad.index_endian = 1;  // 8in16: not handled
  if (BoneIndex(word, 4, 0, bad, &b)) return 12;

  // --- Layout from the shader's fetches (A1F7E9885EC466DF, frame-map section 9) ---
  auto fetch = [](uint32_t slot, uint32_t swz, uint32_t fmt, bool mini, uint32_t stride, int32_t off,
                  bool normalized) {
    VertexFetch f;
    f.fetch_slot = slot;
    f.dst_swizzle = swz;
    f.format = fmt;
    f.mini = mini;
    f.stride_dwords = stride;
    f.offset_dwords = off;
    f.is_signed = !normalized;
    f.normalized = normalized;
    return f;
  };
  const std::vector<VertexFetch> fs = {
      fetch(95, 0x4C1, 32, false, 7, 0, false),  // 0: position half4 yxwz
      fetch(95, 0x447, 16, true, 7, 2, true),    // 1: normal
      fetch(95, 0xE3F, 6, true, 7, 3, false),    // 2: r5.z = x of 8_8_8_8 (integer)
      fetch(95, 0xFC1, 31, true, 7, 5, false),   // 3: uv
      fetch(95, 0x688, 7, true, 7, 6, true),     // 4
      fetch(94, 0x213, 38, false, 4, 0, false),  // 5
      fetch(92, 0x4C1, 32, false, 6, 0, false),  // 6: bone row 0
      fetch(92, 0x4C1, 32, false, 6, 2, false),  // 7: bone row 1
      fetch(92, 0x4C1, 32, false, 6, 4, false),  // 8: bone row 2
  };
  const SkinSpec spec{2, 2, {6, 7, 8}};
  PosLayout lpos;
  if (!SelectPosition(fs, -1, &lpos, 0xAC1)) return 20;
  RigidSkin sel;
  uint32_t bone_slot = 0;
  if (!SelectSkin(fs, spec, lpos, &sel, &bone_slot)) return 21;
  if (bone_slot != 92 || sel.index_offset_bytes != 12 || sel.index_shift != 0) return 22;
  if (sel.bone_stride != 24 || sel.rows[1].offset_bytes != 8 || sel.rows[2].offset_bytes != 16) return 23;
  if (sel.rows[0].format != PosFormat::kHalf4 || sel.rows[0].swizzle != 0x4C1) return 24;
  if (!sel.active) return 25;
  // Component y of the index fetch is not written: no index.
  if (SelectSkin(fs, SkinSpec{2, 1, {6, 7, 8}}, lpos, &sel, &bone_slot)) return 26;
  // The index must come from the position's stream, as an integer 8_8_8_8.
  std::vector<VertexFetch> other = fs;
  other[2].fetch_slot = 94;
  if (SelectSkin(other, spec, lpos, &sel, &bone_slot)) return 27;
  other = fs;
  other[2].normalized = true;
  if (SelectSkin(other, spec, lpos, &sel, &bone_slot)) return 28;
  // Rows from different streams, or a fetch index past the list, fail.
  other = fs;
  other[7].fetch_slot = 91;
  if (SelectSkin(other, spec, lpos, &sel, &bone_slot)) return 29;
  if (SelectSkin(fs, SkinSpec{2, 2, {6, 7, 9}}, lpos, &sel, &bone_slot)) return 30;

  std::cout << "PASS: rigid skin\n";
  return 0;
}
