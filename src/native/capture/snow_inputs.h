#pragma once
#include "presentation_frame.h"
namespace fable2::native::capture {
inline constexpr float kSnowLiterals[12][4]={
  {-3.14159274f,10,6.28318548f,8},{2,1,.800000012f,1.5915494f},
  {-5,-8,-.5f,.499999583f},{.5f,-1,.300000012f,5},{.600000024f,.899999976f,.400000006f,0},
  {87.4400024f,71.6500015f,76.8499985f,-1},{1.41421354f,3.14159274f,2.71828175f,.100000001f},
  {17.0100002f,13.5100002f,56.0999985f,5},{87.2300034f,51.9399986f,46.8699989f,.25f},
  {7.69999981f,5.5f,6.5999999f,6.28318024f},{.130999997f,.310000002f,.151999995f,1.5f},
  {.5f,732.050781f,236.067978f,645.751282f}};
inline bool SnowRegisterUsed(uint32_t reg) {
  return reg<=3 || reg==8 || reg==46 || reg==47 || (reg>=72 && reg<=75) ||
    (reg>=113 && reg<=144) || reg>=244;
}
inline bool SnowInputsValid(const WeatherConstants& bank,uint32_t minmax) {
  const auto& c=bank.c;
  for(uint32_t reg=0;reg<256;reg++) if(SnowRegisterUsed(reg))
    for(float value:c[reg]) if(!UiFinite(value)) return false;
  if(c[46][0]<=0 || c[46][1]<=0 || c[46][0]>65536 || c[46][1]>65536 ||
     c[75][1]<=c[75][0] || c[75][2]<0 || (minmax&65535)>(minmax>>16)) return false;
  for(uint32_t reg:{73u,74u}) for(float value:c[reg]) if(value<0) return false;
  return true;
}
}
