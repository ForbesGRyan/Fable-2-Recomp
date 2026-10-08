#include "d3d_messages.h"

#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>

#include <dxgidebug.h>
#endif

namespace fable2::native::render {

#ifdef _WIN32
namespace {
// DXGI_DEBUG_ALL and IID_IDXGIInfoQueue (dxgidebug.h), written out so that no
// GUID library is linked for them.
constexpr GUID kDxgiDebugAll = {0xe48ae283, 0xda80, 0x490b, {0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x08}};
constexpr GUID kIidDxgiInfoQueue = {0xD67441C7, 0x672A, 0x476f, {0x9E, 0x82, 0xCD, 0x55, 0xB4, 0x49, 0x49, 0xCE}};

// The process's information queue, looked up once; null when dxgidebug.dll or
// the interface is not there (the debug layer is not installed).
IDXGIInfoQueue* Queue() {
  static IDXGIInfoQueue* const queue = []() -> IDXGIInfoQueue* {
    const HMODULE dll = LoadLibraryW(L"dxgidebug.dll");
    if (!dll) return nullptr;
    using GetInterface = HRESULT(WINAPI*)(REFIID, void**);
    const auto get = reinterpret_cast<GetInterface>(reinterpret_cast<void*>(GetProcAddress(dll, "DXGIGetDebugInterface")));
    void* found = nullptr;
    if (!get || FAILED(get(kIidDxgiInfoQueue, &found))) return nullptr;
    return static_cast<IDXGIInfoQueue*>(found);
  }();
  return queue;
}

D3dSeverity SeverityOf(DXGI_INFO_QUEUE_MESSAGE_SEVERITY s) {
  switch (s) {
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION: return D3dSeverity::kCorruption;
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR: return D3dSeverity::kError;
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING: return D3dSeverity::kWarning;
    default: return D3dSeverity::kOther;
  }
}
}  // namespace

bool D3dMessagesAvailable() { return Queue() != nullptr; }

void PollD3dMessages(D3dMessageLog& log, const std::function<void(const std::string&)>& write) {
  IDXGIInfoQueue* const queue = Queue();
  if (!queue) return;
  UINT64 count = queue->GetNumStoredMessages(kDxgiDebugAll);
  if (count == 0) return;
  std::vector<unsigned char> storage;
  const auto read = [&](UINT64 index) {
    SIZE_T bytes = 0;
    if (FAILED(queue->GetMessage(kDxgiDebugAll, index, nullptr, &bytes)) || bytes < sizeof(DXGI_INFO_QUEUE_MESSAGE)) return;
    storage.assign(bytes, 0);
    auto* const message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(storage.data());
    if (FAILED(queue->GetMessage(kDxgiDebugAll, index, message, &bytes))) return;
    // The text is bounded by its stated length and by its terminator, whichever comes first.
    const char* const text = message->pDescription ? message->pDescription : "";
    const std::string line = log.Add(SeverityOf(message->Severity), int(message->ID),
                                     std::string_view(text, strnlen(text, message->pDescription ? message->DescriptionByteLength : 0)));
    if (!line.empty()) write(line);
  };
  // The count is read again after the last message: one that another thread stored meanwhile is
  // read too. One stored between that count and the clear below is lost (the draws' own messages
  // are stored by the thread that polls, so none of those is).
  for (UINT64 i = 0; i < count; ++i) {
    read(i);
    if (i + 1 == count) count = queue->GetNumStoredMessages(kDxgiDebugAll);
  }
  queue->ClearStoredMessages(kDxgiDebugAll);
}
#else
bool D3dMessagesAvailable() { return false; }
void PollD3dMessages(D3dMessageLog&, const std::function<void(const std::string&)>&) {}
#endif

}  // namespace fable2::native::render
