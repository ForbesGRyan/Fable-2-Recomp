#pragma once

// The frame's light constants (pure), and the model of where the GPU's value of
// a pixel shader constant comes from at a draw (pure): the device's constant
// bank, or guest memory that a LOAD_ALU_CONSTANT packet named (xdk_layout.h,
// kAluLoad*; frame-map section 14, "Pixel constants").

#include <cmath>
#include <cstdint>

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
// first light is directional (w = 0), which MakeLightBlock requires.
// Three more registers of the light set are carried as they are, for the
// forms that need them (docs/native-renderer/lighting-formulas.md): c109, the
// axis of the mask term (the sun direction in every set seen); c112, which
// light the screen mask shadows (one-hot at the sun's slot in every draw
// seen); c94, per light 1 when its range and cone apply (0 for the sun, 1 for
// a lantern in every draw seen); and c80.w. The two- and four-light forms
// reduce to the one-light form when c112.x = 1, c94.x = 0 and c80.w = 0.
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

// The registers MakeLightBlock reads.
inline constexpr uint32_t kLightBlockRegisters[] = {0, 19, 20, 21, 22, 23, 80, 87, 94, 109, 112};

// The block from the pixel registers by number: `reg[r]` is register r,
// `have[r]` whether it was read. Valid only when every register of the block
// was read and is finite, the exponent is in (0, 8], the output scale is in
// (0, 1024], and the sun direction is a unit vector (length within 0.05 of 1)
// of a directional light (c80.w = 0), or exactly zero (night, or an unused
// light slot, which holds (0, 0, 0, 1)). c94, c109 and c112 only have to be
// finite. An invalid block is all zero: nothing of it may be used.
inline LightBlock MakeLightBlock(const float (*reg)[4], const bool* have) {
  if (!reg || !have) return LightBlock{};
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
  const bool night = x == 0.0 && y == 0.0 && z == 0.0;
  const bool directional = reg[80][3] == 0.0f;
  if (!night && !(directional && length >= 0.95 && length <= 1.05)) return LightBlock{};
  l.valid = true;
  return l;
}

// Where the GPU's value of one constant register comes from.
struct ConstantSource {
  enum class Kind : uint8_t {
    kBank,     // the device's constant bank (uploaded by the draw builders)
    kMemory,   // register `index` of the constant buffer `object`
    kUnknown,  // a load the model could not follow: the value must not be used
  };
  Kind kind = Kind::kBank;
  uint32_t object = 0;
  uint32_t index = 0;
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
class ConstantLoads {
 public:
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
    uint64_t seq = 0;
  };

  // A load of `count` registers from register `first` of the buffer `object`
  // to registers [start, start + count); `predicated` with `mask` the bin mask
  // in force. Arguments that cannot be a load (no buffer, a run past register
  // 255 or past the buffer bound) leave the registers they name unknown.
  void Load(uint32_t start, uint32_t count, uint32_t object, uint32_t first, bool predicated, uint32_t mask) {
    if (count == 0 || start >= kRegisters) return;
    const bool fits = count <= kRegisters - start;
    const uint32_t n = fits ? count : kRegisters - start;
    const bool good = fits && object != 0 && first < kMaxBufferRegisters && count <= kMaxBufferRegisters - first;
    if (!predicated) mask = kAllBins;
    if (mask == 0) return;  // executes in no pass
    const uint64_t seq = ++seq_;
    if (good && mask != kAllBins) {
      Collect(start, n, mask);
      if (binned_count_ == kMaxBinned) Sweep();
      if (binned_count_ < kMaxBinned) {
        binned_[binned_count_++] = Binned{mask, start, n, object, first, seq};
        MarkLoaded(start, n);
        return;
      }
      // No room: nothing the tile loads covered can be told apart any more.
      for (uint32_t i = 0; i < binned_count_; ++i) SetEvery(binned_[i].start, binned_[i].count, Unknown(), seq);
      binned_count_ = 0;
      SetEvery(start, n, Unknown(), seq);
      return;
    }
    // Under every bin (or not a load: unknown in every pass).
    Collect(start, n, kAllBins);
    for (uint32_t i = 0; i < n; ++i) {
      every_[start + i] = good ? Entry{{ConstantSource::Kind::kMemory, object, first + i}, seq} : Entry{Unknown(), seq};
    }
    MarkLoaded(start, n);
  }

  // The draw builder uploads the device bank's groups in `dirty` (the pending
  // mask handed to SetPending_AluConstants: bit 63 - g is registers 4g..4g+3).
  void BankUpload(uint64_t dirty) {
    uint64_t hit = dirty & loaded_;
    if (!hit) return;
    const uint64_t seq = ++seq_;
    for (uint32_t g = 0; g < kRegisters / 4; ++g) {
      if (!(hit & (1ull << (63 - g)))) continue;
      Collect(4 * g, 4, kAllBins);
      SetEvery(4 * g, 4, ConstantSource{}, seq);
    }
  }

  // The source of `reg` as a pass with this bin select sees it: the last
  // upload it executed.
  ConstantSource FindForBins(uint32_t reg, uint32_t bin_select) const {
    if (reg >= kRegisters) return Unknown();
    const Entry* best = &every_[reg];
    ConstantSource from = best->source;
    uint64_t seq = best->seq;
    for (uint32_t i = 0; i < binned_count_; ++i) {
      const Binned& b = binned_[i];
      if (!(b.mask & bin_select) || reg < b.start || reg - b.start >= b.count || b.seq < seq) continue;
      seq = b.seq;
      from = ConstantSource{ConstantSource::Kind::kMemory, b.object, b.first + (reg - b.start)};
    }
    return from;
  }

  // The source of `reg` for a draw record: what the first colour tile sees
  // (the lowest colour bin any tile load of the register names; xdk_layout.h
  // kColourBins). A load for pre-pass bins alone never decides it.
  ConstantSource Find(uint32_t reg) const {
    if (reg >= kRegisters) return Unknown();
    uint32_t bins = 0;
    for (uint32_t i = 0; i < binned_count_; ++i) {
      const Binned& b = binned_[i];
      if (reg >= b.start && reg - b.start < b.count) bins |= b.mask & xdk::kColourBins;
    }
    return FindForBins(reg, bins & (0u - bins));
  }

  // The tile loads, oldest first, and whether one still decides `reg` in the
  // passes its mask names (nothing later replaced it there in every pass).
  uint32_t binned_count() const { return binned_count_; }
  const Binned& binned(uint32_t i) const { return binned_[i]; }
  bool InForce(const Binned& b, uint32_t reg) const {
    return reg < kRegisters && reg >= b.start && reg - b.start < b.count && b.seq > every_[reg].seq;
  }

 private:
  struct Entry {
    ConstantSource source;
    uint64_t seq = 0;
  };
  static ConstantSource Unknown() { return ConstantSource{ConstantSource::Kind::kUnknown, 0, 0}; }

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
  // Bit 63 - g: a load has named a register of group g at some time. It only
  // grows: a bank upload of another group costs one test, and one of such a
  // group is followed whether or not a load is still in force there.
  uint64_t loaded_ = 0;
};

}  // namespace fable2::native::capture
