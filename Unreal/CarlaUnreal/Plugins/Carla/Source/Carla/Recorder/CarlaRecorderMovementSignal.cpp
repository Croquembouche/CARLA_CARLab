#include "CarlaRecorderMovementSignal.h"
#include "CarlaRecorder.h"
#include "CarlaRecorderHelpers.h"
void CarlaRecorderMovementSignals::Write(std::ostream& Out) {
  WriteValue<char>(Out, static_cast<char>(CarlaRecorderPacketId::MovementSignal));
  WriteValue<uint32_t>(Out, 2 + 6 * Signals.size());
  WriteValue<uint16_t>(Out, Signals.size());
  for (const auto& S : Signals) { WriteValue<uint32_t>(Out,S.DatabaseId); WriteValue<uint16_t>(Out,S.States); }
}
void CarlaRecorderMovementSignals::Read(std::istream& In) {
  Signals.clear(); uint16_t Count=0; ReadValue<uint16_t>(In,Count);
  for (uint16_t I=0; I<Count; ++I) { CarlaRecorderMovementSignal S; ReadValue<uint32_t>(In,S.DatabaseId); ReadValue<uint16_t>(In,S.States); Signals.push_back(S); }
}
