#pragma once
#include "texture_cache.h"
#include "clay_logic.h"
namespace fable2::native::render {
// Native scene/depth only. Three current-frame HDR steps precede F51's composite.
class DofPass {
 public:
  bool Render(nrhi::Cmd*,nrhi::Device*,const capture::DofBlock&,nrhi::Shader* vertex,
              nrhi::Texture* depth,nrhi::TextureView* scene,nrhi::TextureView* bloom,
              nrhi::Texture* front,nrhi::Buffer* post_constants,uint64_t post_offset);
  void Release(nrhi::Device*);
  bool failed() const {return failed_;}
 private:
  bool Ensure(nrhi::Device*,nrhi::Shader*,nrhi::Texture*);
  nrhi::Device* device_=nullptr;
  nrhi::BindingLayout* layout_=nullptr;
  nrhi::Shader* shaders_[3]{};
  nrhi::Pipeline* pipelines_[3]{};
  nrhi::Texture* targets_[2]{};
  nrhi::TextureView* views_[2]{};
  nrhi::Texture* depth_source_=nullptr; // borrowed, never destroyed
  nrhi::TextureView* depth_view_=nullptr;
  bool failed_=false;
  uint32_t lines_=0;
};
}
