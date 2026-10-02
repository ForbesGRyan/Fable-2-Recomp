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
  std::cout << "autoplay ok\n";
  return 0;
}
