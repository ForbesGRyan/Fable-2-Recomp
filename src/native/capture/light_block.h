#pragma once

// The frame's light constants (pure), and the model of where the GPU's value of
// a pixel shader constant comes from at a draw (pure): the device's constant
// bank, or guest memory that a LOAD_ALU_CONSTANT packet named (xdk_layout.h,
// kAluLoad*; frame-map section 14, "Pixel constants"). Also the pure parts of
// the hooks that feed the model.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "xdk_layout.h"

namespace fable2::native::capture {

// The pixel constants every lit scene shader shares (frame-map section 14):
// eye c19.xyz; tint c21.xyz and exponent c21.w of the baked light; additive
// light c20.xyz and output scale c20.w; floor c22.xy and axis c23.xyz of the
// ambient factor; sun direction c80.xyz and colour c87.xyz; c0, which maps a
// pixel position to the screen-size sun mask.
// c80 / c87 are the draw's first light. That is the sun for the one-light
// shaders (four draws in five at the bridge); the two- and four-light shaders
// have their lanterns first (a position with w = 1) and the sun in c81 / c88
// or c82 / c89, so a block is the frame's only when it is made at a draw whose
// first light is the sun, which MakeLightBlock requires.
// Three more registers of the light set are carried for the forms that need
// them (docs/native-renderer/lighting-formulas.md): c109, the axis of the mask
// term (the sun direction in every set seen); c112, which light the screen
// mask shadows (one-hot at the sun's slot in every draw seen); c94, per light
// 1 when its range and cone apply (0 for the sun, 1 for a lantern in every
// draw seen); and c80.w. The two- and four-light forms reduce to the one-light
// form when c112.x = 1, c94.x = 0 and c80.w = 0.
struct LightBlock {
  float eye[3];
  float tint[3];
  float tint_pow;
  float add[3];
  float out_scale;
  float floor[2];
  float axis[3];
  float sun_dir[3];
  float sun_col[3];
  float mask_xform[4];
  float sun_w;          // c80.w
  float mask_axis[4];   // c109
  float shadow_mix[4];  // c112
  float light_mix[4];   // c94
  bool valid;
};
// 39 floats and the flag: one is copied into each frame's scene (FrameScene::light).
static_assert(sizeof(LightBlock) == 160, "LightBlock grew or shrank: the documents state its size");

// The registers MakeLightBlock reads, and those of them that belong to the
// light set (c80..c115, loaded from a constant buffer per group of draws).
inline constexpr uint32_t kLightBlockRegisters[] = {0, 19, 20, 21, 22, 23, 80, 87, 94, 109, 112};
inline constexpr uint32_t kLightSetRegisters[] = {80, 87, 94, 109, 112};

// The block from the pixel registers by number: `reg[r]` is register r,
// `have[r]` whether it was read. `light_set_this_scene`: the light set's
// registers (kLightSetRegisters) come from a constant buffer that was loaded
// since the current main scene began (ConstantLoads::LoadedThisScene). Without
// that the registers are not a frame's light: the bank holds zeros where no
// load was followed, and a buffer of an earlier scene holds an earlier light.
// Valid only when that holds and
//  - every register of the block was read and is finite;
//  - the exponent is in (0, 8] and the output scale in (0, 1024];
//  - c80 is the sun: directional (w = 0) and a unit vector (length within 0.05
//    of 1). A zero direction is not a block: an unused light slot holds
//    (0, 0, 0, 1), and what a night's light set holds has not been captured
//    (controller's ruling, fix round 1: no "night" form until a night capture
//    shows it);
//  - the set's own account of its sun agrees, as it does in every one-light
//    draw of the GPU logs: c112 = (1, 0, 0, 0) (the mask shadows light 0),
//    c109.xyz the same bits as c80.xyz, c94.x = 0 (light 0 has no range).
// An invalid block is all zero: nothing of it may be used.
inline LightBlock MakeLightBlock(const float (*reg)[4], const bool* have, bool light_set_this_scene) {
  if (!reg || !have || !light_set_this_scene) return LightBlock{};
  for (uint32_t r : kLightBlockRegisters) {
    if (!have[r]) return LightBlock{};
    for (int c = 0; c < 4; ++c) {
      if (!std::isfinite(reg[r][c])) return LightBlock{};
    }
  }
  LightBlock l{};
  for (int c = 0; c < 3; ++c) {
    l.eye[c] = reg[19][c];
    l.tint[c] = reg[21][c];
    l.add[c] = reg[20][c];
    l.axis[c] = reg[23][c];
    l.sun_dir[c] = reg[80][c];
    l.sun_col[c] = reg[87][c];
  }
  for (int c = 0; c < 4; ++c) {
    l.mask_xform[c] = reg[0][c];
    l.mask_axis[c] = reg[109][c];
    l.shadow_mix[c] = reg[112][c];
    l.light_mix[c] = reg[94][c];
  }
  l.sun_w = reg[80][3];
  l.tint_pow = reg[21][3];
  l.out_scale = reg[20][3];
  l.floor[0] = reg[22][0];
  l.floor[1] = reg[22][1];
  if (!(l.tint_pow > 0.0f && l.tint_pow <= 8.0f)) return LightBlock{};
  if (!(l.out_scale > 0.0f && l.out_scale <= 1024.0f)) return LightBlock{};
  // In double: the squared length of three finite floats cannot overflow.
  const double x = l.sun_dir[0], y = l.sun_dir[1], z = l.sun_dir[2];
  const double length = std::sqrt(x * x + y * y + z * z);
  if (!(l.sun_w == 0.0f && length >= 0.95 && length <= 1.05)) return LightBlock{};
  if (!(l.shadow_mix[0] == 1.0f && l.shadow_mix[1] == 0.0f && l.shadow_mix[2] == 0.0f && l.shadow_mix[3] == 0.0f)) {
    return LightBlock{};
  }
  if (std::memcmp(l.mask_axis, l.sun_dir, sizeof(l.sun_dir)) != 0) return LightBlock{};
  if (l.light_mix[0] != 0.0f) return LightBlock{};
  l.valid = true;
  return l;
}

// The two constant banks as LoadAluConstants numbers them (r4).
inline constexpr uint32_t kVertexBank = 0;
inline constexpr uint32_t kPixelBank = 1;

// The registers a LoadAluConstants call can name: the ranges of the game's ten
// call sites (xdk_layout.h, kAluLoadSites, where the instructions are named).
// Every other register is bank-only: no buffer is ever its source, so the
// device's bank holds its latest value at any draw, whatever uploads were or
// were not followed (controller's ruling, fix round 2). The pixel registers
// outside these ranges were read that way before the load model existed, and
// the first two captures of frame-map section 14 prove them so.
struct BufferLoadRange {
  uint32_t bank;
  uint32_t start;
  uint32_t count;
};
inline constexpr BufferLoadRange kBufferLoadRanges[] = {
    {kPixelBank, 0, 8},     // c0..c3 per colour tile, c4..c7 per bin group
    {kPixelBank, 80, 36},   // the light set
    {kVertexBank, 40, 4},   // per bin group
    {kVertexBank, 76, 32},  // the vertex light set
};
constexpr bool IsBufferLoadRegister(uint32_t bank, uint32_t reg) {
  for (const BufferLoadRange& r : kBufferLoadRanges) {
    if (r.bank == bank && reg >= r.start && reg - r.start < r.count) return true;
  }
  return false;
}
// The table is the call sites' registers, no fewer and no more.
constexpr bool BufferLoadRangesAreTheCallSites() {
  for (uint32_t bank = 0; bank < 2; ++bank) {
    for (uint32_t reg = 0; reg < 256; ++reg) {
      bool named = false;
      for (const xdk::AluLoadSite& s : xdk::kAluLoadSites) {
        named = named || (s.bank == bank && reg >= s.start && reg - s.start < s.count);
      }
      if (named != IsBufferLoadRegister(bank, reg)) return false;
    }
  }
  for (const xdk::AluLoadSite& s : xdk::kAluLoadSites) {
    if (s.bank > 1 || s.count == 0 || s.start >= 256 || s.count > 256 - s.start) return false;
  }
  return true;
}
static_assert(BufferLoadRangesAreTheCallSites(),
              "kBufferLoadRanges must be exactly the registers the LoadAluConstants call sites name");

// Where the GPU's value of one constant register comes from.
struct ConstantSource {
  enum class Kind : uint8_t {
    kBank,     // the device's constant bank (uploaded by the draw builders)
    kMemory,   // register `index` of the constant buffer `object`
    kUnknown,  // not followed: the value must not be used
  };
  Kind kind = Kind::kBank;
  uint32_t object = 0;
  uint32_t index = 0;
  uint32_t scene = 0;  // kMemory: ConstantLoads::scene() when the load was issued
};

// The loads of one constant bank (256 registers) as one guest thread issued
// them. The GPU holds, per register, the value of the last upload it executed:
//  - the draw builders upload groups of four registers from the device's bank
//    when their pending bit is set (BankUpload; in every pass);
//  - LoadAluConstants loads a run of registers from a constant buffer (Load),
//    in every pass or, when predicated under a bin mask that leaves bins out,
//    only in the passes whose bin select meets the mask. The tiled main scene
//    loads c0..c3 once per colour tile that way and c4..c7 once for the
//    pre-pass bins and once for the colour bins.
// A draw record is drawn once, so Find gives it what the first colour tile
// sees; FindForBins gives what a pass with a given bin select sees.
//
// Only the registers a load can name need the model (kBufferLoadRanges for
// the bank given at construction; a register that a followed call names
// outside them is added and counted, untabled_loads). Every other register is
// the bank's, always.
// For the load registers the model says something only while it is whole:
// every upload since it was last made whole was handed to it. The capture
// follows uploads only while somebody reads the result (records or
// discovery); for an upload it does not follow it calls Skip, and from then on
// no load register is known. Observe tells the model whether the main scene's
// bracket is open; at the first begin of a scene with every upload since the
// last observation followed, the model is whole again, knowing nothing of
// them: each load register is unknown until an upload of it is followed (a
// load, or the bank's upload of its group).
class ConstantLoads {
 public:
  // constexpr: the capture's two models are thread-local, and a constant
  // initialiser lets them be `constinit`, so that no access tests a guard.
  constexpr explicit ConstantLoads(uint32_t bank = kPixelBank) {
    for (uint32_t r = 0; r < kRegisters; ++r) {
      if (IsBufferLoadRegister(bank, r)) load_register_[r / 64] |= 1ull << (r % 64);
    }
  }

  static constexpr uint32_t kRegisters = 256;
  static constexpr uint32_t kMaxBinned = 16;
  static constexpr uint32_t kAllBins = 0xFFFFFFFFu;
  // Registers a buffer can hold (a bound on the arguments, 1 MB of constants).
  static constexpr uint32_t kMaxBufferRegisters = 65536;

  // A load issued under a bin mask that leaves bins out.
  struct Binned {
    uint32_t mask = 0;
    uint32_t start = 0;
    uint32_t count = 0;
    uint32_t object = 0;
    uint32_t first = 0;  // the buffer's register that goes to `start`
    uint32_t scene = 0;
    uint64_t seq = 0;
  };

  // A load of `count` registers from register `first` of the buffer `object`
  // to registers [start, start + count); `predicated` with `mask` the bin mask
  // in force (the low word: a caller with bins in the high word must not call,
  // see MapAluLoad). Returns whether the load was followed. Arguments that
  // cannot be a load of this bank (no buffer, a start or a run outside
  // registers 0..255, which the packet would write to the other bank's
  // registers, a run past the buffer bound) and a tile load the table has no
  // room for are not followed: the model stops being whole (Unfollowable). The
  // game's callers pass constants inside the bank (xdk_layout.h, kAluLoadSites).
  bool Load(uint32_t start, uint32_t count, uint32_t object, uint32_t first, bool predicated, uint32_t mask) {
    if (count == 0) return true;  // loads nothing
    if (start >= kRegisters || count > kRegisters - start || object == 0 || first >= kMaxBufferRegisters ||
        count > kMaxBufferRegisters - first) {
      Unfollowable(start, count);
      return false;
    }
    if (!predicated) mask = kAllBins;
    // Named first: a call under no bin loads nothing this time, but its call
    // site exists, and a register it names outside the table is counted.
    Name(start, count);
    if (mask == 0) return true;  // predicated under no bin: executes in no pass
    if (!whole_) return true;    // followed, and not believed before the model is whole again
    const uint64_t seq = ++seq_;
    if (mask != kAllBins) {
      Collect(start, count, mask);
      if (binned_count_ == kMaxBinned) Sweep();
      if (binned_count_ == kMaxBinned) {
        Skip();  // no room: the tile loads cannot be told apart any more
        return false;
      }
      binned_[binned_count_++] = Binned{mask, start, count, object, first, scene_, seq};
      MarkLoaded(start, count);
      return true;
    }
    Collect(start, count, kAllBins);
    for (uint32_t i = 0; i < count; ++i) {
      every_[start + i] = Entry{{ConstantSource::Kind::kMemory, object, first + i, scene_}, seq};
    }
    MarkLoaded(start, count);
    return true;
  }

  // The draw builder uploads the device bank's groups in `dirty` (the pending
  // mask handed to SetPending_AluConstants: bit 63 - g is registers 4g..4g+3).
  void BankUpload(uint64_t dirty) {
    uint64_t hit = dirty & loaded_;
    if (!hit || !whole_) return;
    const uint64_t seq = ++seq_;
    for (uint32_t g = 0; g < kRegisters / 4; ++g) {
      if (!(hit & (1ull << (63 - g)))) continue;
      Collect(4 * g, 4, kAllBins);
      SetEvery(4 * g, 4, ConstantSource{}, seq);
      // The group is the bank's and newer than every tile load over it: a
      // further upload of it changes nothing until a load names it again.
      loaded_ &= ~(1ull << (63 - g));
    }
  }

  // A LoadAluConstants call that was seen and cannot be followed: the registers
  // of this bank that it names are load registers from now on, and its upload
  // counts as skipped.
  void Unfollowable(uint32_t start, uint32_t count) {
    Name(start, count);
    Skip();
  }

  // Calls seen that named a register outside the table for the first time: a
  // call site the static reading does not know. The capture logs the first.
  uint32_t untabled_loads() const { return untabled_loads_; }

  // An upload happened that was not handed to the model.
  void Skip() {
    whole_ = false;
    ever_skipped_ = true;
    skip_pending_ = true;
  }

  // The main scene's bracket as a hook whose uploads are being followed sees
  // it (`tiling`: the device's tiling flag is set), before the hook's own
  // upload is handed over. The first observation with the flag set begins a
  // scene. A model that is not whole becomes whole there if no upload was
  // skipped since the observation before (so every upload of the scene, from
  // its begin, is followed).
  void Observe(bool tiling) {
    if (tiling && !in_scene_) {
      ++scene_;
      if (!whole_ && !skip_pending_) Resume();
    }
    in_scene_ = tiling;
    skip_pending_ = false;
  }

  bool whole() const { return whole_; }
  bool ever_skipped() const { return ever_skipped_; }  // since construction: the model had a gap
  bool in_scene() const { return in_scene_; }
  uint32_t scene() const { return scene_; }  // scenes begun

  // Whether `reg`, as a draw record sees it (Find), comes from a constant
  // buffer that was loaded since the current scene began.
  bool LoadedThisScene(uint32_t reg) const {
    if (!in_scene_) return false;
    const ConstantSource from = Find(reg);
    return from.kind == ConstantSource::Kind::kMemory && from.scene == scene_;
  }

  // The source of `reg` as a pass with this bin select sees it: the bank for a
  // register no load names, in any state of the model; for a load register the
  // last upload the pass executed, unknown while the model is not whole.
  // `upload` (optional) receives the order number of that upload: one number
  // per followed upload of this model (a load, the bank's upload of a group),
  // never 0, so that two answers with one number are one upload and a register
  // loaded again, from the same buffer too, has another. 0 where no followed
  // upload decides the register: one no load names, one out of range, a model
  // that is not whole, a load register nothing was uploaded to yet.
  ConstantSource FindForBins(uint32_t reg, uint32_t bin_select, uint64_t* upload = nullptr) const {
    if (upload) *upload = 0;
    if (reg >= kRegisters) return Unknown();
    if (!IsLoadRegister(reg)) return ConstantSource{};
    if (!whole_) return Unknown();
    const Entry* best = &every_[reg];
    ConstantSource from = best->source;
    uint64_t seq = best->seq;
    for (uint32_t i = 0; i < binned_count_; ++i) {
      const Binned& b = binned_[i];
      if (!(b.mask & bin_select) || reg < b.start || reg - b.start >= b.count || b.seq < seq) continue;
      seq = b.seq;
      from = ConstantSource{ConstantSource::Kind::kMemory, b.object, b.first + (reg - b.start), b.scene};
    }
    if (upload && from.kind != ConstantSource::Kind::kUnknown) *upload = seq;
    return from;
  }

  // The source of `reg` for a draw record: what the first colour tile sees
  // (the lowest colour bin any tile load of the register names; xdk_layout.h
  // kColourBins). A load for pre-pass bins alone never decides it. `upload`:
  // as FindForBins.
  ConstantSource Find(uint32_t reg, uint64_t* upload = nullptr) const {
    if (upload) *upload = 0;
    if (reg >= kRegisters) return Unknown();
    uint32_t bins = 0;
    for (uint32_t i = 0; i < binned_count_; ++i) {
      const Binned& b = binned_[i];
      if (reg >= b.start && reg - b.start < b.count) bins |= b.mask & xdk::kColourBins;
    }
    return FindForBins(reg, bins & (0u - bins), upload);
  }

  // The tile loads, oldest first, and whether one still decides `reg` in the
  // passes its mask names (nothing later replaced it there in every pass).
  // None while the model is not whole: what the table holds then was loaded
  // before the gap, and no register it names is known (FindForBins).
  uint32_t binned_count() const { return whole_ ? binned_count_ : 0; }
  const Binned& binned(uint32_t i) const { return binned_[i]; }
  bool InForce(const Binned& b, uint32_t reg) const {
    return reg < kRegisters && reg >= b.start && reg - b.start < b.count && b.seq > every_[reg].seq;
  }

 private:
  struct Entry {
    ConstantSource source;
    uint64_t seq = 0;
  };
  static ConstantSource Unknown() { return ConstantSource{ConstantSource::Kind::kUnknown, 0, 0, 0}; }

  bool IsLoadRegister(uint32_t reg) const { return (load_register_[reg / 64] >> (reg % 64)) & 1; }
  // The registers of this bank in [start, start + count) become load registers.
  void Name(uint32_t start, uint32_t count) {
    bool added = false;
    for (uint32_t r = start; r < kRegisters && r - start < count; ++r) {
      if (IsLoadRegister(r)) continue;
      load_register_[r / 64] |= 1ull << (r % 64);
      added = true;
    }
    if (added) ++untabled_loads_;
  }
  // Whole again, knowing nothing of what the GPU holds in the load registers.
  void Resume() {
    whole_ = true;
    binned_count_ = 0;
    const uint64_t seq = ++seq_;
    for (uint32_t r = 0; r < kRegisters; ++r) every_[r] = Entry{IsLoadRegister(r) ? Unknown() : ConstantSource{}, seq};
    loaded_ = ~0ull;  // every group's next bank upload is followed
  }
  void SetEvery(uint32_t start, uint32_t n, ConstantSource source, uint64_t seq) {
    for (uint32_t i = 0; i < n; ++i) every_[start + i] = Entry{source, seq};
    MarkLoaded(start, n);
  }
  void MarkLoaded(uint32_t start, uint32_t n) {
    for (uint32_t g = start / 4; g <= (start + n - 1) / 4; ++g) loaded_ |= 1ull << (63 - g);
  }
  void Remove(uint32_t i) {
    for (uint32_t k = i + 1; k < binned_count_; ++k) binned_[k - 1] = binned_[k];
    --binned_count_;
  }
  // Drops the tile loads a new upload of [start, start + n) under `mask`
  // replaces in every pass they execute in.
  void Collect(uint32_t start, uint32_t n, uint32_t mask) {
    for (uint32_t i = binned_count_; i-- > 0;) {
      const Binned& b = binned_[i];
      if (b.start >= start && b.start - start < n && b.count <= n - (b.start - start) && !(b.mask & ~mask)) Remove(i);
    }
  }
  // Drops the tile loads that decide no register any more (called when the table is full).
  void Sweep() {
    for (uint32_t i = binned_count_; i-- > 0;) {
      const Binned& b = binned_[i];
      bool in_force = false;
      for (uint32_t r = b.start; r < b.start + b.count && !in_force; ++r) in_force = InForce(b, r);
      if (!in_force) Remove(i);
    }
  }

  Entry every_[kRegisters];
  Binned binned_[kMaxBinned];
  uint32_t binned_count_ = 0;
  uint64_t seq_ = 0;
  // Bit 63 - g: group g may hold something other than the bank's value as of
  // its last upload (a load named one of its registers, or the model was made
  // whole and does not know the group yet). A bank upload of another group
  // costs one test.
  uint64_t loaded_ = 0;
  uint32_t scene_ = 0;
  bool in_scene_ = false;
  bool whole_ = true;
  bool ever_skipped_ = false;
  bool skip_pending_ = false;  // an upload was skipped since the last observation
  uint64_t load_register_[kRegisters / 64] = {};  // bit r: a load can name register r
  uint32_t untabled_loads_ = 0;
};

// --- the light set: the lanterns of a multi-light draw (local lights, 2026-10-13) --------------
// One local light as the two-, four- and seven-light shaders read slot j (lighting-formulas.md, the
// multi form; light_formulas.local_light): the position or direction c(80 + j) with its w, the colour
// c(87 + j), and component j of c94 (blend of cone and range), c96 (range term), c105 and c107 (the
// cone's cosine and slope), c112 (shadow strength), with the cone axis c(98 + j); slots 4 to 6 read
// c95, c97, c106, c108, c113. 64 bytes, the layout the frame constant buffer carries (four float4).
struct LocalLight {
  float pos[3]; float w;
  float col[3]; float blend;
  float range; float cone_cos; float cone_scale; float shadow;
  float axis[3]; float pad;
};
static_assert(sizeof(LocalLight) == 64);
inline constexpr uint32_t kLightSetSlots = 7, kMaxLightSetLanterns = 6;
// The number of distinct light sets a frame keeps (the capture's side array and the pass read it).
inline constexpr uint32_t kMaxFrameLightSets = 16;
enum class LightSetReason : uint8_t { kNone, kNotThisScene, kNoSun, kUnread, kUnusable, kTooMany, kCount };
inline const char* LightSetReasonName(LightSetReason r) {
  switch (r) {
    case LightSetReason::kNone: return "none";
    case LightSetReason::kNotThisScene: return "not-this-scene";
    case LightSetReason::kNoSun: return "no-sun";
    case LightSetReason::kUnread: return "unread";
    case LightSetReason::kUnusable: return "unusable";
    case LightSetReason::kTooMany: return "too-many";
    default: return "?";
  }
}
// A draw's light set as the pass needs it: the lanterns (the slots before the sun's, lit ones, in
// slot order), the sun's slot and its direction (the pass compares it with the frame's block:
// MakeLanternBuffer refuses a set whose sun is not the frame's). `valid` false: `reason` says why
// and nothing else is read.
struct LightSet {
  LocalLight lights[kMaxLightSetLanterns] = {};  // (zero past `count`: no indeterminate bytes)
  uint32_t count = 0;
  uint32_t sun_slot = 0;
  float sun_dir[3] = {};
  LightSetReason reason = LightSetReason::kNone;
  bool valid = false;
  uint8_t pad[2] = {};
};
static_assert(sizeof(LightSet) == 6 * 64 + 4 + 4 + 12 + 1 + 1 + 2);
// The usable magnitudes, as the pass's rules (clay_logic.h LightValueUsable, LightPositionUsable):
// a value at most 1,024, a position at most 65,536.
inline constexpr float kLightSetValueMax = 1024.0f, kLightSetPositionMax = 65536.0f;
inline bool LightSetValueUsable(float v) { return std::fabs(v) <= kLightSetValueMax; }
inline bool LightSetPositionUsable(float v) { return std::fabs(v) <= kLightSetPositionMax; }
// The per-slot registers: {blend, range, cone cos, cone slope, shadow} and the component, by slot.
struct LightSetSlotRegs { uint32_t blend, range, cone_cos, cone_scale, shadow, comp; };
constexpr LightSetSlotRegs LightSetRegsOf(uint32_t slot) {
  return slot < 4 ? LightSetSlotRegs{94, 96, 105, 107, 112, slot} : LightSetSlotRegs{95, 97, 106, 108, 113, slot - 4};
}
// The sun's slot: the one component of c112.xyzw, c113.xyz that is 1 with every other 0 (one-hot).
// kLightSetSlots when the two registers were not read or are not one-hot.
inline uint32_t LightSetSunSlot(const float (*reg)[4], const bool* have) {
  if (!reg || !have || !have[112] || !have[113]) return kLightSetSlots;
  uint32_t hot = kLightSetSlots, hots = 0;
  for (uint32_t slot = 0; slot < kLightSetSlots; ++slot) {
    const LightSetSlotRegs r = LightSetRegsOf(slot);
    const float v = reg[r.shadow][r.comp];
    if (v == 1.0f) { hot = slot; ++hots; } else if (v != 0.0f) return kLightSetSlots;
  }
  return hots == 1 ? hot : kLightSetSlots;
}
inline LightSet MakeLightSet(const float (*reg)[4], const bool* have, bool light_set_this_scene) {
  LightSet s;
  auto refuse = [&](LightSetReason why) { LightSet r; r.reason = why; return r; };
  if (!reg || !have) return refuse(LightSetReason::kUnread);
  if (!light_set_this_scene) return refuse(LightSetReason::kNotThisScene);
  const uint32_t sun = LightSetSunSlot(reg, have);
  if (sun >= kLightSetSlots) return refuse(LightSetReason::kNoSun);
  // The sun: a direction (w = 0) without range or cone (c94 zero there), read.
  const LightSetSlotRegs sr = LightSetRegsOf(sun);
  if (!have[80 + sun] || !have[sr.blend]) return refuse(LightSetReason::kUnread);
  if (reg[80 + sun][3] != 0.0f || reg[sr.blend][sr.comp] != 0.0f) return refuse(LightSetReason::kNoSun);
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(reg[80 + sun][i])) return refuse(LightSetReason::kUnusable);
    s.sun_dir[i] = reg[80 + sun][i];
  }
  s.sun_slot = sun;
  for (uint32_t slot = 0; slot < sun; ++slot) {
    const LightSetSlotRegs r = LightSetRegsOf(slot);
    if (!have[80 + slot] || !have[87 + slot] || !have[98 + slot] || !have[r.blend] || !have[r.range] || !have[r.cone_cos] ||
        !have[r.cone_scale] || !have[r.shadow]) {
      return refuse(LightSetReason::kUnread);
    }
    LocalLight l{};
    for (int i = 0; i < 3; ++i) {
      l.pos[i] = reg[80 + slot][i];
      l.col[i] = reg[87 + slot][i];
      l.axis[i] = reg[98 + slot][i];
      if (!LightSetPositionUsable(l.pos[i]) || !LightSetValueUsable(l.col[i]) || !LightSetValueUsable(l.axis[i])) {
        return refuse(LightSetReason::kUnusable);
      }
    }
    l.w = reg[80 + slot][3];
    l.blend = reg[r.blend][r.comp];
    l.range = reg[r.range][r.comp];
    l.cone_cos = reg[r.cone_cos][r.comp];
    l.cone_scale = reg[r.cone_scale][r.comp];
    l.shadow = reg[r.shadow][r.comp];
    if (!LightSetValueUsable(l.w) || !LightSetValueUsable(l.blend) || !LightSetValueUsable(l.range) ||
        !LightSetValueUsable(l.cone_cos) || !LightSetValueUsable(l.cone_scale) || !LightSetValueUsable(l.shadow)) {
      return refuse(LightSetReason::kUnusable);
    }
    if (l.col[0] == 0.0f && l.col[1] == 0.0f && l.col[2] == 0.0f) continue;  // an unused slot
    s.lights[s.count++] = l;
  }
  s.valid = true;
  return s;
}
inline bool LightSetEqual(const LightSet& a, const LightSet& b) {
  if (a.valid != b.valid || a.count != b.count || a.sun_slot != b.sun_slot || a.reason != b.reason) return false;
  return std::memcmp(a.lights, b.lights, sizeof(LocalLight) * a.count) == 0 && std::memcmp(a.sun_dir, b.sun_dir, sizeof(a.sun_dir)) == 0;
}

// Which reads of the frame's light block the capture makes (capture.cpp,
// RecordDraw). A read is costly (eleven ReadPixelConstant calls) and its
// result depends on the light set in force, which the game loads once per
// group of draws (57 to 59 loads of pixel c80..c115 in a main scene of the two
// places captured: frame-map section 14, "Light inputs"). So a set that gave
// no block (a lantern's, a set without a sun) is not read again at the draws
// that follow under it: the next read is at the first draw under another
// upload of the set. A read can also fail for a register that is not the
// set's (one of c0, c19..c23 not read or out of range); that is not told
// apart, and such a frame gets its block one set later: at the next
// one-light draw under another load of the set, which is not a few draws on
// (the final review counted 76 draws at the bridge and 86 by the trees).
// `set` is that upload's order number
// (ConstantLoads::Find(c80, &set) where the set was loaded in this scene; 0
// where it was not, and then nothing is read).
// At most kMaxSets sets are read in a frame without a block: what a frame with
// no sun at all costs, eight reads of eleven registers, as much as the reads
// cost before this rule (when they were counted per draw and one set without a
// sun could take all eight). Once a read gives a block the frame has it and
// nothing more is read. One per guest thread, as the load model is whose
// numbers it holds; `frame` is any number that changes with the frame.
class LightBlockTries {
 public:
  static constexpr uint32_t kMaxSets = 8;

  // Whether the frame still wants a block: none yet and fewer than kMaxSets
  // sets read for it. A `frame` other than the last one's begins anew.
  constexpr bool Open(uint32_t frame) {
    if (frame != frame_) {
      frame_ = frame;
      last_failed_ = 0;
      failed_ = 0;
      done_ = false;
    }
    return !done_ && failed_ < kMaxSets;
  }
  // Whether to read the block at a draw under `set` (call after Open said
  // yes): there is a set, and it is not the one the last read without a block
  // was made under. Only that one is held: a set is another upload each time
  // it is loaded, so an earlier number does not come again.
  constexpr bool ShouldTry(uint64_t set) const { return !done_ && failed_ < kMaxSets && set != 0 && set != last_failed_; }
  // A read was made under `set`; `valid`: it gave the frame's block.
  constexpr void Tried(uint64_t set, bool valid) {
    if (valid) {
      done_ = true;
    } else if (failed_ < kMaxSets) {
      last_failed_ = set;
      ++failed_;
    }
  }
  // Sets read without a block in the frame (at most kMaxSets).
  constexpr uint32_t failed() const { return failed_; }

 private:
  uint64_t last_failed_ = 0;
  uint32_t frame_ = 0;
  uint32_t failed_ = 0;
  bool done_ = false;
};

// The light sets a thread read this frame, by the upload that loaded them (capture.cpp
// LightSetUpload: an upload's order number in the thread's load model): at most kEntries, the
// oldest replaced; another frame empties it. So a set is read (capture.cpp ReadLightSet: 4 and at most
// 7 per lantern slot ReadPixelConstant calls) once per upload, at the first draw of a multi-light family
// or the leaves under it, not at every such draw. A set read in one frame is never another frame's
// (the frame number keys it); the builder dedupes by value anyway (FrameBuilder::AddLightSet). One
// per guest thread, as the load model is whose numbers it holds; `frame` is any number that
// changes with the frame (g_light_tries_frame, as for LightBlockTries).
class LightSetCache {
 public:
  static constexpr uint32_t kEntries = 16;
  const LightSet* Find(uint32_t frame, uint64_t upload) {
    Open(frame);
    for (uint32_t i = 0; i < count_; ++i) {
      if (uploads_[i] == upload) return &sets_[i];
    }
    return nullptr;
  }
  void Put(uint32_t frame, uint64_t upload, const LightSet& set) {
    Open(frame);
    const uint32_t at = count_ < kEntries ? count_++ : next_++ % kEntries;
    uploads_[at] = upload;
    sets_[at] = set;
  }

 private:
  void Open(uint32_t frame) {
    if (frame != frame_) {
      frame_ = frame;
      count_ = 0;
      next_ = 0;
    }
  }
  uint64_t uploads_[kEntries] = {};
  LightSet sets_[kEntries] = {};
  uint32_t frame_ = 0, count_ = 0, next_ = 0;
};

// --- the hooks' pure parts ---------------------------------------------------

// A LoadAluConstants call (0x8227CFC8) as its hook sees it: the arguments, and
// the device's predicate word and bin mask (`state_read` false when one of
// them could not be read).
struct AluLoadCall {
  uint32_t bank = 0;    // r4: 0 vertex, 1 pixel
  uint32_t start = 0;   // r5: first register
  uint32_t count = 0;   // r6
  uint32_t object = 0;  // r7: constant buffer object
  uint32_t first = 0;   // r8: the buffer's first register
  bool state_read = false;
  uint32_t predicate = 0;  // device + kDevicePredicateOffset
  uint32_t mask_lo = 0;    // device + kDeviceBinMaskLoOffset
  uint32_t mask_hi = 0;    // device + kDeviceBinMaskHiOffset
};

// The same as a ConstantLoads::Load of one bank's model.
struct AluLoad {
  bool pixel = false;
  uint32_t start = 0;
  uint32_t count = 0;
  uint32_t object = 0;
  uint32_t first = 0;
  bool predicated = false;
  uint32_t mask = 0;
};

// Maps a call to a load. False when it cannot be followed, and the caller then
// tells the bank's model (both models when the bank is neither) that an upload
// was skipped: a bank number other than 0 and 1; device state that could not
// be read; a start or a run outside registers 0..255 (the packet's register
// index would reach the other bank; the game's callers pass constants inside
// the bank); no buffer or a run past the buffer bound; and a predicated load
// with bins in the high word of the bin mask (the model keeps the low word,
// which is what the masks of the game's tiled scene use: 0x15555555,
// 0x2AAAAAAA, 2 << 2i; the "aluload" row carries the high word).
inline bool MapAluLoad(const AluLoadCall& c, AluLoad* out) {
  if (!out || c.bank > 1 || !c.state_read) return false;
  if (c.start >= ConstantLoads::kRegisters || c.count > ConstantLoads::kRegisters - c.start) return false;
  if (c.count != 0 && (c.object == 0 || c.first >= ConstantLoads::kMaxBufferRegisters ||
                       c.count > ConstantLoads::kMaxBufferRegisters - c.first)) {
    return false;
  }
  const bool predicated = c.predicate != 0;
  if (predicated && c.mask_hi != 0) return false;
  out->pixel = c.bank == 1;
  out->start = c.start;
  out->count = c.count;
  out->object = c.object;
  out->first = c.first;
  out->predicated = predicated;
  out->mask = predicated ? c.mask_lo : ConstantLoads::kAllBins;
  return true;
}

// The guest address of register `index` of a constant buffer whose data is at
// `data`. False without a buffer, past the buffer bound, or when the
// register's 16 bytes would not lie below 4 GB.
inline bool BufferRegisterAddress(uint32_t data, uint32_t index, uint32_t* address) {
  if (!address || !data || index >= ConstantLoads::kMaxBufferRegisters) return false;
  const uint64_t at = uint64_t(data) + 16ull * index;
  if (at + 16 > 0x100000000ull) return false;
  *address = uint32_t(at);
  return true;
}

// Discovery evidence: the pixel bool word (b128..b159) as its last writer on
// one guest thread put it on the GPU; `known` false when that cannot be said.
// Two writers (xdk_layout.h): GpuLoadShaders uploads the engine's flow
// constants when one of its shaders uses them (the shaders' own bool literals,
// which the upload also carries, are not applied here), and a draw builder
// that finds the bool shadow pending writes the device's shadow. The two
// account for the GPU's word in the main scene's colour executions; in the
// pre-pass and in a pass at pitch 1040 it differs, in bits no shader running
// there tests (frame-map section 14, "The bool word").
struct BoolWordWriters {
  uint32_t word = 0;
  bool known = false;

  // GpuLoadShaders returned. `flags_read`: both shaders' records were read;
  // `uploads`: one of them uses flow constants; the engine's word and whether
  // it was read.
  void OnShaderLoad(bool flags_read, bool uploads, bool engine_read, uint32_t engine_word) {
    if (!flags_read) {
      Forget();
    } else if (uploads) {
      Set(engine_read, engine_word);
    }  // else the GPU keeps what it has
  }
  // A draw builder that found the bool shadow pending returned: the device's
  // shadow word and whether it was read.
  void OnBuilderFlush(bool shadow_read, uint32_t shadow_word) { Set(shadow_read, shadow_word); }
  void Forget() { known = false; }

 private:
  void Set(bool read, uint32_t value) {
    known = read;
    word = read ? value : 0;
  }
};

}  // namespace fable2::native::capture
