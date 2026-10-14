#pragma once

// Closed 29B6506F billboard vertex formula. Inputs are owned at the draw;
// relative curve addressing is checked before reading the register bank.
#include "presentation_frame.h"

namespace fable2::native::capture {
// Native world depth has no stencil consumer. An always comparison cannot
// reject particle colour/depth; any narrower comparison remains unsupported.
inline bool ParticleStencilSupported(uint32_t depth) {
  return !(depth&1) || (((depth>>8)&7)==7 && (!(depth&128) || ((depth>>20)&7)==7));
}
// vfetch uses trunc(vertex_index * .25), not the quad vertex's own index.
inline bool ParticleStreamSpan(uint32_t start,uint32_t count,uint32_t available,uint32_t* first,uint32_t* bytes) {
  if(!first || !bytes || !count || count>4096 || start%4 || count%4 || uint64_t(start)+count>65536) return false;
  const uint64_t offset=uint64_t(start/4)*48,extent=uint64_t(count/4)*48;
  if(offset+extent>available) return false;
  *first=uint32_t(offset);*bytes=uint32_t(extent);return true;
}
inline bool ParticleVertexOf(uint32_t index, const float position[4], const float shape[4],
                             const float (*c)[4], UiVertex* out) {
  if (!c || !out || index >= 65536) return false;
  // Fetch swizzle 0xA88 supplies W=1; record word 3 is unused storage.
  for (uint32_t i=0;i<3;i++) if(!UiFinite(position[i])) return false;
  for (uint32_t i=0;i<4;i++) if(!UiFinite(shape[i])) return false;
  if (shape[3]<0 || shape[3]>=99) return false;
  const uint32_t first=uint32_t(shape[3]);
  const float fraction=shape[3]-std::floor(shape[3]);
  constexpr uint32_t required[]={0,1,2,3,8,46,47,72,73,74,76,77,78,79,83,84,85,86,90,113,114,115,253,254,255};
  for(uint32_t reg:required) for(float value:std::array<float,4>{c[reg][0],c[reg][1],c[reg][2],c[reg][3]})
    if(!UiFinite(value)) return false;
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
  const float u=corner==1 || corner==2 ? 1.0f : 0.0f;
  const float v=corner==2 || corner==3 ? 1.0f : 0.0f;
  const float cycle=shape[2]*c[254][3]+c[255][1];
  const float angle=(cycle-std::floor(cycle))*c[254][1]+c[255][3];
  const float x=(u+c[253][0])*shape[0]*size[0],y=(v+c[253][0])*shape[1]*size[1];
  const float rx=(x*std::cos(angle)-y*std::sin(angle))*stretch[0];
  const float ry=(x*std::sin(angle)+y*std::cos(angle))*stretch[1];
  float center[3],world[3],normal[3],offset[3],eye[3];
  const auto dot=[](const float* a,const float* b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
  const auto normalize=[&](float* value) {
    const float length=std::sqrt(dot(value,value));
    if(!UiFinite(length)) return false;
    for(uint32_t i=0;i<3;i++) value[i]=length>0 ? value[i]/length : 0;
    return true;
  };
  for(uint32_t i=0;i<3;i++) {
    center[i]=dot(c[72+i],position)+c[72+i][3];
    world[i]=center[i]+rx*c[115][i]+ry*c[114][i];
    offset[i]=world[i]-center[i];eye[i]=c[46][i]-center[i];
  }
  if(!normalize(offset) || !normalize(eye)) return false;
  for(uint32_t i=0;i<3;i++) normal[i]=eye[i]+offset[i]*c[255][1];
  if(!normalize(normal)) return false;
  float distance2=0;
  for(uint32_t i=0;i<3;i++) distance2+=(c[46][i]-world[i])*(c[46][i]-world[i]);
  const float distance=std::sqrt(distance2);
  const auto sat=[](float value) {return std::clamp(value,0.0f,1.0f);};
  UiVertex result;
  result.uv[0]=u;result.uv[1]=1-v;result.uv[2]=size[3];
  result.colour[3]=sat((distance-c[47][0])*c[47][2])*sat((c[113][0]-distance)*c[113][1])*c[113][2]*colour[3];
  float lit[3]={c[8][0],c[8][1],c[8][2]};
  for(uint32_t light=0;light<4;light++) {
    float direction[3];
    for(uint32_t i=0;i<3;i++) direction[i]=c[76+light][i]-world[i]*c[76+light][3];
    const float length2=dot(direction,direction);
    if(!normalize(direction)) return false;
    const float denominator=1+c[253][1]*c[90][light]*length2;
    if(!UiFinite(denominator) || denominator<=0) return false;
    const float factor=sat(dot(direction,normal))/denominator;
    for(uint32_t i=0;i<3;i++) lit[i]+=c[83+light][i]*factor;
  }
  for(uint32_t i=0;i<3;i++) {
    result.colour[i]=lit[i]*(1-size[2])+colour[i]*size[2];
    result.extra[i]=world[i];
  }
  for(uint32_t i=0;i<4;i++) result.clip[i]=dot(c[i],world)+c[i][3];
  for(const float* values:{result.clip,result.uv,result.colour,result.extra})
    for(uint32_t i=0;i<4;i++) if(!UiFinite(values[i])) return false;
  *out=result;return true;
}
}  // namespace fable2::native::capture
