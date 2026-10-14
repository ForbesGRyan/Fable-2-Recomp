#pragma once
#include "post_block.h"

namespace fable2::native::capture {
inline constexpr uint64_t kPostDofPs=0xF51AF7DB09EDED71ull;
inline constexpr uint64_t kPostDownsamplePs=0x87E85199E1E9B260ull;
// Pins from run 829. Only the physical base may vary. Keep sampler, sign,
// swizzle, dimensions and format together; an HDR texture alone is not a producer.
inline constexpr uint32_t kDofSceneFetch[6]={0x88C04956,0x0000005D,0x0059E45F,0x01280D11,3,0x218};
inline constexpr uint32_t kDofDownFetch[6]={0x88C24956,0x0000005D,0x0059E45F,0x01000D11,0,0x218};
inline constexpr uint32_t kDofDepthFetch[6]={0x88C04802,0x00000097,0x0059E45F,0x01000D11,0,0x218};
inline constexpr uint32_t kDofBloomFetch[6]={0x82424802,0x000000B6,0x00166117,0x01280C14,3,0x218};
inline constexpr uint32_t kDofBlurFetch[6]={0x82424956,0x0000005D,0x00166117,0x01000D11,0,0x218};
inline constexpr uint32_t kDofResultFetch[6]={0x82424956,0x0000005D,0x00166117,0x01280D11,3,0x218};
inline bool DofFetchMatches(const uint32_t* fetch,const uint32_t* pin) {
  if(!fetch || !(fetch[1]&0xFFFFF000u)) return false;
  for(uint32_t i=0;i<6;i++) if((i==1 ? fetch[i]&0xFFFu:fetch[i])!=pin[i]) return false;
  return true;
}
struct DofChain {
  uint32_t scene[6]{},down[6]{},blur_source[2][6]{};
  float weights[2][2][4]{};
  bool have_scene=false,have_down=false,have_blur[2]{};
};
struct DofBlock {
  float projection[2]{};
  float focus[4]{}; // near slope/distance, far distance/slope; finite after validation
  float caps[2]{};
  float blur[2][7]{};
  bool valid=false;
};
inline void RecordDofBlur(DofChain& chain,uint32_t axis,const uint32_t* fetch,const float (*weights)[4]) {
  if(axis>=2) return;
  chain.have_blur[axis]=DofFetchMatches(fetch,kDofBlurFetch) && weights;
  if(chain.have_blur[axis]) {
    std::memcpy(chain.blur_source[axis],fetch,sizeof(chain.blur_source[axis]));
    std::memcpy(chain.weights[axis],weights,sizeof(chain.weights[axis]));
  }
}
inline DofBlock MakeDofBlock(const DofChain& chain,const uint32_t fetch[5][6],
                           const float* projection,const float* focus,const float* caps) {
  DofBlock result{};
  if(!fetch || !projection || !focus || !caps || !chain.have_scene || !chain.have_down ||
     !chain.have_blur[0] || !chain.have_blur[1] ||
     !DofFetchMatches(chain.scene,kDofDownFetch) || !DofFetchMatches(chain.down,kDofBlurFetch) ||
     !DofFetchMatches(chain.blur_source[0],kDofBlurFetch) || !DofFetchMatches(chain.blur_source[1],kDofBlurFetch) ||
     !DofFetchMatches(fetch[0],kDofSceneFetch) || !DofFetchMatches(fetch[1],kDofDepthFetch) ||
     !DofFetchMatches(fetch[3],kDofBloomFetch) || !DofFetchMatches(fetch[4],kDofResultFetch) ||
     PostTableFetchFault(fetch[2])!=FogTableFault::kNone) return result;
  const auto base=[](const uint32_t* f){return f[1]&0xFFFFF000u;};
  if(base(chain.scene)!=base(fetch[0]) || base(chain.down)!=base(chain.blur_source[0]) ||
     base(chain.blur_source[1])!=base(fetch[4]) || base(chain.blur_source[0])==base(chain.blur_source[1]) ||
     base(fetch[3])==base(fetch[4])) return result;
  // Guest depth is [0,1]. X>1 and Y<0 guarantee positive, finite distance
  // throughout that interval, without a reciprocal singularity.
  if(!(projection[0]>1 && projection[0]<=2) ||
     !(projection[1]<=-1e-5f && projection[1]>=-65536)) return result;
  for(uint32_t i=0;i<2;i++) if(!(caps[i]>=0 && caps[i]<=1)) return result;
  if(!(focus[1]>=0 && focus[1]<=1e7f) || !(focus[2]>=focus[1] && focus[2]<=1e7f)) return result;
  for(uint32_t i=0;i<4;i++) result.focus[i]=focus[i];
  for(uint32_t i=0;i<2;i++) {
    const uint32_t slope=i ? 3:0;
    if(std::isinf(focus[slope]) && focus[slope]>0 && caps[i]==0) result.focus[slope]=0;
    else if(!(focus[slope]>=0 && focus[slope]<=1e6f)) return DofBlock{};
    const auto& w=chain.weights[i];
    const float values[7]={w[0][0],w[0][1],w[0][2],w[0][3],w[1][0],w[1][1],w[1][2]};
    double total=values[0];
    for(uint32_t j=0;j<7;j++) {
      if(!(values[j]>=0 && values[j]<=1)) return DofBlock{};
      if(j) total+=2*values[j];
      result.blur[i][j]=values[j];
    }
    if(std::abs(total-1)>1e-3) return DofBlock{};
    result.projection[i]=projection[i];result.caps[i]=caps[i];
  }
  result.valid=true;return result;
}
}
