#include "../../src/native/capture/page_cache.h"
#include <iostream>
using namespace fable2::native::capture;
int main() {
  // Regions: [0x10000, 0x30000) readable, [0x30000, 0x40000) not, [0x40000, 0x80000) readable.
  int probes = 0;
  bool flip = false;  // makes [0x10000, 0x30000) unreadable when set
  auto probe = [&](uint64_t a) -> ReadRegion {
    ++probes;
    if (a >= 0x10000 && a < 0x30000) return {0x10000, 0x30000, !flip};
    if (a >= 0x30000 && a < 0x40000) return {0x30000, 0x40000, false};
    if (a >= 0x40000 && a < 0x80000) return {a & ~uint64_t(0xFFF), 0x80000, true};
    return {a, a, false};  // no progress: unreadable
  };
  RegionReadCache c;
  // One probe covers a whole region.
  if (!c.RangeReadable(0x10000, 0x20000, 1, probe) || probes != 1) return 1;
  if (!c.RangeReadable(0x18000, 0x100, 1, probe) || probes != 1) return 2;  // cached
  // A range crossing into an unreadable region fails; crossing two readable
  // regions needs one probe per region.
  if (c.RangeReadable(0x2F000, 0x2000, 1, probe) || probes != 2) return 3;
  if (!c.RangeReadable(0x40000, 0x40000, 1, probe) || probes != 3) return 4;
  if (c.RangeReadable(0x40000, 0x40001, 1, probe)) return 5;  // past the last region
  // Size 0 still needs its address readable.
  if (!c.RangeReadable(0x20000, 0, 1, probe)) return 6;
  if (c.RangeReadable(0x30000, 0, 1, probe)) return 7;
  // A probe that makes no progress is unreadable, never a loop.
  if (c.RangeReadable(0x90000, 16, 1, probe)) return 8;
  // A new generation drops the cache.
  flip = true;
  if (!c.RangeReadable(0x10000, 16, 1, probe)) return 9;  // still cached
  const int before = probes;
  if (c.RangeReadable(0x10000, 16, 2, probe) || probes != before + 1) return 10;
  // More regions than entries: wraps without losing correctness.
  for (uint64_t p = 0; p < 70; ++p) c.RangeReadable(0x40000 + p * 0x1000, 4, 2, probe);
  if (!c.RangeReadable(0x40000, 4, 2, probe)) return 11;

  if (!PhysicalRangeInWindow(0x1FFFFFF0, 16)) return 20;
  if (PhysicalRangeInWindow(0x1FFFFFF0, 17)) return 21;
  if (!PhysicalRangeInWindow(0x20000010, 0)) return 22;  // masked, size 0
  if (PhysicalRangeInWindow(0, 0xFFFFFFFFu)) return 23;
  std::cout << "PASS: page cache\n";
  return 0;
}
