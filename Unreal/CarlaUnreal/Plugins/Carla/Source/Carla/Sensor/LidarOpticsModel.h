// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include <algorithm>
#include <cmath>
namespace CarlaLidarOptics
{
// Effective near-IR response presets, not measured calibration data.
struct Material {
  float diffuse=.3f, retro=0, specular=0, transmission=0, ior=1.5f, roughness=.02f, thinSheet=1.f;
  int infrared=-1;
};
inline float fresnel(float cosine, float n1, float n2) {
  const float c=std::clamp(cosine,0.f,1.f), eta=n1/n2;
  const float s2=eta*eta*(1-c*c);
  if (s2>=1) return 1;
  const float t=std::sqrt(1-s2);
  const float rs=(n1*c-n2*t)/std::max(1.e-8f,n1*c+n2*t);
  const float rp=(n2*c-n1*t)/std::max(1.e-8f,n2*c+n1*t);
  return std::clamp((rs*rs+rp*rp)*.5f,0.f,1.f);
}
inline float surface(const Material& m,float cosine,float f) {
  const float c=std::clamp(cosine,0.f,1.f);
  const float width=std::max(.001f,m.roughness);
  const float lobe=std::exp(-2.f*(1.f-c*c)/(width*width));
  return std::clamp(m.diffuse*c+m.retro*std::sqrt(c)+
      (m.transmission>0 ? f : m.specular)*lobe,0.f,1.f);
}
}
