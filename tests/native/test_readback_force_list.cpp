// Synthetic standalone test; no game or GPU.
#include "../../src/core/readback_force_list.h"

#include <cctype>
#include <iostream>
#include <string>
#include <string_view>

using namespace fable2;

// The SDK's rule for the list (CommandProcessor::ShouldForceReadbackResolve,
// thirdparty/rexglue-sdk/src/graphics/command_processor.cpp): entries divided by commas, blanks
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

  std::cout << "PASS: forced read-back list (hero and dog, exposure: four lists under the SDK's rule)\n";
  return 0;
}
