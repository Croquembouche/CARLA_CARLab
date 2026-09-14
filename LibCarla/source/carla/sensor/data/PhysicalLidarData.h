// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace carla { namespace sensor { namespace data {
// Explicitly opt-in little-endian wire format, version 1. Existing XYZI packets
// and sensor registry IDs are unchanged. Positions use CARLA sensor coordinates.
struct PhysicalLidarHeader {
  uint32_t magic = 0x504c4431; // PLD1
  uint16_t version = 1, point_stride = 64;
  uint32_t channels = 0, point_count = 0, pulse_count = 0, flags = 0;
  double scan_start = 0, scan_end = 0;
  float horizontal_angle = 0, wavelength_nm = 905;
  uint32_t profile_crc = 0, reserved = 0;
  uint64_t sequence = 0;
};
struct PhysicalLidarDetection {
  float x = 0, y = 0, z = 0, intensity = 0;
  float range = 0, signal = 0, ambient = 0, pulse_width = 0;
  float azimuth = 0, elevation = 0, time_offset = 0, confidence = 0;
  uint64_t pulse_id = 0;
  uint16_t channel = 0;
  uint8_t return_id = 0, return_count = 0;
  uint32_t flags = 0;
};
static_assert(sizeof(PhysicalLidarHeader) == 64, "Physical lidar header wire size");
static_assert(sizeof(PhysicalLidarDetection) == 64, "Physical lidar point wire size");
static_assert(offsetof(PhysicalLidarDetection, pulse_id) == 48, "Physical lidar point wire offsets");
class PhysicalLidarData {
public:
  PhysicalLidarHeader header;
  std::vector<PhysicalLidarDetection> points;
  void Reset(uint32_t channels, double start, double end, uint64_t sequence) {
    header = PhysicalLidarHeader{};
    header.channels = channels; header.scan_start = start; header.scan_end = end; header.sequence = sequence;
    points.clear();
  }
};
}}}
