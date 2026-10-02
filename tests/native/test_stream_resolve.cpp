// Synthetic standalone test for the index scan and stream resolution helpers;
// no game or GPU.
#include "../../src/native/capture/stream_resolve.h"

#include <cstdint>
#include <iostream>

using namespace fable2::native::capture;

int main() {
  // --- Index range against the buffer ---
  IbView ib;
  ib.size = 64;
  ib.index32 = false;
  if (!IndexRangeFits(ib, 0, 32) || IndexRangeFits(ib, 1, 32) || IndexRangeFits(ib, 0, 0)) return 1;
  ib.index32 = true;
  if (!IndexRangeFits(ib, 4, 12) || IndexRangeFits(ib, 4, 13)) return 2;
  ib.size = 0xFFFFFFFFu;
  if (IndexRangeFits(ib, 0, kMaxDrawCount + 1)) return 3;  // garbage count
  if (IndexRangeFits(ib, 0xFFFFFFF0u, 0x20)) return 4;    // start + count overflows 32 bits

  // --- Index words as the GPU fetches them (little-endian, then the swap) ---
  {
    // 16-bit, 8in16: memory holds big-endian words.
    const uint8_t be16[] = {0x00, 0x05, 0x01, 0x02, 0xFF, 0xFF, 0x00, 0x07};
    IndexScan s;
    ScanIndexWords(be16, 4, false, 1, &s);
    if (s.max_index != 0x0102 || s.restarts != 1 || s.n_first != 3) return 10;
    if (s.first[0] != 5 || s.first[1] != 0x0102 || s.first[2] != 7) return 11;
    // 16-bit, no swap: the same bytes read little-endian.
    IndexScan n;
    ScanIndexWords(be16, 2, false, 0, &n);
    if (n.max_index != 0x0500 || n.first[1] != 0x0201) return 12;
  }
  {
    // 32-bit 8in32 (big-endian dwords) and 16in32 (halves swapped).
    const uint8_t be32[] = {0x00, 0x01, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0xFF};
    IndexScan s;
    ScanIndexWords(be32, 2, true, 2, &s);
    if (s.max_index != 0x00010002 || s.restarts != 1 || s.n_first != 1) return 13;
    const uint8_t w[] = {0x01, 0x00, 0x02, 0x00};  // LE 0x00020001 -> 16in32 0x00010002
    IndexScan h;
    ScanIndexWords(w, 1, true, 3, &h);
    if (h.max_index != 0x00010002) return 14;
  }
  {
    // Only reset indices: no maximum. More than 64 indices keep the first 64.
    const uint8_t cut[] = {0xFF, 0xFF, 0xFF, 0xFF};
    IndexScan s;
    ScanIndexWords(cut, 2, false, 1, &s);
    if (s.max_index != -1 || s.restarts != 2 || s.n_first != 0) return 15;
    uint8_t many[200];
    for (int i = 0; i < 100; ++i) {
      many[2 * i] = 0;
      many[2 * i + 1] = uint8_t(i);
    }
    IndexScan m;
    ScanIndexWords(many, 100, false, 1, &m);
    if (m.n_first != 64 || m.first[63] != 63 || m.max_index != 99) return 16;
  }

  // --- Stream for a fetch slot (stream i feeds slot 95 - i) ---
  uint32_t stream = 99;
  if (!StreamForFetchSlot(95, &stream) || stream != 0) return 20;
  if (!StreamForFetchSlot(80, &stream) || stream != 15) return 21;
  if (StreamForFetchSlot(79, &stream) || StreamForFetchSlot(96, &stream)) return 22;

  // --- Fetch constant match: the object's dwords plus the stream offset equal
  // the device shadow (xdk_layout.h kVbFetchDword example: object dword6
  // 0xFA63AD03, dword7 0x10012C02, offset 0x7E0, shadow fc 0x1A63C4E3 0x10012422).
  {
    StreamView v;
    StreamFromFetch(0xFA63AD03u, 0x10012C02u, 0x1A63C4E3u, 0x10012422u, &v);
    if (v.base != xdk::GpuAddress(0xFA63AD00u) || v.offset != 0x7E0 || !v.fc_match) return 30;
    if (v.size != ((0x10012C02u >> 2) & 0xFFFFFF) * 4) return 31;
    StreamView t;  // fetch constant type is not 3
    StreamFromFetch(0xFA63AD03u, 0x10012C02u, 0x1A63C4E2u, 0x10012422u, &t);
    if (t.fc_match) return 32;
    StreamView d;  // size dword does not account for the offset
    StreamFromFetch(0xFA63AD03u, 0x10012C02u, 0x1A63C4E3u, 0x10012C02u, &d);
    if (d.fc_match) return 33;
    StreamView o;  // offset past the end of the buffer (base above the shadow address)
    StreamFromFetch(0xFA63AD03u, 0x10012C02u, 0x1A63ACE3u, 0x10012C02u, &o);
    if (o.fc_match || o.offset < o.size) return 34;
  }
  std::cout << "PASS: stream resolve\n";
  return 0;
}
