#include "../../src/native/capture/xdk_layout.h"
#include <iostream>
namespace xdk = fable2::native::capture::xdk;
static_assert(xdk::GpuAddress(0xE0000000u) == 0x00001000u);  // 4 KB view: +0x1000
static_assert(xdk::GpuAddress(0xDFFFF000u) == 0x1FFFF000u);  // 16 MB view: no offset
static_assert(xdk::GpuAddress(0xA0001000u) == 0x00001000u);  // 64 KB view: no offset
int main() {
  // Real evidence (frame-map section 8): VB object 0x401288C8 dword 6/7 =
  // 0xFA63AD03 / 0x10012C02, stream offset 0x7E0 -> [vbind] fc=0x1A63C4E3 0x10012422.
  const uint32_t d6 = 0xFA63AD03u, d7 = 0x10012C02u, offset = 0x7E0u;
  if (xdk::GpuAddress(d6 + offset) != 0x1A63C4E3u) return 1;
  if (d7 - offset != 0x10012422u) return 2;
  if (xdk::GpuAddress(d6 & ~3u) + offset != (0x1A63C4E3u & ~3u)) return 3;
  std::cout << "PASS: xdk layout\n";
  return 0;
}
