#include "../../src/native/render/presentation_shaders.h"
#include "../../src/native/render/particle_shaders.h"
#include "../../src/native/fable2_native_shaders.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <cstring>
#include <iostream>
int main() {
  using fable2::native::render::ui_shaders::kSource;
  const char* entries[]={"vertex","full_vertex","mask_vertex","sprite","text","video","copy","history","display","particle","fade","mask","mask_clear","mask_resolve","sprite_coverage","text_coverage"};
  for(unsigned i=0;i<12;i++) {
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    auto hr=D3DCompile(kSource,std::strlen(kSource),entries[i],nullptr,nullptr,entries[i],
      i<3 ? "vs_5_0" : "ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(hr)) {
      std::cerr<<entries[i]<<": "<<(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed")<<'\n';
    }
    if(code)code->Release();if(errors)errors->Release();if(FAILED(hr))return 1;
  }
  std::cout<<"Presentation: all sixteen HLSL entry points compile\n";
  for(const char* entry:{"vertex","pixel","soft","full_vertex","copy_depth","half_depth","snow_vertex","snow_pixel"}) {
    const auto source=fable2::native::render::particle_shaders::kSource;
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    auto hr=D3DCompile(source,std::strlen(source),entry,nullptr,nullptr,entry,
      (std::strcmp(entry,"vertex")==0 || std::strcmp(entry,"full_vertex")==0 || std::strcmp(entry,"snow_vertex")==0) ? "vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(hr)) std::cerr<<(errors ? static_cast<const char*>(errors->GetBufferPointer()):"particle compile failed")<<'\n';
    if(code)code->Release();if(errors)errors->Release();if(FAILED(hr))return 1;
  }
  std::cout<<"Particles: all eight HLSL entry points compile\n";
  for(const char* source:{fable2::native::shaders::kClayPostDofDownPs,
                        fable2::native::shaders::kClayPostDofBlurPs,
                        fable2::native::shaders::kClayPostDofCompositePs}) {
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    auto hr=D3DCompile(source,std::strlen(source),"native_dof",nullptr,nullptr,"main","ps_5_0",
      D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(hr)) std::cerr<<(errors ? static_cast<const char*>(errors->GetBufferPointer()):"DOF compile failed")<<'\n';
    if(code)code->Release();if(errors)errors->Release();if(FAILED(hr))return 1;
  }
  std::cout<<"DOF: all three HLSL entry points compile\n";
}
