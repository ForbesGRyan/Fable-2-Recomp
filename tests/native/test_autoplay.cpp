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
  {  // view and discovery actions
    auto v = Parse("1:VIEW=Off,2:view=SPLIT,3:View=overlay,4:VIEW=nAtIvE,5:VIEW=pattern", &err);
    if (!v || v->size() != 5) return 74;
    const uint8_t want[5] = {0, 1, 2, 3, 4};
    const char* names[5] = {"VIEW=off", "VIEW=split", "VIEW=overlay", "VIEW=native", "VIEW=pattern"};
    for (int i = 0; i < 5; ++i) {
      const Step& st = (*v)[i];
      if (st.action != 1 || st.view != want[i] || st.name != names[i] || st.buttons != 0 ||
          st.stick != 0) return 75;
    }
    auto d = Parse("5:disc=120/4", &err);
    if (!d || d->size() != 1) return 76;
    if ((*d)[0].action != 2 || (*d)[0].disc_frames != 120 || (*d)[0].disc_every != 4 ||
        (*d)[0].name != "DISC=120/4") return 77;
    auto lim = Parse("1:DISC=1/1,2:DISC=100000/64", &err);
    if (!lim || (*lim)[1].disc_frames != 100000 || (*lim)[1].disc_every != 64) return 78;
  }
  {  // a mixed plan stays in time order; actions press nothing and move no stick
    auto m = Parse("60:VIEW=native,23:A,50:LS=0/100/4000,66:DISC=60/4,52:VIEW=off,30:Down", &err);
    if (!m || m->size() != 6) return 79;
    const double order[6] = {23, 30, 50, 52, 60, 66};
    for (int i = 0; i < 6; ++i)
      if ((*m)[i].t_seconds != order[i]) return 80;
    if ((*m)[3].action != 1 || (*m)[4].action != 1 || (*m)[5].action != 2) return 81;
    if (ButtonsAt(*m, 52.0, 1.0) != 0 || ButtonsAt(*m, 60.0, 1.0) != 0 ||
        ButtonsAt(*m, 66.0, 1.0) != 0) return 82;
    if (SticksAt(*m, 60.0).ly != 0 || SticksAt(*m, 66.0).lx != 0) return 83;
    if (SticksAt(*m, 52.0).ly != 32767) return 84;  // the stick entry still holds at 52 s
    auto tail = Parse("10:A,20:VIEW=native", &err);
    if (EndTime(*tail, 0.1) != 20.0) return 85;     // covers a trailing action
    auto only = Parse("90:DISC=10/1", &err);
    if (EndTime(*only, 0.1) != 90.0) return 86;
  }
  {  // ActionsBetween: each action exactly once across consecutive 5 ms polls
    auto m = *Parse("1:A,1.0:VIEW=native,1.0:DISC=5/2,1.5:VIEW=off,2:LS=0/10/100,0.005:VIEW=split", &err);
    // 0.005 is exactly t1 of the first poll (0, 0.005]; 1.0 twice (plan order); 1.5.
    std::vector<std::string> seen;
    double last = 0;
    for (int i = 1; i <= 500; ++i) {  // 5 ms steps up to 2.5 s
      const double t = i * 0.005;
      for (const Step* s : ActionsBetween(m, last, t)) seen.push_back(s->name);
      last = t;
    }
    if (seen.size() != 4) return 87;
    if (seen[0] != "VIEW=split" || seen[1] != "VIEW=native" || seen[2] != "DISC=5/2" ||
        seen[3] != "VIEW=off") return 88;
    // boundaries: t1 inclusive, t0 exclusive
    auto b = *Parse("10:VIEW=off,10:VIEW=native", &err);
    if (ActionsBetween(b, 9.0, 10.0).size() != 2) return 89;
    if (!ActionsBetween(b, 10.0, 11.0).empty()) return 90;
    if (!ActionsBetween(b, 9.0, 9.999).empty()) return 91;
    const auto both = ActionsBetween(b, 0.0, 20.0);
    if (both.size() != 2 || both[0]->view != 0 || both[1]->view != 3) return 92;  // plan order
    if (!ActionsBetween(*Parse("10:A,11:LS=1/1/100", &err), 0, 20).empty()) return 93;
    if (!ActionsBetween({}, 0, 20).empty()) return 94;
  }
  for (const char* bad : {"5:VIEW=", "5:VIEW=sideways", "5:VIEW=native/1", "5:DISC=120", "5:DISC=0/4",
                          "5:DISC=120/0", "5:DISC=120/65", "5:DISC=100001/4", "5:DISC=a/4",
                          "5:DISC=120/4/1", "5:DISC=", "5:DISC=/4", "5:DISC=120/", "5:VIEWS=off",
                          "5:VIEW"}) {
    if (Parse(bad, &err)) return 95;
    if (Parse(std::string("10:A,") + bad + ",20:B", &err)) return 96;  // the whole plan
  }
  std::cout << "autoplay ok\n";
  return 0;
}
