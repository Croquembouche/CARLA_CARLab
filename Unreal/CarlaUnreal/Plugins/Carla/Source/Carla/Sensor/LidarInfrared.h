// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include "CoreMinimal.h"
#include "Carla/Sensor/LidarOpticsModel.h"
#include "Engine/HitResult.h"
class UPrimitiveComponent;
class UMaterialInterface;
namespace CarlaLidarInfrared {
// Explicit spectral coefficients and linear near-IR masks, independent of RGB.
void Prepare();
// Game-thread only, with no pending native sensor frames. Parse into temporary
// storage and publish atomically; failure leaves the previous registry intact.
bool Reload(const FString& Path,FString& Error);
uint64 Revision();
FString ProfileName(int32 Profile);
FString Status();
int32 Find(UPrimitiveComponent* Component,UMaterialInterface* Material);
CarlaLidarOptics::Material Sample(CarlaLidarOptics::Material Base,const FHitResult& Hit,
    float WavelengthNm,float Wetness,const FVector2f* MotionUV=nullptr);
// Append eight float4 metadata records per render section, plus only the UVs
// required by authored masks. Triangle indices are relative to each section.
// Snapshot-local: shared LOD0 topology is immutable during the game-thread build.
// A fresh cache per snapshot observes mesh edits, registry reloads and world changes.
// Geometry identity excludes component pose and material coefficients. Asset
// rebuilds replace render/UV resources or lighting GUID; weak IDs survive GC.
struct GeometryKey {
  TWeakObjectPtr<UObject> Mesh;
  const void* Render=nullptr;
  const void* Texcoords=nullptr;
  const void* Indices=nullptr;
  FGuid Guid;
  uint32 Section=0,Vertices=0,Triangles=0,FirstIndex=0;
  bool operator==(const GeometryKey& B) const {
    return Mesh==B.Mesh && Render==B.Render && Texcoords==B.Texcoords && Indices==B.Indices && Guid==B.Guid &&
      Section==B.Section && Vertices==B.Vertices && Triangles==B.Triangles && FirstIndex==B.FirstIndex;
  }
};
GeometryKey Key(UPrimitiveComponent* Component,uint32 Section);
using SnapshotUVCache = TMap<TPair<const UObject*,uint32>,FIntPoint>;
void Append(UPrimitiveComponent* Component,uint32 Section,int32 Profile,
    TArray<FVector4f>& Info,TArray<FVector4f>& UVs,SnapshotUVCache* Cache=nullptr);
// Geometric coverage is separate from spectral return/transmission.
bool HasOpacity(int32 Profile);
bool AllowsHit(int32 Profile,const FVector2f& UV);
const TArray<FVector4f>& Pixels();
}
