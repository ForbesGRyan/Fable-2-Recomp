// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/bone_skin.h"
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
  BoneSkin s;
  s.active = true;
  s.index_offset_bytes = 12;
  s.index_shift[0] = 0;  // component x
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
  BoneSkin w = s;
  w.index_offset_bytes = 0;
  if (!BoneIndex(word, 4, 0, w, &b) || b != 0) return 8;  // 8in32: last byte
  BoneSkin le = w;
  le.index_endian = 0;
  if (!BoneIndex(word, 4, 0, le, &b) || b != 7) return 9;
  le.index_shift[0] = 8;  // component y
  word[1] = 3;
  if (!BoneIndex(word, 4, 0, le, &b) || b != 3) return 10;
  if (BoneIndex(word, 3, 0, le, &b)) return 11;  // out of range
  BoneSkin bad = w;
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
  const SkinSpec spec{2, -1, {6, 7, 8}, {0, 0, 0}, 1, {2, 0, 0, 0}, {0, 0, 0, 0}};
  PosLayout lpos;
  if (!SelectPosition(fs, -1, &lpos, 0xAC1)) return 20;
  BoneSkin sel;
  uint32_t bone_slot = 0;
  if (!SelectSkin(fs, spec, lpos, &sel, &bone_slot)) return 21;
  if (bone_slot != 92 || sel.index_offset_bytes != 12 || sel.index_shift[0] != 0) return 22;
  if (sel.bone_stride != 24 || sel.rows[1].offset_bytes != 8 || sel.rows[2].offset_bytes != 16) return 23;
  if (sel.rows[0].format != PosFormat::kHalf4 || sel.rows[0].swizzle != 0x4C1) return 24;
  if (!sel.active) return 25;
  // Component y of the index fetch is not written: no index.
  if (SelectSkin(fs, SkinSpec{2, -1, {6, 7, 8}, {0, 0, 0}, 1, {1, 0, 0, 0}, {0, 0, 0, 0}}, lpos, &sel, &bone_slot)) return 26;
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
  if (SelectSkin(fs, SkinSpec{2, -1, {6, 7, 9}, {0, 0, 0}, 1, {2, 0, 0, 0}, {0, 0, 0, 0}}, lpos, &sel, &bone_slot)) return 30;

  // --- weighted blend: two bones at 50/50, the other two influences zero ---
  {
    const uint32_t wstride = 28;
    std::vector<uint8_t> wvb(2 * wstride, 0);
    PutHalf4(&wvb[0], 2, 0, 0, 99);
    // Index word (8in32, big-endian in memory): x = bone 0, y = bone 1, z = 9 (past the palette), w = 9.
    PutBe32(&wvb[12], 0x09090100);
    // Weight word: x = 128, y = 127, z = 0, w = 0  -> 128/255 and 127/255.
    PutBe32(&wvb[16], 0x00007F80);
    // Vertex 1: all weights zero.
    PutHalf4(&wvb[wstride], 1, 1, 1, 99);
    PutBe32(&wvb[wstride + 12], 0x00000000);
    PutBe32(&wvb[wstride + 16], 0x00000000);
    std::vector<uint8_t> wpal(2 * 24, 0);
    // Bone 0: identity. Bone 1: x' = x + 10.
    PutHalf4(&wpal[0], 1, 0, 0, 0); PutHalf4(&wpal[8], 0, 1, 0, 0); PutHalf4(&wpal[16], 0, 0, 1, 0);
    PutHalf4(&wpal[24], 1, 0, 0, 10); PutHalf4(&wpal[32], 0, 1, 0, 0); PutHalf4(&wpal[40], 0, 0, 1, 0);
    PosLayout wpos;
    wpos.format = PosFormat::kHalf4; wpos.stride_bytes = wstride; wpos.swizzle = 0xAC1; wpos.swap16 = true;
    BoneSkin w;
    w.active = true; w.bones = 4; w.weighted = true;
    w.index_offset_bytes = 12; w.index_endian = 2;
    w.weight_offset_bytes = 16;
    for (uint32_t k = 0; k < 4; ++k) { w.index_shift[k] = 8 * k; w.weight_shift[k] = 8 * k; }
    w.bone_stride = 24;
    for (int k = 0; k < 3; ++k) {
      w.rows[k].format = PosFormat::kHalf4; w.rows[k].stride_bytes = 24; w.rows[k].offset_bytes = uint32_t(8 * k);
      w.rows[k].swizzle = 0x688; w.rows[k].swap16 = false;
    }
    Float4 wp[2];
    if (!DecodePositions(wvb.data(), wvb.size(), wpos, 0, 2, wp)) return 60;
    if (!SkinPositions(wvb.data(), wvb.size(), wpal.data(), wpal.size(), w, wstride, 0, 2, wp)) return 61;
    // x' = 2 + 10 * 127/255; the zero-weight influences name bone 9 (past the palette) and must not cull.
    if (!Near(wp[0].x, 2.0f + 10.0f * 127.0f / 255.0f) || !Near(wp[0].y, 0) || !Near(wp[0].w, 1)) return 62;
    // All weights zero: culled (NaN), not left at the origin.
    if (!std::isnan(wp[1].x)) return 63;
    // A nonzero weight on a bone past the palette culls the vertex.
    PutBe32(&wvb[16], 0x00017F80);  // z weight 1 -> bone 9
    if (!DecodePositions(wvb.data(), wvb.size(), wpos, 0, 1, wp)) return 64;
    if (!SkinPositions(wvb.data(), wvb.size(), wpal.data(), wpal.size(), w, wstride, 0, 1, wp)) return 65;
    if (!std::isnan(wp[0].x)) return 66;
  }
  // --- SelectSkin with a weight fetch and pairs ---
  {
    std::vector<VertexFetch> sf(6);
    sf[0].format = 32; sf[0].fetch_slot = 95; sf[0].stride_dwords = 7;                       // position
    sf[1].format = 6; sf[1].fetch_slot = 95; sf[1].stride_dwords = 7; sf[1].offset_dwords = 3;
    sf[1].normalized = false; sf[1].mini = true; sf[1].dst_swizzle = 0x688;                  // indices
    sf[2].format = 6; sf[2].fetch_slot = 95; sf[2].stride_dwords = 7; sf[2].offset_dwords = 4;
    sf[2].normalized = true; sf[2].mini = true; sf[2].dst_swizzle = 0x60A;                   // weights .zyxw
    for (int k = 0; k < 3; ++k) {
      sf[3 + k].format = 32; sf[3 + k].fetch_slot = 92; sf[3 + k].stride_dwords = 6; sf[3 + k].offset_dwords = 2 * k;
    }
    PosLayout sp;
    sp.format = PosFormat::kHalf4; sp.stride_bytes = 28; sp.fetch_slot = 95;
    // Pairs (index component, weight component): (x, z), (y, y), (z, x), (w, w).
    SkinSpec spec{1, 2, {3, 4, 5}, {0, 0, 0}, 4, {0, 1, 2, 3}, {2, 1, 0, 3}};
    BoneSkin sel;
    uint32_t slot = 0;
    if (!SelectSkin(sf, spec, sp, &sel, &slot) || slot != 92 || !sel.weighted || sel.bones != 4) return 67;
    // Weight register component z reads source x (swizzle .zyxw): shift 0; component x reads source z: shift 16.
    if (sel.weight_shift[0] != 0 || sel.weight_shift[2] != 16 || sel.weight_offset_bytes != 16) return 68;
    if (sel.index_shift[1] != 8 || sel.index_offset_bytes != 12) return 69;
    // Rejections: weights not normalized 8_8_8_8, weights in another stream, zero or five bones.
    std::vector<VertexFetch> bad = sf; bad[2].normalized = false;
    if (SelectSkin(bad, spec, sp, &sel, &slot)) return 70;
    bad = sf; bad[2].fetch_slot = 94;
    if (SelectSkin(bad, spec, sp, &sel, &slot)) return 71;
    SkinSpec none = spec; none.bones = 0;
    if (SelectSkin(sf, none, sp, &sel, &slot)) return 72;
    SkinSpec five = spec; five.bones = 5;
    if (SelectSkin(sf, five, sp, &sel, &slot)) return 73;
  }

  std::cout << "PASS: bone skin\n";
  return 0;
}
