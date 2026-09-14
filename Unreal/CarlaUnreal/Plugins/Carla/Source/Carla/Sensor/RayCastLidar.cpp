// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla/Sensor/RayCastLidar.h"
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Carla.h"
#include "Carla/Actor/ActorBlueprintFunctionLibrary.h"

#include <util/disable-ue4-macros.h>
#include "carla/geom/Math.h"
#include "carla/ros2/ROS2.h"
#include "carla/geom/Location.h"
#include <util/enable-ue4-macros.h>

#include <util/ue-header-guard-begin.h>
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Kismet/KismetMathLibrary.h"
#include <util/ue-header-guard-end.h>

#include <cmath>
#include "Carla/Sensor/LidarWeatherModel.h"
#include "Carla/Sensor/LidarSceneWeather.h"
#include "Carla/Weather/Weather.h"

FActorDefinition ARayCastLidar::GetSensorDefinition()
{
  return UActorBlueprintFunctionLibrary::MakeLidarDefinition(TEXT("ray_cast"));
}


ARayCastLidar::ARayCastLidar(const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer) {

  RandomEngine = CreateDefaultSubobject<URandomEngine>(TEXT("RandomEngine"));
  SetSeed(Description.RandomSeed);
}

void ARayCastLidar::Set(const FActorDescription &ActorDescription)
{
  ASensor::Set(ActorDescription);
  FLidarDescription LidarDescription;
  UActorBlueprintFunctionLibrary::SetLidar(ActorDescription, LidarDescription);
  Set(LidarDescription);
}

void ARayCastLidar::Set(const FLidarDescription &LidarDescription)
{
  Description = LidarDescription;
  FString PhysicalError;
  bConfigured = Description.PhysicalModel ? PhysicalState.Initialize(Description, PhysicalError)
      : Description.OutputFormat == TEXT("xyzi");
  if (!bConfigured) {
    UE_LOG(LogCarla, Error, TEXT("LiDAR configuration rejected: %s"),
        PhysicalError.IsEmpty() ? TEXT("extended output requires physical_model=true") : *PhysicalError);
    return;
  }
  LidarData = FLidarData(Description.Channels);
  CreateLasers();
  PointsPerChannel.resize(Description.Channels);

  // Compute drop off model parameters
  DropOffBeta = 1.0f - Description.DropOffAtZeroIntensity;
  DropOffAlpha = Description.DropOffAtZeroIntensity / Description.DropOffIntensityLimit;
  DropOffGenActive = Description.DropOffGenRate > std::numeric_limits<float>::epsilon();
}

void ARayCastLidar::PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaTime)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(ARayCastLidar::PostPhysTick);
  // Multi-GPU worlds replicate sensors, but only the assigned stream worker
  // should spend time tracing. Retain recording and ROS consumers.
  // AreClientsListening also includes enabled ROS streams and forced activity.
  const bool Needed = AreClientsListening() || bSavingDataToDisk;
  if (!bConfigured) return;
  if (!Needed) { if (Description.PhysicalModel) PhysicalState.Observe(*this); return; }
  // Snapshot on the game thread before any parallel hit processing.
  if (const auto* Weather = GetEpisode().GetWeather()) {
    const auto& Current = Weather->GetCurrentWeather();
    SceneRain=Current.Precipitation; SceneFog=Current.FogDensity;
    SceneFogStart=FMath::Max(0.f,Current.FogDistance); SceneDust=Current.DustStorm;
  }
  CarlaGpuSensors::BeginSensorTick(*this);
  const AActor* ParentAtCapture = GetAttachParentActor();
  const FTransform RelativeAtCapture = ParentAtCapture
      ? GetActorTransform().GetRelativeTransform(ParentAtCapture->GetActorTransform()) : GetActorTransform();
  auto DataStream = GetDataStream(*this);
  if (Description.PhysicalModel) {
    const TWeakObjectPtr<ARayCastLidar> Owner(this);
    const bool Extended = Description.OutputFormat == TEXT("extended");
    const double CaptureTime = GetEpisode().GetElapsedGameTime();
    PhysicalState.Simulate(*this, Description, DeltaTime,
        [Owner, Extended, CaptureTime, RelativeAtCapture, DataStream = MoveTemp(DataStream)](FPhysicalLidarFrame&& Frame) mutable {
      const double DelaySeconds = Frame.DeliveryDelayMs * .001;
      auto Deliver = MakeShared<TUniqueFunction<void()>>(
          [Owner, Extended, CaptureTime, RelativeAtCapture, DataStream = MoveTemp(DataStream), Data = MoveTemp(Frame.Data)]() mutable {
        if (!Owner.IsValid()) return;
        auto& Sensor = *Owner.Get();
        FLidarData Projection(Data.header.channels);
        std::vector<uint32_t> Counts(Data.header.channels, 0);
        for (const auto& P : Data.points) ++Counts[P.channel];
        Projection.ResetMemory(Counts);
        Projection.SetHorizontalAngle(Data.header.horizontal_angle);
        for (const auto& P : Data.points) {
          FDetection Point;Point.point = carla::geom::Location(P.x, P.y, P.z);Point.intensity = P.intensity;
          Projection.WritePointSync(Point);
        }
        Projection.WriteChannelCount(Counts);
        if (Extended) DataStream.SerializeAndSend(Sensor, Data, DataStream.PopBufferFromPool());
        else DataStream.SerializeAndSend(Sensor, Projection, DataStream.PopBufferFromPool());
#if defined(WITH_ROS2)
        auto ROS2 = carla::ros2::ROS2::GetInstance();
        if (ROS2->IsEnabled()) {
          const auto StreamId = carla::streaming::detail::token_type(Sensor.GetToken()).get_stream_id();
          if (Extended) ROS2->ProcessDataFromPhysicalLidar(StreamId, RelativeAtCapture, Data, CaptureTime, &Sensor);
          else ROS2->ProcessDataFromLidar(DataStream.GetSensorType(), StreamId, RelativeAtCapture, Projection, &Sensor, CaptureTime);
        }
#endif
#if WITH_EDITOR
        if (Sensor.bSavingDataToDisk) {
          Sensor.PointCloudLidarData.Empty(Data.points.size()*4);
          for (const auto& P : Data.points) {
            FDetection Point;Point.point = carla::geom::Location(P.x,P.y,P.z);Point.intensity=P.intensity;
            Sensor.PointCloudWritePointSync(Point);
          }
        }
#endif
      });
      if (Owner.IsValid()) CarlaGpuSensors::ScheduleDelivery(*Owner.Get(),DelaySeconds,
          [Deliver]() { (*Deliver)(); });
    });
    return;
  }
  auto Send = [this, RelativeAtCapture, DataStream = MoveTemp(DataStream)]() mutable
  {
  auto SensorTransform = DataStream.GetSensorTransform();

  {
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("Send Stream");
    DataStream.SerializeAndSend(*this, LidarData, DataStream.PopBufferFromPool());
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
      ROS2->ProcessDataFromLidar(DataStream.GetSensorType(), StreamId, LocalTransformRelativeToParent, LidarData, this);
    }
    else
    {
      ROS2->ProcessDataFromLidar(DataStream.GetSensorType(), StreamId, SensorTransform, LidarData, this);
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

float ARayCastLidar::ComputeIntensity(const FSemanticDetection& RawDetection) const
{
  const carla::geom::Location HitPoint = RawDetection.point;
  const float Distance = HitPoint.Length();

  const float AttenAtm = Description.AtmospAttenRate;
  const float AbsAtm = exp(-AttenAtm * Distance);

  const float IntRec = AbsAtm;

  return IntRec;
}

ARayCastLidar::FDetection ARayCastLidar::ComputeDetection(const FHitResult& HitInfo, const FTransform& SensorTransf) const
{
  FDetection Detection;
  const FVector HitPoint = HitInfo.ImpactPoint;
  Detection.point = SensorTransf.Inverse().TransformPosition(HitPoint);

  const float Distance = Detection.point.Length();

  const float AttenAtm = Description.AtmospAttenRate;
  const float AbsAtm = exp(-AttenAtm * Distance);

  const float IntRec = AbsAtm;

  Detection.intensity = IntRec;

  return Detection;
}

  void ARayCastLidar::PreprocessRays(uint32_t Channels, uint32_t MaxPointsPerChannel) {
    Super::PreprocessRays(Channels, MaxPointsPerChannel);

    for (auto ch = 0u; ch < Channels; ch++) {
      for (auto p = 0u; p < MaxPointsPerChannel; p++) {
        RayPreprocessCondition[ch][p] = !(DropOffGenActive && RandomEngine->GetUniformFloat() < Description.DropOffGenRate);
      }
    }
  }

  bool ARayCastLidar::PostprocessDetection(FDetection& Detection) const
  {
    if (Description.NoiseStdDev > std::numeric_limits<float>::epsilon()) {
      const auto ForwardVector = Detection.point.MakeUnitVector();
      const auto Noise = ForwardVector * RandomEngine->GetNormalDistribution(0.0f, Description.NoiseStdDev);
      Detection.point += Noise;
    }

    const float OriginalRange = Detection.point.Length();
    float WeatherRange = OriginalRange;
    // The legacy receiver is an approximation, but shares scene controls and
    // the physical model's extinction. Fog only occupies the path after its start.
    const float FogExtinction = CarlaLidarSceneWeather::fog_extinction(SceneFog);
    if (FogExtinction>0 && WeatherRange>SceneFogStart) {
      float FogRange=WeatherRange-SceneFogStart;
      const CarlaLidarWeather::Medium Fog{FogExtinction,.15f,0};
      if (!CarlaLidarWeather::apply(Fog,FogRange,Detection.intensity,
          [this]() { return RandomEngine->GetUniformFloat(); })) return false;
      WeatherRange=FogRange+SceneFogStart;
    }
    const float Extinction=CarlaLidarSceneWeather::rain_extinction(
        CarlaLidarSceneWeather::rain_mm_h(SceneRain))+CarlaLidarSceneWeather::dust_extinction(SceneDust);
    const CarlaLidarWeather::Medium Medium{Extinction,.15f,0};
    if (!CarlaLidarWeather::apply(Medium,WeatherRange,Detection.intensity,
        [this]() { return RandomEngine->GetUniformFloat(); })) return false;
    if (OriginalRange>0) Detection.point*=WeatherRange/OriginalRange;

    const float Intensity = Detection.intensity;
    if(Intensity > Description.DropOffIntensityLimit)
      return true;
    else
      return RandomEngine->GetUniformFloat() < DropOffAlpha * Intensity + DropOffBeta;
  }

  void ARayCastLidar::ComputeAndSaveDetections(const FTransform& SensorTransform) {
    for (auto idxChannel = 0u; idxChannel < Description.Channels; ++idxChannel)
      PointsPerChannel[idxChannel] = RecordedHits[idxChannel].size();

    LidarData.ResetMemory(PointsPerChannel);
#if WITH_EDITOR
    if(bSavingDataToDisk)
    {
      PointCloudResetMemory();
    }
#endif

    for (auto idxChannel = 0u; idxChannel < Description.Channels; ++idxChannel) {
      for (auto& hit : RecordedHits[idxChannel]) {
        FDetection Detection = ComputeDetection(hit.Hit, SensorTransform);
        Detection.intensity *= hit.SurfaceReturn;
        if (PostprocessDetection(Detection))
        {
          LidarData.WritePointSync(Detection);
#if WITH_EDITOR
          if(bSavingDataToDisk)
          {
            PointCloudWritePointSync(Detection);
          }
#endif
        }
        else
          PointsPerChannel[idxChannel]--;
      }
    }

    LidarData.WriteChannelCount(PointsPerChannel);
  }

void ARayCastLidar::PointCloudResetMemory()
{
  PointCloudLidarData.Empty();
  PointCloudLidarData.Reserve(static_cast<uint32_t>(std::accumulate(PointsPerChannel.begin(), PointsPerChannel.end(), 0)) * 4);
}

void ARayCastLidar::PointCloudWritePointSync(const FDetection& Detection)
{
  PointCloudLidarData.Emplace(Detection.point.x);
  PointCloudLidarData.Emplace(Detection.point.y);
  PointCloudLidarData.Emplace(Detection.point.z);
  PointCloudLidarData.Emplace(Detection.intensity);
}
