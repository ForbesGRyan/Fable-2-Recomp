#pragma once
#include <cstddef>
#include <cstdint>

namespace fable2::native::capture {
// CPU-emitted PM4, not the emulator's register file. Only complete, direct
// window writes are accepted. XDK 821F97E8 emits type 0 or SET_CONSTANT;
// the clear builder also resets these registers. A broken span loses ownership.
struct WindowCommands {
  uint32_t words[3]{};
  uint8_t known = 0;
  void Invalidate() { known = 0; }
  bool valid() const { return known == 7; }
  bool Apply(const uint8_t* be, size_t count) {
    auto word = [be](size_t i) {
      const uint8_t* p = be + 4*i;
      return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3];
    };
    auto write = [this](uint32_t reg, uint32_t value) {
      if (reg >= 0x2080 && reg <= 0x2082) {
        words[reg-0x2080] = value;
        known |= uint8_t(1u << (reg-0x2080));
      }
    };
    if (!be && count) { Invalidate(); return false; }
    for (size_t at=0; at<count;) {
      const uint32_t header=word(at++), type=header>>30;
      if (type==2) continue;
      if (type==1) {
        if (count-at<2) { Invalidate(); return false; }
        write(header&0x7FF,word(at));write((header>>11)&0x7FF,word(at+1));
        at+=2;continue;
      }
      const size_t n=((header>>16)&0x3FFF)+1;
      if (n>count-at) { Invalidate(); return false; }
      if (type==0) {
        for (size_t i=0;i<n;i++) write((header&0x7FFF)+((header&0x8000)?0:uint32_t(i)),word(at+i));
      } else {
        const uint32_t op=(header>>8)&0x7F;
        // Conditional ranges can skip otherwise direct window writes. Fetched
        // state and memory/RMW writes also cannot establish this ownership.
        if (op==0x3F || op==0x37 || op==0x21 || op==0x2E || op==0x25 ||
            op==0x3D || op==0x3E || op==0x4F || op==0x44 || op==0x45 ||
            op==0x2C || op==0x48 || op==0x64) {
          Invalidate(); return false;
        }
        if (op==0x2D || op==0x55 || op==0x56) {
          const uint32_t offset=word(at);
          const uint32_t base=op!=0x2D ? offset&0xFFFF : 0x2000+(offset&0x7FF);
          if (op!=0x2D || ((offset>>16)&0xFF)==4) {
            if((header&1) && base<=0x2082 && uint64_t(base)+n-1>0x2080) {
              Invalidate();return false;
            }
            for(size_t i=1;i<n;i++) write(base+uint32_t(i-1),word(at+i));
          }
        }
      }
      at+=n;
    }
    return true;
  }
};
}
