// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.


#include "carla/sensor/data/LidarMeasurement.h"
#include "carla/sensor/data/PhysicalLidarMeasurement.h"
#include "carla/sensor/s11n/LidarSerializer.h"

namespace carla {
namespace sensor {
namespace s11n {

  SharedPtr<SensorData> LidarSerializer::Deserialize(RawData &&data) {
    uint32_t magic = 0;
    if (data.size() >= sizeof(magic)) std::memcpy(&magic, data.begin(), sizeof(magic));
    if (magic == 0x504c4431)
      return SharedPtr<data::PhysicalLidarMeasurement>(new data::PhysicalLidarMeasurement{std::move(data)});
    return SharedPtr<data::LidarMeasurement>(
        new data::LidarMeasurement{std::move(data)});
  }

} // namespace s11n
} // namespace sensor
} // namespace carla
