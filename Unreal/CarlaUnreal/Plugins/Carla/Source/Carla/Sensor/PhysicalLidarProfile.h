// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include "CoreMinimal.h"
#include "Carla/Sensor/PhysicalLidarModel.h"

struct FPhysicalLidarVolume {
  FTransform Transform;
  FVector Extent = FVector(100); // UE cm, transform scale is always one
  double Extinction = 0, Backscatter = 0;
};
struct FPhysicalLidarCover {
  double AzimuthMin = -180, AzimuthMax = 180, ElevationMin = -90, ElevationMax = 90;
  double Transmission = 1, Scatter = 0;
};
struct FPhysicalLidarProfile {
  CarlaPhysicalLidar::Receiver Receiver;
  FString Name, Model = TEXT("generic-pulsed-tof"), CalibrationStatus = TEXT("uncalibrated");
  uint32 Crc = 0;
  int BeamSamples = 7;
  double BeamDiameterM = .008, BeamDivergenceDeg = .12, AngularNoiseDeg = .005;
  double EncoderBiasDeg = 0, EncoderNoiseDeg = .002;
  bool MotionDistortion = true, TargetMotion = true;
  double ClockOffsetS = 0, ClockDriftPpm = 0, ClockJitterNs = 0;
  double FogVisibilityM = 0, RainMmH = 0, SmokeExtinction = 0;
  double DustExtinction = 0, SnowExtinction = 0;
  double AmbientLux = 10000, BackgroundPerKlux = .1, DirectSunGain = 4;
  bool AmbientFromWeather = true, RoadSpray = true;
  double SprayExtinction = .05;
  TArray<double> ElevationDeg, AzimuthOffsetDeg, FiringOffsetUs, ChannelRangeBiasM, ChannelGain;
  TArray<FVector> BeamOriginsM;
  TArray<FPhysicalLidarVolume> Volumes;
  TArray<FPhysicalLidarCover> Cover;
  // Profile name is a basename in Content/Carla/Config/Lidar, never a user-controlled path.
  static bool Load(const FString& Name, uint32 Channels, float RangeCm, FPhysicalLidarProfile& Out, FString& Error);
};
