#include "../../src/native/capture/dof_block.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
using namespace fable2::native::capture;
struct Fixture {
  DofChain chain;
  uint32_t fetch[5][6]{};
  float projection[4]={1.000020027f,-.100002006f,0,0};
  float focus[4]={std::numeric_limits<float>::infinity(),0,50,.006666667f};
  float caps[4]={0,.358834f,0,0};
  Fixture() {
    const uint32_t* pins[]={kDofSceneFetch,kDofDepthFetch,nullptr,kDofBloomFetch,kDofResultFetch};
    const uint32_t bases[]={0x199E0000,0x1A02A000,0x1FAA0000,0x12D9F000,0x12DD5000};
    constexpr uint32_t table[6]={0x04000956,0xA4,511,0x010016D1,0,0x18};
    for(uint32_t i=0;i<5;i++) {
      std::copy(pins[i] ? pins[i]:table,(pins[i] ? pins[i]:table)+6,fetch[i]);
      fetch[i][1]|=bases[i];
    }
    std::copy(kDofDownFetch,kDofDownFetch+6,chain.scene);chain.scene[1]|=bases[0];chain.have_scene=true;
    std::copy(kDofBlurFetch,kDofBlurFetch+6,chain.down);chain.down[1]|=0x12D33000;chain.have_down=true;
    const float weights[2][2][4]={{{.228005081f,.193659723f,.118665278f,.052456286f},
      {.016728655f,.003848699f,.000638788f,0}},{{.199675649f,.17621313f,.121109404f,.064825192f},
      {.027023159f,.008773136f,.002218196f,0}}};
    uint32_t source[6];std::copy(chain.down,chain.down+6,source);
    RecordDofBlur(chain,0,source,weights[0]);
    source[1]=(source[1]&0xFFF)|bases[4];RecordDofBlur(chain,1,source,weights[1]);
  }
  DofBlock make() {return MakeDofBlock(chain,fetch,projection,focus,caps);}
};
int main() {
  Fixture f;const auto b=f.make();assert(b.valid && b.focus[0]==0 && b.caps[1]>0);
  assert(b.blur[0][0]==f.chain.weights[0][0][0]);assert(b.blur[1][6]==f.chain.weights[1][1][2]);
  for(uint32_t slot=0;slot<5;slot++) for(uint32_t word=0;word<6;word++) {
    if(slot==2) continue; // Table pin validates its meaningful fields, not reserved bits.
    Fixture bad;bad.fetch[slot][word]^=word==1 ? 1:0x80000000u;assert(!bad.make().valid);
  }
  {Fixture bad;bad.fetch[2][1]^=1;assert(!bad.make().valid);}
  {Fixture bad;bad.fetch[2][2]=510;assert(!bad.make().valid);}
  {Fixture bad;bad.fetch[0][1]+=0x1000;assert(!bad.make().valid);}
  {Fixture bad;bad.fetch[4][1]+=0x1000;assert(!bad.make().valid);}
  {Fixture bad;bad.chain.down[1]+=0x1000;assert(!bad.make().valid);}
  {Fixture bad;bad.chain.blur_source[1][1]=bad.chain.blur_source[0][1];assert(!bad.make().valid);}
  {Fixture bad;bad.chain.have_scene=false;assert(!bad.make().valid);}
  {Fixture bad;bad.chain.have_down=false;assert(!bad.make().valid);}
  for(uint32_t axis=0;axis<2;axis++) {
    Fixture bad;RecordDofBlur(bad.chain,axis,nullptr,nullptr);assert(!bad.make().valid);
    assert(!bad.chain.have_blur[axis]);
    bad=Fixture{};RecordDofBlur(bad.chain,axis,bad.chain.blur_source[axis],nullptr);assert(!bad.make().valid);
    bad=Fixture{};bad.chain.weights[axis][0][0]=0;assert(!bad.make().valid);
    bad=Fixture{};bad.chain.weights[axis][1][2]=std::numeric_limits<float>::quiet_NaN();assert(!bad.make().valid);
  }
  for(float x:{0.f,1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
    Fixture bad;bad.projection[0]=x;assert(!bad.make().valid);
  }
  for(float y:{0.f,1.f,-1e-6f,-65537.f,-std::numeric_limits<float>::infinity()}) {
    Fixture bad;bad.projection[1]=y;assert(!bad.make().valid);
  }
  for(float x:{-1.f,std::numeric_limits<float>::quiet_NaN(),-std::numeric_limits<float>::infinity()}) {
    Fixture bad;bad.focus[0]=x;assert(!bad.make().valid);
  }
  {Fixture bad;bad.caps[0]=.1;assert(!bad.make().valid);}
  {Fixture good;good.focus[0]=1;good.caps[0]=.1;assert(good.make().valid);}
  {Fixture bad;bad.focus[1]=51;assert(!bad.make().valid);}
  {Fixture bad;bad.caps[1]=1.1;assert(!bad.make().valid);}
  {Fixture good;good.focus[3]=std::numeric_limits<float>::infinity();good.caps[1]=0;
    assert(good.make().valid && good.make().focus[3]==0);}
  {DofChain fresh;Fixture old;assert(!MakeDofBlock(fresh,old.fetch,old.projection,old.focus,old.caps).valid);}
  // Every admitted projection endpoint has positive finite reconstructed distance.
  for(float depth:{0.f,.5f,1.f}) {
    float distance=b.projection[1]/(1-b.projection[0]-depth);assert(std::isfinite(distance) && distance>0);
  }
  std::cout<<"DOF capture: producer/source pins, finite domain, zero-cap normalization, kernels and fresh-frame refusal pass\n";
}
