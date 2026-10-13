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

// in slots 0 and 1, the sun in slot 2, c112 one-hot at 2.
Bank FourLight() {
  Bank b = Day();
  b.Set(80, 93.7503f, 161.322f, 42.3773f, 1.0f);
  b.Set(81, 115.876f, 158.233f, 41.4589f, 1.0f);
  b.Set(82, -0.671334f, 0.0940108f, 0.735169f, 0.0f);
  b.Set(83, 0.0f, 0.0f, 0.0f, 1.0f);
  b.Set(87, 1.55806f, 0.727093f, 0.0916504f, 0.998754f);
  b.Set(88, 1.64307f, 0.766766f, 0.0966512f, 1.05325f);
  b.Set(89, 1.00523f, 0.84597f, 0.408496f, 1.0f);
  b.Set(90, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(94, 1.0f, 1.0f, 0.0f, 0.0f);
  b.Set(96, 0.2505f, 0.2302f, 0.0f, 0.0f);
  b.Set(98, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(99, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(100, 0.0f, 0.0f, 0.0f, 0.0f);
  b.Set(105, -1.0f, -1.0f, -1.0f, -1.0f);
  b.Set(107, 1.0f, 1.0f, 1.0f, 1.0f);
  b.Set(109, -0.671334f, 0.0940108f, 0.735169f, 0.0f);
  b.Set(112, 0.0f, 0.0f, 1.0f, 0.0f);
  b.Set(113, 0.0f, 0.0f, 0.0f, 0.0f);
  return b;
}

// A block made where the light set was loaded from its buffer since the scene began.
LightBlock Make(const Bank& b) { return MakeLightBlock(b.reg, b.have, true); }

// The sun direction lives in c80 and, as the mask term's axis, in c109: both scaled.
void ScaleSun(Bank& b, float scale) {
  for (int c = 0; c < 3; ++c) {
    b.reg[80][c] *= scale;
    b.reg[109][c] *= scale;
  }
}

bool AllZero(const LightBlock& l) {
  const LightBlock zero{};
  return std::memcmp(&l, &zero, sizeof(l)) == 0;
}

using Kind = ConstantSource::Kind;
constexpr uint32_t kAll = ConstantLoads::kAllBins;

bool Is(const ConstantSource& s, Kind kind, uint32_t object = 0, uint32_t index = 0) {
  return s.kind == kind && (kind != Kind::kMemory || (s.object == object && s.index == index));
}

// The capture keeps one model per bank and guest thread. Their constructor is constexpr, so
// they are constant-initialised (`constinit`: this does not compile otherwise) and no access
// tests a thread-local guard.
constinit ConstantLoads g_constinit_pixel{kPixelBank};
constinit thread_local ConstantLoads t_constinit_vertex{kVertexBank};

// A model in a scene whose uploads were all followed.
ConstantLoads InScene() {
  ConstantLoads t;
  t.Observe(true);
  return t;
}

AluLoadCall Call(uint32_t bank, uint32_t start, uint32_t count, uint32_t object = 0x40001000, uint32_t first = 0) {
  AluLoadCall c;
  c.bank = bank, c.start = start, c.count = count, c.object = object, c.first = first;
  c.state_read = true;
  c.predicate = 0, c.mask_lo = kAll, c.mask_hi = 0;
  return c;
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
  // --- the light set must have been loaded from its buffer since the scene began (fix round 1,
  //     I1): registers that only look like a block (a bank that holds zeros where no load was
  //     followed, a buffer of an earlier frame) make no block ---
  {
    const Bank b = Day();
    const LightBlock l = MakeLightBlock(b.reg, b.have, false);
    CHECK(!l.valid);
    CHECK(AllZero(l));
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
  // --- the sun direction is a unit vector (within 0.05) ---
  for (float scale : {0.96f, 1.0f, 1.04f}) {
    Bank b = Day();
    ScaleSun(b, scale);
    CHECK(Make(b).valid);
  }
  for (float scale : {1e-6f, 0.5f, 0.94f, 1.06f, 2.0f, 1e19f, 1e30f}) {  // 1e30: the squared length overflows
    Bank b = Day();
    ScaleSun(b, scale);
    CHECK(!Make(b).valid);
  }
  // --- a zero sun direction is not a block (controller's ruling, fix round 1: there is no
  //     "night" form until a night capture shows what a night's light set holds; the first
  //     version of this test pinned a zero direction as valid). An unused light slot holds
  //     (0, 0, 0, 1) and colour 0. ---
  for (float w : {0.0f, 1.0f}) {
    Bank b = Day();
    b.Set(80, 0.0f, 0.0f, 0.0f, w);
    b.Set(87, 0.0f, 0.0f, 0.0f, 0.0f);
    b.Set(109, 0.0f, 0.0f, 0.0f, 0.0f);
    const LightBlock l = Make(b);
    CHECK(!l.valid);
    CHECK(AllZero(l));
    b.Set(80, -0.0f, 0.0f, -0.0f, w);  // negative zeros are zero
    b.Set(109, -0.0f, 0.0f, -0.0f, 0.0f);
    CHECK(!Make(b).valid);
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
    b.reg[80][3] = -0.0f;  // a negative zero is zero
    CHECK(Make(b).valid);
    b.Set(80, 93.7503f, 161.322f, 42.3773f, 1.0f);  // the lantern by the bridge
    b.Set(87, 1.55806f, 0.727093f, 0.0916504f, 0.998754f);
    CHECK(!Make(b).valid);
  }
  // --- the set's own account of its sun must agree (fix round 1, m1; so in every one-light draw
  //     of the GPU logs): c112 marks light 0 as the one the screen mask shadows, c109 is the sun
  //     direction bit for bit, and light 0 has no range or cone (c94.x = 0) ---
  for (int c = 0; c < 4; ++c) {
    for (float v : {0.0f, 1.0f, 0.5f, -1.0f}) {
      Bank b = Day();
      if (b.reg[112][c] == v) continue;
      b.reg[112][c] = v;
      CHECK(!Make(b).valid);
    }
  }
  {
    Bank b = Day();
    b.Set(112, 0.0f, 1.0f, 0.0f, 0.0f);  // the sun is the set's second light: a two-light draw
    b.Set(94, 1.0f, 0.0f, 0.0f, 0.0f);
    const LightBlock l = Make(b);
    CHECK(!l.valid);
    CHECK(AllZero(l));
  }
  for (int c = 0; c < 3; ++c) {
    Bank b = Day();
    b.reg[109][c] = std::nextafter(b.reg[109][c], 2.0f);  // one bit off the sun direction
    CHECK(!Make(b).valid);
  }
  {
    Bank b = Day();
    b.reg[109][3] = 7.0f;  // w is not part of the direction
    CHECK(Make(b).valid);
    b = Day();
    b.Set(80, -0.828f, 0.0f, 0.560f, 0.0f);  // +0 against -0: equal directions, other bits
    b.Set(109, -0.828f, -0.0f, 0.560f, 0.0f);
    CHECK(!Make(b).valid);
  }
  for (float v : {1.0f, 0.5f, -1.0f, 1e-6f}) {
    Bank b = Day();
    b.reg[94][0] = v;
    CHECK(!Make(b).valid);
  }
  {
    Bank b = Day();
    b.Set(94, 0.0f, 1.0f, 1.0f, 0.5f);  // the other lights' switches are theirs
    const LightBlock l = Make(b);
    CHECK(l.valid);
    CHECK(l.light_mix[1] == 1.0f && l.light_mix[3] == 0.5f);
  }
  // Null inputs: invalid.
  {
    const Bank b = Day();
    CHECK(!MakeLightBlock(nullptr, b.have, true).valid);
    CHECK(!MakeLightBlock(b.reg, nullptr, true).valid);
  }

  // --- constant sources: with no load every register is the bank's ---
  {
    ConstantLoads t;
    CHECK(t.whole() && !t.ever_skipped() && !t.in_scene() && t.scene() == 0);
    for (uint32_t r : {0u, 80u, 255u}) CHECK(Is(t.Find(r), Kind::kBank));
    CHECK(Is(t.Find(256), Kind::kUnknown));  // not a register
    CHECK(Is(t.FindForBins(256, kAll), Kind::kUnknown) && Is(t.FindForBins(0xFFFFFFFF, 0x2), Kind::kUnknown));
    CHECK(t.binned_count() == 0);
  }
  // --- a load under every bin: the registers come from the buffer, the others stay ---
  {
    ConstantLoads t;
    CHECK(t.Load(80, 36, 0x40001000, 0, false, kAll));
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40001000, 0));
    CHECK(Is(t.Find(87), Kind::kMemory, 0x40001000, 7));
    CHECK(Is(t.Find(115), Kind::kMemory, 0x40001000, 35));
    CHECK(Is(t.Find(79), Kind::kBank) && Is(t.Find(116), Kind::kBank));
    // The buffer's first register may be another one, and a later load replaces an earlier one.
    CHECK(t.Load(84, 8, 0x40002000, 4, false, kAll));
    CHECK(Is(t.Find(83), Kind::kMemory, 0x40001000, 3));
    CHECK(Is(t.Find(84), Kind::kMemory, 0x40002000, 4));
    CHECK(Is(t.Find(91), Kind::kMemory, 0x40002000, 11));
    CHECK(Is(t.Find(92), Kind::kMemory, 0x40001000, 12));
    // A predicated load whose mask has every bin is the same thing.
    CHECK(t.Load(80, 1, 0x40003000, 0, true, kAll));
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40003000, 0));
    CHECK(t.binned_count() == 0);
    // The last register of the bank, and a whole bank.
    CHECK(t.Load(255, 1, 0x40004000, 9, false, kAll));
    CHECK(Is(t.Find(255), Kind::kMemory, 0x40004000, 9));
    CHECK(t.Load(0, 256, 0x40005000, 0, false, kAll));
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40005000, 0) && Is(t.Find(255), Kind::kMemory, 0x40005000, 255));
    CHECK(t.whole());
  }
  // --- the device bank uploads a group of four registers: it replaces the load there ---
  {
    ConstantLoads t;
    t.Load(80, 36, 0x40001000, 0, false, kAll);
    t.BankUpload(1ull << (63 - 21));  // registers 84..87
    CHECK(Is(t.Find(83), Kind::kMemory, 0x40001000, 3));
    for (uint32_t r = 84; r < 88; ++r) CHECK(Is(t.Find(r), Kind::kBank));
    CHECK(Is(t.Find(88), Kind::kMemory, 0x40001000, 8));
    // Another loaded group, in a later upload: followed too.
    t.BankUpload(1ull << (63 - 23));  // registers 92..95
    CHECK(Is(t.Find(92), Kind::kBank) && Is(t.Find(95), Kind::kBank));
    CHECK(Is(t.Find(91), Kind::kMemory, 0x40001000, 11) && Is(t.Find(96), Kind::kMemory, 0x40001000, 16));
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
    // A predicated load with an empty mask executes in no pass: followed, and nothing changes.
    // (The mask is the low word; a call with bins in the high word is not handed to the model:
    // MapAluLoad, below.)
    CHECK(t.Load(8, 4, 0x40060000, 0, true, 0));
    CHECK(Is(t.Find(8), Kind::kBank));
    CHECK(t.binned_count() == 1 && t.whole());
  }
  // --- arguments that cannot be a load of this bank: the model stops being whole and no
  //     register a load can name is known until it is whole again (fix round 1, m3: a run that
  //     leaves the bank would write the other bank's registers). The registers of this bank
  //     that the call names count as load registers from then on (fix round 2). ---
  {
    struct Bad {
      uint32_t start, count, object, first;
    };
    for (const Bad& bad : {Bad{80, 36, 0, 0},                    // no buffer
                           Bad{250, 10, 0x40001000, 0},          // runs past register 255
                           Bad{256, 4, 0x40001000, 0},           // starts in the other bank
                           Bad{300, 4, 0x40001000, 0},
                           Bad{0xFFFFFFFF, 2, 0x40001000, 0},
                           Bad{10, 0xFFFFFFFF, 0x40001000, 0},
                           Bad{0, 4, 0x40001000, 0xFFFFFFF0},    // past the buffer bound
                           Bad{0, 4, 0x40001000, 65533}}) {
      ConstantLoads t;
      t.Load(80, 36, 0x40001000, 0, false, kAll);
      CHECK(t.whole());
      CHECK(!t.Load(bad.start, bad.count, bad.object, bad.first, false, kAll));
      CHECK(!t.whole() && t.ever_skipped());
      for (uint32_t r : {0u, 7u, 80u, 115u}) CHECK(Is(t.Find(r), Kind::kUnknown));
      CHECK(Is(t.FindForBins(80, kAll), Kind::kUnknown));
      // What the call named inside the bank is no bank-only register any more; the others
      // that no load names stay the bank's.
      for (uint32_t r = 0; r < ConstantLoads::kRegisters; ++r) {
        const bool named = r >= bad.start && uint64_t(r) < uint64_t(bad.start) + bad.count;
        if (named || IsBufferLoadRegister(kPixelBank, r)) {
          CHECK(Is(t.Find(r), Kind::kUnknown));
        } else {
          CHECK(Is(t.Find(r), Kind::kBank));
        }
      }
    }
    // Nothing loaded is not a load, and is no fault.
    ConstantLoads t;
    CHECK(t.Load(20, 0, 0x40001000, 0, false, kAll));
    CHECK(t.Load(300, 0, 0, 0xFFFFFFFF, true, 0x2));  // whatever else its arguments say
    CHECK(t.whole() && Is(t.Find(20), Kind::kBank));
    // The buffer bound itself.
    CHECK(t.Load(0, 4, 0x40001000, 65532, false, kAll));
    CHECK(Is(t.Find(3), Kind::kMemory, 0x40001000, 65535));
  }
  // --- more tile loads than the table holds: the model stops being whole ---
  {
    ConstantLoads t;
    for (uint32_t i = 0; i < ConstantLoads::kMaxBinned; ++i) CHECK(t.Load(i * 4, 4, 0x40000000 + i, 0, true, 0x2));
    CHECK(t.binned_count() == ConstantLoads::kMaxBinned);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40000000, 0));
    CHECK(!t.Load(200, 4, 0x4FFF0000, 0, true, 0x2));
    CHECK(!t.whole());
    CHECK(Is(t.Find(0), Kind::kUnknown) && Is(t.Find(200), Kind::kUnknown) && Is(t.Find(60), Kind::kUnknown));
    CHECK(Is(t.Find(64), Kind::kBank));  // no load named it
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
    CHECK(t.Load(200, 4, 0x4FFF0000, 0, true, 0x2));
    CHECK(t.binned_count() == 1 && t.whole());
    CHECK(Is(t.Find(5), Kind::kMemory, 0x40100001, 1));
    CHECK(Is(t.Find(200), Kind::kMemory, 0x4FFF0000, 0));
  }

  // --- scenes (fix round 1, I1): a load carries the scene it was issued in, and the light set
  //     counts for a block only when its buffer was loaded since the current scene began ---
  {
    ConstantLoads t;
    CHECK(!t.LoadedThisScene(80));  // nothing loaded: the bank, which holds zeros there
    t.Load(80, 36, 0x40001000, 0, false, kAll);  // before any scene (another pass of the frame)
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40001000, 0));
    CHECK(!t.LoadedThisScene(80));  // not in a scene
    t.Observe(true);  // the scene begins
    CHECK(t.in_scene() && t.scene() == 1);
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40001000, 0));  // the register is still read from there
    CHECK(!t.LoadedThisScene(80) && !t.LoadedThisScene(87));  // but that load is not this scene's
    t.Observe(true);  // further draws of the scene
    CHECK(t.scene() == 1);
    t.Load(80, 36, 0x40002000, 0, false, kAll);
    CHECK(t.LoadedThisScene(80) && t.LoadedThisScene(87) && t.LoadedThisScene(115));
    CHECK(t.Find(80).scene == 1);
    CHECK(!t.LoadedThisScene(79) && !t.LoadedThisScene(116) && !t.LoadedThisScene(19));  // bank registers
    CHECK(!t.LoadedThisScene(256));
    // The bank takes a group back: not a buffer's any more.
    t.BankUpload(1ull << (63 - 21));
    CHECK(t.LoadedThisScene(80) && !t.LoadedThisScene(87));
    t.Load(80, 36, 0x40002000, 0, false, kAll);
    CHECK(t.LoadedThisScene(87));
    // The scene ends: no block outside it.
    t.Observe(false);
    CHECK(!t.in_scene() && !t.LoadedThisScene(80));
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40002000, 0));
    // The next frame's scene: the same buffer, reloaded each frame with new contents, counts
    // again only once it is loaded again.
    t.Observe(true);
    CHECK(t.scene() == 2 && !t.LoadedThisScene(80));
    t.Load(80, 36, 0x40002000, 0, false, kAll);
    CHECK(t.LoadedThisScene(80) && t.Find(80).scene == 2);
    // A tile load counts the same way.
    CHECK(!t.LoadedThisScene(0));
    t.Load(0, 4, 0x40010000, 0, true, 0x2);
    t.Load(0, 4, 0x40020000, 0, true, 0x8);
    CHECK(t.LoadedThisScene(0) && t.LoadedThisScene(3) && !t.LoadedThisScene(4));
    t.Observe(false);
    t.Observe(true);
    CHECK(!t.LoadedThisScene(0));  // last scene's tile loads
  }
  // --- uploads that were not followed (fix round 1, G1: with records off nothing is tracked):
  //     the model is not whole, no register is known, and it is whole again at the first scene
  //     whose uploads are all followed from its begin ---
  {
    ConstantLoads t = InScene();
    t.Load(80, 36, 0x40001000, 0, false, kAll);
    t.Load(0, 4, 0x40010000, 0, true, 0x2);
    CHECK(t.LoadedThisScene(80));
    t.Skip();  // records went off: an upload happened that nobody looked at
    CHECK(!t.whole() && t.ever_skipped());
    for (uint32_t r : {0u, 4u, 80u, 115u}) CHECK(Is(t.Find(r), Kind::kUnknown));
    for (uint32_t r : {19u, 20u, 21u, 22u, 23u, 255u}) CHECK(Is(t.Find(r), Kind::kBank));  // bank-only
    CHECK(Is(t.FindForBins(0, 0x2), Kind::kUnknown));
    CHECK(!t.LoadedThisScene(80));
    t.Skip();  // scenes pass, nothing is observed
    t.Skip();
    // Records come back in the middle of a later scene: its first uploads were not followed.
    t.Observe(true);
    CHECK(t.scene() == 1 && !t.whole());
    CHECK(t.Load(80, 36, 0x40002000, 0, false, kAll));  // followed, and not believed yet
    CHECK(!t.whole() && Is(t.Find(80), Kind::kUnknown) && !t.LoadedThisScene(80));
    CHECK(t.Load(0, 4, 0x40012000, 0, true, 0x2) && t.Load(4, 4, 0x40013000, 0, true, 0x2));
    CHECK(t.binned_count() == 0);  // nor listed: a row carries no tile load while nothing is known
    t.BankUpload(~0ull);
    CHECK(Is(t.Find(84), Kind::kUnknown));
    t.Observe(true);  // more hooks of that scene, all followed: still not from its begin
    t.Observe(true);
    CHECK(!t.whole() && Is(t.Find(84), Kind::kUnknown));
    // More tile loads than the table holds, while nothing is kept: no skip (the next scene
    // still makes the model whole).
    for (uint32_t i = 0; i <= ConstantLoads::kMaxBinned; ++i) CHECK(t.Load(0, 4, 0x40000000 + i, 0, true, 1u << i));
    t.Observe(false);
    CHECK(!t.whole());
    // The next scene begins with every upload followed: whole again.
    t.Observe(true);
    CHECK(t.whole() && t.scene() == 2);
    // What was loaded before that is not believed: a register a load can name is unknown
    // until an upload of it is followed, a load or the bank's upload of its group.
    CHECK(Is(t.Find(80), Kind::kUnknown) && Is(t.Find(0), Kind::kUnknown) && Is(t.Find(115), Kind::kUnknown));
    CHECK(t.binned_count() == 0);
    t.BankUpload(1ull << (63 - 22));  // registers 88..91: the builder uploads the group
    CHECK(Is(t.Find(88), Kind::kBank) && Is(t.Find(91), Kind::kBank));
    CHECK(Is(t.Find(87), Kind::kUnknown) && Is(t.Find(92), Kind::kUnknown));
    // The bank-only registers were never in doubt.
    for (uint32_t r : {8u, 19u, 20u, 21u, 22u, 23u, 79u, 116u, 255u}) CHECK(Is(t.Find(r), Kind::kBank));
    t.Load(0, 4, 0x40011000, 0, true, 0x2);
    t.Load(80, 36, 0x40003000, 0, false, kAll);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40011000, 0) && Is(t.Find(80), Kind::kMemory, 0x40003000, 0));
    CHECK(t.LoadedThisScene(80) && t.LoadedThisScene(87));
    CHECK(Is(t.FindForBins(0, 0x8), Kind::kUnknown));  // the other tile was not loaded since
    CHECK(t.ever_skipped());  // stays: the vertex rows are written only by a model never interrupted
    // The bank taking a loaded group back after that is followed as before.
    t.BankUpload(1ull << (63 - 20));
    CHECK(Is(t.Find(80), Kind::kBank) && Is(t.Find(84), Kind::kMemory, 0x40003000, 4));
    // And the model stays whole over the scenes that follow.
    t.Observe(false);
    t.Observe(true);
    CHECK(t.whole() && Is(t.Find(19), Kind::kBank) && Is(t.Find(84), Kind::kMemory, 0x40003000, 4));
    CHECK(t.untabled_loads() == 0);
  }
  // An upload skipped between the last thing seen and the scene's begin may belong to the scene:
  // that scene is not taken as followed from its begin; the next one is.
  {
    ConstantLoads t;
    t.Skip();
    t.Observe(false);
    t.Skip();          // not followed, and nothing was seen since
    t.Observe(true);   // the scene's first followed hook
    CHECK(!t.whole());
    t.Observe(false);
    t.Observe(true);
    CHECK(t.whole());
  }
  // A skip outside a scene, with every upload of the next scene followed: whole at its begin.
  {
    ConstantLoads t;
    t.Skip();
    CHECK(!t.whole());
    t.Observe(false);
    CHECK(!t.whole());  // no scene has begun
    t.Observe(true);
    CHECK(t.whole() && t.scene() == 1 && Is(t.Find(80), Kind::kUnknown) && Is(t.Find(19), Kind::kBank));
  }
  // --- bank-only registers (fix round 2, controller's ruling): a register that no call site
  //     of LoadAluConstants names is the bank's whatever the model's state. The game is
  //     usually started with the view off, so the model is not whole when the view comes on,
  //     and a register the game sets once (c22 reads the same in every row of every capture)
  //     would otherwise never be known. ---
  {
    // The table: the registers the ten call sites name (xdk_layout.h, kAluLoadSites).
    for (uint32_t r = 0; r < ConstantLoads::kRegisters; ++r) {
      CHECK(IsBufferLoadRegister(kPixelBank, r) == (r < 8 || (r >= 80 && r < 116)));
      CHECK(IsBufferLoadRegister(kVertexBank, r) == ((r >= 40 && r < 44) || (r >= 76 && r < 108)));
      CHECK(!IsBufferLoadRegister(2, r));
    }
    CHECK(!IsBufferLoadRegister(kPixelBank, 256) && !IsBufferLoadRegister(kVertexBank, 0xFFFFFFFF));
    CHECK(sizeof(xdk::kAluLoadSites) / sizeof(xdk::kAluLoadSites[0]) == 10);
    for (const xdk::AluLoadSite& s : xdk::kAluLoadSites) {
      CHECK(s.count != 0);
      for (uint32_t r = s.start; r < s.start + s.count; ++r) CHECK(IsBufferLoadRegister(s.bank, r));
    }
    // Every register of the table is named by a site: the table holds nothing else.
    for (uint32_t bank : {kVertexBank, kPixelBank}) {
      for (uint32_t r = 0; r < ConstantLoads::kRegisters; ++r) {
        bool named = false;
        for (const xdk::AluLoadSite& s : xdk::kAluLoadSites) named |= s.bank == bank && r >= s.start && r - s.start < s.count;
        CHECK(named == IsBufferLoadRegister(bank, r));
      }
    }
    // The light block's bank registers are bank-only; its other registers are loaded.
    for (uint32_t r : {19u, 20u, 21u, 22u, 23u}) CHECK(!IsBufferLoadRegister(kPixelBank, r));
    for (uint32_t r : {0u, 80u, 87u, 94u, 109u, 112u}) CHECK(IsBufferLoadRegister(kPixelBank, r));
    for (uint32_t r : kLightSetRegisters) CHECK(IsBufferLoadRegister(kPixelBank, r));
  }
  // The game as it is usually run: started with the view off, the view switched on later.
  {
    ConstantLoads t;
    // Nothing is followed from the first upload on (among them the bank's one upload of the
    // group of c22, which the game sets once).
    for (int i = 0; i < 5; ++i) t.Skip();
    CHECK(!t.whole());
    // After the gap c19..c23 are readable at once, in no scene and before any upload.
    for (uint32_t r : {19u, 20u, 21u, 22u, 23u}) CHECK(Is(t.Find(r), Kind::kBank));
    CHECK(Is(t.Find(80), Kind::kUnknown) && Is(t.Find(0), Kind::kUnknown));
    CHECK(!t.LoadedThisScene(80));
    // The view comes on at a frame boundary: the next scene is followed from its begin.
    t.Observe(false);
    t.Observe(true);
    CHECK(t.whole());
    for (uint32_t r : {19u, 20u, 21u, 22u, 23u}) CHECK(Is(t.Find(r), Kind::kBank));  // no upload was needed
    // c80 is unknown until its range is loaded in the scene; so is c0 until its tile load.
    CHECK(Is(t.Find(80), Kind::kUnknown) && Is(t.Find(109), Kind::kUnknown) && Is(t.Find(0), Kind::kUnknown));
    CHECK(!t.LoadedThisScene(80));
    // The bank's upload of a load register's group is an upload of it: known, and the bank's.
    t.BankUpload(1ull << (63 - 28));  // registers 112..115
    CHECK(Is(t.Find(112), Kind::kBank) && Is(t.Find(115), Kind::kBank) && Is(t.Find(111), Kind::kUnknown));
    CHECK(!t.LoadedThisScene(112));
    t.Load(0, 4, 0x40010000, 0, true, 0x2);
    t.Load(0, 4, 0x40020000, 0, true, 0x8);
    CHECK(Is(t.Find(0), Kind::kMemory, 0x40010000, 0) && Is(t.Find(80), Kind::kUnknown));
    // The first scene whose light-set load was followed has everything a block reads.
    t.Load(80, 36, 0x40001000, 0, false, kAll);
    for (uint32_t r : kLightSetRegisters) CHECK(t.LoadedThisScene(r));
    for (uint32_t r : kLightBlockRegisters) CHECK(!Is(t.Find(r), Kind::kUnknown));
    // The bank's uploads of the bank-only groups change nothing, before or after.
    t.BankUpload(1ull << (63 - 5));
    CHECK(Is(t.Find(22), Kind::kBank));
    // The view goes off and on again: the bank-only registers are read all the way through.
    t.Observe(false);
    t.Skip();
    CHECK(Is(t.Find(22), Kind::kBank) && Is(t.Find(19), Kind::kBank) && Is(t.Find(80), Kind::kUnknown));
    t.Observe(true);  // a scene with a skipped upload before its first hook
    CHECK(!t.whole() && Is(t.Find(22), Kind::kBank) && !t.LoadedThisScene(80));
    t.Load(80, 36, 0x40001000, 0, false, kAll);  // followed, not believed: the scene was not followed from its begin
    CHECK(Is(t.Find(80), Kind::kUnknown) && !t.LoadedThisScene(80));
    t.Observe(false);
    t.Observe(true);
    t.Load(80, 36, 0x40001000, 0, false, kAll);
    CHECK(t.whole() && t.LoadedThisScene(80) && Is(t.Find(22), Kind::kBank));
    CHECK(t.untabled_loads() == 0);
  }
  // The vertex bank's model has the vertex ranges.
  {
    ConstantLoads v(kVertexBank);
    v.Skip();
    for (uint32_t r : {40u, 43u, 76u, 107u}) CHECK(Is(v.Find(r), Kind::kUnknown));
    for (uint32_t r : {0u, 4u, 18u, 39u, 44u, 75u, 108u, 115u, 255u}) CHECK(Is(v.Find(r), Kind::kBank));
    ConstantLoads p(kPixelBank);
    p.Skip();
    CHECK(Is(p.Find(40), Kind::kBank) && Is(p.Find(4), Kind::kUnknown));
  }
  // The unexpected: a followed load names a register outside the table (a call site the static
  // reading missed). The register is a load register from then on, and the model counts it.
  {
    ConstantLoads t = InScene();
    CHECK(t.untabled_loads() == 0);
    CHECK(t.Load(80, 36, 0x40001000, 0, false, kAll) && t.Load(0, 4, 0x40010000, 0, true, 0x2));
    CHECK(t.untabled_loads() == 0);  // the table's own ranges
    CHECK(t.Load(20, 4, 0x40070000, 0, false, kAll));  // c20..c23 from a buffer
    CHECK(t.untabled_loads() == 1);
    CHECK(Is(t.Find(20), Kind::kMemory, 0x40070000, 0) && Is(t.Find(23), Kind::kMemory, 0x40070000, 3));
    CHECK(t.Load(20, 4, 0x40071000, 0, false, kAll));  // the same registers again: nothing new
    CHECK(t.untabled_loads() == 1);
    CHECK(t.Load(114, 4, 0x40072000, 0, false, kAll));  // c114..c117: two of the table, two not
    CHECK(t.untabled_loads() == 2);
    t.Skip();
    for (uint32_t r : {20u, 21u, 22u, 23u, 116u, 117u}) CHECK(Is(t.Find(r), Kind::kUnknown));  // no longer bank-only
    CHECK(Is(t.Find(19), Kind::kBank) && Is(t.Find(24), Kind::kBank) && Is(t.Find(118), Kind::kBank));
    t.Observe(false);
    t.Observe(true);
    CHECK(t.whole() && Is(t.Find(20), Kind::kUnknown) && Is(t.Find(19), Kind::kBank));
    t.BankUpload(1ull << (63 - 5));  // the bank's upload of the group is followed now
    CHECK(Is(t.Find(20), Kind::kBank) && Is(t.Find(23), Kind::kBank));
    // A tile load outside the table counts the same way.
    CHECK(t.Load(200, 4, 0x40073000, 0, true, 0x2));
    CHECK(t.untabled_loads() == 3 && Is(t.Find(200), Kind::kMemory, 0x40073000, 0));
    CHECK(Is(t.FindForBins(200, 0x8), Kind::kBank));  // the other tile still has the bank's value
    // A load that is followed and not believed (the model is not whole) still names its registers.
    t.Skip();
    CHECK(t.Load(210, 2, 0x40074000, 0, false, kAll));
    CHECK(t.untabled_loads() == 4 && Is(t.Find(210), Kind::kUnknown) && Is(t.Find(212), Kind::kBank));
    // A call that cannot be followed (the hook could not read the device's state): the registers
    // it names inside the bank are load registers too, and the upload is skipped.
    ConstantLoads u = InScene();
    u.Unfollowable(30, 2);
    CHECK(!u.whole() && u.untabled_loads() == 1);
    CHECK(Is(u.Find(30), Kind::kUnknown) && Is(u.Find(31), Kind::kUnknown) && Is(u.Find(32), Kind::kBank));
    u.Unfollowable(254, 100);     // runs out of the bank: its registers up to 255
    CHECK(u.untabled_loads() == 2 && Is(u.Find(254), Kind::kUnknown) && Is(u.Find(255), Kind::kUnknown));
    u.Unfollowable(300, 4);       // another bank's registers
    u.Unfollowable(8, 0);         // names nothing
    u.Unfollowable(0xFFFFFFFF, 0xFFFFFFFF);
    CHECK(u.untabled_loads() == 2 && Is(u.Find(8), Kind::kBank) && Is(u.Find(253), Kind::kBank));
    u.Unfollowable(80, 36);       // the table's: a skip, nothing new
    CHECK(u.untabled_loads() == 2);
  }
  // A model never interrupted believes the bank from the start (no upload is needed first).
  {
    ConstantLoads t = InScene();
    CHECK(Is(t.Find(19), Kind::kBank) && t.whole() && !t.ever_skipped());
  }

  // --- a LoadAluConstants call as the hook sees it (fix round 1, m6): which bank, which
  //     registers, under which bins ---
  {
    AluLoad l;
    CHECK(MapAluLoad(Call(1, 80, 36, 0x40CF28F0, 0), &l));  // the light set
    CHECK(l.pixel && l.start == 80 && l.count == 36 && l.object == 0x40CF28F0 && l.first == 0 && !l.predicated);
    CHECK(MapAluLoad(Call(0, 76, 32, 0x401CD790, 0), &l));  // the vertex light set
    CHECK(!l.pixel && l.start == 76 && l.count == 32);
    AluLoadCall tile = Call(1, 4, 4, 0x40981080, 4);        // c4..c7 for the colour bins
    tile.predicate = 1, tile.mask_lo = 0x2AAAAAAA;
    CHECK(MapAluLoad(tile, &l));
    CHECK(l.pixel && l.start == 4 && l.count == 4 && l.first == 4 && l.predicated && l.mask == 0x2AAAAAAA);
    tile.predicate = 0x80;  // any non-zero predicate word
    CHECK(MapAluLoad(tile, &l) && l.predicated);
    // An unpredicated load executes in every pass whatever the mask words hold.
    AluLoadCall every = Call(1, 80, 36);
    every.mask_lo = 0x80000000, every.mask_hi = 0x1234;
    CHECK(MapAluLoad(every, &l) && !l.predicated && l.mask == kAll);
    // Not followable: another bank number, a run that leaves the bank (the packet's index would
    // reach the other bank's registers; the static callers pass constants inside it), no buffer,
    // the device's predicate word or bin mask unread, and a predicated load with bins in the
    // high word of the bin mask (the model keeps the low word only).
    CHECK(!MapAluLoad(Call(2, 80, 36), &l));
    CHECK(!MapAluLoad(Call(0xFFFFFFFF, 80, 36), &l));
    CHECK(!MapAluLoad(Call(1, 250, 10), &l));
    CHECK(!MapAluLoad(Call(0, 256, 4), &l));
    CHECK(!MapAluLoad(Call(1, 0, 257), &l));
    CHECK(!MapAluLoad(Call(1, 0xFFFFFFFF, 2), &l));
    CHECK(!MapAluLoad(Call(1, 10, 0xFFFFFFFF), &l));
    CHECK(!MapAluLoad(Call(1, 80, 36, 0), &l));
    CHECK(!MapAluLoad(Call(1, 80, 36, 0x40001000, 65536), &l));
    CHECK(!MapAluLoad(Call(1, 80, 36, 0x40001000, 70000), &l));
    CHECK(!MapAluLoad(Call(1, 80, 36, 0x40001000, 0xFFFFFFF0), &l));
    CHECK(MapAluLoad(Call(1, 80, 36, 0x40001000, 65500), &l) && l.first == 65500);
    AluLoadCall unread = Call(1, 80, 36);
    unread.state_read = false;
    CHECK(!MapAluLoad(unread, &l));
    AluLoadCall high = Call(1, 0, 4);
    high.predicate = 1, high.mask_lo = 0x2, high.mask_hi = 1;
    CHECK(!MapAluLoad(high, &l));
    high.mask_hi = 0;
    CHECK(MapAluLoad(high, &l));
    // The edges of a bank, and a call that loads nothing.
    CHECK(MapAluLoad(Call(1, 255, 1), &l) && MapAluLoad(Call(0, 0, 256), &l));
    CHECK(MapAluLoad(Call(1, 80, 0), &l) && l.count == 0);
    CHECK(MapAluLoad(Call(1, 80, 0, 0, 70000), &l) && l.count == 0);  // nothing loaded: no buffer needed
    CHECK(!MapAluLoad(Call(1, 80, 36), nullptr));
  }
  // --- a register of a constant buffer in guest memory (the bounds of the read) ---
  {
    uint32_t a = 0;
    CHECK(BufferRegisterAddress(0xFFA9A000, 0, &a) && a == 0xFFA9A000);
    CHECK(BufferRegisterAddress(0xFFA9A000, 7, &a) && a == 0xFFA9A070);
    CHECK(BufferRegisterAddress(0x40000000, 65535, &a) && a == 0x400FFFF0);
    CHECK(!BufferRegisterAddress(0, 0, &a));            // no buffer
    CHECK(!BufferRegisterAddress(0x40000000, 65536, &a));  // past the buffer bound
    CHECK(BufferRegisterAddress(0xFFFFFFF0, 0, &a) && a == 0xFFFFFFF0);  // the last 16 bytes
    CHECK(!BufferRegisterAddress(0xFFFFFFF0, 1, &a));   // would wrap
    CHECK(!BufferRegisterAddress(0xFFFFFFF4, 0, &a));   // the register would run past the end
    CHECK(BufferRegisterAddress(0xFFF00000, 65535, &a) && a == 0xFFFFFFF0);
    CHECK(!BufferRegisterAddress(0xFFF00010, 65535, &a));
    CHECK(!BufferRegisterAddress(0x40000000, 0, nullptr));
  }
  // --- the pixel bool word's two writers (discovery evidence) ---
  {
    BoolWordWriters w;
    CHECK(!w.known);
    // GpuLoadShaders with a shader that uses flow constants uploads the engine's word.
    w.OnShaderLoad(true, true, true, 0x00000110);
    CHECK(w.known && w.word == 0x110);
    // Shaders that use none: the GPU keeps what it has.
    w.OnShaderLoad(true, false, true, 0x00000100);
    CHECK(w.known && w.word == 0x110);
    // A draw builder that finds the bool shadow pending writes the device's (stale) word.
    w.OnBuilderFlush(true, 0x00000100);
    CHECK(w.known && w.word == 0x100);
    w.OnShaderLoad(true, true, true, 0x00000131);
    CHECK(w.known && w.word == 0x131);
    // What cannot be read is not known: the shaders' records, the engine's word, the shadow.
    w.OnShaderLoad(false, true, true, 0x00000110);
    CHECK(!w.known);
    w.OnShaderLoad(true, true, true, 0x00000110);
    w.OnShaderLoad(true, true, false, 0xDEAD);
    CHECK(!w.known);
    w.OnShaderLoad(true, false, true, 0x00000100);
    CHECK(!w.known);  // an upload that was not known stays unknown under a load that uploads nothing
    w.OnBuilderFlush(true, 0x00000100);
    CHECK(w.known && w.word == 0x100);
    w.OnBuilderFlush(false, 0xDEAD);
    CHECK(!w.known);
    w.OnShaderLoad(true, true, true, 0x00000110);
    w.Forget();  // not followed any more (discovery off)
    CHECK(!w.known);
  }
  // --- the models as the capture declares them (constant-initialised) ---
  {
    CHECK(g_constinit_pixel.whole() && Is(g_constinit_pixel.Find(19), Kind::kBank));
    g_constinit_pixel.Skip();
    CHECK(Is(g_constinit_pixel.Find(80), Kind::kUnknown) && Is(g_constinit_pixel.Find(40), Kind::kBank));
    t_constinit_vertex.Skip();
    CHECK(Is(t_constinit_vertex.Find(76), Kind::kUnknown) && Is(t_constinit_vertex.Find(40), Kind::kUnknown));
    CHECK(Is(t_constinit_vertex.Find(4), Kind::kBank) && Is(t_constinit_vertex.Find(108), Kind::kBank));
  }
  // --- the tile loads a row lists ("ptiles"): none while the model is not whole. What the
  //     table holds then was loaded before the gap; beside the dashes of "pfull" it would be a
  //     stale value. ---
  {
    ConstantLoads t = InScene();
    CHECK(t.Load(0, 4, 0x40010000, 0, true, 0x2) && t.Load(0, 4, 0x40011000, 0, true, 0x8));
    CHECK(t.binned_count() == 2 && t.InForce(t.binned(0), 0));
    t.Skip();
    CHECK(t.binned_count() == 0 && Is(t.Find(0), Kind::kUnknown));
    t.Observe(true);  // a hook of the same scene: still not whole, still nothing listed
    CHECK(t.binned_count() == 0);
    t.Observe(false);
    t.Observe(true);  // the next scene, followed from its begin
    CHECK(t.whole() && t.binned_count() == 0);
    CHECK(t.Load(0, 4, 0x40012000, 0, true, 0x2));
    CHECK(t.binned_count() == 1 && t.binned(0).object == 0x40012000);
  }
  // --- a call that names a register outside the table is counted whatever it loads: also
  //     when it is predicated under no bin and so executes in no pass (its call site exists) ---
  {
    ConstantLoads t = InScene();
    CHECK(t.Load(200, 4, 0x40073000, 0, true, 0));
    CHECK(t.untabled_loads() == 1);
    CHECK(Is(t.Find(200), Kind::kBank) && t.binned_count() == 0);  // nothing was loaded
    t.Skip();
    CHECK(Is(t.Find(200), Kind::kUnknown) && Is(t.Find(203), Kind::kUnknown) && Is(t.Find(204), Kind::kBank));
    // The table's own registers under no bin: nothing new.
    ConstantLoads u = InScene();
    CHECK(u.Load(0, 4, 0x40010000, 0, true, 0) && u.untabled_loads() == 0 && u.binned_count() == 0);
    CHECK(Is(u.Find(0), Kind::kBank));
  }
  // --- the bank's upload of a group ends the following of that group and of no other: each
  //     loaded group is taken back by its own upload, in either order, and a group loaded
  //     again after its upload is taken back again. (Bit 63 - g of the pending mask is group
  //     g; groups 0 and 63 are each other's mirror, 20 and 43 too.) ---
  {
    for (int order = 0; order < 2; ++order) {
      ConstantLoads t = InScene();
      CHECK(t.Load(0, 4, 0x40010000, 0, false, kAll));     // group 0
      CHECK(t.Load(252, 4, 0x40020000, 0, false, kAll));   // group 63 (outside the table: counted)
      CHECK(t.Load(80, 4, 0x40030000, 0, false, kAll));    // group 20
      CHECK(t.Load(172, 4, 0x40040000, 0, false, kAll));   // group 43
      const uint32_t first = order ? 252 : 0, second = order ? 0 : 252;
      t.BankUpload(1ull << (63 - first / 4));
      CHECK(Is(t.Find(first), Kind::kBank) && Is(t.Find(second), Kind::kMemory, order ? 0x40010000 : 0x40020000, 0));
      CHECK(Is(t.Find(80), Kind::kMemory, 0x40030000, 0) && Is(t.Find(172), Kind::kMemory, 0x40040000, 0));
      t.BankUpload(1ull << (63 - second / 4));
      CHECK(Is(t.Find(second), Kind::kBank) && Is(t.Find(first + 3), Kind::kBank));
      t.BankUpload(1ull << (63 - 20));
      CHECK(Is(t.Find(80), Kind::kBank) && Is(t.Find(172), Kind::kMemory, 0x40040000, 0));
      t.BankUpload(1ull << (63 - 43));
      CHECK(Is(t.Find(172), Kind::kBank));
      // Loaded again after its upload: its next upload is followed again.
      CHECK(t.Load(first, 4, 0x40050000, 0, false, kAll));
      CHECK(Is(t.Find(first), Kind::kMemory, 0x40050000, 0));
      t.BankUpload(~0ull);
      CHECK(Is(t.Find(first), Kind::kBank));
    }
  }
  // --- the upload that decides a register (Task 4 fix round 1, I2): what tells one light set
  //     from the next. A number per followed upload, never 0; 0 where no upload decides ---
  {
    ConstantLoads t = InScene();
    uint64_t a = 77, b = 77, c = 77;
    CHECK(Is(t.Find(80, &a), Kind::kBank) && a == 0);      // nothing loaded yet: the bank's, no upload
    CHECK(t.Load(80, 36, 0x40010000, 0, false, kAll));
    CHECK(Is(t.Find(80, &a), Kind::kMemory, 0x40010000, 0) && a != 0);
    CHECK(Is(t.Find(80, &b), Kind::kMemory, 0x40010000, 0) && b == a);  // asked again: the same upload
    CHECK(Is(t.Find(115, &b), Kind::kMemory, 0x40010000, 35) && b == a);  // one load, one number for its run
    CHECK(Is(t.Find(80), Kind::kMemory, 0x40010000, 0));                 // (the number is optional)
    // Uploads of other registers do not change it: a tile's c0..c3, the bank's upload of another
    // group, a load of the vertex-side range's numbers in this bank.
    CHECK(t.Load(0, 4, 0x40020000, 0, true, 0x100));
    t.BankUpload(1ull << (63 - 5));
    CHECK(t.Load(4, 4, 0x40020000, 4, false, kAll));
    CHECK(Is(t.Find(80, &b), Kind::kMemory, 0x40010000, 0) && b == a);
    // The same buffer loaded again is a new upload: its contents may be another light.
    CHECK(t.Load(80, 36, 0x40010000, 0, false, kAll));
    CHECK(Is(t.Find(80, &b), Kind::kMemory, 0x40010000, 0) && b != a && b != 0);
    // So is another buffer, and a load of a part of the set that covers c80.
    CHECK(t.Load(80, 36, 0x40030000, 0, false, kAll));
    CHECK(Is(t.Find(80, &c), Kind::kMemory, 0x40030000, 0) && c != b && c != a && c != 0);
    CHECK(t.Load(80, 4, 0x40040000, 8, false, kAll));
    CHECK(Is(t.Find(80, &a), Kind::kMemory, 0x40040000, 8) && a != c && a != 0);
    CHECK(Is(t.Find(87, &b), Kind::kMemory, 0x40030000, 7) && b == c);  // the rest of the set: the older load
    // A part that does not cover c80 leaves its number.
    CHECK(t.Load(100, 4, 0x40050000, 0, false, kAll));
    CHECK(Is(t.Find(80, &b), Kind::kMemory, 0x40040000, 8) && b == a);
    // A tile load of the set: a record sees the first colour tile's (Find), each pass its own.
    CHECK(t.Load(80, 36, 0x40060000, 0, true, 0x300));
    CHECK(Is(t.Find(80, &b), Kind::kMemory, 0x40060000, 0) && b != a && b != 0);
    CHECK(Is(t.FindForBins(80, 0x100, &c), Kind::kMemory, 0x40060000, 0) && c == b);
    CHECK(Is(t.FindForBins(80, 0x400, &c), Kind::kMemory, 0x40040000, 8) && c == a);
    // The bank's upload of the group: the bank's value, by that upload; no load decides it, and
    // LoadedThisScene is false, so the capture reads no block there.
    t.BankUpload(1ull << (63 - 20));
    CHECK(Is(t.Find(80, &c), Kind::kBank) && c != 0 && c != b && !t.LoadedThisScene(80));
    // A register no load names is the bank's, by no upload; a register past the bank and one of a
    // model that is not whole are unknown, by no upload.
    c = 77;
    CHECK(Is(t.Find(19, &c), Kind::kBank) && c == 0);
    c = 77;
    CHECK(Is(t.Find(256, &c), Kind::kUnknown) && c == 0);
    c = 77;
    CHECK(Is(t.FindForBins(256, 0x100, &c), Kind::kUnknown) && c == 0);
    t.Skip();
    c = 77;
    CHECK(Is(t.Find(80, &c), Kind::kUnknown) && c == 0);
    c = 77;
    CHECK(Is(t.Find(19, &c), Kind::kBank) && c == 0);
    // Whole again at the next scene, knowing nothing of the set: unknown, by no upload, until a
    // load of it is followed.
    t.Observe(false);
    t.Observe(true);
    c = 77;
    CHECK(t.whole() && Is(t.Find(80, &c), Kind::kUnknown) && c == 0);
    c = 77;
    CHECK(Is(t.FindForBins(80, 0x200, &c), Kind::kUnknown) && c == 0);
    CHECK(t.Load(80, 36, 0x40070000, 0, false, kAll));
    CHECK(Is(t.Find(80, &c), Kind::kMemory, 0x40070000, 0) && c != 0 && t.LoadedThisScene(80));
  }

  // --- the frame's block is tried once per light set, not once per draw (Task 4 fix round 1, I2) ---
  {
    LightBlockTries t;
    // No set (the light set is not a buffer's of this scene): nothing to read.
    CHECK(t.Open(0) && !t.ShouldTry(0) && t.failed() == 0);
    // A set: read. Without a block, the draws that follow under the same set do not read again,
    // however many they are.
    CHECK(t.ShouldTry(5));
    t.Tried(5, false);
    CHECK(t.failed() == 1);
    for (int draw = 0; draw < 1000; ++draw) CHECK(t.Open(0) && !t.ShouldTry(5));
    // A new load of the set: one more read. Any other number is another load.
    CHECK(t.ShouldTry(6) && t.ShouldTry(4) && !t.ShouldTry(0));
    t.Tried(6, true);
    // The frame has its block: nothing more is read, whatever is loaded.
    CHECK(!t.Open(0) && !t.ShouldTry(7) && !t.ShouldTry(5) && t.failed() == 1);
    // The next frame begins anew: also for a number seen in the frame before.
    CHECK(t.Open(1) && t.failed() == 0 && t.ShouldTry(5) && t.ShouldTry(6));
    CHECK(t.Open(1) && t.ShouldTry(5));  // asking again in the frame forgets nothing
    // At most kMaxSets sets are read in a frame without a block.
    CHECK(LightBlockTries::kMaxSets == 8);
    for (uint64_t set = 10; set < 10 + LightBlockTries::kMaxSets; ++set) {
      CHECK(t.Open(1) && t.ShouldTry(set));
      t.Tried(set, false);
      CHECK(!t.ShouldTry(set) && t.failed() == set - 9);
    }
    CHECK(!t.Open(1) && !t.ShouldTry(99) && t.failed() == 8);
    t.Tried(99, false);  // (not asked for: the count stays at the cap's)
    CHECK(t.failed() == 8 && !t.Open(1));
    CHECK(t.Open(2) && t.ShouldTry(99) && t.failed() == 0);
    // A frame number that goes back is another frame too.
    t.Tried(99, false);
    CHECK(t.Open(1) && t.failed() == 0 && t.ShouldTry(99));
    // Only the last failed set is held: a set is a new upload each time it is loaded, so an
    // earlier number cannot come again in the frame, and one that did would be read as new.
    // A read that gave the block is marked only when the frame has taken it (capture.cpp, RecordDraw:
    // after SetLight, under the scene's lock). Until then the frame stays open and the same set is
    // read again at the next draw: a draw dropped between the read and the lock loses nothing.
    {
      LightBlockTries pending;
      CHECK(pending.Open(3) && pending.ShouldTry(41));   // the read is made and gives a block ...
      CHECK(pending.Open(3) && pending.ShouldTry(41) && pending.failed() == 0);  // ... not marked: read again
      pending.Tried(41, true);                            // the frame took it
      CHECK(!pending.Open(3) && !pending.ShouldTry(41) && !pending.ShouldTry(42));
      CHECK(pending.Open(4) && pending.ShouldTry(41));    // the next frame reads its own
    }
    LightBlockTries u;
    CHECK(u.Open(0));
    u.Tried(3, false);
    u.Tried(4, false);
    CHECK(u.ShouldTry(3) && !u.ShouldTry(4) && u.failed() == 2);
  }
  // The frame the review describes: a lantern's set first, twenty one-light draws under it, then
  // the sun's set. One read without a block, then the block; per draw it took all eight reads at
  // the lantern's draws and the frame had none.
  {
    ConstantLoads loads = InScene();
    LightBlockTries tries;
    uint32_t reads = 0, blocks = 0;
    auto draw = [&](bool sun) {
      if (!tries.Open(7)) return;
      uint64_t set = 0;
      if (!loads.LoadedThisScene(80) || loads.Find(80, &set).kind != Kind::kMemory) set = 0;
      if (!tries.ShouldTry(set)) return;
      ++reads;
      blocks += sun ? 1 : 0;
      tries.Tried(set, sun);
    };
    draw(false);  // before any load of the set: no read
    CHECK(reads == 0);
    CHECK(loads.Load(80, 36, 0x40010000, 0, false, kAll));
    for (int i = 0; i < 20; ++i) draw(false);
    CHECK(reads == 1 && blocks == 0 && tries.failed() == 1);
    CHECK(loads.Load(80, 36, 0x40020000, 0, false, kAll));
    for (int i = 0; i < 20; ++i) draw(true);
    CHECK(reads == 2 && blocks == 1 && tries.failed() == 1);
    CHECK(loads.Load(80, 36, 0x40030000, 0, false, kAll));
    for (int i = 0; i < 20; ++i) draw(true);
    CHECK(reads == 2 && blocks == 1);
    // A frame of lantern sets only: one read per set, eight at most.
    LightBlockTries night;
    reads = 0;
    for (int group = 0; group < 59; ++group) {  // (59 loads of the set in a frame at the bridge)
      CHECK(loads.Load(80, 36, 0x40040000, 0, false, kAll));
      for (int i = 0; i < 15; ++i) {
        if (!night.Open(8)) continue;
        uint64_t set = 0;
        loads.Find(80, &set);
        if (!night.ShouldTry(set)) continue;
        ++reads;
        night.Tried(set, false);
      }
    }
    CHECK(reads == 8 && night.failed() == 8);
  }
  // constant-initialised, as the capture declares it (thread-local)
  {
    static constinit LightBlockTries tries;
    CHECK(tries.Open(0) && tries.failed() == 0);
  }
  // The light set: the bridge's four-light set gives two lanterns in slot order, the sun at slot 2.
  {
    static_assert(sizeof(LocalLight) == 64 && sizeof(LightSet) == 6 * 64 + 4 + 4 + 12 + 1 + 1 + 2);
    Bank b = FourLight();
    CHECK(LightSetSunSlot(b.reg, b.have) == 2);
    LightSet s = MakeLightSet(b.reg, b.have, true);
    CHECK(s.valid && s.reason == LightSetReason::kNone && s.count == 2 && s.sun_slot == 2);
    CHECK(s.lights[0].pos[0] == 93.7503f && s.lights[0].w == 1.0f && s.lights[0].col[1] == 0.727093f && s.lights[0].blend == 1.0f &&
          s.lights[0].range == 0.2505f && s.lights[0].cone_cos == -1.0f && s.lights[0].cone_scale == 1.0f && s.lights[0].shadow == 0.0f &&
          s.lights[0].axis[2] == 0.0f);
    CHECK(s.lights[1].pos[0] == 115.876f && s.lights[1].range == 0.2302f && s.lights[1].col[0] == 1.64307f);
    CHECK(s.sun_dir[0] == -0.671334f && s.sun_dir[2] == 0.735169f);
    CHECK(LightSetEqual(s, MakeLightSet(b.reg, b.have, true)));
    // Not loaded this scene: refused.
    CHECK(!MakeLightSet(b.reg, b.have, false).valid && MakeLightSet(b.reg, b.have, false).reason == LightSetReason::kNotThisScene);
    // A set whose c112 is not one-hot, or whose named slot is a position (w = 1), or has c94 = 1 there: no sun.
    Bank two = b; two.Set(112, 1.0f, 0.0f, 1.0f, 0.0f);
    CHECK(MakeLightSet(two.reg, two.have, true).reason == LightSetReason::kNoSun);
    Bank pos = b; pos.Set(82, 1.0f, 2.0f, 3.0f, 1.0f);
    CHECK(MakeLightSet(pos.reg, pos.have, true).reason == LightSetReason::kNoSun);
    Bank blend = b; blend.Set(94, 1.0f, 1.0f, 1.0f, 0.0f);
    CHECK(MakeLightSet(blend.reg, blend.have, true).reason == LightSetReason::kNoSun);
    // A lantern register not read: unread; a NaN colour, a position past 1,024, an infinite range: unusable.
    Bank unread = b; unread.have[96] = false;
    CHECK(MakeLightSet(unread.reg, unread.have, true).reason == LightSetReason::kUnread);
    Bank nan = b; nan.Set(88, std::numeric_limits<float>::quiet_NaN(), 0.7f, 0.1f, 1.0f);
    CHECK(MakeLightSet(nan.reg, nan.have, true).reason == LightSetReason::kUnusable);
    Bank far = b; far.Set(81, 100000.0f, 158.0f, 41.0f, 1.0f);
    CHECK(MakeLightSet(far.reg, far.have, true).reason == LightSetReason::kUnusable);
    Bank inf = b; inf.Set(96, 0.25f, std::numeric_limits<float>::infinity(), 0.0f, 0.0f);
    CHECK(MakeLightSet(inf.reg, inf.have, true).reason == LightSetReason::kUnusable);
    // The sun in slot 0 (a one-light set): valid, no lanterns. A dark lantern (colour zero) is dropped.
    Bank day = Day(); day.Set(113, 0.0f, 0.0f, 0.0f, 0.0f);  // a set loads c112 and c113 together
    LightSet none = MakeLightSet(day.reg, day.have, true);
    CHECK(none.valid && none.count == 0 && none.sun_slot == 0);
    Bank dark = b; dark.Set(87, 0.0f, 0.0f, 0.0f, 1.0f);
    LightSet one = MakeLightSet(dark.reg, dark.have, true);
    CHECK(one.valid && one.count == 1 && one.lights[0].pos[0] == 115.876f);
    // Slots 4 to 6 read the second bank: the sun at slot 4 with lanterns in 0..3 uses c95, c97, c106, c108, c113 for none of
    // them; a sun at slot 5 reads the lantern of slot 4 from the second bank.
    Bank seven = b;
    seven.Set(82, 50.0f, 60.0f, 40.0f, 1.0f); seven.Set(89, 1.0f, 0.5f, 0.1f, 1.0f);
    seven.Set(83, 51.0f, 61.0f, 41.0f, 1.0f); seven.Set(90, 1.1f, 0.5f, 0.1f, 1.0f);
    seven.Set(84, 52.0f, 62.0f, 42.0f, 1.0f); seven.Set(91, 1.2f, 0.5f, 0.1f, 1.0f);
    seven.Set(85, -0.671334f, 0.0940108f, 0.735169f, 0.0f); seven.Set(92, 1.0f, 0.8f, 0.4f, 1.0f);
    seven.Set(94, 1.0f, 1.0f, 1.0f, 1.0f); seven.Set(95, 1.0f, 0.0f, 0.0f, 0.0f);
    seven.Set(96, 0.25f, 0.25f, 0.25f, 0.25f); seven.Set(97, 0.125f, 0.0f, 0.0f, 0.0f);
    seven.Set(101, 0.0f, 0.0f, 0.0f, 0.0f); seven.Set(102, 0.0f, 0.0f, 1.0f, 0.0f);
    seven.Set(106, -1.0f, -1.0f, -1.0f, 0.0f); seven.Set(108, 1.0f, 1.0f, 1.0f, 0.0f);
    seven.Set(112, 0.0f, 0.0f, 0.0f, 0.0f); seven.Set(113, 0.0f, 1.0f, 0.0f, 0.0f);
    LightSet s7 = MakeLightSet(seven.reg, seven.have, true);
    CHECK(s7.valid && s7.sun_slot == 5 && s7.count == 5 && s7.lights[4].pos[0] == 52.0f && s7.lights[4].range == 0.125f &&
          s7.lights[4].axis[2] == 1.0f && s7.lights[4].blend == 1.0f && s7.lights[4].shadow == 0.0f);
    CHECK(std::strcmp(LightSetReasonName(LightSetReason::kTooMany), "too-many") == 0);
  }
  // The per-thread cache of sets by upload: a hit within the frame, none across frames, 16 entries.
  {
    LightSetCache cache;
    Bank b = FourLight();
    const LightSet s = MakeLightSet(b.reg, b.have, true);
    CHECK(cache.Find(7, 100) == nullptr);
    cache.Put(7, 100, s);
    CHECK(cache.Find(7, 100) != nullptr && LightSetEqual(*cache.Find(7, 100), s));
    CHECK(cache.Find(8, 100) == nullptr);   // another frame: empty (and the entry of frame 7 is gone)
    CHECK(cache.Find(7, 100) == nullptr);
    for (uint64_t u = 1; u <= 20; ++u) cache.Put(9, u, s);
    CHECK(cache.Find(9, 20) != nullptr && cache.Find(9, 1) == nullptr);  // the oldest entries make room
  }
  std::printf("PASS: light block\n");
  return 0;
}
