// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/LidarOptics.h"
#include "Carla/Sensor/LidarMotion.h"
#include "Carla/Sensor/LidarInfrared.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
namespace CarlaLidarOptics {
Material Resolve(UPrimitiveComponent* C, UMaterialInterface* M) {
  Material P;
  // Match material names, never the actor name (a vehicle may have glass,
  // rubber, paint and mirror surfaces on separate sections).
  FString Name=M ? M->GetName().ToLower() : FString();
  FString Override;
  bool RoadLine=false;
  if (C) for (const auto& Tag:C->ComponentTags) {
    const FString S=Tag.ToString();
    RoadLine |= S==TEXT("RoadLine") || S==TEXT("RoadLines");
    if (S.Split(TEXT("LidarMaterial="),nullptr,&Override)) { Name=Override.ToLower(); break; }
  }
  if (RoadLine && Override.IsEmpty()) { P.diffuse=.45f; P.retro=.45f; }
  else if (Name.Contains(TEXT("mirror"))) { P.diffuse=0; P.specular=.95f; }
  else if (Name.Contains(TEXT("glass")) || Name.Contains(TEXT("window"))) {
    P.diffuse=.002f; P.transmission=.96f;
  }
  else if (Name.Contains(TEXT("roadline")) || Name.Contains(TEXT("road_line")) ||
           Name.Contains(TEXT("marking")) || Name.Contains(TEXT("lane_mark"))) {
    P.diffuse=.45f; P.retro=.45f;
  }
  else if (Name.Contains(TEXT("sign")) && !Name.Contains(TEXT("signal")) && !Name.Contains(TEXT("design")) && !Name.Contains(TEXT("post")) && !Name.Contains(TEXT("back"))) {
    P.diffuse=.25f; P.retro=.7f;
  }
  else if (Name.Contains(TEXT("asphalt"))) P.diffuse=.1f;
  else if (Name.Contains(TEXT("concrete")) || Name.Contains(TEXT("cement"))) P.diffuse=.3f;
  else if (Name.Contains(TEXT("rubber")) || Name.Contains(TEXT("tire"))) P.diffuse=.08f;
  if (C && C->ComponentHasTag(TEXT("LidarSolid"))) P.thinSheet=0.f;
  // Explicit scalar parameters override heuristic presets, and can be authored
  // on material instances independently of visible-light base color.
  if (M) {
    const TCHAR* Names[]={TEXT("LidarDiffuse"),TEXT("LidarRetro"),TEXT("LidarSpecular"),TEXT("LidarTransmission"),TEXT("LidarIOR"),TEXT("LidarRoughness"),TEXT("LidarThinSheet")};
    float* Values[]={&P.diffuse,&P.retro,&P.specular,&P.transmission,&P.ior,&P.roughness,&P.thinSheet};
    for (int I=0;I<7;++I) { float V; if (M->GetScalarParameterValue(FMaterialParameterInfo(Names[I]),V) && FMath::IsFinite(V)) *Values[I]=FMath::Clamp(V,I==4?1.f:0.f,I==4?3.f:1.f); }
  }
  P.infrared=CarlaLidarInfrared::Find(C,M);
  return P;
}
TArray<Material> Sections(UPrimitiveComponent* C) {
  TArray<Material> Out;
  if (auto* S=Cast<UStaticMeshComponent>(C)) {
    if (S->GetStaticMesh() && S->GetStaticMesh()->GetRenderData() && S->GetStaticMesh()->GetRenderData()->LODResources.Num())
      for (const auto& Section:S->GetStaticMesh()->GetRenderData()->LODResources[0].Sections)
        Out.Add(Resolve(C,C->GetMaterial(Section.MaterialIndex)));
  } else if (auto* S=Cast<USkinnedMeshComponent>(C)) {
    if (auto* Mesh=Cast<USkeletalMesh>(S->GetSkinnedAsset()))
      if (auto* Data=Mesh->GetResourceForRendering())
        if (Data->LODRenderData.Num()) for (const auto& Section:Data->LODRenderData[0].RenderSections)
          Out.Add(Resolve(C,C->GetMaterial(Section.MaterialIndex)));
  }
  if (Out.IsEmpty()) Out.Add(Resolve(C,C->GetMaterial(0)));
  return Out;
}
struct OpticalComponent { TWeakObjectPtr<UStaticMeshComponent> Component; FBox Bounds; };
TArray<OpticalComponent> NonSolidOptics;
TWeakObjectPtr<UWorld> OpticalWorld;
uint64 OpticalFrame=MAX_uint64;
void Prepare(UWorld* World) {
  check(IsInGameThread());
  if (OpticalWorld==World && OpticalFrame==GFrameCounter) return;
  OpticalWorld=World;OpticalFrame=GFrameCounter;NonSolidOptics.Reset();
  for (TActorIterator<AActor> A(World);A;++A) {
    if (!A->GetActorEnableCollision() || A->IsHidden()) continue;
    TInlineComponentArray<UStaticMeshComponent*> Components;A->GetComponents(Components);
    for (auto* C:Components) {
      if (!C->IsRegistered() || !C->IsVisible() || C->bHiddenInGame || C->ComponentHasTag(TEXT("LidarIgnore"))) continue;
      if (C->IsQueryCollisionEnabled() && C->GetCollisionResponseToChannel(ECC_GameTraceChannel2)==ECR_Block) continue;
      const auto Materials=Sections(C);
      if (Materials.ContainsByPredicate([](const Material& M) { return M.transmission>0 || M.specular>.5f || M.retro>0; }))
        NonSolidOptics.Add({C,C->Bounds.GetBox()});
    }
  }
}
// Chaos may discard backfaces even when the ray starts inside a glass solid.
// Test the current static glass mesh two-sided to find its exit interface.
// This narrowly scoped query avoids changing collision flags on shared assets.
bool GlassExit(UStaticMeshComponent* C,const FVector& Origin,const FVector& Direction,float Range,FHitResult& Hit,bool ExitOnly=true,UMaterialInterface** OutMaterial=nullptr) {
  if (!C || !C->GetStaticMesh() || !C->GetStaticMesh()->GetRenderData() || C->GetStaticMesh()->GetRenderData()->LODResources.IsEmpty()) return false;
  const auto& LOD=C->GetStaticMesh()->GetRenderData()->LODResources[0];
  const auto Indices=LOD.IndexBuffer.GetArrayView();
  if (!Indices.Num() || !LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices()) return false;
  const FTransform Transform=C->GetComponentTransform();
  const FVector O=Transform.InverseTransformPosition(Origin),D=Transform.InverseTransformVector(Direction);
  bool Found=false;float Nearest=Range;
  for (int32 I=0;I+2<Indices.Num();I+=3) {
    const FVector A(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I]));
    const FVector B(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I+1]));
    const FVector V(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I+2]));
    const FVector E1=B-A,E2=V-A,P=FVector::CrossProduct(D,E2);
    const double Det=FVector::DotProduct(E1,P);if (FMath::Abs(Det)<1.e-10) continue;
    const FVector T=O-A;const double U=FVector::DotProduct(T,P)/Det;if (U<0 || U>1) continue;
    const FVector Q=FVector::CrossProduct(T,E1);const double W=FVector::DotProduct(D,Q)/Det;if (W<0 || U+W>1) continue;
    const double Distance=FVector::DotProduct(E2,Q)/Det;if (Distance<=.001 || Distance>=Nearest) continue;
    FVector N=FVector::CrossProduct(Transform.TransformVector(E1),Transform.TransformVector(E2)).GetSafeNormal();
    if (ExitOnly) { if (FVector::DotProduct(N,Direction)<0) N=-N; }
    else {
      const FVector LocalNormal(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(Indices[I]));
      if (FVector::DotProduct(FVector::CrossProduct(E1,E2),LocalNormal)<0) N=-N;
    }
    Hit=FHitResult(C->GetOwner(),C,Origin+Direction*Distance,N);Hit.bBlockingHit=true;Hit.Distance=Distance;Hit.FaceIndex=I/3;
    if (OutMaterial) {
      *OutMaterial=nullptr;
      for (const auto& Section:LOD.Sections)
        if (uint32(I)>=Section.FirstIndex && uint32(I)<Section.FirstIndex+Section.NumTriangles*3) {
          *OutMaterial=C->GetMaterial(Section.MaterialIndex);break;
        }
    }
    Nearest=Distance;Found=true;
  }
  return Found;
}
FCarlaLidarHit Trace(AActor& Sensor,const FVector& Origin,const FVector& Direction,
    float Range,float AtmosphericAttenuation,const FCollisionQueryParams& Params,FCarlaLidarTrace* AllEchoes,const CarlaLidarMotion::Snapshot* Motion,float TimeFraction,float WavelengthNm,float Wetness,double SceneTime) {
  if (IsInGameThread()) Prepare(Sensor.GetWorld());
  if(AllEchoes) { AllEchoes->Echoes.Reset();AllEchoes->UnoccludedRange=Range; }
  struct Path { FVector origin, direction; float length, weight, ior; int depth; UStaticMeshComponent* glass; };
  Path Stack[8]; int Count=1;
  Stack[0]={Origin,Direction,0,1,1,0,nullptr};
  FCarlaLidarHit Best; float BestScore=1.e-6f;
  while (Count) {
    const Path P=Stack[--Count];
    if (P.length>=Range) continue;
    FHitResult H;
    const float Remaining=(Range-P.length)/P.ior;
    bool Found=Sensor.GetWorld()->ParallelLineTraceSingleByChannel(H,P.origin,P.origin+P.direction*Remaining,ECC_GameTraceChannel2,Params,FCollisionResponseParams::DefaultResponseParam);
    FHitResult Exit;
    UMaterialInterface* SurfaceMaterial=nullptr;
    if (P.glass && (!Motion || !Motion->Primitives.Contains(P.glass->GetPrimitiveSceneId().PrimIDValue)) && GlassExit(P.glass,P.origin,P.direction,Found?H.Distance:Remaining,Exit,true,&SurfaceMaterial)) { H=Exit;Found=true; }
    for (const auto& Item:NonSolidOptics) {
      auto* Component=Item.Component.Get();
      if (!Component || Component->GetOwner()==Sensor.GetAttachParentActor() || Component->GetOwner()==&Sensor) continue;
      if(Motion && Motion->Primitives.Contains(Component->GetPrimitiveSceneId().PrimIDValue))continue;
      const float Limit=Found?H.Distance:Remaining;
      if (!FMath::LineBoxIntersection(Item.Bounds,P.origin,P.origin+P.direction*Limit,P.direction*Limit)) continue;
      FHitResult OpticalHit;UMaterialInterface* OpticalMaterial=nullptr;
      if (GlassExit(Component,P.origin,P.direction,Limit,OpticalHit,false,&OpticalMaterial)) { H=OpticalHit;SurfaceMaterial=OpticalMaterial;Found=true; }
    }
    TArray<Material> MotionMaterials;uint32 MotionSection=0;bool MotionHit=false;FVector2f MotionUV;
    if(Motion) {
      FHitResult Moving;FVector2f UV;
      if(Motion->Intersect(P.origin,P.direction,Found?H.Distance:Remaining,TimeFraction,Moving,MotionSection,UV,SceneTime)) {
        H=Moving;Found=true;SurfaceMaterial=nullptr;MotionHit=true;MotionUV=UV;
        MotionMaterials=Sections(H.GetComponent());
      }
    }
    if (!Found) continue;
    const float Length=P.length+H.Distance*P.ior;
    auto* C=H.GetComponent(); int32 Section=0;
    auto* M=SurfaceMaterial ? SurfaceMaterial : (C ? C->GetMaterialFromCollisionFaceIndex(H.FaceIndex,Section) : nullptr);
    Material Mat=MotionHit && MotionMaterials.IsValidIndex(MotionSection)?MotionMaterials[MotionSection]:Resolve(C,M);
    if(WavelengthNm>0)Mat=CarlaLidarInfrared::Sample(Mat,H,WavelengthNm,Wetness,MotionHit?&MotionUV:nullptr);
    FVector N=H.ImpactNormal.GetSafeNormal();
    const bool Front=FVector::DotProduct(P.direction,N)<0;
    if (!Front) N=-N;
    const float Cos=FMath::Clamp(float(-FVector::DotProduct(P.direction,N)),0.f,1.f);
    const bool Thin=Mat.transmission>0 && Mat.thinSheet>=.5f;
    const float NextIOR=(Thin || Front)?Mat.ior:1.f;
    const float F=fresnel(Cos,P.ior,NextIOR);
    const float Strength=P.weight*surface(Mat,Cos,F);
    const float Score=Strength*FMath::Exp(-AtmosphericAttenuation*Length*.01f);
    if(AllEchoes) {
      if(Strength>1.e-10f) {
        FCarlaLidarHit Echo{H,Strength,uint32(P.depth)};
        Echo.Hit.Distance=Length;Echo.Hit.ImpactPoint=Echo.Hit.Location=Origin+Direction*Length;
        AllEchoes->Echoes.Add(Echo);
      }
      if(Mat.transmission<=0 && FVector::DotProduct(P.direction,Direction)>.99999)
        AllEchoes->UnoccludedRange=FMath::Min(AllEchoes->UnoccludedRange,Length);
    }
    if (Score>BestScore) {
      BestScore=Score; Best.Hit=H; Best.SurfaceReturn=Strength;
      Best.Hit.Distance=Length; Best.Hit.TraceStart=Origin;
      Best.Hit.Location=Best.Hit.ImpactPoint=Origin+Direction*Length;
    }
    if (P.depth>=3 || Length>=Range) continue;
    auto Push=[&](FVector D,float W,float IOR,UStaticMeshComponent* Glass) {
      // Square one-way transmission/reflection for the reciprocal return path.
      W=P.weight*W*W;
      if (W>1.e-6f && Count<8) Stack[Count++]={H.ImpactPoint+D*.05f,D,Length+.05f*IOR,W,IOR,P.depth+1,Glass};
    };
    const float R=Mat.transmission>0?F:Mat.specular;
    if (R>0) Push(P.direction+2*Cos*N,R,P.ior,P.glass);
    if (Thin && F<1) {
      // A zero-thickness pane has two interfaces and exits back into the
      // current medium. Do not leave the entire space behind it inside glass.
      const float T=Mat.transmission*(1-F);
      Push(P.direction,T*T,P.ior,P.glass);
    } else if (Mat.transmission>0 && F<1) {
      const float Eta=P.ior/NextIOR;
      const float K=1-Eta*Eta*(1-Cos*Cos);
      if (K>=0) Push((Eta*P.direction+(Eta*Cos-FMath::Sqrt(K))*N).GetSafeNormal(),Mat.transmission*(1-F),NextIOR,NextIOR>1.f?Cast<UStaticMeshComponent>(C):nullptr);
    }
  }
  return Best;
}
}
