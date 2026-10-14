#pragma once
namespace fable2::native::render::particle_shaders {
inline constexpr const char* kSource=R"hlsl(
cbuffer Draw : register(b0) {
  float4 viewport; float4 texinfo[3]; float4 controls; float4 blend;
  float4 projection[5]; float4 points;
};
struct Vertex { float4 clip; float4 uv; float4 colour; float4 extra; };
StructuredBuffer<Vertex> vertices : register(t3);
StructuredBuffer<float4> weather : register(t5);
Texture2D<float4> images[3] : register(t0);
Texture2D<float> scene_depth : register(t4);
SamplerState linear_clamp : register(s0);
SamplerState linear_wrap : register(s1);
SamplerState point_clamp : register(s2);
SamplerState point_wrap : register(s3);
struct Out {float4 pos:SV_Position;float4 uv:TEXCOORD0;float4 colour:TEXCOORD1;float3 world:TEXCOORD2;};
Out vertex(uint id:SV_VertexID) {
  Vertex v=vertices[id];Out o;
  float2 window=v.clip.xy*viewport.xz+viewport.yw*v.clip.w;
  float2 extent=points.w!=0 ? float2(560,360):float2(1120,720);
  o.pos=float4(window*float2(2.0,-2.0)/extent+float2(-1,1)*v.clip.w,v.clip.zw);
  o.uv=v.uv;o.colour=v.colour;o.world=v.extra.xyz;return o;
}
float4 sample_image(Texture2D<float4> image,float2 uv,float4 info) {
  uv*=info.yz;
  if(info.x==0) return image.Sample(linear_clamp,uv);
  if(info.x==1) return image.Sample(linear_wrap,uv);
  if(info.x==2) return image.Sample(point_clamp,uv);
  return image.Sample(point_wrap,uv);
}
float4 tested(float4 value) {
  uint f=(uint)controls.z;
  bool accepted=f==7 || (f==1 && value.a<controls.w) || (f==2 && value.a==controls.w) ||
    (f==3 && value.a<=controls.w) || (f==4 && value.a>controls.w) ||
    (f==5 && value.a!=controls.w) || (f==6 && value.a>=controls.w);
  if(!accepted) discard;
  return value;
}
float4 pixel(Out i):SV_Target {
  float4 sampled=sample_image(images[0],i.uv.xy,texinfo[0]);
  if(controls.y>=2) sampled=lerp(sampled,sample_image(images[1],i.uv.xy,texinfo[1]),saturate((i.uv.z-blend.x)*blend.z));
  if(controls.y>=3) sampled=lerp(sampled,sample_image(images[2],i.uv.xy,texinfo[2]),saturate((i.uv.z-blend.y)*blend.w));
  return tested(float4(sampled.rgb*sampled.rgb*i.colour.rgb*i.colour.a*controls.x,sampled.a*i.colour.a));
}
float4 soft(Out i):SV_Target {
  float4 sampled=sample_image(images[0],i.uv.xy,texinfo[0]);
  // Guest depth is 1-clipZ under its reversed viewport; native D32 stores clipZ.
  float2 uv=abs(floor(i.pos.xy))*projection[0].zw+projection[0].xy;
  float z=scene_depth.Sample(point_clamp,uv+projection[0].zw*.5);
  float4 clip=float4(2*uv.x-1,1-2*uv.y,z,1);
  float4 p=float4(dot(projection[1],clip),dot(projection[2],clip),dot(projection[3],clip),dot(projection[4],clip));
  float3 delta=i.world-p.xyz/p.w;
  float distance2=dot(delta,delta)*blend.x;
  float t=saturate(2*(distance2>.5 ? 1-distance2:distance2));
  float fade=.5*t*t*t*t;
  if(distance2>.5) fade=1-fade;
  if(sampled.a*i.colour.a<=.01) fade=0;
  return tested(float4(sampled.rgb*sampled.rgb*i.colour.rgb*i.colour.a*controls.x,sampled.a*i.colour.a)*fade);
}
struct Full {float4 pos:SV_Position;};
Full full_vertex(uint id:SV_VertexID) {
  float2 uv=float2((id<<1)&2,id&2);Full o;o.pos=float4(uv*float2(2,-2)+float2(-1,1),0,1);return o;
}
float copy_depth(Full i):SV_Target {return scene_depth.Load(int3((int2)i.pos.xy,0));}
// Replay the producer's own quad UV and integer-centred viewport. Deriving
// UV from host pixel centres shifts the point sample by one full-size pixel.
// Native D32 already stores the reversed clip Z.
float half_depth(Out i):SV_Depth {
  return scene_depth.SampleLevel(point_clamp,i.uv.xy,0);
}
float4 SnowC(float reg) { return weather[(uint)reg]; }
// SNOW FORMULA BEGIN
float3 SnowUnit(float3 value) {
  float length2 = dot(value,value);
  return (length2>0) ? value/sqrt(length2) : float3(0,0,0);
}
float3 SnowCell(float index) {
  precise float n = index+.5;
  precise float z = floor(n/(SnowC(46).x*SnowC(46).y));
  precise float rem = n-z*SnowC(46).x*SnowC(46).y;
  precise float y = floor(rem/SnowC(46).x);
  precise float x = floor(rem-y*SnowC(46).x);
  return float3(x,y,z)*SnowC(46).w+SnowC(8).xyz;
}
float SnowHash(float index) {
  precise float3 cell = SnowCell(index);
  precise float3 square = cell.zxy*cell.zxy;
  return frac(dot(square,SnowC(250).xyz));
}
float3 SnowRandom(float index) { return frac(SnowHash(index)*SnowC(255).yzw); }
float SnowPhase(float index) { return frac(SnowC(47).x*SnowC(250).w+SnowHash(index))*SnowC(244).y; }
float3 SnowCapsule(float3 p,float3 h,float phase,float4 a,float4 b,float inverse_radius) {
  float3 axis = b.xyz-a.xyz;
  float length2 = dot(axis,axis);
  float projection = (length2>0) ? saturate(dot(p-a.xyz,axis)/length2) : 0;
  float3 offset = a.xyz+axis*projection-p;
  float weight = saturate(1-sqrt(dot(offset,offset))*inverse_radius);
  float age = SnowC(47).x-(a.w+(b.w-a.w)*projection);
  float3 result = float3(0,0,0);
  [branch] if (weight>0 && age>0 && age<phase) {
    float3 random_axis = 2*h-1;
    float3 tangent = SnowUnit(cross(axis,random_axis));
    float3 radial = SnowUnit(cross(tangent,random_axis));
    float advance = age*(h.x+.5);
    float angle = frac((advance+h.y*SnowC(253).w+h.x)*SnowC(245).w+.5)*SnowC(244).z+SnowC(244).x;
    float fade = saturate(1-(advance-SnowC(248).y)*(advance-SnowC(248).y));
    float advance2 = advance*advance;
    float3 inward = offset*advance2/(advance2+SnowC(247).z);
    float3 axial = SnowUnit(axis)*SnowC(247).w*advance2/(advance2+.5);
    float3 orbit = (tangent*sin(angle)-radial*cos(angle))*SnowC(247).z*fade*fade*fade*fade;
    result = (inward+axial+orbit)*weight;
  }
  return result;
}
float3 SnowPlanes(float3 world,float first) {
  float4 p = float4(world,1);
  float sum = saturate(dot(SnowC(first),p))+saturate(dot(SnowC(first+1),p))+saturate(dot(SnowC(first+2),p))+saturate(dot(SnowC(first+3),p))+saturate(dot(SnowC(first+4),p));
  return (sum==0) ? SnowC(72).xyz : world;
}
float3 SnowWorld(float index) {
  float3 h = SnowRandom(index);
  float phase = SnowPhase(index);
  float3 p = SnowCell(index)+h-.5;
  float size = frac(dot(h.zxy,SnowC(253).xyz));
  float3 flutter = 2*frac((phase+h.yxz)*SnowC(254).xyz)-1;
  float3 cubic = flutter*flutter*flutter-flutter;
  float random_size = 1.5-size;
  float sx = SnowC(245).z*SnowC(75).w*cubic.x*random_size;
  float sy = SnowC(248).y*cubic.z*SnowC(75).w*random_size;
  float sz = SnowC(248).x*(1-(cubic.y+SnowC(248).z)*(cubic.y+SnowC(248).z))*SnowC(75).w*random_size;
  float3 world = p+float3(sx+SnowC(144).x*(phase-5),sy+SnowC(144).y*(phase-5),sz-SnowC(144).z*(phase-5)*SnowC(75).w);
  world = world+SnowCapsule(p,h,phase,SnowC(128),SnowC(129),SnowC(73).x);
  world = world+SnowCapsule(p,h,phase,SnowC(130),SnowC(131),SnowC(73).y);
  world = world+SnowCapsule(p,h,phase,SnowC(132),SnowC(133),SnowC(73).z);
  world = world+SnowCapsule(p,h,phase,SnowC(134),SnowC(135),SnowC(73).w);
  world = world+SnowCapsule(p,h,phase,SnowC(136),SnowC(137),SnowC(74).x);
  world = world+SnowCapsule(p,h,phase,SnowC(138),SnowC(139),SnowC(74).y);
  world = world+SnowCapsule(p,h,phase,SnowC(140),SnowC(141),SnowC(74).z);
  world = world+SnowCapsule(p,h,phase,SnowC(142),SnowC(143),SnowC(74).w);
  return SnowPlanes(SnowPlanes(SnowPlanes(world,113),118),123);
}
float4 SnowClip(float3 world) {
  float4 p = float4(world,1);
  return float4(dot(SnowC(0),p),dot(SnowC(1),p),dot(SnowC(2),p),dot(SnowC(3),p));
}
float SnowDiameter(float index,float w) {
  return SnowC(75).z*frac(dot(SnowRandom(index).zxy,SnowC(253).xyz))/w;
}
float SnowFade(float index,float3 world) {
  float phase = SnowPhase(index);
  float alpha = (phase>8) ? 1-(phase-8)*.5 : 1;
  alpha = (phase<2) ? phase*.5 : alpha;
  float3 delta = world-SnowC(72).xyz;
  return saturate(1-(sqrt(dot(delta,delta))-SnowC(75).x)/(SnowC(75).y-SnowC(75).x))*alpha;
}
float3 SnowAtlas(float index) {
  float3 h = SnowRandom(index).zxy;
  return frac(float3(dot(h,SnowC(251).xyz),dot(h,SnowC(252).xyz),dot(h,SnowC(249).xyz)));
}
float2 SnowAtlasOrigin(float3 atlas) { return floor(2*atlas.xy)*.5+.25; }
float2 SnowAtlasX(float3 atlas) {
  float angle = (atlas.y*SnowC(246).w+.5)*SnowC(244).z+SnowC(244).x;
  return float2(sin(angle),cos(angle))*.25;
}
float2 SnowAtlasY(float3 atlas) {
  float2 basis = SnowAtlasX(atlas);
  return (atlas.z>.5) ? float2(-basis.y,basis.x) : float2(basis.y,-basis.x);
}
float2 SnowUv(float2 coord,float2 origin,float2 basis_x,float2 basis_y) {
  float2 corner = abs(coord)*2-1;
  return origin+corner.x*basis_x+corner.y*basis_y;
}
float4 SnowColour(float4 sampled,float fade,float exposure,float3 tint) {
  return float4(sampled.xyz*exposure*tint*fade,1);
}
// SNOW FORMULA END
struct SnowOut {float4 pos:SV_Position;float2 coord:TEXCOORD0;float2 origin:TEXCOORD1;float2 basis_x:TEXCOORD2;float2 basis_y:TEXCOORD3;float fade:TEXCOORD4;};
SnowOut snow_vertex(uint id:SV_VertexID) {
  uint corner=id%6;
  float2 uv=corner==0 || corner==3 ? float2(0,0):corner==1 ? float2(1,0):corner==5 ? float2(0,1):float2(1,1);
  float index=points.x+float(id/6);
  float3 world=SnowWorld(index);float4 clip=SnowClip(world);
  // SDK PA_SU_POINT_MINMAX specifies half sizes in 12.4 units. The exported
  // diameter is clamped in screen pixels before expansion, preserving Z/W.
  float diameter=clamp(SnowDiameter(index,clip.w),points.y,points.z);
  float2 window=clip.xy*viewport.xz+viewport.yw*clip.w+(uv*2-1)*diameter*.5*clip.w;
  SnowOut o;o.pos=float4(window*float2(2.0/1120.0,-2.0/720.0)+float2(-1,1)*clip.w,clip.zw);
  float3 atlas=SnowAtlas(index);o.coord=uv;o.origin=SnowAtlasOrigin(atlas);
  o.basis_x=SnowAtlasX(atlas);o.basis_y=SnowAtlasY(atlas);o.fade=SnowFade(index,world);return o;
}
float4 snow_pixel(SnowOut i):SV_Target {
  float4 sampled=sample_image(images[0],SnowUv(i.coord,i.origin,i.basis_x,i.basis_y),texinfo[0]);
  return tested(SnowColour(sampled,i.fade,controls.x,blend.xyz));
}
)hlsl";
}
