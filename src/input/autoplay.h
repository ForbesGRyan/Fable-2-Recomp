// fable_2 - Autoplay: scripted gamepad button sequence (pure logic, std only).
//
// FABLE2_AUTOPLAY="23:A,27:Down,30:A" -> press A at 23 s, Down at 27 s, ...
// (seconds since process start). The runtime side lives in fable_2_app.h.
// Stick entries "55:LS=0/100/3000" / "60:RS=60/0/1500" hold a stick at x/y percent
// (positive y forward, positive x right) for the given milliseconds.
// Action entries "56:VIEW=native" (off|split|overlay|native|pattern) and "66:DISC=60/4"
// (discovery capture of 60 frames, every 4th draw) press nothing; the app's autoplay
// thread runs them (ActionsBetween) against the renderer and the capture.

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
  std::string name;  // canonical spelling, e.g. "Down" or "LS=0/100/3000"
  uint8_t stick = 0;  // 0 button step, 1 left stick, 2 right stick
  int8_t x = 0, y = 0;  // percent of full deflection, [-100, 100]
  uint32_t stick_ms = 0;  // how long the stick is held, [1, 60000]
  uint8_t action = 0;  // 0 none, 1 set the native view, 2 arm a discovery capture
  uint8_t view = 0;  // action 1: 0 off, 1 split, 2 overlay, 3 native, 4 pattern
  uint32_t disc_frames = 0;  // action 2: frames, [1, 100000]
  uint32_t disc_every = 0;  // action 2: sample every Nth draw, [1, 64]
};

// Stick axes in the driver's units (-32767..32767, positive y forward).
struct Sticks { int32_t lx = 0, ly = 0, rx = 0, ry = 0; };

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

// Decimal integer, optional leading '-' when allow_neg; digits only, at most 6 of them.
inline bool ParseInt(std::string_view s, bool allow_neg, int* out) {
  bool neg = false;
  if (allow_neg && !s.empty() && s.front() == '-') { neg = true; s.remove_prefix(1); }
  if (s.empty() || s.size() > 6) return false;
  int v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  *out = neg ? -v : v;
  return true;
}

// "LS=x/y/ms" or "RS=x/y/ms" (case-insensitive prefix); false when it is not well formed.
inline bool ParseStick(std::string_view name, Step* step) {
  if (name.size() < 3 || name[2] != '=') return false;
  uint8_t which = 0;
  if (IEquals(name.substr(0, 2), "LS")) which = 1;
  else if (IEquals(name.substr(0, 2), "RS")) which = 2;
  else return false;
  std::string_view rest = name.substr(3);
  std::string_view f[3];
  for (int i = 0; i < 3; ++i) {
    const size_t slash = rest.find('/');
    if ((i < 2) != (slash != std::string_view::npos)) return false;  // exactly three fields
    f[i] = rest.substr(0, slash);
    if (i < 2) rest.remove_prefix(slash + 1);
  }
  int x = 0, y = 0, ms = 0;
  if (!ParseInt(f[0], true, &x) || !ParseInt(f[1], true, &y) || !ParseInt(f[2], false, &ms)) return false;
  if (x < -100 || x > 100 || y < -100 || y > 100 || ms < 1 || ms > 60000) return false;
  step->buttons = 0;
  step->stick = which;
  step->x = static_cast<int8_t>(x);
  step->y = static_cast<int8_t>(y);
  step->stick_ms = static_cast<uint32_t>(ms);
  step->name = std::string(which == 1 ? "LS=" : "RS=") + std::to_string(x) + "/" +
               std::to_string(y) + "/" + std::to_string(ms);
  return true;
}

// "VIEW=name" or "DISC=frames/every" (case-insensitive prefix); false when malformed.
inline bool ParseAction(std::string_view name, Step* step) {
  const size_t eq = name.find('=');
  if (eq == std::string_view::npos) return false;
  const std::string_view key = name.substr(0, eq), val = name.substr(eq + 1);
  if (IEquals(key, "VIEW")) {
    static constexpr const char* kViews[] = {"off", "split", "overlay", "native", "pattern"};
    for (uint8_t i = 0; i < 5; ++i) {
      if (!IEquals(val, kViews[i])) continue;
      step->buttons = 0;
      step->action = 1;
      step->view = i;
      step->name = std::string("VIEW=") + kViews[i];
      return true;
    }
    return false;
  }
  if (IEquals(key, "DISC")) {
    const size_t slash = val.find('/');
    if (slash == std::string_view::npos) return false;
    int frames = 0, every = 0;
    if (!ParseInt(val.substr(0, slash), false, &frames) ||
        !ParseInt(val.substr(slash + 1), false, &every))  // a second '/' is not a digit
      return false;
    if (frames < 1 || frames > 100000 || every < 1 || every > 64) return false;
    step->buttons = 0;
    step->action = 2;
    step->disc_frames = static_cast<uint32_t>(frames);
    step->disc_every = static_cast<uint32_t>(every);
    step->name = "DISC=" + std::to_string(frames) + "/" + std::to_string(every);
    return true;
  }
  return false;
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
      if (bname.find('=') != std::string_view::npos) {
        Step st{t, 0, {}};
        const bool ok = (bname.size() >= 3 && bname[2] == '=')
                            ? detail::ParseStick(bname, &st)
                            : detail::ParseAction(bname, &st);
        if (!ok) return fail();
        steps.push_back(std::move(st));
        if (comma == std::string_view::npos) break;
        spec.remove_prefix(comma + 1);
        continue;
      }
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
    if (s.stick == 0 && s.action == 0 && s.t_seconds <= t && t < s.t_seconds + hold_seconds) mask |= s.buttons;
  return mask;
}

// Stick positions at time t. Per stick the active entry (t_step <= t < t_step + ms) with
// the latest start wins; none active means centred.
inline Sticks SticksAt(const std::vector<Step>& steps, double t_seconds) {
  Sticks out;
  const Step* best[3] = {nullptr, nullptr, nullptr};
  for (const Step& s : steps) {
    if (s.stick == 0) continue;
    if (s.t_seconds <= t_seconds && t_seconds < s.t_seconds + s.stick_ms / 1000.0 &&
        (!best[s.stick] || s.t_seconds >= best[s.stick]->t_seconds))
      best[s.stick] = &s;
  }
  auto axis = [](int percent) { return static_cast<int32_t>(percent * 32767 / 100); };
  if (best[1]) { out.lx = axis(best[1]->x); out.ly = axis(best[1]->y); }
  if (best[2]) { out.rx = axis(best[2]->x); out.ry = axis(best[2]->y); }
  return out;
}

// Time after which no button is held and no stick deflected again (last button t + hold,
// last stick t + ms); 0 for empty.
inline double EndTime(const std::vector<Step>& steps, double hold_seconds) {
  double end = 0;
  for (const Step& s : steps)
    end = std::max(end, s.t_seconds + (s.stick ? s.stick_ms / 1000.0 : s.action ? 0.0 : hold_seconds));
  return end;
}

// Action steps (view, discovery) with t0 < t_step <= t1, in plan order (time order for a
// sorted plan, plan order for equal times). A poller calling it with consecutive times
// runs each action exactly once; it starts with t0 = kBeforePlan, so that its first
// interval holds an action at 0 s (no step is earlier: Parse refuses negative times).
// Valid while `steps` is not modified.
inline constexpr double kBeforePlan = -1.0;
inline std::vector<const Step*> ActionsBetween(const std::vector<Step>& steps, double t0, double t1) {
  std::vector<const Step*> out;
  for (const Step& s : steps)
    if (s.action != 0 && t0 < s.t_seconds && s.t_seconds <= t1) out.push_back(&s);
  return out;
}

}  // namespace fable2::autoplay
