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
// Optional game-thread diagnostic of the selected optical return. No sensor
// wire format changes and no extra material lookup when omitted.
struct FCarlaLidarSurfaceAudit {
  UMaterialInterface* Material=nullptr;
  CarlaLidarOptics::Material Optical;
  uint32 GeometrySource=0; // 0 collision, 1 render triangles, 2 motion snapshot
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
  // Per-call editor audit only. Default/null audit always uses production repair.
  bool LegacyCollisionLookupForTest=false;
  bool LegacyCollisionLookupUsedForTest=false;
#endif
};
namespace CarlaLidarMotion { struct Snapshot; }
namespace CarlaLidarOptics {
// Owned by one game-thread scene snapshot. Never retained across updates.
struct MaterialInputs {
  FString Name;
  Material Preset;
  bool HasPreset=false;
  float Scalars[7]={};
  bool Present[7]={};
};
using SnapshotMaterialCache=TMap<UMaterialInterface*,MaterialInputs>;
void Prepare(UWorld* World);
Material Resolve(UPrimitiveComponent* Component, UMaterialInterface* Material,SnapshotMaterialCache* Cache=nullptr);
TArray<Material> Sections(UPrimitiveComponent* Component,SnapshotMaterialCache* Cache=nullptr);
// Explicit IR profiles also opt diffuse-only visible surfaces into native
// material-aware render geometry, independently of mechanical collision.
// Explicit optical exclusion keeps mechanical and legacy collision intact.
bool IgnoresOpticalCollision(UPrimitiveComponent* Component);
bool NeedsRenderSurface(UPrimitiveComponent* Component,const TArray<Material>& Materials);
FCarlaLidarHit Trace(AActor& Sensor, const FVector& Origin, const FVector& Direction,
    float Range, float AtmosphericAttenuation, const FCollisionQueryParams& Params,FCarlaLidarTrace* AllEchoes=nullptr,
    const CarlaLidarMotion::Snapshot* Motion=nullptr,float TimeFraction=1.f,float WavelengthNm=0.f,float Wetness=0.f,double SceneTime=-1,FCarlaLidarSurfaceAudit* Audit=nullptr);
}
