// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/ros2/publishers/CarlaLidarPublisher.h"

#include <cstring>

namespace carla {
namespace ros2 {

namespace {

// Wire-compatible POD mirror of sensor::data::LidarDetection: {x, y, z,
// intensity} as four contiguous float32s. The real struct lives behind
// carla/sensor/data/LidarData.h, which transitively includes
// carla/MsgPack.h + carla/Buffer.h and therefore pulls Boost — not
// available on the carla-ros2-native ExternalProject include path. The
// POD shape and stride (16 B per point) are pinned by kLidarFields in
// PointCloudFieldsLayout.h and by test_point_cloud_fields.cpp.
struct LidarPoint {
  float x;
  float y;
  float z;
  float intensity;
};

static_assert(
    sizeof(LidarPoint) == 16u,
    "LidarPoint must be tightly packed (4 float32s) for the wire layout");

}  // namespace

std::size_t CarlaLidarPublisher::GetPointSize() const {
  return _extended ? 64u : sizeof(LidarPoint);
}

const PointFieldDescriptor *CarlaLidarPublisher::GetFieldDescriptors() const {
  return _extended ? kPhysicalLidarFields.data() : kLidarFields.data();
}

std::size_t CarlaLidarPublisher::GetFieldDescriptorCount() const {
  return _extended ? kPhysicalLidarFields.size() : kLidarFields.size();
}

std::vector<std::uint8_t> CarlaLidarPublisher::ComputePointCloud(
    std::uint32_t height, std::uint32_t width, const std::uint8_t *data) const {
  const std::size_t total_points =
      static_cast<std::size_t>(height) * static_cast<std::size_t>(width);
  const std::size_t total_bytes = total_points * GetPointSize();

  std::vector<std::uint8_t> bytes(total_bytes);
  if (total_bytes) std::memcpy(bytes.data(), data, total_bytes);

  // Byte copies avoid alignment and aliasing assumptions about vector<uint8_t>.
  for (std::size_t i = 0; i < total_points; ++i) {
    const auto base = i * GetPointSize();
    float value;
    std::memcpy(&value, bytes.data()+base+4, sizeof(value));value = -value;
    std::memcpy(bytes.data()+base+4, &value, sizeof(value));
    if (_extended) {
      std::memcpy(&value, bytes.data()+base+32, sizeof(value));value = -value;
      std::memcpy(bytes.data()+base+32, &value, sizeof(value));
    }
  }
  return bytes;
}

}  // namespace ros2
}  // namespace carla
