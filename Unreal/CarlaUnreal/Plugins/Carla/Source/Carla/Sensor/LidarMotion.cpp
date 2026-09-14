// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/LidarMotion.h"
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

namespace CarlaLidarMotion {
namespace {
struct Geometry {
  TArray<FVector3f> Positions,Normals;
  TArray<FVector2f> UVs;
  TArray<uint32> Indices,Sections;
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
TSharedPtr<Snapshot,ESPMode::ThreadSafe> Cached;

bool ReadGeometry(UPrimitiveComponent* C,TSharedPtr<Geometry>& Out,TWeakObjectPtr<UObject>& Asset,uint32& PoseHash,const History* Old) {
  Out=MakeShared<Geometry>();
  if (auto* Static=Cast<UStaticMeshComponent>(C)) {
    UStaticMesh* Mesh=Static->GetStaticMesh();Asset=Mesh;
    if (Old && Old->Asset==Asset && Old->Shape) { Out=Old->Shape;return true; }
    if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty()) return false;
    const auto& L=Mesh->GetRenderData()->LODResources[0];const auto Index=L.IndexBuffer.GetArrayView();
    const uint32 Count=L.VertexBuffers.PositionVertexBuffer.GetNumVertices();
    if (!Count || !Index.Num()) return false;
    Out->Positions.Reserve(Count);Out->Normals.Reserve(Count);Out->UVs.Reserve(Count);
    for (uint32 I=0;I<Count;++I) {
      Out->Positions.Add(L.VertexBuffers.PositionVertexBuffer.VertexPosition(I));
      Out->Normals.Add(L.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(I));
      Out->UVs.Add(L.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()?L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,0):FVector2f::ZeroVector);
    }
    Out->Indices.Reserve(Index.Num());for(int I=0;I<Index.Num();++I)Out->Indices.Add(Index[I]);Out->Sections.SetNumZeroed(Index.Num()/3);
    for (int S=0;S<L.Sections.Num();++S) for(uint32 I=0;I<L.Sections[S].NumTriangles;++I)
      Out->Sections[L.Sections[S].FirstIndex/3+I]=S;
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
    if (Old && Old->Asset==Asset && Old->PoseHash==PoseHash && Old->Shape) { Out=Old->Shape;return true; }
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
    return true;
  }
  return false;
}
uint32 Build(Mesh& M,int First,int Count,int Depth=0) {
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
  const uint32 Left=Build(M,First,Middle-First,Depth+1),Right=Build(M,Middle,First+Count-Middle,Depth+1);
  M.Nodes[Index].Left=Left;M.Nodes[Index].Right=Right;return Index;
}
float Bits(uint32 U) { float F;FMemory::Memcpy(&F,&U,4);return F; }
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
  if(CachedFrame==GFrameCounter)return Cached;
  CachedFrame=GFrameCounter;Cached=MakeShared<Snapshot,ESPMode::ThreadSafe>();TSet<uint32> Seen;
  Cached->Time=World->GetTimeSeconds();Cached->OldestCoveredTime=0;
  struct PendingMesh { UPrimitiveComponent* Component; History Before,After; bool Compatible=false; };
  TArray<PendingMesh> Pending;TSet<AActor*> MovingActors;
  for(auto* C:Components) {
    if(!C || C->Mobility!=EComponentMobility::Movable || !C->IsRegistered() ||
        !C->IsVisible() || C->bHiddenInGame || C->GetOwner()->IsHidden() || C->ComponentHasTag(TEXT("LidarIgnore")))continue;
    if(!C->IsA<UStaticMeshComponent>() && !C->IsA<USkinnedMeshComponent>())continue;
    const uint32 Id=C->GetPrimitiveSceneId().PrimIDValue;if(!Id)continue;
    Seen.Add(Id);History* Old=Histories.Find(Id);const FTransform Current=C->GetComponentTransform();
    if(Old && Old->Component!=C) { Histories.Remove(Id);Old=nullptr; }
    TSharedPtr<Geometry> Shape;TWeakObjectPtr<UObject> Asset;uint32 PoseHash=0;
    const bool Available=ReadGeometry(C,Shape,Asset,PoseHash,Old);
    const bool Moved=Old && (!Old->Transform.Equals(Current,1.e-8) || Old->Shape!=Shape);
    const bool Compatible=Available && Old && Old->Shape && Old->Asset==Asset &&
        Old->Shape->Positions.Num()==Shape->Positions.Num() && Old->Shape->Indices==Shape->Indices;
    if(Moved) { MovingActors.Add(C->GetOwner());if(!Compatible)++Cached->MissingGeometry; }
    const History After{C,Asset,Current,Shape,PoseHash,Cached->Time};
    Pending.Add({C,Old?*Old:After,After,Compatible});Histories.Add(Id,After);
  }
  // Include stationary sibling meshes when a wheel or skinned part moves. This
  // permits removing a shared collision proxy without losing the rest of a car.
  for(const auto& Entry:Pending) {
    auto* C=Entry.Component;
    if(!Entry.Compatible || !MovingActors.Contains(C->GetOwner()))continue;
    const auto& Shape=Entry.After.Shape;const auto& Current=Entry.After.Transform;
    const uint32 Id=C->GetPrimitiveSceneId().PrimIDValue;
      Mesh M;M.Component=C;M.Primitive=Id;M.Before=Entry.Before.Transform;M.After=Current;M.BeforeTime=Entry.Before.Time;M.AfterTime=Entry.After.Time;
      Cached->OldestCoveredTime=FMath::Max(Cached->OldestCoveredTime,M.BeforeTime);
      M.Center=(M.Before.GetLocation()+M.After.GetLocation())*.5;
      float LocalRadius=0;
      for(int I=0;I<Shape->Indices.Num()/3;++I) {
        Triangle T;T.Section=Shape->Sections[I];T.Face=I;
        for(int V=0;V<3;++V) { const uint32 Vertex=Shape->Indices[I*3+V];T.Before[V]=Entry.Before.Shape->Positions[Vertex];T.After[V]=Shape->Positions[Vertex];T.UV[V]=Shape->UVs[Vertex];LocalRadius=FMath::Max(LocalRadius,FMath::Max(T.Before[V].Size(),T.After[V].Size())); }
        // Winding orientation from the undeformed render mesh tangents.
        if(auto* S=Cast<UStaticMeshComponent>(C)) {
          if(FVector3f::DotProduct(FVector3f::CrossProduct(T.After[1]-T.After[0],T.After[2]-T.After[0]),Shape->Normals[Shape->Indices[I*3]])<0)T.NormalSign=-1;
        }
        M.Triangles.Add(T);
      }
      M.Radius=LocalRadius*FMath::Max(M.Before.GetScale3D().GetAbsMax(),M.After.GetScale3D().GetAbsMax())+
          FVector::Distance(M.Before.GetLocation(),M.After.GetLocation())*.5+1;
      if(!M.Triangles.IsEmpty()) { Build(M,0,M.Triangles.Num());Cached->Primitives.Add(Id);Cached->Meshes.Add(MoveTemp(M)); }
  }
  for(auto It=Histories.CreateIterator();It;++It) if(!Seen.Contains(It.Key()) || !It.Value().Component.IsValid())It.RemoveCurrent();
  return Cached;
}

void Snapshot::GpuBuffers(const FVector& Translation,TArray<FVector4f>& Info,TArray<FVector4f>& NodeData,TArray<FVector4f>& TriangleData,TArray<FUintVector4>& Table) const {
  const uint32 Size=FMath::RoundUpToPowerOfTwo(FMath::Max(2,Meshes.Num()*2));Table.SetNumZeroed(Size);
  for(int I=0;I<Meshes.Num();++I) {
    const auto& M=Meshes[I];const uint32 NodeBase=NodeData.Num()/2,TriBase=TriangleData.Num()/7;
    auto Q0=M.Before.GetRotation(),Q1=M.After.GetRotation();if((Q0|Q1)<0)Q1=Q1*-1;
    Info.Add(FVector4f(FVector3f(M.Before.GetLocation()+Translation),Bits(NodeBase)));
    Info.Add(FVector4f(FVector3f(M.After.GetLocation()+Translation),Bits(M.Nodes.Num())));
    Info.Add(FVector4f(Q0.X,Q0.Y,Q0.Z,Q0.W));Info.Add(FVector4f(Q1.X,Q1.Y,Q1.Z,Q1.W));
    Info.Add(FVector4f(FVector3f(M.Before.GetScale3D()),Bits(TriBase)));
    Info.Add(FVector4f(FVector3f(M.After.GetScale3D()),Bits(M.Triangles.Num())));
    Info.Add(FVector4f(FVector3f(M.Center+Translation),M.Radius));
    Info.Add(FVector4f(Bits(M.Primitive),M.BeforeTime-Time,M.AfterTime-Time,0));
    for(const auto& N:M.Nodes) { NodeData.Add(FVector4f(N.Min,Bits(N.Left)));NodeData.Add(FVector4f(N.Max,Bits(N.Right))); }
    for(const auto& T:M.Triangles) {
      TriangleData.Add(FVector4f(T.Before[0],T.UV[0].X));TriangleData.Add(FVector4f(T.Before[1],T.UV[0].Y));TriangleData.Add(FVector4f(T.Before[2],T.UV[1].X));
      TriangleData.Add(FVector4f(T.After[0],T.UV[1].Y));TriangleData.Add(FVector4f(T.After[1],T.UV[2].X));TriangleData.Add(FVector4f(T.After[2],T.UV[2].Y));
      TriangleData.Add(FVector4f(Bits(T.Section),Bits(T.Face),T.NormalSign,0));
    }
    uint32 Slot=(M.Primitive*2654435761u)&(Size-1);while(Table[Slot].X)Slot=(Slot+1)&(Size-1);Table[Slot]=FUintVector4(M.Primitive,I+1,0,0);
  }
  if(Info.IsEmpty())Info.SetNumZeroed(8);if(NodeData.IsEmpty())NodeData.SetNumZeroed(2);if(TriangleData.IsEmpty())TriangleData.SetNumZeroed(7);
}

bool Snapshot::Intersect(const FVector& Origin,const FVector& Direction,float Range,float TimeFraction,FHitResult& Hit,uint32& Section,FVector2f& UV,double SceneTime) const {
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
      const auto& N=M.Nodes[Stack[--Count]];if(!Box(O,D,N.Min,N.Max,Nearest))continue;
      if(!(N.Left&0x80000000u)) { Stack[Count++]=N.Left;Stack[Count++]=N.Right;continue; }
      for(uint32 I=0;I<N.Right;++I) {
        const auto& T=M.Triangles[(N.Left&0x7fffffffu)+I];
        const FVector A(FMath::Lerp(T.Before[0],T.After[0],Alpha)),B(FMath::Lerp(T.Before[1],T.After[1],Alpha)),V(FMath::Lerp(T.Before[2],T.After[2],Alpha));
        const FVector E1=B-A,E2=V-A,P=FVector::CrossProduct(D,E2);const double Det=FVector::DotProduct(E1,P);if(FMath::Abs(Det)<1.e-12)continue;
        const FVector X=O-A;const double U=FVector::DotProduct(X,P)/Det;if(U<0 || U>1)continue;
        const FVector Q=FVector::CrossProduct(X,E1);const double W=FVector::DotProduct(D,Q)/Det;if(W<0 || W+U>1)continue;
        const double Distance=FVector::DotProduct(E2,Q)/Det;if(Distance<.001 || Distance>=Nearest)continue;
        const FVector Normal=FVector::CrossProduct(Transform.TransformVector(E1),Transform.TransformVector(E2)).GetSafeNormal()*T.NormalSign;
        Hit=FHitResult(C->GetOwner(),C,Origin+Direction*Distance,Normal);Hit.bBlockingHit=true;Hit.Distance=Distance;Hit.FaceIndex=T.Face;
        Section=T.Section;UV=T.UV[0]*(1-U-W)+T.UV[1]*U+T.UV[2]*W;Nearest=Distance;Found=true;
      }
    }
  }return Found;
}
}
