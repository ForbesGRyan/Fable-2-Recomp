#pragma once

namespace fable2::native::render::ui_shaders {
// c0,c30,c31,c46,c47,c77,c253,c254,c255, followed by viewport,
// alpha test, and each texture's sampler selector / BC extent correction.
inline constexpr const char* kSource = R"hlsl(
cbuffer Draw : register(b0) {
  float4 c[9]; float4 viewport; float4 alpha;
  float4 texinfo[3]; float4 finish;
  float4 ramp[512];
};
struct Vertex { float4 clip; float4 uv; float4 colour; float4 extra; };
StructuredBuffer<Vertex> vertices : register(t3);
Texture2D<float4> textures[3] : register(t0);
SamplerState linear_clamp : register(s0);
SamplerState linear_wrap : register(s1);
SamplerState point_clamp : register(s2);
SamplerState point_wrap : register(s3);
struct Out { float4 pos : SV_Position; float4 uv : TEXCOORD0;
             float4 colour : TEXCOORD1; float4 extra : TEXCOORD2; };
Out vertex(uint id : SV_VertexID) {
  Vertex v = vertices[id]; Out o;
  float2 window = v.clip.xy * viewport.xz + viewport.yw * v.clip.w;
  o.pos = float4(window * float2(2.0/1280.0,-2.0/720.0) +
                 float2(-1,1)*v.clip.w, v.clip.z, v.clip.w);
  o.uv=v.uv; o.colour=v.colour; o.extra=v.extra; return o;
}
Out mask_vertex(uint id : SV_VertexID) {
  Vertex v=vertices[id];Out o;
  float2 window=v.clip.xy*viewport.xz+viewport.yw*v.clip.w;
  o.pos=float4(window*float2(2.0/640.0,-2.0/360.0)+float2(-1,1)*v.clip.w,v.clip.z,v.clip.w);
  o.uv=v.uv;o.colour=v.colour;o.extra=v.extra;return o;
}
float4 sample_texture(Texture2D<float4> image, float2 uv, float selector) {
  if (selector == 0) return image.Sample(linear_clamp,uv);
  if (selector == 1) return image.Sample(linear_wrap,uv);
  if (selector == 2) return image.Sample(point_clamp,uv);
  return image.Sample(point_wrap,uv);
}
float4 sample_at(uint slot, float2 uv) {
  uv *= texinfo[slot].yz;
  if(slot==0) return sample_texture(textures[0],uv,texinfo[0].x);
  if(slot==1) return sample_texture(textures[1],uv,texinfo[1].x);
  return sample_texture(textures[2],uv,texinfo[2].x);
}
// MASK COPY FORMULA BEGIN
float4 MaskCopy(float4 sampled) {return sampled;}
// MASK COPY FORMULA END
float4 mask(Out i):SV_Target {return MaskCopy(sample_at(0,i.uv.xy));}
// The game's clear of a mask rectangle: the colour (c[0]) under the captured
// blend, scissored to the rectangle. The resolve copies the shared scratch
// target texel for texel into the mask's own texture.
float4 mask_clear(float4 pos : SV_Position):SV_Target {return c[0];}
float4 mask_resolve(float4 pos : SV_Position):SV_Target {return textures[0].Load(int3(pos.xy,0));}
float4 tested(float4 value) {
  uint f = (uint)alpha.x;
  bool accepted = f==7 || (f==1 && value.a<alpha.y) || (f==2 && value.a==alpha.y) ||
    (f==3 && value.a<=alpha.y) || (f==4 && value.a>alpha.y) ||
    (f==5 && value.a!=alpha.y) || (f==6 && value.a>=alpha.y);
  if (!accepted) discard;
  return value;
}
float4 sprite(Out i) : SV_Target {
  float4 first=sample_at(0,i.uv.xy);
  float3 rgb=pow(abs(first.rgb),c[1].xxx); float a=first.a*i.extra.w;
  if(c[2].x==c[8].x) {
    float4 second=sample_at(1,i.uv.zw);
    rgb=lerp(rgb,pow(abs(second.rgb),c[1].xxx),c[4].x); a*=second.a;
  }
  if(c[3].x==c[8].x) rgb-=(c[8].xxx-i.colour.rgb)*i.colour.a;
  if(c[5].x==c[8].x) a*=sample_at(2,abs(floor(i.pos.xy))*c[0].zw+c[0].xy).a;
  return tested(float4(rgb,a));
}
float4 text(Out i) : SV_Target {
  float4 first=sample_at(0,i.uv.xy)*c[2];
  float3 rgb=pow(abs(first.rgb),c[1].xxx); float a=first.a;
  if(a<c[8].x) {
    float m=c[8].y;
    static const float2 offsets[8]={float2(0,-1),float2(-1,-1),float2(-1,0),float2(-1,1),
                                  float2(0,1),float2(1,1),float2(1,0),float2(1,-1)};
    [unroll] for(uint n=0;n<8;n++) m=max(m,sample_at(0,i.uv.xy+offsets[n]*c[3].xy).a);
    float outline=m*c[4].w; a=a*(c[8].x-outline)+outline;
  }
  if(c[5].x==c[8].x) a*=sample_at(1,abs(floor(i.pos.xy))*c[0].zw+c[0].xy).a;
  return tested(float4(rgb,a));
}
float4 video(Out i) : SV_Target {
  float y=sample_at(0,i.uv.xy).r, cr=sample_at(1,i.uv.xy).r, cb=sample_at(2,i.uv.xy).r;
  float3 rgb=float3(y*c[6].z+cr*c[6].w+c[8].x,
    cb*c[6].x+y*c[7].z+cr*c[7].w+c[7].x,
    cb*c[6].y+y*c[7].z+c[7].y);
  return tested(float4(rgb*c[2].rgb,c[2].w));
}
float4 particle(Out i):SV_Target {
  float4 sampled=lerp(sample_at(0,i.uv.xy),sample_at(1,i.uv.xy),saturate((i.uv.z-c[2].x)*c[3].x));
  return tested(float4(sampled.rgb*sampled.rgb*i.colour.rgb*i.colour.a*c[0].w,sampled.a*i.colour.a));
}
// Only captured black fades are supported: UiBlackFade rejects coloured uses
// of this program. Xenos zero-product semantics keep black RGB zero even when
// the reciprocal alpha is infinite; retain that at the transparent endpoint.
// BLACK FADE FORMULA BEGIN
float4 BlackFade(float sampled_alpha,float opacity) {
  return float4(0,0,0,sampled_alpha*opacity);
}
// BLACK FADE FORMULA END
float4 fade(Out i):SV_Target { return tested(BlackFade(sample_at(0,i.uv.xy).a,c[2].w)); }
// Diagnostic coverage: each UI draw's alpha, accumulated over (src One, dst
// InvSrcAlpha) into an R8 plane, so a readback can be judged where the UI is
// opaque regardless of the world behind it.
float4 sprite_coverage(Out i):SV_Target {return sprite(i).aaaa;}
float4 text_coverage(Out i):SV_Target {return text(i).aaaa;}
struct FullOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
FullOut full_vertex(uint id : SV_VertexID) {
  FullOut o; o.uv=float2((id<<1)&2,id&2);
  o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float4 copy(FullOut i) : SV_Target {
  float4 sampled=textures[0].Sample(linear_clamp,i.uv);
  if(finish.w>0) {
    float3 v=saturate(sampled.rgb)*255.0;
    uint3 lo=(uint3)floor(v), hi=min(lo+1,255);
    float3 a=float3(ramp[256+lo.x].x,ramp[256+lo.y].y,ramp[256+lo.z].z);
    float3 b=float3(ramp[256+hi.x].x,ramp[256+hi.y].y,ramp[256+hi.z].z);
    sampled.rgb=lerp(a,b,frac(v));
  }
  return sampled;
}
// MENU HISTORY FORMULA BEGIN
float3 MenuMap(float3 rgb,float power) {
  return pow(abs(rgb),power);
}
float3 QuantizeHistory(float3 rgb) {
  return floor(saturate(rgb)*1023+.5)/1023;
}
float3 ResolveHistory(float3 rgb) {
  return floor(saturate(rgb)*255+.5)/255;
}
float3 HistoryRgb(float3 current,float3 previous,float share) {
  float3 value = QuantizeHistory(current);
  // 686DE15F reads previous history from tf13 format 6 (RGBA8), even though
  // the destination is R10. Reproduce that resolve before the history blend.
  previous = ResolveHistory(previous);
  [branch] if (share>0) value = lerp(value,previous,share);
  return QuantizeHistory(value);
}
// MENU HISTORY FORMULA END
// MENU SOURCE RESOLVE BEGIN
float MenuSourceResolve(float value) {
  // Format 12's R16 host target is packed to unsigned 7e3 by the SDK's
  // EDRAM dump before resolving to the F16 texture sampled by 93E7.
  // Clamp after UI blending, and round ties to even exactly as the packer.
  uint bits=asuint(clamp(value,0.0,31.875));
  uint biased;
  if(bits<0x3E800000u) biased=((bits&0x7FFFFFu)|0x800000u)>>min(125u-(bits>>23),24u);
  else biased=bits+0xC2000000u;
  uint packed=((biased+0x7FFFu+((biased>>16)&1u))>>16)&0x3FFu;
  uint exponent=packed>>7,mantissa=packed&127u;
  if(exponent==0) return float(mantissa)/512.0;
  return exp2(float(exponent)-3.0)*(1.0+float(mantissa)/128.0);
}
// MENU SOURCE RESOLVE END
float4 history(FullOut i) : SV_Target {
  float4 v=textures[0].Sample(point_clamp,i.uv);
  if(finish.z>1) v.rgb=float3(MenuSourceResolve(v.r),MenuSourceResolve(v.g),MenuSourceResolve(v.b));
  v.rgb=MenuMap(v.rgb,finish.x);
  // 93E7's final exp writes blue to both B and A.
  if(finish.z>0) v.a=v.b;
  float4 previous=textures[1].Sample(point_clamp,i.uv);
  v.rgb=HistoryRgb(v.rgb,previous.rgb,finish.y);
  v.a=floor(saturate(v.a)*3+.5)/3;
  if(finish.y>0) v.a=floor(saturate(lerp(v.a,previous.a,finish.y))*3+.5)/3;
  return v;
}
// SCANOUT INDEX FORMULA BEGIN
float RampIndex(float value) {
  return min(floor(saturate(value)*255+.5),255);
}
// SCANOUT INDEX FORMULA END
float4 display(FullOut i) : SV_Target {
  // SDK apply_gamma_table_cs loads the nearest table entry, with no ramp
  // interpolation. Interpolation lifted nominal black menu pixels to RGB 3.
  float3 v=textures[0].Sample(point_clamp,i.uv).rgb;
  uint3 index=uint3(RampIndex(v.r),RampIndex(v.g),RampIndex(v.b));
  return float4(ramp[index.x].x,ramp[index.y].y,ramp[index.z].z,1);
}
)hlsl";
}  // namespace fable2::native::render::ui_shaders
