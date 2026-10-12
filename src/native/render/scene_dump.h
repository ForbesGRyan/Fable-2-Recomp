#pragma once
// The stored native scene (frame-map section 18, "Stored native scene"): a diagnostic file of the
// clay pass's scene before post, written once for a discovery capture with
// --fable2_native_scene_dump=on. Pure: the file's format, the two conversions and the run's bounds;
// the pass (clay_pass.cpp) makes the copies and fable2_native_render.cpp writes the file.
//
// File "F2NS", little-endian:
//   u32 magic 'F2NS', u32 version 1, u32 width, u32 height, u64 scene_frame, u64 asked_frame,
//   u32 flags, f32 exposure, u32 bad_values, u32 reserved (0);
//   width * height * 3 f32: the linear target (R11G11B10_FLOAT decoded), rows from the top;
//   width * height u32: the scene depth's 24-bit level (0 the near plane, 16777215 the far plane
//     and the cleared depth);
//   with kSceneDumpLayer two planes of (width / 2) * (height / 2) u32: the sun layer's depth
//     before and after the tree occluders, as levels.
// Version 2 (sub-project 13, the water; frame-map section 20): A FILE IS VERSION 2 IFF IT HAS A FLAG ABOVE 4,
// and a file without one is version 1, byte for byte the file of before. After every plane of before, in
// this order:
//   with kSceneDumpPreWater width * height u32: the scene's depth taken just before the water step, as levels;
//   with kSceneDumpWater width * height * 3 f32: the linear target taken right after the water step (the
//     surface as the step wrote it, before any later record: what a replay of the surface is held to);
//   with kSceneDumpTargets u32 count (1 to kSceneDumpMaxTargets), then for each of the frame's reflection
//     targets u32 base (the slot-13 base its records name), f32 height (its water's level) and
//     256 * 256 * 4 bytes: red, green, blue, alpha, rows from the top.
// scene_frame is the frame number of the scene the Render drew (a discovery frame row's
// "scene_frame"); asked_frame the one that was asked for (they differ when the Render that served
// the request had a later scene). A value of the linear target that is not finite is written as 0
// and counted in bad_values.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
namespace fable2::native::render {
inline constexpr uint32_t kSceneDumpMagic = 0x534E3246u, kSceneDumpVersion = 1, kSceneDumpVersion2 = 2;
// kSceneDumpPost: the frame was a post frame (its exposure is the game's scale of that frame;
// otherwise the fixed one). kSceneDumpLayer: the sun layer was drawn; its two planes follow.
// kSceneDumpPrepass: the frame's shadow mask, with its depth pre-pass, was drawn.
inline constexpr uint32_t kSceneDumpPost = 1u, kSceneDumpLayer = 2u, kSceneDumpPrepass = 4u;
// The water's (version 2): kSceneDumpPreWater: the depth before the water step follows; kSceneDumpWater: the
// linear target after it; kSceneDumpTargets: the frame's reflection targets. Only a frame whose water is drawn
// has any of them.
inline constexpr uint32_t kSceneDumpPreWater = 8u, kSceneDumpTargets = 16u, kSceneDumpWater = 32u;
inline constexpr uint32_t kSceneDumpVersion2Flags = kSceneDumpPreWater | kSceneDumpTargets | kSceneDumpWater;
inline constexpr uint32_t kSceneDumpKnownFlags = kSceneDumpPost | kSceneDumpLayer | kSceneDumpPrepass | kSceneDumpVersion2Flags;
inline constexpr uint32_t kSceneDumpTargetSize = 256, kSceneDumpMaxTargets = 4;   // render::kWaterTargetSize, kMaxWaterTargets
inline constexpr uint64_t kSceneDumpTargetBytes = uint64_t(kSceneDumpTargetSize) * kSceneDumpTargetSize * 4;
// The version a file with `flags` has.
constexpr uint32_t SceneDumpVersionOf(uint32_t flags) { return (flags & kSceneDumpVersion2Flags) != 0 ? kSceneDumpVersion2 : kSceneDumpVersion; }
// The bounds: a side of the picture, and the files of one run (a request past it is refused and
// counted, "over-limit": AskSceneDump).
inline constexpr uint32_t kSceneDumpMaxSide = 8192;
inline constexpr uint32_t kSceneDumpMaxFiles = 8;
struct SceneDumpHeader {
  uint32_t magic, version, width, height;
  uint64_t scene_frame, asked_frame;
  uint32_t flags;
  float exposure;
  uint32_t bad_values, reserved;
};
static_assert(sizeof(SceneDumpHeader) == 48);

// The float target's texel (R in bits 0 to 10 and G in 11 to 21: 5 bits of exponent over 6 of
// mantissa; B in 22 to 31: 5 over 5; no sign). Exponent 0 is a denormal, 2^-14 * m / 2^bits;
// exponent 31 an infinity (mantissa 0) or no number; else 2^(e - 15) * (1 + m / 2^bits).
inline float DecodeUnsignedFloat(uint32_t bits, uint32_t mantissa_bits) {
  const uint32_t mantissa = bits & ((1u << mantissa_bits) - 1), exponent = (bits >> mantissa_bits) & 31u;
  const float fraction = float(mantissa) / float(1u << mantissa_bits);
  if (exponent == 0) return std::ldexp(fraction, -14);
  if (exponent == 31) return mantissa ? std::nanf("") : HUGE_VALF;
  return std::ldexp(1.0f + fraction, int(exponent) - 15);
}
inline void DecodeR11G11B10(uint32_t word, float out[3]) {
  out[0] = DecodeUnsignedFloat(word & 0x7FFu, 6);
  out[1] = DecodeUnsignedFloat((word >> 11) & 0x7FFu, 6);
  out[2] = DecodeUnsignedFloat(word >> 22, 5);
}

// A depth as one of 16777216 levels: the nearest to sat(d) * 16777215, a half rounded up; 0 for a
// value that is no number. In single precision exactly as kClayDepthPackPs computes it (the
// product, its whole part, one more when the rest is a half or above): `d * 16777215 + 0.5` in
// single precision would round the far plane's 16777215.5 to 16777216, which is no level.
inline uint32_t DepthLevel24(float depth) {
  if (!(depth > 0.0f)) return 0;
  const float x = (depth < 1.0f ? depth : 1.0f) * 16777215.0f;
  uint32_t level = uint32_t(x);
  if (x - float(level) >= 0.5f) ++level;
  return level < 16777215u ? level : 16777215u;
}
inline float DepthFromLevel24(uint32_t level) {
  return float(level < 16777215u ? level : 16777215u) / 16777215.0f;
}

// The bytes of a file of `width` x `height` with `flags` and `targets` reflection targets; 0 for sizes or
// flags a file cannot have (a side of 0 or above kSceneDumpMaxSide, an unknown flag, an odd side with the
// layer's planes; kSceneDumpTargets without 1 to kSceneDumpMaxTargets targets, or targets without the flag).
// SceneDumpWaterOffset: where the water's part begins (the bytes of the planes of before).
inline constexpr uint64_t SceneDumpWaterOffset(uint32_t width, uint32_t height, uint32_t flags) {
  const uint64_t texels = uint64_t(width) * height;
  return sizeof(SceneDumpHeader) + texels * 16 + ((flags & kSceneDumpLayer) != 0 ? 2 * (uint64_t(width / 2) * (height / 2)) * 4 : 0);
}
inline constexpr uint64_t SceneDumpBytes(uint32_t width, uint32_t height, uint32_t flags, uint32_t targets = 0) {
  if (width == 0 || height == 0 || width > kSceneDumpMaxSide || height > kSceneDumpMaxSide) return 0;
  if ((flags & ~kSceneDumpKnownFlags) != 0) return 0;
  const bool layer = (flags & kSceneDumpLayer) != 0;
  if (layer && ((width | height) & 1u) != 0) return 0;
  const bool with_targets = (flags & kSceneDumpTargets) != 0;
  if (with_targets ? (targets == 0 || targets > kSceneDumpMaxTargets) : targets != 0) return 0;
  const uint64_t texels = uint64_t(width) * height;
  return SceneDumpWaterOffset(width, height, flags) + ((flags & kSceneDumpPreWater) != 0 ? texels * 4 : 0) +
         ((flags & kSceneDumpWater) != 0 ? texels * 12 : 0) + (with_targets ? 4 + targets * (8 + kSceneDumpTargetBytes) : 0);
}

// Where the water's part of a parsed file lies, as offsets into the file (0: the file has none of that plane),
// and its targets' heads.
struct SceneDumpWater {
  uint64_t pre_water = 0;                       // width * height u32
  uint64_t water = 0;                           // width * height * 3 f32
  uint32_t target_count = 0;
  uint32_t target_base[kSceneDumpMaxTargets] = {};
  float target_height[kSceneDumpMaxTargets] = {};
  uint64_t target[kSceneDumpMaxTargets] = {};   // kSceneDumpTargetBytes each
};

// The head of a file of `bytes` bytes, if it is one: the magic, the version ITS FLAGS ASK FOR (1 without a
// flag above 4, 2 with one: a version-1 head with such a flag and a version-2 head without one are no files),
// sizes and flags a file can have, the reserved word 0, a targets' count of 1 to kSceneDumpMaxTargets where
// the flag is set, and exactly the bytes its head (and that count) ask for. `water` (optional): where the
// water's part lies; all zero for a version-1 file. Nothing is written on a refusal.
inline bool ParseSceneDump(const uint8_t* file, size_t bytes, SceneDumpHeader* head, SceneDumpWater* water = nullptr) {
  if (!file || !head || bytes < sizeof(SceneDumpHeader)) return false;
  SceneDumpHeader h;
  std::memcpy(&h, file, sizeof(h));
  if (h.magic != kSceneDumpMagic || h.reserved != 0) return false;
  if (h.width == 0 || h.height == 0 || h.width > kSceneDumpMaxSide || h.height > kSceneDumpMaxSide || (h.flags & ~kSceneDumpKnownFlags) != 0) return false;
  if (h.version != SceneDumpVersionOf(h.flags)) return false;
  SceneDumpWater w;
  const uint64_t texels = uint64_t(h.width) * h.height;
  uint64_t at = SceneDumpWaterOffset(h.width, h.height, h.flags);
  if ((h.flags & kSceneDumpPreWater) != 0) {
    w.pre_water = at;
    at += texels * 4;
  }
  if ((h.flags & kSceneDumpWater) != 0) {
    w.water = at;
    at += texels * 12;
  }
  if ((h.flags & kSceneDumpTargets) != 0) {
    if (at > bytes || bytes - at < 4) return false;
    std::memcpy(&w.target_count, file + at, 4);
    if (w.target_count == 0 || w.target_count > kSceneDumpMaxTargets) return false;
  }
  const uint64_t want = SceneDumpBytes(h.width, h.height, h.flags, w.target_count);
  if (want == 0 || want != bytes) return false;
  for (uint32_t k = 0; k < w.target_count; ++k) {
    const uint64_t one = at + 4 + k * (8 + kSceneDumpTargetBytes);
    std::memcpy(&w.target_base[k], file + one, 4);
    std::memcpy(&w.target_height[k], file + one + 4, 4);
    w.target[k] = one + 8;
  }
  *head = h;
  if (water) *water = w;
  return true;
}

// The read-back rows a file is made of: the linear target's words (four bytes a texel, rows of
// `linear_pitch` bytes), the depth step's target (R8G8B8A8: a level's low, middle and high byte in
// r, g, b; rows of `depth_pitch` bytes) and, for a file with the layer's planes, the two layer
// targets of half the size (rows of `layer_pitch` bytes).
struct SceneDumpPlanes {
  const uint8_t* linear = nullptr;
  uint32_t linear_pitch = 0;
  const uint8_t* depth = nullptr;
  uint32_t depth_pitch = 0;
  const uint8_t* layer[2] = {};
  uint32_t layer_pitch = 0;
  // The water's (version 2; each read only with its flag): the depth step's target of the depth before the water
  // step (as `depth`); the linear target's words after it (as `linear`); and the frame's reflection targets,
  // R8G8B8A8 rows of `target_pitch` bytes, with their bases and heights.
  const uint8_t* pre_water = nullptr;
  uint32_t pre_water_pitch = 0;
  const uint8_t* water = nullptr;
  uint32_t water_pitch = 0;
  uint32_t target_count = 0;
  const uint8_t* target[kSceneDumpMaxTargets] = {};
  uint32_t target_pitch = 0;
  uint32_t target_base[kSceneDumpMaxTargets] = {};
  float target_height[kSceneDumpMaxTargets] = {};
};
// The file. False, with `file` empty, for sizes or flags a file cannot have, a plane that is
// missing or a pitch shorter than a row: a file is whole or not written. Only the picture's bytes
// of a row are read. A value of the linear target that is not finite is written as 0 and counted;
// an exposure that is not finite is written as 0.
inline bool PackSceneDump(uint32_t width, uint32_t height, uint64_t scene_frame, uint64_t asked_frame, uint32_t flags, float exposure,
                          const SceneDumpPlanes& planes, std::vector<uint8_t>* file) {
  if (!file) return false;
  file->clear();
  const bool with_targets = (flags & kSceneDumpTargets) != 0;
  const uint64_t bytes = SceneDumpBytes(width, height, flags, with_targets ? planes.target_count : 0);
  const bool layer = (flags & kSceneDumpLayer) != 0;
  if (bytes == 0 || !planes.linear || !planes.depth || planes.linear_pitch / 4 < width || planes.depth_pitch / 4 < width) return false;
  if (layer && (!planes.layer[0] || !planes.layer[1] || planes.layer_pitch / 4 < width / 2)) return false;
  // (the water's planes: each asked for by its flag is there, whole; targets without their flag are no file)
  if (!with_targets && planes.target_count != 0) return false;
  if ((flags & kSceneDumpPreWater) != 0 && (!planes.pre_water || planes.pre_water_pitch / 4 < width)) return false;
  if ((flags & kSceneDumpWater) != 0 && (!planes.water || planes.water_pitch / 4 < width)) return false;
  if (with_targets) {
    if (planes.target_pitch / 4 < kSceneDumpTargetSize) return false;
    for (uint32_t k = 0; k < planes.target_count; ++k) {
      if (!planes.target[k]) return false;
    }
  }
  file->resize(size_t(bytes));
  uint8_t* out = file->data() + sizeof(SceneDumpHeader);
  uint32_t bad = 0;
  auto floats = [&](const uint8_t* rows, uint32_t pitch) {
    for (uint32_t y = 0; y < height; ++y) {
      const uint8_t* row = rows + size_t(y) * pitch;
      for (uint32_t x = 0; x < width; ++x) {
        uint32_t word;
        std::memcpy(&word, row + 4 * size_t(x), 4);
        float c[3];
        DecodeR11G11B10(word, c);
        for (float& v : c) {
          if (!std::isfinite(v)) {
            v = 0.0f;
            ++bad;
          }
        }
        std::memcpy(out, c, 12);
        out += 12;
      }
    }
  };
  floats(planes.linear, planes.linear_pitch);
  auto levels = [&](const uint8_t* rows, uint32_t pitch, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; ++y) {
      const uint8_t* row = rows + size_t(y) * pitch;
      for (uint32_t x = 0; x < w; ++x) {
        const uint8_t* t = row + 4 * size_t(x);
        const uint32_t level = uint32_t(t[0]) | uint32_t(t[1]) << 8 | uint32_t(t[2]) << 16;
        std::memcpy(out, &level, 4);
        out += 4;
      }
    }
  };
  levels(planes.depth, planes.depth_pitch, width, height);
  if (layer) {
    levels(planes.layer[0], planes.layer_pitch, width / 2, height / 2);
    levels(planes.layer[1], planes.layer_pitch, width / 2, height / 2);
  }
  if ((flags & kSceneDumpPreWater) != 0) levels(planes.pre_water, planes.pre_water_pitch, width, height);
  if ((flags & kSceneDumpWater) != 0) floats(planes.water, planes.water_pitch);   // (a value that is no number: 0, counted with the scene's)
  if (with_targets) {
    std::memcpy(out, &planes.target_count, 4);
    out += 4;
    for (uint32_t k = 0; k < planes.target_count; ++k) {
      std::memcpy(out, &planes.target_base[k], 4);
      std::memcpy(out + 4, &planes.target_height[k], 4);
      out += 8;
      for (uint32_t y = 0; y < kSceneDumpTargetSize; ++y) {
        std::memcpy(out, planes.target[k] + size_t(y) * planes.target_pitch, size_t(kSceneDumpTargetSize) * 4);
        out += size_t(kSceneDumpTargetSize) * 4;
      }
    }
  }
  SceneDumpHeader head{kSceneDumpMagic, SceneDumpVersionOf(flags), width, height, scene_frame, asked_frame, flags,
                       std::isfinite(exposure) ? exposure : 0.0f, bad, 0};
  std::memcpy(file->data(), &head, sizeof(head));
  return true;
}

// The run's bounds and what became of its requests, for the log. A request is refused past
// kSceneDumpMaxFiles taken ones ("over-limit"); one that comes while another still waits for its
// Render replaces it ("replaced"). A taken request that gives no file is counted by its reason:
// the dump's objects could not be made ("objects"), a read-back buffer could not be made
// ("buffer") or read ("read"), or the pass was rebuilt while the copy was pending ("lost").
enum class SceneDumpFault : uint8_t { kObjects, kBuffer, kRead, kOverLimit, kReplaced, kLost, kCount };
inline const char* SceneDumpFaultName(SceneDumpFault fault) {
  switch (fault) {
    case SceneDumpFault::kObjects: return "objects";
    case SceneDumpFault::kBuffer: return "buffer";
    case SceneDumpFault::kRead: return "read";
    case SceneDumpFault::kOverLimit: return "over-limit";
    case SceneDumpFault::kReplaced: return "replaced";
    case SceneDumpFault::kLost: return "lost";
    default: return "?";
  }
}
struct SceneDumpCounts {
  uint32_t asked = 0;       // requests taken (at most kSceneDumpMaxFiles)
  uint32_t written = 0;     // files handed out (TakeSceneDump)
  uint32_t replaced = 0, over_limit = 0, objects = 0, buffer = 0, read = 0, lost = 0;
};
inline void CountSceneDumpFault(SceneDumpFault fault, SceneDumpCounts& counts) {
  switch (fault) {
    case SceneDumpFault::kObjects: ++counts.objects; break;
    case SceneDumpFault::kBuffer: ++counts.buffer; break;
    case SceneDumpFault::kRead: ++counts.read; break;
    case SceneDumpFault::kOverLimit: ++counts.over_limit; break;
    case SceneDumpFault::kReplaced: ++counts.replaced; break;
    case SceneDumpFault::kLost: ++counts.lost; break;
    default: break;
  }
}
enum class SceneDumpAsk : uint8_t { kTaken, kNoFrame, kOverLimit };
// A request for the scene of `frame` (0 asks nothing); `waiting`: an earlier request has not yet
// met its Render.
inline SceneDumpAsk AskSceneDump(uint64_t frame, bool waiting, SceneDumpCounts& counts) {
  if (frame == 0) return SceneDumpAsk::kNoFrame;
  if (counts.asked >= kSceneDumpMaxFiles) {
    ++counts.over_limit;
    return SceneDumpAsk::kOverLimit;
  }
  ++counts.asked;
  if (waiting) ++counts.replaced;
  return SceneDumpAsk::kTaken;
}
// Whether a Render of the scene `scene_frame` serves a request for `asked`: the scene asked for or
// a later one (the file's head then names both), never an earlier one.
inline bool SceneDumpDue(uint64_t asked, uint64_t scene_frame) { return asked != 0 && scene_frame >= asked; }
// Whether the caller asks anew: the capture's first scene frame is known (not 0) and is not the one
// the last request was made for (each capture asks once).
inline bool SceneDumpNewRequest(uint64_t first_scene_frame, uint64_t last_asked) {
  return first_scene_frame != 0 && first_scene_frame != last_asked;
}

// The impostors' pictures the pass used in a dumped frame (sub-project 12, plan Task 14; spec criterion 6):
// native_scene_<stamp>.f2np beside the scene's file. A head, then for each record the step drew with a picture
// its head and the picture's base level as it was uploaded: 256 x 256 RGBA8, the TEXTURE's channels as
// Decode1555Level gives them (the fetch's swizzle is the view's, not in these bytes). `base`: the picture's
// address (the record's tf0 base); `place`: the record's place in the frame (ImpostorRecord::place); `hash`:
// ImpostorPictureHash of the bytes the picture was decoded from (every byte of the base). The file's head has a
// flags word: bit 0 (kImpostorPicturesFromFeed) says the bytes came from the diagnostic feed's file
// (fable2_native_impostor_feed), NOT from the running game's guest memory (ledger ruling 47: such a file reads
// 100% against the feed's own capture and must not be taken for the game's pictures); no other bit has a
// meaning, and a file with one is refused. tools/xdk_sigmatch/impostor_pictures.py `used` holds each picture
// against the paired capture's dump of the same base and prints the flag.
inline constexpr uint32_t kImpostorPicturesMagic = 0x504E3246u, kImpostorPicturesVersion = 1;   // "F2NP"
inline constexpr uint32_t kImpostorPicturesFromFeed = 1u;                                       // ImpostorPicturesHeader::flags
inline constexpr uint32_t kImpostorPicturesMax = 32;                                            // capture::kMaxImpostorRecords
inline constexpr size_t kImpostorPictureBytes = size_t(256) * 256 * 4;
struct ImpostorPicturesHeader {
  uint32_t magic, version;
  uint64_t scene_frame;
  uint32_t count, flags;
};
static_assert(sizeof(ImpostorPicturesHeader) == 24);
struct ImpostorPictureHead {
  uint32_t base, place;
  uint64_t hash;
};
static_assert(sizeof(ImpostorPictureHead) == 16);
struct ImpostorPictureEntry {
  ImpostorPictureHead head;
  const uint8_t* rgba = nullptr;   // kImpostorPictureBytes bytes
};
// The file. False, with `file` empty, for more than kImpostorPicturesMax pictures or one without bytes: a
// file is whole or not written. No picture at all is a file (a frame whose step drew none). `from_feed`: the
// pictures' bytes were the diagnostic feed's (the head's flag); without it the file of before.
inline bool PackImpostorPictures(uint64_t scene_frame, const ImpostorPictureEntry* entries, uint32_t count, std::vector<uint8_t>* file,
                                 bool from_feed = false) {
  if (!file) return false;
  file->clear();
  if (count > kImpostorPicturesMax || (count && !entries)) return false;
  for (uint32_t i = 0; i < count; ++i) {
    if (!entries[i].rgba) return false;
  }
  file->resize(sizeof(ImpostorPicturesHeader) + size_t(count) * (sizeof(ImpostorPictureHead) + kImpostorPictureBytes));
  const ImpostorPicturesHeader head{kImpostorPicturesMagic, kImpostorPicturesVersion, scene_frame, count, from_feed ? kImpostorPicturesFromFeed : 0u};
  uint8_t* out = file->data();
  std::memcpy(out, &head, sizeof(head));
  out += sizeof(head);
  for (uint32_t i = 0; i < count; ++i) {
    std::memcpy(out, &entries[i].head, sizeof(ImpostorPictureHead));
    out += sizeof(ImpostorPictureHead);
    std::memcpy(out, entries[i].rgba, kImpostorPictureBytes);
    out += kImpostorPictureBytes;
  }
  return true;
}
// A file's head, its pictures' heads and where each picture's bytes lie in `file`. False, with both lists
// empty, unless the magic, the version, a count of at most kImpostorPicturesMax, no flag but
// kImpostorPicturesFromFeed and exactly the bytes the count asks for.
inline bool ParseImpostorPictures(const uint8_t* file, size_t bytes, ImpostorPicturesHeader* head, std::vector<ImpostorPictureHead>* heads,
                                  std::vector<const uint8_t*>* pictures) {
  if (heads) heads->clear();
  if (pictures) pictures->clear();
  if (!file || !head || !heads || !pictures || bytes < sizeof(ImpostorPicturesHeader)) return false;
  ImpostorPicturesHeader h;
  std::memcpy(&h, file, sizeof(h));
  if (h.magic != kImpostorPicturesMagic || h.version != kImpostorPicturesVersion || (h.flags & ~kImpostorPicturesFromFeed) != 0 || h.count > kImpostorPicturesMax) return false;
  if (bytes != sizeof(ImpostorPicturesHeader) + size_t(h.count) * (sizeof(ImpostorPictureHead) + kImpostorPictureBytes)) return false;
  const uint8_t* at = file + sizeof(ImpostorPicturesHeader);
  for (uint32_t i = 0; i < h.count; ++i) {
    ImpostorPictureHead one;
    std::memcpy(&one, at, sizeof(one));
    heads->push_back(one);
    pictures->push_back(at + sizeof(one));
    at += sizeof(one) + kImpostorPictureBytes;
  }
  *head = h;
  return true;
}
}  // namespace fable2::native::render
