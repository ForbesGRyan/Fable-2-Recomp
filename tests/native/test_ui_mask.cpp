#include "../../src/native/capture/ui_mask.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
using namespace fable2::native::capture;
static std::vector<uint8_t> bytes(std::initializer_list<uint32_t> words) {
  std::vector<uint8_t> result;
  for(auto w:words) for(int shift=24;shift>=0;shift-=8) result.push_back(uint8_t(w>>shift));
  return result;
}
int main(int argc,char** argv) {
  UiMaskCopy copy;
  auto commands=bytes({0x00032318,0x100000,0x12B1D000,0x01680280,0x01000302,
    0x00014800,0x1FA1E51F,0x1000001A,0x00002208,6,0xC0003600,0x00030088,
    0x00002318,0,0x0000231B,0,0x00002208,4});
  assert(copy.Read(commands.data(),commands.size()/4) && copy.base==0x12B1D000);
  assert(copy.vertex_fetch[0]==0x1FA1E51F);
  auto bad=commands;bad[11*4+3]=0; // malformed initiator
  assert(!copy.Read(bad.data(),bad.size()/4));
  bad=commands;bad[10*4+3]=1; // unknown predication
  assert(!copy.Read(bad.data(),bad.size()/4));
  bad=commands;bad[4*4+3]=3; // another resolve format
  assert(!copy.Read(bad.data(),bad.size()/4));
  bad=commands;bad[2*4+3]=1; // destination alignment
  assert(!copy.Read(bad.data(),bad.size()/4));
  for(size_t n=0;n<12;n++) assert(!copy.Read(commands.data(),n));
  assert(!copy.Read(nullptr,16) && !copy.Read(commands.data(),2049));
  auto indirect=bytes({0xC0013F00,0x100000,4});
  commands.insert(commands.begin(),indirect.begin(),indirect.end());
  assert(!copy.Read(commands.data(),commands.size()/4));
  assert(copy.base==0 && copy.vertex_fetch[0]==0);
  auto copy_only=bytes({0x00032318,0x100000,0x12B1D000,0x01680280,0x01000302,
    0x00014800,0x1FA1E51F,0x1000001A,0x00002208,6,0xC0003600,0x00030088});
  for(uint32_t op:{0x3Du,0x3Eu,0x44u,0x45u,0x34u,0x35u,0x25u}) {
    auto unsafe=bytes({0xC0000000|(op<<8),0});unsafe.insert(unsafe.end(),copy_only.begin(),copy_only.end());
    assert(!copy.Read(unsafe.data(),unsafe.size()/4));
  }
  auto late=copy_only;const auto write=bytes({0xC0003D00,0});late.insert(late.end(),write.begin(),write.end());
  assert(!copy.Read(late.data(),late.size()/4) && copy.base==0 && copy.vertex_fetch[0]==0);
  auto clear=bytes({0x00054800,0x1FA1E41F,0x10000056,0,0,0,0,0x00025000,0,0,0,
    0x00012000,0x0A000280,0x5A0,0x2301,0,0x2200,0,0x2208,4,0xC0003601,0x30088});
  assert(UiMaskClearCommands(clear.data(),22,true));
  assert(!UiMaskClearCommands(clear.data(),22,false));
  clear[20*4+3]=0;
  assert(UiMaskClearCommands(clear.data(),22,false));
  clear[13*4+3]=0;
  assert(!UiMaskClearCommands(clear.data(),22,true));
  auto quad=bytes({0xC0032200,0,0x60004,0x1FA1D510,12});
  assert(UiMaskDrawCommands(quad.data(),5,false));
  quad[3]=1;assert(!UiMaskDrawCommands(quad.data(),5,false));
  assert(UiMaskDrawCommands(quad.data(),5,true));
  for(uint32_t op:{0x50u,0x51u,0x60u,0x61u,0x62u,0x63u}) {
    auto changed=bytes({0xC0000000|(op<<8),0});
    changed.insert(changed.end(),quad.begin(),quad.end());
    assert(!UiMaskDrawCommands(changed.data(),changed.size()/4,true));
  }
  const auto duplicate=quad;quad.insert(quad.end(),duplicate.begin(),duplicate.end());
  assert(!UiMaskDrawCommands(quad.data(),quad.size()/4,true));
  float xy[]={0,0,640,0,0,360};assert(UiMaskCopyRectangle(xy));
  float centred[]={-0.5f,-0.5f,639.5f,-0.5f,639.5f,359.5f};assert(UiMaskCopyRectangle(centred));
  centred[1]=0;assert(!UiMaskCopyRectangle(centred));
  xy[2]=639;assert(!UiMaskCopyRectangle(xy));
  xy[2]=std::numeric_limits<float>::quiet_NaN();assert(!UiMaskCopyRectangle(xy));
  assert(!UiMaskCopyRectangle(nullptr));
  auto reset=bytes({0xC0006000,UINT32_MAX,0xC0006200,UINT32_MAX,0xC0006100,0,0xC0006300,0});
  assert(UiMaskBinResetCommands(reset.data(),8));
  reset[7]=0;assert(!UiMaskBinResetCommands(reset.data(),8));
  uint32_t fetch[]={0x85004802,0x12B1D086,0x002CE27F,0x00A80C14,3,0x218};
  assert(UiMaskFetch(fetch));fetch[3]^=1;assert(!UiMaskFetch(fetch));fetch[3]^=1;
  UiFrame frame;UiMask mask;mask.base=0x12B1D000;frame.masks.push_back(mask);
  UiDraw draw;draw.kind=UiKind::kSprite;draw.vertices.resize(3);draw.registers[5][0]=1;
  std::copy(fetch,fetch+6,draw.fetch[2]);draw.masks[2]=0;
  assert(frame.Add(draw));
  draw.masks[2]=1;assert(!frame.Add(draw));
  UiFrame fresh;assert(fresh.masks.empty() && !fresh.Add(frame.draws[0]));
  UiFrame partial;partial.mask_pending=1;partial.Seal();assert(!partial.complete);
  UiDraw state;state.surface_info=0x0A000280;state.colour_info=0x5A0;
  state.state.read=true;state.state.depth_control=0x00708764;state.state.blend_control=0x07060706;
  state.state.color_control=4;state.state.color_mask=15;state.state.su_sc_mode=0x00218000;
  state.state.vte_cntl=0x43F;state.clip_control=0x80000;
  const float viewport[]={320,320,-180,180};std::copy(viewport,viewport+4,state.viewport);
  assert(UiMaskState(state));state.state.depth_control|=2;assert(!UiMaskState(state));state.state.depth_control&=~2u;
  state.colour_info^=1;assert(!UiMaskState(state));state.colour_info^=1;
  state.viewport[0]=640;assert(!UiMaskState(state));
  if(argc>=2) {
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    uint32_t cases=0;input.read(reinterpret_cast<char*>(&cases),4);assert(cases>=4 && cases<1000);
    for(uint32_t i=0;i<cases;i++) {
      uint32_t count=0,base=0,accepted=0;input.read(reinterpret_cast<char*>(&count),4);input.read(reinterpret_cast<char*>(&base),4);input.read(reinterpret_cast<char*>(&accepted),4);
      assert(count && count<=2048);std::vector<uint8_t> span(count*4);
      input.read(reinterpret_cast<char*>(span.data()),span.size());assert(input);
      const bool actual=copy.Read(span.data(),count);
      if(actual!=bool(accepted) || (actual && copy.base!=base)) {std::cerr<<"Captured copy case "<<i<<", dwords "<<count<<", base "<<std::hex<<base<<" mismatch\n";return 1;}
    }
    std::cout<<"Captured mask copy spans: "<<cases<<" PASS\n";
  }
  if(argc==3) {
    std::ifstream input(argv[2],std::ios::binary);assert(input);
    uint32_t count=0;input.read(reinterpret_cast<char*>(&count),4);assert(count==512);
    for(uint32_t i=0;i<count;i++) {
      float values[28]{};input.read(reinterpret_cast<char*>(values),sizeof(values));assert(input);
      UiVertex vertex;assert(UiMaskVertexOf(values+16,values+20,reinterpret_cast<const float (*)[4]>(values),&vertex));
      for(uint32_t k=0;k<4;k++) {
        float scale=std::abs(values[4*k+3]);
        for(uint32_t col=0;col<3;col++) scale+=std::abs(values[4*k+col]*values[16+col]);
        assert(std::abs(vertex.clip[k]-values[22+k])<=2e-6f*std::max(1.f,scale));
      }
      for(uint32_t k=0;k<2;k++) assert(vertex.uv[k]==values[26+k]);
    }
    std::cout<<"Native mask vertices: 512 independent interpreter fixtures PASS\n";
  }
  float p[]={1,2,3,std::numeric_limits<float>::quiet_NaN()},uv[]={0,1},matrix[4][4]{};UiVertex vertex;
  for(uint32_t k=0;k<4;k++) matrix[k][k]=1;
  assert(UiMaskVertexOf(p,uv,matrix,&vertex) && vertex.clip[3]==1);
  p[0]=std::numeric_limits<float>::quiet_NaN();assert(!UiMaskVertexOf(p,uv,matrix,&vertex));p[0]=1;
  uv[0]=65;assert(!UiMaskVertexOf(p,uv,matrix,&vertex));uv[0]=0;
  matrix[0][0]=std::numeric_limits<float>::infinity();assert(!UiMaskVertexOf(p,uv,matrix,&vertex));
  assert(!UiMaskVertexOf(nullptr,uv,matrix,&vertex));
  std::cout<<"UI mask ownership/bounds: PASS\n";
}
