// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/PhysicalLidar.h"
#include "Carla/Sensor/Sensor.h"
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Carla/Sensor/LidarOptics.h"
#include "Carla/Sensor/LidarMotion.h"
#include "Carla/Sensor/LidarSceneWeather.h"
#include "Carla/Weather/Weather.h"
#include "Carla/Vehicle/CarlaWheeledVehicle.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Carla.h"
#include <atomic>
#include <type_traits>
#include "Misc/Crc.h"
#include "HAL/IConsoleManager.h"

using namespace CarlaPhysicalLidar;
namespace {
static TAutoConsoleVariable<int32> CVarReuseReceiverScratch(
    TEXT("carla.Sensors.ReuseReceiverScratch"),0,
    TEXT("Reuse physical receiver scratch within bounded 128-pulse blocks; preserve pulse order and streams."),ECVF_Default);
using FSharedRays=TSharedPtr<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe>;
FSharedRays AcquireRayStorage(FSharedRays& Storage) {
  // Only this game-thread owner publishes new references. No weak references
  // escape. Pending GPU batches and receivers hold strong references, so a
  // borrowed frame always gets separate storage. Pair the uniqueness load with
  // the shared controller's release decrement before writing reused storage.
  if(Storage.IsValid() && Storage.IsUnique())std::atomic_thread_fence(std::memory_order_acquire);
  else Storage=MakeShared<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe>();
  return Storage;
}
struct Firing {
  FVector Origin,Direction,LocalOrigin,LocalDirection;
  uint64 Id=0;
  uint32 Channel=0,FirstRay=0;
  double Time=0,Azimuth=0,Elevation=0,Transmission=1,Scatter=0;
};
bool VolumeInterval(const FPhysicalLidarVolume& V,const FVector& Origin,const FVector& Direction,double Range,double& Begin,double& End) {
  const FVector O=V.Transform.InverseTransformPosition(Origin),D=V.Transform.InverseTransformVectorNoScale(Direction)*100;
  Begin=0;End=Range;
  for(int Axis=0;Axis<3;++Axis) {
    if(FMath::Abs(D[Axis])<1.e-12) { if(FMath::Abs(O[Axis])>V.Extent[Axis])return false;continue; }
    double A=(-V.Extent[Axis]-O[Axis])/D[Axis],B=(V.Extent[Axis]-O[Axis])/D[Axis];if(A>B)Swap(A,B);
    Begin=FMath::Max(Begin,A);End=FMath::Min(End,B);if(End<=Begin)return false;
  }return true;
}
double Value(const TArray<double>& A,uint32 Channel,double Default) { return A.IsValidIndex(Channel)?A[Channel]:Default; }
template<typename SampleArray>
void PrepareRays(TArray<Firing>& Pulses,TArray<FCarlaGpuRay>& Rays,
    const FPhysicalLidarProfile& Profile,const FLidarDescription& D,const SampleArray& Samples,
    const FTransform& Before,const FTransform& Current,const FWeatherParameters& Weather,
    double Epoch,double Start,double End,double SceneTime,bool WarmStart,bool Parallel) {
  TRACE_CPUPROFILER_EVENT_SCOPE(CarlaPhysicalPrepareRays);
  static_assert(std::is_trivially_copyable<FCarlaGpuRay>::value,"Ray slots must support complete aggregate assignment");
  const double ResizeStarted=FPlatformTime::Seconds();
  // Every slot is assigned a complete ray below. Keep the original initialized
  // array in the serial oracle to catch missing fields or unwritten slots.
  if(Parallel)Rays.SetNumUninitialized(Pulses.Num()*Samples.size(),EAllowShrinking::No);
  else Rays.SetNum(Pulses.Num()*Samples.size());
  if(Parallel)CarlaGpuSensors::ProfileSample(TEXT("physical_ray_resize_ms"),(FPlatformTime::Seconds()-ResizeStarted)*1000);
  const double MathStarted=FPlatformTime::Seconds();
  // Beam quadrature is constant for a scan. Preserve exactly the same
  // normalization and offset arithmetic without repeating it for every firing.
  TArray<FVector> BeamDirections,BeamOffsets;
  const double AngularSigma=FMath::DegreesToRadians(Profile.BeamDivergenceDeg)/2.354820045;
  for(const auto& Sample:Samples) {
    BeamDirections.Add(FVector(1,Sample.x*AngularSigma,Sample.y*AngularSigma).GetSafeNormal());
    BeamOffsets.Add(FVector(0,Sample.x,Sample.y));
  }
  ParallelFor(Pulses.Num(),[&](int32 I) {
      auto& F=Pulses[I];const uint32 C=F.Channel;const double Time=F.Time;
      F.FirstRay=I*Samples.size();
      F.Azimuth=std::fmod((Time-Epoch)*D.RotationFrequency*D.HorizontalFov,D.HorizontalFov)-D.HorizontalFov*.5+
          Value(Profile.AzimuthOffsetDeg,C,0)+Profile.EncoderBiasDeg;
      F.Elevation=Value(Profile.ElevationDeg,C,D.Channels==1?D.UpperFovLimit:D.UpperFovLimit-double(C)*(D.UpperFovLimit-D.LowerFovLimit)/(D.Channels-1));
      Random Randomness(uint64(D.RandomSeed)^F.Id^UINT64_C(0x75b849381d263acf));
      const double TrueAz=F.Azimuth+Profile.EncoderNoiseDeg*Randomness.normal()+Profile.AngularNoiseDeg*Randomness.normal();
      const double TrueEl=F.Elevation+Profile.AngularNoiseDeg*Randomness.normal();
      const double Alpha=Profile.MotionDistortion?FMath::Clamp((Time-Start)/FMath::Max(1.e-9,End-Start),0.0,1.0):1;
      FTransform Pose;Pose.Blend(Before,Current,Alpha);
      F.LocalOrigin=Profile.BeamOriginsM.IsValidIndex(C)?Profile.BeamOriginsM[C]*100:FVector::ZeroVector;
      F.LocalDirection=FRotator(F.Elevation,F.Azimuth,0).Vector();
      const FQuat Rotation=Pose.GetRotation()*FRotator(TrueEl,TrueAz,0).Quaternion();
      F.Direction=Rotation.GetForwardVector();F.Origin=Pose.TransformPosition(F.LocalOrigin);
      const double Az=FMath::UnwindDegrees(F.Azimuth);
      for(const auto& Cover:Profile.Cover) {
        const bool WithinAz=Cover.AzimuthMin<=Cover.AzimuthMax?(Az>=Cover.AzimuthMin && Az<=Cover.AzimuthMax):(Az>=Cover.AzimuthMin || Az<=Cover.AzimuthMax);
        if(WithinAz && F.Elevation>=Cover.ElevationMin && F.Elevation<=Cover.ElevationMax) {
          F.Scatter+=F.Transmission*Cover.Scatter;F.Transmission*=Cover.Transmission;
        }
      }
      int32 SubIndex=0;
      for(const auto& Sample:Samples) {
        // The serial automation reference retains per-firing quadrature math
        // and checks the cached parallel output against it bit for bit.
        const FVector Local=Parallel?BeamDirections[SubIndex]:FVector(1,Sample.x*AngularSigma,Sample.y*AngularSigma).GetSafeNormal();
        const FVector Aperture=Parallel?BeamOffsets[SubIndex]:FVector(0,Sample.x,Sample.y);
        const FVector Offset=Rotation.RotateVector(Aperture)*Profile.BeamDiameterM*100/2.354820045;
        FCarlaGpuRay Ray{F.Origin+Offset,Rotation.RotateVector(Local),D.Range,true,0};
        Ray.PhysicalModel=true;Ray.TimeFraction=Alpha;Ray.TargetMotion=Profile.TargetMotion && !WarmStart;
        Ray.WavelengthNm=Profile.Receiver.wavelength_nm;Ray.Wetness=Weather.Wetness*.01f;Ray.SceneTime=SceneTime-(End-Time);
        Rays[F.FirstRay+SubIndex++]=Ray;
      }
  },Parallel?EParallelForFlags::None:EParallelForFlags::ForceSingleThread);
  if(Parallel)CarlaGpuSensors::ProfileSample(TEXT("physical_ray_math_ms"),(FPlatformTime::Seconds()-MathStarted)*1000);
}
std::vector<carla::sensor::data::PhysicalLidarDetection> ReceivePhysicalPoints(
    const TArray<Firing>& Pulses,const TArray<FCarlaGpuRay>& Rays,
    const CarlaGpuSensors::FPhysicalHitView& Hits,const std::vector<BeamSample>& Samples,
    const FPhysicalLidarProfile& ProfileCopy,const FLidarDescription& D,
    const TArray<FPhysicalLidarVolume>& Volumes,const FWeatherParameters& Weather,
    double Start,double ClockScale,bool ReuseScratch,int32 RequestedBlockSize) {
    TArray<std::vector<carla::sensor::data::PhysicalLidarDetection>> Results;const int32 BlockSize=ReuseScratch?FMath::Clamp(RequestedBlockSize,1,1024):1;
    const int32 Blocks=Pulses.Num()/BlockSize+(Pulses.Num()%BlockSize!=0);
    Results.SetNum(Blocks);
    // Weather and receiver background are immutable snapshots. Their order of
    // arithmetic and per-pulse random streams remain unchanged.
    const double Rain=FMath::Max(ProfileCopy.RainMmH,CarlaLidarSceneWeather::rain_mm_h(Weather.Precipitation));
    const double Extinction=FMath::Max(0.0,double(D.AtmospAttenRate))+
        CarlaLidarSceneWeather::rain_extinction(Rain)+(ProfileCopy.FogVisibilityM>0?3.912/ProfileCopy.FogVisibilityM:0)+
        ProfileCopy.SmokeExtinction+ProfileCopy.DustExtinction+ProfileCopy.SnowExtinction+
        CarlaLidarSceneWeather::dust_extinction(Weather.DustStorm);
    const double SceneFog=CarlaLidarSceneWeather::fog_extinction(Weather.FogDensity);
    const double FogStart=FMath::Clamp(double(Weather.FogDistance),0.0,double(D.Range)*.01);
    const double Backscatter=FMath::Max(0.0,Extinction-FMath::Max(0.0,double(D.AtmospAttenRate)))*.15;
    std::vector<MediumSegment> BaseMedium{{0,D.Range*.01,Extinction,Backscatter}};
    if(SceneFog>0)BaseMedium.push_back({FogStart,D.Range*.01,SceneFog,SceneFog*.15});
    double Lux=ProfileCopy.AmbientLux;
    if(ProfileCopy.AmbientFromWeather) Lux=100000*FMath::Max(0.0,std::sin(FMath::DegreesToRadians(double(Weather.SunAltitudeAngle))))*(1-.8*FMath::Clamp(double(Weather.Cloudiness)*.01,0.0,1.0));
    const FVector SunDirection=FRotator(Weather.SunAltitudeAngle,Weather.SunAzimuthAngle,0).Vector();
    ParallelFor(Blocks,[&](int32 Block) {
      std::vector<Echo> Echoes;
      std::vector<Detection> Detected;
      std::vector<Echo> Sub;
      std::vector<MediumSegment> Medium;
      if(!Volumes.IsEmpty())Medium.reserve(BaseMedium.size()+Volumes.Num());
      const int32 First=Block*BlockSize,Last=First+FMath::Min(BlockSize,Pulses.Num()-First);
      for(int32 I=First;I<Last;++I) {
        const auto& F=Pulses[I];Echoes.clear();
        for(int S=0;S<int(Samples.size());++S) {
          const int Index=F.FirstRay+S;if(!Hits.IsValidIndex(Index))continue;
          const auto& H=Hits[Index];const auto& Ray=Rays[Index];Sub.clear();
          for(uint32 E=0;E<H.EchoCount;++E) {
            const auto& V=H.Echoes[E];Sub.push_back({V.Distance*.01,V.SurfaceReturn*Samples[S].weight,V.Flags,V.Component});
          }
          // Scene fog starts at the same distance used by the camera renderer.
          // Weather is snapshotted every scan; existing sensors update in place.
          if(!Volumes.IsEmpty()) {
            Medium.assign(BaseMedium.begin(),BaseMedium.end());
            for(const auto& V:Volumes) { double Begin,Finish;if(VolumeInterval(V,Ray.Origin,Ray.Direction,D.Range*.01,Begin,Finish))Medium.push_back({Begin,Finish,V.Extinction,V.Backscatter}); }
          }
          Random WeatherRandom(uint64(D.RandomSeed)^F.Id^(UINT64_C(0x7b23ad819657d8c1)*(S+1)));
          propagate_medium(Sub,Volumes.IsEmpty()?BaseMedium:Medium,H.UnoccludedRange*.01,Samples[S].weight,WeatherRandom);
          for(auto& E:Sub) { E.response*=F.Transmission*F.Transmission*Value(ProfileCopy.ChannelGain,F.Channel,1);Echoes.push_back(E); }
        }
        if(F.Scatter>0)Echoes.push_back({.2,F.Scatter,Cover,0});
        Receiver ReceiverCopy=ProfileCopy.Receiver;ReceiverCopy.range_bias_m+=Value(ProfileCopy.ChannelRangeBiasM,F.Channel,0);
        const double Direct=Weather.SunAltitudeAngle>0?ProfileCopy.DirectSunGain*std::exp((FVector::DotProduct(F.Direction,SunDirection)-1)/.002):0;
        ReceiverCopy.background_photons+=Lux*.001*ProfileCopy.BackgroundPerKlux*(1+Direct);
        Random DetectionRandom(uint64(D.RandomSeed)^F.Id^UINT64_C(0xa42b507ce5390217));
        if(ReuseScratch)detect_into(Echoes,ReceiverCopy,DetectionRandom,Detected);
        else Detected=detect(MoveTemp(Echoes),ReceiverCopy,DetectionRandom);
        for(size_t R=0;R<Detected.size();++R) {
          const auto& V=Detected[R];carla::sensor::data::PhysicalLidarDetection Point;
          const FVector Local=F.LocalOrigin*.01+F.LocalDirection*V.range_m;
          Point.x=Local.X;Point.y=Local.Y;Point.z=Local.Z;Point.intensity=V.reflectivity;
          Point.range=V.range_m;Point.signal=V.signal;Point.ambient=V.ambient;Point.pulse_width=V.pulse_width_ns;
          Point.azimuth=FMath::DegreesToRadians(F.Azimuth);Point.elevation=FMath::DegreesToRadians(F.Elevation);
          Point.time_offset=(F.Time-Start)*ClockScale;Point.confidence=V.confidence;
          Point.pulse_id=F.Id;Point.channel=F.Channel;Point.return_id=R;Point.return_count=Detected.size();Point.flags=V.flags;
          Results[Block].push_back(Point);
        }
      }
    });
    size_t PointCount=0;
    for(const auto& Points:Results)PointCount+=Points.size();
    std::vector<carla::sensor::data::PhysicalLidarDetection> Output;Output.reserve(PointCount);
    for(auto& Points:Results)Output.insert(Output.end(),Points.begin(),Points.end());
    return Output;
}
}
bool FPhysicalLidarState::Initialize(const FLidarDescription& D,FString& Error) {
  if(D.OutputFormat!=TEXT("xyzi") && D.OutputFormat!=TEXT("extended")) { Error=TEXT("output_format must be xyzi or extended");return false; }
  if(!D.MaterialModel) { Error=TEXT("physical_model requires material_model=true");return false; }
  if(!FMath::IsFinite(D.RotationFrequency) || D.RotationFrequency<=0 || D.PointsPerSecond<1 ||
      !FMath::IsFinite(D.HorizontalFov) || D.HorizontalFov<=0 || D.HorizontalFov>360 ||
      !FMath::IsFinite(D.UpperFovLimit) || !FMath::IsFinite(D.LowerFovLimit) || D.UpperFovLimit>90 || D.LowerFovLimit< -90 || D.UpperFovLimit<D.LowerFovLimit) {
    Error=TEXT("Invalid physical lidar scan configuration");return false;
  }
  if(!FPhysicalLidarProfile::Load(D.PhysicalProfile,D.Channels,D.Range,Profile,Error))return false;
  const double Period=double(D.Channels)/D.PointsPerSecond;
  for(double Offset:Profile.FiringOffsetUs)if(Offset*1.e-6>=Period) { Error=TEXT("Channel firing offsets must fit within a firing column");return false; }
  if(Profile.BeamSamples*double(D.PointsPerSecond)>20000000) { Error=TEXT("Physical lidar exceeds 20 million sub-beams per simulation second");return false; }
  return true;
}
void FPhysicalLidarState::Observe(const ASensor& Sensor) {
  PreviousPose=Sensor.GetActorTransform();PreviousTime=Sensor.GetEpisode().GetElapsedGameTime();
  // No subscription means no output, not a stopped physical rotor.
  if(Epoch<0)Epoch=PreviousTime;
  NextFiring.Reset();
}
void FPhysicalLidarState::Simulate(ASensor& Sensor,const FLidarDescription& D,float DeltaTime,
    TUniqueFunction<void(FPhysicalLidarFrame&&)>&& Complete) {
  TRACE_CPUPROFILER_EVENT_SCOPE(CarlaPhysicalSimulate);
  const double PrepareStarted=FPlatformTime::Seconds();
  const double End=Sensor.GetEpisode().GetElapsedGameTime();
  const bool WarmStart=PreviousTime<0 || PreviousTime>=End;
  const double Start=WarmStart?End-DeltaTime:PreviousTime;
  const FTransform Current=Sensor.GetActorTransform(),Before=WarmStart?Current:PreviousPose;
  if(Epoch<0 || End<Epoch)Epoch=Start;
  const double Period=double(D.Channels)/D.PointsPerSecond;
  if(NextFiring.Num()!=int32(D.Channels)) {
    NextFiring.SetNum(D.Channels);
    for(uint32 C=0;C<D.Channels;++C) {
      const double Offset=Value(Profile.FiringOffsetUs,C,double(C)*1.e6/D.PointsPerSecond)*1.e-6;
      const double Column=FMath::CeilToDouble((Start-Epoch-Offset)/Period-1.e-9);
      NextFiring[C]=Epoch+FMath::Max(0.0,Column)*Period+Offset;
    }
  }
  FWeatherParameters Weather;
  if(auto* W=Sensor.GetEpisode().GetWeather())Weather=W->GetCurrentWeather();
  const auto Samples=beam_samples(Profile.BeamSamples);
  const double StorageStarted=FPlatformTime::Seconds();
  const bool Reusing=RayStorage.IsValid() && RayStorage.IsUnique();
  auto RayCopy=AcquireRayStorage(RayStorage);
  auto& Rays=*RayCopy;
  TArray<Firing> Pulses;
  const double Expected=FMath::Max(0.0,End-Start)*D.PointsPerSecond;
  if((FMath::CeilToDouble(Expected)+D.Channels)*Samples.size()>2097120) {
    UE_LOG(LogCarla,Error,TEXT("Physical lidar frame exceeds the GPU dispatch bound; reduce points_per_second, beam_samples or sensor_tick"));
    FPhysicalLidarFrame Failed;Failed.Data.Reset(D.Channels,Start,End,Sequence++);Failed.Data.header.flags=16;Complete(MoveTemp(Failed));
    PreviousPose=Current;PreviousTime=End;NextFiring.Reset();return;
  }
  Pulses.Reserve(FMath::CeilToInt(Expected)+D.Channels);Rays.Reserve(FMath::CeilToInt(Expected)*Samples.size()+D.Channels*Samples.size());
  CarlaGpuSensors::ProfileSample(TEXT("physical_ray_storage_ms"),(FPlatformTime::Seconds()-StorageStarted)*1000);
  CarlaGpuSensors::ProfileSample(TEXT("physical_ray_storage_reused"),Reusing?1:0);
  const double ScheduleStarted=FPlatformTime::Seconds();
  for(uint32 C=0;C<D.Channels;++C) {
    while(NextFiring[C]<End-1.e-10) {
      const double Time=NextFiring[C];NextFiring[C]+=Period;if(Time<Start-1.e-8)continue;
      const double Offset=Value(Profile.FiringOffsetUs,C,double(C)*1.e6/D.PointsPerSecond)*1.e-6;
      const uint64 Column=uint64(FMath::Max(0.0,std::round((Time-Epoch-Offset)/Period)));
      Firing F;F.Id=Column*D.Channels+C;F.Channel=C;F.Time=Time;F.FirstRay=0;
      Pulses.Add(F);
    }
  }
  CarlaGpuSensors::ProfileSample(TEXT("physical_pulse_schedule_ms"),(FPlatformTime::Seconds()-ScheduleStarted)*1000);
  // The scheduler above remains serial and defines pulse order. Preparation
  // writes disjoint preallocated pulse/ray slots and never accesses UObjects.
  PrepareRays(Pulses,Rays,Profile,D,Samples,Before,Current,Weather,Epoch,Start,End,
      End,WarmStart,true);
  PreviousPose=Current;PreviousTime=End;
  TArray<FPhysicalLidarVolume> Volumes=Profile.Volumes;
  if(Profile.RoadSpray && Weather.Wetness>0) {
    for(TActorIterator<ACarlaWheeledVehicle> A(Sensor.GetWorld());A;++A) {
      const FVector Velocity=A->GetVelocity();const double Speed=Velocity.Size()*.01;if(Speed<2)continue;
      FPhysicalLidarVolume V;V.Transform=FTransform(A->GetActorQuat(),A->GetActorLocation()-Velocity.GetSafeNormal()*(200+Speed*10));
      V.Extent=FVector(150+Speed*15,110,70);V.Extinction=Profile.SprayExtinction*(Weather.Wetness*.01)*FMath::Min(2.0,Speed/10);
      V.Backscatter=V.Extinction*.3;Volumes.Add(V);
    }
  }
  const auto ProfileCopy=Profile;
  const uint64 PacketSequence=Sequence++;
  // Preserve per-ray directions for spatial medium integration after readback.
  auto Receive=[Pulses=MoveTemp(Pulses),RayCopy,Samples,ProfileCopy,D,Volumes=MoveTemp(Volumes),Weather,Start,End,WarmStart,PacketSequence,Complete=MoveTemp(Complete)](const CarlaGpuSensors::FPhysicalHitView& Hits) mutable {
    TRACE_CPUPROFILER_EVENT_SCOPE(CarlaPhysicalReceiver);
    const double ReceiveStarted=FPlatformTime::Seconds();
    FPhysicalLidarFrame Frame;Random PacketRandom(uint64(D.RandomSeed)^PacketSequence^UINT64_C(0x215d92410c45af23));
    const double ClockScale=1+ProfileCopy.ClockDriftPpm*1.e-6;
    const double ClockOffset=ProfileCopy.ClockOffsetS+ProfileCopy.ClockJitterNs*1.e-9*PacketRandom.normal();
    Frame.Data.Reset(D.Channels,Start*ClockScale+ClockOffset,End*ClockScale+ClockOffset,PacketSequence);
    Frame.Data.header.flags=(ProfileCopy.CalibrationStatus==TEXT("validated")?1u:0u)|(WarmStart?2u:0u);
    if(Hits.HasIncompleteMotion())Frame.Data.header.flags|=8u;
    Frame.Data.header.pulse_count=Pulses.Num();Frame.Data.header.profile_crc=ProfileCopy.Crc;
    Frame.Data.header.wavelength_nm=ProfileCopy.Receiver.wavelength_nm;
    Frame.DeliveryDelayMs=FMath::Max(0.0,ProfileCopy.Receiver.latency_ms+ProfileCopy.Receiver.latency_jitter_ms*PacketRandom.normal());
    if(PacketRandom.uniform()<ProfileCopy.Receiver.packet_loss_probability) {
      Frame.Data.header.flags|=4;Complete(MoveTemp(Frame));return;
    }
    const bool ReuseScratch=CVarReuseReceiverScratch.GetValueOnAnyThread()!=0;
    CarlaGpuSensors::ProfileSample(TEXT("physical_receiver_scratch_reused"),ReuseScratch?1:0);
    Frame.Data.points=ReceivePhysicalPoints(Pulses,*RayCopy,Hits,Samples,ProfileCopy,D,
        Volumes,Weather,Start,ClockScale,ReuseScratch,128);
    Frame.Data.header.point_count=Frame.Data.points.size();
    if(!Pulses.IsEmpty())Frame.Data.header.horizontal_angle=FMath::DegreesToRadians(Pulses.Last().Azimuth);
    CarlaGpuSensors::ProfileSample(TEXT("physical_receiver_ms"),(FPlatformTime::Seconds()-ReceiveStarted)*1000);
    Complete(MoveTemp(Frame));
  };
  CarlaGpuSensors::ProfileSample(TEXT("physical_generate_rays_ms"),(FPlatformTime::Seconds()-PrepareStarted)*1000);
  if(CarlaGpuSensors::IsEnabled())CarlaGpuSensors::SubmitPhysical(Sensor,RayCopy.ToSharedRef(),MoveTemp(Receive));
  else {
    // CPU compatibility uses the existing collision/static-render optics path.
    // The GPU path additionally interpolates deforming render meshes.
    const auto& Rays=*RayCopy;
    CarlaLidarOptics::Prepare(Sensor.GetWorld());TArray<FCarlaPhysicalGpuHit> Hits;Hits.SetNum(Rays.Num());
    TArray<UPrimitiveComponent*> Components;
    for(TActorIterator<AActor> A(Sensor.GetWorld());A;++A) {
      if(!A->GetActorEnableCollision() || A->IsHidden())continue;
      TInlineComponentArray<UPrimitiveComponent*> Parts;A->GetComponents(Parts);
      bool HasProxy=false;
      for(auto* C:Parts)if(C->GetCollisionProfileName()==FName(TEXT("CustomSensorCollision")) && C->IsQueryCollisionEnabled() && C->GetCollisionResponseToChannel(ECC_GameTraceChannel2)==ECR_Block)HasProxy=true;
      for(auto* C:Parts) {
        if(C->GetOwner()==&Sensor)continue;
        const bool Solid=C->IsQueryCollisionEnabled() && C->GetCollisionResponseToChannel(ECC_GameTraceChannel2)==ECR_Block;
        const auto Materials=CarlaLidarOptics::Sections(C);
        if(HasProxy || Solid || CarlaLidarOptics::NeedsRenderSurface(C,Materials))Components.Add(C);
      }
    }
    auto Motion=CarlaLidarMotion::Prepare(Sensor.GetWorld(),Components);
    FCollisionQueryParams MotionParams(SCENE_QUERY_STAT(PhysicalLidarMotion),true,&Sensor);
    MotionParams.bReturnFaceIndex=true;
    for(const auto& M:Motion->Meshes)if(auto* C=M.Component.Get()) {
      MotionParams.AddIgnoredComponent(C);
      TInlineComponentArray<UPrimitiveComponent*> Parts;C->GetOwner()->GetComponents(Parts);
      for(auto* Part:Parts)if(Part->GetCollisionProfileName()==FName(TEXT("CustomSensorCollision")))MotionParams.AddIgnoredComponent(Part);
    }
    ParallelFor(Rays.Num(),[&](int32 I) {
      const auto& R=Rays[I];FCollisionQueryParams Params(SCENE_QUERY_STAT(PhysicalLidar),true,&Sensor);Params.bReturnFaceIndex=true;
      FCarlaLidarTrace Result;CarlaLidarOptics::Trace(Sensor,R.Origin,R.Direction,R.Range,0,R.TargetMotion?MotionParams:Params,&Result,R.TargetMotion?Motion.Get():nullptr,R.TimeFraction,R.WavelengthNm,R.Wetness,R.SceneTime);
      auto& H=Hits[I];H.MotionIncomplete=R.TargetMotion && (Motion->MissingGeometry>0 || R.SceneTime<Motion->OldestCoveredTime-1.e-7);H.UnoccludedRange=Result.UnoccludedRange;
      for(const auto& E:Result.Echoes) { if(H.EchoCount==8)break;auto& V=H.Echoes[H.EchoCount++];V.Distance=E.Hit.Distance;V.SurfaceReturn=E.SurfaceReturn;V.Component=E.Hit.GetComponent()?E.Hit.GetComponent()->GetUniqueID():0;V.Flags=E.OpticalDepth?Surface|Multipath:Surface; }
    });Receive(CarlaGpuSensors::FPhysicalHitView(Hits));
  }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"

#include "Carla/Sensor/Tests/PhysicalLidarReceiverReference.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaPhysicalReceiverScratchTest,"Carla.Sensors.PhysicalReceiverScratch",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaPhysicalReceiverScratchTest::RunTest(const FString&) {
  // The frozen allocating algorithm is independent of the new scratch wrapper.
  std::vector<Echo> Scratch;std::vector<Detection> Output;
  for(int32 Setting=0;Setting<24;++Setting)for(int32 Count:{0,1,2,7,8,33,129,512})for(uint64 Seed=0;Seed<16;++Seed) {
    Receiver P;P.maximum_range_m=150;P.max_returns=Setting%5;P.return_order=Setting%3;
    P.shot_noise=Setting%2;P.false_alarms=Setting%4!=0;P.background_photons=Setting%3?1:500;
    P.electronic_noise_photons=Setting%3;P.saturation_photons=Setting%4?65535:200;
    P.range_bias_m=.01;P.temperature_c=32;P.temperature_bias_m_per_c=.001;
    P.time_walk_m=.2;P.saturation_bias_m=.04;P.range_quantization_m=Setting%3?.002:0;
    std::vector<Echo> Echoes;Random Generate(Seed+Setting*1234);
    for(int32 I=0;I<Count;++I) {
      Echo E{Generate.uniform()*170-5,Generate.uniform()*.9,uint32(1u<<(I%4)),uint32(I%7)};
      if(I%9==0)E.range_m=12+.03*(I%4);
      if(I%31==0)E.response=std::numeric_limits<double>::quiet_NaN();
      if(I%29==0)E.range_m=std::numeric_limits<double>::infinity();
      Echoes.push_back(E);
    }
    Random A(Seed),B(Seed);const auto Reference=detect_reference(Echoes,P,A);
    Scratch.assign(Echoes.begin(),Echoes.end());Output.assign(17,Detection{999});
    detect_into(Scratch,P,B,Output);
    bool Equal=Reference.size()==Output.size() && A.state==B.state;
    for(size_t I=0;Equal && I<Output.size();++I)Equal=FMemory::Memcmp(&Reference[I],&Output[I],sizeof(Detection))==0;
    if(!Equal) {AddError(FString::Printf(TEXT("Frozen receiver mismatch setting=%d count=%d seed=%u"),Setting,Count,uint32(Seed)));return false;}
  }
  const auto Samples=beam_samples(7);
  for(int32 Scenario=0;Scenario<5;++Scenario)for(int32 Count:{0,1,127,128,129,4097,80001}) {
    // Large throughput-sized coverage uses the clear scenario; the remaining
    // cases exercise weather, calibration, cover and return ordering at tails.
    if(Count==80001 && Scenario!=0)continue;
    FLidarDescription D;D.Channels=64;D.Range=15000;D.RandomSeed=42;
    FPhysicalLidarProfile Profile;Profile.Receiver.maximum_range_m=150;
    Profile.Receiver.shot_noise=Scenario%2;Profile.Receiver.max_returns=1+Scenario%4;
    Profile.Receiver.return_order=Scenario%3;Profile.Receiver.false_alarms=true;
    Profile.Receiver.background_photons=Scenario==3?500:1;
    Profile.Receiver.range_bias_m=.017;Profile.ClockDriftPpm=40;
    Profile.ChannelGain={.3,.9,1.1,2.};Profile.ChannelRangeBiasM={-.003,.001,.02};
    Profile.RainMmH=Scenario==1?20:0;Profile.FogVisibilityM=Scenario==2?300:0;
    Profile.AmbientFromWeather=Scenario!=3;Profile.DirectSunGain=.4;Profile.BackgroundPerKlux=1;
    FWeatherParameters Weather;Weather.SunAltitudeAngle=35;Weather.SunAzimuthAngle=70;
    Weather.Cloudiness=23;Weather.Wetness=Scenario==1?50:0;
    Weather.FogDensity=Scenario==2?12:0;Weather.FogDistance=5;
    TArray<FPhysicalLidarVolume> Volumes;
    if(Scenario==4) {
      FPhysicalLidarVolume V;V.Transform=FTransform(FRotator(0,30,0),FVector(1400,-120,80));
      V.Extent=FVector(900,500,150);V.Extinction=.01;V.Backscatter=.004;Volumes.Add(V);
    }
    TArray<Firing> Pulses;Pulses.SetNum(Count);
    TArray<FCarlaGpuRay> Rays;Rays.SetNum(Count*Samples.size());
    TArray<FCarlaPhysicalGpuHit> Hits;Hits.SetNum(Rays.Num());
    for(int32 I=0;I<Count;++I) {
      auto& F=Pulses[I];F.Channel=I%64;F.Id=UINT64_C(0x123456780000)+I;
      F.FirstRay=I*Samples.size();F.Azimuth=(I%360)-180;F.Elevation=-15+(I%31);
      F.Time=7+I*.000002;F.LocalOrigin=FVector(.12,-.23,.45);
      F.LocalDirection=FRotator(F.Elevation,F.Azimuth,0).Vector();F.Direction=F.LocalDirection;
      F.Origin=FVector(2000000000.0,-1000000000.0,3000000000.0);
      F.Transmission=I%7==0?.63:1;F.Scatter=I%11==0?.02:0;
      for(int32 S=0;S<int32(Samples.size());++S) {
        const int32 Index=F.FirstRay+S;auto& Ray=Rays[Index];auto& H=Hits[Index];
        Ray.Origin=FVector::ZeroVector;Ray.Direction=F.Direction;Ray.Range=D.Range;
        H.UnoccludedRange=15000;H.EchoCount=Index%5==0?0:1+Index%8;
        for(uint32 E=0;E<H.EchoCount;++E) {
          auto& V=H.Echoes[E];V.Distance=500+(I%4000)+E*10;
          V.SurfaceReturn=.02+(S%4)*.03;V.Flags=E%2?Surface:Surface|Multipath;V.Component=E%3;
        }
      }
    }
    const uint32 RayHash=FCrc::MemCrc32(Rays.GetData(),Rays.Num()*sizeof(FCarlaGpuRay));
    const uint32 HitHash=FCrc::MemCrc32(Hits.GetData(),Hits.Num()*sizeof(FCarlaPhysicalGpuHit));
    const CarlaGpuSensors::FPhysicalHitView View(Hits);
    const auto Reference=ReceivePhysicalPoints(Pulses,Rays,View,Samples,Profile,D,Volumes,Weather,7,1.00004,false,1);
    for(int32 BlockSize:{1,3,128,1024}) {
      if(Count==80001 && BlockSize!=128)continue;
      const auto Actual=ReceivePhysicalPoints(Pulses,Rays,View,Samples,Profile,D,Volumes,Weather,7,1.00004,true,BlockSize);
      bool Equal=Reference.size()==Actual.size();
      for(size_t I=0;Equal && I<Actual.size();++I)Equal=FMemory::Memcmp(&Reference[I],&Actual[I],sizeof(Actual[I]))==0;
      if(!Equal) {AddError(FString::Printf(TEXT("Physical wire/order mismatch scenario=%d pulses=%d block=%d"),Scenario,Count,BlockSize));return false;}
    }
    TestEqual(TEXT("receiver leaves every ray byte immutable"),FCrc::MemCrc32(Rays.GetData(),Rays.Num()*sizeof(FCarlaGpuRay)),RayHash);
    TestEqual(TEXT("receiver leaves every hit byte immutable"),FCrc::MemCrc32(Hits.GetData(),Hits.Num()*sizeof(FCarlaPhysicalGpuHit)),HitHash);
  }
  return true;
}
static FAutoConsoleCommand PhysicalReceiverScratchTestCommand(
    TEXT("carla.Sensors.TestPhysicalReceiverScratch"),TEXT("Verify frozen receiver model and complete scratch/legacy wire output."),
    FConsoleCommandDelegate::CreateLambda([]() {
      auto& Framework=FAutomationTestFramework::Get();
      if(Framework.GetCurrentTest()) {UE_LOG(LogCarla,Warning,TEXT("PHYSICAL_RECEIVER_SCRATCH_TEST busy"));return;}
      Framework.StartTestByName(TEXT("FCarlaPhysicalReceiverScratchTest"),0,TEXT("Carla.Sensors.PhysicalReceiverScratch"));
      if(!Framework.GetCurrentTest()) {UE_LOG(LogCarla,Warning,TEXT("PHYSICAL_RECEIVER_SCRATCH_TEST result=Fail test_did_not_start"));return;}
      FAutomationTestExecutionInfo Info;const bool Passed=Framework.StopTest(Info);
      UE_LOG(LogCarla,Display,TEXT("PHYSICAL_RECEIVER_SCRATCH_TEST result=%s"),Passed?TEXT("Success"):TEXT("Fail"));
    }));

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaPhysicalRayPreparationTest,"Carla.Sensors.PhysicalRayPreparation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaPhysicalRayPreparationTest::RunTest(const FString&) {
  {
    FSharedRays Storage;
    auto Borrowed=AcquireRayStorage(Storage);
    Borrowed->SetNum(5);(*Borrowed)[0].Range=1234;
    auto Next=AcquireRayStorage(Storage);
    TestTrue(TEXT("pending frame gets independent ray storage"),Next.Get()!=Borrowed.Get());
    Next->SetNum(7);(*Next)[0].Range=9876;
    TestEqual(TEXT("a pending frame remains immutable"),(*Borrowed)[0].Range,1234.f);
    auto* Array=Next.Get();auto* Allocation=Next->GetData();
    Next.Reset();
    auto Reused=AcquireRayStorage(Storage);
    TestTrue(TEXT("completed frame reuses its array"),Reused.Get()==Array);
    TestTrue(TEXT("completed frame retains its allocation"),Reused->GetData()==Allocation);
    TestEqual(TEXT("reused storage contents survive until overwritten"),(*Reused)[0].Range,9876.f);
    Reused->SetNumUninitialized(0,EAllowShrinking::No);
    TestTrue(TEXT("empty frame retains capacity"),Reused->GetData()==Allocation && Reused->Max()>=7);
  }
  {
    TArray<FCarlaGpuRay> Rays;Rays.SetNum(32769);
    const FVector Translation(-1000000000.0,2000000000.0,-3000000000.0);
    for(int32 I=0;I<Rays.Num();++I) {
      auto& R=Rays[I];R.Origin=-Translation+FVector(.125*I,.03125*I,-.0625*I);
      R.Direction=FVector(.125,-.25,.5);R.Range=15000.f-I*.01f;
      R.MaterialModel=I%2;R.PhysicalModel=I%3;R.TargetMotion=I%5;
      R.AtmosphericAttenuation=.004f;R.TimeFraction=I/32768.f;
      R.WavelengthNm=905.f+I%7;R.Wetness=(I%4)*.125f;
      R.SceneTime=I%2?12345678.125+I*.000001:-1;
    }
    for(bool HasMotion:{false,true}) {
      TArray<FVector4f> Packed,Reference;
      CarlaGpuSensors::PackRayInputs(Rays,Translation,HasMotion,12345678.0,Packed);
      for(const auto& R:Rays) {
        const FVector Origin=R.Origin+Translation;
        Reference.Add(FVector4f(FVector3f(Origin),R.Range));
        Reference.Add(FVector4f(FVector3f(R.Direction),R.MaterialModel?R.AtmosphericAttenuation+1.f:0.f));
        Reference.Add(FVector4f(R.PhysicalModel?1.f:0.f,R.TimeFraction,R.TargetMotion?1.f:0.f,0));
        Reference.Add(FVector4f(R.WavelengthNm,R.Wetness,R.SceneTime>=0 && HasMotion?float(R.SceneTime-12345678.0):FLT_MAX,0));
      }
      TestEqual(TEXT("packed input count"),Packed.Num(),Reference.Num());
      TestTrue(TEXT("parallel GPU input bit-identical to serial encoding"),
          FMemory::Memcmp(Packed.GetData(),Reference.GetData(),Reference.Num()*sizeof(FVector4f))==0);
      TestEqual(TEXT("large-world sub-centimetre origin retained"),Packed[4].X,.125f);
      CarlaGpuSensors::PackRayInputs(TConstArrayView<FCarlaGpuRay>(),Translation,HasMotion,0,Packed);
      TestEqual(TEXT("empty input clears reused output"),Packed.Num(),0);
    }
  }

  {
    auto* M=UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface),GetTransientPackage());
    FScalarParameterValue Scalar;Scalar.ParameterInfo=FMaterialParameterInfo(TEXT("LidarDiffuse"));Scalar.ParameterValue=.73f;
    M->ScalarParameterValues.Add(Scalar);
    auto* C=NewObject<UStaticMeshComponent>();
    C->ComponentTags.Add(TEXT("LidarMaterial=glass"));
    CarlaLidarOptics::SnapshotMaterialCache Cache;
    auto A=CarlaLidarOptics::Resolve(C,M,&Cache);
    auto B=CarlaLidarOptics::Resolve(C,M);
    TestEqual(TEXT("cached material scalar matches uncached"),A.diffuse,B.diffuse);
    TestEqual(TEXT("authored material scalar is retained"),A.diffuse,.73f);
    TestEqual(TEXT("component glass preset remains active"),A.transmission,.96f);
    C->ComponentTags.Reset();C->ComponentTags.Add(TEXT("LidarMaterial=mirror"));
    B=CarlaLidarOptics::Resolve(C,M,&Cache);
    TestEqual(TEXT("component presets are not cached across components"),B.specular,.95f);
    TestEqual(TEXT("shared material read only once per snapshot"),Cache.Num(),1);
    M->ScalarParameterValues[0].ParameterValue=.31f;
    CarlaLidarOptics::SnapshotMaterialCache NextSnapshot;
    B=CarlaLidarOptics::Resolve(C,M,&NextSnapshot);
    TestEqual(TEXT("next snapshot observes runtime scalar edits"),B.diffuse,.31f);
  }
  {
    auto* Var=IConsoleManager::Get().FindConsoleVariable(TEXT("carla.Sensors.CacheMaterialPresets"));
    TestNotNull(TEXT("material preset trial registered"),Var);
    if(Var) {
      const int32 Previous=Var->GetInt();
      const EConsoleVariableFlags PreviousFlags=Var->GetFlags();
      Var->Set(1,ECVF_SetByConsole);
      struct Expected { const TCHAR* Name; float Diffuse,Retro,Specular,Transmission; };
      const Expected Cases[]={
        {TEXT("MIRROR_window"),0,0,.95f,0},{TEXT("glass_sign"),.002f,0,0,.96f},
        {TEXT("Window"),.002f,0,0,.96f},{TEXT("roadline"),.45f,.45f,0,0},
        {TEXT("road_line"),.45f,.45f,0,0},{TEXT("marking"),.45f,.45f,0,0},
        {TEXT("lane_mark"),.45f,.45f,0,0},{TEXT("sign"),.25f,.7f,0,0},
        {TEXT("signal"),.3f,0,0,0},{TEXT("design"),.3f,0,0,0},
        {TEXT("sign_post"),.3f,0,0,0},{TEXT("sign_back"),.3f,0,0,0},
        {TEXT("asphalt"),.1f,0,0,0},{TEXT("concrete"),.3f,0,0,0},
        {TEXT("cement"),.3f,0,0,0},{TEXT("rubber"),.08f,0,0,0},
        {TEXT("tire"),.08f,0,0,0},{TEXT("unclassified"),.3f,0,0,0}};
      auto Check=[&](const CarlaLidarOptics::Material& A,const CarlaLidarOptics::Material& B) {
        TestEqual(TEXT("preset diffuse exact"),A.diffuse,B.diffuse);
        TestEqual(TEXT("preset retro exact"),A.retro,B.retro);
        TestEqual(TEXT("preset specular exact"),A.specular,B.specular);
        TestEqual(TEXT("preset transmission exact"),A.transmission,B.transmission);
        TestEqual(TEXT("preset IOR exact"),A.ior,B.ior);
        TestEqual(TEXT("preset roughness exact"),A.roughness,B.roughness);
        TestEqual(TEXT("preset thin sheet exact"),A.thinSheet,B.thinSheet);
        TestEqual(TEXT("preset infrared exact"),A.infrared,B.infrared);
      };
      auto* C=NewObject<UStaticMeshComponent>();
      for(const auto& E:Cases) {
        auto* Outer=NewObject<UStaticMeshComponent>();
        auto* M=UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface),Outer,FName(E.Name));
        CarlaLidarOptics::SnapshotMaterialCache Cache;
        const auto A=CarlaLidarOptics::Resolve(C,M,&Cache);
        CarlaLidarOptics::Material ExpectedMaterial;
        ExpectedMaterial.diffuse=E.Diffuse;ExpectedMaterial.retro=E.Retro;
        ExpectedMaterial.specular=E.Specular;ExpectedMaterial.transmission=E.Transmission;
        Check(A,ExpectedMaterial);
        Check(CarlaLidarOptics::Resolve(C,M,&Cache),A);
        TestTrue(TEXT("snapshot preset stored after first lookup"),Cache.FindChecked(M).HasPreset);
        for(const auto& Tags:TArray<TArray<FName>>{
          {TEXT("RoadLine")},{TEXT("RoadLines"),TEXT("LidarMaterial=mirror")},
          {TEXT("LidarMaterial=glass"),TEXT("RoadLine")},{TEXT("RoadLine"),TEXT("LidarMaterial=")},
          {TEXT("LidarMaterial="),TEXT("RoadLine")},{TEXT("LidarMaterial=glass"),TEXT("LidarSolid")},
          {TEXT("LidarIR=missing"),TEXT("LidarIR=physical-test-checker")}}) {
          C->ComponentTags=Tags;
          const auto Cached=CarlaLidarOptics::Resolve(C,M,&Cache);
          const auto Uncached=CarlaLidarOptics::Resolve(C,M);
          Check(Cached,Uncached);
        }
        C->ComponentTags.Reset();
      }
      auto* Outer=NewObject<UStaticMeshComponent>();
      auto* M=UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface),Outer,TEXT("physical-test-checker"));
      CarlaLidarOptics::SnapshotMaterialCache Cache;
      auto A=CarlaLidarOptics::Resolve(C,M,&Cache);
      TestTrue(TEXT("cached material infrared profile found"),A.infrared>=0);
      C->ComponentTags={TEXT("LidarIR=missing")};
      Check(CarlaLidarOptics::Resolve(C,M,&Cache),CarlaLidarOptics::Resolve(C,M));
      C->ComponentTags={TEXT("RoadLine"),TEXT("LidarMaterial=")};
      A=CarlaLidarOptics::Resolve(C,M,&Cache);
      TestEqual(TEXT("empty override after road tag retains marking preset"),A.retro,.45f);
      C->ComponentTags={TEXT("LidarMaterial="),TEXT("RoadLine")};
      A=CarlaLidarOptics::Resolve(C,M,&Cache);
      TestEqual(TEXT("tags after first material override stay ignored"),A.retro,0.f);
      FScalarParameterValue Scalar;Scalar.ParameterInfo=FMaterialParameterInfo(TEXT("LidarDiffuse"));Scalar.ParameterValue=.123f;
      M->ScalarParameterValues.Add(Scalar);
      CarlaLidarOptics::SnapshotMaterialCache Next;
      TestEqual(TEXT("fresh snapshot observes changed scalar with cached presets"),CarlaLidarOptics::Resolve(C,M,&Next).diffuse,.123f);
      TestEqual(TEXT("existing snapshot retains captured scalar inputs"),CarlaLidarOptics::Resolve(C,M,&Cache).diffuse,.3f);
      Var->Set(Previous,ECVF_SetByConsole);
      Var->ClearFlags(ECVF_SetByMask);
      Var->SetFlags(static_cast<EConsoleVariableFlags>(PreviousFlags&ECVF_SetByMask));
    }
  }
  TestTrue(TEXT("physical readback avoids the generic collision hit storage"),sizeof(FCarlaPhysicalGpuHit)<sizeof(FCarlaGpuHit));
  FVector4f Output[10];
  for(uint32 E=0;E<8;++E) {
    uint32 Component=0xff800001u+E,Flags=0x80000001u+E;
    float ComponentBits,FlagBits;
    FMemory::Memcpy(&ComponentBits,&Component,sizeof(Component));
    FMemory::Memcpy(&FlagBits,&Flags,sizeof(Flags));
    Output[E+2]=FVector4f(100.25f+E,ComponentBits,.125f*E,FlagBits);
  }
  for(int32 Count:{-1,0,1,8,12}) {
    Output[0]=FVector4f(0,0,0,Count);
    FCarlaPhysicalGpuHit Physical;FCarlaGpuHit General;
    Physical.UnoccludedRange=1234.5f;Physical.MotionIncomplete=true;
    CarlaGpuSensors::DecodePhysicalEchoes(Physical,Output);
    CarlaGpuSensors::DecodePhysicalEchoes(General,Output);
    TestEqual(TEXT("echo count is bounded by the eight GPU slots"),Physical.EchoCount,uint32(FMath::Clamp(Count,0,8)));
    TestEqual(TEXT("unoccluded range remains independent"),Physical.UnoccludedRange,1234.5f);
    TestTrue(TEXT("motion coverage flag remains independent"),Physical.MotionIncomplete);
    for(uint32 E=0;E<Physical.EchoCount;++E) {
      const auto& V=Physical.Echoes[E];
      TestEqual(TEXT("echo distance exact"),V.Distance,100.25f+E);
      TestEqual(TEXT("echo return exact"),V.SurfaceReturn,.125f*E);
      TestEqual(TEXT("component bits preserved including float NaN patterns"),V.Component,0xff800001u+E);
      TestEqual(TEXT("optical flags preserved"),V.Flags,0x80000001u+E);
      TestTrue(TEXT("physical and generic decode agree"),FMemory::Memcmp(&V,&General.Echoes[E],sizeof(V))==0);
    }
  }
  {
    TArray<FVector4f> Encoded;Encoded.SetNumZeroed(30);
    TArray<FCarlaGpuRay> TestRays;TestRays.SetNum(3);
    TArray<FCarlaPhysicalGpuHit> Decoded;Decoded.SetNum(3);
    for(int32 I=0;I<3;++I) {
      TestRays[I].TargetMotion=I!=0;TestRays[I].SceneTime=I==2?1.0:3.0;
      Encoded[I*10]=FVector4f(100,0,0,I==0?0:8);
      Encoded[I*10+1]=FVector4f(0,0,0,15000-I);
      for(int32 E=0;E<8;++E)Encoded[I*10+2+E]=Output[2+E];
      Decoded[I].UnoccludedRange=15000-I;Decoded[I].MotionIncomplete=I==2;
      CarlaGpuSensors::DecodePhysicalEchoes(Decoded[I],&Encoded[I*10]);
    }
    CarlaGpuSensors::FPhysicalHitView View(Encoded,TestRays,10,true,false,2.0);
    CarlaGpuSensors::FPhysicalHitView CpuView(Decoded);
    TestTrue(TEXT("GPU view preserves incomplete motion coverage"),View.HasIncompleteMotion());
    TestTrue(TEXT("CPU view preserves incomplete motion coverage"),CpuView.HasIncompleteMotion());
    TestFalse(TEXT("view rejects negative index"),View.IsValidIndex(-1));
    TestFalse(TEXT("view rejects past-end index"),View.IsValidIndex(3));
    for(int32 I=0;I<3;++I) {
      const auto H=View[I],C=CpuView[I];
      TestEqual(TEXT("borrowed echo count exact"),H.EchoCount,C.EchoCount);
      TestEqual(TEXT("borrowed unoccluded range exact"),H.UnoccludedRange,C.UnoccludedRange);
      TestEqual(TEXT("borrowed per-ray motion flag exact"),H.MotionIncomplete,C.MotionIncomplete);
      for(uint32 E=0;E<H.EchoCount;++E)
        TestTrue(TEXT("borrowed echo fields bit-identical"),FMemory::Memcmp(&H.Echoes[E],&C.Echoes[E],sizeof(FCarlaGpuEcho))==0);
    }
    CarlaGpuSensors::FPhysicalHitView NoMotion(Encoded,TestRays,10,false,true,2.0);
    TestFalse(TEXT("no motion snapshot does not mark coverage incomplete"),NoMotion.HasIncompleteMotion());
    TestRays[2].SceneTime=-1;
    TestFalse(TEXT("unknown scene time preserves prior coverage semantics"),View.HasIncompleteMotion());
    CarlaGpuSensors::FPhysicalHitView Missing(Encoded,TestRays,10,true,true,2.0);
    TestTrue(TEXT("missing geometry still marks moving rays incomplete"),Missing.HasIncompleteMotion());
    TestFalse(TEXT("empty view is valid and complete"),CarlaGpuSensors::FPhysicalHitView().HasIncompleteMotion());
  }
  FLidarDescription D;D.Channels=64;D.Range=15000;D.PointsPerSecond=400000;
  D.RotationFrequency=10;D.HorizontalFov=360;D.RandomSeed=42;
  FPhysicalLidarProfile Profile;FString Error;
  if(!TestTrue(TEXT("generic physical profile loads"),FPhysicalLidarProfile::Load(TEXT("generic"),D.Channels,D.Range,Profile,Error)))return false;
  Profile.EncoderNoiseDeg=.01;Profile.AngularNoiseDeg=.02;
  const auto Samples=beam_samples(Profile.BeamSamples);
  FWeatherParameters Weather;Weather.Wetness=37;
  const FTransform Before(FRotator(3,179,-2),FVector(100000,-23000,170));
  const FTransform After(FRotator(-1,-175,4),FVector(100140,-22970,172));
  TArray<Firing> Scheduled;
  for(int32 I=0;I<4096;++I) { Firing F;F.Id=10000+I;F.Channel=I%64;F.Time=1+I*.1/4096;Scheduled.Add(F); }
  for(bool WarmStart:{false,true}) {
    auto Serial=Scheduled,Parallel=Scheduled;TArray<FCarlaGpuRay> SerialRays,ParallelRays;
    ParallelRays.SetNumUninitialized(Scheduled.Num()*Samples.size()*2);
    FMemory::Memset(ParallelRays.GetData(),0xa5,ParallelRays.Num()*sizeof(FCarlaGpuRay));
    PrepareRays(Serial,SerialRays,Profile,D,Samples,Before,After,Weather,0,1,1.1,12.1,WarmStart,false);
    PrepareRays(Parallel,ParallelRays,Profile,D,Samples,Before,After,Weather,0,1,1.1,12.1,WarmStart,true);
    bool PulsesMatch=Serial.Num()==Parallel.Num(),RaysMatch=SerialRays.Num()==ParallelRays.Num();
    for(int32 I=0;PulsesMatch && I<Serial.Num();++I) {
      const auto& A=Serial[I];const auto& B=Parallel[I];
      PulsesMatch=A.Id==B.Id && A.Channel==B.Channel && A.Time==B.Time && A.FirstRay==B.FirstRay &&
          A.Origin==B.Origin && A.Direction==B.Direction && A.LocalOrigin==B.LocalOrigin && A.LocalDirection==B.LocalDirection &&
          A.Azimuth==B.Azimuth && A.Elevation==B.Elevation && A.Transmission==B.Transmission && A.Scatter==B.Scatter;
    }
    for(int32 I=0;RaysMatch && I<SerialRays.Num();++I) {
      const auto& A=SerialRays[I];const auto& B=ParallelRays[I];
      RaysMatch=A.Origin==B.Origin && A.Direction==B.Direction && A.Range==B.Range && A.MaterialModel==B.MaterialModel &&
          A.AtmosphericAttenuation==B.AtmosphericAttenuation && A.PhysicalModel==B.PhysicalModel && A.TimeFraction==B.TimeFraction &&
          A.TargetMotion==B.TargetMotion && A.WavelengthNm==B.WavelengthNm && A.Wetness==B.Wetness && A.SceneTime==B.SceneTime;
    }
    TestTrue(TEXT("parallel preparation preserves ordered pulse data exactly"),PulsesMatch);
    TestTrue(TEXT("parallel preparation preserves all sub-beam values exactly"),RaysMatch);
  }
  {
    // A worker's local UWorld clock may advance by 0.197 seconds while the
    // replicated episode advances by 0.2. Firing and history must both use
    // the episode endpoints; a local-clock conversion would reject this scan.
    auto ClockPulses=Scheduled;TArray<FCarlaGpuRay> ClockRays;
    PrepareRays(ClockPulses,ClockRays,Profile,D,Samples,Before,After,Weather,0,1,1.1,1.1,false,true);
    bool SameClock=true;
    for(const auto& Pulse:ClockPulses)for(int32 B=0;B<int32(Samples.size());++B)
      SameClock=SameClock && FMath::Abs(ClockRays[Pulse.FirstRay+B].SceneTime-Pulse.Time)<1.e-12;
    TestTrue(TEXT("motion ray times retain the exact episode firing clock"),SameClock);
    TArray<FVector4f> Hits;Hits.SetNumZeroed(ClockRays.Num()*10);
    CarlaGpuSensors::FPhysicalHitView Covered(Hits,ClockRays,10,true,false,1.0);
    TestFalse(TEXT("all early pulses are covered by the previous episode frame"),Covered.HasIncompleteMotion());
    CarlaGpuSensors::FPhysicalHitView LocalClock(Hits,ClockRays,10,true,false,1.0029);
    TestTrue(TEXT("a mismatched local world clock still fails the coverage guard"),LocalClock.HasIncompleteMotion());
  }
  return true;
}
static FAutoConsoleCommand PhysicalRayPreparationTestCommand(
    TEXT("carla.Sensors.TestPhysicalRayPreparation"),TEXT("Run CPU physical ray preparation and material snapshot tests."),
    FConsoleCommandDelegate::CreateLambda([]() {
      auto& Framework=FAutomationTestFramework::Get();
      if(Framework.GetCurrentTest()) {UE_LOG(LogCarla,Warning,TEXT("PHYSICAL_RAY_PREPARATION_TEST busy"));return;}
      Framework.StartTestByName(TEXT("FCarlaPhysicalRayPreparationTest"),0,TEXT("Carla.Sensors.PhysicalRayPreparation"));
      if(!Framework.GetCurrentTest()) {UE_LOG(LogCarla,Warning,TEXT("PHYSICAL_RAY_PREPARATION_TEST result=Fail test_did_not_start"));return;}
      FAutomationTestExecutionInfo Info;
      const bool Passed=Framework.StopTest(Info);
      UE_LOG(LogCarla,Display,TEXT("PHYSICAL_RAY_PREPARATION_TEST result=%s"),Passed?TEXT("Success"):TEXT("Fail"));
    }));
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Carla/Sensor/Tests/PhysicalLidarMaterialFixture.inl"
#include "Carla/Sensor/Tests/LidarSensorStreamingFixture.inl"
#include "Carla/Sensor/Tests/Town02MaterialProbe.inl"
#include "Carla/Sensor/Tests/Town02SensorStream.inl"
#endif
