#pragma once
#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>
struct CarlaRecorderMovementSignal {
  uint32_t DatabaseId = 0;
  uint16_t States = 0;
};
class CarlaRecorderMovementSignals {
public:
  std::vector<CarlaRecorderMovementSignal> Signals;
  void Clear() { Signals.clear(); }
  void Add(CarlaRecorderMovementSignal Value) { Signals.push_back(Value); }
  void Write(std::ostream& Out);
  void Read(std::istream& In);
};
