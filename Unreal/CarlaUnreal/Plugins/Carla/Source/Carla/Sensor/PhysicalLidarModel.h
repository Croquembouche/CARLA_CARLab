// Copyright (c) 2026. Licensed under the MIT license.
#pragma once

// Engine-independent pulsed time-of-flight model. SI units throughout.
// The generic defaults are explicitly uncalibrated; they are not a product specification.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace CarlaPhysicalLidar {
constexpr double LightSpeed = 299792458.0;
constexpr double Planck = 6.62607015e-34;
constexpr double Pi = 3.14159265358979323846;
enum Flag : uint32_t {
  Surface = 1, Atmosphere = 2, Cover = 4, Multipath = 8,
  Saturated = 16, Mixed = 32, FalseAlarm = 64, ClippedCandidates = 128
};

// Counter-derived streams make results independent of worker scheduling and dropped packets.
struct Random {
  uint64_t state;
  explicit Random(uint64_t seed) : state(seed) {}
  uint64_t next() {
    uint64_t z = (state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
  }
  double uniform() { return (double(next() >> 11) + .5) * (1.0 / 9007199254740992.0); }
  double normal() { return std::sqrt(-2.0 * std::log(uniform())) * std::cos(2 * Pi * uniform()); }
  double poisson(double mean) {
    if (!(mean > 0)) return 0;
    if (mean > 40) return std::max(0.0, std::floor(mean + std::sqrt(mean) * normal() + .5));
    const double limit = std::exp(-mean); double product = 1; int count = 0;
    do { ++count; product *= uniform(); } while (product > limit && count < 1024);
    return count - 1;
  }
};

struct Receiver {
  double wavelength_nm = 905;
  double pulse_energy_nj = 40;
  double aperture_diameter_m = .015;
  double efficiency = .10;             // optics throughput times photon detection efficiency
  double pulse_fwhm_ns = 3;
  double overlap_distance_m = .5;      // smooth near-field transmitter/receiver overlap
  double minimum_range_m = .15;
  double maximum_range_m = 100;
  double background_photons = 1;
  double electronic_noise_photons = .5;
  double detection_sigma = 5;
  double minimum_signal_photons = 5;
  double saturation_photons = 65535;
  double range_noise_floor_m = .005;
  double range_bias_m = 0;
  double time_walk_m = 0;
  double saturation_bias_m = 0;
  double range_quantization_m = .002;
  double echo_separation_m = .30;
  double temperature_c = 25;
  double reference_temperature_c = 25;
  double temperature_bias_m_per_c = 0;
  double packet_loss_probability = 0;
  double latency_ms = 0;
  double latency_jitter_ms = 0;
  int max_returns = 2;
  // 0 strongest, 1 nearest, 2 farthest. Applied after detection, not before it.
  int return_order = 0;
  bool shot_noise = true;
  bool false_alarms = true;
  double gain() const {
    const double photons = pulse_energy_nj * 1.e-9 * wavelength_nm * 1.e-9 / (Planck * LightSpeed);
    return photons * aperture_diameter_m * aperture_diameter_m * .25 * efficiency;
  }
  double signal(double range, double response) const {
    return gain() * std::max(0.0, response) / std::max(1.e-8, range * range + overlap_distance_m * overlap_distance_m);
  }
};

struct Echo {
  double range_m = 0;
  double response = 0;                 // integrated returned fraction, includes footprint and optical path losses
  uint32_t flags = Surface;
  uint32_t object_id = 0;
};
struct Detection {
  double range_m = 0, signal = 0, reflectivity = 0, ambient = 0;
  double pulse_width_ns = 0, confidence = 0;
  uint32_t flags = 0, object_id = 0;
};

struct MediumSegment {
  double begin_m = 0, end_m = 0;
  double extinction_m_inv = 0, backscatter_m_inv = 0;
};
inline double optical_depth(const std::vector<MediumSegment>& medium, double distance) {
  double depth = 0;
  for (const auto& m : medium)
    depth += std::max(0.0, std::min(distance, m.end_m) - m.begin_m) * m.extinction_m_inv;
  return depth;
}
// Operates on every emitted sub-beam, including those with no surface intersection.
inline void propagate_medium(std::vector<Echo>& echoes, const std::vector<MediumSegment>& medium,
                             double unoccluded_range, double footprint_weight, Random& random) {
  for (auto& e : echoes) e.response *= std::exp(-2 * optical_depth(medium, e.range_m));
  for (const auto& m : medium) {
    const double end = std::min(unoccluded_range, m.end_m), length = end - m.begin_m;
    if (length <= 0 || m.backscatter_m_inv <= 0) continue;
    // Bounded Monte Carlo quadrature: sample one return per occupied range cell.
    // The finite cells and sample cap are numerical approximations, not individual photons.
    const int cells = std::max(1, std::min(64, int(std::ceil(length / 2.0))));
    const double cell = length / cells;
    for (int i = 0; i < cells; ++i) {
      const double probability = -std::expm1(-m.backscatter_m_inv * cell);
      if (random.uniform() >= probability) continue;
      const double distance = m.begin_m + (i + random.uniform()) * cell;
      const double response = footprint_weight * .02 * std::exp(-2 * optical_depth(medium, distance));
      echoes.push_back({distance, response, Atmosphere, 0});
    }
  }
}

// Gaussian beam quadrature. Each sample has an aperture offset and angular
// offset; all weights sum to one, including when a sample misses the scene.
struct BeamSample { double x, y, weight; };
inline std::vector<BeamSample> beam_samples(int count) {
  count = std::max(1, std::min(33, count));
  std::vector<BeamSample> samples;
  if (count == 1) return {{0, 0, 1}};
  for (int i = 0; i < count; ++i) {
    const double q = (i + .5) / count;
    // Equal-probability annuli of a Gaussian truncated at three sigma.
    const double radius = std::sqrt(-2 * std::log(1 - q * (1 - std::exp(-4.5))));
    const double angle = i * 2.39996322972865332;
    samples.push_back({radius * std::cos(angle), radius * std::sin(angle), 1.0 / count});
  }
  double mean_x=0, mean_y=0;
  for (const auto& s : samples) { mean_x+=s.x*s.weight; mean_y+=s.y*s.weight; }
  for (auto& s : samples) { s.x-=mean_x; s.y-=mean_y; }
  return samples;
}

inline std::vector<Detection> detect(std::vector<Echo> echoes, const Receiver& p, Random& random) {
  echoes.erase(std::remove_if(echoes.begin(), echoes.end(), [&](const Echo& e) {
    return !std::isfinite(e.range_m) || !std::isfinite(e.response) || e.response <= 0 ||
        e.range_m < p.minimum_range_m || e.range_m > p.maximum_range_m;
  }), echoes.end());
  std::sort(echoes.begin(), echoes.end(), [](const Echo& a, const Echo& b) { return a.range_m < b.range_m; });
  const double pulse_sigma_m = p.pulse_fwhm_ns * 1.e-9 * LightSpeed / (2 * 2.354820045);
  // Echoes closer than the receiver's resolving distance contribute to a
  // single fitted pulse. Keep the anchor fixed to avoid transitive merging.
  const double separation = std::max(p.echo_separation_m, 2 * pulse_sigma_m);
  const double background_sigma = std::sqrt(std::max(0.0, p.background_photons) +
      p.electronic_noise_photons * p.electronic_noise_photons);
  const double threshold = p.minimum_signal_photons + p.detection_sigma * background_sigma;
  std::vector<Detection> output;
  for (size_t first = 0; first < echoes.size();) {
    size_t last = first; double energy = 0, weighted = 0, weighted2 = 0, largest = -1;
    uint32_t flags = 0, object = 0; bool mixed_object = false;
    while (last < echoes.size() && echoes[last].range_m - echoes[first].range_m < separation) {
      const Echo& e = echoes[last++]; const double photons = p.signal(e.range_m, e.response);
      energy += photons; weighted += photons * e.range_m; weighted2 += photons * e.range_m * e.range_m;
      flags |= e.flags;
      if (e.object_id != echoes[first].object_id) mixed_object = true;
      if (photons > largest) { largest = photons; object = e.object_id; }
    }
    first = last;
    if (!(energy > 0)) continue;
    double observed = energy;
    if (p.shot_noise) observed = random.poisson(energy + p.background_photons) - p.background_photons +
        p.electronic_noise_photons * random.normal();
    if (observed < threshold) continue;
    double range = weighted / energy;
    const double variance = std::max(0.0, weighted2 / energy - range * range);
    if (mixed_object || variance > .0001) flags |= Mixed;
    if (observed >= p.saturation_photons) {
      flags |= Saturated; range += p.saturation_bias_m;
      observed = p.saturation_photons;
    }
    range += p.range_bias_m + p.time_walk_m / std::sqrt(std::max(1.0, observed)) +
        (p.temperature_c - p.reference_temperature_c) * p.temperature_bias_m_per_c;
    if (p.shot_noise) range += std::sqrt(p.range_noise_floor_m * p.range_noise_floor_m +
        (pulse_sigma_m * pulse_sigma_m + variance) / std::max(1.0, observed)) * random.normal();
    if (p.range_quantization_m > 0) range = std::round(range / p.range_quantization_m) * p.range_quantization_m;
    if (range < p.minimum_range_m || range > p.maximum_range_m) continue;
    const double reflectivity = std::clamp(observed / std::max(1.e-20, p.signal(range, 1)), 0.0, 1.0);
    output.push_back({range, observed, reflectivity, p.background_photons,
        std::sqrt(pulse_sigma_m * pulse_sigma_m + variance) * 2.354820045 * 2 / LightSpeed * 1.e9,
        std::clamp((observed - threshold) / std::max(1.0, observed), 0.0, 1.0), flags, object});
  }
  if (p.false_alarms && p.shot_noise && p.background_photons > 0) {
    const double bins = (p.maximum_range_m - p.minimum_range_m) / std::max(.01, separation);
    const double z = threshold / std::max(1.e-9, background_sigma);
    const double probability = std::min(.25, bins * .5 * std::erfc(z / std::sqrt(2.0)));
    if (random.uniform() < probability) {
      const double range = p.minimum_range_m + random.uniform() * (p.maximum_range_m - p.minimum_range_m);
      output.push_back({range, threshold, std::clamp(threshold / p.signal(range, 1), 0.0, 1.0),
          p.background_photons, p.pulse_fwhm_ns, 0, FalseAlarm, 0});
    }
  }
  // Select the N strongest detected echoes first; return_order only sorts that
  // set, matching the documented distinction between selection and ordering.
  std::stable_sort(output.begin(), output.end(), [](const Detection& a, const Detection& b) { return a.signal > b.signal; });
  if (output.size() > size_t(p.max_returns)) output.resize(p.max_returns);
  if (p.return_order != 0) std::sort(output.begin(), output.end(), [&](const Detection& a, const Detection& b) {
    return p.return_order == 1 ? a.range_m < b.range_m : a.range_m > b.range_m;
  });
  return output;
}
} // namespace CarlaPhysicalLidar
