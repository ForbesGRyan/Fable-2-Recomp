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
  std::cout << "PASS: index convert\n";
  return 0;
}
