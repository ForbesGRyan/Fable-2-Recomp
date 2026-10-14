#include "../../src/native/render/presentation_shaders.h"
#include "../../src/native/fable2_native_shaders.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
int main(int argc,char** argv) {
  assert(argc==2 || (argc==3 && std::string(argv[2])=="dof"));
  const bool dof=argc==3;
  std::ifstream input(argv[1],std::ios::binary);assert(input);
  uint32_t count=0;input.read(reinterpret_cast<char*>(&count),4);
  assert(dof ? count>68605 && count<500000:count==68605);
  std::vector<float> values(count),expected(count);
  for(uint32_t i=0;i<count;i++) {
    input.read(reinterpret_cast<char*>(&values[i]),4);
    input.read(reinterpret_cast<char*>(&expected[i]),4);
  }
  assert(input);
  std::string text=dof ? fable2::native::shaders::kClayPostDofDownPs:fable2::native::render::ui_shaders::kSource;
  auto first=text.find(dof ? "// DOF HDR RESOLVE BEGIN\n":"// MENU SOURCE RESOLVE BEGIN\n");
  auto last=text.find(dof ? "// DOF HDR RESOLVE END\n":"// MENU SOURCE RESOLVE END\n");
  assert(first!=std::string::npos && last>first);
  text=text.substr(first,last-first)+(dof ? "#define CheckedResolve DofResolve\n":"#define CheckedResolve MenuSourceResolve\n")+R"hlsl(
StructuredBuffer<float> source : register(t0);
RWStructuredBuffer<float> result : register(u0);
[numthreads(64,1,1)] void main(uint id:SV_DispatchThreadID) {
  uint count,stride;source.GetDimensions(count,stride);
  if(id<count) result[id]=CheckedResolve(source[id]);
})hlsl";
  ComPtr<ID3DBlob> code,errors;
  auto hr=D3DCompile(text.data(),text.size(),"menu-resolve",nullptr,nullptr,"main","cs_5_0",
    D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
  if(FAILED(hr)) {std::cerr<<(errors ? static_cast<const char*>(errors->GetBufferPointer()):"compile failed");return 1;}
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
  assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
    D3D11_SDK_VERSION,&device,nullptr,&context)));
  ComPtr<ID3D11ComputeShader> shader;
  assert(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)));
  D3D11_BUFFER_DESC desc{};desc.ByteWidth=count*4;desc.Usage=D3D11_USAGE_DEFAULT;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=4;
  D3D11_SUBRESOURCE_DATA data{};data.pSysMem=values.data();
  ComPtr<ID3D11Buffer> source,result,readback;
  assert(SUCCEEDED(device->CreateBuffer(&desc,&data,&source)));
  desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&result)));
  desc.BindFlags=0;desc.MiscFlags=0;desc.StructureByteStride=0;
  desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&readback)));
  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};srv_desc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;srv_desc.Buffer.NumElements=count;
  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};uav_desc.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uav_desc.Buffer.NumElements=count;
  ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;
  assert(SUCCEEDED(device->CreateShaderResourceView(source.Get(),&srv_desc,&srv)));
  assert(SUCCEEDED(device->CreateUnorderedAccessView(result.Get(),&uav_desc,&uav)));
  auto* source_view=srv.Get();auto* result_view=uav.Get();
  context->CSSetShader(shader.Get(),nullptr,0);context->CSSetShaderResources(0,1,&source_view);
  context->CSSetUnorderedAccessViews(0,1,&result_view,nullptr);context->Dispatch((count+63)/64,1,1);
  result_view=nullptr;context->CSSetUnorderedAccessViews(0,1,&result_view,nullptr);
  context->CopyResource(readback.Get(),result.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));
  auto* actual=static_cast<const float*>(mapped.pData);
  for(uint32_t i=0;i<count;i++) if(actual[i]!=expected[i]) {
    std::cerr<<"Resolve case "<<i<<": input "<<values[i]<<", expected "<<expected[i]<<", actual "<<actual[i]<<'\n';
    context->Unmap(readback.Get(),0);return 1;
  }
  context->Unmap(readback.Get(),0);
  std::cout<<"Presentation: actual WARP resolve shader matches "<<count<<" independent cases\n";
}
