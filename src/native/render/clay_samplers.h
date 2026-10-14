#pragma once

#include <rex/graphics/native_rhi.h>

#include "clay_logic.h"

namespace fable2::native::render {

inline uint32_t WaterSurfaceFilterMode(int32_t setting, const TextureFetch& normal) {
  return normal.linear_filter && LevelCount(normal) > 1 ? FilterMode(setting) : 0;
}

inline void ConfigureClaySamplers(rex::graphics::nrhi::BindingLayoutDesc& desc, uint32_t mode) {
  namespace rhi = rex::graphics::nrhi;
  static_assert(rhi::kMaxStaticSamplers >= 4);
  for (uint32_t i = 0; i < 4; ++i) {
    auto& sampler = desc.static_samplers[i];
    sampler.shader_register = i;
    const bool overridden = (i & 1) && mode > 0 && mode < kFilterModes;
    sampler.filter = (i & 1) ? (overridden ? rhi::Filter::kAnisotropic : rhi::Filter::kLinear) : rhi::Filter::kPoint;
    sampler.max_anisotropy = overridden ? FilterAnisotropy(mode) : 1;
    sampler.address = (i & 2) ? rhi::AddressMode::kClamp : rhi::AddressMode::kWrap;
  }
  desc.static_sampler_count = 4;
}

}  // namespace fable2::native::render
