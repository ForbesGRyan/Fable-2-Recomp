#pragma once

// Bounds-checked guest memory reads for the capture layer. The guest can hand
// us stale or garbage pointers, so every range is verified with VirtualQuery
// before the host pointer is returned.

#include <atomic>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

#include <rex/system/xmemory.h>

#include "page_cache.h"

namespace fable2::native::capture {

inline rex::memory::Memory*& GuestMemory() {
  static rex::memory::Memory* memory = nullptr;
  return memory;
}

namespace detail {
// Bumped once per guest frame by capture::OnSwap(); the per-thread page cache
// is per-frame and clears itself when this changes.
inline std::atomic<uint32_t>& CacheGeneration() {
  static std::atomic<uint32_t> generation{1};
  return generation;
}

inline bool ProbeHostPage(uintptr_t page_base) {
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(reinterpret_cast<LPCVOID>(page_base), &mbi, sizeof(mbi)) == sizeof(mbi) &&
      mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & kReadable) != 0;
  }
  return false;
#else
  (void)page_base;
  return true;
#endif
}

inline bool HostPageReadable(uintptr_t page_base) {
  thread_local PageReadCache cache;
  return cache.Query(page_base, CacheGeneration().load(std::memory_order_relaxed),
                     ProbeHostPage);
}

inline const uint8_t* CheckHostRange(const uint8_t* host, uint32_t size) {
  if (!host) return nullptr;
  constexpr uintptr_t kPage = 4096;
  const uintptr_t first = reinterpret_cast<uintptr_t>(host) & ~(kPage - 1);
  const uintptr_t last =
      (reinterpret_cast<uintptr_t>(host) + (size ? size : 1) - 1) & ~(kPage - 1);
  for (uintptr_t p = first; p <= last; p += kPage) {
    if (!HostPageReadable(p)) return nullptr;
  }
  return host;
}
}  // namespace detail

// Host pointer for [guest_virtual, +size) or nullptr if any page is not committed+readable.
inline const uint8_t* ReadVirtual(uint32_t guest_virtual, uint32_t size) {
  rex::memory::Memory* memory = GuestMemory();
  if (!memory || uint64_t(guest_virtual) + size > 0x100000000ull) return nullptr;
  return detail::CheckHostRange(memory->TranslateVirtual<const uint8_t*>(guest_virtual), size);
}

inline const uint8_t* ReadPhysical(uint32_t guest_physical, uint32_t size) {
  rex::memory::Memory* memory = GuestMemory();
  if (!memory || !PhysicalRangeInWindow(guest_physical, size)) return nullptr;
  return detail::CheckHostRange(
      memory->TranslatePhysical<const uint8_t*>(guest_physical & 0x1FFFFFFF), size);
}

inline uint32_t LoadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

}  // namespace fable2::native::capture
