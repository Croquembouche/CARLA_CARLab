// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include "carla/sensor/data/Array.h"
#include "carla/sensor/data/PhysicalLidarData.h"
#include <cstring>
#include <stdexcept>

namespace carla { namespace sensor { namespace s11n { class LidarSerializer; }
namespace data {
class PhysicalLidarMeasurement : public Array<PhysicalLidarDetection> {
  using Super = Array<PhysicalLidarDetection>;
  friend s11n::LidarSerializer;
  static size_t Validate(const RawData& data) {
    if (data.size() < sizeof(PhysicalLidarHeader)) throw std::runtime_error("Truncated physical lidar header");
    PhysicalLidarHeader h; std::memcpy(&h, data.begin(), sizeof(h));
    if (h.magic != 0x504c4431 || h.version != 1 || h.point_stride != sizeof(PhysicalLidarDetection) ||
        h.channels == 0 || h.channels > 4096 || h.point_count > 8388608 ||
        data.size() != sizeof(h) + size_t(h.point_count) * sizeof(PhysicalLidarDetection))
      throw std::runtime_error("Invalid or unsupported physical lidar packet");
    return sizeof(h);
  }
  explicit PhysicalLidarMeasurement(RawData&& data) : Super(std::move(data), &Validate) {}
  PhysicalLidarHeader Header() const {
    PhysicalLidarHeader h; std::memcpy(&h, Super::GetRawData().begin(), sizeof(h)); return h;
  }
public:
  double GetScanStart() const { return Header().scan_start; }
  double GetScanEnd() const { return Header().scan_end; }
  uint32_t GetChannelCount() const { return Header().channels; }
  uint32_t GetPulseCount() const { return Header().pulse_count; }
  uint32_t GetFlags() const { return Header().flags; }
  uint32_t GetProfileCrc() const { return Header().profile_crc; }
  uint64_t GetSequence() const { return Header().sequence; }
  float GetHorizontalAngle() const { return Header().horizontal_angle; }
  float GetWavelength() const { return Header().wavelength_nm; }
};
}}}
