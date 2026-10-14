// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <algorithm>
#include <cmath>

#include "carla/trafficmanager/Constants.h"
#include "carla/trafficmanager/DataStructures.h"

namespace carla {
namespace traffic_manager {

namespace chr = std::chrono;

using namespace constants::PID;

using TimeInstance = chr::time_point<chr::system_clock, chr::nanoseconds>;

namespace PID {

/// Controller state is sampled on the episode clock, not the wall clock.
/// A newly registered vehicle has equal state timestamps; use the world's
/// last simulation delta in that case. Stale or rewound state uses that same
/// delta instead of integrating the entire pause or reversing derivatives.
inline float UpdateInterval(const StateEntry &present_state,
                            const StateEntry &previous_state) {
  const double elapsed = present_state.time_instance.elapsed_seconds -
      previous_state.time_instance.elapsed_seconds;
  const auto valid = [](double seconds) {
    return std::isfinite(seconds) && seconds > 0.0 && seconds <= 1.0;
  };
  if (valid(elapsed)) return static_cast<float>(elapsed);
  const double delta = present_state.time_instance.delta_seconds;
  return valid(delta) ? static_cast<float>(delta) : DT;
}

/// This function calculates the actuation signals based on the resent state
/// change of the vehicle to minimize PID error.
inline ActuationSignal RunStep(StateEntry present_state,
                        StateEntry previous_state,
                        const std::vector<float> &longitudinal_parameters,
                        const std::vector<float> &lateral_parameters) {

  const float step = UpdateInterval(present_state, previous_state);
  const float inverse_step = 1.0f / step;

  // Longitudinal PID calculation.
  const float expr_v =
      longitudinal_parameters[0] * present_state.velocity_deviation +
      longitudinal_parameters[1] * (present_state.velocity_deviation + previous_state.velocity_deviation) * step +
      longitudinal_parameters[2] * (present_state.velocity_deviation - previous_state.velocity_deviation) * inverse_step;

  float throttle;
  float brake;

  if (expr_v > 0.0f) {
    throttle = std::min(expr_v, MAX_THROTTLE);
    brake = 0.0f;
  } else {
    throttle = 0.0f;
    brake = std::min(std::abs(expr_v), MAX_BRAKE);
  }

  // Lateral PID calculation.
  float steer =
      lateral_parameters[0] * present_state.angular_deviation +
      lateral_parameters[1] * (present_state.angular_deviation + previous_state.angular_deviation) * step +
      lateral_parameters[2] * (present_state.angular_deviation - previous_state.angular_deviation) * inverse_step;

  const float steering_step = MAX_STEERING_DIFF * (step * INV_DT);
  steer = std::max(previous_state.steer - steering_step, std::min(steer, previous_state.steer + steering_step));
  steer = std::max(-MAX_STEERING, std::min(steer, MAX_STEERING));

  return ActuationSignal{throttle, brake, steer};
}

} // namespace PID
} // namespace traffic_manager
} // namespace carla
