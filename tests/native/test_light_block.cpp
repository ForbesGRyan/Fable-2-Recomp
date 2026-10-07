// Synthetic standalone test for the light block and for the model of where a pixel constant
// comes from (light_block.h); no game or GPU. Values are of the bridge scene (frame-map
// section 14, "Pixel constants").
#include "../../src/native/capture/light_block.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

using namespace fable2::native::capture;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);        \
      return 1;                                                  \
    }                                                            \
  } while (0)

namespace {

struct Bank {
  float reg[256][4] = {};
  bool have[256] = {};
  void Set(uint32_t r, float x, float y, float z, float w) {
    reg[r][0] = x, reg[r][1] = y, reg[r][2] = z, reg[r][3] = w;
    have[r] = true;
  }
};

// The registers of a daylight frame at a draw of a one-light shader (the sun is the set's first light).
Bank Day() {
  Bank b;
  b.Set(0, 0.5f / 1120, 0.5f / 720, 1.0f / 1120, 1.0f / 720);
  b.Set(19, 82.07f, 157.34f, 50.17f, 1.0f);
  b.Set(20, 0.0074f, 0.0716f, 0.0926f, 16.0f);
  b.Set(21, 1.1168f, 1.3836f, 1.8652f, 1.3f);
  b.Set(22, 0.0588f, 0.0902f, 0.0f, 0.0f);
  b.Set(23, -0.619f, 0.016f, 0.785f, 4.0f);
  b.Set(80, -0.828f, 0.022f, 0.560f, 0.0f);
  b.Set(87, 2.5f, 2.25f, 1.75f, 0.0f);
  b.Set(94, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(109, -0.828f, 0.022f, 0.560f, 0.0f);
  b.Set(112, 1.0f, 0.0f, 0.0f, 0.0f);
  return b;
}

LightBlock Make(const Bank& b) { return MakeLightBlock(b.reg, b.have); }

bool AllZero(const LightBlock& l) {
  const LightBlock zero{};
  return std::memcmp(&l, &zero, sizeof(l)) == 0;
}

using Kind = ConstantSource::Kind;
constexpr uint32_t kAll = ConstantLoads::kAllBins;

bool Is(const ConstantSource& s, Kind kind, uint32_t object = 0, uint32_t index = 0) {
  return s.kind == kind && (kind != Kind::kMemory || (s.object == object && s.index == index));
}

}  // namespace

int main() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  // --- the block's fields are the registers the brief names ---
  {
    const LightBlock l = Make(Day());
    CHECK(l.valid);
    CHECK(l.eye[0] == 82.07f && l.eye[1] == 157.34f && l.eye[2] == 50.17f);
    CHECK(l.tint[0] == 1.1168f && l.tint[1] == 1.3836f && l.tint[2] == 1.8652f && l.tint_pow == 1.3f);
    CHECK(l.add[0] == 0.0074f && l.add[1] == 0.0716f && l.add[2] == 0.0926f && l.out_scale == 16.0f);
    CHECK(l.floor[0] == 0.0588f && l.floor[1] == 0.0902f);
    CHECK(l.axis[0] == -0.619f && l.axis[1] == 0.016f && l.axis[2] == 0.785f);
    CHECK(l.sun_dir[0] == -0.828f && l.sun_dir[1] == 0.022f && l.sun_dir[2] == 0.560f);
    CHECK(l.sun_col[0] == 2.5f && l.sun_col[1] == 2.25f && l.sun_col[2] == 1.75f);
    CHECK(l.mask_xform[0] == 0.5f / 1120 && l.mask_xform[1] == 0.5f / 720 && l.mask_xform[2] == 1.0f / 1120 &&
          l.mask_xform[3] == 1.0f / 720);
    // The sun's w, the mask term's axis c109, the shadow mix c112 and the light mix c94, as read.
    CHECK(l.sun_w == 0.0f);
    CHECK(l.mask_axis[0] == -0.828f && l.mask_axis[1] == 0.022f && l.mask_axis[2] == 0.560f && l.mask_axis[3] == 0.0f);
    CHECK(l.shadow_mix[0] == 1.0f && l.shadow_mix[1] == 0.0f && l.shadow_mix[2] == 0.0f && l.shadow_mix[3] == 0.0f);
    CHECK(l.light_mix[0] == 0.0f && l.light_mix[1] == 0.0f && l.light_mix[2] == 0.0f && l.light_mix[3] == 0.0f);
    CHECK(sizeof(kLightBlockRegisters) / sizeof(kLightBlockRegisters[0]) == 11);
  }
  // The three further registers are stored as they are: no bound is put on them beyond finite
  // (the values seen are one-hot c112, c94 of zeros and ones, c109 the sun direction with w 0).
  {
    Bank b = Day();
    b.Set(94, 1.0f, 1.0f, 0.0f, 0.0f);
    b.Set(109, 0.3f, -7.0f, 0.25f, 2.0f);
    b.Set(112, 0.0f, 0.0f, 1.0f, 0.0f);
    const LightBlock l = Make(b);
    CHECK(l.valid);
    CHECK(l.light_mix[0] == 1.0f && l.light_mix[1] == 1.0f && l.mask_axis[1] == -7.0f && l.mask_axis[3] == 2.0f &&
          l.shadow_mix[2] == 1.0f && l.shadow_mix[0] == 0.0f);
    // Night: the unused slot's w is kept.
    b.Set(80, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(Make(b).valid && Make(b).sun_w == 1.0f);
  }
  // --- a missing register: invalid, and nothing of the block is left to use ---
  for (uint32_t r : kLightBlockRegisters) {
    Bank b = Day();
    b.have[r] = false;
    const LightBlock l = Make(b);
    CHECK(!l.valid);
    CHECK(AllZero(l));
  }
  // A register outside the block does not matter.
  {
    Bank b = Day();
    b.have[81] = false;
    b.Set(88, nan, nan, nan, nan);
    CHECK(Make(b).valid);
  }
  // --- a non-finite component of any register of the block: invalid ---
  for (uint32_t r : kLightBlockRegisters) {
    for (int c = 0; c < 4; ++c) {
      for (float bad : {nan, inf, -inf}) {
        Bank b = Day();
        b.reg[r][c] = bad;
        const LightBlock l = Make(b);
        CHECK(!l.valid);
        CHECK(AllZero(l));
      }
    }
  }
  // --- the exponent is in (0, 8] ---
  for (float w : {0.0f, -1.3f, 8.0001f, 1e30f}) {
    Bank b = Day();
    b.reg[21][3] = w;
    CHECK(!Make(b).valid);
  }
  for (float w : {1e-3f, 1.0f, 1.3f, 8.0f}) {
    Bank b = Day();
    b.reg[21][3] = w;
    CHECK(Make(b).valid);
  }
  // --- the output scale is in (0, 1024] ---
  for (float w : {0.0f, -16.0f, 1024.5f, 1e30f}) {
    Bank b = Day();
    b.reg[20][3] = w;
    CHECK(!Make(b).valid);
  }
  for (float w : {1.0f, 15.99999f, 16.0f, 1024.0f}) {
    Bank b = Day();
    b.reg[20][3] = w;
    CHECK(Make(b).valid);
  }
  // --- the sun direction is a unit vector (within 0.05) or exactly zero ---
  {
    Bank b = Day();
    b.Set(80, 0.0f, 0.0f, 0.0f, 0.0f);  // night
    b.Set(87, 0.0f, 0.0f, 0.0f, 0.0f);
    const LightBlock l = Make(b);
    CHECK(l.valid);
    CHECK(l.sun_dir[0] == 0.0f && l.sun_dir[1] == 0.0f && l.sun_dir[2] == 0.0f);
    b.Set(80, -0.0f, 0.0f, -0.0f, 1.0f);  // negative zeros are zero; an unused light slot is (0, 0, 0, 1)
    CHECK(Make(b).valid);
  }
  for (float scale : {0.96f, 1.0f, 1.04f}) {
    Bank b = Day();
    for (int c = 0; c < 3; ++c) b.reg[80][c] *= scale;
    CHECK(Make(b).valid);
  }
  // c80 is the draw's first light, and only a directional one (w = 0) is the sun: the two- and
  // four-light shaders have a lantern there (a position with w = 1) and the sun in a later
  // register. A position that happens to be of length 1 is not a sun direction.
  for (float w : {1.0f, -1.0f, 0.5f, 1e-6f}) {
    Bank b = Day();
    b.reg[80][3] = w;
    const LightBlock l = Make(b);
    CHECK(!l.valid);
    CHECK(AllZero(l));
  }
  {
    Bank b = Day();
    b.Set(80, 93.7503f, 161.322f, 42.3773f, 1.0f);  // the lantern by the bridge
    b.Set(87, 1.55806f, 0.727093f, 0.0916504f, 0.998754f);
    CHECK(!Make(b).valid);
    b.reg[80][3] = -0.0f;  // a negative zero is zero
    b.Set(80, -0.828f, 0.022f, 0.560f, -0.0f);
    CHECK(Make(b).valid);
  }
  for (float scale : {1e-6f, 0.5f, 0.94f, 1.06f, 2.0f, 1e19f, 1e30f}) {  // 1e30: the squared length overflows
    Bank b = Day();
    for (int c = 0; c < 3; ++c) b.reg[80][c] *= scale;
    CHECK(!Make(b).valid);
  }
  // Null inputs: invalid.
  {
    const Bank b = Day();
    CHECK(!MakeLightBlock(nullptr, b.have).valid);
    CHECK(!MakeLightBlock(b.reg, nullptr).valid);
  }

  // --- constant sources: with no load every register is the bank's ---
  {
    ConstantLoads t;
    for (uint32_t r : {0u, 80u, 255u}) CHECK(Is(t.Find(r), Kind::kBank));
    CHECK(Is(t.Find(256), Kind::kUnknown));  // not a register
    CHECK(t.binned_count() == 0);
  }
  // --- a load under every bin: the registers come from the buffer, the others stay ---
  {
    ConstantLoads t;
    t.Load(80, 36, 0x40001000, 0, false, kAll);
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40001000, 0));
    CHECK(Is(t.Find(87), Kind::kMemory, 0x40001000, 7));
    CHECK(Is(t.Find(115), Kind::kMemory, 0x40001000, 35));
    CHECK(Is(t.Find(79), Kind::kBank) && Is(t.Find(116), Kind::kBank));
    // The buffer's first register may be another one, and a later load replaces an earlier one.
    t.Load(84, 8, 0x40002000, 4, false, kAll);
    CHECK(Is(t.Find(83), Kind::kMemory, 0x40001000, 3));
    CHECK(Is(t.Find(84), Kind::kMemory, 0x40002000, 4));
    CHECK(Is(t.Find(91), Kind::kMemory, 0x40002000, 11));
    CHECK(Is(t.Find(92), Kind::kMemory, 0x40001000, 12));
    // A predicated load whose mask has every bin is the same thing.
    t.Load(80, 1, 0x40003000, 0, true, kAll);
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40003000, 0));
    CHECK(t.binned_count() == 0);
  }
  // --- the device bank uploads a group of four registers: it replaces the load there ---
  {
    ConstantLoads t;
    t.Load(80, 36, 0x40001000, 0, false, kAll);
    t.BankUpload(1ull << (63 - 21));  // registers 84..87
    CHECK(Is(t.Find(83), Kind::kMemory, 0x40001000, 3));
    for (uint32_t r = 84; r < 88; ++r) CHECK(Is(t.Find(r), Kind::kBank));
    CHECK(Is(t.Find(88), Kind::kMemory, 0x40001000, 8));
    // Groups without a load: nothing changes.
    t.BankUpload(~0ull & ~(0xFFull << 36));
    t.BankUpload(0);
    // And a load after the upload wins again.
    t.Load(80, 36, 0x40004000, 0, false, kAll);
    CHECK(Is(t.Find(85), Kind::kMemory, 0x40004000, 5));
  }
  // --- loads under a tile's bin mask (c0..c3 once per colour tile, c4..c7 for the pre-pass
  //     and for the colour bins): a draw record takes the first colour tile's ---
  {
    ConstantLoads t;
    t.Load(0, 4, 0x40010000, 0, true, 0x2);
    t.Load(0, 4, 0x40020000, 0, true, 0x8);
    t.Load(4, 4, 0x40030000, 0, true, 0x15555555);
    t.Load(4, 4, 0x40030000, 4, true, 0x2AAAAAAA);
    CHECK(t.binned_count() == 4);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40010000, 0));
    CHECK(Is(t.Find(3), Kind::kMemory, 0x40010000, 3));
    CHECK(Is(t.Find(4), Kind::kMemory, 0x40030000, 4));
    CHECK(Is(t.Find(7), Kind::kMemory, 0x40030000, 7));
    CHECK(Is(t.Find(8), Kind::kBank));
    // What a pass sees: the last load whose mask meets its bin select.
    CHECK(Is(t.FindForBins(0, 0x2), Kind::kMemory, 0x40010000, 0));
    CHECK(Is(t.FindForBins(0, 0x8), Kind::kMemory, 0x40020000, 0));
    CHECK(Is(t.FindForBins(0, 0x80000003), Kind::kMemory, 0x40010000, 0));
    CHECK(Is(t.FindForBins(0, 0xC), Kind::kMemory, 0x40020000, 0));
    CHECK(Is(t.FindForBins(0, kAll), Kind::kMemory, 0x40020000, 0));
    CHECK(Is(t.FindForBins(0, 0x80000001), Kind::kBank));  // no load reaches the pre-pass bins
    CHECK(Is(t.FindForBins(4, 0x80000001), Kind::kMemory, 0x40030000, 0));
    CHECK(Is(t.FindForBins(4, 0x80000003), Kind::kMemory, 0x40030000, 4));
    // The next frame issues the same loads again: they replace their predecessors.
    t.Load(0, 4, 0x40011000, 0, true, 0x2);
    t.Load(0, 4, 0x40021000, 0, true, 0x8);
    t.Load(4, 4, 0x40031000, 0, true, 0x15555555);
    t.Load(4, 4, 0x40031000, 4, true, 0x2AAAAAAA);
    CHECK(t.binned_count() == 4);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40011000, 0));
    CHECK(Is(t.FindForBins(0, 0x8), Kind::kMemory, 0x40021000, 0));
    CHECK(Is(t.Find(5), Kind::kMemory, 0x40031000, 5));
    // The order of issue decides, not the order of the first issue.
    t.Load(4, 4, 0x40032000, 0, true, 0x15555555);
    CHECK(Is(t.FindForBins(4, 0x80000003), Kind::kMemory, 0x40032000, 0));
    CHECK(Is(t.Find(4), Kind::kMemory, 0x40031000, 4));  // the colour bins still have theirs
    // A load under every bin after them replaces them all.
    t.Load(0, 8, 0x40040000, 0, false, kAll);
    CHECK(t.binned_count() == 0);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40040000, 0));
    CHECK(Is(t.FindForBins(4, 0x80000001), Kind::kMemory, 0x40040000, 4));
  }
  // --- a tile load is in force only where nothing later replaced it ---
  {
    ConstantLoads t;
    t.Load(0, 8, 0x40010000, 0, true, 0x2);
    t.Load(4, 4, 0x40050000, 0, false, kAll);  // covers half of it
    CHECK(t.binned_count() == 1);
    CHECK(Is(t.Find(3), Kind::kMemory, 0x40010000, 3));
    CHECK(Is(t.Find(4), Kind::kMemory, 0x40050000, 0));
    CHECK(t.InForce(t.binned(0), 3) && !t.InForce(t.binned(0), 4) && !t.InForce(t.binned(0), 8));
    t.BankUpload(1ull << 63);  // registers 0..3 from the bank
    CHECK(Is(t.Find(0), Kind::kBank));
    CHECK(!t.InForce(t.binned(0), 0));
    // A tile load after the upload is in force again.
    t.Load(0, 4, 0x40011000, 0, true, 0x2);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40011000, 0));
  }
  // --- a mask with no colour bin never decides a record's value ---
  {
    ConstantLoads t;
    t.Load(4, 4, 0x40030000, 0, true, 0x15555555);
    CHECK(Is(t.Find(4), Kind::kBank));
    CHECK(Is(t.FindForBins(4, 0x80000001), Kind::kMemory, 0x40030000, 0));
    // A predicated load with an empty mask executes nowhere.
    t.Load(8, 4, 0x40060000, 0, true, 0);
    CHECK(Is(t.Find(8), Kind::kBank));
    CHECK(t.binned_count() == 1);
  }
  // --- arguments that cannot be a load: the registers they name are unknown, never guessed ---
  {
    ConstantLoads t;
    t.Load(80, 36, 0, 0, false, kAll);  // no buffer
    CHECK(Is(t.Find(80), Kind::kUnknown) && Is(t.Find(115), Kind::kUnknown));
    CHECK(Is(t.Find(79), Kind::kBank));
    t.Load(80, 36, 0x40001000, 0, false, kAll);  // a good load repairs them
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40001000, 0));
    t.Load(250, 10, 0x40001000, 0, false, kAll);  // runs past register 255
    CHECK(Is(t.Find(250), Kind::kUnknown) && Is(t.Find(255), Kind::kUnknown));
    CHECK(Is(t.Find(249), Kind::kBank));
    t.Load(300, 4, 0x40001000, 0, false, kAll);   // not registers at all: nothing to mark
    t.Load(0xFFFFFFFF, 2, 0x40001000, 0, false, kAll);
    t.Load(10, 0xFFFFFFFF, 0x40001000, 0, false, kAll);
    CHECK(Is(t.Find(10), Kind::kUnknown) && Is(t.Find(255), Kind::kUnknown) && Is(t.Find(9), Kind::kBank));
    t.Load(20, 0, 0x40001000, 0, false, kAll);    // nothing loaded
    CHECK(Is(t.Find(20), Kind::kUnknown));        // (still from the load above)
    t.BankUpload(~0ull);
    CHECK(Is(t.Find(20), Kind::kBank) && Is(t.Find(250), Kind::kBank));
    // A buffer register past 65535 registers is not believed either.
    t.Load(0, 4, 0x40001000, 0xFFFFFFF0, false, kAll);
    CHECK(Is(t.Find(0), Kind::kUnknown));
  }
  // --- more tile loads than the table holds: every register they covered is unknown ---
  {
    ConstantLoads t;
    for (uint32_t i = 0; i < ConstantLoads::kMaxBinned; ++i) t.Load(i * 4, 4, 0x40000000 + i, 0, true, 0x2);
    CHECK(t.binned_count() == ConstantLoads::kMaxBinned);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40000000, 0));
    t.Load(200, 4, 0x4FFF0000, 0, true, 0x2);
    CHECK(t.binned_count() == 0);
    CHECK(Is(t.Find(0), Kind::kUnknown) && Is(t.Find(63), Kind::kUnknown) && Is(t.Find(200), Kind::kUnknown));
    CHECK(Is(t.Find(64), Kind::kBank) && Is(t.Find(204), Kind::kBank));
    // The next frame's loads repair it.
    t.Load(0, 4, 0x40011000, 0, true, 0x2);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40011000, 0));
    CHECK(Is(t.FindForBins(0, 0x8), Kind::kUnknown));  // the other tile was not issued again
  }
  // --- a full table first drops the tile loads that decide nothing any more ---
  {
    ConstantLoads t;
    // Sixteen overlapping runs of eight registers, then loads under every bin that cover each
    // of them in two halves: none is replaced whole, all are dead.
    for (uint32_t i = 0; i < ConstantLoads::kMaxBinned; ++i) t.Load(i * 4, 8, 0x40000000 + i, 0, true, 0x2);
    for (uint32_t i = 0; i <= ConstantLoads::kMaxBinned; ++i) t.Load(i * 4, 4, 0x40100000 + i, 0, false, kAll);
    CHECK(t.binned_count() == ConstantLoads::kMaxBinned);
    CHECK(Is(t.Find(5), Kind::kMemory, 0x40100001, 1));
    t.Load(200, 4, 0x4FFF0000, 0, true, 0x2);
    CHECK(t.binned_count() == 1);
    CHECK(Is(t.Find(5), Kind::kMemory, 0x40100001, 1));
    CHECK(Is(t.Find(200), Kind::kMemory, 0x4FFF0000, 0));
  }
  std::printf("PASS: light block\n");
  return 0;
}
