#pragma once

// Bounds-checked guest memory reads for the capture layer. The guest can hand
// us stale or garbage pointers, so every range is checked against the guest
// heap page tables (committed and readable) before the host pointer is
// returned. The heaps commit host memory together with their page tables, so
// a readable guest page is a readable host page. (VirtualQuery on the guest
// arena's file-mapped views was the first check; it costs about 0.5 ms per call
// there, which made per-draw capture run at 2-3 fps.)

#include <algorithm>
#include <atomic>
#include <cstdint>

#include <rex/system/xmemory.h>

#include "page_cache.h"

namespace fable2::native::capture {

inline rex::memory::Memory*& GuestMemory() {
  static rex::memory::Memory* memory = nullptr;
  return memory;
}

namespace detail {
// Bumped once per guest frame by capture::OnSwap(); the per-thread region
// caches are per-frame and clear themselves when this changes.
inline std::atomic<uint32_t>& CacheGeneration() {
  static std::atomic<uint32_t> generation{1};
  return generation;
}

// The heap region (page-table attributes) containing guest address `a`.
inline ReadRegion ProbeHeapRegion(rex::memory::BaseHeap* heap, uint64_t a) {
  const uint64_t heap_end = uint64_t(heap->heap_base()) + heap->heap_size();
  if (a < heap->heap_base() || a >= heap_end) return {};
  const uint32_t page = uint32_t(a) & ~(heap->page_size() - 1);
  rex::memory::HeapAllocationInfo info{};
  if (!heap->QueryRegionInfo(page, &info) || info.region_size == 0) return {};
  ReadRegion r;
  r.begin = page;
  r.end = std::min<uint64_t>(uint64_t(page) + info.region_size, heap_end);
  r.readable = (info.state & rex::memory::kMemoryAllocationCommit) &&
               (info.protect & rex::memory::kMemoryProtectRead);
  return r;
}

inline bool HeapRangeReadable(rex::memory::BaseHeap* heap, RegionReadCache& cache, uint32_t addr,
                              uint32_t size) {
  if (!heap) return false;
  return cache.RangeReadable(addr, size, CacheGeneration().load(std::memory_order_relaxed),
                             [heap](uint64_t a) { return ProbeHeapRegion(heap, a); });
}
}  // namespace detail

// Host pointer for [guest_virtual, +size) or nullptr if any page is not
// committed and readable in its guest heap.
inline const uint8_t* ReadVirtual(uint32_t guest_virtual, uint32_t size) {
  rex::memory::Memory* memory = GuestMemory();
  if (!memory || uint64_t(guest_virtual) + size > 0x100000000ull) return nullptr;
  thread_local RegionReadCache cache;
  // One heap per call: a range that leaves the heap fails its region probe.
  if (!detail::HeapRangeReadable(memory->LookupHeap(guest_virtual), cache, guest_virtual, size)) {
    return nullptr;
  }
  return memory->TranslateVirtual<const uint8_t*>(guest_virtual);
}

inline const uint8_t* ReadPhysical(uint32_t guest_physical, uint32_t size) {
  rex::memory::Memory* memory = GuestMemory();
  if (!memory || !PhysicalRangeInWindow(guest_physical, size)) return nullptr;
  thread_local RegionReadCache cache;
  const uint32_t addr = guest_physical & 0x1FFFFFFF;
  if (!detail::HeapRangeReadable(memory->GetPhysicalHeap(), cache, addr, size)) return nullptr;
  return memory->TranslatePhysical<const uint8_t*>(addr);
}

inline uint32_t LoadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

}  // namespace fable2::native::capture
