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
#include "Async/ParallelFor.h"
#include "Carla.h"

using namespace CarlaPhysicalLidar;
namespace {
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
  TArray<Firing> Pulses;TArray<FCarlaGpuRay> Rays;
  const double Expected=FMath::Max(0.0,End-Start)*D.PointsPerSecond;
  if((FMath::CeilToDouble(Expected)+D.Channels)*Samples.size()>2097120) {
    UE_LOG(LogCarla,Error,TEXT("Physical lidar frame exceeds the GPU dispatch bound; reduce points_per_second, beam_samples or sensor_tick"));
    FPhysicalLidarFrame Failed;Failed.Data.Reset(D.Channels,Start,End,Sequence++);Failed.Data.header.flags=16;Complete(MoveTemp(Failed));
    PreviousPose=Current;PreviousTime=End;NextFiring.Reset();return;
  }
  Pulses.Reserve(FMath::CeilToInt(Expected)+D.Channels);Rays.Reserve(FMath::CeilToInt(Expected)*Samples.size()+D.Channels*Samples.size());
  for(uint32 C=0;C<D.Channels;++C) {
    while(NextFiring[C]<End-1.e-10) {
      const double Time=NextFiring[C];NextFiring[C]+=Period;if(Time<Start-1.e-8)continue;
      const double Offset=Value(Profile.FiringOffsetUs,C,double(C)*1.e6/D.PointsPerSecond)*1.e-6;
      const uint64 Column=uint64(FMath::Max(0.0,std::round((Time-Epoch-Offset)/Period)));
      Firing F;F.Id=Column*D.Channels+C;F.Channel=C;F.Time=Time;F.FirstRay=Rays.Num();
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
      for(const auto& Sample:Samples) {
        const double AngularSigma=FMath::DegreesToRadians(Profile.BeamDivergenceDeg)/2.354820045;
        const FVector Local(1,Sample.x*AngularSigma,Sample.y*AngularSigma);
        const FVector Offset=Rotation.RotateVector(FVector(0,Sample.x,Sample.y))*Profile.BeamDiameterM*100/2.354820045;
        FCarlaGpuRay Ray{F.Origin+Offset,Rotation.RotateVector(Local.GetSafeNormal()),D.Range,true,0};
        Ray.PhysicalModel=true;Ray.TimeFraction=Alpha;Ray.TargetMotion=Profile.TargetMotion && !WarmStart;
        Ray.WavelengthNm=Profile.Receiver.wavelength_nm;Ray.Wetness=Weather.Wetness*.01f;Ray.SceneTime=Sensor.GetWorld()->GetTimeSeconds()-(End-Time);
        Rays.Add(Ray);
      }
      Pulses.Add(F);
    }
  }
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
  auto RayCopy=MakeShared<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe>(Rays);
  auto Receive=[Pulses=MoveTemp(Pulses),RayCopy,Samples,ProfileCopy,D,Volumes=MoveTemp(Volumes),Weather,Start,End,WarmStart,PacketSequence,Complete=MoveTemp(Complete)](TArray<FCarlaGpuHit>&& Hits) mutable {
    FPhysicalLidarFrame Frame;Random PacketRandom(uint64(D.RandomSeed)^PacketSequence^UINT64_C(0x215d92410c45af23));
    const double ClockScale=1+ProfileCopy.ClockDriftPpm*1.e-6;
    const double ClockOffset=ProfileCopy.ClockOffsetS+ProfileCopy.ClockJitterNs*1.e-9*PacketRandom.normal();
    Frame.Data.Reset(D.Channels,Start*ClockScale+ClockOffset,End*ClockScale+ClockOffset,PacketSequence);
    Frame.Data.header.flags=(ProfileCopy.CalibrationStatus==TEXT("validated")?1u:0u)|(WarmStart?2u:0u);
    if(Hits.ContainsByPredicate([](const FCarlaGpuHit& H) { return H.MotionIncomplete; }))Frame.Data.header.flags|=8u;
    Frame.Data.header.pulse_count=Pulses.Num();Frame.Data.header.profile_crc=ProfileCopy.Crc;
    Frame.Data.header.wavelength_nm=ProfileCopy.Receiver.wavelength_nm;
    Frame.DeliveryDelayMs=FMath::Max(0.0,ProfileCopy.Receiver.latency_ms+ProfileCopy.Receiver.latency_jitter_ms*PacketRandom.normal());
    if(PacketRandom.uniform()<ProfileCopy.Receiver.packet_loss_probability) {
      Frame.Data.header.flags|=4;Complete(MoveTemp(Frame));return;
    }
    TArray<std::vector<carla::sensor::data::PhysicalLidarDetection>> Results;Results.SetNum(Pulses.Num());
    ParallelFor(Pulses.Num(),[&](int32 I) {
      const auto& F=Pulses[I];std::vector<Echo> Echoes;
      const double Rain=FMath::Max(ProfileCopy.RainMmH,CarlaLidarSceneWeather::rain_mm_h(Weather.Precipitation));
      // Rain law is an empirical generic approximation; fog uses visibility to extinction.
      const double Extinction=FMath::Max(0.0,double(D.AtmospAttenRate))+
          CarlaLidarSceneWeather::rain_extinction(Rain)+(ProfileCopy.FogVisibilityM>0?3.912/ProfileCopy.FogVisibilityM:0)+
          ProfileCopy.SmokeExtinction+ProfileCopy.DustExtinction+ProfileCopy.SnowExtinction+
          CarlaLidarSceneWeather::dust_extinction(Weather.DustStorm);
      const double SceneFog=CarlaLidarSceneWeather::fog_extinction(Weather.FogDensity);
      const double FogStart=FMath::Clamp(double(Weather.FogDistance),0.0,double(D.Range)*.01);
      const double Backscatter=FMath::Max(0.0,Extinction-FMath::Max(0.0,double(D.AtmospAttenRate)))*.15;
      for(int S=0;S<int(Samples.size());++S) {
        const int Index=F.FirstRay+S;if(!Hits.IsValidIndex(Index))continue;
        const auto& H=Hits[Index];const auto& Ray=(*RayCopy)[Index];std::vector<Echo> Sub;
        for(uint32 E=0;E<H.EchoCount;++E) {
          const auto& V=H.Echoes[E];Sub.push_back({V.Distance*.01,V.SurfaceReturn*Samples[S].weight,V.Flags,V.Component});
        }
        std::vector<MediumSegment> Medium{{0,D.Range*.01,Extinction,Backscatter}};
        // Scene fog starts at the same distance used by the camera renderer.
        // Weather is snapshotted every scan; existing sensors update in place.
        if(SceneFog>0)Medium.push_back({FogStart,D.Range*.01,SceneFog,SceneFog*.15});
        for(const auto& V:Volumes) { double Begin,Finish;if(VolumeInterval(V,Ray.Origin,Ray.Direction,D.Range*.01,Begin,Finish))Medium.push_back({Begin,Finish,V.Extinction,V.Backscatter}); }
        Random WeatherRandom(uint64(D.RandomSeed)^F.Id^(UINT64_C(0x7b23ad819657d8c1)*(S+1)));
        propagate_medium(Sub,Medium,H.UnoccludedRange*.01,Samples[S].weight,WeatherRandom);
        for(auto& E:Sub) { E.response*=F.Transmission*F.Transmission*Value(ProfileCopy.ChannelGain,F.Channel,1);Echoes.push_back(E); }
      }
      if(F.Scatter>0)Echoes.push_back({.2,F.Scatter,Cover,0});
      Receiver ReceiverCopy=ProfileCopy.Receiver;ReceiverCopy.range_bias_m+=Value(ProfileCopy.ChannelRangeBiasM,F.Channel,0);
      double Lux=ProfileCopy.AmbientLux;
      if(ProfileCopy.AmbientFromWeather) Lux=100000*FMath::Max(0.0,std::sin(FMath::DegreesToRadians(double(Weather.SunAltitudeAngle))))*(1-.8*FMath::Clamp(double(Weather.Cloudiness)*.01,0.0,1.0));
      const FVector SunDirection=FRotator(Weather.SunAltitudeAngle,Weather.SunAzimuthAngle,0).Vector();
      const double Direct=Weather.SunAltitudeAngle>0?ProfileCopy.DirectSunGain*std::exp((FVector::DotProduct(F.Direction,SunDirection)-1)/.002):0;
      ReceiverCopy.background_photons+=Lux*.001*ProfileCopy.BackgroundPerKlux*(1+Direct);
      Random DetectionRandom(uint64(D.RandomSeed)^F.Id^UINT64_C(0xa42b507ce5390217));
      auto Detected=detect(MoveTemp(Echoes),ReceiverCopy,DetectionRandom);
      for(size_t R=0;R<Detected.size();++R) {
        const auto& V=Detected[R];carla::sensor::data::PhysicalLidarDetection Point;
        const FVector Local=F.LocalOrigin*.01+F.LocalDirection*V.range_m;
        Point.x=Local.X;Point.y=Local.Y;Point.z=Local.Z;Point.intensity=V.reflectivity;
        Point.range=V.range_m;Point.signal=V.signal;Point.ambient=V.ambient;Point.pulse_width=V.pulse_width_ns;
        Point.azimuth=FMath::DegreesToRadians(F.Azimuth);Point.elevation=FMath::DegreesToRadians(F.Elevation);
        Point.time_offset=(F.Time-Start)*ClockScale;Point.confidence=V.confidence;
        Point.pulse_id=F.Id;Point.channel=F.Channel;Point.return_id=R;Point.return_count=Detected.size();Point.flags=V.flags;
        Results[I].push_back(Point);
      }
    });
    for(auto& Points:Results)Frame.Data.points.insert(Frame.Data.points.end(),Points.begin(),Points.end());
    Frame.Data.header.point_count=Frame.Data.points.size();
    if(!Pulses.IsEmpty())Frame.Data.header.horizontal_angle=FMath::DegreesToRadians(Pulses.Last().Azimuth);
    Complete(MoveTemp(Frame));
  };
  if(CarlaGpuSensors::IsEnabled())CarlaGpuSensors::Submit(Sensor,MoveTemp(Rays),MoveTemp(Receive));
  else {
    // CPU compatibility uses the existing collision/static-render optics path.
    // The GPU path additionally interpolates deforming render meshes.
    CarlaLidarOptics::Prepare(Sensor.GetWorld());TArray<FCarlaGpuHit> Hits;Hits.SetNum(Rays.Num());
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
        if(HasProxy || Solid || Materials.ContainsByPredicate([](const CarlaLidarOptics::Material& M) { return M.transmission>0 || M.specular>.5f || M.retro>0; }))Components.Add(C);
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
    });Receive(MoveTemp(Hits));
  }
}
