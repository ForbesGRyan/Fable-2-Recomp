// Loads <game_dir>\default.xex through the ReXGlue runtime in tool mode (a
// sibling default.xexp is applied automatically by UserModule) and writes the
// in-memory image plus a JSON description. Local analysis input only: never
// commit the outputs.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

using rex::X_STATUS;

// Copies only committed, readable pages: some sections (e.g. Skate 3's .reloc)
// are discarded after load and their host pages are unmapped.
static void CopyReadable(uint8_t* dst, const void* src, size_t size) {
  const auto* p = static_cast<const uint8_t*>(src);
  size_t off = 0;
  while (off < size) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p + off, &mbi, sizeof(mbi))) break;
    size_t region = size_t(static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize - (p + off));
    size_t n = std::min(region, size - off);
    const bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    if (readable) std::memcpy(dst + off, p + off, n);
    off += n;
  }
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <game_dir> <out_prefix>\n", argv[0]);
    return 2;
  }
  const std::filesystem::path game_dir = argv[1];
  const std::string out_prefix = argv[2];
  const bool patched = std::filesystem::exists(game_dir / "default.xexp");

  rex::Runtime runtime(game_dir);
  if (runtime.Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel,
                                       .tool_mode = true}) != X_STATUS_SUCCESS) {
    std::fprintf(stderr, "runtime setup failed\n");
    return 1;
  }
  if (runtime.LoadXexImage("game:\\default.xex") != X_STATUS_SUCCESS) {
    std::fprintf(stderr, "LoadXexImage failed\n");
    return 1;
  }
  auto user_module = runtime.kernel_state()->GetExecutableModule();
  auto* xex = user_module ? user_module->xex_module() : nullptr;
  if (!xex) {
    std::fprintf(stderr, "no executable module\n");
    return 1;
  }

  const uint32_t base = xex->base_address();
  uint32_t end = base;
  for (const auto& s : xex->binary_sections()) {
    end = std::max(end, s.virtual_address + s.virtual_size);
  }
  std::vector<uint8_t> image(end - base, 0);
  for (const auto& s : xex->binary_sections()) {
    if (s.host_data && s.virtual_size) {
      CopyReadable(image.data() + (s.virtual_address - base), s.host_data, s.virtual_size);
    }
  }
  std::ofstream(out_prefix + ".img", std::ios::binary)
      .write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));

  std::ofstream json(out_prefix + ".json");
  json << "{\"base\": " << base << ", \"size\": " << image.size()
       << ", \"entry\": " << xex->entry_point() << ", \"patched\": " << (patched ? "true" : "false")
       << ", \"pdata\": {\"address\": " << xex->exception_directory_address()
       << ", \"size\": " << xex->exception_directory_size() << "}, \"sections\": [";
  bool first = true;
  for (const auto& s : xex->binary_sections()) {
    json << (first ? "" : ", ") << "{\"name\": \"" << s.name << "\", \"address\": "
         << s.virtual_address << ", \"size\": " << s.virtual_size
         << ", \"executable\": " << (s.executable ? "true" : "false") << "}";
    first = false;
  }
  json << "]}\n";
  std::printf("dumped 0x%08X..0x%08X (%zu bytes), patched=%d\n", base, end, image.size(), patched);
  return 0;
}
