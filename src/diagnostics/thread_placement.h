// thread_placement.h - experiment: place named host threads on chosen logical CPUs.
//
// FABLE2_THREAD_PLACE="GPU Commands=2;3D Engine=4;GameThread=6"
//   name=cpu pairs separated by ';'. The name is matched as a substring of the
//   thread's description (SetThreadDescription, as the SDK names its threads;
//   guest threads carry the guest's own names such as "3D Engine (F80000E8)").
//   cpu is a logical processor index (0..63). Several cpus may be given as a
//   mask with '+': "GPU Commands=2+3" pins to logical 2 and 3 (one physical
//   core with SMT).
// FABLE2_THREAD_PLACE=auto (the default when the variable is unset; "off" disables)
//   The rules are built from the processor topology: the physical cores of the
//   cache group (CCD) with the largest L3, in order, skipping the first core
//   (logical 0/1, where the interrupt and system work lands), one core each for
//   "GPU Commands", "D3D12 Submit", "3D Engine" and "GameThread". Fewer than
//   five physical cores in that group: no placement.
// FABLE2_THREAD_PLACE_MODE=affinity (default) | ideal
//   affinity: SetThreadAffinityMask (hard); ideal: SetThreadIdealProcessor (soft hint).
// FABLE2_THREAD_PLACE_AT=<guest frame> (default 200): the frame at which the
//   placement is applied once (threads are all created by then); a thread
//   created later is not placed.
//
// Why an experiment: the 9950X3D has two CCDs (one with the 96 MB V-cache) and
// SMT; the GPU-emulation thread, the guest render thread and the game thread are
// each a long serial chain that may land on sibling hyperthreads or on the
// non-cache CCD. Inert without the variable.

#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <rex/logging/macros.h>

namespace fable2::threadplace {

struct Rule {
  std::string name;
  uint64_t mask = 0;
};

inline std::vector<Rule> ParseRules(const char* spec) {
  std::vector<Rule> rules;
  std::string s = spec ? spec : "";
  size_t pos = 0;
  while (pos < s.size()) {
    size_t end = s.find(';', pos);
    if (end == std::string::npos) end = s.size();
    std::string item = s.substr(pos, end - pos);
    pos = end + 1;
    size_t eq = item.find('=');
    if (eq == std::string::npos) continue;
    Rule r;
    r.name = item.substr(0, eq);
    std::string cpus = item.substr(eq + 1);
    size_t p = 0;
    while (p < cpus.size()) {
      size_t plus = cpus.find('+', p);
      if (plus == std::string::npos) plus = cpus.size();
      int cpu = std::atoi(cpus.substr(p, plus - p).c_str());
      if (cpu >= 0 && cpu < 64) r.mask |= uint64_t(1) << cpu;
      p = plus + 1;
    }
    if (!r.name.empty() && r.mask) rules.push_back(r);
  }
  return rules;
}

inline std::string Description(HANDLE h) {
  typedef HRESULT(WINAPI * Fn)(HANDLE, PWSTR*);
  static Fn fn = reinterpret_cast<Fn>(
      GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription"));
  std::string name;
  if (!fn) return name;
  PWSTR w = nullptr;
  if (SUCCEEDED(fn(h, &w)) && w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n > 1) {
      name.resize(size_t(n) - 1);
      WideCharToMultiByte(CP_UTF8, 0, w, -1, name.data(), n, nullptr, nullptr);
    }
    LocalFree(w);
  }
  return name;
}

// Rules from the topology: one physical core of the largest-L3 cache group per
// hot thread, the group's first core left out. Empty when the machine is too small.
inline std::vector<Rule> AutoRules() {
  std::vector<Rule> rules;
  DWORD length = 0;
  GetLogicalProcessorInformationEx(RelationAll, nullptr, &length);
  if (!length) return rules;
  std::vector<uint8_t> buffer(length);
  auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
  if (!GetLogicalProcessorInformationEx(RelationAll, info, &length)) return rules;
  // Largest L3 group (group 0 only).
  uint64_t best_l3_mask = 0;
  uint64_t best_l3_size = 0;
  std::vector<uint64_t> core_masks;  // one mask per physical core
  for (DWORD offset = 0; offset < length;) {
    auto* entry = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
    if (entry->Relationship == RelationCache && entry->Cache.Level == 3 &&
        entry->Cache.GroupMask.Group == 0) {
      if (entry->Cache.CacheSize > best_l3_size) {
        best_l3_size = entry->Cache.CacheSize;
        best_l3_mask = entry->Cache.GroupMask.Mask;
      }
    } else if (entry->Relationship == RelationProcessorCore && entry->Processor.GroupCount >= 1 &&
               entry->Processor.GroupMask[0].Group == 0) {
      core_masks.push_back(entry->Processor.GroupMask[0].Mask);
    }
    offset += entry->Size;
  }
  if (!best_l3_mask || core_masks.empty()) return rules;
  std::vector<uint64_t> cores_in_group;
  for (uint64_t m : core_masks) {
    if (m & best_l3_mask) cores_in_group.push_back(m);
  }
  std::sort(cores_in_group.begin(), cores_in_group.end());
  if (cores_in_group.size() < 5) return rules;
  static const char* kThreads[] = {"GPU Commands", "D3D12 Submit", "3D Engine", "GameThread"};
  for (size_t i = 0; i < 4; ++i) {
    Rule r;
    r.name = kThreads[i];
    r.mask = cores_in_group[i + 1];  // skip the group's first core
    rules.push_back(r);
  }
  REXSYS_INFO("[threadplace] auto: L3 group mask 0x{:X} ({} MB), {} cores; rules {}",
              best_l3_mask, best_l3_size >> 20, cores_in_group.size(),
              [&] {
                std::string t;
                for (auto& r : rules) t += r.name + "=0x" + std::to_string(r.mask) + " ";
                return t;
              }());
  return rules;
}

// Applies the rules once. Returns the number of threads placed.
inline int Apply() {
  // Default: auto. FABLE2_THREAD_PLACE=off (or 0) disables the placement.
  const char* env = std::getenv("FABLE2_THREAD_PLACE");
  const char* spec = (env && *env) ? env : "auto";
  if (std::string(spec) == "off" || std::string(spec) == "0") return 0;
  const char* mode_env = std::getenv("FABLE2_THREAD_PLACE_MODE");
  const bool ideal = mode_env && std::string(mode_env) == "ideal";
  const std::vector<Rule> rules = std::string(spec) == "auto" ? AutoRules() : ParseRules(spec);
  if (rules.empty()) {
    REXSYS_WARN("[threadplace] FABLE2_THREAD_PLACE set but no valid rule in \"{}\"", spec);
    return 0;
  }
  const DWORD pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  int placed = 0;
  THREADENTRY32 te;
  te.dwSize = sizeof(te);
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID != pid) continue;
      HANDLE h = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE,
                            te.th32ThreadID);
      if (!h) continue;
      const std::string desc = Description(h);
      for (const Rule& r : rules) {
        if (desc.find(r.name) == std::string::npos) continue;
        bool ok;
        if (ideal) {
          unsigned long cpu = 0;
          for (; cpu < 64; ++cpu) {
            if (r.mask & (uint64_t(1) << cpu)) break;
          }
          PROCESSOR_NUMBER pn = {};
          pn.Group = 0;
          pn.Number = BYTE(cpu);
          ok = SetThreadIdealProcessorEx(h, &pn, nullptr) != 0;
        } else {
          ok = SetThreadAffinityMask(h, DWORD_PTR(r.mask)) != 0;
        }
        REXSYS_INFO("[threadplace] {} thread {} \"{}\" -> mask 0x{:X}: {}",
                    ideal ? "ideal" : "affinity", te.th32ThreadID, desc, r.mask,
                    ok ? "ok" : "FAILED");
        if (ok) ++placed;
        break;
      }
      CloseHandle(h);
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);
  REXSYS_INFO("[threadplace] placed {} thread(s)", placed);
  return placed;
}

// Called once per guest frame from the render thread; applies at the chosen frame.
inline void PollFrame() {
  static bool done = false;
  static uint64_t frame = 0;
  if (done) return;
  ++frame;
  static const uint64_t at = [] {
    const char* v = std::getenv("FABLE2_THREAD_PLACE_AT");
    return v ? uint64_t(std::max(1, std::atoi(v))) : uint64_t(200);
  }();
  if (frame < at) return;
  done = true;
  Apply();
}

}  // namespace fable2::threadplace
