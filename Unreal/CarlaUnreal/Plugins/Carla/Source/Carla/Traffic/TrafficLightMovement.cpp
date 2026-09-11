#include "TrafficLightComponent.h"
#include "TrafficLightBase.h"
#include "Carla/Util/BoundingBoxCalculator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

void UTrafficLightComponent::SetMovementStates(uint16 States)
{
  if (MovementStates == States) return;
  MovementStates = States;
  RebuildMovementDisplay();
}

void UTrafficLightComponent::RebuildMovementDisplay()
{
  const bool Enabled = (MovementStates & 0x8000) != 0;
  if (!Enabled)
  {
    if (MovementMesh) MovementMesh->SetVisibility(false);
    for (auto* Mesh : MovementOriginalMeshes) if (IsValid(Mesh)) Mesh->SetVisibility(true);
    return;
  }
  auto* Light = Cast<ATrafficLightBase>(GetOwner());
  if (!Light) return;
  if (!MovementMesh)
  {
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carla/Static/TrafficSignal/M_MovementSignal.M_MovementSignal"));
    if (!Material) { UE_LOG(LogTemp, Error, TEXT("Movement signal material is missing")); return; }
    TArray<FBoundingBox> Boxes;
    TArray<uint8> Tags;
    UBoundingBoxCalculator::GetTrafficLightBoundingBox(Light, Boxes, Tags, 7);
    for (const auto& Box : Boxes)
    {
      TArray<UStaticMeshComponent*> Meshes;
      UBoundingBoxCalculator::GetMeshCompsFromActorBoundingBox(Light, Box, Meshes);
      // CARLA's semantic boxes also include pedestrian heads and push buttons.
      // Only replace the vehicle lamp stacks, retaining pedestrian hardware.
      int32 Count=0; FVector Center=FVector::ZeroVector;
      for (auto* Mesh : Meshes) if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName().Contains(TEXT("TrafficLight_Signal")))
      {
        MovementOriginalMeshes.AddUnique(Mesh); Center+=Mesh->GetComponentLocation(); ++Count;
      }
      if (Count) MovementHeadTransforms.Add(FTransform(Box.Rotation, Center/Count));
    }
    MovementMesh = NewObject<UProceduralMeshComponent>(Light, TEXT("MovementSignalHeads"));
    MovementMesh->SetupAttachment(Light->GetRootComponent());
    MovementMesh->SetMobility(EComponentMobility::Movable);
    MovementMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MovementMesh->SetCanEverAffectNavigation(false);
    MovementMesh->SetCastShadow(false);
    MovementMesh->SetRenderCustomDepth(true);
    MovementMesh->SetCustomDepthStencilValue(7);
    MovementMesh->RegisterComponent();
    Light->AddInstanceComponent(MovementMesh);
    MovementMesh->SetMaterial(0, Material);
  }
  for (auto* Mesh : MovementOriginalMeshes) if (IsValid(Mesh)) Mesh->SetVisibility(false);
  MovementMesh->SetVisibility(true);
  TArray<FVector> Vertices, Normals;
  TArray<int32> Triangles;
  TArray<FVector2D> UV;
  TArray<FLinearColor> Colors;
  TArray<FProcMeshTangent> Tangents;
  const FTransform WorldToActor = Light->GetActorTransform().Inverse();
  for (const auto& Head : MovementHeadTransforms)
  {
    auto Triangle = [&](FVector A, FVector B, FVector C, FLinearColor Color)
    {
      const int32 Start = Vertices.Num();
      for (const auto& V : {A,B,C})
      {
        Vertices.Add(WorldToActor.TransformPosition(Head.TransformPosition(V)));
        Normals.Add(FVector(0,1,0)); UV.Add(FVector2D::ZeroVector); Colors.Add(Color);
      }
      Triangles.Append({Start,Start+1,Start+2});
    };
    auto Quad = [&](float X1, float Z1, float X2, float Z2, float Y, FLinearColor Color)
    {
      Triangle(FVector(X1,Y,Z1),FVector(X2,Y,Z1),FVector(X2,Y,Z2),Color);
      Triangle(FVector(X1,Y,Z1),FVector(X2,Y,Z2),FVector(X1,Y,Z2),Color);
    };
    const int32 BoardStart=Triangles.Num();
    Quad(-58,-38,58,38,22,FLinearColor(.004f,.004f,.004f,1));
    // Black housing is visible from behind; arrow faces only face approaching traffic.
    for (int32 I=BoardStart; I<BoardStart+6; I+=3)
      Triangles.Append({Triangles[I],Triangles[I+2],Triangles[I+1]});
    for (int32 Movement=0; Movement<3; ++Movement)
    {
      const int32 Indication = (MovementStates >> (3*Movement)) & 7;
      FLinearColor Color(.025f,.025f,.025f,1);
      if (Indication==0) Color=FLinearColor(4,0.015f,0.005f,1);
      if (Indication==1 || (Indication==3 && (MovementStates & 0x4000))) Color=FLinearColor(4,1.4f,0,1);
      if (Indication==2) Color=FLinearColor(.005f,4,.07f,1);
      const float X = (Movement-1)*37.f;
      if (Movement==1)
      {
        Quad(X-3,-20,X+3,7,24,Color);
        Triangle(FVector(X-12,24,5),FVector(X+12,24,5),FVector(X,24,24),Color);
      }
      else
      {
        const float D = Movement==0 ? -1.f : 1.f;
        Quad(X-3,-20,X+3,5,24,Color);
        Quad(FMath::Min(X,X+D*12),2,FMath::Max(X,X+D*12),8,24,Color);
        Triangle(FVector(X+D*8,24,-5),FVector(X+D*8,24,15),FVector(X+D*20,24,5),Color);
      }
    }
  }
  MovementMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV, Colors, Tangents, false);
}
