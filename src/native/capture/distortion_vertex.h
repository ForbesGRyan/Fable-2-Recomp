#pragma once

// Closed 00D85B79 vertex formula for the separate distortion producer.
// This is not yet published by capture: target/depth dependencies must be
// established before the consumer can be enabled. Never feed guide effects.
#include "presentation_frame.h"
#include <cstring>

namespace fable2::native::capture {
inline bool DistortionVertexOf(uint32_t index, const float position[4],
                               const float shape[4], const float (*c)[4], UiVertex* out) {
  if(!position || !shape || !c || !out || index>=65536) return false;
  for(uint32_t i=0;i<3;i++) if(!UiFinite(position[i])) return false;
  for(uint32_t i=0;i<4;i++) if(!UiFinite(shape[i])) return false;
  if(shape[3]<0 || shape[3]>=99) return false;
  constexpr float literals[3][4]={{4,6.28318548f,1,.159154937f},
                                {.25f,.5f,-2,-3.14159274f},{-.5f,0,0,0}};
  if(std::memcmp(c+253,literals,sizeof(literals))) return false;
  constexpr uint32_t required[]={0,1,2,3,8,46,47,72,73,113,114,115};
  for(uint32_t row:required) for(uint32_t i=0;i<4;i++) if(!UiFinite(c[row][i])) return false;
  const uint32_t first=uint32_t(shape[3]);
  const float fraction=shape[3]-std::floor(shape[3]);
  float colour[4],size[4],stretch[4];
  const auto curve=[&](uint32_t base,float* result) {
    for(uint32_t i=0;i<4;i++) {
      if(!UiFinite(c[base+first][i]) || !UiFinite(c[base+first+1][i])) return false;
      result[i]=c[base+first][i]*(1-fraction)+c[base+first+1][i]*fraction;
    }
    return true;
  };
  if(!curve(116,colour) || !curve(136,size) || !curve(156,stretch)) return false;
  const uint32_t corner=index%4;
  const float u=corner==1 || corner==2 ? 1.f:0.f;
  const float v=corner==2 || corner==3 ? 1.f:0.f;
  const float cycle=shape[2]*c[253][3]+c[254][1];
  const float angle=(cycle-std::floor(cycle))*c[253][1]+c[254][3];
  const float x=(u+c[255][0])*shape[0]*size[0],y=(v+c[255][0])*shape[1]*size[1];
  const float rx=(x*std::cos(angle)-y*std::sin(angle))*stretch[0];
  const float ry=(x*std::sin(angle)+y*std::cos(angle))*stretch[1];
  const auto dot=[](const float* a,const float* b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
  float world[3],distance2=0;
  for(uint32_t i=0;i<3;i++) {
    world[i]=dot(c[113+i],position)+c[113+i][3]+rx*c[73][i]+ry*c[72][i];
    distance2+=(c[8][i]-world[i])*(c[8][i]-world[i]);
  }
  const float distance=std::sqrt(distance2);
  const auto sat=[](float value) {return std::clamp(value,0.f,1.f);};
  const float fade=sat((distance-c[46][0])*c[46][2])*sat((c[47][0]-distance)*c[47][1])*c[47][2];
  UiVertex result;
  result.uv[0]=u;result.uv[1]=1-v;result.uv[2]=size[3];
  for(uint32_t i=0;i<4;i++) {
    result.clip[i]=dot(c[i],world)+c[i][3];result.colour[i]=fade*colour[i];
  }
  std::copy(world,world+3,result.extra);
  for(const float* values:{result.clip,result.uv,result.colour,result.extra})
    for(uint32_t i=0;i<4;i++) if(!UiFinite(values[i])) return false;
  *out=result;return true;
}
}  // namespace fable2::native::capture
