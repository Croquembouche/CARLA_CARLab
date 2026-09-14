// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include "CoreMinimal.h"
#include "Carla/Sensor/PhysicalLidarProfile.h"
#include "Carla/Sensor/LidarDescription.h"
#include <util/disable-ue4-macros.h>
#include <carla/sensor/data/PhysicalLidarData.h>
#include <util/enable-ue4-macros.h>
class ASensor;
struct FPhysicalLidarFrame {
  carla::sensor::data::PhysicalLidarData Data;
  double DeliveryDelayMs=0;
};
class FPhysicalLidarState {
public:
  FPhysicalLidarProfile Profile;
  bool Initialize(const FLidarDescription& Description,FString& Error);
  void Observe(const ASensor& Sensor);
  void Simulate(ASensor& Sensor,const FLidarDescription& Description,float DeltaTime,
      TUniqueFunction<void(FPhysicalLidarFrame&&)>&& Complete);
private:
  FTransform PreviousPose;
  double PreviousTime=-1,Epoch=-1;
  TArray<double> NextFiring;
  uint64 PulseId=0,Sequence=0;
};
