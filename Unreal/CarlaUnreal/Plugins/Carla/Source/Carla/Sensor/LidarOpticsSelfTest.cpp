// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Carla/Sensor/LidarOptics.h"
#include "Carla.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "TimerManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
namespace CarlaLidarOpticsTest {
void Run(UWorld* World) {
  if (!World || !CarlaGpuSensors::IsEnabled()) return;
  UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
  AActor* Owner=World->SpawnActor<AActor>();
  const FVector Base(0,0,100000);
  auto Spawn=[&](FVector Position,FVector Scale,const TCHAR* Name,float Yaw=0) {
    auto* A=World->SpawnActor<AStaticMeshActor>();auto* C=A->GetStaticMeshComponent();
    C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);
    C->SetCollisionProfileName(TEXT("Custom"));C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    C->SetCollisionResponseToAllChannels(ECR_Ignore);C->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);
    if (FString(Name)==TEXT("Glass")) C->ComponentTags.Add(TEXT("LidarSolid"));
    C->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Carla/Testing/Optics/M_CarlaOptics_%s.M_CarlaOptics_%s"),Name,Name)));
    A->SetActorLocation(Base+Position);A->SetActorScale3D(Scale);A->SetActorRotation(FRotator(0,Yaw,0));
    return A;
  };
  Spawn(FVector(1000,0,0),FVector(1,4,4),TEXT("Concrete"));
  Spawn(FVector(1000,600,0),FVector(1,4,4),TEXT("RoadMarking"));
  Spawn(FVector(1000,1200,0),FVector(1,4,4),TEXT("Sign"));
  Spawn(FVector(500,1800,0),FVector(.2,4,4),TEXT("Glass"));
  Spawn(FVector(1000,1800,0),FVector(1,4,4),TEXT("Concrete"));
  Spawn(FVector(1000,2600,0),FVector(.05,4,4),TEXT("Mirror"),45);
  Spawn(FVector(1000,1600,0),FVector(4,.05,4),TEXT("Concrete"));
  Spawn(FVector(1000,3400,0),FVector(.05,4,4),TEXT("Mirror"),45);
  Spawn(FVector(500,4300,0),FVector(.2,4,4),TEXT("Glass"),30);
  Spawn(FVector(1000,4300,0),FVector(1,4,4),TEXT("Concrete"));
  Spawn(FVector(1000,5000,0),FVector(.05,4,4),TEXT("Mirror"));
  auto* NonSolid=Spawn(FVector(500,11000,0),FVector(.2,4,4),TEXT("Glass"));
  NonSolid->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Spawn(FVector(1000,11000,0),FVector(1,4,4),TEXT("Concrete"));
  auto* DisabledGlass=Spawn(FVector(500,12000,0),FVector(.2,4,4),TEXT("Glass"));
  DisabledGlass->SetActorEnableCollision(false);
  Spawn(FVector(1000,12000,0),FVector(1,4,4),TEXT("Concrete"));
  auto* HiddenGlass=Spawn(FVector(500,13000,0),FVector(.2,4,4),TEXT("Glass"));
  HiddenGlass->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  HiddenGlass->SetActorHiddenInGame(true);
  Spawn(FVector(1000,13000,0),FVector(1,4,4),TEXT("Concrete"));
  UStaticMesh* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
  for (int I=0;I<2;++I) {
    const float Y=14000+I*1000;
    auto* Sheet=Spawn(FVector(500,Y,0),FVector(4,4,4),TEXT("Glass"));
    auto* C=Sheet->GetStaticMeshComponent();C->SetStaticMesh(Plane);
    C->ComponentTags.Remove(TEXT("LidarSolid"));C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sheet->SetActorRotation(FRotator(90,I*30,0));
    Spawn(FVector(1000,Y,0),FVector(1,4,4),TEXT("Concrete"));
  }
  // Camera fixtures: the colored target is behind the camera and can only be
  // seen in the mirror. Glass has an emissive target behind its surface.
  Spawn(FVector(1000,6000,0),FVector(.05,10,10),TEXT("Mirror"));
  Spawn(FVector(-200,6300,0),FVector(2,2,2),TEXT("Red"));
  Spawn(FVector(500,8000,0),FVector(.2,8,8),TEXT("Glass"));
  Spawn(FVector(1000,8000,0),FVector(1,4,4),TEXT("Green"));
  Spawn(FVector(-200,8300,0),FVector(2,2,2),TEXT("Red"));
  const TWeakObjectPtr<AActor> WeakOwner(Owner);
  FTimerHandle Timer;
  World->GetTimerManager().SetTimer(Timer,[World,Base,WeakOwner] {
    if (!WeakOwner.IsValid()) return;
    TArray<FCarlaGpuRay> Rays;
    for (float Y:{0.f,600.f,1200.f,1800.f,2600.f,3400.f}) Rays.Add({Base+FVector(0,Y,0),FVector(1,0,0),3000,true,0});
    Rays.Add({Base+FVector(0,1800,0),FVector(1,0,0),3000,false,0});
    Rays.Add({Base+FVector(0,4300,0),FVector(1,0,0),3000,true,0});
    Rays.Add({Base+FVector(0,5000,0),FVector(1,0,0),3000,true,0});
    Rays.Add({Base+FVector(0,11000,0),FVector(1,0,0),3000,true,0});
    Rays.Add({Base+FVector(0,11000,0),FVector(1,0,0),3000,false,0});
    Rays.Add({Base+FVector(0,12000,0),FVector(1,0,0),3000,true,0});
    Rays.Add({Base+FVector(0,13000,0),FVector(1,0,0),3000,true,0});
    Rays.Add({Base+FVector(0,14000,0),FVector(1,0,0),3000,true,0});
    Rays.Add({Base+FVector(0,15000,0),FVector(1,0,0),3000,true,0});
    TArray<FCarlaLidarHit> Cpu;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CarlaOpticsTest),true,WeakOwner.Get());Params.bReturnFaceIndex=true;
    for (const auto& R:Rays) {
      if (R.MaterialModel) Cpu.Add(CarlaLidarOptics::Trace(*WeakOwner.Get(),R.Origin,R.Direction,R.Range,0,Params));
      else { FCarlaLidarHit H;World->LineTraceSingleByChannel(H.Hit,R.Origin,R.Origin+R.Direction*R.Range,ECC_GameTraceChannel2,Params);Cpu.Add(H); }
    }
    CarlaGpuSensors::Submit(*WeakOwner.Get(),MoveTemp(Rays),[Cpu=MoveTemp(Cpu)](TArray<FCarlaGpuHit>&& Hits) {
      const TCHAR* Names[]={TEXT("concrete"),TEXT("marking"),TEXT("sign"),TEXT("glass_transmission"),TEXT("mirror_bounce"),TEXT("mirror_miss"),TEXT("legacy_glass"),TEXT("oblique_glass"),TEXT("normal_mirror"),TEXT("non_solid_glass"),TEXT("legacy_non_solid_glass"),TEXT("disabled_glass"),TEXT("hidden_glass"),TEXT("thin_sheet_glass"),TEXT("oblique_thin_sheet_glass")};
      TArray<TSharedPtr<FJsonValue>> Rows;
      bool Passed=Hits.Num()==15;
      for (int I=0;I<Hits.Num();++I) {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("case"),Names[I]);
        Row->SetBoolField(TEXT("gpu_hit"),Hits[I].Hit.bBlockingHit);Row->SetNumberField(TEXT("gpu_range_cm"),Hits[I].Hit.Distance);Row->SetNumberField(TEXT("gpu_return"),Hits[I].SurfaceReturn);
        Row->SetBoolField(TEXT("cpu_hit"),Cpu[I].Hit.bBlockingHit);Row->SetNumberField(TEXT("cpu_range_cm"),Cpu[I].Hit.Distance);Row->SetNumberField(TEXT("cpu_return"),Cpu[I].SurfaceReturn);
        bool Match=Hits[I].Hit.bBlockingHit==Cpu[I].Hit.bBlockingHit;
        if (Hits[I].Hit.bBlockingHit) Match=Match && FMath::Abs(Hits[I].Hit.Distance-Cpu[I].Hit.Distance)<.5 && FMath::Abs(Hits[I].SurfaceReturn-Cpu[I].SurfaceReturn)<.005;
        Row->SetBoolField(TEXT("cpu_gpu_match"),Match); Passed=Passed&&Match;Rows.Add(MakeShared<FJsonValueObject>(Row));
      }
      if (Hits.Num()==15) Passed=Passed && Hits[0].Hit.bBlockingHit && Hits[1].SurfaceReturn>Hits[0].SurfaceReturn && Hits[2].SurfaceReturn>Hits[0].SurfaceReturn && Hits[3].Hit.Distance>900 && Hits[4].Hit.Distance>1500 && !Hits[5].Hit.bBlockingHit && Hits[6].Hit.Distance<600 && Hits[7].Hit.Distance>900 && Hits[8].SurfaceReturn>.9 && Hits[9].Hit.Distance>955 && FMath::Abs(Hits[10].Hit.Distance-950)<.5 && FMath::Abs(Hits[11].Hit.Distance-950)<.5 && FMath::Abs(Hits[12].Hit.Distance-950)<.5 && FMath::Abs(Hits[13].Hit.Distance-950)<.5 && FMath::Abs(Hits[14].Hit.Distance-950)<.5 && Hits[13].SurfaceReturn>.2 && Hits[13].SurfaceReturn<.23;
      auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Passed);Report->SetArrayField(TEXT("cases"),Rows);
      FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Report,Writer);
      FFileHelper::SaveStringToFile(Text,TEXT("/mnt/simulations/verification/lidar-optics-20260912/native-optics.json"));
      UE_LOG(LogCarla,Display,TEXT("CARLA OPTICS SELF TEST %s"),Passed?TEXT("PASSED"):TEXT("FAILED"));
    });
  },8,false);
}
FAutoConsoleCommandWithWorld Cmd(TEXT("carla.Sensors.OpticsSelfTest"),TEXT("Spawn optical fixtures and compare CPU/GPU strongest returns."),FConsoleCommandWithWorldDelegate::CreateStatic(&Run));
}
