#include "clay_pass.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <rex/logging.h>

#include "../capture/xdk_hook_ids.h"
#include "../fable2_native_shaders.h"

namespace fable2::native::render {

namespace {
constexpr float kClearColor[4] = {0.08f, 0.08f, 0.1f, 1.0f};
constexpr uint32_t kConstantCount = sizeof(ClayConstants) / 4;  // 36
static_assert(kConstantCount == 36);
// Binding parameters (see Ensure). Root signature cost: 36 words of constants,
// 2 per buffer and per constant buffer, 1 per texture table: 46 of 64.
constexpr uint32_t kParamConstants = 0, kParamPositions = 1, kParamIndices = 2, kParamUvs = 3,
                   kParamAlbedo = 4, kParamTerrainTextures = 5, kParamTerrainConstants = 6;
// The terrain constant buffer's size is a multiple of this (256 slots), so the
// pool's same-size reuse holds while the number of terrain draws moves.
constexpr uint64_t kTerrainBufferStep = 256ull * kClayTerrainSlotBytes;

// PipelineKey values (capture enums) as the RHI's. A key holds only values the
// RHI has (MakePipelineKey); the defaults below are never reached with one.
nrhi::BlendFactor ToFactor(uint8_t f) {
  switch (capture::Factor(f)) {
    case capture::Factor::kZero: return nrhi::BlendFactor::kZero;
    case capture::Factor::kSrcAlpha: return nrhi::BlendFactor::kSrcAlpha;
    case capture::Factor::kInvSrcAlpha: return nrhi::BlendFactor::kInvSrcAlpha;
    default: return nrhi::BlendFactor::kOne;
  }
}

nrhi::BlendOp ToBlendOp(uint8_t op) {
  return capture::BlendOp(op) == capture::BlendOp::kMin ? nrhi::BlendOp::kMin : nrhi::BlendOp::kAdd;
}

nrhi::CompareFunc ToCompare(uint8_t f) {
  switch (capture::Compare(f)) {
    case capture::Compare::kAlways: return nrhi::CompareFunc::kAlways;
    case capture::Compare::kLessEqual: return nrhi::CompareFunc::kLessEqual;
    default: return nrhi::CompareFunc::kLess;
  }
}

// PipelineKey::cull is in the pass's window space with clockwise front faces
// (ClayCull), which is the RHI's (D3D12's default front face).
nrhi::CullMode ToCull(uint8_t c) {
  switch (capture::Cull(c)) {
    case capture::Cull::kFront: return nrhi::CullMode::kFront;
    case capture::Cull::kBack: return nrhi::CullMode::kBack;
    default: return nrhi::CullMode::kNone;
  }
}
}  // namespace

void ClayPass::DestroyObjects() {
  if (device_) {
    for (nrhi::Pipeline* p : pipelines_) device_->DestroyDeferred(p);
    device_->DestroyDeferred(vs_);
    for (nrhi::Shader* ps : ps_) device_->DestroyDeferred(ps);
    terrain_destroy_.clear();
    terrain_pool_.Drain(&terrain_destroy_);
    for (nrhi::Buffer* b : terrain_destroy_) device_->DestroyDeferred(b);
    device_->DestroyDeferred(terrain_cb_);
    device_->DestroyDeferred(color_);
    device_->DestroyDeferred(depth_);
    device_->DestroyDeferred(white_view_);
    device_->DestroyDeferred(white_);
    device_->DestroyDeferred(white_staging_);
  }
  pipeline_keys_.clear();
  pipelines_.clear();
  vs_ = nullptr;
  for (nrhi::Shader*& ps : ps_) ps = nullptr;
  terrain_destroy_.clear();
  terrain_cb_ = nullptr;
  color_ = depth_ = nullptr;
  white_ = nullptr;
  white_view_ = nullptr;
  white_staging_ = nullptr;
  ready_ = false;
}

// The default key (an invalid state) is the pass's pipeline before render
// states: opaque, depth Less with write, no cull.
nrhi::Pipeline* ClayPass::CreatePipeline(nrhi::Device* dev, const PipelineKey& key) {
  nrhi::GraphicsPipelineDesc pipe;
  pipe.layout = layout_;
  pipe.vs = vs_;
  pipe.ps = ps_[key.textured < kClayPixelShaders ? key.textured : 0];
  pipe.rtv_format = nrhi::Format::kR8G8B8A8_UNORM;
  pipe.dsv_format = nrhi::Format::kD32_FLOAT;
  pipe.blend.enable = key.blend != 0;
  pipe.blend.src = ToFactor(key.src);
  pipe.blend.dst = ToFactor(key.dst);
  pipe.blend.op = ToBlendOp(key.op);
  pipe.blend.src_alpha = ToFactor(key.src_alpha);
  pipe.blend.dst_alpha = ToFactor(key.dst_alpha);
  pipe.blend.op_alpha = ToBlendOp(key.op_alpha);
  pipe.blend.write_mask = key.color_mask;  // 0 with depth write: a depth-only draw
  pipe.depth.test_enable = key.depth_test != 0;
  pipe.depth.write_enable = key.depth_write != 0;
  pipe.depth.func = ToCompare(key.depth_func);
  pipe.cull = ToCull(key.cull);
  return dev->CreateGraphicsPipeline(pipe);
}

nrhi::Pipeline* ClayPass::PipelineFor(nrhi::Device* dev, const PipelineKey& key, ClayStats& st) {
  const size_t slot = FindOrAddPipelineSlot(pipeline_keys_, key);
  if (slot != kNoPipelineSlot) {
    if (slot == pipelines_.size()) {  // a new key
      pipelines_.push_back(CreatePipeline(dev, key));
      if (!pipelines_.back()) {
        REXLOG_ERROR("[native] clay: pipeline creation failed for a render state (its draws use the default)");
      }
    }
    if (pipelines_[slot]) return pipelines_[slot];
  }
  ++st.pipeline_overflow;
  return pipelines_[key.textured < kClayPixelShaders ? key.textured : 0];
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

bool ClayPass::Ensure(nrhi::Device* dev) {
  if (ready_ && dev == device_) return true;
  if (dev != device_) {
    // A different device: the old objects are not ours to destroy through it.
    geometry_.Release(nullptr);
    textures_.Release(nullptr);
    pipeline_keys_.clear();
    pipelines_.clear();
    vs_ = nullptr;
    for (nrhi::Shader*& ps : ps_) ps = nullptr;
    terrain_destroy_.clear();
    terrain_pool_.Drain(&terrain_destroy_);
    terrain_destroy_.clear();
    terrain_cb_ = nullptr;
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
    // The terrain shaders' own table (t4 the colour, t5 the weight cells) and
    // constant buffer (b1); the other shaders declare neither.
    desc.params[kParamTerrainTextures].kind = nrhi::BindingParamKind::kTextureTable;
    desc.params[kParamTerrainTextures].shader_register = 4;
    desc.params[kParamTerrainTextures].count = 2;
    desc.params[kParamTerrainTextures].visibility = nrhi::Visibility::kPixel;
    desc.params[kParamTerrainConstants].kind = nrhi::BindingParamKind::kConstantBuffer;
    desc.params[kParamTerrainConstants].shader_register = 1;
    desc.params[kParamTerrainConstants].visibility = nrhi::Visibility::kPixel;
    desc.param_count = 7;
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
  static constexpr const char* kPsNames[kClayPixelShaders] = {"fable2_clay_ps", "fable2_clay_textured_ps",
                                                              "fable2_clay_terrain_layer_ps",
                                                              "fable2_clay_terrain_far_ps"};
  static constexpr const char* kPsSources[kClayPixelShaders] = {shaders::kClayPs, shaders::kClayTexturedPs,
                                                                shaders::kClayTerrainLayerPs,
                                                                shaders::kClayTerrainFarPs};
  for (uint8_t textured = 0; textured < kClayPixelShaders; ++textured) {
    nrhi::ShaderDesc sd;
    sd.stage = nrhi::ShaderStage::kPixel;
    sd.name = kPsNames[textured];
    sd.hlsl_source = kPsSources[textured];
    sd.entry_point = "main";
    ps_[textured] = dev->CreateShader(sd);
    if (!ps_[textured]) {
      REXLOG_ERROR("[native] clay: pixel shader compile failed");
      return false;
    }
  }
  // Cache slots 0 to 3: the default pipelines (PipelineFor falls back to them).
  for (uint8_t textured = 0; textured < kClayPixelShaders; ++textured) {
    PipelineKey key;
    key.textured = textured;
    pipeline_keys_.push_back(key);
    pipelines_.push_back(CreatePipeline(dev, key));
    if (!pipelines_.back()) {
      REXLOG_ERROR("[native] clay: pipeline creation failed");
      return false;
    }
  }
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

nrhi::TextureView* ClayPass::ResolveTerrainTexture(nrhi::Cmd* cmd, nrhi::Device* dev, const uint32_t fetch[6],
                                                   bool memo, uint32_t* sampler, float* uv_fix,
                                                   capture::MaterialStatus* status, TextureStats& tst) {
  if (memo) {
    for (const TerrainTexture& t : terrain_textures_) {
      if (std::memcmp(t.fetch, fetch, sizeof(t.fetch)) != 0) continue;
      *sampler = t.sampler;
      uv_fix[0] = t.uv_fix[0];
      uv_fix[1] = t.uv_fix[1];
      *status = t.status;
      return t.view;
    }
  }
  TerrainTexture t;
  std::memcpy(t.fetch, fetch, sizeof(t.fetch));
  capture::Material m;  // the cache reads the fetch constant only
  std::memcpy(m.fetch, fetch, sizeof(m.fetch));
  t.view = textures_.Resolve(cmd, dev, m, &t.sampler, t.uv_fix, &t.status, tst);
  if (memo) terrain_textures_.push_back(t);
  *sampler = t.sampler;
  uv_fix[0] = t.uv_fix[0];
  uv_fix[1] = t.uv_fix[1];
  *status = t.status;
  return t.view;
}

void ClayPass::PrepareTerrain(nrhi::Cmd* cmd, nrhi::Device* dev, const capture::DrawRecord& r, bool textured,
                              ClayStats& st, TextureStats& tst) {
  using capture::MaterialStatus;
  Prepared p;
  p.r = &r;
  p.terrain = true;
  uint32_t buffer_vertices = 0;
  p.positions = geometry_.Positions(dev, r, &buffer_vertices, st);
  if (p.positions) {
    p.vertex_count = DrawVertexCount(buffer_vertices, RecordVertexCount(r));
    p.indices = geometry_.Indices(dev, r, p.vertex_count, &p.index_count, st);
  }
  // The adaptive builder draws the far ground and the layers' underlay; every
  // other terrain record is a layer of the layer builder.
  const bool adaptive = r.func_id == capture::kHook_DrawIndx_82207C30;
  TerrainPlanItem item;
  item.patch = &r.terrain;
  item.layer = !adaptive;
  item.drawable = p.positions && p.indices;
  if (textured && item.drawable) {
    const capture::TerrainMaterialInputs& m = r.terrain.material;
    const capture::MaterialKind kind = r.material.kind;
    MaterialStatus colour = MaterialStatus::kTextured, weight = MaterialStatus::kTextured;
    bool far_uvs = true;
    // Nothing is asked of the caches for a record whose inputs are not usable.
    if (TerrainMaterialStatus(m, kind, adaptive, colour, weight, far_uvs) == MaterialStatus::kTextured) {
      // The layers share a few detail textures and one weight atlas; most far draws have their own colour.
      p.albedo = ResolveTerrainTexture(cmd, dev, m.detail_fetch, !adaptive, &p.sampler, p.uv_fix, &colour, tst);
      if (adaptive) {
        p.uvs = p.albedo ? geometry_.TerrainFarUvs(dev, r, p.vertex_count, st) : nullptr;
        far_uvs = p.uvs != nullptr;
      } else {
        float fix[2];  // the weights' coordinate is in texels of the guest extent; the decode pads nothing
        p.weights = ResolveTerrainTexture(cmd, dev, m.weight_fetch, true, &p.weight_sampler, fix, &weight, tst);
      }
    }
    p.status = TerrainMaterialStatus(m, kind, adaptive, colour, weight, far_uvs);
    // A blend the RHI cannot express (no terrain row has one) leaves the record
    // without a material too: left out of its square's sum it would darken it.
    if (p.status == MaterialStatus::kTextured &&
        DecideDrawState(capture::DecodeRenderState(r.state_words), r.material, TerrainTextured(kind), false).skip) {
      p.status = MaterialStatus::kTerrainInputs;
    }
    item.usable = p.status == MaterialStatus::kTextured;
  }
  p.plan = uint32_t(terrain_items_.size());
  terrain_items_.push_back(item);
  prepared_.push_back(p);
}

void ClayPass::FinishTerrain(nrhi::Device* dev, bool textured, ClayStats& st, TextureStats& tst) {
  using capture::MaterialStatus;
  if (terrain_items_.empty()) return;
  uint8_t* slots = nullptr;
  if (textured) {
    // One slot per terrain record: at most that many are drawn with the material.
    const uint64_t bytes = (uint64_t(terrain_items_.size()) * kClayTerrainSlotBytes + kTerrainBufferStep - 1) /
                           kTerrainBufferStep * kTerrainBufferStep;
    if (auto reused = terrain_pool_.Take(bytes, dev->CompletedSubmission())) {
      terrain_cb_ = *reused;
    } else {
      nrhi::BufferDesc desc;
      desc.size = bytes;
      desc.heap = nrhi::HeapKind::kUpload;
      desc.bind_class = nrhi::BufferBindClass::kFull;
      terrain_cb_ = dev->CreateBuffer(desc);
    }
    terrain_cb_bytes_ = bytes;
    slots = terrain_cb_ ? static_cast<uint8_t*>(dev->Map(terrain_cb_)) : nullptr;  // kept mapped
    if (!slots) {
      // No constants, no material: every square is flat this frame.
      dev->DestroyDeferred(terrain_cb_);
      terrain_cb_ = nullptr;
      for (Prepared& p : prepared_) {
        if (p.terrain && p.status == MaterialStatus::kTextured) p.status = MaterialStatus::kTextureBad;
      }
      for (TerrainPlanItem& item : terrain_items_) item.usable = false;
    }
    PlanTerrain(terrain_items_);
  } else {
    // The texture path is off: every terrain record is flat clay, drawn opaque.
    for (TerrainPlanItem& item : terrain_items_) {
      item.action = item.drawable ? TerrainAction::kFlat : TerrainAction::kSkip;
    }
  }
  terrain_pairs_.clear();
  uint32_t slot = 0;
  for (Prepared& p : prepared_) {
    if (!p.terrain) continue;
    const TerrainPlanItem& item = terrain_items_[p.plan];
    const capture::DrawRecord& r = *p.r;
    if (item.action == TerrainAction::kSkip) {
      // (a record without geometry was counted by the geometry cache)
      if (item.drawable) ++st.terrain_skipped;
      continue;
    }
    const bool material = item.action == TerrainAction::kMaterial;
    if (material) {
      p.textured = TerrainTextured(r.material.kind);
    } else {
      p.albedo = p.weights = nullptr;
      p.uvs = nullptr;
    }
    const DrawState d = DecideDrawState(capture::DecodeRenderState(r.state_words), r.material, p.textured, !material);
    p.alpha = d.alpha;
    p.pipeline = PipelineFor(dev, d.key, st);
    if (d.alpha_tested) ++st.alpha_tested;
    if (d.blended) ++st.blended;
    if (d.state_invalid) ++st.state_invalid;
    if (d.alpha_unknown) {
      ++st.alpha_unknown;
      st.unknown_by_ps.Add(r.material.ps_hash, kTallyAlphaUnknown);
    }
    if (material) {
      p.slot = slot++;
      // The weights are read with a linear filter whatever the fetch constant says (it is the
      // vertex shader's, point sampled at the cell's texel centres).
      const ClayTerrainConstants c = MakeClayTerrainConstants(r.terrain.material, r.material.kind, p.uv_fix,
                                                              p.sampler, p.weight_sampler | 1u);
      std::memcpy(slots + size_t(p.slot) * kClayTerrainSlotBytes, &c, sizeof(c));
      const std::pair<nrhi::TextureView*, nrhi::TextureView*> pair{p.albedo, p.weights};
      if (std::find(terrain_pairs_.begin(), terrain_pairs_.end(), pair) == terrain_pairs_.end()) {
        terrain_pairs_.push_back(pair);
      }
    }
    if (textured) {
      // The final status of a drawn terrain record: textured, its own reason, or its square's.
      const MaterialStatus status = material ? MaterialStatus::kTextured
                                    : p.status == MaterialStatus::kTextured ? MaterialStatus::kTerrainSquare
                                                                            : p.status;
      ++tst.status[size_t(status)];
      ++tst.terrain;
      if (material) {
        ++tst.textured;
        ++tst.terrain_textured;
      }
    }
  }
  st.terrain_tuples = uint32_t(terrain_pairs_.size());
}

void ClayPass::Render(nrhi::Cmd* cmd, nrhi::Device* dev, const FrameScene& scene, ClayColor color,
                      bool textured, ClayStats& st, TextureStats& tst) {
  if (!ready_ || dev != device_) return;
  // A latched texture cache (RHI failure) resolves nothing: skip the UV decode
  // too, and report the latch on F3.
  textured = textured && !textures_.latched();
  const auto t0 = std::chrono::steady_clock::now();
  const double hash0 = st.hash_ms, decode0 = st.decode_ms, tex0 = tst.decode_ms;

  // Geometry, albedo and render state of every drawable record. Texture
  // uploads record copies and barriers, so this runs before the render targets
  // are bound; pipelines are created here too.
  prepared_.clear();
  terrain_items_.clear();
  terrain_textures_.clear();
  for (const capture::DrawRecord& r : scene.draws) {
    if (r.terrain.active) {
      PrepareTerrain(cmd, dev, r, textured, st, tst);
      continue;
    }
    const capture::RenderState rs = capture::DecodeRenderState(r.state_words);
    const bool may_texture = textured && r.material.status == capture::MaterialStatus::kTextured;
    auto leave_out = [&] {
      ++st.blend_unknown;
      st.unknown_by_ps.Add(r.material.ps_hash, kTallyBlendUnknown);
    };
    // A blended draw that cannot be textured is left out before any geometry
    // is built for it.
    if (!may_texture && DecideDrawState(rs, r.material, 0, false).skip) {
      leave_out();
      continue;
    }
    Prepared p;
    p.r = &r;
    uint32_t buffer_vertices = 0;
    p.positions = geometry_.Positions(dev, r, &buffer_vertices, st);
    if (!p.positions) continue;
    p.vertex_count = DrawVertexCount(buffer_vertices, RecordVertexCount(r));
    p.indices = geometry_.Indices(dev, r, p.vertex_count, &p.index_count, st);
    if (!p.indices) continue;
    capture::MaterialStatus status = r.material.status;  // capture statuses pass through
    if (may_texture) {
      // UVs first: a stream that does not decode costs no texture upload.
      p.uvs = geometry_.Uvs(dev, r, p.vertex_count, st);
      if (!p.uvs) {
        status = capture::MaterialStatus::kUvUnsupported;
      } else {
        p.albedo = textures_.Resolve(cmd, dev, r.material, &p.sampler, p.uv_fix, &status, tst);
        if (!p.albedo) p.uvs = nullptr;
      }
    }
    const DrawState d = DecideDrawState(rs, r.material, p.albedo ? 1 : 0, false);
    if (d.skip) {  // blended, and its texture is not usable this frame
      leave_out();
      continue;
    }
    if (textured) {
      if (size_t(status) < size_t(capture::MaterialStatus::kCount)) ++tst.status[size_t(status)];
      if (p.albedo) ++tst.textured;
    }
    p.alpha = d.alpha;
    p.pipeline = PipelineFor(dev, d.key, st);
    if (d.alpha_tested) ++st.alpha_tested;
    if (d.blended) ++st.blended;
    if (d.state_invalid) ++st.state_invalid;
    if (d.alpha_unknown) {
      ++st.alpha_unknown;
      st.unknown_by_ps.Add(r.material.ps_hash, kTallyAlphaUnknown);
    }
    prepared_.push_back(p);
  }
  FinishTerrain(dev, textured, st, tst);
  // A slot whose creation failed holds no pipeline.
  st.pipelines = uint32_t(pipelines_.size() - std::count(pipelines_.begin(), pipelines_.end(),
                                                             static_cast<nrhi::Pipeline*>(nullptr)));

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
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  // The terrain table and constant buffer hold something from the first draw on
  // (only the terrain shaders read them, and each terrain draw binds its own).
  cmd->SetTexturePair(kParamTerrainTextures, white_view_, white_view_);
  if (terrain_cb_) cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_, 0);

  nrhi::Pipeline* bound = nullptr;
  for (const Prepared& p : prepared_) {
    if (!p.pipeline) continue;  // a terrain record that is not drawn (FinishTerrain)
    const capture::DrawRecord& r = *p.r;
    if (p.pipeline != bound) {
      cmd->SetPipeline(p.pipeline);
      bound = p.pipeline;
    }
    // A terrain record with its material has its textures in the terrain table;
    // a far one reads its colour coordinates from t2 like a textured draw.
    const bool terrain_material = p.textured != 0;
    const bool has_albedo = p.albedo != nullptr && !terrain_material;
    const bool has_uvs = has_albedo || p.textured == kTexturedTerrainFar;
    float uv[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    if (has_albedo) {
      // BC textures are padded to whole blocks: scale into the guest extent.
      const float* x = r.material.uv_xform;
      uv[0] = x[0] * p.uv_fix[0];
      uv[1] = x[1] * p.uv_fix[1];
      uv[2] = x[2] * p.uv_fix[0];
      uv[3] = x[3] * p.uv_fix[1];
    } else if (has_uvs) {
      uv[0] = p.uv_fix[0];
      uv[1] = p.uv_fix[1];
    }
    const ClayConstants constants = MakeClayConstants(r, p.vertex_count, ClayColorValue(color, r),
                                                      uv, has_uvs, p.sampler, p.alpha);
    cmd->SetRootConstants(kParamConstants, kConstantCount, &constants, 0);
    cmd->SetBufferSrv(kParamPositions, p.positions, 0);
    cmd->SetBufferSrv(kParamIndices, p.indices, 0);
    // Draws without UVs never read t2; bind the (in-range) positions.
    cmd->SetBufferSrv(kParamUvs, has_uvs ? p.uvs : p.positions, 0);
    cmd->SetTexture(kParamAlbedo, has_albedo ? p.albedo : white_view_);
    if (terrain_material) {
      // The white view stands in for the weights of a far draw: the backend
      // skips a null entry of a table.
      cmd->SetTexturePair(kParamTerrainTextures, p.albedo, p.weights ? p.weights : white_view_);
      cmd->SetConstantBuffer(kParamTerrainConstants, terrain_cb_, uint64_t(p.slot) * kClayTerrainSlotBytes);
    }
    cmd->Draw(p.index_count, 0);
    ++st.drawn;
    if (r.deformed) ++st.deformed;
    // Which builder made the positions (the order of GeometryCache::Positions).
    if (!r.terrain.active) {
      if (r.instances.active) {
        ++st.instanced;
      } else if (r.skin.active) {
        ++st.skinned;
      }
    }
  }

  cmd->Barrier(color_, nrhi::ResourceState::kRenderTarget,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->Barrier(depth_, nrhi::ResourceState::kDepthWrite,
               nrhi::ResourceState::kPixelShaderResource);
  cmd->FlushBarriers();

  if (terrain_cb_) {
    // In use by this submission: reusable once it has completed.
    terrain_destroy_.clear();
    terrain_pool_.Retire(terrain_cb_, terrain_cb_bytes_, dev->CurrentSubmission(), &terrain_destroy_);
    for (nrhi::Buffer* b : terrain_destroy_) dev->DestroyDeferred(b);
    terrain_destroy_.clear();
    terrain_cb_ = nullptr;
  }

  st.resident_bytes = geometry_.resident_bytes();
  tst.resident_bytes = textures_.resident_bytes();
  tst.latched = textures_.latched();
  const double total =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  st.record_ms += std::max(0.0, total - (st.hash_ms - hash0) - (st.decode_ms - decode0) -
                                    (tst.decode_ms - tex0));
}

}  // namespace fable2::native::render
