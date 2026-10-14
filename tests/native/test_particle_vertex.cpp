#include "../../src/native/capture/particle_vertex.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
using namespace fable2::native::capture;
int main(int argc,char** argv) {
  assert(ParticleStencilSupported(0x00708763));
  assert(ParticleStencilSupported(0));
  assert(!ParticleStencilSupported(0x00708063));
  assert(!ParticleStencilSupported(0x006087E3));
  uint32_t first=0,bytes=0;
  assert(ParticleStreamSpan(0,160,40*48,&first,&bytes) && first==0 && bytes==40*48);
  assert(ParticleStreamSpan(8,4,3*48,&first,&bytes) && first==2*48 && bytes==48);
  assert(!ParticleStreamSpan(8,4,3*48-1,&first,&bytes));
  assert(!ParticleStreamSpan(1,4,48,&first,&bytes));assert(!ParticleStreamSpan(0,3,48,&first,&bytes));
  assert(!ParticleStreamSpan(UINT32_MAX,4,UINT32_MAX,&first,&bytes));
  assert(argc==2);
  std::ifstream input(argv[1],std::ios::binary);assert(input);
  uint32_t trials=0;input.read(reinterpret_cast<char*>(&trials),4);assert(trials==512);
  for(uint32_t trial=0;trial<trials;trial++) {
    uint32_t index;float bank[256][4],position[4],shape[4],expected[12];
    input.read(reinterpret_cast<char*>(&index),4);
    input.read(reinterpret_cast<char*>(bank),sizeof(bank));
    input.read(reinterpret_cast<char*>(position),sizeof(position));input.read(reinterpret_cast<char*>(shape),sizeof(shape));
    input.read(reinterpret_cast<char*>(expected),sizeof(expected));assert(input);
    UiVertex actual;assert(ParticleVertexOf(index,position,shape,bank,&actual));
    position[3]=std::numeric_limits<float>::quiet_NaN();
    assert(ParticleVertexOf(index,position,shape,bank,&actual));
    const float* outputs[]={actual.clip,actual.uv,actual.colour};
    for(uint32_t row=0;row<3;row++) for(uint32_t col=0;col<4;col++) {
      float got=outputs[row][col],want=expected[row*4+col];
      assert(std::abs(got-want)<=3e-4f*std::max({1.0f,std::abs(got),std::abs(want)}));
    }
    auto bad=shape[3];shape[3]=99;assert(!ParticleVertexOf(index,position,shape,bank,&actual));shape[3]=bad;
    assert(!ParticleVertexOf(65536,position,shape,bank,&actual));
    bank[156+uint32_t(shape[3])][0]=std::numeric_limits<float>::quiet_NaN();
    assert(!ParticleVertexOf(index,position,shape,bank,&actual));
  }
  UiFrame frame;UiDraw draw;draw.vertices.resize(6);assert(frame.AddParticle(draw));
  frame.particle_vertices=32768;assert(!frame.AddParticle(draw));assert(frame.complete && frame.particle_refused==1);
  std::cout<<"Particles: 512 independent microcode fixtures and rejection bounds pass\n";
}
