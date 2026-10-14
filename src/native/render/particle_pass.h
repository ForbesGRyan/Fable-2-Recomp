#pragma once
#include "../capture/presentation_frame.h"
#include "texture_cache.h"
#include "clay_logic.h"
namespace fable2::native::render {
// Own textures and programs, borrowing only native world render targets.
class ParticlePass {
 public:
  bool Render(nrhi::Cmd* cmd,nrhi::Device* dev,const capture::UiFrame& frame,
              nrhi::Texture* colour,nrhi::Texture* depth,float exposure);
  void Release(nrhi::Device* dev);
  nrhi::TextureView* Effects(uint64_t frame) const {return half_frame_==frame ? half_view_:nullptr;}
 private:
  bool RenderGroup(nrhi::Cmd*,nrhi::Device*,const capture::UiFrame&,nrhi::Texture*,nrhi::Texture*,float,bool);
  bool EnsureHalf(nrhi::Device* dev,nrhi::Texture* depth);
  bool Ensure(nrhi::Device* dev);
  bool EnsureDepth(nrhi::Device* dev,nrhi::Texture* source);
  nrhi::Pipeline* Pipeline(const capture::UiDraw& draw);
  nrhi::Device* device_=nullptr;
  nrhi::BindingLayout* layout_=nullptr;
  nrhi::Shader* vs_=nullptr;
  nrhi::Shader* ps_=nullptr;
  nrhi::Shader* soft_ps_=nullptr;
  nrhi::Shader* full_vs_=nullptr;
  nrhi::Shader* copy_ps_=nullptr;
  nrhi::Shader* snow_vs_=nullptr;
  nrhi::Shader* snow_ps_=nullptr;
  nrhi::Pipeline* copy_pipeline_=nullptr;
  nrhi::Pipeline* half_depth_pipeline_=nullptr;
  nrhi::Shader* half_depth_ps_=nullptr;
  nrhi::Texture* half_colour_=nullptr;
  nrhi::Texture* half_depth_=nullptr;
  nrhi::TextureView* half_view_=nullptr;
  uint64_t half_frame_=0;
  nrhi::Texture* depth_copy_=nullptr;
  nrhi::Texture* depth_source_=nullptr;  // borrowed native world target
  nrhi::TextureView* depth_copy_view_=nullptr;
  nrhi::TextureView* depth_source_view_=nullptr;
  struct Entry {PipelineKey key;bool depth_clip;bool half;nrhi::Pipeline* pipeline;};
  std::vector<Entry> pipelines_;
  bool failed_=false;
  uint32_t lines_=0;
  TextureCache textures_{64ull<<20};
};
}
