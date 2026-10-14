// Frozen pre-refactor receiver model for automation comparisons. MIT license.
#pragma once
namespace CarlaPhysicalLidar {
inline std::vector<Detection> detect_reference(std::vector<Echo> echoes, const Receiver& p, Random& random) {
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
}
