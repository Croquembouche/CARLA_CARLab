// Shared scene-weather mapping for ordinary LiDAR. MIT license.
// CARLA percentages are artistic controls, not measured concentrations.
#pragma once
#include <algorithm>
#include <cmath>
namespace CarlaLidarSceneWeather {
inline double percentage(double value) {
  return std::isfinite(value) ? std::clamp(value, 0.0, 100.0) : 0.0;
}
inline double rain_mm_h(double precipitation) { return percentage(precipitation)*.5; }
inline double rain_extinction(double rain_mm_h) {
  return rain_mm_h>0 ? .01*std::pow(rain_mm_h,.6) : 0;
}
inline double fog_extinction(double density) {
  // Smooth generic mapping: 0 -> clear, 50 -> 0.0613/m, 100 -> 2/m.
  // Detection still follows two-way transmission and receiver thresholds;
  // there is no artificial point-cloud cutoff at a particular density.
  return .002*std::expm1(std::log(1001.0)*percentage(density)*.01);
}
// UE divides component density by 1000 per centimetre and uses exp2.
// 100 cm per metre gives sigma = component_density * ln(2) / 10.
inline double unreal_fog_density(double density) {
  return fog_extinction(density)*10/std::log(2.0);
}
inline double dust_extinction(double density) { return percentage(density)*.0003; }
}
