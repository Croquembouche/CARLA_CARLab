// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Carla/Sensor/LidarOptics.h"
#include "Carla/Sensor/LidarMotion.h"
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
namespace CarlaPhysicalLidarTest {
void Run(UWorld* World) {
  if(!World||!CarlaGpuSensors::IsEnabled())return;
  auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
  auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Carla/Testing/Optics/M_CarlaOptics_Concrete.M_CarlaOptics_Concrete"));
  auto* Owner=World->SpawnActor<AActor>();const FVector Base(0,0,110000);
  TArray<UPrimitiveComponent*> Components;
  auto Spawn=[&](FVector Position,FVector Scale) {
    auto* A=World->SpawnActor<AStaticMeshActor>();auto* C=A->GetStaticMeshComponent();
    C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Cube);C->SetMaterial(0,Material);
    C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);C->SetCollisionResponseToAllChannels(ECR_Ignore);C->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);
    A->SetActorLocation(Base+Position);A->SetActorScale3D(Scale);Components.Add(C);return A;
  };
  auto* Moving=Spawn(FVector(1000,0,0),FVector(1,1,2));
  auto* Mask=Spawn(FVector(1000,1000,0),FVector(1,4,4));Mask->GetStaticMeshComponent()->ComponentTags.Add(TEXT("LidarIR=physical-test-checker"));
  Spawn(FVector(500,3100,0),FVector(.2,2,4));Spawn(FVector(1000,3000,0),FVector(1,8,4));
  // Angled wall and a narrow foreground pole for full sensor timing/footprint checks.
  auto* Wall=Spawn(FVector(1000,5000,0),FVector(1,10,10));Wall->SetActorRotation(FRotator(0,30,0));
  const TWeakObjectPtr<AActor> WeakOwner(Owner),WeakMoving(Moving);
  FTimerHandle Warm;
  World->GetTimerManager().SetTimer(Warm,[World,Base,Components,WeakOwner,WeakMoving] {
    if(!WeakOwner.IsValid()||!WeakMoving.IsValid())return;
    CarlaLidarMotion::Prepare(World,Components);
    FTimerHandle Step;
    World->GetTimerManager().SetTimer(Step,[World,Base,Components,WeakOwner,WeakMoving] {
      if(!WeakOwner.IsValid()||!WeakMoving.IsValid())return;
      WeakMoving->SetActorLocation(Base+FVector(1000,200,0));
      const auto Motion=CarlaLidarMotion::Prepare(World,Components);
      TArray<FCarlaGpuRay> Rays;TArray<FString> Names;TArray<FCarlaLidarHit> Cpu;
      // Keep probes away from the exact silhouette: a zero-width ray at an
      // edge has no robust binary hit expectation under float rounding.
      for(float A:{0.f,.2f,.5f,.75f,1.f}) {
        FCarlaGpuRay R{Base,FVector(1,0,0),3000,true,0};R.PhysicalModel=true;R.TargetMotion=true;R.TimeFraction=A;R.SceneTime=FMath::Lerp(Motion->Meshes[0].BeforeTime,Motion->Meshes[0].AfterTime,double(A));Rays.Add(R);Names.Add(FString::Printf(TEXT("moving_%.2f"),A));
      }
      for(int Condition=0;Condition<3;++Condition)for(float Y:{900.f,1100.f})for(float Z:{-100.f,100.f}) {
        FCarlaGpuRay R{Base+FVector(0,Y,Z),FVector(1,0,0),3000,true,0};R.PhysicalModel=true;R.WavelengthNm=Condition==1?1550:905;R.Wetness=Condition==2?1:0;Rays.Add(R);Names.Add(FString::Printf(TEXT("infrared_%d_y%.0f_z%.0f"),Condition,Y,Z));
      }
      CarlaLidarOptics::Prepare(World);
      for(const auto& R:Rays) {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(CarlaPhysicalTest),true,WeakOwner.Get());Params.bReturnFaceIndex=true;
        if(R.TargetMotion)for(auto* C:Components)if(Motion->Primitives.Contains(C->GetPrimitiveSceneId().PrimIDValue))Params.AddIgnoredComponent(C);
        Cpu.Add(CarlaLidarOptics::Trace(*WeakOwner.Get(),R.Origin,R.Direction,R.Range,0,Params,nullptr,R.TargetMotion?Motion.Get():nullptr,R.TimeFraction,R.WavelengthNm,R.Wetness,R.SceneTime));
      }
      CarlaGpuSensors::Submit(*WeakOwner.Get(),MoveTemp(Rays),[Cpu=MoveTemp(Cpu),Names=MoveTemp(Names)](TArray<FCarlaGpuHit>&& Hits) {
        auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;bool Passed=Hits.Num()==17;
        TSet<int> MaskLevels;
        for(int I=0;I<Hits.Num();++I) {
          const auto& G=Hits[I];const auto& C=Cpu[I];auto Row=MakeShared<FJsonObject>();
          bool Match=G.Hit.bBlockingHit==C.Hit.bBlockingHit;
          if(G.Hit.bBlockingHit)Match=Match&&FMath::Abs(G.Hit.Distance-C.Hit.Distance)<.5&&FMath::Abs(G.SurfaceReturn-C.SurfaceReturn)<.002;
          Row->SetStringField(TEXT("case"),Names[I]);Row->SetBoolField(TEXT("match"),Match);Row->SetBoolField(TEXT("gpu_hit"),G.Hit.bBlockingHit);Row->SetBoolField(TEXT("cpu_hit"),C.Hit.bBlockingHit);
          Row->SetNumberField(TEXT("gpu_return"),G.SurfaceReturn);Row->SetNumberField(TEXT("cpu_return"),C.SurfaceReturn);Row->SetNumberField(TEXT("gpu_range_cm"),G.Hit.Distance);Row->SetNumberField(TEXT("cpu_range_cm"),C.Hit.Distance);Rows.Add(MakeShared<FJsonValueObject>(Row));Passed=Passed&&Match;
          if(I>=5&&I<9)MaskLevels.Add(FMath::RoundToInt(G.SurfaceReturn*100));
        }
        if(Hits.Num()==17) {
          Passed=Passed&&Hits[0].Hit.bBlockingHit&&!Hits[4].Hit.bBlockingHit&&MaskLevels.Num()==2;
          for(int I=5;I<9;++I)Passed=Passed&&FMath::Abs(Hits[I+4].SurfaceReturn-Hits[I].SurfaceReturn*.25)<.002&&FMath::Abs(Hits[I+8].SurfaceReturn-Hits[I].SurfaceReturn*.5)<.002;
        }
        Report->SetBoolField(TEXT("passed"),Passed);Report->SetArrayField(TEXT("cases"),Rows);FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Report,Writer);
        FFileHelper::SaveStringToFile(Text,TEXT("/mnt/simulations/verification/lidar-physical-20260912/native-physical-fixtures.json"));UE_LOG(LogCarla,Display,TEXT("CARLA PHYSICAL FIXTURES %s"),Passed?TEXT("PASSED"):TEXT("FAILED"));
      });
    },.05,false);
  },8,false);
}
FAutoConsoleCommandWithWorld Cmd(TEXT("carla.Sensors.PhysicalSelfTest"),TEXT("Test infrared masks and moving render geometry, and spawn physical sensor fixtures."),FConsoleCommandWithWorldDelegate::CreateStatic(&Run));
}
