#pragma once

#include "Templates/Atomic.h"

// One game-thread producer and one render-thread consumer. This bounds queued
// polling commands, not readbacks: each command still visits every readback.
class FCarlaReadbackPollGate
{
public:
  bool TryQueue(bool Coalesce)
  {
    if (Coalesce && Outstanding.Load() != 0) return false;
    ++Outstanding;
    return true;
  }

  void Complete() { --Outstanding; }
  int32 NumOutstanding() const { return Outstanding.Load(); }

private:
  TAtomic<int32> Outstanding{0};
};
