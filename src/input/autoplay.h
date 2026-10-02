// fable_2 - Autoplay: scripted gamepad button sequence (pure logic, std only).
//
// FABLE2_AUTOPLAY="23:A,27:Down,30:A" -> press A at 23 s, Down at 27 s, ...
// (seconds since process start). The runtime side lives in fable_2_app.h.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fable2::autoplay {

struct Step {
  double t_seconds;
  uint16_t buttons;
  std::string name;  // canonical spelling, e.g. "Down"
};

namespace detail {

struct ButtonName { const char* name; uint16_t bit; };
inline constexpr ButtonName kButtons[] = {
    {"Up", 0x0001},    {"Down", 0x0002},  {"Left", 0x0004}, {"Right", 0x0008},
    {"Start", 0x0010}, {"Back", 0x0020},  {"L3", 0x0040},   {"R3", 0x0080},
    {"LB", 0x0100},    {"RB", 0x0200},    {"A", 0x1000},    {"B", 0x2000},
    {"X", 0x4000},     {"Y", 0x8000}};

inline std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

inline bool IEquals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}

inline bool ParseSeconds(std::string_view s, double* out) {
  if (s.empty()) return false;
  bool digit = false, dot = false;
  for (char c : s) {
    if (c >= '0' && c <= '9') digit = true;
    else if (c == '.' && !dot) dot = true;
    else return false;
  }
  if (!digit) return false;
  try { *out = std::stod(std::string(s)); } catch (...) { return false; }
  return true;
}

}  // namespace detail

// Parses "23:A,27:Down". Returns nullopt and sets *error (the bad token) on failure.
inline std::optional<std::vector<Step>> Parse(std::string_view spec, std::string* error) {
  std::vector<Step> steps;
  while (true) {
    const size_t comma = spec.find(',');
    const std::string_view token = detail::Trim(spec.substr(0, comma));
    if (!token.empty()) {
      auto fail = [&] {
        if (error) *error = std::string(token);
        return std::nullopt;
      };
      const size_t colon = token.find(':');
      if (colon == std::string_view::npos) return fail();
      double t = 0;
      if (!detail::ParseSeconds(detail::Trim(token.substr(0, colon)), &t)) return fail();
      const std::string_view bname = detail::Trim(token.substr(colon + 1));
      const detail::ButtonName* found = nullptr;
      for (const auto& b : detail::kButtons)
        if (detail::IEquals(bname, b.name)) { found = &b; break; }
      if (!found) return fail();
      steps.push_back({t, found->bit, found->name});
    }
    if (comma == std::string_view::npos) break;
    spec.remove_prefix(comma + 1);
  }
  std::stable_sort(steps.begin(), steps.end(),
                   [](const Step& a, const Step& b) { return a.t_seconds < b.t_seconds; });
  return steps;
}

// Buttons held at time t: OR of steps with t_step <= t < t_step + hold.
inline uint16_t ButtonsAt(const std::vector<Step>& steps, double t, double hold_seconds) {
  uint16_t mask = 0;
  for (const Step& s : steps)
    if (s.t_seconds <= t && t < s.t_seconds + hold_seconds) mask |= s.buttons;
  return mask;
}

// Time after which no button is ever held again (last t + hold); 0 for empty.
inline double EndTime(const std::vector<Step>& steps, double hold_seconds) {
  double end = 0;
  for (const Step& s : steps) end = std::max(end, s.t_seconds + hold_seconds);
  return end;
}

}  // namespace fable2::autoplay
