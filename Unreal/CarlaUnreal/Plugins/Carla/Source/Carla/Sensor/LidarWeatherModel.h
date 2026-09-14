// Synthetic homogeneous-medium LiDAR augmentation for teaching (version 1).
// Density controls are relative 0..100, not calibrated physical concentrations.
#pragma once
#include <algorithm>
#include <cmath>
namespace CarlaLidarWeather {
struct Medium {
  float extinction, scatterFraction, jitter;
  bool active() const { return extinction > 0.0f; }
};
inline float density(float x) { return std::isfinite(x) ? std::max(0.0f, std::min(100.0f, x)) / 100.0f : 0.0f; }
inline Medium medium(float rain, float fog, float smoke) {
  rain=density(rain); fog=density(fog); smoke=density(smoke);
  const float a=0.006f*rain, b=0.035f*fog, c=0.060f*smoke, sum=a+b+c;
  return {sum, sum>0 ? (0.30f*a+0.15f*b+0.12f*c)/sum : 0.0f,
          0.015f*rain+0.010f*fog+0.025f*smoke};
}
// Uniform supplies values in [0,1]. Clear consumes no random draws and leaves
// range/intensity bit-for-bit intact. Existing CARLA noise/dropoff follows this.
template <typename Uniform>
bool apply(const Medium& m, float& range, float& intensity, Uniform uniform) {
  if (!m.active()) return true;
  if (!std::isfinite(range) || range<=0 || !std::isfinite(intensity)) return false;
  const float transmission=std::exp(-2.0f*m.extinction*range);
  const float event=uniform();
  if (event > transmission) {
    if (uniform() >= m.scatterFraction || range <= 0.20f) return false;
    // First interaction in a truncated exponential path, before the surface.
    const float u=std::max(0.000001f,std::min(0.999999f,uniform()));
    const float span=range-0.20f;
    range=0.10f-std::log(1.0f-u*(1.0f-std::exp(-2.0f*m.extinction*span)))/(2.0f*m.extinction);
    intensity=0.04f+0.12f*uniform();
    return true;
  }
  intensity*=transmission;
  const float u=std::max(0.000001f,uniform());
  const float normal=std::sqrt(-2.0f*std::log(u))*std::cos(6.28318530718f*uniform());
  range=std::max(0.01f,range+normal*m.jitter*(1.0f+range/50.0f));
  return true;
}
}
