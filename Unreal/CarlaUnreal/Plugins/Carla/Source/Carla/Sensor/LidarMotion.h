// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include "CoreMinimal.h"
#include "Engine/HitResult.h"
class UPrimitiveComponent;
class UWorld;
namespace CarlaLidarMotion {
struct Triangle {
  FVector3f Before[3], After[3];
  FVector2f UV[3];
  uint32 Section=0, Face=0;
  float NormalSign=1;
};
struct Node {
  FVector3f Min,Max;
  uint32 Left=0,Right=0; // leaf: Left high bit plus first triangle, Right count
};
// Immutable local-space geometry can outlive a frame and be shared with the
// render thread. Transforms and sample times remain specific to each Mesh.
struct MeshGeometry {
  TArray<Triangle> Triangles;
  TArray<Node> Nodes;
  TArray<FVector4f> PackedNodes, PackedTriangles;
  float LocalRadius=0;
};
struct Mesh {
  TWeakObjectPtr<UPrimitiveComponent> Component;
  uint32 Primitive=0;
  FTransform Before,After;
  FVector Center=FVector::ZeroVector;
  float Radius=0;
  double BeforeTime=0,AfterTime=0;
  TSharedPtr<const MeshGeometry,ESPMode::ThreadSafe> Geometry;
  TArray<int32> InfraredProfiles;
};
struct Snapshot {
  TArray<Mesh> Meshes;
  TSet<uint32> Primitives;
  uint32 MissingGeometry=0;
  double Time=0,OldestCoveredTime=0;
  void GpuBuffers(const FVector& Translation,TArray<FVector4f>& Info,TArray<FVector4f>& Nodes,
      TArray<FVector4f>& Triangles,TArray<FUintVector4>& Lookup,bool IncludeGeometry=true,const TArray<FIntPoint>* GeometryOffsets=nullptr) const;
  bool Intersect(const FVector& Origin,const FVector& Direction,float Range,float TimeFraction,
      FHitResult& Hit,uint32& Section,FVector2f& UV,double SceneTime=-1,bool OpticalOpacity=false) const;
};
// Called on the game thread. Actor transforms and skinned vertex positions are
// interpolated between observed ticks; physics itself is never rewound. Times
// use the replicated CARLA episode clock, matching physical firing times;
// standalone non-CARLA worlds fall back to their local world clock.
TSharedPtr<const Snapshot,ESPMode::ThreadSafe> Prepare(UWorld* World,const TArray<UPrimitiveComponent*>& Components);
// Select a per-query view only after preparing the complete world history.
// Swept mesh spheres cover rotation, scale and deformation between endpoints;
// the caller must bound every optical path, including reflected/refracted rays.
// Coverage and missing-geometry guards retain the full snapshot's values.
TSharedPtr<const Snapshot,ESPMode::ThreadSafe> WithinSphere(
    const TSharedPtr<const Snapshot,ESPMode::ThreadSafe>& Source,const FSphere& Bounds);
}
