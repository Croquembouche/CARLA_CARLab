// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/PhysicalLidarProfile.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"

namespace {
bool Number(const TSharedPtr<FJsonObject>& J, const TCHAR* Key, double& Value, double Low, double High, FString& Error) {
  if (!J->HasField(Key)) return true;
  double V;
  if (!J->TryGetNumberField(Key,V) || !FMath::IsFinite(V) || V<Low || V>High) {
    Error=FString::Printf(TEXT("%s must be finite in [%g,%g]"),Key,Low,High);return false;
  }
  Value=V;return true;
}
bool Boolean(const TSharedPtr<FJsonObject>& J, const TCHAR* Key, bool& Value,FString& Error) {
  if (!J->HasField(Key)) return true;
  if (!J->TryGetBoolField(Key,Value)) { Error=FString(Key)+TEXT(" must be boolean");return false; }return true;
}
bool Array(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,uint32 Count,TArray<double>& Values,double Low,double High,FString& Error) {
  if (!J->HasField(Key)) return true;
  const TArray<TSharedPtr<FJsonValue>>* A;
  if (!J->TryGetArrayField(Key,A) || A->Num()!=int32(Count)) { Error=FString(Key)+TEXT(" must contain one value per channel");return false; }
  for (const auto& Entry:*A) {
    double V;if (!Entry->TryGetNumber(V) || !FMath::IsFinite(V) || V<Low || V>High) { Error=FString(Key)+TEXT(" contains an invalid value");return false; }
    Values.Add(V);
  }return true;
}
bool Vector(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,FVector& Value,FString& Error) {
  if (!J->HasField(Key)) return true;
  TArray<double> V;if (!Array(J,Key,3,V,-1.e6,1.e6,Error)) return false;
  Value=FVector(V[0],V[1],V[2]);return true;
}
}
bool FPhysicalLidarProfile::Load(const FString& ProfileName,uint32 Channels,float RangeCm,FPhysicalLidarProfile& Out,FString& Error) {
  Out=FPhysicalLidarProfile();Out.Name=ProfileName;
  if (Channels<1 || Channels>4096 || !FMath::IsFinite(RangeCm) || RangeCm<=0 || RangeCm>100000) { Error=TEXT("Invalid physical lidar channel count or range");return false; }
  if (ProfileName.IsEmpty() || ProfileName.Len()>80) { Error=TEXT("Invalid physical lidar profile name");return false; }
  for (TCHAR C:ProfileName) if (!FChar::IsAlnum(C) && C!='_' && C!='-') { Error=TEXT("Physical lidar profile must be a simple basename");return false; }
  FString Text;
  const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Carla/Config/Lidar"),ProfileName+TEXT(".json"));
  if (!FFileHelper::LoadFileToString(Text,*Path)) { Error=TEXT("Cannot read physical lidar profile: ")+ProfileName;return false; }
  if (Text.Len()>2*1024*1024) { Error=TEXT("Physical lidar profile exceeds 2 MiB");return false; }
  TSharedPtr<FJsonObject> J;
  if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J) || !J.IsValid()) { Error=TEXT("Invalid physical lidar profile JSON");return false; }
  double Version=0;if (!J->TryGetNumberField(TEXT("version"),Version) || Version!=1) { Error=TEXT("Unsupported physical lidar profile version");return false; }
  Out.Crc=FCrc::StrCrc32(*Text);J->TryGetStringField(TEXT("model"),Out.Model);
  const TSharedPtr<FJsonObject>* Calibration;
  if (J->TryGetObjectField(TEXT("calibration"),Calibration)) {
    (*Calibration)->TryGetStringField(TEXT("status"),Out.CalibrationStatus);
    if (Out.CalibrationStatus!=TEXT("uncalibrated") && Out.CalibrationStatus!=TEXT("fitted") && Out.CalibrationStatus!=TEXT("validated")) { Error=TEXT("Invalid calibration status");return false; }
    FString Evidence;
    if (Out.CalibrationStatus!=TEXT("uncalibrated") && (!(*Calibration)->TryGetStringField(TEXT("evidence_sha256"),Evidence) || Evidence.Len()!=64)) {
      Error=TEXT("A fitted/validated profile requires the calibration evidence SHA256");return false;
    }
  }
  auto& R=Out.Receiver;R.maximum_range_m=RangeCm*.01;
  const TSharedPtr<FJsonObject>* ReceiverObject;TSharedPtr<FJsonObject> RJ=MakeShared<FJsonObject>();
  if (J->HasField(TEXT("receiver")) && !J->TryGetObjectField(TEXT("receiver"),ReceiverObject)) { Error=TEXT("receiver must be an object");return false; }
  if (J->TryGetObjectField(TEXT("receiver"),ReceiverObject)) RJ=*ReceiverObject;
#define N(KEY,FIELD,LOW,HIGH) if (!Number(RJ,TEXT(KEY),R.FIELD,LOW,HIGH,Error)) return false
  N("wavelength_nm",wavelength_nm,300,2000);N("pulse_energy_nj",pulse_energy_nj,.0001,100000);
  N("aperture_diameter_m",aperture_diameter_m,.0001,1);N("efficiency",efficiency,.000001,1);
  N("pulse_fwhm_ns",pulse_fwhm_ns,.01,1000);N("overlap_distance_m",overlap_distance_m,0,100);
  N("minimum_range_m",minimum_range_m,0,1000);N("background_photons",background_photons,0,1.e9);
  N("electronic_noise_photons",electronic_noise_photons,0,1.e6);N("detection_sigma",detection_sigma,0,20);
  N("minimum_signal_photons",minimum_signal_photons,0,1.e9);N("saturation_photons",saturation_photons,1,1.e12);
  N("range_noise_floor_m",range_noise_floor_m,0,10);N("range_bias_m",range_bias_m,-10,10);
  N("time_walk_m",time_walk_m,-10,10);N("saturation_bias_m",saturation_bias_m,-10,10);
  N("range_quantization_m",range_quantization_m,0,10);N("echo_separation_m",echo_separation_m,.001,100);
  N("temperature_c",temperature_c,-100,150);N("reference_temperature_c",reference_temperature_c,-100,150);
  N("temperature_bias_m_per_c",temperature_bias_m_per_c,-1,1);
  N("packet_loss_probability",packet_loss_probability,0,1);N("latency_ms",latency_ms,0,10000);
  N("latency_jitter_ms",latency_jitter_ms,0,10000);
#undef N
  double Returns=R.max_returns;if (!Number(RJ,TEXT("max_returns"),Returns,1,4,Error) || Returns!=FMath::FloorToDouble(Returns)) { Error=TEXT("max_returns must be an integer from 1 to 4");return false; }R.max_returns=int(Returns);
  FString Order=TEXT("strongest");RJ->TryGetStringField(TEXT("return_order"),Order);
  if (Order!=TEXT("strongest") && Order!=TEXT("nearest") && Order!=TEXT("farthest")) { Error=TEXT("return_order must be strongest, nearest or farthest");return false; }
  R.return_order=Order==TEXT("nearest")?1:Order==TEXT("farthest")?2:0;
  if (!Boolean(RJ,TEXT("shot_noise"),R.shot_noise,Error) || !Boolean(RJ,TEXT("false_alarms"),R.false_alarms,Error)) return false;
  if (R.minimum_range_m>=R.maximum_range_m) { Error=TEXT("minimum_range_m must be less than sensor range");return false; }
#define P(KEY,FIELD,LOW,HIGH) if (!Number(J,TEXT(KEY),Out.FIELD,LOW,HIGH,Error)) return false
  P("beam_diameter_m",BeamDiameterM,0,.5);P("beam_divergence_deg",BeamDivergenceDeg,0,10);
  P("angular_noise_deg",AngularNoiseDeg,0,10);P("encoder_bias_deg",EncoderBiasDeg,-180,180);P("encoder_noise_deg",EncoderNoiseDeg,0,10);
  P("clock_offset_s",ClockOffsetS,-1.e6,1.e6);P("clock_drift_ppm",ClockDriftPpm,-10000,10000);P("clock_jitter_ns",ClockJitterNs,0,1.e9);
  P("fog_visibility_m",FogVisibilityM,0,1.e6);P("rain_mm_h",RainMmH,0,500);
  P("smoke_extinction_m_inv",SmokeExtinction,0,10);P("dust_extinction_m_inv",DustExtinction,0,10);P("snow_extinction_m_inv",SnowExtinction,0,10);
  P("ambient_lux",AmbientLux,0,1.e6);P("background_photons_per_klux",BackgroundPerKlux,0,1.e6);P("direct_sun_gain",DirectSunGain,0,1000);
  P("spray_extinction_m_inv",SprayExtinction,0,10);
#undef P
  double Samples=Out.BeamSamples;if (!Number(J,TEXT("beam_samples"),Samples,1,33,Error) || Samples!=FMath::FloorToDouble(Samples)) { Error=TEXT("beam_samples must be an integer from 1 to 33");return false; }Out.BeamSamples=int(Samples);
  if (!Boolean(J,TEXT("motion_distortion"),Out.MotionDistortion,Error) || !Boolean(J,TEXT("target_motion"),Out.TargetMotion,Error) ||
      !Boolean(J,TEXT("ambient_from_weather"),Out.AmbientFromWeather,Error) || !Boolean(J,TEXT("road_spray"),Out.RoadSpray,Error)) return false;
  if (!Array(J,TEXT("elevation_deg"),Channels,Out.ElevationDeg,-90,90,Error) ||
      !Array(J,TEXT("azimuth_offset_deg"),Channels,Out.AzimuthOffsetDeg,-180,180,Error) ||
      !Array(J,TEXT("firing_offset_us"),Channels,Out.FiringOffsetUs,0,1.e6,Error) ||
      !Array(J,TEXT("channel_range_bias_m"),Channels,Out.ChannelRangeBiasM,-10,10,Error) ||
      !Array(J,TEXT("channel_gain"),Channels,Out.ChannelGain,.001,1000,Error)) return false;
  const TArray<TSharedPtr<FJsonValue>>* Origins;
  if (J->HasField(TEXT("beam_origins_m"))) {
    if (!J->TryGetArrayField(TEXT("beam_origins_m"),Origins) || Origins->Num()!=int32(Channels)) { Error=TEXT("beam_origins_m must contain one XYZ array per channel");return false; }
    for (const auto& Entry:*Origins) {
      const TArray<TSharedPtr<FJsonValue>>* V;if (!Entry->TryGetArray(V) || V->Num()!=3) { Error=TEXT("Invalid beam origin");return false; }
      FVector Point;for (int I=0;I<3;++I) { double Value;if (!(*V)[I]->TryGetNumber(Value) || !FMath::IsFinite(Value) || FMath::Abs(Value)>10) { Error=TEXT("Invalid beam origin coordinate");return false; }Point[I]=Value; }
      Out.BeamOriginsM.Add(Point);
    }
  }
  const TArray<TSharedPtr<FJsonValue>>* Volumes;
  if (J->HasField(TEXT("volumes"))) {
    if (!J->TryGetArrayField(TEXT("volumes"),Volumes) || Volumes->Num()>64) { Error=TEXT("volumes must be an array with at most 64 entries");return false; }
    for (const auto& Value:*Volumes) {
      const TSharedPtr<FJsonObject>* V;if (!Value->TryGetObject(V)) { Error=TEXT("Invalid medium volume");return false; }
      FPhysicalLidarVolume Volume;FVector Position(0),Rotation(0),Extent(1);
      if (!Vector(*V,TEXT("position_m"),Position,Error) || !Vector(*V,TEXT("rotation_deg"),Rotation,Error) || !Vector(*V,TEXT("extent_m"),Extent,Error) ||
          !Number(*V,TEXT("extinction_m_inv"),Volume.Extinction,0,10,Error) || !Number(*V,TEXT("backscatter_m_inv"),Volume.Backscatter,0,10,Error)) return false;
      if (Extent.GetMin()<=0) { Error=TEXT("Medium volume extent must be positive");return false; }
      Volume.Transform=FTransform(FRotator(Rotation.X,Rotation.Y,Rotation.Z),Position*100);Volume.Extent=Extent*100;Out.Volumes.Add(Volume);
    }
  }
  const TArray<TSharedPtr<FJsonValue>>* Covers;
  if (J->HasField(TEXT("cover"))) {
    if (!J->TryGetArrayField(TEXT("cover"),Covers) || Covers->Num()>128) { Error=TEXT("cover must be an array with at most 128 entries");return false; }
    for (const auto& Value:*Covers) {
      const TSharedPtr<FJsonObject>* V;if (!Value->TryGetObject(V)) { Error=TEXT("Invalid cover sector");return false; }
      FPhysicalLidarCover C;
      if (!Number(*V,TEXT("azimuth_min_deg"),C.AzimuthMin,-180,180,Error) || !Number(*V,TEXT("azimuth_max_deg"),C.AzimuthMax,-180,180,Error) ||
          !Number(*V,TEXT("elevation_min_deg"),C.ElevationMin,-90,90,Error) || !Number(*V,TEXT("elevation_max_deg"),C.ElevationMax,-90,90,Error) ||
          !Number(*V,TEXT("transmission"),C.Transmission,0,1,Error) || !Number(*V,TEXT("scatter"),C.Scatter,0,1,Error)) return false;
      if (C.ElevationMin>C.ElevationMax || C.Transmission+C.Scatter>1.000001) { Error=TEXT("Invalid cover bounds or energy fractions");return false; }
      Out.Cover.Add(C);
    }
  }
  return true;
}
