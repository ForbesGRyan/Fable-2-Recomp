// Synthetic standalone test; no game or GPU.
#include "../../src/core/readback_force_list.h"

#include <cctype>
#include <iostream>
#include <string>
#include <string_view>

using namespace fable2;

// The SDK's rule for the list (CommandProcessor::ShouldForceReadbackResolve,
// thirdparty/rexglue-sdk/src/graphics/command_processor.cpp, lines 203 to 274 when this was
// written; a COPY, since a pure test takes no SDK header: it does not notice a change of the
// SDK's window or parsing, so an SDK update is compared with those lines by hand): entries divided by commas, blanks
// around an entry dropped, each read by std::stoull with base 0 (0x-hex or decimal; a malformed
// entry is address 0), an entry matching [entry, entry + 64 KiB). Returns the entry that matched,
// 0 for none; *entries is the number of non-empty entries, *clean whether every one of them was
// read to its end.
static uint32_t SdkMatch(const std::string& spec, uint32_t base, int* entries = nullptr, bool* clean = nullptr) {
  constexpr uint32_t kWindow = 1u << 16;
  uint32_t matched = 0;
  if (entries) *entries = 0;
  if (clean) *clean = true;
  size_t start = 0;
  while (start < spec.size()) {
    size_t end = spec.find(',', start);
    if (end == std::string::npos) end = spec.size();
    std::string_view token = std::string_view(spec).substr(start, end - start);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.remove_prefix(1);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
    if (!token.empty()) {
      if (entries) ++*entries;
      uint64_t addr = 0;
      size_t pos = 0;
      try {
        addr = std::stoull(std::string(token), &pos, 0);
      } catch (const std::exception&) {
        addr = 0;
      }
      if (clean && (pos != token.size() || addr == 0 || addr > 0xFFFFFFFFull)) *clean = false;
      const uint32_t a = static_cast<uint32_t>(addr);
      if (!matched && base >= a && base - a < kWindow) matched = a;
    }
    start = end + 1;
  }
  return matched;
}

int main() {
  constexpr uint32_t kDog = 0x12704000u;
  constexpr uint32_t kOdd = 0x1A385000u;   // the luminance resolve of the odd frames
  constexpr uint32_t kEven = 0x1A34F000u;  // and of the even ones (frame map section 17, question 1)

  // The constants are the measured addresses, and the window is the SDK's.
  if (kHeroDogResolveBase != kDog) return 1;
  if (kLuminanceResolveBases[0] != kOdd || kLuminanceResolveBases[1] != kEven) return 2;
  if (kForceReadbackWindow != (1u << 16)) return 3;

  // The four lists, as text: the hero and dog entry is spelled as it always was.
  const std::string none = ForceReadbackList(false, false);
  const std::string dog = ForceReadbackList(true, false);
  const std::string exposure = ForceReadbackList(false, true);
  const std::string both = ForceReadbackList(true, true);
  if (none != "") return 10;
  if (dog != "0x12704000") return 11;
  if (exposure != "0x1A385000,0x1A34F000") return 12;
  if (both != "0x12704000,0x1A385000,0x1A34F000") return 13;

  // Each list under the SDK's rule: the number of entries, every entry read whole, and which
  // resolve bases it forces.
  struct Case {
    const std::string* list;
    int entries;
    bool dog, exposure;
  };
  const Case cases[] = {{&none, 0, false, false}, {&dog, 1, true, false}, {&exposure, 2, false, true}, {&both, 3, true, true}};
  int n = 0;
  for (const Case& c : cases) {
    ++n;
    int entries = -1;
    bool clean = false;
    SdkMatch(*c.list, 0, &entries, &clean);
    if (entries != c.entries || !clean) { std::cerr << "case " << n << ": entries " << entries << " clean " << clean << "\n"; return 20 + n; }
    // The hero and dog texture, whatever the exposure switch says; the entry that matches is its own.
    if (SdkMatch(*c.list, kDog) != (c.dog ? kDog : 0u)) { std::cerr << "case " << n << ": dog\n"; return 30 + n; }
    // Both luminance addresses or neither: the two are 0x36000 apart, more than a window.
    if (SdkMatch(*c.list, kOdd) != (c.exposure ? kOdd : 0u)) { std::cerr << "case " << n << ": odd\n"; return 40 + n; }
    if (SdkMatch(*c.list, kEven) != (c.exposure ? kEven : 0u)) { std::cerr << "case " << n << ": even\n"; return 50 + n; }
    // Nothing else: the dog's neighbour 128 KiB on (the SDK's comment names it), the byte before
    // each entry, the first byte after each window, the post block's buffers, the front buffer.
    for (uint32_t other : {0x12724000u, kDog - 1, kDog + 0x10000u, kOdd - 1, kOdd + 0x10000u, kEven - 1, kEven + 0x10000u,
                           0x12CFD000u, 0x12D33000u, 0x12D9F000u, 0x1ECFF000u, 0u, 0xFFFFFFFFu})
      if (SdkMatch(*c.list, other) != 0) { std::cerr << "case " << n << ": forces " << std::hex << other << "\n"; return 60 + n; }
    // The last byte of a window still belongs to its entry.
    if (SdkMatch(*c.list, kDog + 0xFFFFu) != (c.dog ? kDog : 0u)) return 70 + n;
    if (SdkMatch(*c.list, kEven + 0xFFFFu) != (c.exposure ? kEven : 0u)) return 80 + n;
  }

  // The impostors' pictures (sub-project 12, plan Task 12; the plan's returns 20 to 31 are 120 to 131 here: this
  // file's cases above already return 21 to 84). Review Focus 4: without the third input, and with it false,
  // today's lists.
  if (ForceReadbackList(true, false) != "0x12704000" || ForceReadbackList(true, false, false) != "0x12704000") return 120;
  if (ForceReadbackList(true, true, false) != "0x12704000,0x1A385000,0x1A34F000") return 121;
  if (ForceReadbackList(false, true, false) != "0x1A385000,0x1A34F000") return 122;
  if (!ForceReadbackList(false, false, false).empty() || !ForceReadbackList(false, false).empty()) return 123;
  const std::string fourteen = "0x12704000,0x1272F000,0x1275A000,0x12785000,0x127B0000,0x127DB000,0x12806000,"
                               "0x12831000,0x1285C000,0x12887000,0x128B2000,0x128DD000,0x12908000,0x12933000";
  if (ForceReadbackList(true, false, true) != fourteen) return 124;        // the hero's entry is impostor 0's: once
  if (ForceReadbackList(false, false, true) != fourteen) return 125;       // without the hero's fix the base is listed all the same
  if (ForceReadbackList(true, true, true) != "0x12704000,0x1A385000,0x1A34F000" + fourteen.substr(10)) return 126;
  if (ForceReadbackList(false, true, true) != "0x1A385000,0x1A34F000," + fourteen) return 127;
  for (uint32_t n = 0; n < kImpostorPictures; ++n) {
    const uint32_t base = ImpostorPictureBase(n), level = base + kImpostorLevelOffset;
    if (n + 1 < kImpostorPictures && ImpostorPictureBase(n + 1) - base < kForceReadbackWindow) return 128;
    for (uint32_t m = 0; m < kImpostorPictures; ++m) {
      const uint32_t entry = ImpostorPictureBase(m);
      if (level >= entry && level - entry < kForceReadbackWindow) return 129;       // a level address inside a window
    }
    for (uint32_t lum : kLuminanceResolveBases) {
      if (base >= lum ? base - lum < kForceReadbackWindow : lum - base < kForceReadbackWindow) return 130;
    }
  }
  if (ImpostorPictureBase(13) != 0x12933000u) return 131;
  // Ledger ruling 47: the pictures' bases are listed only when the switch is `on` AND the native renderer is on
  // (with the renderer off nothing reads them: about 1.8 ms a frame for no consumer). Every combination of the
  // four inputs: the list is that of before without the pictures unless both hold.
  for (int mask = 0; mask < 16; ++mask) {
    const bool dog = (mask & 1) != 0, exposure = (mask & 2) != 0, on = (mask & 4) != 0, render = (mask & 8) != 0;
    const bool listed = ImpostorPicturesInList(render, on);
    if (listed != (on && render)) return 141;
    const std::string list = ForceReadbackList(dog, exposure, listed);
    if (!listed && list != ForceReadbackList(dog, exposure)) return 142;                 // the list of before, byte for byte
    if (listed && (list.find("0x12933000") == std::string::npos || !ForceListHasImpostorPictures(list))) return 143;
    if (!listed && ForceListHasImpostorPictures(list)) return 144;
    // The seeding's log lines: nothing added with the switch off or flat (criterion 13 holds an off-run's line to
    // log 657's), whatever the renderer; with it on, which of the two happened and why.
    const std::string suffix = ForceReadbackSeedSuffix(on, render);
    if (!on && !suffix.empty()) return 145;
    if (on && render && suffix != ", fable2_native_impostors=on with fable2_native_render=true: the fourteen picture bases listed") return 146;
    if (on && !render && suffix != ", fable2_native_impostors=on without fable2_native_render: the picture bases NOT listed") return 147;
    // (a list given by hand is never seeded: its line gains a word only with the switch on)
    const std::string hand_suffix = ForceReadbackHandSuffix(on);
    if (on ? hand_suffix != ", fable2_native_impostors=on: the pictures are read back only if this list holds their fourteen bases" : !hand_suffix.empty()) return 151;
  }
  static_assert(ImpostorPicturesInList(true, true) && !ImpostorPicturesInList(true, false) && !ImpostorPicturesInList(false, true) &&
                !ImpostorPicturesInList(false, false));
  // A list given by hand (review of Tasks 10 to 14, Minor 3): the pictures are in guest memory when the list, read
  // by the SDK's rule, forces each of the fourteen bases; the header's reading of the rule against this file's copy.
  {
    const std::string hand[] = {
        "",
        fourteen,
        "0x1A385000, 0x1A34F000 ," + fourteen + ",0x1ECFF000",                          // more entries, blanks around them
        fourteen.substr(0, fourteen.size() - 11),                                       // thirteen: the last is missing
        fourteen.substr(11),                                                            // the first is missing
        "0x12704000",
        "309346304,0x1272F000,0x1275A000,0x12785000,0x127B0000,0x127DB000,0x12806000,"  // the first in decimal
        "0x12831000,0x1285C000,0x12887000,0x128B2000,0x128DD000,0x12908000,0x12933000",
        "0x12700000,0x1272F000,0x1275A000,0x12785000,0x127B0000,0x127DB000,0x12806000," // an entry 0x4000 below the first base: its window holds it
        "0x12831000,0x1285C000,0x12887000,0x128B2000,0x128DD000,0x12908000,0x12933000",
        "0x126F4000,0x1272F000,0x1275A000,0x12785000,0x127B0000,0x127DB000,0x12806000," // an entry a whole window below: it does not
        "0x12831000,0x1285C000,0x12887000,0x128B2000,0x128DD000,0x12908000,0x12933000",
        "junk," + fourteen,                                                             // a malformed entry is address 0 and harms nothing
        "0x12704000;0x1272F000",                                                        // not divided by commas: one entry, read to the semicolon
        ",,," + fourteen + ",,",
        "   , \t ," + fourteen,                                                         // entries of blanks alone are no entries (not address 0)
    };
    const bool want[] = {false, true, true, false, false, false, true, true, false, true, false, true, true};
    static_assert(sizeof(hand) / sizeof(hand[0]) == sizeof(want) / sizeof(want[0]));
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i) {
      if (ForceListHasImpostorPictures(hand[i]) != want[i]) { std::cerr << "hand list " << i << "\n"; return 148; }
      for (uint32_t n = 0; n < kImpostorPictures; ++n) {
        for (uint32_t base : {ImpostorPictureBase(n), ImpostorPictureBase(n) + 0xFFFFu, ImpostorPictureBase(n) + 0x10000u, ImpostorPictureBase(n) - 1}) {
          if (ForceListForces(hand[i], base) != (SdkMatch(hand[i], base) != 0)) { std::cerr << "hand list " << i << " base " << std::hex << base << "\n"; return 149; }
        }
      }
      for (uint32_t base : {0u, 0xFFFFu, 0x10000u, 0x1A385000u, 0x1ECFF000u, 0xFFFFFFFFu}) {
        // (SdkMatch gives the matching entry, which is 0 for a malformed one: asked for a base no entry here names but address 0's)
        const bool sdk = hand[i].find("junk") != std::string::npos && base < 0x10000u ? true : SdkMatch(hand[i], base) != 0;
        if (ForceListForces(hand[i], base) != sdk) return 150;
      }
    }
  }
  // Every combination under the SDK's rule: the entries read whole, the hero's and the exposure's resolves forced
  // exactly as without the pictures (plus the hero's base as impostor 0's), each picture's base forced by its own
  // entry, and no picture's stored levels (base + 0x20000), the byte before a base or the first byte after a window.
  for (int mask = 0; mask < 8; ++mask) {
    const bool dog = (mask & 1) != 0, exposure = (mask & 2) != 0, pictures = (mask & 4) != 0;
    const std::string list = ForceReadbackList(dog, exposure, pictures);
    int entries = -1;
    bool clean = false;
    SdkMatch(list, 0, &entries, &clean);
    const int want = (pictures ? int(kImpostorPictures) : dog ? 1 : 0) + (exposure ? 2 : 0);
    if (entries != want || !clean) { std::cerr << "mask " << mask << ": entries " << entries << "\n"; return 132; }
    if (SdkMatch(list, kDog) != (dog || pictures ? kDog : 0u)) return 133;
    if (SdkMatch(list, kOdd) != (exposure ? kOdd : 0u) || SdkMatch(list, kEven) != (exposure ? kEven : 0u)) return 134;
    for (uint32_t n = 1; n < kImpostorPictures; ++n) {
      const uint32_t base = ImpostorPictureBase(n);
      if (SdkMatch(list, base) != (pictures ? base : 0u)) return 135;
      if (SdkMatch(list, base + 0xFFFFu) != (pictures ? base : 0u)) return 136;
      if (SdkMatch(list, base - 1) != 0 || SdkMatch(list, base + 0x10000u) != 0) return 137;
    }
    for (uint32_t n = 0; n < kImpostorPictures; ++n)
      if (SdkMatch(list, ImpostorPictureBase(n) + kImpostorLevelOffset) != 0) return 138;
    for (uint32_t other : {0x12CFD000u, 0x12D33000u, 0x12D9F000u, 0x1ECFF000u, 0u, 0xFFFFFFFFu, ImpostorPictureBase(14)})
      if (SdkMatch(list, other) != 0) return 139;
  }

  // The read-back mode (review of the whole branch, I1; ledger ruling 54). The SDK's effective mode, against this
  // file's copy of CommandProcessor::GetReadbackResolveMode and ParseReadbackResolveMode (thirdparty/rexglue-sdk/
  // src/graphics/command_processor.cpp, lines 108 to 119 and 191 to 201 when this was written): the shared switch's
  // word, and the legacy --d3d12_readback_resolve only while the shared switch is at its default "none".
  {
    const auto sdk_mode = [](const std::string& word, bool legacy) {
      const ReadbackMode shared = word == "fast" ? ReadbackMode::kFast : word == "some" ? ReadbackMode::kSome : word == "full" ? ReadbackMode::kFull : ReadbackMode::kNone;
      const bool overrides = shared != ReadbackMode::kNone || word != "none";   // (HasNonDefaultValue: the value is not the default's)
      return overrides ? shared : legacy ? ReadbackMode::kFast : ReadbackMode::kNone;
    };
    for (const char* word : {"none", "fast", "some", "full", "", "FULL", "on"}) {
      for (bool legacy : {false, true}) {
        if (EffectiveReadbackMode(word, legacy) != sdk_mode(word, legacy)) { std::cerr << "mode '" << word << "' legacy " << legacy << "\n"; return 160; }
      }
    }
    static_assert(EffectiveReadbackMode("none", false) == ReadbackMode::kNone && EffectiveReadbackMode("none", true) == ReadbackMode::kFast &&
                  EffectiveReadbackMode("full", true) == ReadbackMode::kFull && EffectiveReadbackMode("some", true) == ReadbackMode::kSome &&
                  EffectiveReadbackMode("fast", false) == ReadbackMode::kFast);
    // The table: the pictures are in guest memory, each frame's own, when the mode is "none" and the list forces
    // the fourteen bases (a forced resolve waits for the GPU and copies its own bytes), or the mode is "full"
    // (every resolve does); in "fast" every picture is the previous resolve's, in "some" the first one's for the
    // whole run, whatever the list holds: not available, each by its own name.
    struct Row { ReadbackMode mode; bool list; ImpostorReadback want; bool available; const char* reason; };
    const Row rows[] = {
        {ReadbackMode::kNone, true, ImpostorReadback::kListed, true, "picture: base"},
        {ReadbackMode::kNone, false, ImpostorReadback::kNoList, false, "picture: base"},
        {ReadbackMode::kFull, true, ImpostorReadback::kFull, true, "picture: base"},
        {ReadbackMode::kFull, false, ImpostorReadback::kFull, true, "picture: base"},
        {ReadbackMode::kFast, true, ImpostorReadback::kModeFast, false, "picture: mode"},
        {ReadbackMode::kFast, false, ImpostorReadback::kModeFast, false, "picture: mode"},
        {ReadbackMode::kSome, true, ImpostorReadback::kModeSome, false, "picture: mode"},
        {ReadbackMode::kSome, false, ImpostorReadback::kModeSome, false, "picture: mode"},
    };
    for (const Row& r : rows) {
      const ImpostorReadback got = DecideImpostorReadback(r.mode, r.list);
      if (got != r.want || ImpostorReadbackAvailable(got) != r.available) { std::cerr << "mode " << int(r.mode) << " list " << r.list << "\n"; return 161; }
      // (a mode's refusal is told from a missing list: the pass counts the first "picture: mode", the second "picture: base")
      if (ImpostorReadbackByMode(got) != (std::string(r.reason) == "picture: mode" && !r.available)) return 162;
    }
    // The words of the one log line and of the install line: each names its mode, and the two refusals by mode say
    // what the far trees would have been.
    const std::string listed = ImpostorReadbackWhy(ImpostorReadback::kListed), full = ImpostorReadbackWhy(ImpostorReadback::kFull),
                      nolist = ImpostorReadbackWhy(ImpostorReadback::kNoList), fast = ImpostorReadbackWhy(ImpostorReadback::kModeFast),
                      some = ImpostorReadbackWhy(ImpostorReadback::kModeSome);
    if (listed.rfind("available: ", 0) != 0 || full.rfind("available: ", 0) != 0) return 163;
    if (nolist.rfind("NOT available: ", 0) != 0 || fast.rfind("NOT available: ", 0) != 0 || some.rfind("NOT available: ", 0) != 0) return 164;
    if (listed.find("readback_resolve none") == std::string::npos || full.find("readback_resolve full") == std::string::npos ||
        nolist.find("readback_resolve none") == std::string::npos || fast.find("readback_resolve fast") == std::string::npos ||
        some.find("readback_resolve some") == std::string::npos) return 165;
    if (fast.find("a frame old") == std::string::npos || some.find("first") == std::string::npos ||
        fast.find("picture: mode") == std::string::npos || some.find("picture: mode") == std::string::npos ||
        nolist.find("picture: base") == std::string::npos) return 166;
    // From the switches' words in one step, as the renderer asks: the mode's word, the legacy switch, the list.
    if (DecideImpostorReadback(EffectiveReadbackMode("none", false), ForceListHasImpostorPictures(fourteen)) != ImpostorReadback::kListed ||
        DecideImpostorReadback(EffectiveReadbackMode("none", true), ForceListHasImpostorPictures(fourteen)) != ImpostorReadback::kModeFast ||
        DecideImpostorReadback(EffectiveReadbackMode("some", false), ForceListHasImpostorPictures(fourteen)) != ImpostorReadback::kModeSome ||
        DecideImpostorReadback(EffectiveReadbackMode("full", false), ForceListHasImpostorPictures("")) != ImpostorReadback::kFull ||
        DecideImpostorReadback(EffectiveReadbackMode("none", false), ForceListHasImpostorPictures("0x12704000")) != ImpostorReadback::kNoList) return 167;
  }

  std::cout << "PASS: forced read-back list (hero and dog, exposure, the impostors' pictures: eight lists under the SDK's rule; the pictures "
               "only with the switch on and the native renderer on, sixteen combinations; a list given by hand; the pictures' availability "
               "by read-back mode, eight rows)\n";
  return 0;
}
