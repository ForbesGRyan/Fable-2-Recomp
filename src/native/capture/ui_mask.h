#pragma once
#include "presentation_frame.h"

namespace fable2::native::capture {
inline constexpr uint64_t kUiMaskVs=0x81BADE63A169E3DCull, kUiMaskPs=0x06547412E4B72251ull;
inline bool UiMaskOpaqueCommand(uint32_t op) {
  // Memory writes, conditionally skipped packet ranges, binned draws and
  // fetched state cannot establish the direct copy/draw ownership model.
  return op==0x3F || op==0x37 || op==0x21 || op==0x2E || op==0x25 ||
    op==0x3D || op==0x3E || op==0x4F || op==0x44 || op==0x45 ||
    op==0x34 || op==0x35 || op==0x2C || op==0x48 || op==0x64;
}
inline bool UiMaskFetch(const uint32_t* fetch) {
  return fetch && fetch[0]==0x85004802 && (fetch[1]&4095)==0x86 && (fetch[1]>>12) &&
    fetch[2]==0x002CE27F && fetch[3]==0x00A80C14 && fetch[4]==3 && fetch[5]==0x218;
}
inline bool UiMaskVertexOf(const float* position,const float* uv,const float (*matrix)[4],UiVertex* out) {
  if(!position || !uv || !matrix || !out) return false;
  for(uint32_t i=0;i<3;i++) if(!UiFinite(position[i])) return false;
  for(uint32_t i=0;i<2;i++) if(!UiFinite(uv[i],64)) return false;
  UiVertex vertex;
  for(uint32_t row=0;row<4;row++) {
    for(uint32_t col=0;col<4;col++) {
      if(!UiFinite(matrix[row][col])) return false;
      vertex.clip[row]+=matrix[row][col]*(col==3 ? 1.f:position[col]);
    }
    if(!UiFinite(vertex.clip[row])) return false;
  }
  std::copy(uv,uv+2,vertex.uv);*out=vertex;return true;
}
// A resolve must be established by its own writes at its draw, before later
// restoration. This is deliberately smaller than a general PM4 interpreter.
// Predicated copies, indirect buffers and memory/RMW writes lose ownership.
struct UiMaskCopy {
  uint32_t base=0, vertex_fetch[2]{};
  bool Read(const uint8_t* be,size_t count) {
    *this={};
    if(!be || !count || count>2048) return false;
    auto word=[&](size_t i) {const auto* p=be+4*i;return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
    uint32_t copy[4]{},vf[2]{},resolved_base=0,resolved_vf[2]{},mode=0,surface=0x0A000280,colour=0x5A0;uint8_t known=0,vknown=0;bool mknown=false,found=false;
    auto write=[&](uint32_t reg,uint32_t value) {
      if(reg>=0x2318 && reg<=0x231B) {copy[reg-0x2318]=value;known|=uint8_t(1u<<(reg-0x2318));}
      if(reg==0x2208) {mode=value;mknown=true;}
      if(reg==0x2000) surface=value;
      if(reg==0x2001) colour=value;
      if(reg==0x4800 || reg==0x4801) {vf[reg-0x4800]=value;vknown|=uint8_t(1u<<(reg-0x4800));}
    };
    // The command writer kicks the ring with two EVENT_WRITE_SHD (0x58) fences
    // and then starts a new segment behind a short descriptor block that is
    // not PM4 (its words may look like packets and swallow real ones). A
    // forward-contiguous span can cross that boundary: directly after the two
    // fences, unless the rest already parses, resume once at the first offset
    // (within 16 words) from which every packet header parses to the end.
    // Anything else that fails to parse refuses.
    uint32_t fences=0;bool resumed=false;
    const auto parses=[&](size_t from) {
      for(size_t i=from;i<count;) {
        uint32_t g=word(i++),t=g>>30;
        if(t==2) continue;
        if(t==1) {if(count-i<2) return false;i+=2;continue;}
        size_t m=((g>>16)&16383)+1;if(m>count-i) return false;i+=m;
      }
      return true;
    };
    for(size_t at=0;at<count;) {
      uint32_t h=word(at++),type=h>>30;
      if(type==2) continue;
      if(type==1) {if(count-at<2) return false;write(h&2047,word(at));write((h>>11)&2047,word(at+1));at+=2;continue;}
      size_t n=((h>>16)&16383)+1;if(n>count-at) return false;
      if(type==0) {for(size_t i=0;i<n;i++) write((h&32767)+((h&32768)?0:uint32_t(i)),word(at+i));}
      else {
        uint32_t op=(h>>8)&127;
        fences=op==0x58 ? fences+1:0;
        if(fences==2 && !resumed && !parses(at+n)) {
          // Every captured segment start begins with EVENT_WRITE C0004600 00000019
          // (three descriptor variants, 7 to 11 words); anchor the resume there.
          const size_t kick=at+n;size_t resume=0;
          for(size_t i=kick+1;i+1<count && i<=kick+24;i++) if(word(i)==0xC0004600 && word(i+1)==0x19 && parses(i)) {resume=i;break;}
          if(!resume) return false;
          at=resume;resumed=true;fences=0;continue;
        }
        if(UiMaskOpaqueCommand(op)) return false;
        if(op==0x2D || op==0x55 || op==0x56) {
          uint32_t offset=word(at),reg=op==0x2D ? 0x2000+(offset&2047):offset&65535;
          if(op!=0x2D || ((offset>>16)&255)==4) {
            // A predicated write before the copy could alter its registers;
            // after the complete copy only draws and copies matter.
            if(h&1) {if(!found) return false;}
            else for(size_t i=1;i<n;i++) write(reg+uint32_t(i-1),word(at+i));
          }
        }
        if(op==0x22 || op==0x36) {
          // Subsequent ordinary raster draws cannot alter the resolved image.
          // A second copy could overwrite its base and must remain a refusal.
          if(found) {if(!mknown || mode==6) return false;at+=n;continue;}
          // No intervening raster draw may alter the captured mask before copy.
          if((h&1) || op!=0x36 || n!=1 || word(at)!=0x00030088 ||
             known!=15 || !mknown || mode!=6 || vknown!=3 ||
             copy[0]!=0x00100000 || !copy[1] || (copy[1]&4095) ||
             copy[2]!=0x01680280 || copy[3]!=0x01000302 || surface!=0x0A000280 || colour!=0x5A0 ||
             (vf[0]&3)!=3 || vf[1]!=0x1000001A) return false;
          resolved_base=copy[1];std::copy(vf,vf+2,resolved_vf);found=true;
        }
      }
      at+=n;
    }
    if(found) {base=resolved_base;std::copy(resolved_vf,resolved_vf+2,vertex_fetch);}
    return found;
  }
};
// The resolve rectangle is CPU-emitted float2 data, not a guest render target.
// Its three corners cover the complete 640x360 mask in one of the two
// captured forms: pixel edges (the Maps view: {0,0,640,0,0,360}) or pixel
// centres (the pause menu: {-0.5,-0.5,639.5,-0.5,639.5,359.5}). Refuse
// cropped copies and any other corner order.
inline bool UiMaskCopyRectangle(const float* xy) {
  if(!xy) return false;
  constexpr float forms[2][6]={{0,0,640,0,0,360},{-0.5f,-0.5f,639.5f,-0.5f,639.5f,359.5f}};
  for(const auto& form:forms) {
    bool match=true;
    for(uint32_t i=0;i<6;i++) match=match && xy[i]==form[i];
    if(match) return true;
  }
  return false;
}
inline bool UiMaskClearCommands(const uint8_t* be,size_t count,bool predicate_pass) {
  if(!be || count!=22) return false;
  auto word=[&](size_t i) {const auto* p=be+4*i;return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
  if(word(0)!=0x00054800 || (word(1)&3)!=3 || !(word(1)&~3u) || word(2)!=0x10000056 || word(7)!=0x00025000) return false;
  for(size_t i:{3u,4u,5u,6u,8u,9u,10u}) if(word(i)!=0) return false;
  // The clear builder's own final writes, not restored device registers.
  const uint32_t tail[]={0x00012000,0x0A000280,0x5A0,0x2301,0,0x2200,0,0x2208,4};
  for(size_t i=0;i<9;i++) if(word(count-11+i)!=tail[i]) return false;
  uint32_t h=word(count-2);
  return (h==0xC0003600 || (h==0xC0003601 && predicate_pass)) && word(count-1)==0x00030088;
}
inline bool UiMaskBinResetCommands(const uint8_t* be,size_t count) {
  // 822A6318(r4=0) emits these four unpredicated packets in this order,
  // followed by its ordinary scissor routine. The mask words are CPU-owned.
  if(!be || count<8 || count>128) return false;
  auto word=[&](size_t i) {const auto* p=be+4*i;return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
  return word(0)==0xC0006000 && word(1)==UINT32_MAX &&
    word(2)==0xC0006200 && word(3)==UINT32_MAX &&
    word(4)==0xC0006100 && word(5)==0 && word(6)==0xC0006300 && word(7)==0;
}
inline bool UiMaskDrawCommands(const uint8_t* be,size_t count,bool predicate_pass) {
  if(!be || !count || count>8192) return false;
  auto word=[&](size_t i) {const auto* p=be+4*i;return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
  uint32_t draws=0;
  for(size_t at=0;at<count;) {
    uint32_t h=word(at++),type=h>>30;
    if(type==2) continue;
    size_t n=type==1 ? 2:((h>>16)&16383)+1;if(n>count-at) return false;
    if(type==3) {
      uint32_t op=(h>>8)&127;
      // The externally observed bin reset proves the entry selection only.
      // Don't keep that proof across a selection/mask change inside this draw.
      if(UiMaskOpaqueCommand(op) || op==0x50 || op==0x51 || (op>=0x60 && op<=0x63)) return false;
      if(op==0x22 || op==0x36) {
        if((h&1) && !predicate_pass) return false;
        if(++draws!=1 || (op==0x22 && n<2)) return false;
        uint32_t init=word(at+(op==0x22 ? 1:0));
        if((init&63)!=4 || (init>>16)!=6) return false;
      }
    }
    at+=n;
  }
  return draws==1;
}
inline bool UiMaskState(const UiDraw& d) {
  return d.surface_info==0x0A000280 && d.colour_info==0x5A0 && d.state.read &&
    d.state.depth_control==0x00708764 && d.state.blend_control==0x07060706 &&
    d.state.color_control==4 && d.state.color_mask==15 && d.state.su_sc_mode==0x00218000 &&
    d.state.vte_cntl==0x43F && d.clip_control==0x80000 &&
    d.viewport[0]==320 && d.viewport[1]==320 && d.viewport[2]==-180 && d.viewport[3]==180;
}
}
