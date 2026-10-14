#pragma once

// Frame-owned presentation inputs. Independent of the world tiling bracket:
// menu sprites can be tiled; a movie can have no world records at all.
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "render_state.h"
#include "dof_block.h"
#include "../render/texture_decode.h"

namespace fable2::native::capture {
enum class UiKind : uint32_t { kSprite = 1, kText, kVideo, kParticle, kFade };
inline constexpr uint32_t kUiKindCount=5;
inline constexpr uint64_t kUiSpriteVs = 0x7B22146D51E0E4F5ull;
inline constexpr uint64_t kUiSpritePs = 0x0542FFDF2B118509ull;
inline constexpr uint64_t kUiTextVs = 0x901891D9B86C181Aull;
inline constexpr uint64_t kUiTextPs = 0x94C373AB309F37DBull;
inline constexpr uint64_t kUiVideoVs = 0xBD2477FEDE6BEAB1ull;
inline constexpr uint64_t kUiVideoPs = 0x130DCA19B2727F68ull;
inline constexpr uint64_t kUiFadePs = 0x95503A81AE52EA05ull;
inline constexpr bool UiUnsupportedWorldPost(uint64_t hash) {
  return hash==0x577CB5267A8C92C3ull || hash==0x15675EB73FB39170ull;
}
inline constexpr uint32_t kUiMaxDraws = 1024;
inline constexpr uint32_t kUiMaxVertices = 65536;
inline constexpr uint32_t kUiMaxDrawVertices = 16384;
inline constexpr uint32_t kUiMaxPlaneBytes = 4u << 20;
inline constexpr uint32_t kUiMaxFramePlaneBytes = 16u << 20;
inline constexpr uint32_t kUiMaxAssetBytes = 32u << 20, kUiMaxAssets = 256;
inline constexpr uint32_t kUiMaxMasks=8;

inline constexpr UiKind UiShaderKind(uint64_t vs, uint64_t ps) {
  return vs == kUiSpriteVs && ps == kUiSpritePs ? UiKind::kSprite :
         vs == kUiTextVs && ps == kUiTextPs ? UiKind::kText :
         vs == kUiVideoVs && ps == kUiVideoPs ? UiKind::kVideo :
         vs == kUiVideoVs && ps == kUiFadePs ? UiKind::kFade : UiKind(0);
}
inline bool UiFinite(float value, float bound = 1.0e7f) {
  return std::isfinite(value) && std::abs(value) <= bound;
}
struct UiVertex {
  float clip[4]{};
  float uv[4]{};
  float colour[4]{};
  float extra[4]{};
};
static_assert(sizeof(UiVertex) == 64);
struct UiPlane {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> pixels;  // untiled, endian-correct, frame-owned K8
};
struct WeatherConstants {float c[256][4]{};};
struct UiDraw {
  UiKind kind{};
  uint32_t particle_textures=0;
  bool particle_soft=false;
  bool particle_half=false;
  uint32_t point_first=0,point_count=0,point_minmax=0;
  std::shared_ptr<const WeatherConstants> weather;
  float particle_projection[5][4]{};  // PS c0 and inverse projection c116..119
  uint32_t colour_info = 0, surface_info = 0;
  uint32_t vertex_control=0,clip_control=(1u<<19)|(1u<<16);
  StateWords state;
  float registers[9][4]{};  // c0,c30,c31,c46,c47,c77,c253,c254,c255
  uint32_t fetch[3][6]{};
  float viewport[4]{};  // XSCALE,XOFFSET,YSCALE,YOFFSET in guest pixels
  uint32_t screen_scissor[2]{};
  uint32_t window_offset=0;
  uint32_t window_scissor[2]{0,0x3FFF3FFF};
  std::vector<UiVertex> vertices;  // triangle list, no guest pointer retained
  UiPlane planes[3];
  std::shared_ptr<const render::TextureSnapshot> textures[3];
  uint32_t masks[3]{UINT32_MAX,UINT32_MAX,UINT32_MAX}; // own-frame native producer indices
};
struct UiMask {
  uint32_t base=0;
  float clear[4]{};
  // The game's clear: one rectangle {x1,y1,x2,y2} of the 640x360 target drawn
  // with the colour under RB_BLENDCONTROL0 (One/Zero 00010001, or the copy
  // quad's SrcAlpha/InvSrcAlpha 07060706, which with alpha 0 keeps what the
  // previous mask of the frame left there). The Maps view clears the whole
  // target; the pause menu clears rows 352-360 under each mask. Masks of a
  // frame share one EDRAM target in sequence and are resolved one by one.
  int32_t clear_rect[4]{0,0,640,360};
  uint32_t clear_blend=0x00010001;
  UiDraw draw;
};
struct UiAsset {
  std::array<uint32_t,6> fetch;
  std::shared_ptr<const render::TextureSnapshot> source;
};
// The observed depth producer is an indexed fullscreen rectangle, with UV
// supplied by its own vertex data. Refuse another mesh instead of inventing UV.
inline bool UiEffectDepthQuad(const std::vector<UiVertex>& vertices) {
  if(vertices.size()!=6) return false;
  constexpr float corners[4][5]={{-1,-1,.5f,0,1},{-1,1,.5f,0,0},
                               {1,-1,.5f,1,1},{1,1,.5f,1,0}};
  constexpr uint32_t indices[6]={0,1,2,1,3,2};
  for(uint32_t i=0;i<6;i++) {
    const auto& v=vertices[i];const float* expected=corners[indices[i]];
    for(uint32_t k=0;k<3;k++) if(v.clip[k]!=expected[k]) return false;
    if(v.clip[3]!=1 || v.uv[0]!=expected[3] || v.uv[1]!=expected[4]) return false;
  }
  return true;
}
inline int32_t UiSigned15(uint32_t value) {return int32_t((value&32767)^16384)-16384;}
// PA_CL_CLIP_CNTL: disabled clipping also disables user planes in the SDK.
// Enabled user planes and OpenGL -W..W depth require a different vertex path.
inline bool UiClipSupported(uint32_t control) {
  return (control&(1u<<16)) || (!(control&63) && (control&(1u<<19)));
}
inline bool UiDepthClip(uint32_t control) {return !(control&(1u<<16));}
inline std::array<int32_t,4> UiScissorOf(const UiDraw& draw,int32_t width,int32_t height,bool half_pixel=false) {
  const int32_t x=(draw.window_scissor[0]&0x80000000) ? 0:UiSigned15(draw.window_offset);
  const int32_t y=(draw.window_scissor[0]&0x80000000) ? 0:UiSigned15(draw.window_offset>>16);
  int32_t left=std::clamp(std::max(int32_t(draw.window_scissor[0]&16383)+x,UiSigned15(draw.screen_scissor[0])),0,width);
  int32_t top=std::clamp(std::max(int32_t((draw.window_scissor[0]>>16)&16383)+y,UiSigned15(draw.screen_scissor[0]>>16)),0,height);
  int32_t right=std::clamp(std::min(int32_t(draw.window_scissor[1]&16383)+x,UiSigned15(draw.screen_scissor[1])),left,width);
  int32_t bottom=std::clamp(std::min(int32_t((draw.window_scissor[1]>>16)&16383)+y,UiSigned15(draw.screen_scissor[1]>>16)),top,height);
  // An enabled clipper limits XY to the original viewport as well as the
  // window/screen scissor. The native pass uses a full-target host viewport.
  // SDK draw.cpp crops its host viewport at these integer bounds, including
  // the window and half-pixel additions before integer truncation.
  if(UiDepthClip(draw.clip_control)) {
    int32_t* low[]={&left,&top};int32_t* high[]={&right,&bottom};
    const int32_t extent[]={width,height};
    for(uint32_t axis=0;axis<2;axis++) {
      const uint32_t component=axis*2;
      const float scale=draw.state.vte_cntl&(1u<<component) ? draw.viewport[component]:1;
      float offset=draw.state.vte_cntl&(2u<<component) ? draw.viewport[component+1]:0;
      if(draw.state.su_sc_mode&(1u<<16)) offset+=float(UiSigned15(draw.window_offset>>(axis*16)));
      if(half_pixel && !(draw.vertex_control&1)) offset+=.5f;
      if(!UiFinite(scale,16384) || !UiFinite(offset,32768)) return {0,0,0,0};
      const int32_t a=int32_t(std::clamp(offset-std::abs(scale),0.f,float(extent[axis])));
      const int32_t b=int32_t(std::clamp(offset+std::abs(scale),0.f,float(extent[axis])));
      *low[axis]=std::max(*low[axis],a);*high[axis]=std::max(*low[axis],std::min(*high[axis],b));
    }
  }
  return {left,top,right,bottom};
}
inline std::array<float,4> UiViewportOf(const UiDraw& draw,bool half_pixel) {
  std::array<float,4> out={draw.state.vte_cntl&1 ? draw.viewport[0]:1,
    draw.state.vte_cntl&2 ? draw.viewport[1]:0,draw.state.vte_cntl&4 ? draw.viewport[2]:1,
    draw.state.vte_cntl&8 ? draw.viewport[3]:0};
  if(draw.state.su_sc_mode&(1u<<16)) {out[1]+=float(UiSigned15(draw.window_offset));out[3]+=float(UiSigned15(draw.window_offset>>16));}
  if(half_pixel && !(draw.vertex_control&1)) {out[1]+=.5f;out[3]+=.5f;}
  return out;
}
struct UiFrame {
  uint64_t frame = 0;
  bool enabled = false, complete = true, menu = false;
  bool menu_float10=false;
  uint32_t refused = 0, vertices = 0, plane_bytes = 0, pending = 0;
  float menu_power = 1.0f, previous_share = 0.0f;
  float effect_gain=0;
  uint64_t unsupported_world_post=0; // current frame only; requires a separate distortion producer
  bool dof_requested=false;
  DofBlock dof;
  std::shared_ptr<const UiDraw> effect_depth;
  std::vector<UiDraw> draws;
  std::vector<UiMask> masks;
  uint32_t mask_pending=0;
  // Draws of the mask shader pair that are not UI mask producers (other target
  // sizes/blend, e.g. small per-texture passes): counted, not owned. A consumer
  // whose mask base is not an owned producer still refuses (fail closed).
  uint32_t foreign_masks=0;
  // World billboards are drawn with the native world's depth, before post.
  std::vector<UiDraw> world_particles;
  uint32_t particle_vertices=0, particle_refused=0, particle_unsupported=0,weather_points=0,weather_draws=0;
  std::vector<UiAsset> assets;
  uint32_t asset_bytes=0;
  bool standalone() const {
    if(menu) return true;
    for(const auto& draw:draws) if(draw.kind==UiKind::kVideo) return true;
    return false;
  }
  bool world_post_supported() const {return !unsupported_world_post || standalone();}
  bool Begin() {
    if(!complete || draws.size()+pending>=kUiMaxDraws) {Refuse();return false;}
    ++pending;return true;
  }
  void Seal() { if(pending || mask_pending) Refuse(); }
  std::shared_ptr<const render::TextureSnapshot> ReuseTexture(
      const uint32_t fetch[6],const render::TextureSnapshot& source) const {
    for(const auto& asset:assets) {
      if(std::equal(asset.fetch.begin(),asset.fetch.end(),fetch) && asset.source &&
         asset.source->hash==source.hash && asset.source->base==source.base &&
         asset.source->mips==source.mips) return asset.source;
    }
    return nullptr;
  }
  bool HoldTexture(const uint32_t fetch[6],std::shared_ptr<const render::TextureSnapshot> source) {
    const uint64_t bytes=source ? source->base.size()+source->mips.size() : 0;
    if(!complete || !bytes || asset_bytes>kUiMaxAssetBytes || bytes>kUiMaxAssetBytes-asset_bytes || assets.size()>=kUiMaxAssets) {
      Refuse();return false;
    }
    UiAsset asset;std::copy(fetch,fetch+6,asset.fetch.begin());asset.source=std::move(source);
    assets.push_back(std::move(asset));asset_bytes+=uint32_t(bytes);return true;
  }
  bool Add(UiDraw draw) {
    for(uint32_t slot=0;slot<3;slot++) if(draw.masks[slot]!=UINT32_MAX) {
      const uint32_t mask=draw.masks[slot];
      if(mask>=masks.size() || masks.size()>kUiMaxMasks || !masks[mask].base ||
         (draw.fetch[slot][1]&~4095u)!=masks[mask].base ||
         !((draw.kind==UiKind::kSprite && slot==2) || (draw.kind==UiKind::kText && slot==1)) ||
         draw.registers[5][0]!=1) {Refuse();return false;}
    }
    uint64_t added_planes=0;
    for(const auto& plane:draw.planes) added_planes+=plane.pixels.size();
    if (!complete || uint32_t(draw.kind)<1 || uint32_t(draw.kind)>kUiKindCount ||
        vertices>kUiMaxVertices || plane_bytes>kUiMaxFramePlaneBytes ||
        added_planes>kUiMaxFramePlaneBytes-plane_bytes ||
        draws.size() >= kUiMaxDraws || draw.vertices.empty() ||
        draw.vertices.size() % 3 || draw.vertices.size() > kUiMaxDrawVertices ||
        draw.vertices.size() > kUiMaxVertices - vertices) {
      Refuse();
      return false;
    }
    vertices += uint32_t(draw.vertices.size());
    plane_bytes += uint32_t(added_planes);
    draws.push_back(std::move(draw));
    return true;
  }
  void Refuse() { complete = false; ++refused; }
  bool AddParticle(UiDraw draw) {
    const bool snow=bool(draw.weather);
    if(world_particles.size()>=128 ||
       (snow && (!draw.vertices.empty() || !draw.point_count || draw.point_count>4096 ||
        uint64_t(draw.point_first)+draw.point_count>65536 || weather_draws>=64 ||
        weather_points>131072 || draw.point_count>131072-weather_points)) ||
       (!snow && (draw.vertices.empty() || draw.vertices.size()%6 ||
        particle_vertices>32768 || draw.vertices.size()>32768-particle_vertices))) {
      ++particle_refused;return false;
    }
    if(snow) {weather_points+=draw.point_count;++weather_draws;}
    particle_vertices+=uint32_t(draw.vertices.size());world_particles.push_back(std::move(draw));return true;
  }
};
inline bool UiCompositable(const UiFrame& frame,bool world) {
  return frame.enabled && frame.complete && frame.world_post_supported() &&
    (!frame.draws.empty() || (world && !frame.menu));
}
inline bool UiBlackFade(const float tint[4]) {
  return tint && tint[0]==0 && tint[1]==0 && tint[2]==0 && UiFinite(tint[3],1) && tint[3]>=0;
}
// Invert a monotone scan-out table so already displayed native fallback-world
// pixels can be composed in the same pre-ramp domain as the UI. Never apply the
// display transfer twice, even when the world's post block is absent.
inline bool UiInverseRamp(const float (*ramp)[3],float (*inverse)[4]) {
  if(!ramp || !inverse) return false;
  for(uint32_t channel=0;channel<3;channel++) {
    for(uint32_t i=0;i<256;i++) {
      if(!UiFinite(ramp[i][channel],1) || ramp[i][channel]<0 ||
         (i && ramp[i][channel]<ramp[i-1][channel])) return false;
    }
    uint32_t hi=0;
    for(uint32_t i=0;i<256;i++) {
      const float value=float(i)/255;
      while(hi<255 && ramp[hi][channel]<value) ++hi;
      float result=float(hi)/255;
      if(hi && ramp[hi][channel]>ramp[hi-1][channel])
        result=(float(hi-1)+std::clamp((value-ramp[hi-1][channel])/
          (ramp[hi][channel]-ramp[hi-1][channel]),0.0f,1.0f))/255;
      inverse[i][channel]=result;
    }
  }
  return true;
}
class UiPublisher {
 public:
  void Publish(std::shared_ptr<const UiFrame> frame) {
    std::lock_guard<std::mutex> guard(mutex_);
    latest_ = std::move(frame);
  }
  std::shared_ptr<const UiFrame> Latest() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return latest_;
  }
 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const UiFrame> latest_;
};
UiPublisher& PresentationPublisher();
void SetPresentationWanted(bool wanted);
}  // namespace fable2::native::capture
