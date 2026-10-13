#include <cstdio>

#include "../../src/native/render/water_sampler.h"

int main() {
  namespace rhi = rex::graphics::nrhi;
  using namespace fable2::native::render;
  constexpr uint32_t expected[] = {1, 1, 2, 4, 8, 16};
  TextureFetch normal{};
  normal.width = normal.height = 256;
  normal.mip_phys = 0x1000;
  normal.mip_max = 8;
  normal.linear_filter = true;
  if (WaterSurfaceFilterMode(3, normal) != 3) return 3;
  normal.mip_max = 0;
  if (WaterSurfaceFilterMode(3, normal) != 0) return 4;
  normal.mip_max = 8;
  normal.mip_phys = 0;
  if (WaterSurfaceFilterMode(3, normal) != 0) return 5;
  normal.mip_phys = 0x1000;
  normal.linear_filter = false;
  if (WaterSurfaceFilterMode(3, normal) != 0) return 6;
  for (int32_t setting : {-1, 0, 1, 2, 3, 4, 5, 6}) {
    rhi::BindingLayoutDesc desc;
    ConfigureWaterSamplers(desc, WaterFilterMode(setting));
    if (desc.static_sampler_count != 4) return 1;
    for (uint32_t i = 0; i < 4; ++i) {
      const auto& sampler = desc.static_samplers[i];
      const bool overridden = (i & 1) && setting > 0 && setting < 6;
      const auto filter = (i & 1) ? (overridden ? rhi::Filter::kAnisotropic : rhi::Filter::kLinear) : rhi::Filter::kPoint;
      if (sampler.shader_register != i || sampler.filter != filter ||
          sampler.max_anisotropy != (overridden ? expected[setting] : 1u) ||
          sampler.address != ((i & 2) ? rhi::AddressMode::kClamp : rhi::AddressMode::kWrap)) {
        std::printf("FAIL water sampler setting=%d register=%u\n", setting, i);
        return 2;
      }
    }
  }
  std::puts("water_sampler PASS (point/wrap/clamp and all shared override modes)");
}
