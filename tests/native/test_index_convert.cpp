// Synthetic standalone test; no game or GPU.
#include "../../src/native/capture/index_convert.h"
#include <iostream>

using namespace fable2::native::capture;

static std::vector<uint8_t> Be16s(std::initializer_list<uint32_t> v) {
  std::vector<uint8_t> b;
  for (uint32_t x : v) { b.push_back(uint8_t(x >> 8)); b.push_back(uint8_t(x)); }
  return b;
}
static std::vector<uint8_t> Be32s(std::initializer_list<uint32_t> v) {
  std::vector<uint8_t> b;
  for (uint32_t x : v) { b.push_back(x >> 24); b.push_back(x >> 16); b.push_back(x >> 8); b.push_back(uint8_t(x)); }
  return b;
}
static bool Eq(const std::vector<uint32_t>& a, std::initializer_list<uint32_t> b) {
  return a == std::vector<uint32_t>(b);
}

int main() {
  std::vector<uint32_t> out;
  uint32_t mx = 0;
  // List, 16-bit, start offset 1.
  auto l16 = Be16s({9, 0, 1, 2, 2, 1, 3});
  if (!BuildTriangleList({l16.data(), l16.size(), false}, 1, 6, kPrimTriangleList, out, &mx)) return 1;
  if (!Eq(out, {0, 1, 2, 2, 1, 3}) || mx != 3) return 2;
  // Strip with odd-triangle winding swap and a 0xFFFF cut.
  out.clear();
  auto s16 = Be16s({0, 1, 2, 3, 0xFFFF, 4, 5, 6});
  if (!BuildTriangleList({s16.data(), s16.size(), false}, 0, 8, kPrimTriangleStrip, out, &mx)) return 3;
  if (!Eq(out, {0, 1, 2, 2, 1, 3, 4, 5, 6}) || mx != 6) return 4;
  // 32-bit strip with 0xFFFFFFFF cut.
  out.clear();
  auto s32 = Be32s({10, 11, 12, 0xFFFFFFFF, 20, 21, 22});
  if (!BuildTriangleList({s32.data(), s32.size(), true}, 0, 7, kPrimTriangleStrip, out, &mx)) return 5;
  if (!Eq(out, {10, 11, 12, 20, 21, 22}) || mx != 22) return 6;
  // Fan, sequential.
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 5, 5, kPrimTriangleFan, out, &mx)) return 7;
  if (!Eq(out, {5, 6, 7, 5, 7, 8, 5, 8, 9}) || mx != 9) return 8;
  // Quad list.
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 0, 4, kPrimQuadList, out, &mx)) return 9;
  if (!Eq(out, {0, 1, 2, 0, 2, 3})) return 10;
  // Rect list and line types are unsupported.
  if (BuildTriangleList({nullptr, 0, false}, 0, 3, kPrimRectangleList, out, &mx)) return 11;
  if (BuildTriangleList({nullptr, 0, false}, 0, 2, 2, out, &mx)) return 12;
  // Reading past the buffer fails.
  if (BuildTriangleList({l16.data(), l16.size(), false}, 2, 6, kPrimTriangleList, out, &mx)) return 13;
  // Partial trailing triangle in a list is dropped.
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 0, 5, kPrimTriangleList, out, &mx)) return 14;
  if (!Eq(out, {0, 1, 2})) return 15;
  // The smallest index of the triangles made, beside the largest: a list, a strip with a cut (the
  // cut value itself and an index no triangle uses do not count), generated indices, and none.
  uint32_t mn = 99;
  out.clear();
  auto l16b = Be16s({7, 9, 5, 12, 6, 8});   // (the smallest as a triangle's third index and as its second)
  if (!BuildTriangleList({l16b.data(), l16b.size(), false}, 0, 6, kPrimTriangleList, out, &mx, &mn)) return 16;
  if (mn != 5 || mx != 12) return 17;
  out.clear();
  auto s16b = Be16s({4, 5, 6, 0xFFFF, 1, 2, 0xFFFF, 9, 8, 7});
  if (!BuildTriangleList({s16b.data(), s16b.size(), false}, 0, 10, kPrimTriangleStrip, out, &mx, &mn)) return 18;
  if (!Eq(out, {4, 5, 6, 9, 8, 7}) || mn != 4 || mx != 9) return 19;   // (1 and 2 make no triangle)
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 30, 6, kPrimTriangleList, out, &mx, &mn)) return 20;
  if (mn != 30 || mx != 35) return 21;
  out.clear();
  mn = 99;
  if (!BuildTriangleList({nullptr, 0, false}, 30, 2, kPrimTriangleList, out, &mx, &mn)) return 22;
  if (!out.empty() || mn != 0 || mx != 0) return 23;
  // The smallest index, case by case (the final fix wave: the range check of a record's low end,
  // IndexRangeValid, is what keeps the vertex shader's unguarded loads of t0 and t2 inside their
  // buffers, and six changes of the minimum got past the cases above).
  // In a list: the smallest as a triangle's FIRST, SECOND and THIRD index, each in a LATER triangle only.
  for (int place = 0; place < 3; ++place) {
    std::initializer_list<uint32_t> lists[3] = {{9, 8, 7, 3, 12, 10}, {9, 8, 7, 12, 3, 10}, {9, 8, 7, 12, 10, 3}};
    auto data = Be16s(lists[place]);
    out.clear();
    mn = 99;
    if (!BuildTriangleList({data.data(), data.size(), false}, 0, 6, kPrimTriangleList, out, &mx, &mn)) return 24;
    if (mn != 3 || mx != 12) return 25 + place;  // 25, 26, 27
  }
  // A quad's second triangle (a, c, d): the fourth index is in no other triangle. And the largest there.
  {
    auto q = Be16s({5, 6, 7, 2, 10, 11, 12, 40});
    out.clear();
    if (!BuildTriangleList({q.data(), q.size(), false}, 0, 8, kPrimQuadList, out, &mx, &mn)) return 28;
    if (!Eq(out, {5, 6, 7, 5, 7, 2, 10, 11, 12, 10, 12, 40}) || mn != 2 || mx != 40) return 29;
    // (the second quad's fourth index the smallest)
    auto q2 = Be16s({5, 6, 7, 8, 10, 11, 12, 1});
    out.clear();
    if (!BuildTriangleList({q2.data(), q2.size(), false}, 0, 8, kPrimQuadList, out, &mx, &mn)) return 30;
    if (mn != 1 || mx != 12) return 31;
    // (a quad's SECOND index is in its first triangle only)
    auto q3 = Be16s({5, 1, 7, 8});
    out.clear();
    if (!BuildTriangleList({q3.data(), q3.size(), false}, 0, 4, kPrimQuadList, out, &mx, &mn)) return 50;
    if (!Eq(out, {5, 1, 7, 5, 7, 8}) || mn != 1 || mx != 8) return 51;
  }
  // A fan: the smallest as the fan's first index, and as the last index of its last triangle; a fan
  // after a cut starts anew (its first index is the new run's).
  {
    auto f = Be16s({2, 8, 7, 9});
    out.clear();
    if (!BuildTriangleList({f.data(), f.size(), false}, 0, 4, kPrimTriangleFan, out, &mx, &mn)) return 32;
    if (!Eq(out, {2, 8, 7, 2, 7, 9}) || mn != 2 || mx != 9) return 33;
    auto f2 = Be16s({9, 8, 7, 3});
    out.clear();
    if (!BuildTriangleList({f2.data(), f2.size(), false}, 0, 4, kPrimTriangleFan, out, &mx, &mn)) return 34;
    if (!Eq(out, {9, 8, 7, 9, 7, 3}) || mn != 3 || mx != 9) return 35;
    auto f3 = Be16s({20, 21, 22, 0xFFFF, 6, 30, 31});
    out.clear();
    if (!BuildTriangleList({f3.data(), f3.size(), false}, 0, 7, kPrimTriangleFan, out, &mx, &mn)) return 36;
    if (!Eq(out, {20, 21, 22, 6, 30, 31}) || mn != 6 || mx != 31) return 37;
  }
  // A strip: the smallest in an odd triangle (the swapped winding), as that triangle's new index.
  {
    auto st = Be16s({9, 8, 7, 4, 10});
    out.clear();
    if (!BuildTriangleList({st.data(), st.size(), false}, 0, 5, kPrimTriangleStrip, out, &mx, &mn)) return 38;
    if (!Eq(out, {9, 8, 7, 7, 8, 4, 7, 4, 10}) || mn != 4 || mx != 10) return 39;
    // (a strip that ends in an odd triangle: its new index is in no other)
    auto st2 = Be16s({9, 8, 7, 4});
    out.clear();
    if (!BuildTriangleList({st2.data(), st2.size(), false}, 0, 4, kPrimTriangleStrip, out, &mx, &mn)) return 52;
    if (!Eq(out, {9, 8, 7, 7, 8, 4}) || mn != 4 || mx != 9) return 53;
  }
  // 32-bit indices are whole: above 65535 the smallest is not its low half, and a restart value of
  // the 16-bit kind is an index.
  {
    auto w = Be32s({0x10005, 0x10006, 0x10007, 0x2FFFF, 0x10009, 0x1000A});
    out.clear();
    if (!BuildTriangleList({w.data(), w.size(), true}, 0, 6, kPrimTriangleList, out, &mx, &mn)) return 40;
    if (mn != 0x10005 || mx != 0x2FFFF) return 41;
    auto ws = Be32s({0x10005, 0xFFFF, 0x10007});
    out.clear();
    if (!BuildTriangleList({ws.data(), ws.size(), true}, 0, 3, kPrimTriangleStrip, out, &mx, &mn)) return 42;
    if (!Eq(out, {0x10005, 0xFFFF, 0x10007}) || mn != 0xFFFF || mx != 0x10007) return 43;
  }
  // One triangle of one vertex: the smallest equals the largest and is that index, not 0.
  {
    auto one = Be16s({4, 4, 4});
    out.clear();
    mn = 99;
    if (!BuildTriangleList({one.data(), one.size(), false}, 0, 3, kPrimTriangleList, out, &mx, &mn)) return 44;
    if (mn != 4 || mx != 4) return 45;
  }
  // Generated indices of a quad list and of a fan from a start: the start is the smallest.
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 12, 8, kPrimQuadList, out, &mx, &mn)) return 46;
  if (mn != 12 || mx != 19) return 47;
  out.clear();
  if (!BuildTriangleList({nullptr, 0, false}, 12, 5, kPrimTriangleFan, out, &mx, &mn)) return 48;
  if (mn != 12 || mx != 16) return 49;
  std::cout << "PASS: index convert\n";
  return 0;
}
