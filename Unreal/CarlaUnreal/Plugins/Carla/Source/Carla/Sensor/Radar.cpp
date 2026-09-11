// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla/Sensor/Radar.h"
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Carla.h"
#include "Carla/Actor/ActorBlueprintFunctionLibrary.h"

#include <util/disable-ue4-macros.h>
#include <carla/geom/Math.h>
#include <carla/ros2/ROS2.h>
#include <util/enable-ue4-macros.h>

#include <util/ue-header-guard-begin.h>
#include "Kismet/KismetMathLibrary.h"
#include "Async/ParallelFor.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include <util/ue-header-guard-end.h>

FActorDefinition ARadar::GetSensorDefinition()
{
  return UActorBlueprintFunctionLibrary::MakeRadarDefinition();
}

ARadar::ARadar(const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer)
{
  PrimaryActorTick.bCanEverTick = true;

  RandomEngine = CreateDefaultSubobject<URandomEngine>(TEXT("RandomEngine"));

  TraceParams = FCollisionQueryParams(FName(TEXT("Laser_Trace")), true, this);
  TraceParams.bTraceComplex = true;
  TraceParams.bReturnPhysicalMaterial = false;

}

void ARadar::Set(const FActorDescription &ActorDescription)
{
  Super::Set(ActorDescription);
  UActorBlueprintFunctionLibrary::SetRadar(ActorDescription, this);
}

void ARadar::SetHorizontalFOV(float NewHorizontalFOV)
{
  HorizontalFOV = NewHorizontalFOV;
}

void  ARadar::SetVerticalFOV(float NewVerticalFOV)
{
  VerticalFOV = NewVerticalFOV;
}

void ARadar::SetRange(float NewRange)
{
  Range = NewRange;
}

void ARadar::SetPointsPerSecond(int NewPointsPerSecond)
{
  PointsPerSecond = NewPointsPerSecond;
  RadarData.SetResolution(PointsPerSecond);
}

const carla::sensor::data::RadarData& ARadar::GetRadarData() const{
  return RadarData;
}

void ARadar::BeginPlay()
{
  Super::BeginPlay();

  PrevLocation = GetActorLocation();
}

void ARadar::PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaTime)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(ARadar::PostPhysTick);
  CalculateCurrentVelocity(DeltaTime);

  // Multi-GPU worlds replicate sensors, but only the assigned stream worker
  // should spend time tracing. Retain recording and ROS consumers.
  // AreClientsListening also includes enabled ROS streams and forced activity.
  const bool Needed = AreClientsListening() || bSavingDataToDisk;
  if (!Needed) return;
  CarlaGpuSensors::BeginSensorTick(*this);
  const AActor* ParentAtCapture = GetAttachParentActor();
  const FTransform RelativeAtCapture = ParentAtCapture
      ? GetActorTransform().GetRelativeTransform(ParentAtCapture->GetActorTransform()) : GetActorTransform();
  auto DataStream = GetDataStream(*this);
  auto Send = [this, RelativeAtCapture, DataStream = MoveTemp(DataStream)]() mutable
  {

  // ROS2
  #if defined(WITH_ROS2)
  auto ROS2 = carla::ros2::ROS2::GetInstance();
  if (ROS2->IsEnabled())
  {
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("ARadar::PostPhysTick ROS2 Send");
    auto StreamId = carla::streaming::detail::token_type(GetToken()).get_stream_id();
    AActor* ParentActor = GetAttachParentActor();
    if (ParentActor)
    {
      FTransform LocalTransformRelativeToParent = RelativeAtCapture;
      ROS2->ProcessDataFromRadar(DataStream.GetSensorType(), StreamId, LocalTransformRelativeToParent, RadarData, this);
    }
    else
    {
      ROS2->ProcessDataFromRadar(DataStream.GetSensorType(), StreamId, DataStream.GetSensorTransform(), RadarData, this);
    }
  }
  #endif

  {
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("Send Stream");
    DataStream.SerializeAndSend(*this, RadarData, DataStream.PopBufferFromPool());
  }
  };
  if (CarlaGpuSensors::IsEnabled()) SendGpuTraces(DeltaTime, MoveTemp(Send));
  else
  {
    RadarData.Reset();
    SendLineTraces(DeltaTime);
    Send();
  }

}

void ARadar::CalculateCurrentVelocity(const float DeltaTime)
{
  const FVector RadarLocation = GetActorLocation();
  CurrentVelocity = (RadarLocation - PrevLocation) / DeltaTime;
  PrevLocation = RadarLocation;
}

void ARadar::SendLineTraces(float DeltaTime)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(ARadar::SendLineTraces);
  constexpr float TO_METERS = 1e-2;
  const FTransform& ActorTransform = GetActorTransform();
  const FRotator& TransformRotator = ActorTransform.Rotator();
  const FVector& RadarLocation = GetActorLocation();
  const FVector& ForwardVector = GetActorForwardVector();
  const FVector TransformXAxis = ActorTransform.GetUnitAxis(EAxis::X);
  const FVector TransformYAxis = ActorTransform.GetUnitAxis(EAxis::Y);
  const FVector TransformZAxis = ActorTransform.GetUnitAxis(EAxis::Z);

  // Maximum radar radius in horizontal and vertical direction
  const float MaxRx = FMath::Tan(FMath::DegreesToRadians(HorizontalFOV * 0.5f)) * Range;
  const float MaxRy = FMath::Tan(FMath::DegreesToRadians(VerticalFOV * 0.5f)) * Range;
  const int NumPoints = (int)(PointsPerSecond * DeltaTime);

  // Generate the parameters of the rays in a deterministic way
  Rays.clear();
  Rays.resize(NumPoints);
  for (int i = 0; i < Rays.size(); i++) {
    Rays[i].Radius = RandomEngine->GetUniformFloat();
    Rays[i].Angle = RandomEngine->GetUniformFloatInRange(0.0f, carla::geom::Math::Pi2<float>());
    Rays[i].Hitted = false;
  }

  auto LockedPhysObject = FPhysicsObjectExternalInterface::LockRead(GetWorld()->GetPhysicsScene());
  {
    TRACE_CPUPROFILER_EVENT_SCOPE(ParallelFor);
    ParallelFor(NumPoints, [&](int32 idx) {
      TRACE_CPUPROFILER_EVENT_SCOPE(ParallelForTask);
      FHitResult OutHit(ForceInit);
      const float Radius = Rays[idx].Radius;
      const float Angle  = Rays[idx].Angle;

      float Sin, Cos;
      FMath::SinCos(&Sin, &Cos, Angle);

      const FVector EndLocation = RadarLocation + TransformRotator.RotateVector({
        Range,
        MaxRx * Radius * Cos,
        MaxRy * Radius * Sin
      });

      const bool Hitted = GetWorld()->ParallelLineTraceSingleByChannel(
        OutHit,
        RadarLocation,
        EndLocation,
        ECC_GameTraceChannel2,
        TraceParams,
        FCollisionResponseParams::DefaultResponseParam
      );

      const TWeakObjectPtr<AActor> HittedActor = OutHit.GetActor();
      if (Hitted && HittedActor.Get()) {
        Rays[idx].Hitted = true;

        Rays[idx].RelativeVelocity = CalculateRelativeVelocity(OutHit, RadarLocation);

        Rays[idx].AzimuthAndElevation = FMath::GetAzimuthAndElevation (
          (EndLocation - RadarLocation).GetSafeNormal() * Range,
          TransformXAxis,
          TransformYAxis,
          TransformZAxis
        );

        Rays[idx].Distance = OutHit.Distance * TO_METERS;
      }
    });
  }
  LockedPhysObject.Release();
  
  // Write the detections in the output structure
  for (auto& ray : Rays)
  {
    if (ray.Hitted)
    {
      RadarData.WriteDetection(
      {
        ray.RelativeVelocity,
        UKismetMathLibrary::Conv_DoubleToFloat(ray.AzimuthAndElevation.X),
        UKismetMathLibrary::Conv_DoubleToFloat(ray.AzimuthAndElevation.Y),
        ray.Distance
      });
    }
  }
}

float ARadar::CalculateRelativeVelocity(const FHitResult& OutHit, const FVector& RadarLocation)
{
  constexpr float TO_METERS = 1e-2;

  const TWeakObjectPtr<AActor> HittedActor = OutHit.GetActor();
  const FVector TargetVelocity = HittedActor->GetVelocity();
  const FVector TargetLocation = OutHit.ImpactPoint;
  const FVector Direction = (TargetLocation - RadarLocation).GetSafeNormal();
  const FVector DeltaVelocity = (TargetVelocity - CurrentVelocity);
  const float V = TO_METERS * FVector::DotProduct(DeltaVelocity, Direction);

  return V;
}

void ARadar::SendGpuTraces(float DeltaTime, TUniqueFunction<void()>&& Complete)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(CarlaGpuRadarSubmit);
  CarlaGpuSensors::Pump();
  const FTransform Transform = GetActorTransform();
  const FVector Velocity = CurrentVelocity;
  const float MaxX = FMath::Tan(FMath::DegreesToRadians(HorizontalFOV * 0.5f)) * Range;
  const float MaxY = FMath::Tan(FMath::DegreesToRadians(VerticalFOV * 0.5f)) * Range;
  const int32 Count = FMath::Max(0, int32(PointsPerSecond * DeltaTime));
  TArray<FCarlaGpuRay> Rays;
  TArray<FVector2D> Angles;
  TArray<FVector> Directions;
  Rays.Reserve(Count);
  for (int32 I = 0; I < Count; ++I)
  {
    const float Radius = RandomEngine->GetUniformFloat();
    const float Angle = RandomEngine->GetUniformFloatInRange(0, carla::geom::Math::Pi2<float>());
    float Sin, Cos;
    FMath::SinCos(&Sin, &Cos, Angle);
    const FVector Delta = Transform.GetRotation().RotateVector(FVector(Range, MaxX * Radius * Cos, MaxY * Radius * Sin));
    const FVector Direction = Delta.GetSafeNormal();
    Rays.Add({Transform.GetLocation(), Direction, float(Delta.Size())});
    Directions.Add(Direction);
    Angles.Add(FMath::GetAzimuthAndElevation(Direction * Range,
        Transform.GetUnitAxis(EAxis::X), Transform.GetUnitAxis(EAxis::Y), Transform.GetUnitAxis(EAxis::Z)));
  }
  CarlaGpuSensors::Submit(*this, MoveTemp(Rays),
      [this, Velocity, Directions = MoveTemp(Directions), Angles = MoveTemp(Angles),
       Complete = MoveTemp(Complete)](TArray<FCarlaGpuHit>&& Hits) mutable
  {
    TRACE_CPUPROFILER_EVENT_SCOPE(CarlaGpuRadarComplete);
    RadarData.Reset();
    for (int32 I = 0; I < Hits.Num(); ++I)
      if (Hits[I].Hit.bBlockingHit)
        RadarData.WriteDetection({float(0.01 * FVector::DotProduct(Hits[I].TargetVelocity - Velocity, Directions[I])),
            float(Angles[I].X), float(Angles[I].Y), Hits[I].Hit.Distance * 0.01f});
    Complete();
  });
}
