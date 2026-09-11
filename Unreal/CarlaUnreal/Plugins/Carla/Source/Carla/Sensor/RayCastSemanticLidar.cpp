// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla/Sensor/RayCastSemanticLidar.h"
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Carla.h"
#include "Carla/Actor/ActorBlueprintFunctionLibrary.h"
#include "Carla/Game/Tagger.h"

#include <util/disable-ue4-macros.h>
#include "carla/geom/Math.h"
#include "carla/ros2/ROS2.h"
#include <util/enable-ue4-macros.h>

#include <util/ue-header-guard-begin.h>
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Kismet/KismetMathLibrary.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "Async/ParallelFor.h"
#include <util/ue-header-guard-end.h>
#include "Landscape.h"

#include <cmath>

namespace crp = carla::rpc;

FActorDefinition ARayCastSemanticLidar::GetSensorDefinition()
{
  return UActorBlueprintFunctionLibrary::MakeLidarDefinition(TEXT("ray_cast_semantic"));
}

ARayCastSemanticLidar::ARayCastSemanticLidar(const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer)
{
  PrimaryActorTick.bCanEverTick = true;
}

void ARayCastSemanticLidar::Set(const FActorDescription &ActorDescription)
{
  Super::Set(ActorDescription);
  FLidarDescription LidarDescription;
  UActorBlueprintFunctionLibrary::SetLidar(ActorDescription, LidarDescription);
  Set(LidarDescription);
}

void ARayCastSemanticLidar::Set(const FLidarDescription &LidarDescription)
{
  Description = LidarDescription;
  SemanticLidarData = FSemanticLidarData(Description.Channels);
  CreateLasers();
  PointsPerChannel.resize(Description.Channels);
}

void ARayCastSemanticLidar::CreateLasers()
{
  const auto NumberOfLasers = Description.Channels;
  check(NumberOfLasers > 0u);
  const float DeltaAngle = NumberOfLasers == 1u ? 0.f :
    (Description.UpperFovLimit - Description.LowerFovLimit) /
    static_cast<float>(NumberOfLasers - 1);
  LaserAngles.Empty(NumberOfLasers);
  for(auto i = 0u; i < NumberOfLasers; ++i)
  {
    const float VerticalAngle =
        Description.UpperFovLimit - static_cast<float>(i) * DeltaAngle;
    LaserAngles.Emplace(VerticalAngle);
  }
}

void ARayCastSemanticLidar::PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaTime)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(ARayCastSemanticLidar::PostPhysTick);
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
  auto SensorTransform = DataStream.GetSensorTransform();
  {
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("Send Stream");
    DataStream.SerializeAndSend(*this, SemanticLidarData, DataStream.PopBufferFromPool());
  }
  // ROS2
  #if defined(WITH_ROS2)
  auto ROS2 = carla::ros2::ROS2::GetInstance();
  if (ROS2->IsEnabled())
  {
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("ROS2 Send");
    auto StreamId = carla::streaming::detail::token_type(GetToken()).get_stream_id();
    AActor* ParentActor = GetAttachParentActor();
    if (ParentActor)
    {
      FTransform LocalTransformRelativeToParent = RelativeAtCapture;
      ROS2->ProcessDataFromSemanticLidar(DataStream.GetSensorType(), StreamId, LocalTransformRelativeToParent, SemanticLidarData, this);
    }
    else
    {
      ROS2->ProcessDataFromSemanticLidar(DataStream.GetSensorType(), StreamId, SensorTransform, SemanticLidarData, this);
    }
  }
  #endif
  };
  if (CarlaGpuSensors::IsEnabled())
    SimulateLidarGpu(DeltaTime, MoveTemp(Send));
  else
  {
    SimulateLidar(DeltaTime);
    Send();
  }

}

void ARayCastSemanticLidar::SimulateLidar(const float DeltaTime)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(ARayCastSemanticLidar::SimulateLidar);
  const uint32 ChannelCount = Description.Channels;
  const uint32 PointsToScanWithOneLaser =
    FMath::RoundHalfFromZero(
        Description.PointsPerSecond * DeltaTime / float(ChannelCount));

  if (PointsToScanWithOneLaser <= 0)
  {
    UE_LOG(
        LogCarla,
        Warning,
        TEXT("%s: no points requested this frame, try increasing the number of points per second."),
        *GetName());
    return;
  }

  check(ChannelCount == LaserAngles.Num());

  const float CurrentHorizontalAngle = carla::geom::Math::ToDegrees(
      SemanticLidarData.GetHorizontalAngle());
  const float AngleDistanceOfTick = Description.RotationFrequency * Description.HorizontalFov
      * DeltaTime;
  const float AngleDistanceOfLaserMeasure = AngleDistanceOfTick / PointsToScanWithOneLaser;

  ResetRecordedHits(ChannelCount, PointsToScanWithOneLaser);
  PreprocessRays(ChannelCount, PointsToScanWithOneLaser);

  auto LockedPhysObject = FPhysicsObjectExternalInterface::LockRead(GetWorld()->GetPhysicsScene());
  {
    TRACE_CPUPROFILER_EVENT_SCOPE(ParallelFor);
    ParallelFor(ChannelCount, [&](int32 idxChannel) {
      TRACE_CPUPROFILER_EVENT_SCOPE(ParallelForTask);

      FCollisionQueryParams TraceParams = FCollisionQueryParams(FName(TEXT("Laser_Trace")), true, this);
      TraceParams.bTraceComplex = true;
      TraceParams.bReturnPhysicalMaterial = false;

      for (auto idxPtsOneLaser = 0u; idxPtsOneLaser < PointsToScanWithOneLaser; idxPtsOneLaser++) {
        FHitResult HitResult;
        const float VertAngle = LaserAngles[idxChannel];
        const float HorizAngle = std::fmod(CurrentHorizontalAngle + AngleDistanceOfLaserMeasure
            * idxPtsOneLaser, Description.HorizontalFov) - Description.HorizontalFov / 2;
        const bool PreprocessResult = RayPreprocessCondition[idxChannel][idxPtsOneLaser];

        if (PreprocessResult && ShootLaser(VertAngle, HorizAngle, HitResult, TraceParams)) {
          WritePointAsync(idxChannel, HitResult);
        }
      };
    });
  }
  LockedPhysObject.Release();

  FTransform ActorTransf = GetTransform();
  ComputeAndSaveDetections(ActorTransf);

  const float HorizontalAngle = carla::geom::Math::ToRadians(
      std::fmod(CurrentHorizontalAngle + AngleDistanceOfTick, Description.HorizontalFov));
  SemanticLidarData.SetHorizontalAngle(HorizontalAngle);
}

void ARayCastSemanticLidar::ResetRecordedHits(uint32_t Channels, uint32_t MaxPointsPerChannel) {
  RecordedHits.resize(Channels);

  for (auto& hits : RecordedHits) {
    hits.clear();
    hits.reserve(MaxPointsPerChannel);
  }
}

void ARayCastSemanticLidar::SimulateLidarGpu(float DeltaTime, TUniqueFunction<void()>&& Complete)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(CarlaGpuLidarSubmit);
  // Complete the preceding GPU batch before consuming the next random values.
  CarlaGpuSensors::Pump();
  const uint32 Channels = Description.Channels;
  const uint32 Count = FMath::Max(0, FMath::RoundHalfFromZero(
      Description.PointsPerSecond * DeltaTime / float(Channels)));
  const FTransform Transform = GetActorTransform();
  const float Start = carla::geom::Math::ToDegrees(SemanticLidarData.GetHorizontalAngle());
  const float Sweep = Description.RotationFrequency * Description.HorizontalFov * DeltaTime;
  const float NextAngle = carla::geom::Math::ToRadians(FMath::Fmod(Start + Sweep, Description.HorizontalFov));
  PreprocessRays(Channels, Count);
  TArray<FCarlaGpuRay> Rays;
  TArray<uint32> ChannelIndices;
  Rays.Reserve(Channels * Count);
  ChannelIndices.Reserve(Channels * Count);
  for (uint32 Channel = 0; Channel < Channels; ++Channel)
    for (uint32 Point = 0; Point < Count; ++Point)
    {
      if (!RayPreprocessCondition[Channel][Point]) continue;
      const float Horizontal = FMath::Fmod(Start + Sweep * Point / Count,
          Description.HorizontalFov) - Description.HorizontalFov * 0.5f;
      // Rotate the unit direction directly: avoid converting the sensor's
      // quaternion to Euler angles and back for every point in the batch.
      const FVector Direction = Transform.TransformVectorNoScale(
          FRotator(LaserAngles[Channel], Horizontal, 0).Vector());
      Rays.Add({Transform.GetLocation(), Direction, Description.Range});
      ChannelIndices.Add(Channel);
    }
  CarlaGpuSensors::Submit(*this, MoveTemp(Rays),
      [this, Transform, Channels, Count, NextAngle, ChannelIndices = MoveTemp(ChannelIndices),
       Complete = MoveTemp(Complete)](TArray<FCarlaGpuHit>&& Hits) mutable
  {
    TRACE_CPUPROFILER_EVENT_SCOPE(CarlaGpuLidarComplete);
    ResetRecordedHits(Channels, Count);
    for (int32 I = 0; I < Hits.Num(); ++I)
      if (Hits[I].Hit.bBlockingHit) WritePointAsync(ChannelIndices[I], Hits[I].Hit);
    ComputeAndSaveDetections(Transform);
    SemanticLidarData.SetHorizontalAngle(NextAngle);
    Complete();
  });
}

void ARayCastSemanticLidar::PreprocessRays(uint32_t Channels, uint32_t MaxPointsPerChannel) {
  RayPreprocessCondition.resize(Channels);

  for (auto& conds : RayPreprocessCondition) {
    conds.clear();
    conds.resize(MaxPointsPerChannel);
    std::fill(conds.begin(), conds.end(), true);
  }
}

void ARayCastSemanticLidar::WritePointAsync(uint32_t channel, FHitResult &detection) {
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
  DEBUG_ASSERT(GetChannelCount() > channel);
  RecordedHits[channel].emplace_back(detection);
}

void ARayCastSemanticLidar::ComputeAndSaveDetections(const FTransform& SensorTransform) {
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
  for (auto idxChannel = 0u; idxChannel < Description.Channels; ++idxChannel)
    PointsPerChannel[idxChannel] = RecordedHits[idxChannel].size();
  SemanticLidarData.ResetMemory(PointsPerChannel);

  for (auto idxChannel = 0u; idxChannel < Description.Channels; ++idxChannel) {
    for (auto& hit : RecordedHits[idxChannel]) {
      FSemanticDetection detection;
      ComputeRawDetection(hit, SensorTransform, detection);
      SemanticLidarData.WritePointSync(detection);
    }
  }

  SemanticLidarData.WriteChannelCount(PointsPerChannel);
}

void ARayCastSemanticLidar::ComputeRawDetection(const FHitResult& HitInfo, const FTransform& SensorTransf, FSemanticDetection& Detection) const
{
    const FVector HitPoint = HitInfo.ImpactPoint;
    Detection.point = SensorTransf.Inverse().TransformPosition(HitPoint);

    const FVector VecInc = - (HitPoint - SensorTransf.GetLocation()).GetSafeNormal();
    Detection.cos_inc_angle = FVector::DotProduct(VecInc, HitInfo.ImpactNormal);

    const FActorRegistry &Registry = GetEpisode().GetActorRegistry();

    const AActor* actor = HitInfo.GetActor();
    Detection.object_idx = 0;
    
    // Given that landscapes do not have tags for now, asign it here if the actor is a landscape, otherwise get the component tag
    if (actor && actor->IsA<ALandscape>()){
      Detection.object_tag = static_cast<uint32_t>(ATagger::GetTagFromString("Terrain"));
    }
    else if (HitInfo.Component.IsValid() && !HitInfo.Component->ComponentTags.IsEmpty()) {
      Detection.object_tag = static_cast<uint32_t>(ATagger::GetTagFromString(HitInfo.Component->ComponentTags[0].ToString()));
    }
    else {
      Detection.object_tag = 0;
    }

    if (actor != nullptr) {

      const FCarlaActor* view = Registry.FindCarlaActor(actor);
      if(view)
        Detection.object_idx = view->GetActorId();

    }
    else {
      UE_LOG(LogCarla, Warning, TEXT("Actor not valid %p!!!!"), actor);
    }
}


bool ARayCastSemanticLidar::ShootLaser(const float VerticalAngle, const float HorizontalAngle, FHitResult& HitResult, FCollisionQueryParams& TraceParams) const
{
  TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);

  FHitResult HitInfo(ForceInit);

  FTransform ActorTransf = GetTransform();
  FVector LidarBodyLoc = ActorTransf.GetLocation();
  FRotator LidarBodyRot = ActorTransf.Rotator();

  FRotator LaserRot (VerticalAngle, HorizontalAngle, 0);  // float InPitch, float InYaw, float InRoll
  FRotator ResultRot = UKismetMathLibrary::ComposeRotators(
    LaserRot,
    LidarBodyRot
  );

  const auto Range = Description.Range;
  FVector EndTrace = Range * UKismetMathLibrary::GetForwardVector(ResultRot) + LidarBodyLoc;
  
  GetWorld()->ParallelLineTraceSingleByChannel(
    HitInfo,
    LidarBodyLoc,
    EndTrace,
    ECC_GameTraceChannel2,
    TraceParams,
    FCollisionResponseParams::DefaultResponseParam
  );

  if (HitInfo.bBlockingHit) {
    HitResult = HitInfo;
    return true;
  } else {
    return false;
  }
}
