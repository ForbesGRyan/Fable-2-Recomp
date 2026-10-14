#include "../../src/native/capture/distortion_vertex.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
using namespace fable2::native::capture;
int main(int argc,char** argv) {
  assert(argc==2);
  std::ifstream input(argv[1],std::ios::binary);assert(input);
  uint32_t trials=0;input.read(reinterpret_cast<char*>(&trials),4);assert(trials==512);
  for(uint32_t trial=0;trial<trials;trial++) {
    uint32_t index;float bank[256][4],position[4],shape[4],expected[12];
    input.read(reinterpret_cast<char*>(&index),4);
    input.read(reinterpret_cast<char*>(bank),sizeof(bank));
    input.read(reinterpret_cast<char*>(position),sizeof(position));
    input.read(reinterpret_cast<char*>(shape),sizeof(shape));
    input.read(reinterpret_cast<char*>(expected),sizeof(expected));assert(input);
    UiVertex actual;assert(DistortionVertexOf(index,position,shape,bank,&actual));
    position[3]=std::numeric_limits<float>::quiet_NaN();
    assert(DistortionVertexOf(index,position,shape,bank,&actual)); // fetch supplies W=1
    const float* output[]={actual.clip,actual.uv,actual.colour};
    for(uint32_t row=0;row<3;row++) for(uint32_t col=0;col<4;col++) {
      const float got=output[row][col],want=expected[row*4+col];
      assert(std::abs(got-want)<=3e-4f*std::max({1.f,std::abs(got),std::abs(want)}));
    }
    const auto age=shape[3];
    for(float bad:{-1.f,99.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
      shape[3]=bad;assert(!DistortionVertexOf(index,position,shape,bank,&actual));
    }
    shape[3]=age;
    assert(!DistortionVertexOf(65536,position,shape,bank,&actual));
    assert(!DistortionVertexOf(index,nullptr,shape,bank,&actual));
    assert(!DistortionVertexOf(index,position,shape,bank,nullptr));
    const float literal=bank[253][0];bank[253][0]=3;
    assert(!DistortionVertexOf(index,position,shape,bank,&actual));bank[253][0]=literal;
    bank[156+uint32_t(age)+1][0]=std::numeric_limits<float>::quiet_NaN();
    assert(!DistortionVertexOf(index,position,shape,bank,&actual));
  }
  std::cout<<"Distortion vertex: 512 independent microcode fixtures and rejection bounds pass\n";
}
