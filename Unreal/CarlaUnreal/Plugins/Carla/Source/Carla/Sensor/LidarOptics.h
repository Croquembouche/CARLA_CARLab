// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Carla/Sensor/LidarOpticsModel.h"
class UPrimitiveComponent;
class UMaterialInterface;
class AActor;
class UWorld;
struct FCarlaLidarHit {
  FHitResult Hit;
  float SurfaceReturn=1.f;
  uint32 OpticalDepth=0;
};
struct FCarlaLidarTrace {
  TArray<FCarlaLidarHit> Echoes;
  float UnoccludedRange=0;
};
namespace CarlaLidarMotion { struct Snapshot; }
namespace CarlaLidarOptics {
// Owned by one game-thread scene snapshot. Never retained across updates.
struct MaterialInputs {
  FString Name;
  float Scalars[7]={};
  bool Present[7]={};
};
using SnapshotMaterialCache=TMap<UMaterialInterface*,MaterialInputs>;
void Prepare(UWorld* World);
Material Resolve(UPrimitiveComponent* Component, UMaterialInterface* Material,SnapshotMaterialCache* Cache=nullptr);
TArray<Material> Sections(UPrimitiveComponent* Component,SnapshotMaterialCache* Cache=nullptr);
FCarlaLidarHit Trace(AActor& Sensor, const FVector& Origin, const FVector& Direction,
    float Range, float AtmosphericAttenuation, const FCollisionQueryParams& Params,FCarlaLidarTrace* AllEchoes=nullptr,
    const CarlaLidarMotion::Snapshot* Motion=nullptr,float TimeFraction=1.f,float WavelengthNm=0.f,float Wetness=0.f,double SceneTime=-1);
}
