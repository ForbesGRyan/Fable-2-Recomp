// thread_placement.h - experiment: place named host threads on chosen logical CPUs.
//
// FABLE2_THREAD_PLACE="GPU Commands=2;3D Engine=4;GameThread=6"
//   name=cpu pairs separated by ';'. The name is matched as a substring of the
//   thread's description (SetThreadDescription, as the SDK names its threads;
//   guest threads carry the guest's own names such as "3D Engine (F80000E8)").
//   cpu is a logical processor index (0..63). Several cpus may be given as a
//   mask with '+': "GPU Commands=2+3" pins to logical 2 and 3 (one physical
//   core with SMT).
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

// Applies the rules once. Returns the number of threads placed.
inline int Apply() {
  const char* spec = std::getenv("FABLE2_THREAD_PLACE");
  if (!spec || !*spec) return 0;
  const char* mode_env = std::getenv("FABLE2_THREAD_PLACE_MODE");
  const bool ideal = mode_env && std::string(mode_env) == "ideal";
  const std::vector<Rule> rules = ParseRules(spec);
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
