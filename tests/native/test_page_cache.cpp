#include "../../src/native/capture/page_cache.h"
#include <iostream>
using namespace fable2::native::capture;
int main() {
  PageReadCache c;
  int probes = 0;
  bool readable = true;
  auto probe = [&](uintptr_t) { ++probes; return readable; };
  if (!c.Query(0x1000, 1, probe) || probes != 1) return 1;
  readable = false;
  if (!c.Query(0x1000, 1, probe) || probes != 1) return 2;  // cached within generation
  if (c.Query(0x1000, 2, probe) || probes != 2) return 3;   // new generation re-probes
  readable = true;
  if (c.Query(0x1000, 2, probe) || probes != 2) return 4;   // unreadable cached in-gen
  if (!c.Query(0x1000, 3, probe) || probes != 3) return 5;  // recovers next generation
  for (uintptr_t p = 0; p < 70; ++p) c.Query(0x10000 + p * 4096, 3, probe);  // wrap
  if (!PhysicalRangeInWindow(0x1FFFFFF0, 16)) return 6;
  if (PhysicalRangeInWindow(0x1FFFFFF0, 17)) return 7;
  if (!PhysicalRangeInWindow(0x20000010, 0)) return 8;  // masked, size 0
  if (PhysicalRangeInWindow(0, 0xFFFFFFFFu)) return 9;
  std::cout << "PASS: page cache\n";
  return 0;
}
