// sampler.cpp - user-mode sampling profiler for fable_2.exe (no admin needed).
//
// Suspends each thread of the target process in turn at a fixed interval, walks
// its stack with DbgHelp (StackWalk64 over ReadProcessMemory) and resolves the
// frames with the PDBs next to the modules (fable_2.pdb names every guest
// function sub_XXXXXXXX; the SDK DLLs need a -g build, see
// tools/perf/build_sdk_sym.cmd). Writes:
//   <out>.threads.txt  one line per thread: id, name, samples
//   <out>.flat.txt     per thread: top functions by self samples and by inclusive samples
//   <out>.stacks.txt   per thread: collapsed stacks "root;...;leaf count" (flamegraph input)
//
// Build: clang++ -O2 -std=c++20 sampler.cpp -o sampler.exe -ldbghelp -lpsapi
// Usage: sampler.exe <pid> <seconds> <interval_ms> <out_prefix> [thread name filter...]

#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct ThreadRec {
  DWORD id = 0;
  HANDLE h = nullptr;
  std::string name;
  uint64_t samples = 0;
  std::unordered_map<std::string, uint64_t> self, incl, stacks;
};

static std::string Symbolize(HANDLE process, DWORD64 addr,
                             std::unordered_map<DWORD64, std::string>& cache) {
  auto it = cache.find(addr);
  if (it != cache.end()) return it->second;
  char buf[sizeof(SYMBOL_INFO) + 512] = {};
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 511;
  DWORD64 disp = 0;
  std::string name;
  if (SymFromAddr(process, addr, &disp, sym)) {
    name = sym->Name;
  } else {
    HMODULE mods[256];
    DWORD needed = 0;
    char modname[MAX_PATH] = "?";
    if (EnumProcessModules(process, mods, sizeof(mods), &needed)) {
      for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i) {
        MODULEINFO mi;
        if (GetModuleInformation(process, mods[i], &mi, sizeof(mi))) {
          auto base = reinterpret_cast<DWORD64>(mi.lpBaseOfDll);
          if (addr >= base && addr < base + mi.SizeOfImage) {
            GetModuleBaseNameA(process, mods[i], modname, sizeof(modname));
            char tmp[MAX_PATH + 32];
            std::snprintf(tmp, sizeof(tmp), "%s+0x%llx", modname,
                          (unsigned long long)(addr - base));
            name = tmp;
            break;
          }
        }
      }
    }
    if (name.empty()) {
      char tmp[40];
      std::snprintf(tmp, sizeof(tmp), "0x%llx", (unsigned long long)addr);
      name = tmp;
    }
  }
  cache.emplace(addr, name);
  return name;
}

static std::string ThreadName(HANDLE h) {
  typedef HRESULT(WINAPI * Fn)(HANDLE, PWSTR*);
  static Fn fn = reinterpret_cast<Fn>(
      GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription"));
  std::string name;
  if (fn) {
    PWSTR w = nullptr;
    if (SUCCEEDED(fn(h, &w)) && w) {
      int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
      if (n > 1) {
        name.resize(n - 1);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, name.data(), n, nullptr, nullptr);
      }
      LocalFree(w);
    }
  }
  return name;
}

template <class M>
static std::vector<std::pair<std::string, uint64_t>> Top(const M& m, size_t n) {
  std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
  std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
  if (v.size() > n) v.resize(n);
  return v;
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::fprintf(stderr,
                 "usage: sampler <pid> <seconds> <interval_ms> <out_prefix> [name filter...]\n");
    return 2;
  }
  DWORD pid = DWORD(std::strtoul(argv[1], nullptr, 10));
  double seconds = std::atof(argv[2]);
  int interval_ms = std::atoi(argv[3]);
  std::string out = argv[4];
  std::vector<std::string> filters;
  for (int i = 5; i < argc; ++i) filters.push_back(argv[i]);

  HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (!process) {
    std::fprintf(stderr, "OpenProcess(%lu) failed: %lu\n", pid, GetLastError());
    return 1;
  }
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_NO_PROMPTS);
  // Symbol search path: the directory of the target's exe (PDBs next to the modules).
  char exe_path[MAX_PATH] = {};
  GetModuleFileNameExA(process, nullptr, exe_path, MAX_PATH);
  std::string exe_dir = exe_path;
  size_t slash = exe_dir.find_last_of("\\/");
  if (slash != std::string::npos) exe_dir.resize(slash);
  if (!SymInitialize(process, exe_dir.c_str(), TRUE)) {
    std::fprintf(stderr, "SymInitialize failed: %lu\n", GetLastError());
    return 1;
  }

  std::vector<ThreadRec> threads;
  {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
      do {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                              FALSE, te.th32ThreadID);
        if (!h) continue;
        ThreadRec r;
        r.id = te.th32ThreadID;
        r.h = h;
        r.name = ThreadName(h);
        bool keep = filters.empty();
        for (auto& f : filters) {
          if (r.name.find(f) != std::string::npos) keep = true;
        }
        if (keep) {
          threads.push_back(r);
        } else {
          CloseHandle(h);
        }
      } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
  }
  std::fprintf(stderr, "sampling %zu threads of pid %lu for %.0f s every %d ms\n", threads.size(),
               pid, seconds, interval_ms);

  std::unordered_map<DWORD64, std::string> symcache;
  const auto t_end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  uint64_t total = 0;
  std::vector<std::string> frames;
  std::vector<DWORD64> addrs;
  while (std::chrono::steady_clock::now() < t_end) {
    for (auto& t : threads) {
      if (SuspendThread(t.h) == DWORD(-1)) continue;
      CONTEXT ctx;
      std::memset(&ctx, 0, sizeof(ctx));
      ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
      addrs.clear();
      if (GetThreadContext(t.h, &ctx)) {
        STACKFRAME64 sf;
        std::memset(&sf, 0, sizeof(sf));
        sf.AddrPC.Offset = ctx.Rip;
        sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = ctx.Rbp;
        sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Offset = ctx.Rsp;
        sf.AddrStack.Mode = AddrModeFlat;
        for (int depth = 0; depth < 48; ++depth) {
          if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, t.h, &sf, &ctx, nullptr,
                           SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
          }
          if (sf.AddrPC.Offset == 0) break;
          addrs.push_back(sf.AddrPC.Offset);
        }
      }
      ResumeThread(t.h);
      if (addrs.empty()) continue;
      ++t.samples;
      ++total;
      frames.clear();
      for (size_t i = 0; i < addrs.size(); ++i) {
        // Return addresses point after the call instruction; step back one byte
        // for every frame but the leaf.
        frames.push_back(Symbolize(process, i == 0 ? addrs[i] : addrs[i] - 1, symcache));
      }
      ++t.self[frames[0]];
      std::vector<std::string> uniq(frames);
      std::sort(uniq.begin(), uniq.end());
      uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
      for (auto& f : uniq) ++t.incl[f];
      std::string collapsed;
      for (size_t i = frames.size(); i-- > 0;) {
        if (!collapsed.empty()) collapsed += ';';
        collapsed += frames[i];
      }
      ++t.stacks[collapsed];
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }

  std::sort(threads.begin(), threads.end(),
            [](const ThreadRec& a, const ThreadRec& b) { return a.samples > b.samples; });
  {
    FILE* f = std::fopen((out + ".threads.txt").c_str(), "w");
    if (!f) return 1;
    std::fprintf(f, "total samples %llu (every thread is sampled once per tick)\n",
                 (unsigned long long)total);
    for (auto& t : threads) {
      std::fprintf(f, "%6lu  %-32s %8llu\n", t.id, t.name.c_str(), (unsigned long long)t.samples);
    }
    std::fclose(f);
  }
  {
    FILE* f = std::fopen((out + ".flat.txt").c_str(), "w");
    if (!f) return 1;
    for (auto& t : threads) {
      if (t.samples < 10) continue;
      std::fprintf(f, "=== thread %lu %s: %llu samples\n", t.id, t.name.c_str(),
                   (unsigned long long)t.samples);
      std::fprintf(f, "--- self (top 40)\n");
      for (auto& [name, n] : Top(t.self, 40)) {
        std::fprintf(f, "%6.2f%% %8llu  %s\n", 100.0 * double(n) / double(t.samples),
                     (unsigned long long)n, name.c_str());
      }
      std::fprintf(f, "--- inclusive (top 60)\n");
      for (auto& [name, n] : Top(t.incl, 60)) {
        std::fprintf(f, "%6.2f%% %8llu  %s\n", 100.0 * double(n) / double(t.samples),
                     (unsigned long long)n, name.c_str());
      }
    }
    std::fclose(f);
  }
  {
    FILE* f = std::fopen((out + ".stacks.txt").c_str(), "w");
    if (!f) return 1;
    for (auto& t : threads) {
      if (t.samples < 10) continue;
      std::fprintf(f, "=== thread %lu %s\n", t.id, t.name.c_str());
      for (auto& [st, n] : Top(t.stacks, 400)) {
        std::fprintf(f, "%llu %s\n", (unsigned long long)n, st.c_str());
      }
    }
    std::fclose(f);
  }
  for (auto& t : threads) CloseHandle(t.h);
  SymCleanup(process);
  CloseHandle(process);
  std::fprintf(stderr, "done: %llu samples\n", (unsigned long long)total);
  return 0;
}
