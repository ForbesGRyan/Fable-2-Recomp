#include "presentation_pass.h"
#include "presentation_shaders.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <rex/logging.h>
#include <rex/cvar.h>

namespace fable2::native::render {
namespace {
struct Constants {
  float c[9][4]{};
  float viewport[4]{};
  float alpha[4]{7,0,0,0};
  float texinfo[3][4]{};
  float finish[4]{1,0,0,0};
  float ramp[512][4]{};
};
static_assert(offsetof(Constants,ramp)==240);
constexpr uint32_t kFullBytes=8448, kDrawBytes=256;
nrhi::BlendFactor Factor(uint8_t f) {
  switch(capture::Factor(f)) {
    case capture::Factor::kZero:return nrhi::BlendFactor::kZero;
    case capture::Factor::kSrcAlpha:return nrhi::BlendFactor::kSrcAlpha;
    case capture::Factor::kInvSrcAlpha:return nrhi::BlendFactor::kInvSrcAlpha;
    default:return nrhi::BlendFactor::kOne;
  }
}
}
void PresentationPass::Release(nrhi::Device* dev) {
  textures_.Retry(dev);
  if(dev) {
    for(auto e:pipelines_) dev->DestroyDeferred(e.pipeline);
    for(auto p:{copy_,history_,display_}) dev->DestroyDeferred(p);
    dev->DestroyDeferred(vs_); dev->DestroyDeferred(full_vs_);
    dev->DestroyDeferred(mask_vs_);dev->DestroyDeferred(mask_ps_);dev->DestroyDeferred(mask_pipeline_);
    dev->DestroyDeferred(mask_clear_ps_);dev->DestroyDeferred(mask_resolve_ps_);
    for(auto p:{mask_clear_opaque_,mask_clear_blend_,mask_resolve_}) dev->DestroyDeferred(p);
    dev->DestroyDeferred(mask_scratch_view_);dev->DestroyDeferred(mask_scratch_);
    dev->DestroyDeferred(coverage_);for(auto s:coverage_ps_) dev->DestroyDeferred(s);for(auto p:coverage_pipeline_) dev->DestroyDeferred(p);
    for(auto v:mask_views_) dev->DestroyDeferred(v);
    for(auto t:masks_) dev->DestroyDeferred(t);
    for(auto p:ps_) dev->DestroyDeferred(p);
    for(auto p:full_ps_) dev->DestroyDeferred(p);
    for(auto v:{target_view_,front_view_[0],front_view_[1],white_view_}) dev->DestroyDeferred(v);
    for(auto t:{target_,front_[0],front_[1],output_,depth_,white_}) dev->DestroyDeferred(t);
  }
  pipelines_.clear(); vs_=full_vs_=nullptr;
  mask_vs_=mask_ps_=nullptr;mask_pipeline_=nullptr;
  mask_clear_ps_=mask_resolve_ps_=nullptr;mask_clear_opaque_=mask_clear_blend_=mask_resolve_=nullptr;
  mask_scratch_=nullptr;mask_scratch_view_=nullptr;
  coverage_=nullptr;for(auto& s:coverage_ps_) s=nullptr;for(auto& p:coverage_pipeline_) p=nullptr;
  for(auto& v:mask_views_) v=nullptr;
  for(auto& t:masks_) t=nullptr;
  for(auto& p:ps_) p=nullptr;
  for(auto& p:full_ps_) p=nullptr;
  copy_=history_=display_=nullptr; target_=output_=depth_=white_=nullptr;
  target_view_=white_view_=nullptr;
  for(auto& t:front_) t=nullptr;
  for(auto& v:front_view_) v=nullptr;
  white_uploaded_=false; previous_frame_=0; front_write_=0;
  layout_=nullptr; device_=nullptr;
  failed_=false;
}
bool PresentationPass::Ensure(nrhi::Device* dev) {
  if(device_ && device_!=dev) Release(nullptr);
  if(failed_) return false;
  if(output_ && device_==dev) return true;
  if(device_) Release(device_==dev ? device_ : nullptr);
  device_=dev;
  auto fail=[&] { Release(dev); device_=dev; failed_=true; REXLOG_ERROR("[native] presentation: resource creation failed; retry with F6"); return false; };
  nrhi::BindingLayoutDesc ld;
  ld.params[0]={nrhi::BindingParamKind::kConstantBuffer,0,1,nrhi::Visibility::kAll};
  ld.params[1]={nrhi::BindingParamKind::kBufferSrv,3,1,nrhi::Visibility::kVertex};
  ld.params[2]={nrhi::BindingParamKind::kTextureTable,0,3,nrhi::Visibility::kPixel};
  ld.param_count=3; ld.allow_input_layout=false;
  for(uint32_t i=0;i<4;i++) {
    ld.static_samplers[i].shader_register=i;
    ld.static_samplers[i].filter=i<2 ? nrhi::Filter::kLinear : nrhi::Filter::kPoint;
    ld.static_samplers[i].address=(i&1) ? nrhi::AddressMode::kWrap : nrhi::AddressMode::kClamp;
  }
  ld.static_sampler_count=4;
  layout_=dev->CreateBindingLayout(ld);
  if(!layout_) return fail();
  auto shader=[&](const char* entry,nrhi::ShaderStage stage) {
    nrhi::ShaderDesc sd; sd.stage=stage; sd.name=entry;
    sd.entry_point=entry; sd.hlsl_source=ui_shaders::kSource; return dev->CreateShader(sd);
  };
  vs_=shader("vertex",nrhi::ShaderStage::kVertex);
  full_vs_=shader("full_vertex",nrhi::ShaderStage::kVertex);
  mask_vs_=shader("mask_vertex",nrhi::ShaderStage::kVertex);
  mask_ps_=shader("mask",nrhi::ShaderStage::kPixel);
  mask_clear_ps_=shader("mask_clear",nrhi::ShaderStage::kPixel);
  mask_resolve_ps_=shader("mask_resolve",nrhi::ShaderStage::kPixel);
  coverage_ps_[0]=shader("sprite_coverage",nrhi::ShaderStage::kPixel);
  coverage_ps_[1]=shader("text_coverage",nrhi::ShaderStage::kPixel);
  const char* names[]={"sprite","text","video","particle","fade"};
  const char* full_names[]={"copy","history","display"};
  for(uint32_t i=0;i<3;i++) {
    ps_[i]=shader(names[i],nrhi::ShaderStage::kPixel);
    full_ps_[i]=shader(full_names[i],nrhi::ShaderStage::kPixel);
    if(!ps_[i] || !full_ps_[i]) return fail();
  }
  for(uint32_t i=3;i<capture::kUiKindCount;i++) {ps_[i]=shader(names[i],nrhi::ShaderStage::kPixel);if(!ps_[i]) return fail();}
  if(!vs_ || !full_vs_ || !mask_vs_ || !mask_ps_ || !mask_clear_ps_ || !mask_resolve_ps_ || !coverage_ps_[0] || !coverage_ps_[1]) return fail();
  nrhi::TextureDesc td; td.width=1280; td.height=720;
  td.format=nrhi::Format::kR16G16B16A16_FLOAT;
  td.usage=nrhi::kTextureUsageRenderTarget; td.initial_state=nrhi::ResourceState::kPixelShaderResource;
  target_=dev->CreateTexture(td);
  target_view_=target_ ? dev->CreateTextureView(target_,{}) : nullptr;
  // Menu mapping and temporal copy write RB_COLOR_INFO format 2 (10-bit RGB),
  // not an 8-bit surface. Repeated 8-bit history rounding biases dark UI pixels.
  td.format=nrhi::Format::kR10G10B10A2_UNORM;
  for(uint32_t i=0;i<2;i++) {
    front_[i]=dev->CreateTexture(td);
    front_view_[i]=front_[i] ? dev->CreateTextureView(front_[i],{}) : nullptr;
  }
  td.format=nrhi::Format::kR8G8B8A8_UNORM;
  output_=dev->CreateTexture(td);
  td.format=nrhi::Format::kD32_FLOAT; td.usage=nrhi::kTextureUsageDepthStencil;
  td.initial_state=nrhi::ResourceState::kDepthWrite;
  depth_=dev->CreateTexture(td);
  td.width=td.height=1; td.format=nrhi::Format::kR8G8B8A8_UNORM;
  td.usage=nrhi::kTextureUsageNone; td.initial_state=nrhi::ResourceState::kCopyDest;
  white_=dev->CreateTexture(td);
  white_view_=white_ ? dev->CreateTextureView(white_,{}) : nullptr;
  if(!target_view_ || !front_view_[0] || !front_view_[1] || !output_ || !depth_ || !white_view_) return fail();
  nrhi::GraphicsPipelineDesc pd; pd.layout=layout_; pd.vs=full_vs_; pd.ps=full_ps_[0];
  pd.rtv_format=nrhi::Format::kR16G16B16A16_FLOAT;
  copy_=dev->CreateGraphicsPipeline(pd);
  pd.rtv_format=nrhi::Format::kR10G10B10A2_UNORM; pd.ps=full_ps_[1];
  history_=dev->CreateGraphicsPipeline(pd);
  pd.rtv_format=nrhi::Format::kR8G8B8A8_UNORM;pd.ps=full_ps_[2];
  display_=dev->CreateGraphicsPipeline(pd);
  if(!copy_ || !history_ || !display_) return fail();
  pd.vs=full_vs_;pd.ps=mask_resolve_ps_;
  mask_resolve_=dev->CreateGraphicsPipeline(pd);
  pd.ps=mask_clear_ps_;
  mask_clear_opaque_=dev->CreateGraphicsPipeline(pd);
  pd.blend.enable=true;pd.blend.src=pd.blend.src_alpha=nrhi::BlendFactor::kSrcAlpha;
  pd.blend.dst=pd.blend.dst_alpha=nrhi::BlendFactor::kInvSrcAlpha;
  mask_clear_blend_=dev->CreateGraphicsPipeline(pd);
  pd.vs=mask_vs_;pd.ps=mask_ps_;
  pd.depth_clip=true;
  mask_pipeline_=dev->CreateGraphicsPipeline(pd);
  if(!mask_pipeline_ || !mask_resolve_ || !mask_clear_opaque_ || !mask_clear_blend_) return fail();
  {
    nrhi::TextureDesc md;md.width=640;md.height=360;md.format=nrhi::Format::kR8G8B8A8_UNORM;
    md.usage=nrhi::kTextureUsageRenderTarget;md.initial_state=nrhi::ResourceState::kPixelShaderResource;
    mask_scratch_=dev->CreateTexture(md);
    mask_scratch_view_=mask_scratch_ ? dev->CreateTextureView(mask_scratch_,{}):nullptr;
    if(!mask_scratch_view_) return fail();
    nrhi::TextureDesc cd;cd.width=1280;cd.height=720;cd.format=nrhi::Format::kR8_UNORM;
    cd.usage=nrhi::kTextureUsageRenderTarget;cd.initial_state=nrhi::ResourceState::kPixelShaderResource;
    coverage_=dev->CreateTexture(cd);
    if(!coverage_) return fail();
    nrhi::GraphicsPipelineDesc cp;cp.layout=layout_;cp.vs=vs_;cp.rtv_format=nrhi::Format::kR8_UNORM;
    cp.blend.enable=true;cp.blend.src=cp.blend.src_alpha=nrhi::BlendFactor::kOne;
    cp.blend.dst=cp.blend.dst_alpha=nrhi::BlendFactor::kInvSrcAlpha;
    for(uint32_t k=0;k<2;k++) {cp.ps=coverage_ps_[k];coverage_pipeline_[k]=dev->CreateGraphicsPipeline(cp);if(!coverage_pipeline_[k]) return fail();}
  }
  REXLOG_INFO("[native] presentation: ready, 1280x720, sprite/text/video");
  return true;
}
nrhi::Pipeline* PresentationPass::Pipeline(const capture::UiDraw& draw) {
  if(uint32_t(draw.kind)<1 || uint32_t(draw.kind)>capture::kUiKindCount || !capture::UiClipSupported(draw.clip_control)) return nullptr;
  auto rs=capture::DecodeRenderState(draw.state);
  if(!rs.valid || !DepthSupported(rs) || (rs.blend && !BlendSupported(rs)) || rs.alpha_to_mask ||
     (draw.state.depth_control&1)) return nullptr;
  auto key=MakePipelineKey(rs,uint8_t(draw.kind));
  const bool depth_clip=capture::UiDepthClip(draw.clip_control);
  for(auto e:pipelines_) if(e.key==key && e.depth_clip==depth_clip) return e.pipeline;
  if(pipelines_.size()>=64) return nullptr;
  nrhi::GraphicsPipelineDesc pd;
  pd.layout=layout_; pd.vs=vs_; pd.ps=ps_[uint32_t(draw.kind)-1];
  pd.rtv_format=nrhi::Format::kR16G16B16A16_FLOAT; pd.dsv_format=nrhi::Format::kD32_FLOAT;
  pd.blend.enable=key.blend; pd.blend.src=Factor(key.src); pd.blend.dst=Factor(key.dst);
  pd.blend.src_alpha=Factor(key.src_alpha); pd.blend.dst_alpha=Factor(key.dst_alpha);
  pd.blend.op=key.op==2 ? nrhi::BlendOp::kMin : nrhi::BlendOp::kAdd;
  pd.blend.op_alpha=key.op_alpha==2 ? nrhi::BlendOp::kMin : nrhi::BlendOp::kAdd;
  pd.blend.write_mask=key.color_mask;
  pd.depth.test_enable=key.depth_test; pd.depth.write_enable=key.depth_write;
  pd.depth.func=key.depth_func==7 ? nrhi::CompareFunc::kAlways : key.depth_func==3 ? nrhi::CompareFunc::kLessEqual : nrhi::CompareFunc::kLess;
  pd.cull=key.cull==1 ? nrhi::CullMode::kFront : key.cull==2 ? nrhi::CullMode::kBack : nrhi::CullMode::kNone;
  pd.depth_clip=depth_clip;
  auto p=device_->CreateGraphicsPipeline(pd); if(p) pipelines_.push_back({key,depth_clip,p}); return p;
}
bool PresentationPass::Render(nrhi::Cmd* cmd,nrhi::Device* dev,const capture::UiFrame& frame,
                             nrhi::TextureView* world,const float (*ramp)[3],bool world_is_display) {
  auto refuse=[&](const char* reason) {
    Gap(); if((last_reason_!=reason || frame.frame%120==0) && log_lines_<256) {
      ++log_lines_;last_reason_=reason;
      REXLOG_WARN("[native] presentation: fallback frame {} ({}, {} draws, {} refused)",frame.frame,reason,frame.draws.size(),frame.refused);
    }
    return false;
  };
  if(!capture::UiCompositable(frame,bool(world)) || !ramp) return refuse("capture/ramp");
  if(!Ensure(dev)) return refuse("resources");
  if(previous_frame_==frame.frame) return true;
  const uint32_t mode = frame.menu ? 2u : world ? 1u : 4u;
  if (std::getenv("FABLE2_NATIVE_DISCOVERY_UI") && !(diagnostic_modes_ & mode)) {
    diagnostic_modes_ |= mode;
    uint32_t seen[capture::kUiKindCount]{};
    for (size_t i = 0; i < frame.draws.size(); ++i) {
      const auto& d = frame.draws[i];
      if (seen[uint32_t(d.kind)-1]++ >= 3 || d.vertices.empty()) continue;
      const auto& v = d.vertices.front();
      const auto scissor=capture::UiScissorOf(d,1280,720,rex::cvar::GetFlagByName("half_pixel_offset")=="true");
      REXLOG_INFO("[native] presentation input: frame {} draw {} kind {} clip [{},{},{},{}] uv [{},{},{},{}] viewport [{},{},{},{}] window [{:08X},{:08X},{:08X}] scissor [{},{},{},{}]",
        frame.frame,i,uint32_t(d.kind),v.clip[0],v.clip[1],v.clip[2],v.clip[3],v.uv[0],v.uv[1],v.uv[2],v.uv[3],
        d.viewport[0],d.viewport[1],d.viewport[2],d.viewport[3],d.window_offset,d.window_scissor[0],d.window_scissor[1],
        scissor[0],scissor[1],scissor[2],scissor[3]);
    }
  }
  struct Prepared { nrhi::Pipeline* pipeline; std::array<nrhi::TextureView*,3> views; Constants constants; };
  std::vector<Prepared> prepared; prepared.reserve(frame.draws.size());
  std::vector<nrhi::Texture*> planes;
  std::vector<nrhi::TextureView*> plane_views;
  std::vector<nrhi::Buffer*> buffers;
  auto cleanup=[&] {
    for(auto p:plane_views) dev->DestroyDeferred(p);
    for(auto p:planes) dev->DestroyDeferred(p);
    for(auto p:buffers) dev->DestroyDeferred(p);
  };
  auto upload=[&](const void* bytes,uint64_t size) -> nrhi::Buffer* {
    nrhi::BufferDesc bd; bd.heap=nrhi::HeapKind::kUpload; bd.size=size;
    auto b=dev->CreateBuffer(bd); if(!b) return nullptr;
    buffers.push_back(b); void* mapped=dev->Map(b); if(!mapped) return nullptr;
    std::memcpy(mapped,bytes,size); dev->Unmap(b); return b;
  };
  if(!white_uploaded_) {
    uint8_t white[256]{}; std::memset(white,255,4);
    auto b=upload(white,sizeof(white)); if(!b) {cleanup();return refuse("white upload");}
    cmd->CopyBufferToTexture(white_,0,0,b,0,256,1,1,1);
    cmd->Barrier(white_,nrhi::ResourceState::kCopyDest,nrhi::ResourceState::kPixelShaderResource);
    cmd->FlushBarriers(); white_uploaded_=true;
  }
  textures_.BeginFrame(frame.frame,64ull<<20,16ull<<20); TextureStats st;
  const bool half_pixel=rex::cvar::GetFlagByName("half_pixel_offset")=="true";
  struct PreparedMask {nrhi::TextureView* atlas=nullptr;nrhi::Buffer* vertices=nullptr;nrhi::Buffer* constants=nullptr;};
  std::vector<PreparedMask> prepared_masks;
  if(frame.masks.size()>capture::kUiMaxMasks) {cleanup();return refuse("mask count");}
  for(size_t m=0;m<frame.masks.size();m++) {
    const auto& mask=frame.masks[m];const auto& draw=mask.draw;
    if(!mask.base || draw.vertices.size()!=6 || !draw.textures[0]) {cleanup();return refuse("mask producer");}
    if(!masks_[m]) {
      nrhi::TextureDesc td;td.width=640;td.height=360;td.format=nrhi::Format::kR8G8B8A8_UNORM;
      td.usage=nrhi::kTextureUsageRenderTarget;td.initial_state=nrhi::ResourceState::kPixelShaderResource;
      masks_[m]=dev->CreateTexture(td);mask_views_[m]=masks_[m] ? dev->CreateTextureView(masks_[m],{}):nullptr;
    }
    if(!mask_views_[m]) {failed_=true;cleanup();return refuse("mask target");}
    Constants constants;const auto viewport=capture::UiViewportOf(draw,half_pixel);
    std::copy(viewport.begin(),viewport.end(),constants.viewport);
    std::copy(mask.clear,mask.clear+4,constants.c[0]);  // the clear colour (mask_clear)
    if(mask.clear_blend!=0x00010001 && mask.clear_blend!=0x07060706) {cleanup();return refuse("mask clear blend");}
    for(uint32_t i=0;i<4;i++) if(mask.clear_rect[i]<0 || mask.clear_rect[i]>(i&1 ? 360:640)) {cleanup();return refuse("mask clear rectangle");}
    if(mask.clear_rect[0]>=mask.clear_rect[2] || mask.clear_rect[1]>=mask.clear_rect[3]) {cleanup();return refuse("mask clear rectangle");}
    uint32_t sampler=0;float uv[2]{};capture::MaterialStatus status;
    PreparedMask prepared_mask;
    prepared_mask.atlas=textures_.ResolveSnapshot(cmd,dev,draw.fetch[0],*draw.textures[0],&sampler,uv,&status,st);
    if(!prepared_mask.atlas || sampler>=4) {cleanup();return refuse("mask atlas");}
    constexpr uint32_t selectors[]={3,1,2,0};constants.texinfo[0][0]=float(selectors[sampler]);
    constants.texinfo[0][1]=uv[0];constants.texinfo[0][2]=uv[1];
    prepared_mask.vertices=upload(draw.vertices.data(),draw.vertices.size()*sizeof(capture::UiVertex));
    uint8_t bytes[kDrawBytes]{};std::memcpy(bytes,&constants,240);
    prepared_mask.constants=upload(bytes,sizeof(bytes));
    if(!prepared_mask.vertices || !prepared_mask.constants) {cleanup();return refuse("mask buffers");}
    prepared_masks.push_back(prepared_mask);
  }
  for(const auto& draw:frame.draws) {
    if(frame.menu_float10 && ((draw.colour_info>>16)&15)!=12) {cleanup();return refuse("menu source format");}
    Prepared p{}; p.views.fill(white_view_); p.pipeline=Pipeline(draw);
    if(!p.pipeline) {cleanup();return refuse("state/pipeline");}
    std::memcpy(p.constants.c,draw.registers,sizeof(draw.registers));
    const auto viewport=capture::UiViewportOf(draw,half_pixel);
    std::copy(viewport.begin(),viewport.end(),p.constants.viewport);
    // Xenos integer-centred rasterization is half a pixel to the right/down
    // in the host's half-centred rasterizer (SDK draw_util::GetViewportInfo).
    auto rs=capture::DecodeRenderState(draw.state);
    p.constants.alpha[0]=rs.alpha_test ? float(rs.alpha_func) : 7;
    p.constants.alpha[1]=rs.alpha_ref;
    for(uint32_t i=0;i<3;i++) {
      p.constants.texinfo[i][1]=p.constants.texinfo[i][2]=1;
      bool needed=i==0 || draw.kind==capture::UiKind::kVideo;
      if(draw.kind==capture::UiKind::kSprite) needed|=(i==1 && draw.registers[2][0]==1) || (i==2 && draw.registers[5][0]==1);
      if(draw.kind==capture::UiKind::kText) needed|=i==1 && draw.registers[5][0]==1;
      if(draw.kind==capture::UiKind::kParticle) needed=i<draw.particle_textures;
      if(!needed) continue;
      if(draw.masks[i]!=UINT32_MAX) {
        if(draw.masks[i]>=frame.masks.size()) {cleanup();return refuse("mask index");}
        p.views[i]=mask_views_[draw.masks[i]];
        // Pinned masks use normalized, linear/clamp RGBA8 sampling.
        p.constants.texinfo[i][0]=0;
        continue;
      }
      if(draw.kind==capture::UiKind::kVideo) {
        const auto& plane=draw.planes[i];
        if(!plane.width || !plane.height || plane.pixels.size()!=uint64_t(plane.width)*plane.height) {cleanup();return refuse("plane");}
        nrhi::TextureDesc td; td.width=plane.width; td.height=plane.height; td.format=nrhi::Format::kR8_UNORM;
        auto tex=dev->CreateTexture(td); if(!tex) {cleanup();return refuse("video texture");} planes.push_back(tex);
        auto view=dev->CreateTextureView(tex,{}); if(!view) {cleanup();return refuse("video view");} plane_views.push_back(view);p.views[i]=view;
        uint32_t pitch=(plane.width+255)&~255u;
        std::vector<uint8_t> bytes(uint64_t(pitch)*plane.height);
        for(uint32_t y=0;y<plane.height;y++) std::memcpy(bytes.data()+uint64_t(y)*pitch,plane.pixels.data()+uint64_t(y)*plane.width,plane.width);
        auto b=upload(bytes.data(),bytes.size()); if(!b) {cleanup();return refuse("video upload");}
        cmd->CopyBufferToTexture(tex,0,0,b,0,pitch,plane.width,plane.height,1);
        cmd->Barrier(tex,nrhi::ResourceState::kCopyDest,nrhi::ResourceState::kPixelShaderResource);
      } else {
        if(!draw.textures[i]) {cleanup();return refuse("asset snapshot");}
        uint32_t sampler=0; float uv[2]{}; capture::MaterialStatus status;
        p.views[i]=textures_.ResolveSnapshot(cmd,dev,draw.fetch[i],*draw.textures[i],&sampler,uv,&status,st);
        if(!p.views[i]) {cleanup();return refuse(capture::MaterialStatusName(status));}
        // TextureCache sampler order: point wrap, linear wrap, point clamp, linear clamp.
        static constexpr uint32_t selectors[]={3,1,2,0};
        if(sampler>=4) {cleanup();return refuse("sampler");}
        p.constants.texinfo[i][0]=float(selectors[sampler]);
        p.constants.texinfo[i][1]=uv[0]; p.constants.texinfo[i][2]=uv[1];
      }
    }
    prepared.push_back(std::move(p));
  }
  std::vector<capture::UiVertex> vertices; vertices.reserve(frame.vertices);
  std::vector<uint8_t> constants(frame.draws.size()*kDrawBytes+kFullBytes);
  for(size_t i=0;i<frame.draws.size();i++) {
    vertices.insert(vertices.end(),frame.draws[i].vertices.begin(),frame.draws[i].vertices.end());
    std::memcpy(constants.data()+i*kDrawBytes,&prepared[i].constants,240);
  }
  Constants finish;
  finish.finish[2]=frame.menu ? (frame.menu_float10 ? 2.0f:1.0f):0.0f;
  finish.finish[3]=world_is_display ? 1.0f : 0.0f;
  if(world_is_display && !capture::UiInverseRamp(ramp,finish.ramp+256)) {cleanup();return refuse("inverse ramp");}
  finish.finish[0]=frame.menu ? frame.menu_power : 1;
  finish.finish[1]=previous_frame_+1==frame.frame ? frame.previous_share : 0;
  for(uint32_t i=0;i<256;i++) for(uint32_t c=0;c<3;c++) finish.ramp[i][c]=ramp[i][c];
  uint64_t full_offset=frame.draws.size()*kDrawBytes;
  std::memcpy(constants.data()+full_offset,&finish,sizeof(finish));
  // A world frame can legitimately have no HUD during a camera transition.
  // Fullscreen composition still binds a valid, unused vertex SRV.
  if(vertices.empty()) vertices.emplace_back();
  auto vb=upload(vertices.data(),vertices.size()*sizeof(capture::UiVertex));
  auto cb=upload(constants.data(),constants.size());
  if(!vb || !cb) {cleanup();return refuse("buffers");}
  cmd->FlushBarriers(); cmd->ProfileRegion(nrhi::ProfileStage::k2d);
  if(!prepared_masks.empty()) {
    // The game's EDRAM mask target holds whatever the previous pass left; a
    // mask's rows outside its clear rectangle and copy quad come from the
    // mask before it in this frame. The scratch starts the frame transparent
    // black (the game's stale content there is not reproduced: disclosed).
    cmd->Barrier(mask_scratch_,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget);cmd->FlushBarriers();
    const float transparent[4]{0,0,0,0};
    cmd->SetRenderTargets(mask_scratch_,nullptr);cmd->ClearRenderTarget(mask_scratch_,transparent);
  }
  for(size_t m=0;m<prepared_masks.size();m++) {
    const auto& mask=frame.masks[m];const auto& p=prepared_masks[m];
    // 1. The clear: the colour over the rectangle under the captured blend.
    cmd->SetRenderTargets(mask_scratch_,nullptr);cmd->SetViewport({0,0,640,360,0,1});
    cmd->SetScissor({mask.clear_rect[0],mask.clear_rect[1],mask.clear_rect[2],mask.clear_rect[3]});
    cmd->SetBindingLayout(layout_);cmd->SetPipeline(mask.clear_blend==0x00010001 ? mask_clear_opaque_:mask_clear_blend_);
    cmd->SetConstantBuffer(0,p.constants,0);cmd->SetBufferSrv(1,p.vertices,0);
    nrhi::TextureView* clear_views[]={white_view_,white_view_,white_view_};cmd->SetTextures(2,clear_views,3);
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);cmd->Draw(3,0);
    // 2. The atlas copy quad, alpha blended, under the game's window.
    const auto scissor=capture::UiScissorOf(mask.draw,640,360,half_pixel);
    cmd->SetScissor({scissor[0],scissor[1],scissor[2],scissor[3]});
    cmd->SetPipeline(mask_pipeline_);cmd->SetConstantBuffer(0,p.constants,0);
    cmd->SetBufferSrv(1,p.vertices,0);nrhi::TextureView* views[]={p.atlas,white_view_,white_view_};cmd->SetTextures(2,views,3);
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);cmd->Draw(6,0);
    // 3. The resolve: the scratch texel for texel into this mask's texture.
    cmd->Barrier(mask_scratch_,nrhi::ResourceState::kRenderTarget,nrhi::ResourceState::kPixelShaderResource);
    cmd->Barrier(masks_[m],nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget);cmd->FlushBarriers();
    cmd->SetRenderTargets(masks_[m],nullptr);cmd->SetViewport({0,0,640,360,0,1});cmd->SetScissor({0,0,640,360});
    cmd->SetPipeline(mask_resolve_);cmd->SetConstantBuffer(0,p.constants,0);cmd->SetBufferSrv(1,p.vertices,0);
    nrhi::TextureView* resolve_views[]={mask_scratch_view_,white_view_,white_view_};cmd->SetTextures(2,resolve_views,3);
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);cmd->Draw(3,0);
    cmd->Barrier(masks_[m],nrhi::ResourceState::kRenderTarget,nrhi::ResourceState::kPixelShaderResource);
    if(m+1<prepared_masks.size()) cmd->Barrier(mask_scratch_,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget);
    cmd->FlushBarriers();
  }
  auto full=[&](nrhi::Texture* target,nrhi::Pipeline* pipeline,nrhi::TextureView* a,nrhi::TextureView* b) {
    cmd->Barrier(target,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget); cmd->FlushBarriers();
    cmd->SetRenderTargets(target,nullptr); cmd->SetViewport({0,0,1280,720,0,1});cmd->SetScissor({0,0,1280,720});
    cmd->SetBindingLayout(layout_);cmd->SetPipeline(pipeline);cmd->SetConstantBuffer(0,cb,full_offset);
    cmd->SetBufferSrv(1,vb,0);nrhi::TextureView* views[]={a,b ? b:white_view_,white_view_};cmd->SetTextures(2,views,3);
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);cmd->Draw(3,0);
    cmd->Barrier(target,nrhi::ResourceState::kRenderTarget,nrhi::ResourceState::kPixelShaderResource);cmd->FlushBarriers();
  };
  if(world && !frame.menu) full(target_,copy_,world,nullptr);
  cmd->Barrier(target_,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget);cmd->FlushBarriers();
  cmd->SetRenderTargets(target_,depth_);cmd->ClearDepth(depth_,1);
  if(!world || frame.menu) {const float black[4]={0,0,0,1};cmd->ClearRenderTarget(target_,black);}
  cmd->SetViewport({0,0,1280,720,0,1});cmd->SetBindingLayout(layout_);cmd->SetBufferSrv(1,vb,0);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  uint32_t start=0;
  for(size_t i=0;i<frame.draws.size();i++) {
    const auto& d=frame.draws[i]; const auto& p=prepared[i];
    const auto scissor=capture::UiScissorOf(d,1280,720,half_pixel);
    cmd->SetScissor({scissor[0],scissor[1],scissor[2],scissor[3]});
    cmd->SetPipeline(p.pipeline);cmd->SetConstantBuffer(0,cb,i*kDrawBytes);cmd->SetTextures(2,p.views.data(),3);
    cmd->SetBufferSrv(1,vb,uint64_t(start)*sizeof(capture::UiVertex));
    cmd->Draw(uint32_t(d.vertices.size()),0);start+=uint32_t(d.vertices.size());
  }
  cmd->Barrier(target_,nrhi::ResourceState::kRenderTarget,nrhi::ResourceState::kPixelShaderResource);cmd->FlushBarriers();
  if(coverage_wanted_) {
    // The same sprite/text draws once more, their alpha accumulated over.
    cmd->Barrier(coverage_,nrhi::ResourceState::kPixelShaderResource,nrhi::ResourceState::kRenderTarget);cmd->FlushBarriers();
    const float none[4]={0,0,0,0};
    cmd->SetRenderTargets(coverage_,nullptr);cmd->ClearRenderTarget(coverage_,none);
    cmd->SetViewport({0,0,1280,720,0,1});cmd->SetBindingLayout(layout_);
    cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
    uint32_t at=0;
    for(size_t i=0;i<frame.draws.size();i++) {
      const auto& d=frame.draws[i]; const auto& p=prepared[i];
      const uint32_t kind=uint32_t(d.kind);
      if(kind==1 || kind==2) {
        const auto scissor=capture::UiScissorOf(d,1280,720,half_pixel);
        cmd->SetScissor({scissor[0],scissor[1],scissor[2],scissor[3]});
        cmd->SetPipeline(coverage_pipeline_[kind-1]);cmd->SetConstantBuffer(0,cb,i*kDrawBytes);cmd->SetTextures(2,p.views.data(),3);
        cmd->SetBufferSrv(1,vb,uint64_t(at)*sizeof(capture::UiVertex));
        cmd->Draw(uint32_t(d.vertices.size()),0);
      }
      at+=uint32_t(d.vertices.size());
    }
    cmd->Barrier(coverage_,nrhi::ResourceState::kRenderTarget,nrhi::ResourceState::kPixelShaderResource);cmd->FlushBarriers();
  }
  full(front_[front_write_],history_,target_view_,front_view_[front_write_^1]);
  full(output_,display_,front_view_[front_write_],nullptr);
  previous_frame_=frame.frame;front_write_^=1;
  last_rects_.clear();last_rects_frame_=frame.frame;
  for(const auto& d:frame.draws) {
    if(d.vertices.empty()) continue;
    const auto vp=capture::UiViewportOf(d,half_pixel);
    const auto sc=capture::UiScissorOf(d,1280,720,half_pixel);
    DrawRect r{uint32_t(d.kind),1e9f,1e9f,-1e9f,-1e9f};
    for(const auto& v:d.vertices) {
      const float w=v.clip[3]==0 ? 1e-6f:v.clip[3];
      const float x=(v.clip[0]*vp[0]+vp[1]*v.clip[3])/w,y=(v.clip[1]*vp[2]+vp[3]*v.clip[3])/w;
      r.x0=std::min(r.x0,x);r.y0=std::min(r.y0,y);r.x1=std::max(r.x1,x);r.y1=std::max(r.y1,y);
    }
    r.x0=std::max(r.x0,float(sc[0]));r.y0=std::max(r.y0,float(sc[1]));r.x1=std::min(r.x1,float(sc[2]));r.y1=std::min(r.y1,float(sc[3]));
    if(r.x1>r.x0 && r.y1>r.y0) last_rects_.push_back(r);
  }
  if((last_reason_!="native" || frame.frame%120==0) && log_lines_<256) {
    ++log_lines_;last_reason_="native";
    uint32_t kinds[capture::kUiKindCount]{};for(const auto& d:frame.draws) ++kinds[uint32_t(d.kind)-1];
    REXLOG_INFO("[native] presentation: native frame {}, {} draws (sprite {}, text {}, video {}, particles {}, fade {}), {} vertices, menu {}, history {}, owned masks {}, foreign masks {}",frame.frame,frame.draws.size(),kinds[0],kinds[1],kinds[2],kinds[3],kinds[4],frame.vertices,frame.menu,finish.finish[1],frame.masks.size(),frame.foreign_masks);
  }
  cleanup(); return true;
}
}  // namespace fable2::native::render
