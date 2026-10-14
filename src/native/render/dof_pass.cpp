#include "dof_pass.h"
#include "../fable2_native_shaders.h"
#include <rex/logging.h>
namespace fable2::native::render {
void DofPass::Release(nrhi::Device* dev) {
  if(dev) {
    for(auto p:pipelines_) dev->DestroyDeferred(p);
    for(auto p:shaders_) dev->DestroyDeferred(p);
    for(auto p:views_) dev->DestroyDeferred(p);
    for(auto p:targets_) dev->DestroyDeferred(p);
    dev->DestroyDeferred(depth_view_);
  }
  for(auto& p:pipelines_) p=nullptr;for(auto& p:shaders_) p=nullptr;
  for(auto& p:views_) p=nullptr;for(auto& p:targets_) p=nullptr;
  depth_view_=nullptr;depth_source_=nullptr;device_=nullptr;layout_=nullptr;failed_=false;lines_=0;
}
bool DofPass::Ensure(nrhi::Device* dev,nrhi::Shader* vertex,nrhi::Texture* depth) {
  if(device_ && device_!=dev) Release(nullptr);
  if(failed_ || !dev || !vertex || !depth) return false;
  if(depth_source_ && depth_source_!=depth) {failed_=true;return false;}
  if(pipelines_[2] && views_[0] && views_[1] && depth_view_) return true;
  device_=dev;
  auto fail=[&] {Release(dev);device_=dev;failed_=true;
    REXLOG_ERROR("[native] DOF: resource creation refused; retry required");return false;};
  nrhi::BindingLayoutDesc ld;
  ld.params[0]={nrhi::BindingParamKind::kConstantBuffer,0,1,nrhi::Visibility::kPixel};
  ld.params[1]={nrhi::BindingParamKind::kConstants,1,8,nrhi::Visibility::kPixel};
  ld.params[2]={nrhi::BindingParamKind::kConstantBuffer,2,1,nrhi::Visibility::kPixel};
  ld.params[3]={nrhi::BindingParamKind::kTextureTable,0,4,nrhi::Visibility::kPixel};
  ld.param_count=4;ld.allow_input_layout=false;
  for(uint32_t i=0;i<2;i++) {
    ld.static_samplers[i].shader_register=i;
    ld.static_samplers[i].filter=i ? nrhi::Filter::kLinear:nrhi::Filter::kPoint;
    ld.static_samplers[i].address=nrhi::AddressMode::kClamp;
  }
  ld.static_sampler_count=2;layout_=dev->CreateBindingLayout(ld);
  if(!layout_) return fail();
  const char* sources[]={shaders::kClayPostDofDownPs,shaders::kClayPostDofBlurPs,shaders::kClayPostDofCompositePs};
  const char* names[]={"native_dof_down","native_dof_blur","native_dof_composite"};
  for(uint32_t i=0;i<3;i++) {
    nrhi::ShaderDesc sd;sd.stage=nrhi::ShaderStage::kPixel;sd.entry_point="main";sd.name=names[i];sd.hlsl_source=sources[i];
    shaders_[i]=dev->CreateShader(sd);
    if(!shaders_[i]) return fail();
    nrhi::GraphicsPipelineDesc pd;pd.layout=layout_;pd.vs=vertex;pd.ps=shaders_[i];
    pd.rtv_format=i==2 ? nrhi::Format::kR8G8B8A8_UNORM:nrhi::Format::kR16G16B16A16_FLOAT;
    pipelines_[i]=dev->CreateGraphicsPipeline(pd);if(!pipelines_[i]) return fail();
  }
  nrhi::TextureDesc td;td.width=280;td.height=180;td.format=nrhi::Format::kR16G16B16A16_FLOAT;
  td.usage=nrhi::kTextureUsageRenderTarget;td.initial_state=nrhi::ResourceState::kPixelShaderResource;
  for(uint32_t i=0;i<2;i++) {
    targets_[i]=dev->CreateTexture(td);views_[i]=targets_[i] ? dev->CreateTextureView(targets_[i],{}):nullptr;
    if(!views_[i]) return fail();
  }
  depth_source_=depth;depth_view_=dev->CreateTextureView(depth,{});if(!depth_view_) return fail();
  REXLOG_INFO("[native] DOF: own 280x180 HDR blur targets and F51 composite created");return true;
}
bool DofPass::Render(nrhi::Cmd* cmd,nrhi::Device* dev,const capture::DofBlock& block,nrhi::Shader* vertex,
                     nrhi::Texture* depth,nrhi::TextureView* scene,nrhi::TextureView* bloom,
                     nrhi::Texture* front,nrhi::Buffer* post_constants,uint64_t post_offset) {
  if(!cmd || !block.valid || !scene || !front || !post_constants || !Ensure(dev,vertex,depth)) return false;
  struct Constants {float projection_caps[4]{};float focus[4]{};float blur[4][4]{};};
  static_assert(sizeof(Constants)==96);
  Constants constants;
  std::copy(block.projection,block.projection+2,constants.projection_caps);
  std::copy(block.caps,block.caps+2,constants.projection_caps+2);
  std::copy(block.focus,block.focus+4,constants.focus);
  for(uint32_t i=0;i<2;i++) {
    std::copy(block.blur[i],block.blur[i]+4,constants.blur[2*i]);
    std::copy(block.blur[i]+4,block.blur[i]+7,constants.blur[2*i+1]);
  }
  // Fresh upload avoids rewriting data still referenced by a GPU submission.
  nrhi::BufferDesc bd;bd.heap=nrhi::HeapKind::kUpload;bd.size=256;
  auto cb=dev->CreateBuffer(bd);if(!cb) {failed_=true;return false;}
  void* mapped=dev->Map(cb);
  if(!mapped) {dev->DestroyDeferred(cb);failed_=true;return false;}
  std::memcpy(mapped,&constants,sizeof(constants));dev->Unmap(cb);
  auto step=[&](nrhi::Texture* target,uint32_t width,uint32_t height,uint32_t pipeline,
                const ClayPostStepConstants& words,nrhi::TextureView* source,nrhi::TextureView* soft=nullptr) {
    cmd->Barrier(target,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget);cmd->FlushBarriers();
    cmd->SetRenderTargets(target,nullptr);cmd->SetViewport({0,0,float(width),float(height),0,1});
    cmd->SetScissor({0,0,int32_t(width),int32_t(height)});cmd->SetBindingLayout(layout_);cmd->SetPipeline(pipelines_[pipeline]);
    cmd->SetConstantBuffer(0,post_constants,post_offset);cmd->SetRootConstants(1,8,&words,0);cmd->SetConstantBuffer(2,cb,0);
    nrhi::TextureView* textures[4]={source,bloom ? bloom:source,soft ? soft:source,depth_view_};
    cmd->SetTextures(3,textures,4);cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);cmd->Draw(3,0);
    cmd->Barrier(target,nrhi::ResourceState::kRenderTarget,nrhi::ResourceState::kPixelShaderResource);cmd->FlushBarriers();
  };
  step(targets_[0],280,180,0,MakeClayPostStep(ClayPostStep::kDownsample,1120,720,0),scene);
  step(targets_[1],280,180,1,MakeClayPostStep(ClayPostStep::kBlurX,280,180,0),views_[0]);
  step(targets_[0],280,180,1,MakeClayPostStep(ClayPostStep::kBlurY,280,180,0),views_[1]);
  step(front,1120,720,2,MakeClayPostStep(ClayPostStep::kComposite,1120,720,255),scene,views_[0]);
  dev->DestroyDeferred(cb);
  if(lines_++<8) REXLOG_INFO("[native] DOF: current HDR downsample, X/Y blur and F51 composite drawn");
  return true;
}
}
