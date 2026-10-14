#include "particle_pass.h"
#include "particle_shaders.h"
#include "../capture/particle_vertex.h"
#include <rex/logging.h>
#include <rex/cvar.h>
#include <cstring>
namespace fable2::native::render {
namespace {
struct Constants {float viewport[4]{};float texinfo[3][4]{};float controls[4]{};float blend[4]{};float projection[5][4]{};float points[4]{};};
static_assert(sizeof(Constants)==192);
nrhi::BlendFactor Factor(uint8_t f) {
  switch(capture::Factor(f)) {
    case capture::Factor::kZero:return nrhi::BlendFactor::kZero;
    case capture::Factor::kSrcAlpha:return nrhi::BlendFactor::kSrcAlpha;
    case capture::Factor::kInvSrcAlpha:return nrhi::BlendFactor::kInvSrcAlpha;
    default:return nrhi::BlendFactor::kOne;
  }
}
}
void ParticlePass::Release(nrhi::Device* dev) {
  textures_.Retry(dev);
  if(dev) {for(auto e:pipelines_) dev->DestroyDeferred(e.pipeline);
    dev->DestroyDeferred(copy_pipeline_);for(auto p:{vs_,ps_,soft_ps_,full_vs_,copy_ps_,snow_vs_,snow_ps_}) dev->DestroyDeferred(p);
    dev->DestroyDeferred(depth_copy_view_);dev->DestroyDeferred(depth_source_view_);dev->DestroyDeferred(depth_copy_);
    dev->DestroyDeferred(half_view_);dev->DestroyDeferred(half_colour_);dev->DestroyDeferred(half_depth_);
    dev->DestroyDeferred(half_depth_pipeline_);dev->DestroyDeferred(half_depth_ps_);}
  pipelines_.clear();device_=nullptr;layout_=nullptr;vs_=ps_=nullptr;failed_=false;
  soft_ps_=full_vs_=copy_ps_=nullptr;copy_pipeline_=nullptr;depth_copy_=depth_source_=nullptr;
  snow_vs_=snow_ps_=nullptr;
  depth_copy_view_=depth_source_view_=nullptr;
  half_view_=nullptr;half_colour_=half_depth_=nullptr;half_depth_pipeline_=nullptr;half_depth_ps_=nullptr;half_frame_=0;
}
bool ParticlePass::Ensure(nrhi::Device* dev) {
  if(device_ && device_!=dev) Release(nullptr);
  if(failed_) return false;
  if(vs_ && ps_ && snow_vs_ && snow_ps_ && layout_) return true;
  device_=dev;
  auto fail=[&] {Release(dev);device_=dev;failed_=true;return false;};
  nrhi::BindingLayoutDesc ld;
  ld.params[0]={nrhi::BindingParamKind::kConstantBuffer,0,1,nrhi::Visibility::kAll};
  ld.params[1]={nrhi::BindingParamKind::kBufferSrv,3,1,nrhi::Visibility::kVertex};
  ld.params[2]={nrhi::BindingParamKind::kTextureTable,0,3,nrhi::Visibility::kPixel};
  ld.params[3]={nrhi::BindingParamKind::kTextureTable,4,1,nrhi::Visibility::kPixel};
  ld.params[4]={nrhi::BindingParamKind::kBufferSrv,5,1,nrhi::Visibility::kVertex};
  ld.param_count=5;ld.allow_input_layout=false;
  for(uint32_t i=0;i<4;i++) {ld.static_samplers[i].shader_register=i;
    ld.static_samplers[i].filter=i<2 ? nrhi::Filter::kLinear:nrhi::Filter::kPoint;
    ld.static_samplers[i].address=(i&1) ? nrhi::AddressMode::kWrap:nrhi::AddressMode::kClamp;}
  ld.static_sampler_count=4;layout_=dev->CreateBindingLayout(ld);
  auto shader=[&](const char* entry,nrhi::ShaderStage stage) {nrhi::ShaderDesc sd;sd.name=entry;sd.stage=stage;
    sd.entry_point=entry;sd.hlsl_source=particle_shaders::kSource;return dev->CreateShader(sd);};
  vs_=shader("vertex",nrhi::ShaderStage::kVertex);ps_=shader("pixel",nrhi::ShaderStage::kPixel);
  snow_vs_=shader("snow_vertex",nrhi::ShaderStage::kVertex);snow_ps_=shader("snow_pixel",nrhi::ShaderStage::kPixel);
  if(!layout_ || !vs_ || !ps_ || !snow_vs_ || !snow_ps_) return fail();return true;
}
bool ParticlePass::EnsureDepth(nrhi::Device* dev,nrhi::Texture* source) {
  if(depth_source_ && depth_source_!=source) {failed_=true;return false;}
  if(copy_pipeline_ && depth_copy_view_ && depth_source_view_) return true;
  auto shader=[&](const char* entry,nrhi::ShaderStage stage) {nrhi::ShaderDesc sd;sd.name=entry;sd.stage=stage;
    sd.entry_point=entry;sd.hlsl_source=particle_shaders::kSource;return dev->CreateShader(sd);};
  soft_ps_=shader("soft",nrhi::ShaderStage::kPixel);full_vs_=shader("full_vertex",nrhi::ShaderStage::kVertex);
  copy_ps_=shader("copy_depth",nrhi::ShaderStage::kPixel);
  nrhi::TextureDesc td;td.width=1120;td.height=720;td.format=nrhi::Format::kR32_FLOAT;
  td.usage=nrhi::kTextureUsageRenderTarget;td.initial_state=nrhi::ResourceState::kPixelShaderResource;
  depth_copy_=dev->CreateTexture(td);depth_copy_view_=depth_copy_ ? dev->CreateTextureView(depth_copy_,{}):nullptr;
  depth_source_=source;depth_source_view_=dev->CreateTextureView(source,{});
  nrhi::GraphicsPipelineDesc pd;pd.layout=layout_;pd.vs=full_vs_;pd.ps=copy_ps_;pd.rtv_format=nrhi::Format::kR32_FLOAT;
  if(full_vs_ && copy_ps_) copy_pipeline_=dev->CreateGraphicsPipeline(pd);
  if(!soft_ps_ || !copy_pipeline_ || !depth_copy_view_ || !depth_source_view_) {failed_=true;return false;}
  return true;
}
bool ParticlePass::EnsureHalf(nrhi::Device* dev,nrhi::Texture* depth) {
  if(!EnsureDepth(dev,depth)) return false;
  if(half_view_ && half_depth_pipeline_ && half_depth_) return true;
  nrhi::TextureDesc td;td.width=560;td.height=360;td.format=nrhi::Format::kR8G8B8A8_UNORM;
  td.usage=nrhi::kTextureUsageRenderTarget;td.initial_state=nrhi::ResourceState::kPixelShaderResource;
  half_colour_=dev->CreateTexture(td);half_view_=half_colour_ ? dev->CreateTextureView(half_colour_,{}):nullptr;
  td.format=nrhi::Format::kD32_FLOAT;td.usage=nrhi::kTextureUsageDepthStencil;td.initial_state=nrhi::ResourceState::kDepthWrite;
  half_depth_=dev->CreateTexture(td);
  nrhi::ShaderDesc sd;sd.name="half_depth";sd.stage=nrhi::ShaderStage::kPixel;
  sd.entry_point="half_depth";sd.hlsl_source=particle_shaders::kSource;half_depth_ps_=dev->CreateShader(sd);
  nrhi::GraphicsPipelineDesc pd;pd.layout=layout_;pd.vs=vs_;pd.ps=half_depth_ps_;
  pd.rtv_format=nrhi::Format::kUnknown;pd.dsv_format=nrhi::Format::kD32_FLOAT;
  pd.depth.test_enable=true;pd.depth.write_enable=true;pd.depth.func=nrhi::CompareFunc::kAlways;
  if(half_depth_ps_) half_depth_pipeline_=dev->CreateGraphicsPipeline(pd);
  if(!half_view_ || !half_depth_ || !half_depth_pipeline_) {failed_=true;return false;}
  REXLOG_INFO("[native] effects: own 560x360 RGBA8/D32 targets and depth-copy pipeline created");
  return true;
}
nrhi::Pipeline* ParticlePass::Pipeline(const capture::UiDraw& draw) {
  if(!capture::UiClipSupported(draw.clip_control)) return nullptr;
  auto rs=capture::DecodeRenderState(draw.state);
  if(!rs.valid || !DepthSupported(rs) || (rs.blend && !BlendSupported(rs)) || rs.alpha_to_mask ||
     !capture::ParticleStencilSupported(draw.state.depth_control)) return nullptr;
  const auto key=MakePipelineKey(rs,draw.weather ? 2:draw.particle_soft ? 1:0);
  const bool depth_clip=capture::UiDepthClip(draw.clip_control);
  for(auto e:pipelines_) if(e.key==key && e.depth_clip==depth_clip && e.half==draw.particle_half) return e.pipeline;
  if(pipelines_.size()>=32) return nullptr;
  nrhi::GraphicsPipelineDesc pd;pd.layout=layout_;pd.vs=draw.weather ? snow_vs_:vs_;pd.ps=draw.weather ? snow_ps_:draw.particle_soft ? soft_ps_:ps_;
  pd.rtv_format=draw.particle_half ? nrhi::Format::kR8G8B8A8_UNORM:nrhi::Format::kR11G11B10_FLOAT;pd.dsv_format=nrhi::Format::kD32_FLOAT;
  pd.blend.enable=key.blend;pd.blend.src=Factor(key.src);pd.blend.dst=Factor(key.dst);
  pd.blend.src_alpha=Factor(key.src_alpha);pd.blend.dst_alpha=Factor(key.dst_alpha);
  pd.blend.op=key.op==2 ? nrhi::BlendOp::kMin:nrhi::BlendOp::kAdd;
  pd.blend.op_alpha=key.op_alpha==2 ? nrhi::BlendOp::kMin:nrhi::BlendOp::kAdd;
  pd.blend.write_mask=key.color_mask;
  pd.depth.test_enable=key.depth_test;pd.depth.write_enable=key.depth_write;
  pd.depth.func=key.depth_func==7 ? nrhi::CompareFunc::kAlways:key.depth_func==3 ? nrhi::CompareFunc::kLessEqual:nrhi::CompareFunc::kLess;
  pd.cull=key.cull==1 ? nrhi::CullMode::kFront:key.cull==2 ? nrhi::CullMode::kBack:nrhi::CullMode::kNone;
  pd.depth_clip=depth_clip;
  auto p=device_->CreateGraphicsPipeline(pd);if(p) pipelines_.push_back({key,depth_clip,draw.particle_half,p});else failed_=true;return p;
}
bool ParticlePass::Render(nrhi::Cmd* cmd,nrhi::Device* dev,const capture::UiFrame& frame,
                          nrhi::Texture* colour,nrhi::Texture* depth,float exposure) {
  half_frame_=0;
  capture::UiFrame full,half;full.frame=half.frame=frame.frame;
  half.effect_depth=frame.effect_depth;
  full.particle_refused=half.particle_refused=frame.particle_refused;
  for(const auto& draw:frame.world_particles) {
    auto& target=draw.particle_half ? half:full;
    target.world_particles.push_back(draw);target.particle_vertices+=uint32_t(draw.vertices.size());
    target.weather_points+=draw.point_count;
  }
  full.particle_unsupported=frame.particle_unsupported;
  const bool okay=RenderGroup(cmd,dev,full,colour,depth,exposure,false);
  if(half.world_particles.empty() || !frame.effect_depth || !Ensure(dev) || !EnsureHalf(dev,depth)) return okay && half.world_particles.empty();
  if(RenderGroup(cmd,dev,half,half_colour_,depth,exposure,true)) half_frame_=frame.frame;
  return okay && half_frame_==frame.frame;
}
bool ParticlePass::RenderGroup(nrhi::Cmd* cmd,nrhi::Device* dev,const capture::UiFrame& frame,
                              nrhi::Texture* colour,nrhi::Texture* depth,float exposure,bool half) {
  auto note=[&](bool okay) {
    if(lines_<16 || frame.frame%300==0) {++lines_;REXLOG_INFO("[native] particles: frame {} drawn {} records {} vertices {} snow-points {} refused {} unsupported {} target {}",
      frame.frame,okay ? frame.world_particles.size():0,frame.world_particles.size(),frame.particle_vertices,frame.weather_points,frame.particle_refused,frame.particle_unsupported,half ? "560x360":"1120x720");}
    return okay;
  };
  if(frame.world_particles.empty()) return note(true);
  if(frame.particle_refused || !capture::UiFinite(exposure) || exposure<0 || !Ensure(dev)) return note(false);
  const bool soft=std::any_of(frame.world_particles.begin(),frame.world_particles.end(),[](const auto& d) {return d.particle_soft;});
  if(soft && !EnsureDepth(dev,depth)) return note(false);
  struct Prepared {nrhi::Pipeline* pipeline;std::array<nrhi::TextureView*,3> views;Constants c;};
  std::vector<Prepared> prepared;std::vector<capture::UiVertex> vertices;std::vector<uint8_t> constants(frame.world_particles.size()*256);
  std::vector<capture::WeatherConstants> weather(frame.world_particles.size());
  textures_.BeginFrame(frame.frame,64ull<<20,8ull<<20);TextureStats st;
  const bool half_pixel=rex::cvar::GetFlagByName("half_pixel_offset")=="true";
  for(size_t j=0;j<frame.world_particles.size();j++) {
    const auto& d=frame.world_particles[j];Prepared p{};p.pipeline=Pipeline(d);
    if(!p.pipeline || !d.particle_textures || d.particle_textures>3) return note(false);
    const auto viewport=capture::UiViewportOf(d,half_pixel);
    std::copy(viewport.begin(),viewport.end(),p.c.viewport);
    auto rs=capture::DecodeRenderState(d.state);
    p.c.controls[0]=half ? d.registers[0][3]:exposure;p.c.controls[1]=float(d.particle_textures);
    p.c.points[3]=half ? 1.f:0.f;
    p.c.controls[2]=rs.alpha_test ? float(rs.alpha_func):7;p.c.controls[3]=rs.alpha_ref;
    p.c.blend[0]=d.registers[2][0];p.c.blend[1]=d.registers[2][1];p.c.blend[2]=d.registers[3][0];p.c.blend[3]=d.registers[3][1];
    if(d.weather) {
      weather[j]=*d.weather;
      std::copy(d.registers[2],d.registers[2]+3,p.c.blend);
      p.c.points[0]=float(d.point_first);p.c.points[1]=float(d.point_minmax&65535)*.125f;p.c.points[2]=float(d.point_minmax>>16)*.125f;
    }
    std::memcpy(p.c.projection,d.particle_projection,sizeof(p.c.projection));
    for(uint32_t i=0;i<d.particle_textures;i++) {
      if(!d.textures[i]) return note(false);
      uint32_t sampler=0;float uv_fix[2]={1,1};capture::MaterialStatus status;
      p.views[i]=textures_.ResolveSnapshot(cmd,dev,d.fetch[i],*d.textures[i],&sampler,uv_fix,&status,st);
      if(!p.views[i]) return note(false);
      static constexpr uint32_t selectors[]={3,1,2,0};
      if(sampler>=4) return note(false);
      p.c.texinfo[i][0]=float(selectors[sampler]);p.c.texinfo[i][1]=uv_fix[0];p.c.texinfo[i][2]=uv_fix[1];
    }
    for(uint32_t i=d.particle_textures;i<3;i++) p.views[i]=p.views[0];
    std::memcpy(constants.data()+j*256,&p.c,sizeof(p.c));prepared.push_back(p);
    vertices.insert(vertices.end(),d.vertices.begin(),d.vertices.end());
  }
  const uint64_t depth_vertices_offset=uint64_t(vertices.size())*sizeof(capture::UiVertex);
  const uint64_t depth_constants_offset=constants.size();
  if(half) {
    if(!frame.effect_depth || !capture::UiEffectDepthQuad(frame.effect_depth->vertices)) return note(false);
    Constants depth_constants{};
    const auto viewport=capture::UiViewportOf(*frame.effect_depth,half_pixel);
    std::copy(viewport.begin(),viewport.end(),depth_constants.viewport);
    depth_constants.points[3]=1;
    constants.resize(constants.size()+256);
    std::memcpy(constants.data()+depth_constants_offset,&depth_constants,sizeof(depth_constants));
    vertices.insert(vertices.end(),frame.effect_depth->vertices.begin(),frame.effect_depth->vertices.end());
  }
  nrhi::Buffer* vb=nullptr;nrhi::Buffer* cb=nullptr;nrhi::Buffer* wb=nullptr;
  auto upload=[&](const void* data,uint64_t size) {nrhi::BufferDesc bd;bd.heap=nrhi::HeapKind::kUpload;bd.size=size;
    auto b=dev->CreateBuffer(bd);if(!b) return b;auto p=dev->Map(b);
    if(!p) {dev->DestroyDeferred(b);return static_cast<nrhi::Buffer*>(nullptr);}
    std::memcpy(p,data,size);dev->Unmap(b);return b;};
  if(vertices.empty()) vertices.emplace_back(); // A valid unused SRV for weather-only frames.
  vb=upload(vertices.data(),vertices.size()*sizeof(capture::UiVertex));cb=upload(constants.data(),constants.size());
  wb=upload(weather.data(),weather.size()*sizeof(capture::WeatherConstants));
  if(!vb || !cb || !wb) {dev->DestroyDeferred(vb);dev->DestroyDeferred(cb);dev->DestroyDeferred(wb);failed_=true;return note(false);}
  cmd->FlushBarriers();
  if(half) {
    cmd->Barrier(depth,nrhi::ResourceState::kDepthWrite,nrhi::ResourceState::kPixelShaderResource);
    cmd->Barrier(colour,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget);cmd->FlushBarriers();
    cmd->SetRenderTargets(nullptr,half_depth_);cmd->SetViewport({0,0,560,360,0,1});cmd->SetScissor({0,0,560,360});
    cmd->SetBindingLayout(layout_);cmd->SetPipeline(half_depth_pipeline_);cmd->SetConstantBuffer(0,cb,depth_constants_offset);
    cmd->SetBufferSrv(1,vb,depth_vertices_offset);cmd->SetTextures(2,prepared[0].views.data(),3);cmd->SetTexture(3,depth_source_view_);cmd->SetBufferSrv(4,wb,0);
    cmd->ClearDepth(half_depth_,1);
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);cmd->Draw(6,0);
    cmd->Barrier(depth,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kDepthWrite);cmd->FlushBarriers();
    const float black[4]{};cmd->ClearRenderTarget(colour,black);
  }
  if(soft) {
    cmd->Barrier(depth,nrhi::ResourceState::kDepthWrite,nrhi::ResourceState::kPixelShaderResource);
    cmd->Barrier(depth_copy_,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget);cmd->FlushBarriers();
    cmd->SetRenderTargets(depth_copy_,nullptr);cmd->SetViewport({0,0,1120,720,0,1});cmd->SetScissor({0,0,1120,720});
    cmd->SetBindingLayout(layout_);cmd->SetPipeline(copy_pipeline_);cmd->SetConstantBuffer(0,cb,0);
    cmd->SetBufferSrv(1,vb,0);cmd->SetTextures(2,prepared[0].views.data(),3);cmd->SetTexture(3,depth_source_view_);
    cmd->SetBufferSrv(4,wb,0);
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);cmd->Draw(3,0);
    cmd->Barrier(depth_copy_,nrhi::ResourceState::kRenderTarget,nrhi::ResourceState::kPixelShaderResource);
    cmd->Barrier(depth,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kDepthWrite);cmd->FlushBarriers();
  }
  const uint32_t width=half ? 560:1120,height=half ? 360:720;
  cmd->SetRenderTargets(colour,half ? half_depth_:depth);cmd->SetViewport({0,0,float(width),float(height),0,1});
  cmd->SetBindingLayout(layout_);cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  cmd->SetTexture(3,soft ? depth_copy_view_:prepared[0].views[0]);
  uint32_t start=0;
  for(size_t i=0;i<prepared.size();i++) {
    const auto& d=frame.world_particles[i];const auto& p=prepared[i];
    const auto scissor=capture::UiScissorOf(d,width,height,half_pixel);
    cmd->SetScissor({scissor[0],scissor[1],scissor[2],scissor[3]});
    cmd->SetPipeline(p.pipeline);cmd->SetConstantBuffer(0,cb,i*256);cmd->SetTextures(2,p.views.data(),3);
    cmd->SetBufferSrv(1,vb,d.weather ? 0:uint64_t(start)*sizeof(capture::UiVertex));
    cmd->SetBufferSrv(4,wb,i*sizeof(capture::WeatherConstants));
    cmd->Draw(d.weather ? d.point_count*6:uint32_t(d.vertices.size()),0);start+=uint32_t(d.vertices.size());
  }
  if(half) {cmd->Barrier(colour,nrhi::ResourceState::kRenderTarget,nrhi::ResourceState::kPixelShaderResource);cmd->FlushBarriers();}
  dev->DestroyDeferred(vb);dev->DestroyDeferred(cb);dev->DestroyDeferred(wb);return note(true);
}
}
