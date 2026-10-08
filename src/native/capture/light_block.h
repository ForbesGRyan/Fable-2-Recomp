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
// The model says something only while it is whole: every upload since it was
// last made whole was handed to it. The capture follows uploads only while
// somebody reads the result (records or discovery); for an upload it does not
// follow it calls Skip, and from then on no register is known. Observe tells
// the model whether the main scene's bracket is open; at the first begin of a
// scene with every upload since the last observation followed, the model is
// whole again, knowing nothing: each register is unknown until an upload of
// it is followed (the bank's group, or a load).
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
  // room for are not followed: the model stops being whole (Skip). The game's
  // callers pass constants inside the bank (frame-map section 14, "The path").
  bool Load(uint32_t start, uint32_t count, uint32_t object, uint32_t first, bool predicated, uint32_t mask) {
    if (count == 0) return true;  // loads nothing
    if (start >= kRegisters || count > kRegisters - start || object == 0 || first >= kMaxBufferRegisters ||
        count > kMaxBufferRegisters - first) {
      Skip();
      return false;
    }
    if (!predicated) mask = kAllBins;
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

  // The source of `reg` as a pass with this bin select sees it: the last
  // upload it executed. Unknown while the model is not whole.
  ConstantSource FindForBins(uint32_t reg, uint32_t bin_select) const {
    if (reg >= kRegisters || !whole_) return Unknown();
    const Entry* best = &every_[reg];
    ConstantSource from = best->source;
    uint64_t seq = best->seq;
    for (uint32_t i = 0; i < binned_count_; ++i) {
      const Binned& b = binned_[i];
      if (!(b.mask & bin_select) || reg < b.start || reg - b.start >= b.count || b.seq < seq) continue;
      seq = b.seq;
      from = ConstantSource{ConstantSource::Kind::kMemory, b.object, b.first + (reg - b.start), b.scene};
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
  static ConstantSource Unknown() { return ConstantSource{ConstantSource::Kind::kUnknown, 0, 0, 0}; }

  // Whole again, knowing nothing of what the GPU holds.
  void Resume() {
    whole_ = true;
    binned_count_ = 0;
    SetEvery(0, kRegisters, Unknown(), ++seq_);  // marks every group: its next bank upload is followed
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
