// Synthetic standalone test; no game or GPU.
#include "../../src/input/autoplay.h"
#include <iostream>
#include <string>

using namespace fable2::autoplay;

static bool Fails(const char* spec, const char* token) {
  std::string err;
  auto r = Parse(spec, &err);
  if (r.has_value() || err != token) {
    std::cerr << "spec '" << spec << "' error='" << err << "'\n";
    return true;
  }
  return false;
}

int main() {
  std::string err;
  auto p = Parse(" 27:down , 23:A,35:a, 30.5:Lb ,", &err);
  if (!p || p->size() != 4) return 1;
  if ((*p)[0].t_seconds != 23.0 || (*p)[0].buttons != 0x1000 || (*p)[0].name != "A") return 2;
  if ((*p)[1].t_seconds != 27.0 || (*p)[1].buttons != 0x0002 || (*p)[1].name != "Down") return 3;
  if ((*p)[2].t_seconds != 30.5 || (*p)[2].buttons != 0x0100 || (*p)[2].name != "LB") return 4;
  if ((*p)[3].t_seconds != 35.0) return 5;
  auto e = Parse("", &err);
  if (!e || !e->empty()) return 6;

  struct { const char* n; uint16_t b; } all[] = {
      {"Up", 1}, {"Down", 2}, {"Left", 4}, {"Right", 8}, {"Start", 0x10},
      {"Back", 0x20}, {"L3", 0x40}, {"R3", 0x80}, {"LB", 0x100}, {"RB", 0x200},
      {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000}, {"Y", 0x8000}};
  for (auto& a : all) {
    auto q = Parse(std::string("1:") + a.n, &err);
    if (!q || q->size() != 1 || (*q)[0].buttons != a.b) return 7;
  }

  if (Fails("1:Zed", "1:Zed")) return 8;
  if (Fails("23:A,x:A", "x:A")) return 9;
  if (Fails("23:A,-1:A", "-1:A")) return 10;
  if (Fails("23:A,27", "27")) return 11;

  const double hold = 0.1;
  auto s = *Parse("10:A", &err);
  if (ButtonsAt(s, 9.999, hold) != 0) return 12;
  if (ButtonsAt(s, 10.0, hold) != 0x1000) return 13;
  if (ButtonsAt(s, 10.05, hold) != 0x1000) return 14;
  if (ButtonsAt(s, 10.1, hold) != 0) return 15;
  if (ButtonsAt(s, 50.0, hold) != 0) return 16;
  auto o = *Parse("10:A,10.05:Down", &err);
  if (ButtonsAt(o, 10.07, hold) != 0x1002) return 17;
  if (ButtonsAt(o, 10.12, hold) != 0x0002) return 18;
  if (EndTime(o, hold) != 10.05 + hold) return 19;
  if (EndTime({}, hold) != 0.0) return 20;
  {  // stick entries
    auto s = fable2::autoplay::Parse("50:LS=0/100/4000, 52:ls=-50/0/500,53:RS=100/-100/250,54:A", &err);
    if (!s || s->size() != 4) return 60;
    if ((*s)[0].stick != 1 || (*s)[0].x != 0 || (*s)[0].y != 100 || (*s)[0].stick_ms != 4000) return 61;
    if ((*s)[2].stick != 2 || (*s)[2].x != 100 || (*s)[2].y != -100) return 62;
    if ((*s)[3].stick != 0 || (*s)[3].buttons != 0x1000) return 63;
    using fable2::autoplay::SticksAt;
    if (SticksAt(*s, 49.999).ly != 0) return 64;                       // before
    if (SticksAt(*s, 50.0).ly != 32767 || SticksAt(*s, 50.0).lx != 0) return 65;   // start inclusive
    if (SticksAt(*s, 52.2).lx != -16383 || SticksAt(*s, 52.2).ly != 0) return 66;  // later entry wins
    if (SticksAt(*s, 52.6).ly != 32767) return 67;                     // back to the longer entry
    if (SticksAt(*s, 54.0).ly != 0) return 68;                         // end exclusive
    if (SticksAt(*s, 53.1).rx != 32767 || SticksAt(*s, 53.1).ry != -32767) return 69;
    if (fable2::autoplay::ButtonsAt(*s, 52.0, 0.1) != 0) return 70;    // stick entries press nothing
    if (fable2::autoplay::EndTime(*s, 0.1) != 54.1) return 71;         // 54 + hold > 50 + 4
    auto only = fable2::autoplay::Parse("10:LS=10/10/60000", &err);
    if (!only || fable2::autoplay::EndTime(*only, 0.1) != 70.0) return 72;
  }
  for (const char* bad : {"5:LS=0/100", "5:LS=0/100/0", "5:LS=101/0/100", "5:LS=0/-101/100",
                          "5:LS=0/100/60001", "5:LS=0/100/10/1", "5:LS=a/0/100", "5:XS=0/0/100",
                          "5:LS=", "5:LS=0//100"}) {
    if (fable2::autoplay::Parse(bad, &err)) return 73;   // the whole plan is rejected
  }
  std::cout << "autoplay ok\n";
  return 0;
}
