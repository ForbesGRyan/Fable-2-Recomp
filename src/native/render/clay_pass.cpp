#include "clay_pass.h"

#include <chrono>
#include <cstring>

#include <rex/logging.h>

#include "../fable2_native_shaders.h"

namespace fable2::native::render {

namespace {
constexpr float kClearColor[4] = {0.08f, 0.08f, 0.1f, 1.0f};
constexpr uint32_t kConstantCount = sizeof(ClayConstants) / 4;  // 32
static_assert(kConstantCount == 32);
// Binding parameters (see Ensure).
constexpr uint32_t kParamConstants = 0, kParamPositions = 1, kParamIndices = 2, kParamUvs = 3,
                   kParamAlbedo = 4;
}  // namespace

void ClayPass::DestroyObjects() {
  if (device_) {
    device_->DestroyDeferred(pipeline_);
    device_->DestroyDeferred(vs_);
    device_->DestroyDeferred(ps_);
    device_->DestroyDeferred(color_);
    device_->DestroyDeferred(depth_);
    device_->DestroyDeferred(white_view_);
    device_->DestroyDeferred(white_);
    device_->DestroyDeferred(white_staging_);
  }
  pipeline_ = nullptr;
  vs_ = ps_ = nullptr;
  color_ = depth_ = nullptr;
  white_ = nullptr;
  white_view_ = nullptr;
  white_staging_ = nullptr;
  ready_ = false;
}

bool ClayPass::CreatePipeline(nrhi::Device* dev, bool textured) {
  nrhi::ShaderDesc sd;
  sd.stage = nrhi::ShaderStage::kPixel;
  sd.name = textured ? "fable2_clay_textured_ps" : "fable2_clay_ps";
  sd.hlsl_source = textured ? shaders::kClayTexturedPs : shaders::kClayPs;
  sd.entry_point = "main";
  ps_ = dev->CreateShader(sd);
  if (!ps_) {
    REXLOG_ERROR("[native] clay: pixel shader compile failed");
    return false;
  }
  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = layout_;
  pipe.vs = vs_;
  pipe.ps = ps_;
  pipe.rtv_format = nrhi::Format::kR8G8B8A8_UNORM;
  pipe.dsv_format = nrhi::Format::kD32_FLOAT;
  pipe.depth.test_enable = true;
  pipe.depth.write_enable = true;
  pipe.depth.func = nrhi::CompareFunc::kLess;
  pipe.cull = nrhi::CullMode::kNone;
  pipeline_ = dev->CreateGraphicsPipeline(pipe);
  if (!pipeline_) {
    REXLOG_ERROR("[native] clay: pipeline creation failed");
    return false;
  }
  textured_ps_ = textured;
  return true;
}

// The 1x1 white albedo for untextured draws. Its texel is staged here and
// copied by the next Render (Ensure has no command list).
bool ClayPass::CreateWhite(nrhi::Device* dev) {
  nrhi::TextureDesc desc;
  desc.width = desc.height = 1;
  desc.format = nrhi::Format::kR8G8B8A8_UNORM;
  desc.initial_state = nrhi::ResourceState::kCopyDest;
  white_ = dev->CreateTexture(desc);
  if (!white_) return false;
  white_view_ = dev->CreateTextureView(white_, nrhi::TextureViewDesc{});
  nrhi::BufferDesc bd;
  bd.size = nrhi::kRowPitchAlignment;
  bd.heap = nrhi::HeapKind::kUpload;
  bd.bind_class = nrhi::BufferBindClass::kCopySrc;
  white_staging_ = dev->CreateBuffer(bd);
  if (!white_view_ || !white_staging_) return false;
  void* mapped = dev->Map(white_staging_);
  if (!mapped) return false;
  std::memset(mapped, 0xFF, 4);
  dev->Unmap(white_staging_);
  return true;
}

bool ClayPass::Ensure(nrhi::Device* dev, bool textured) {
  if (ready_ && dev == device_) {
    if (textured == textured_ps_) return true;
    // fable2_native_textures changed: swap the pixel shader only.
    device_->DestroyDeferred(pipeline_);
    device_->DestroyDeferred(ps_);
    pipeline_ = nullptr;
    ps_ = nullptr;
    if (CreatePipeline(dev, textured)) return true;
    DestroyObjects();
    return false;
  }
  if (dev != device_) {
    // A different device: the old objects are not ours to destroy through it.
    geometry_.Release(nullptr);
    textures_.Release(nullptr);
    pipeline_ = nullptr;
    vs_ = ps_ = nullptr;
    color_ = depth_ = nullptr;
    white_ = nullptr;
    white_view_ = nullptr;
    white_staging_ = nullptr;
    layout_ = nullptr;
    device_ = dev;
  } else {
    DestroyObjects();  // retry after a partial failure
  }
  ready_ = false;

  if (!layout_) {
    nrhi::BindingLayoutDesc desc;
    desc.params[kParamConstants].kind = nrhi::BindingParamKind::kConstants;
    desc.params[kParamConstants].shader_register = 0;
    desc.params[kParamConstants].count = kConstantCount;
    desc.params[kParamConstants].visibility = nrhi::Visibility::kAll;
    for (uint32_t p : {kParamPositions, kParamIndices, kParamUvs}) {
      desc.params[p].kind = nrhi::BindingParamKind::kBufferSrv;
      desc.params[p].shader_register = p - kParamPositions;  // t0, t1, t2
      desc.params[p].visibility = nrhi::Visibility::kAll;
    }
    desc.params[kParamAlbedo].kind = nrhi::BindingParamKind::kTextureTable;
    desc.params[kParamAlbedo].shader_register = 3;
    desc.params[kParamAlbedo].count = 1;
    desc.params[kParamAlbedo].visibility = nrhi::Visibility::kPixel;
    desc.param_count = 5;
    // s<i>: bit 0 linear, bit 1 clamp (SamplerIndex).
    for (uint32_t i = 0; i < 4; ++i) {
      desc.static_samplers[i].shader_register = i;
      desc.static_samplers[i].filter = (i & 1) ? nrhi::Filter::kLinear : nrhi::Filter::kPoint;
      desc.static_samplers[i].address = (i & 2) ? nrhi::AddressMode::kClamp : nrhi::AddressMode::kWrap;
    }
    desc.static_sampler_count = 4;
    desc.allow_input_layout = false;
    layout_ = dev->CreateBindingLayout(desc);
    if (!layout_) {
      REXLOG_ERROR("[native] clay: binding layout creation failed");
      return false;
    }
  }

  nrhi::ShaderDesc vs;
  vs.stage = nrhi::ShaderStage::kVertex;
  vs.name = "fable2_clay_vs";
  vs.hlsl_source = shaders::kClayVs;
  vs.entry_point = "main";
  vs_ = dev->CreateShader(vs);
  if (!vs_) {
    REXLOG_ERROR("[native] clay: shader compile failed");
    return false;
  }
  if (!CreatePipeline(dev, textured)) return false;
  if (!CreateWhite(dev)) {
    REXLOG_ERROR("[native] clay: white texture creation failed");
    return false;
  }

  nrhi::TextureDesc color;
  color.width = kWidth;
  color.height = kHeight;
  color.format = nrhi::Format::kR8G8B8A8_UNORM;
  color.usage = nrhi::kTextureUsageRenderTarget;
  color.initial_state = nrhi::ResourceState::kPixelShaderResource;
  for (int i = 0; i < 4; ++i) color.clear_color[i] = kClearColor[i];
  color_ = dev->CreateTexture(color);
  nrhi::TextureDesc depth;
  depth.width = kWidth;
  depth.height = kHeight;
  depth.format = nrhi::Format::kD32_FLOAT;
  depth.usage = nrhi::kTextureUsageDepthStencil;
  depth.initial_state = nrhi::ResourceState::kPixelShaderResource;
  depth.clear_depth = 1.0f;
  depth_ = dev->CreateTexture(depth);
  if (!color_ || !depth_) {
    REXLOG_ERROR("[native] clay: target creation failed");
    return false;
  }
  ready_ = true;
  ++targets_generation_;
  return true;
}

void ClayPass::Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
                      bool textured, ClayStats& st, TextureStats& tst) {
  if (!ready_ || dev != device_) return;
  // A latched texture cache (RHI failure) resolves nothing: skip the UV decode
  // too, and report the latch on F3.
  textured = textured && textured_ps_ && !textures_.latched();
  const auto t0 = std::chrono::steady_clock::now();
  const double hash0 = st.hash_ms, decode0 = st.decode_ms, tex0 = tst.decode_ms;

  // Geometry and albedo of every drawable record. Texture uploads record
  // copies and barriers, so this runs before the render targets are bound.
  prepared_.clear();
  for (const capture::DrawRecord& r : scene.draws) {
    Prepared p;
    p.r = &r;
    uint32_t buffer_vertices = 0;
    p.positions = geometry_.Positions(dev, r, &buffer_vertices, st);
    if (!p.positions) continue;
    p.vertex_count = DrawVertexCount(buffer_vertices, RecordVertexCount(r));
    p.indices = geometry_.Indices(dev, r, p.vertex_count, &p.index_count, st);
    if (!p.indices) continue;
    if (textured) {
      capture::MaterialStatus status = r.material.status;  // capture statuses pass through
      if (status == capture::MaterialStatus::kTextured) {
        // UVs first: a stream that does not decode costs no texture upload.
        p.uvs = geometry_.Uvs(dev, r, p.vertex_count, st);
        if (!p.uvs) {
          status = capture::MaterialStatus::kUvUnsupported;
        } else {
          p.albedo = textures_.Resolve(cmd, dev, r.material, &p.sampler, p.uv_fix, &status, tst);
          if (!p.albedo) p.uvs = nullptr;
        }
      }
      if (size_t(status) < size_t(capture::MaterialStatus::kCount)) ++tst.status[size_t(status)];
      if (p.albedo) ++tst.textured;
    }
    prepared_.push_back(p);
  }

  if (white_staging_) {
    cmd->CopyBufferToTexture(white_, 0, 0, white_staging_, 0, nrhi::kRowPitchAlignment, 1, 1, 1);
    cmd->Barrier(white_, nrhi::ResourceState::kCopyDest, nrhi::ResourceState::kPixelShaderResource);
    dev->DestroyDeferred(white_staging_);
    white_staging_ = nullptr;
  }
  cmd->Barrier(color_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kRenderTarget);
  cmd->Barrier(depth_, nrhi::ResourceState::kPixelShaderResource,
               nrhi::ResourceState::kDepthWrite);
  cmd->FlushBarriers();
  cmd->SetRenderTargets(color_, depth_);
  cmd->ClearRenderTarget(color_, kClearColor);
  cmd->ClearDepth(depth_, 1.0f);
  cmd->SetViewport({0.0f, 0.0f, float(kWidth), float(kHeight), 0.0f, 1.0f});
  cmd->SetScissor({0, 0, int32_t(kWidth), int32_t(kHeight)});
  cmd->SetBindingLayout(layout_);
  cmd->SetPipeline(pipeline_);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);

  for (const Prepared& p : prepared_) {
    const capture::DrawRecord& r = *p.r;
    const bool has_albedo = p.albedo != nullptr;
    float uv[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    if (has_albedo) {
      // BC textures are padded to whole blocks: scale into the guest extent.
      const float* x = r.material.uv_xform;
      uv[0] = x[0] * p.uv_fix[0];
      uv[1] = x[1] * p.uv_fix[1];
      uv[2] = x[2] * p.uv_fix[0];
      uv[3] = x[3] * p.uv_fix[1];
    }
    const ClayConstants constants = MakeClayConstants(r, p.vertex_count, ClayColorValue(color, r),
                                                      uv, has_albedo, p.sampler);
    cmd->SetRootConstants(kParamConstants, kConstantCount, &constants, 0);
    cmd->SetBufferSrv(kParamPositions, p.positions, 0);
    cmd->SetBufferSrv(kParamIndices, p.indices, 0);
    // Untextured draws never read t2; bind the (in-range) positions.
    cmd->SetBufferSrv(kParamUvs, has_albedo ? p.uvs : p.positions, 0);
    cmd->SetTexture(kParamAlbedo, has_albedo ? p.albedo : white_view_);
    cmd->Draw(p.index_count, 0);
    ++st.drawn;
    if (r.deformed) ++st.deformed;
  }

  cmd->Barrier(color_, nrhi::ResourceState::kRenderTarget,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->Barrier(depth_, nrhi::ResourceState::kDepthWrite,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->FlushBarriers();

  st.resident_bytes = geometry_.resident_bytes();
  tst.resident_bytes = textures_.resident_bytes();
  tst.latched = textures_.latched();
  const double total =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  st.record_ms += std::max(0.0, total - (st.hash_ms - hash0) - (st.decode_ms - decode0) -
                                    (tst.decode_ms - tex0));
}

}  // namespace fable2::native::render
