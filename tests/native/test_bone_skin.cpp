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

// --- A real draw: shader D4D558DA6A82BDC8 (four-bone blend), frame-map section 12 ---
// position_check.py cpp_fixture() for eight vertices of the dog's body draw (4337 vertices, 90 bones)
// in native_discovery_20261005_103213.jsonl, frame 3: one, two, three and four influences. Streams
// 1C0F0040_0001DA5C.bin (vertices) and 1C9F5040_00000870.bin (palette, cut after bone 26).
// Fetches in DecodeVertexFetches order: {instr_index, fetch_slot, dst_reg, dst_swizzle, format, is_signed,
// normalized, mini, exp_adjust, stride_dwords, offset_dwords}.
static const VertexFetch kFixtureFetches[19] = {
    {0, 95, 7, 0x4C1, 32, true, false, false, 0, 7, 0},
    {1, 95, 0, 0x447, 16, true, true, true, 0, 7, 2},
    {2, 95, 2, 0x688, 6, false, false, true, 0, 7, 3},
    {3, 95, 4, 0x60A, 6, false, true, true, 0, 7, 4},
    {4, 95, 5, 0xFC1, 31, true, false, true, 0, 7, 5},
    {5, 95, 8, 0x688, 7, true, true, true, 0, 7, 6},
    {6, 94, 3, 0x213, 38, true, false, false, 0, 4, 0},
    {7, 92, 10, 0x4C1, 32, true, false, false, 0, 6, 0},
    {8, 92, 11, 0x4C1, 32, true, false, false, 0, 6, 2},
    {9, 92, 12, 0x4C1, 32, true, false, false, 0, 6, 4},
    {10, 92, 15, 0x4C1, 32, true, false, false, 0, 6, 0},
    {11, 92, 14, 0x4C1, 32, true, false, false, 0, 6, 2},
    {12, 92, 13, 0x4C1, 32, true, false, false, 0, 6, 4},
    {13, 92, 16, 0x4C1, 32, true, false, false, 0, 6, 0},
    {14, 92, 17, 0x4C1, 32, true, false, false, 0, 6, 2},
    {15, 92, 18, 0x4C1, 32, true, false, false, 0, 6, 4},
    {16, 92, 1, 0x4C1, 32, true, false, false, 0, 6, 0},
    {17, 92, 9, 0x4C1, 32, true, false, false, 0, 6, 2},
    {18, 92, 6, 0x4C1, 32, true, false, false, 0, 6, 4},
};
// Fetch slot 95, fetch constant endian 2: guest vertices 719, 1048, 1049, 1079, 1094, 1328, 1893, 3764 re-indexed from 0, 28 bytes per vertex.
static const uint8_t kFixtureStream95[224] = {
    0xA8, 0x57, 0x38, 0xC8, 0x3A, 0xF3, 0x44, 0x00, 0x89, 0x0A, 0x30, 0xB0, 0x1A, 0x17, 0x14, 0x13,
    0x40, 0x66, 0x22, 0x37, 0x38, 0xA8, 0x33, 0x45, 0xF9, 0xFF, 0xCA, 0x0B, 0x2A, 0xCC, 0xB0, 0x9A,
    0x3A, 0x02, 0x44, 0x00, 0x79, 0x45, 0xB1, 0x09, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x8C, 0x73,
    0x36, 0x28, 0x39, 0xD0, 0xF7, 0xCF, 0xD9, 0xED, 0x2A, 0xCC, 0xAE, 0x84, 0x39, 0xF1, 0x44, 0x00,
    0x76, 0xC5, 0xF1, 0x3F, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0xA3, 0x5C, 0x36, 0x27, 0x39, 0x8E,
    0xF6, 0x4F, 0x7D, 0xE5, 0x30, 0x22, 0xA6, 0x33, 0x39, 0x3D, 0x48, 0x00, 0x38, 0x7F, 0xA3, 0x96,
    0x00, 0x03, 0x02, 0x01, 0x00, 0x1B, 0xC8, 0x1C, 0x37, 0x06, 0x39, 0x06, 0xE3, 0x6F, 0xF0, 0xE0,
    0x30, 0xBB, 0xA7, 0x0F, 0x38, 0x36, 0x48, 0x00, 0xD5, 0x39, 0x23, 0xA9, 0x00, 0x03, 0x02, 0x01,
    0x00, 0x2B, 0xB6, 0x1E, 0x37, 0xFE, 0x39, 0x0C, 0xE1, 0xF0, 0xBB, 0x5B, 0x80, 0x00, 0xB4, 0x7A,
    0x37, 0x40, 0x44, 0x00, 0x97, 0x2D, 0xD8, 0x0B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF,
    0x38, 0xC8, 0x39, 0xF7, 0xCB, 0x4B, 0xD2, 0x75, 0xAA, 0x26, 0x38, 0x9C, 0x3A, 0xDD, 0x48, 0x00,
    0xC7, 0x93, 0x65, 0x5E, 0x1A, 0x19, 0x17, 0x13, 0x77, 0x1C, 0x37, 0x35, 0x39, 0x79, 0x39, 0x43,
    0xDC, 0x61, 0xA3, 0x2F, 0xAC, 0x47, 0x38, 0x41, 0x3A, 0xCB, 0x48, 0x00, 0xCD, 0x88, 0x24, 0x7A,
    0x1A, 0x19, 0x13, 0x06, 0x1D, 0x67, 0x34, 0x47, 0x39, 0x5D, 0x39, 0xB0, 0xDB, 0xA3, 0x5F, 0x78,
};
// Fetch slot 92, fetch constant endian 2: bones 0..26, 24 bytes per bone.
static const uint8_t kFixtureStream92[648] = {
    0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x35, 0x6F, 0xBA, 0x4D, 0xB8, 0x1C, 0x36, 0x18,
    0xA8, 0xDA, 0x38, 0x46, 0xBA, 0xC1, 0x37, 0x75, 0x3B, 0x84, 0x34, 0xE6, 0x30, 0xDA, 0x2C, 0x58,
    0x3B, 0x45, 0xB6, 0x0E, 0xB1, 0x8F, 0x31, 0x64, 0x31, 0xE4, 0x39, 0xEA, 0xB9, 0x2E, 0x35, 0x76,
    0x35, 0xFA, 0x38, 0x74, 0x39, 0xEF, 0xB5, 0x32, 0x3B, 0xA5, 0xAD, 0xDA, 0x34, 0x79, 0xB1, 0xAA,
    0x31, 0x42, 0x3B, 0xA2, 0xB3, 0xFC, 0x29, 0x7D, 0xB3, 0xCE, 0x34, 0x8D, 0x3B, 0x6A, 0xB6, 0x9F,
    0x3B, 0xAD, 0xB2, 0xEA, 0x31, 0xB9, 0xAC, 0xE0, 0xA4, 0x3B, 0x38, 0xBE, 0x3A, 0x70, 0xB8, 0x64,
    0xB4, 0x7B, 0xBA, 0x34, 0x38, 0x86, 0x2F, 0x9A, 0x3B, 0x8D, 0xB5, 0x41, 0x28, 0x42, 0x2D, 0x0C,
    0x33, 0xBD, 0x3A, 0x0C, 0x38, 0xDD, 0xB7, 0x3A, 0xB3, 0x32, 0xB8, 0x86, 0x3A, 0x58, 0xB0, 0x57,
    0x39, 0x8A, 0xB9, 0x3C, 0x34, 0xDD, 0xA4, 0xA7, 0x39, 0x00, 0x3A, 0x09, 0x32, 0x69, 0xAC, 0xCC,
    0xB5, 0xC4, 0x2A, 0x8E, 0x3B, 0x73, 0xB8, 0x6B, 0x39, 0x8A, 0xB9, 0x76, 0x33, 0x74, 0x2C, 0x45,
    0x39, 0x00, 0x39, 0xD4, 0x34, 0x77, 0xB0, 0x63, 0xB5, 0xC4, 0xAA, 0x1D, 0x3B, 0x73, 0xB7, 0xF4,
    0x39, 0x8A, 0xB9, 0x76, 0x33, 0x74, 0x2C, 0x45, 0x39, 0x00, 0x39, 0xD4, 0x34, 0x77, 0xB0, 0x63,
    0xB5, 0xC4, 0xAA, 0x1D, 0x3B, 0x73, 0xB7, 0xF4, 0x39, 0x8A, 0xB8, 0xF7, 0x35, 0xE2, 0xAE, 0x54,
    0x39, 0x00, 0x3A, 0x2A, 0x2F, 0xF1, 0xA1, 0x20, 0xB5, 0xC4, 0x30, 0x9B, 0x3B, 0x5F, 0xB8, 0xC2,
    0x39, 0x8A, 0xB8, 0xF7, 0x35, 0xE2, 0xAE, 0x54, 0x39, 0x00, 0x3A, 0x2A, 0x2F, 0xF1, 0xA1, 0x20,
    0xB5, 0xC4, 0x30, 0x9B, 0x3B, 0x5F, 0xB8, 0xC2, 0x3A, 0x05, 0xB9, 0x3C, 0xAC, 0xB4, 0x35, 0xBF,
    0x39, 0x25, 0x3A, 0x08, 0xB0, 0x2A, 0x34, 0x11, 0x30, 0x80, 0x2A, 0x7B, 0x3B, 0xE8, 0xB8, 0xB4,
    0x37, 0x4A, 0xB9, 0x3C, 0x38, 0xD3, 0xB4, 0xE4, 0x37, 0x27, 0x3A, 0x08, 0x37, 0xB0, 0xB5, 0x8A,
    0xBA, 0x28, 0x2A, 0x7B, 0x39, 0x17, 0xB3, 0xC4, 0x39, 0x9B, 0xB9, 0x5D, 0x33, 0xC9, 0x2A, 0x5D,
    0x39, 0x48, 0x39, 0xE9, 0x30, 0x52, 0x9C, 0x55, 0xB4, 0x53, 0x2C, 0x39, 0x3B, 0xAF, 0xB8, 0xB2,
    0x39, 0x2B, 0xB9, 0xF7, 0x31, 0x28, 0x31, 0x3B, 0x3A, 0x10, 0x38, 0xD5, 0xB3, 0xDC, 0x37, 0x18,
    0x2D, 0x7F, 0x34, 0x7E, 0x3B, 0xA5, 0xB9, 0x4C, 0x36, 0x3F, 0xBB, 0x4E, 0x2F, 0x4D, 0x34, 0x32,
    0x3B, 0x4E, 0x35, 0xE6, 0xB1, 0x8D, 0x37, 0xFA, 0x2F, 0x73, 0x31, 0x80, 0x3B, 0xD3, 0xB9, 0x0A,
    0x3A, 0x06, 0xB9, 0x3C, 0x2C, 0x28, 0x32, 0xE9, 0x39, 0x40, 0x3A, 0x08, 0xA1, 0x6E, 0x30, 0x2C,
    0xA9, 0x62, 0x2A, 0x7B, 0x3B, 0xFB, 0xB8, 0xF6, 0x38, 0x7F, 0xB9, 0x80, 0x37, 0x59, 0xB0, 0xC6,
    0x38, 0xCF, 0x39, 0xC4, 0x35, 0x7F, 0xB2, 0x80, 0xB8, 0x8A, 0x2D, 0x52, 0x3A, 0x8D, 0xB6, 0xD3,
    0x3B, 0x46, 0xAF, 0x2D, 0x36, 0x69, 0xB6, 0x5E, 0x22, 0x94, 0x3B, 0xC2, 0x33, 0xC0, 0xB1, 0x09,
    0xB6, 0xA7, 0xB2, 0xE2, 0x3B, 0x11, 0xB5, 0xDC, 0x39, 0x8A, 0xB9, 0x16, 0x35, 0x75, 0xAB, 0x8F,
    0x39, 0x00, 0x3A, 0x1E, 0x31, 0x03, 0xA9, 0x51, 0xB5, 0xC4, 0x2E, 0xB4, 0x3B, 0x6A, 0xB8, 0x95,
    0x39, 0xBC, 0xB9, 0x16, 0x34, 0x90, 0xA2, 0x6D, 0x39, 0x15, 0x3A, 0x1E, 0x2E, 0xE0, 0x99, 0x94,
    0xB4, 0x95, 0x2E, 0xAC, 0x3B, 0x9E, 0xB8, 0xDE, 0x3A, 0x15, 0xB9, 0x16, 0x30, 0x3B, 0x2F, 0x4B,
    0x39, 0x26, 0x3A, 0x1E, 0xA6, 0x6D, 0x2F, 0xEB, 0xAD, 0x73, 0x2E, 0xAC, 0x3B, 0xED, 0xB9, 0x2B,
    0x39, 0x8A, 0xB9, 0xB0, 0xAF, 0xDD, 0x36, 0x75, 0x39, 0x00, 0x38, 0x0C, 0x38, 0xC1, 0xB4, 0x4E,
    0xB5, 0xC4, 0xB7, 0xD0, 0x3A, 0x5B, 0xAC, 0x6C, 0x39, 0x8A, 0xB8, 0x10, 0x38, 0x19, 0xB5, 0x05,
    0x39, 0x00, 0x3A, 0x37, 0xAC, 0xBC, 0x31, 0x1C, 0xB5, 0xC4, 0x35, 0xF1, 0x3A, 0xD8, 0xB9, 0x63,
    0x3A, 0x1C, 0xB8, 0x61, 0x35, 0x73, 0xAF, 0x9F, 0x38, 0x39, 0x3A, 0xAD, 0x31, 0x06, 0xAB, 0x20,
    0xB5, 0xED, 0x2B, 0xAC, 0x3B, 0x6A, 0xB8, 0x7F, 0x39, 0x8A, 0xB7, 0x7E, 0x38, 0x63, 0xB5, 0x5B,
    0x39, 0x00, 0x3A, 0x27, 0xB0, 0x37, 0x33, 0x06, 0xB5, 0xC4, 0x36, 0xF2, 0x3A, 0x9A, 0xB9, 0x32,
    0x39, 0x8A, 0xB7, 0x7E, 0x38, 0x63, 0xB5, 0x50, 0x39, 0x00, 0x3A, 0x27, 0xB0, 0x38, 0x33, 0x3F,
    0xB5, 0xC4, 0x36, 0xF2, 0x3A, 0x9A, 0xB9, 0x55,
};
static const uint32_t kFixtureIndices[8] = {5, 1, 3, 4, 0, 7, 6, 2};
// The checker's positions before the transform rows.
static const float kFixtureExpected[8][4] = {
    {0.36845994f, -0.0659365654f, 0.0509130955f, 1.0f},
    {0.134234369f, -0.243846774f, 0.179075494f, 1.0f},
    {0.167886585f, -0.08464849f, 0.20073089f, 1.0f},
    {0.198963493f, -0.00986197591f, 0.113142192f, 1.0f},
    {-0.176621705f, 0.538054287f, 0.307365477f, 1.0f},
    {-0.159093618f, 0.466152817f, 0.297161162f, 1.0f},
    {-0.172887921f, 0.518150449f, 0.305216014f, 1.0f},
    {0.115892991f, -0.209467351f, 0.192866981f, 1.0f},
};
// The same eight vertices in the game's own posed copy of this mesh: the float3 stream the fur shells
// (shader 7C5710DEF3EE33C4) draw in the next frame, 1C8A69C0_000152D4_AEE2BDFF4664F3F6.bin, in the order of
// kFixtureIndices (guest vertices 1328, 1048, 1079, 1094, 719, 3764, 1893, 1049).
static const float kFixtureGame[8][3] = {
    {0.36845994f, -0.0659365654f, 0.0509130955f},
    {0.134234369f, -0.243846744f, 0.17907548f},
    {0.1678866f, -0.0846485496f, 0.20073089f},
    {0.198963493f, -0.00986199267f, 0.1131422f},
    {-0.17662169f, 0.538054347f, 0.307365566f},
    {-0.159093648f, 0.466152847f, 0.297161192f},
    {-0.172887996f, 0.518150508f, 0.305216014f},
    {0.115892984f, -0.209467351f, 0.192866981f},
};

static int RealDraw() {
  const std::vector<VertexFetch> fetches(kFixtureFetches, kFixtureFetches + 19);
  // The "skin" read from the dump (frame-map section 12): indices fetch 2, weights fetch 3, the rows of
  // the first bone fetches 7-9 read as yxwz, (index, weight) register components (x, z), (y, y), (z, x), (w, w).
  const SkinSpec spec{2, 3, {7, 8, 9}, {0x4C1, 0x4C1, 0x4C1}, 4, {0, 1, 2, 3}, {2, 1, 0, 3}};
  PosLayout pos;
  if (!SelectPosition(fetches, -1, &pos, 0xAC1) || !ApplyFetchEndian(&pos, 2) || pos.fetch_slot != 95) return 90;
  auto select = [&](const SkinSpec& from, BoneSkin* out) {
    uint32_t slot = 0;
    if (!SelectSkin(fetches, from, pos, out, &slot) || slot != 92) return false;
    out->index_endian = 2;
    for (PosLayout& row : out->rows) {
      if (!ApplyFetchEndian(&row, 2)) return false;
    }
    out->palette_size = sizeof(kFixtureStream92);
    return true;
  };
  BoneSkin s;
  if (!select(spec, &s) || s.bones != 4 || !s.weighted) return 91;
  if (s.index_offset_bytes != 12 || s.weight_offset_bytes != 16 || s.bone_stride != 24) return 92;
  // Byte k of the index word goes with byte k of the weight word.
  for (uint32_t k = 0; k < 4; ++k) {
    if (s.index_shift[k] != 8 * k || s.weight_shift[k] != 8 * k) return 93;
  }
  if (PaletteBones(s, s.palette_size) != 27) return 94;
  auto skin = [&](const BoneSkin& with, Float4* out) {
    for (int i = 0; i < 8; ++i) {
      if (!DecodePositions(kFixtureStream95, sizeof(kFixtureStream95), pos, kFixtureIndices[i], 1, &out[i]) ||
          !SkinPositions(kFixtureStream95, sizeof(kFixtureStream95), kFixtureStream92, sizeof(kFixtureStream92), with,
                         pos.stride_bytes, kFixtureIndices[i], 1, &out[i])) {
        return false;
      }
    }
    return true;
  };
  auto distance = [](const Float4& p, const float* q) {
    return std::sqrt((p.x - q[0]) * (p.x - q[0]) + (p.y - q[1]) * (p.y - q[1]) + (p.z - q[2]) * (p.z - q[2]));
  };
  Float4 out[8];
  if (!skin(s, out)) return 95;
  for (int i = 0; i < 8; ++i) {
    // The checker's positions, and the game's own skinning of the same vertices.
    if (!(distance(out[i], kFixtureExpected[i]) < 1e-5f) || out[i].w != 1.0f) return 96;
    if (!(distance(out[i], kFixtureGame[i]) < 1e-5f)) return 97;
  }
  // The bind pose is half a unit away (the dog sits; its mesh is 1.4 units long).
  Float4 bind[8];
  for (int i = 0; i < 8; ++i) {
    if (!DecodePositions(kFixtureStream95, sizeof(kFixtureStream95), pos, kFixtureIndices[i], 1, &bind[i])) return 98;
    if (distance(bind[i], kFixtureGame[i]) < 0.4f) return 99;
  }
  // Index and weight components paired straight (x with x): the two-influence vertices 1 and 7 move.
  BoneSkin straight;
  Float4 other[8];
  if (!select(SkinSpec{2, 3, {7, 8, 9}, {0x4C1, 0x4C1, 0x4C1}, 4, {0, 1, 2, 3}, {0, 1, 2, 3}}, &straight)) return 100;
  if (!skin(straight, other)) return 101;
  if (distance(other[1], kFixtureGame[1]) < 0.1f || distance(other[7], kFixtureGame[7]) < 0.1f) return 102;
  // The first two rows exchanged, or the rows' z and w exchanged (translation in the 3x3 part): every vertex moves.
  BoneSkin rows, zw;
  if (!select(SkinSpec{2, 3, {8, 7, 9}, {0x4C1, 0x4C1, 0x4C1}, 4, {0, 1, 2, 3}, {2, 1, 0, 3}}, &rows)) return 103;
  if (!select(SkinSpec{2, 3, {7, 8, 9}, {0x681, 0x681, 0x681}, 4, {0, 1, 2, 3}, {2, 1, 0, 3}}, &zw)) return 104;
  if (!skin(rows, other)) return 105;
  for (int i = 0; i < 8; ++i) {
    if (distance(other[i], kFixtureGame[i]) < 0.1f) return 106;
  }
  if (!skin(zw, other)) return 107;
  for (int i = 0; i < 8; ++i) {
    if (distance(other[i], kFixtureGame[i]) < 0.1f) return 108;
  }
  return 0;
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
  // --- PaletteBones: the whole bones a palette holds; none = the draw cannot be skinned ---
  {
    // `s`: three half4 rows at bytes 0, 8, 16 of 24-byte bones.
    if (PaletteBones(s, 0) != 0 || PaletteBones(s, 23) != 0) return 80;
    if (PaletteBones(s, 24) != 1 || PaletteBones(s, 47) != 1 || PaletteBones(s, 48) != 2) return 81;
    if (PaletteBones(s, 24 * 300) != 256) return 82;  // the index is 8 bits
    // It is 0 exactly where SkinPositions fails for want of a bone (vertex 1 uses bone 0).
    for (size_t size : {size_t(0), size_t(10), size_t(23), size_t(24), size_t(48)}) {
      Float4 p[1] = {{1, 2, 3, 1}};
      const bool skinned = SkinPositions(vb.data(), vb.size(), pal.data(), size, s, stride, 1, 1, p);
      if (skinned != (PaletteBones(s, size) > 0)) return 83;
    }
    BoneSkin broken = s;
    broken.rows[1].stride_bytes = 0;
    if (PaletteBones(broken, 48) != 0) return 84;
    broken = s;
    broken.rows[2].format = PosFormat::kUnknown;
    if (PaletteBones(broken, 48) != 0) return 85;
    broken = s;
    broken.bone_stride = 0;
    if (PaletteBones(broken, 48) != 0) return 86;
  }

  if (const int real = RealDraw()) return real;

  std::cout << "PASS: bone skin\n";
  return 0;
}
