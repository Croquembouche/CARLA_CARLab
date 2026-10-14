// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/LidarMotion.h"
#include "Carla/Sensor/LidarInfrared.h"
#include "Carla.h"
#include "Carla/Game/CarlaStatics.h"
#include "Carla/Game/CarlaEpisode.h"
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Misc/Crc.h"
#include "Engine/World.h"
#include <algorithm>
#include "Async/ParallelFor.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#endif

namespace CarlaLidarMotion {
namespace {
struct Geometry {
  TArray<FVector3f> Positions,Normals;
  TArray<FVector2f> UVs;
  TArray<uint32> Indices,Sections;
  TSharedPtr<const MeshGeometry,ESPMode::ThreadSafe> Acceleration;
  bool AccelerationCorrectWinding=false;
};
struct GeometryReadCache {
  TMap<UStaticMesh*,TSharedPtr<Geometry>> StaticShapes;
  struct SkinShape {
    const FSkinWeightVertexBuffer* Weights;
    TArray<FMatrix44f> Matrices;
    TSharedPtr<Geometry> Shape;
    bool Matches(const FSkinWeightVertexBuffer* OtherWeights,const TArray<FMatrix44f>& OtherMatrices) const {
      return Weights==OtherWeights && Matrices.Num()==OtherMatrices.Num() &&
          FMemory::Memcmp(Matrices.GetData(),OtherMatrices.GetData(),Matrices.Num()*sizeof(FMatrix44f))==0;
    }
  };
  TMap<USkeletalMesh*,TArray<SkinShape>> SkinShapes;
};
struct History {
  TWeakObjectPtr<UPrimitiveComponent> Component;
  TWeakObjectPtr<UObject> Asset;
  FTransform Transform;
  TSharedPtr<Geometry> Shape;
  uint32 PoseHash=0;
  double Time=0;
};
TWeakObjectPtr<UWorld> PreviousWorld;
TMap<uint32,History> Histories;
uint64 CachedFrame=MAX_uint64;
uint64 CachedInfraredRevision=MAX_uint64;
TSharedPtr<Snapshot,ESPMode::ThreadSafe> Cached;

bool ReadGeometry(UPrimitiveComponent* C,TSharedPtr<Geometry>& Out,TWeakObjectPtr<UObject>& Asset,uint32& PoseHash,const History* Old,GeometryReadCache* SharedShapes=nullptr) {
  if (auto* Static=Cast<UStaticMeshComponent>(C)) {
    UStaticMesh* Mesh=Static->GetStaticMesh();Asset=Mesh;
    // Local vertices belong to the asset; transforms, material overrides and
    // primitive IDs remain per component. The map lives for this snapshot.
    if (SharedShapes && Mesh) if (const auto* Shared=SharedShapes->StaticShapes.Find(Mesh)) { Out=*Shared;return true; }
    if (Old && Old->Asset==Asset && Old->Shape) {
      Out=Old->Shape;if(SharedShapes && Mesh)SharedShapes->StaticShapes.Add(Mesh,Out);return true;
    }
    if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty()) return false;
    const auto& L=Mesh->GetRenderData()->LODResources[0];const auto Index=L.IndexBuffer.GetArrayView();
    const uint32 Count=L.VertexBuffers.PositionVertexBuffer.GetNumVertices();
    if (!Count || !Index.Num()) return false;
    Out=MakeShared<Geometry>();
    Out->Positions.Reserve(Count);Out->Normals.Reserve(Count);Out->UVs.Reserve(Count);
    for (uint32 I=0;I<Count;++I) {
      Out->Positions.Add(L.VertexBuffers.PositionVertexBuffer.VertexPosition(I));
      Out->Normals.Add(L.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(I));
      Out->UVs.Add(L.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()?L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,0):FVector2f::ZeroVector);
    }
    Out->Indices.Reserve(Index.Num());for(int I=0;I<Index.Num();++I)Out->Indices.Add(Index[I]);Out->Sections.SetNumZeroed(Index.Num()/3);
    for (int S=0;S<L.Sections.Num();++S) for(uint32 I=0;I<L.Sections[S].NumTriangles;++I)
      Out->Sections[L.Sections[S].FirstIndex/3+I]=S;
    if(SharedShapes)SharedShapes->StaticShapes.Add(Mesh,Out);
    return true;
  }
  if (auto* Skin=Cast<USkinnedMeshComponent>(C)) {
    auto* Mesh=Cast<USkeletalMesh>(Skin->GetSkinnedAsset());Asset=Mesh;
    if (!Mesh || !Mesh->GetResourceForRendering() || Mesh->GetResourceForRendering()->LODRenderData.IsEmpty()) return false;
    const auto& L=Mesh->GetResourceForRendering()->LODRenderData[0];auto* Weights=Skin->GetSkinWeightBuffer(0);
    const auto* Indices=L.MultiSizeIndexContainer.GetIndexBuffer();
    if (!Weights || !Weights->GetNumVertices() || !Indices || !Indices->Num() || !L.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices()) return false;
    TArray<FMatrix44f> Matrices;Skin->GetCurrentRefToLocalMatrices(Matrices,0);
    PoseHash=FCrc::MemCrc32(Matrices.GetData(),Matrices.Num()*sizeof(FMatrix44f));
    // Sharing requires exactly the inputs used by ComputeSkinnedPositions,
    // not merely an equal pose hash. Per-instance transforms stay separate.
    const bool CanShare=SharedShapes && !Skin->LeaderPoseComponent.IsValid();
    if(CanShare) if(const auto* Entries=SharedShapes->SkinShapes.Find(Mesh)) {
      for(const auto& Entry:*Entries) if(Entry.Matches(Weights,Matrices)) { Out=Entry.Shape;return true; }
    }
    if (Old && Old->Asset==Asset && Old->PoseHash==PoseHash && Old->Shape) {
      Out=Old->Shape;if(CanShare)SharedShapes->SkinShapes.FindOrAdd(Mesh).Add({Weights,MoveTemp(Matrices),Out});return true;
    }
    Out=MakeShared<Geometry>();
    USkinnedMeshComponent::ComputeSkinnedPositions(Skin,Out->Positions,Matrices,L,*Weights);
    if (Out->Positions.IsEmpty()) return false;
    for (int I=0;I<Out->Positions.Num();++I) {
      Out->Normals.Add(L.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(I));
      Out->UVs.Add(L.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()?L.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,0):FVector2f::ZeroVector);
    }
    for (int I=0;I<Indices->Num();++I) Out->Indices.Add(Indices->Get(I));
    Out->Sections.SetNumZeroed(Indices->Num()/3);
    for(int S=0;S<L.RenderSections.Num();++S) for(uint32 I=0;I<L.RenderSections[S].NumTriangles;++I)
      Out->Sections[L.RenderSections[S].BaseIndex/3+I]=S;
    if(CanShare)SharedShapes->SkinShapes.FindOrAdd(Mesh).Add({Weights,MoveTemp(Matrices),Out});
    return true;
  }
  return false;
}
struct BuildBounds { FVector3f Min,Max,CenterSum; };
uint32 BuildNodes(MeshGeometry& M,const TArray<BuildBounds>& Bounds,int First,int Count,int Depth) {
  const uint32 Index=M.Nodes.Num();M.Nodes.AddDefaulted();
  FVector3f Lo(FLT_MAX),Hi(-FLT_MAX),Clo(FLT_MAX),Chi(-FLT_MAX);
  for(int I=First;I<First+Count;++I) {
    const auto& B=Bounds[M.Triangles[I].Face];const FVector3f Center=B.CenterSum/6;
    Lo=Lo.ComponentMin(B.Min);Hi=Hi.ComponentMax(B.Max);Clo=Clo.ComponentMin(Center);Chi=Chi.ComponentMax(Center);
  }
  M.Nodes[Index].Min=Lo-FVector3f(.001f);M.Nodes[Index].Max=Hi+FVector3f(.001f);
  if(Count<=4 || Depth>=30) { M.Nodes[Index].Left=0x80000000u|uint32(First);M.Nodes[Index].Right=Count;return Index; }
  const FVector3f Size=Chi-Clo;const int Axis=Size.X>=Size.Y && Size.X>=Size.Z?0:Size.Y>=Size.Z?1:2;
  const int Middle=First+Count/2;
  std::nth_element(M.Triangles.GetData()+First,M.Triangles.GetData()+Middle,M.Triangles.GetData()+First+Count,
      [&Bounds,Axis](const Triangle& A,const Triangle& B) { return Bounds[A.Face].CenterSum[Axis]<Bounds[B.Face].CenterSum[Axis]; });
  const uint32 Left=BuildNodes(M,Bounds,First,Middle-First,Depth+1),Right=BuildNodes(M,Bounds,Middle,First+Count-Middle,Depth+1);
  M.Nodes[Index].Left=Left;M.Nodes[Index].Right=Right;return Index;
}
uint32 Build(MeshGeometry& M,int First,int Count) {
  // Bounds and centroid sums are invariant during partitioning. Preserve the
  // original arithmetic and comparisons while computing them only once.
  TArray<BuildBounds> Bounds;Bounds.SetNumUninitialized(M.Triangles.Num());
  for(const auto& T:M.Triangles) {
    auto& B=Bounds[T.Face];B.Min=FVector3f(FLT_MAX);B.Max=FVector3f(-FLT_MAX);B.CenterSum=FVector3f(0);
    for(int V=0;V<3;++V) {
      B.Min=B.Min.ComponentMin(T.Before[V]).ComponentMin(T.After[V]);B.Max=B.Max.ComponentMax(T.Before[V]).ComponentMax(T.After[V]);B.CenterSum+=T.Before[V]+T.After[V];
    }
  }
  return BuildNodes(M,Bounds,First,Count,0);
}
float Bits(uint32 U) { float F;FMemory::Memcpy(&F,&U,4);return F; }

TSharedPtr<const MeshGeometry,ESPMode::ThreadSafe> BuildGeometry(
    const Geometry& Before,const Geometry& After,bool CorrectWinding) {
  auto M=MakeShared<MeshGeometry,ESPMode::ThreadSafe>();
  M->Triangles.Reserve(After.Indices.Num()/3);
  for(int I=0;I<After.Indices.Num()/3;++I) {
    Triangle T;T.Section=After.Sections[I];T.Face=I;
    for(int V=0;V<3;++V) {
      const uint32 Vertex=After.Indices[I*3+V];T.Before[V]=Before.Positions[Vertex];T.After[V]=After.Positions[Vertex];T.UV[V]=After.UVs[Vertex];
      M->LocalRadius=FMath::Max(M->LocalRadius,FMath::Max(T.Before[V].Size(),T.After[V].Size()));
    }
    if(CorrectWinding && FVector3f::DotProduct(FVector3f::CrossProduct(T.After[1]-T.After[0],T.After[2]-T.After[0]),After.Normals[After.Indices[I*3]])<0)T.NormalSign=-1;
    M->Triangles.Add(T);
  }
  if(!M->Triangles.IsEmpty())Build(*M,0,M->Triangles.Num());
  // These values do not contain world transforms or frame times. Pack them
  // once with the BVH instead of rebuilding seven vectors per triangle/frame.
  M->PackedNodes.Reserve(M->Nodes.Num()*2);M->PackedTriangles.Reserve(M->Triangles.Num()*7);
  for(const auto& N:M->Nodes) { M->PackedNodes.Add(FVector4f(N.Min,Bits(N.Left)));M->PackedNodes.Add(FVector4f(N.Max,Bits(N.Right))); }
  for(const auto& T:M->Triangles) {
    M->PackedTriangles.Add(FVector4f(T.Before[0],T.UV[0].X));M->PackedTriangles.Add(FVector4f(T.Before[1],T.UV[0].Y));M->PackedTriangles.Add(FVector4f(T.Before[2],T.UV[1].X));
    M->PackedTriangles.Add(FVector4f(T.After[0],T.UV[1].Y));M->PackedTriangles.Add(FVector4f(T.After[1],T.UV[2].X));M->PackedTriangles.Add(FVector4f(T.After[2],T.UV[2].Y));
    M->PackedTriangles.Add(FVector4f(Bits(T.Section),Bits(T.Face),T.NormalSign,0));
  }
  return M;
}

TSharedPtr<const MeshGeometry,ESPMode::ThreadSafe> GeometryFor(
    const TSharedPtr<Geometry>& Before,const TSharedPtr<Geometry>& After,bool CorrectWinding,bool& Reused) {
  // Shape identity proves unchanged local vertices, topology, UVs and normals.
  // A deforming transition must never reuse a stationary or previous-pose BVH.
  Reused=Before==After && After->Acceleration.IsValid() && After->AccelerationCorrectWinding==CorrectWinding;
  if(Reused)return After->Acceleration;
  auto Result=BuildGeometry(*Before,*After,CorrectWinding);
  if(Before==After) { After->Acceleration=Result;After->AccelerationCorrectWinding=CorrectWinding; }
  return Result;
}
struct GeometryBuild {
  TSharedPtr<Geometry> Before,After;
  bool CorrectWinding=false;
  TSharedPtr<const MeshGeometry,ESPMode::ThreadSafe> Result;
};
void BuildGeometryJobs(TArray<GeometryBuild>& Jobs) {
  // Only immutable plain geometry is read on workers. UObject reads and cache
  // publication stay on the game thread, and each result has one writer.
  const int32 Workers=FMath::Min(4,Jobs.Num());
  ParallelFor(Workers,[&Jobs,Workers](int32 Worker) {
    for(int32 I=Worker;I<Jobs.Num();I+=Workers) {
      auto& Job=Jobs[I];Job.Result=BuildGeometry(*Job.Before,*Job.After,Job.CorrectWinding);
    }
  });
  for(auto& Job:Jobs) if(Job.Before==Job.After) {
    Job.After->Acceleration=Job.Result;Job.After->AccelerationCorrectWinding=Job.CorrectWinding;
  }
}
bool Box(const FVector& O,const FVector& D,const FVector3f& Lo,const FVector3f& Hi,double Limit) {
  double Near=0,Far=Limit;
  for(int A=0;A<3;++A) {
    if(FMath::Abs(D[A])<1.e-12) { if(O[A]<Lo[A] || O[A]>Hi[A])return false;continue; }
    double X=(Lo[A]-O[A])/D[A],Y=(Hi[A]-O[A])/D[A];if(X>Y)Swap(X,Y);Near=FMath::Max(Near,X);Far=FMath::Min(Far,Y);
    if(Far<Near)return false;
  }return true;
}
}

TSharedPtr<const Snapshot,ESPMode::ThreadSafe> Prepare(UWorld* World,const TArray<UPrimitiveComponent*>& Components) {
  check(IsInGameThread());
  if(PreviousWorld!=World) { Histories.Reset();PreviousWorld=World;CachedFrame=MAX_uint64; }
  CarlaLidarInfrared::Prepare();const uint64 Revision=CarlaLidarInfrared::Revision();
  if(CachedFrame==GFrameCounter && CachedInfraredRevision==Revision)return Cached;
  CachedInfraredRevision=Revision;
  const double Started=FPlatformTime::Seconds();double BuildSeconds=0;int ReusedMeshes=0,RebuiltMeshes=0;
  CachedFrame=GFrameCounter;Cached=MakeShared<Snapshot,ESPMode::ThreadSafe>();TSet<uint32> Seen;
  // Secondary UWorld time advances on its local tick delta, while replicated
  // actor poses and physical firing times use the primary episode clock. Keep
  // motion endpoints in that same clock; mixing them can leave early pulses
  // uncovered even with complete consecutive replicated snapshots.
  const auto* Episode=UCarlaStatics::GetCurrentEpisode(World);
  Cached->Time=Episode?Episode->GetElapsedGameTime():World->GetTimeSeconds();
  Cached->OldestCoveredTime=0;
  struct PendingMesh { UPrimitiveComponent* Component; History Before,After; bool Compatible=false; };
  TArray<PendingMesh> Pending;TSet<AActor*> MovingActors;
  GeometryReadCache SharedShapes;
  for(auto* C:Components) {
    if(!C || C->Mobility!=EComponentMobility::Movable || !C->IsRegistered() ||
        !C->IsVisible() || C->bHiddenInGame || C->GetOwner()->IsHidden() || C->ComponentHasTag(TEXT("LidarIgnore")))continue;
    if(!C->IsA<UStaticMeshComponent>() && !C->IsA<USkinnedMeshComponent>())continue;
    const uint32 Id=C->GetPrimitiveSceneId().PrimIDValue;if(!Id)continue;
    Seen.Add(Id);History* Old=Histories.Find(Id);const FTransform Current=C->GetComponentTransform();
    if(Old && Old->Component!=C) { Histories.Remove(Id);Old=nullptr; }
    TSharedPtr<Geometry> Shape;TWeakObjectPtr<UObject> Asset;uint32 PoseHash=0;
    const bool Available=ReadGeometry(C,Shape,Asset,PoseHash,Old,&SharedShapes);
    const bool Moved=Old && (!Old->Transform.Equals(Current,1.e-8) || Old->Shape!=Shape);
    const bool Compatible=Available && Old && Old->Shape && Old->Asset==Asset &&
        (Old->Shape==Shape || (Old->Shape->Positions.Num()==Shape->Positions.Num() && Old->Shape->Indices==Shape->Indices));
    if(Moved) {
      MovingActors.Add(C->GetOwner());
      if(!Compatible) {
        ++Cached->MissingGeometry;
        UE_LOG(LogCarla,Warning,TEXT("PHYSICAL_MOTION_MISSING actor=%s component=%s available=%d old_vertices=%d new_vertices=%d time=%.12f"),
            *C->GetOwner()->GetName(),*C->GetName(),int32(Available),
            Old->Shape?Old->Shape->Positions.Num():0,Shape?Shape->Positions.Num():0,Cached->Time);
      }
    }
    const History After{C,Asset,Current,Shape,PoseHash,Cached->Time};
    Pending.Add({C,Old?*Old:After,After,Compatible});Histories.Add(Id,After);
  }
  TArray<GeometryBuild> Jobs;
  TArray<int32> JobIndices;JobIndices.Init(INDEX_NONE,Pending.Num());
  TArray<TSharedPtr<const MeshGeometry,ESPMode::ThreadSafe>> Results;Results.SetNum(Pending.Num());
  for(int32 I=0;I<Pending.Num();++I) {
    const auto& Entry=Pending[I];
    if(!Entry.Compatible || !MovingActors.Contains(Entry.Component->GetOwner()))continue;
    const bool Winding=Entry.Component->IsA<UStaticMeshComponent>();
    const auto& Before=Entry.Before.Shape;const auto& After=Entry.After.Shape;
    if(Before==After && After->Acceleration.IsValid() && After->AccelerationCorrectWinding==Winding) {
      Results[I]=After->Acceleration;++ReusedMeshes;continue;
    }
    int32 Job=Jobs.IndexOfByPredicate([&](const GeometryBuild& Existing) {
      return Existing.Before==Before && Existing.After==After && Existing.CorrectWinding==Winding;
    });
    if(Job==INDEX_NONE) { Job=Jobs.Add({Before,After,Winding,{}});++RebuiltMeshes; }
    else ++ReusedMeshes;
    JobIndices[I]=Job;
  }
  const double BuildStarted=FPlatformTime::Seconds();BuildGeometryJobs(Jobs);
  BuildSeconds=FPlatformTime::Seconds()-BuildStarted;
  for(int32 I=0;I<Pending.Num();++I)if(JobIndices[I]!=INDEX_NONE)Results[I]=Jobs[JobIndices[I]].Result;
  CarlaGpuSensors::ProfileSample(TEXT("motion_build_workers"),FMath::Min(4,Jobs.Num()));
  // Include stationary sibling meshes when a wheel or skinned part moves. This
  // permits removing a shared collision proxy without losing the rest of a car.
  for(int32 I=0;I<Pending.Num();++I) {
    const auto& Entry=Pending[I];auto* C=Entry.Component;
    if(!Entry.Compatible || !MovingActors.Contains(C->GetOwner()))continue;
    const auto& Current=Entry.After.Transform;
    const uint32 Id=C->GetPrimitiveSceneId().PrimIDValue;
      Mesh M;M.Component=C;M.Primitive=Id;M.Before=Entry.Before.Transform;M.After=Current;M.BeforeTime=Entry.Before.Time;M.AfterTime=Entry.After.Time;
      Cached->OldestCoveredTime=FMath::Max(Cached->OldestCoveredTime,M.BeforeTime);
      M.Center=(M.Before.GetLocation()+M.After.GetLocation())*.5;
      M.Geometry=Results[I];
      for(const auto& Material:CarlaLidarOptics::Sections(M.Component.Get()))M.InfraredProfiles.Add(Material.infrared);
      M.Radius=M.Geometry->LocalRadius*FMath::Max(M.Before.GetScale3D().GetAbsMax(),M.After.GetScale3D().GetAbsMax())+
          FVector::Distance(M.Before.GetLocation(),M.After.GetLocation())*.5+1;
      if(!M.Geometry->Triangles.IsEmpty()) { Cached->Primitives.Add(Id);Cached->Meshes.Add(MoveTemp(M)); }
  }
  for(auto It=Histories.CreateIterator();It;++It) if(!Seen.Contains(It.Key()) || !It.Value().Component.IsValid())It.RemoveCurrent();
  CarlaGpuSensors::ProfileSample(TEXT("motion_prepare_ms"),(FPlatformTime::Seconds()-Started)*1000);
  CarlaGpuSensors::ProfileSample(TEXT("motion_geometry_ms"),BuildSeconds*1000);
  CarlaGpuSensors::ProfileSample(TEXT("motion_reused_meshes"),ReusedMeshes);
  CarlaGpuSensors::ProfileSample(TEXT("motion_rebuilt_meshes"),RebuiltMeshes);
  return Cached;
}

TSharedPtr<const Snapshot,ESPMode::ThreadSafe> WithinSphere(
    const TSharedPtr<const Snapshot,ESPMode::ThreadSafe>& Source,const FSphere& Bounds) {
  if(!Source || Bounds.Center.ContainsNaN() || !FMath::IsFinite(Bounds.W) || Bounds.W<0)return Source;
  auto InBounds=[&](const Mesh& M) {
    // Invalid bounds cannot prove exclusion. Keep tangent meshes and pad the
    // comparison outward; the same swept sphere is used by the GPU broadphase.
    if(M.Center.ContainsNaN() || !FMath::IsFinite(M.Radius) || M.Radius<0)return true;
    const double Radius=double(M.Radius)+Bounds.W+1.0;
    return FVector::DistSquared(M.Center,Bounds.Center)<=Radius*Radius;
  };
  if(!Source->Meshes.ContainsByPredicate([&](const Mesh& M) { return !InBounds(M); }))return Source;
  auto Selected=MakeShared<Snapshot,ESPMode::ThreadSafe>();
  Selected->Time=Source->Time;Selected->OldestCoveredTime=Source->OldestCoveredTime;
  Selected->MissingGeometry=Source->MissingGeometry;
  Selected->Meshes.Reserve(Source->Meshes.Num());
  for(const auto& M:Source->Meshes)if(InBounds(M)) {
    Selected->Meshes.Add(M);Selected->Primitives.Add(M.Primitive);
  }
  return Selected;
}

void Snapshot::GpuBuffers(const FVector& Translation,TArray<FVector4f>& Info,TArray<FVector4f>& NodeData,TArray<FVector4f>& TriangleData,TArray<FUintVector4>& Table,bool IncludeGeometry,const TArray<FIntPoint>* GeometryOffsets) const {
  check(!GeometryOffsets || (!IncludeGeometry && GeometryOffsets->Num()==Meshes.Num()));
  const uint32 Size=FMath::RoundUpToPowerOfTwo(FMath::Max(2,Meshes.Num()*2));Table.SetNumZeroed(Size);
  int NodeCount=NodeData.Num(),TriangleCount=TriangleData.Num();
  TSet<const MeshGeometry*> UniqueGeometry;
  for(const auto& M:Meshes) if(!UniqueGeometry.Contains(M.Geometry.Get())) {
    UniqueGeometry.Add(M.Geometry.Get());NodeCount+=M.Geometry->PackedNodes.Num();TriangleCount+=M.Geometry->PackedTriangles.Num();
  }
  if(IncludeGeometry) { NodeData.Reserve(NodeCount);TriangleData.Reserve(TriangleCount); }
  Info.Reserve(Info.Num()+Meshes.Num()*8);
  uint32 NextNode=NodeData.Num()/2,NextTriangle=TriangleData.Num()/7;
  TMap<const MeshGeometry*,FIntPoint> PackedOffsets;
  for(int I=0;I<Meshes.Num();++I) {
    const auto& M=Meshes[I];
    uint32 NodeBase=NextNode,TriBase=NextTriangle;bool NewGeometry=false;
    if(GeometryOffsets) { NodeBase=(*GeometryOffsets)[I].X;TriBase=(*GeometryOffsets)[I].Y; }
    else if(const auto* Offset=PackedOffsets.Find(M.Geometry.Get())) { NodeBase=Offset->X;TriBase=Offset->Y; }
    else {
      NewGeometry=true;PackedOffsets.Add(M.Geometry.Get(),FIntPoint(NodeBase,TriBase));
      NextNode+=M.Geometry->PackedNodes.Num()/2;NextTriangle+=M.Geometry->PackedTriangles.Num()/7;
    }
    auto Q0=M.Before.GetRotation(),Q1=M.After.GetRotation();if((Q0|Q1)<0)Q1=Q1*-1;
    Info.Add(FVector4f(FVector3f(M.Before.GetLocation()+Translation),Bits(NodeBase)));
    Info.Add(FVector4f(FVector3f(M.After.GetLocation()+Translation),Bits(M.Geometry->Nodes.Num())));
    Info.Add(FVector4f(Q0.X,Q0.Y,Q0.Z,Q0.W));Info.Add(FVector4f(Q1.X,Q1.Y,Q1.Z,Q1.W));
    Info.Add(FVector4f(FVector3f(M.Before.GetScale3D()),Bits(TriBase)));
    Info.Add(FVector4f(FVector3f(M.After.GetScale3D()),Bits(M.Geometry->Triangles.Num())));
    Info.Add(FVector4f(FVector3f(M.Center+Translation),M.Radius));
    Info.Add(FVector4f(Bits(M.Primitive),M.BeforeTime-Time,M.AfterTime-Time,0));
    if(IncludeGeometry && NewGeometry) { NodeData.Append(M.Geometry->PackedNodes);TriangleData.Append(M.Geometry->PackedTriangles); }
    uint32 Slot=(M.Primitive*2654435761u)&(Size-1);while(Table[Slot].X)Slot=(Slot+1)&(Size-1);Table[Slot]=FUintVector4(M.Primitive,I+1,0,0);
  }
  if(Info.IsEmpty())Info.SetNumZeroed(8);
  if(IncludeGeometry) { if(NodeData.IsEmpty())NodeData.SetNumZeroed(2);if(TriangleData.IsEmpty())TriangleData.SetNumZeroed(7); }
}

bool Snapshot::Intersect(const FVector& Origin,const FVector& Direction,float Range,float TimeFraction,FHitResult& Hit,uint32& Section,FVector2f& UV,double SceneTime,bool OpticalOpacity) const {
  bool Found=false;double Nearest=Range;
  for(const auto& M:Meshes) {
    const float Alpha=SceneTime>=0?FMath::Clamp(float((SceneTime-M.BeforeTime)/FMath::Max(1.e-9,M.AfterTime-M.BeforeTime)),0.f,1.f):FMath::Clamp(TimeFraction,0.f,1.f);
    const FVector Rel=M.Center-Origin;const double Along=FVector::DotProduct(Rel,Direction);
    if(Along+M.Radius<0 || Along-M.Radius>Nearest || Rel.SizeSquared()-Along*Along>M.Radius*M.Radius)continue;
    auto* C=M.Component.Get();if(!C)continue;
    FTransform Transform;Transform.Blend(M.Before,M.After,Alpha);
    const FVector O=Transform.InverseTransformPosition(Origin),D=Transform.InverseTransformVector(Direction);
    uint32 Stack[64];int Count=1;Stack[0]=0;
    while(Count) {
      const auto& N=M.Geometry->Nodes[Stack[--Count]];if(!Box(O,D,N.Min,N.Max,Nearest))continue;
      if(!(N.Left&0x80000000u)) { Stack[Count++]=N.Left;Stack[Count++]=N.Right;continue; }
      for(uint32 I=0;I<N.Right;++I) {
        const auto& T=M.Geometry->Triangles[(N.Left&0x7fffffffu)+I];
        const FVector A(FMath::Lerp(T.Before[0],T.After[0],Alpha)),B(FMath::Lerp(T.Before[1],T.After[1],Alpha)),V(FMath::Lerp(T.Before[2],T.After[2],Alpha));
        const FVector E1=B-A,E2=V-A,P=FVector::CrossProduct(D,E2);const double Det=FVector::DotProduct(E1,P);if(FMath::Abs(Det)<1.e-12)continue;
        const FVector X=O-A;const double U=FVector::DotProduct(X,P)/Det;if(U<0 || U>1)continue;
        const FVector Q=FVector::CrossProduct(X,E1);const double W=FVector::DotProduct(D,Q)/Det;if(W<0 || W+U>1)continue;
        const double Distance=FVector::DotProduct(E2,Q)/Det;if(Distance<.001 || Distance>=Nearest)continue;
        const FVector2f CandidateUV=T.UV[0]*float(1-U-W)+T.UV[1]*float(U)+T.UV[2]*float(W);
        if(OpticalOpacity && M.InfraredProfiles.IsValidIndex(T.Section) && !CarlaLidarInfrared::AllowsHit(M.InfraredProfiles[T.Section],CandidateUV))continue;
        const FVector Normal=FVector::CrossProduct(Transform.TransformVector(E1),Transform.TransformVector(E2)).GetSafeNormal()*T.NormalSign;
        Hit=FHitResult(C->GetOwner(),C,Origin+Direction*Distance,Normal);Hit.bBlockingHit=true;Hit.Distance=Distance;Hit.FaceIndex=T.Face;
        Section=T.Section;UV=T.UV[0]*(1-U-W)+T.UV[1]*U+T.UV[2]*W;Nearest=Distance;Found=true;
      }
    }
  }return Found;
}

#if WITH_DEV_AUTOMATION_TESTS
uint32 ReferenceBuild(MeshGeometry& M,int First,int Count,int Depth=0) {
  const uint32 Index=M.Nodes.Num();M.Nodes.AddDefaulted();
  FVector3f Lo(FLT_MAX),Hi(-FLT_MAX),Clo(FLT_MAX),Chi(-FLT_MAX);
  for(int I=First;I<First+Count;++I) {
    const auto& T=M.Triangles[I];FVector3f Center(0);
    for(int V=0;V<3;++V) { Lo=Lo.ComponentMin(T.Before[V]).ComponentMin(T.After[V]);Hi=Hi.ComponentMax(T.Before[V]).ComponentMax(T.After[V]);Center+=T.Before[V]+T.After[V]; }
    Center/=6;Clo=Clo.ComponentMin(Center);Chi=Chi.ComponentMax(Center);
  }
  M.Nodes[Index].Min=Lo-FVector3f(.001f);M.Nodes[Index].Max=Hi+FVector3f(.001f);
  if(Count<=4 || Depth>=30) { M.Nodes[Index].Left=0x80000000u|uint32(First);M.Nodes[Index].Right=Count;return Index; }
  const FVector3f Size=Chi-Clo;const int Axis=Size.X>=Size.Y && Size.X>=Size.Z?0:Size.Y>=Size.Z?1:2;
  const int Middle=First+Count/2;
  std::nth_element(M.Triangles.GetData()+First,M.Triangles.GetData()+Middle,M.Triangles.GetData()+First+Count,
      [Axis](const Triangle& A,const Triangle& B) { float X=0,Y=0;for(int V=0;V<3;++V) {X+=A.Before[V][Axis]+A.After[V][Axis];Y+=B.Before[V][Axis]+B.After[V][Axis];}return X<Y; });
  const uint32 Left=ReferenceBuild(M,First,Middle-First,Depth+1),Right=ReferenceBuild(M,Middle,First+Count-Middle,Depth+1);
  M.Nodes[Index].Left=Left;M.Nodes[Index].Right=Right;return Index;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMotionGeometryCacheTest,"Carla.Sensors.LidarMotionCache",
    EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::EngineFilter)
bool FMotionGeometryCacheTest::RunTest(const FString&) {
  TStrongObjectPtr<UStaticMeshComponent> InstanceA(NewObject<UStaticMeshComponent>()),InstanceB(NewObject<UStaticMeshComponent>());
  UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
  if(!TestNotNull(TEXT("reference static mesh loads"),Cube))return false;
  InstanceA->SetStaticMesh(Cube);InstanceB->SetStaticMesh(Cube);InstanceB->SetWorldLocation(FVector(100,200,300));
  GeometryReadCache SharedShapes;TSharedPtr<Geometry> ShapeA,ShapeB;TWeakObjectPtr<UObject> AssetA,AssetB;uint32 PoseA=0,PoseB=0;
  TestTrue(TEXT("first instance geometry reads"),ReadGeometry(InstanceA.Get(),ShapeA,AssetA,PoseA,nullptr,&SharedShapes));
  TestTrue(TEXT("second instance geometry reads"),ReadGeometry(InstanceB.Get(),ShapeB,AssetB,PoseB,nullptr,&SharedShapes));
  TestTrue(TEXT("identical assets share local geometry across transformed instances"),ShapeA.IsValid() && ShapeA==ShapeB);
  FSkinWeightVertexBuffer WeightsA,WeightsB;
  TArray<FMatrix44f> Pose{FMatrix44f::Identity};
  GeometryReadCache::SkinShape SharedSkin{&WeightsA,Pose,ShapeA};
  TestTrue(TEXT("identical skinning inputs can share geometry"),SharedSkin.Matches(&WeightsA,Pose));
  TestFalse(TEXT("different skin weights cannot share geometry"),SharedSkin.Matches(&WeightsB,Pose));
  Pose[0].M[3][0]=.0001f;
  TestFalse(TEXT("even a small bone-pose change prevents sharing"),SharedSkin.Matches(&WeightsA,Pose));
  auto Shape=MakeShared<Geometry>();
  for(int Y=0;Y<=4;++Y)for(int X=0;X<=4;++X) {
    Shape->Positions.Add(FVector3f(X,Y,0));Shape->Normals.Add(FVector3f(0,0,1));Shape->UVs.Add(FVector2f(X/4.f,Y/4.f));
  }
  for(int Y=0;Y<4;++Y)for(int X=0;X<4;++X) {
    const uint32 A=Y*5+X;for(uint32 I:{A,A+1,A+5,A+1,A+6,A+5})Shape->Indices.Add(I);
    Shape->Sections.Add((X+Y)%3);Shape->Sections.Add((X+Y)%3);
  }
  // Rebuild with the original per-frame construction and packing order. This
  // oracle deliberately bypasses the cache and the new prepacked-buffer path.
  auto Reference=[](const Geometry& Before,const Geometry& After,bool Winding) {
    auto M=MakeShared<MeshGeometry,ESPMode::ThreadSafe>();
    for(int I=0;I<After.Indices.Num()/3;++I) {
      Triangle T;T.Section=After.Sections[I];T.Face=I;
      for(int V=0;V<3;++V) {
        const uint32 J=After.Indices[I*3+V];T.Before[V]=Before.Positions[J];T.After[V]=After.Positions[J];T.UV[V]=After.UVs[J];
        M->LocalRadius=FMath::Max(M->LocalRadius,FMath::Max(T.Before[V].Size(),T.After[V].Size()));
      }
      if(Winding && FVector3f::DotProduct(FVector3f::CrossProduct(T.After[1]-T.After[0],T.After[2]-T.After[0]),After.Normals[After.Indices[I*3]])<0)T.NormalSign=-1;
      M->Triangles.Add(T);
    }
    ReferenceBuild(*M,0,M->Triangles.Num());return M;
  };
  auto CheckPacked=[this](const MeshGeometry& CachedGeometry,const MeshGeometry& Expected) {
    TArray<FVector4f> Nodes,Triangles;
    for(const auto& N:Expected.Nodes) { Nodes.Add(FVector4f(N.Min,Bits(N.Left)));Nodes.Add(FVector4f(N.Max,Bits(N.Right))); }
    for(const auto& T:Expected.Triangles) {
      Triangles.Add(FVector4f(T.Before[0],T.UV[0].X));Triangles.Add(FVector4f(T.Before[1],T.UV[0].Y));Triangles.Add(FVector4f(T.Before[2],T.UV[1].X));
      Triangles.Add(FVector4f(T.After[0],T.UV[1].Y));Triangles.Add(FVector4f(T.After[1],T.UV[2].X));Triangles.Add(FVector4f(T.After[2],T.UV[2].Y));
      Triangles.Add(FVector4f(Bits(T.Section),Bits(T.Face),T.NormalSign,0));
    }
    TestEqual(TEXT("radius preserved"),CachedGeometry.LocalRadius,Expected.LocalRadius);
    TestTrue(TEXT("BVH buffer identical to original"),Nodes.Num()==CachedGeometry.PackedNodes.Num() &&
        FMemory::Memcmp(Nodes.GetData(),CachedGeometry.PackedNodes.GetData(),Nodes.Num()*sizeof(FVector4f))==0);
    TestTrue(TEXT("triangle attributes identical to original"),Triangles.Num()==CachedGeometry.PackedTriangles.Num() &&
        FMemory::Memcmp(Triangles.GetData(),CachedGeometry.PackedTriangles.GetData(),Triangles.Num()*sizeof(FVector4f))==0);
  };
  bool Reused=false;const auto First=GeometryFor(Shape,Shape,true,Reused);
  TestFalse(TEXT("first shape builds"),Reused);
  const auto Again=GeometryFor(Shape,Shape,true,Reused);
  TestTrue(TEXT("unchanged shape shares immutable acceleration"),Reused && First==Again);
  CheckPacked(*Again,*Reference(*Shape,*Shape,true));

  TStrongObjectPtr<UStaticMeshComponent> Component(NewObject<UStaticMeshComponent>());
  Mesh M;M.Component=Component.Get();M.Primitive=123;M.Geometry=Again;
  M.Before=FTransform(FRotator(0,0,0),FVector(10,20,30),FVector(1,2,1));
  M.After=FTransform(FRotator(0,60,0),FVector(12,23,34),FVector(2,1,3));
  M.BeforeTime=1;M.AfterTime=2;M.Center=(M.Before.GetLocation()+M.After.GetLocation())*.5;
  M.Radius=100;Snapshot S;S.Time=2;S.Meshes.Add(M);
  for(float Alpha:{0.f,.2f,.5f,.9f,1.f}) {
    FTransform T;T.Blend(M.Before,M.After,Alpha);
    const FVector Surface=T.TransformPosition(FVector(.217,.139,0));
    const FVector Normal=T.TransformVectorNoScale(FVector(0,0,1));
    FHitResult Hit;uint32 Section=0;FVector2f UV;
    const bool Found=S.Intersect(Surface+Normal*10,-Normal,100,Alpha,Hit,Section,UV,1+Alpha);
    TestTrue(TEXT("moving, rotating, scaled rigid surface is hit"),Found);
    if(Found) {
      TestTrue(TEXT("motion-interpolated range preserved"),FMath::Abs(Hit.Distance-10)<.0001);
      TestTrue(TEXT("material UV interpolation preserved"),FMath::Abs(UV.X-.217/4)<.0001 && FMath::Abs(UV.Y-.139/4)<.0001);
      TestEqual(TEXT("material section preserved"),Section,uint32(0));
    }
  }
  {
    auto Full=MakeShared<Snapshot,ESPMode::ThreadSafe>();*Full=S;
    Full->OldestCoveredTime=.875;Full->MissingGeometry=7;
    Mesh Far=M;Far.Primitive=456;Far.Before.AddToTranslation(FVector(1000,0,0));
    Far.After.AddToTranslation(FVector(1000,0,0));Far.Center+=FVector(1000,0,0);
    Full->Meshes.Add(Far);Full->Primitives.Add(M.Primitive);Full->Primitives.Add(Far.Primitive);
    const FSphere Query(M.Center,50);
    const auto Selected=WithinSphere(Full,Query);
    TestEqual(TEXT("query view excludes only the distant swept mesh"),Selected->Meshes.Num(),1);
    TestTrue(TEXT("query view shares immutable geometry"),Selected->Meshes[0].Geometry==M.Geometry);
    TestTrue(TEXT("query view preserves source history"),Full->Meshes.Num()==2 && Full->Primitives.Contains(456));
    TestEqual(TEXT("query view keeps full coverage guard"),Selected->OldestCoveredTime,Full->OldestCoveredTime);
    TestEqual(TEXT("query view keeps missing-geometry guard"),Selected->MissingGeometry,Full->MissingGeometry);
    TestEqual(TEXT("query view keeps episode clock"),Selected->Time,Full->Time);
    TestFalse(TEXT("excluded primitive is not marked as GPU motion"),Selected->Primitives.Contains(456));
    for(float Alpha:{0.f,.2f,.5f,.9f,1.f}) {
      FTransform T;T.Blend(M.Before,M.After,Alpha);
      const FVector Surface=T.TransformPosition(FVector(.217,.139,0));
      const FVector Normal=T.TransformVectorNoScale(FVector(0,0,1));
      FHitResult A,B;uint32 SectionA=0,SectionB=0;FVector2f UVA,UVB;
      const bool HitA=Full->Intersect(Surface+Normal*10,-Normal,50,Alpha,A,SectionA,UVA,1+Alpha);
      const bool HitB=Selected->Intersect(Surface+Normal*10,-Normal,50,Alpha,B,SectionB,UVB,1+Alpha);
      TestTrue(TEXT("bounded view preserves moving surface hit"),HitA && HitB);
      TestEqual(TEXT("bounded view preserves exact range"),A.Distance,B.Distance);
      TestTrue(TEXT("bounded view preserves material and UV"),SectionA==SectionB && UVA==UVB);
    }
    auto Boundary=MakeShared<Snapshot,ESPMode::ThreadSafe>();*Boundary=*Full;
    Boundary->Meshes[1].Center=Query.Center+FVector(Query.W+Far.Radius,0,0);
    TestTrue(TEXT("tangent swept sphere is retained without copying"),WithinSphere(Boundary,Query)==Boundary);
    Boundary->Meshes[1].Center=Query.Center+FVector(Query.W+Far.Radius+.5,0,0);
    TestTrue(TEXT("rounding padding retains just-outside boundary"),WithinSphere(Boundary,Query)==Boundary);
    Boundary->Meshes[1].Radius=-1;
    TestTrue(TEXT("invalid mesh bounds cannot prove exclusion"),WithinSphere(Boundary,Query)==Boundary);
    TestTrue(TEXT("invalid query bounds keep full snapshot"),WithinSphere(Full,FSphere(Query.Center,-1))==Full);
    TestTrue(TEXT("containing query reuses full snapshot"),WithinSphere(Full,FSphere(Query.Center,2000))==Full);
    const auto Empty=WithinSphere(Full,FSphere(Query.Center+FVector(10000,0,0),50));
    TestTrue(TEXT("empty query retains strict history metadata"),Empty->Meshes.IsEmpty() && Empty->MissingGeometry==7 && Empty->OldestCoveredTime==.875);
  }
  TArray<FVector4f> Info,Nodes,Triangles;TArray<FUintVector4> Lookup;
  S.GpuBuffers(FVector(100000,-200000,300000),Info,Nodes,Triangles,Lookup);
  TestTrue(TEXT("snapshot uses cached geometry buffers"),Nodes.Num()==First->PackedNodes.Num() &&
      FMemory::Memcmp(Nodes.GetData(),First->PackedNodes.GetData(),Nodes.Num()*sizeof(FVector4f))==0 &&
      FMemory::Memcmp(Triangles.GetData(),First->PackedTriangles.GetData(),Triangles.Num()*sizeof(FVector4f))==0);
  TestTrue(TEXT("snapshot keeps current transform"),Info[0].X==100010 && Info[1].Z==300034);

  // A metadata-only refresh must address the exact existing GPU geometry.
  S.Meshes.Add(M);S.Meshes[1].Primitive=456;S.Meshes[1].After.AddToTranslation(FVector(11,12,13));
  TArray<FVector4f> FullInfo,FullNodes,FullTriangles,RefreshInfo,NoNodes,NoTriangles;
  TArray<FUintVector4> FullLookup,RefreshLookup;
  S.GpuBuffers(FVector(123,456,789),FullInfo,FullNodes,FullTriangles,FullLookup);
  S.GpuBuffers(FVector(123,456,789),RefreshInfo,NoNodes,NoTriangles,RefreshLookup,false);
  TestTrue(TEXT("metadata refresh preserves offsets and current transforms"),FullInfo.Num()==RefreshInfo.Num() &&
      FMemory::Memcmp(FullInfo.GetData(),RefreshInfo.GetData(),FullInfo.Num()*sizeof(FVector4f))==0);
  TestTrue(TEXT("metadata refresh preserves primitive lookup"),FullLookup.Num()==RefreshLookup.Num() &&
      FMemory::Memcmp(FullLookup.GetData(),RefreshLookup.GetData(),FullLookup.Num()*sizeof(FUintVector4))==0);
  TestTrue(TEXT("metadata refresh does not repack geometry"),NoNodes.IsEmpty() && NoTriangles.IsEmpty());
  TestEqual(TEXT("instances share one packed node array"),FullNodes.Num(),Nodes.Num());
  TestEqual(TEXT("instances share one packed triangle array"),FullTriangles.Num(),Triangles.Num());
  TestTrue(TEXT("shared geometry retains distinct instance transforms"),FullInfo[1]!=FullInfo[9]);

  // A subset/permutation can reference a retained GPU atlas without copying it.
  Snapshot Subset;Subset.Time=S.Time;Subset.Meshes.Add(S.Meshes[1]);
  const TArray<FIntPoint> ExistingOffsets{FIntPoint(0,0)};
  TArray<FVector4f> SubsetInfo,SubsetNodes,SubsetTriangles;TArray<FUintVector4> SubsetLookup;
  Subset.GpuBuffers(FVector(123,456,789),SubsetInfo,SubsetNodes,SubsetTriangles,SubsetLookup,false,&ExistingOffsets);
  TestTrue(TEXT("subset references retained geometry with exact metadata"),SubsetInfo.Num()==8 &&
      FMemory::Memcmp(SubsetInfo.GetData(),FullInfo.GetData()+8,8*sizeof(FVector4f))==0);
  TestTrue(TEXT("subset does not copy geometry"),SubsetNodes.IsEmpty() && SubsetTriangles.IsEmpty());

  auto Deformed=MakeShared<Geometry>();Deformed->Positions=Shape->Positions;Deformed->Normals=Shape->Normals;
  Deformed->UVs=Shape->UVs;Deformed->Indices=Shape->Indices;Deformed->Sections=Shape->Sections;
  Deformed->Positions[6].Z=2;
  const auto Transition=GeometryFor(Shape,Deformed,false,Reused);
  TestFalse(TEXT("deforming transition cannot reuse rigid geometry"),Reused);
  TestFalse(TEXT("transition cannot poison destination stationary cache"),Deformed->Acceleration.IsValid());
  CheckPacked(*Transition,*Reference(*Shape,*Deformed,false));
  const auto Settled=GeometryFor(Deformed,Deformed,false,Reused);
  TestFalse(TEXT("new stationary pose builds its own geometry"),Reused);
  TestTrue(TEXT("new stationary pose reuses on next frame"),GeometryFor(Deformed,Deformed,false,Reused)==Settled && Reused);
  CheckPacked(*Settled,*Reference(*Deformed,*Deformed,false));
  CheckPacked(*First,*Reference(*Shape,*Shape,true));
  GeometryFor(Shape,Shape,false,Reused);TestFalse(TEXT("winding policy change invalidates cache"),Reused);
  auto Dense=MakeShared<Geometry>();
  for(int Y=0;Y<=128;++Y)for(int X=0;X<=128;++X) {
    Dense->Positions.Add(FVector3f(X,Y,FMath::Sin(X*.17f)*FMath::Cos(Y*.13f)*5));Dense->Normals.Add(FVector3f(0,0,1));Dense->UVs.Add(FVector2f(X/128.f,Y/128.f));
  }
  for(int Y=0;Y<128;++Y)for(int X=0;X<128;++X) {
    const uint32 A=Y*129+X;for(uint32 I:{A,A+1,A+129,A+1,A+130,A+129})Dense->Indices.Add(I);
    Dense->Sections.Add((X+Y)%3);Dense->Sections.Add((X+Y)%3);
  }
  auto DenseAfter=MakeShared<Geometry>();DenseAfter->Positions=Dense->Positions;DenseAfter->Normals=Dense->Normals;DenseAfter->UVs=Dense->UVs;DenseAfter->Indices=Dense->Indices;DenseAfter->Sections=Dense->Sections;
  for(int I=0;I<DenseAfter->Positions.Num();++I)DenseAfter->Positions[I].Z+=FMath::Sin(I*.071f)*2;
  double OriginalSeconds=0,OptimizedSeconds=0;
  for(int I=0;I<5;++I) {
    double Start=FPlatformTime::Seconds();const auto Expected=Reference(*Dense,*DenseAfter,true);OriginalSeconds+=FPlatformTime::Seconds()-Start;
    Start=FPlatformTime::Seconds();const auto Actual=BuildGeometry(*Dense,*DenseAfter,true);OptimizedSeconds+=FPlatformTime::Seconds()-Start;
    CheckPacked(*Actual,*Expected);
  }
  AddInfo(FString::Printf(TEXT("32768 deforming triangles, mean original geometry/BVH %.3f ms; optimized geometry/BVH including packing %.3f ms"),OriginalSeconds*200,OptimizedSeconds*200));
  TArray<GeometryBuild> ParallelJobs;
  for(int I=0;I<8;++I)ParallelJobs.Add({Dense,DenseAfter,I%2==0,{}});
  BuildGeometryJobs(ParallelJobs);
  for(int I=0;I<ParallelJobs.Num();++I)CheckPacked(*ParallelJobs[I].Result,*Reference(*Dense,*DenseAfter,I%2==0));
  TestFalse(TEXT("parallel deforming jobs do not publish a stationary cache"),DenseAfter->Acceleration.IsValid());

  GeometryFor(DenseAfter,DenseAfter,true,Reused);
  const double CacheStart=FPlatformTime::Seconds();
  for(int I=0;I<1000;++I)GeometryFor(DenseAfter,DenseAfter,true,Reused);
  AddInfo(FString::Printf(TEXT("32768 unchanged triangles, mean cached geometry lookup %.6f ms"),(FPlatformTime::Seconds()-CacheStart)));
  return true;
}
#endif
}
