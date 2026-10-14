// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "SensorManager.h"
#include "Sensor.h"
#include "SceneCaptureCamera.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarCameraCapturePair(
    TEXT("carla.Sensors.CameraCapturePair"), 0,
    TEXT("Opt-in two RGB captures in one view family; preserves separate histories and textures."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarCameraRenderGroup(
    TEXT("carla.Sensors.CameraRenderGroup"), 0,
    TEXT("Opt-in two RGB captures as separate one-view families with grouped renderer lifetime."),
    ECVF_Default);

void FSensorManager::RegisterSensor(ASensor* Sensor)
{
  SensorList.Emplace(Sensor);
}

void FSensorManager::DeRegisterSensor(ASensor* Sensor)
{
  SensorList.Remove(Sensor);
}

void FSensorManager::PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaSeconds)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(FSensorManager::PostPhysTick);
  if (CVarCameraRenderGroup.GetValueOnGameThread() != 0)
  {
    TArray<ASceneCaptureCamera*, TInlineAllocator<2>> Cameras;
    for (ASensor* Sensor : SensorList)
      if (Sensor->ReadyToTick && Sensor->AreClientsListening())
        if (auto* Camera = Cast<ASceneCaptureCamera>(Sensor)) Cameras.Add(Camera);
    if (Cameras.Num() == 2)
      ASceneCaptureSensor::TryCaptureGroup(*Cameras[0], *Cameras[1]);
  }
  else if (CVarCameraCapturePair.GetValueOnGameThread() != 0)
  {
    TArray<ASceneCaptureCamera*, TInlineAllocator<2>> Cameras;
    for (ASensor* Sensor : SensorList)
    {
      if (Sensor->ReadyToTick && Sensor->AreClientsListening())
        if (auto* Camera = Cast<ASceneCaptureCamera>(Sensor)) Cameras.Add(Camera);
    }
    // Unsupported populations/settings use the ordinary per-camera path.
    // Enqueue both renders/copies before either PostPhysTick enqueues readback.
    if (Cameras.Num() == 2)
      ASceneCaptureSensor::TryCapturePair(*Cameras[0], *Cameras[1]);
  }
  for(ASensor* Sensor : SensorList)
  {
    Sensor->PostPhysTickInternal(World, TickType, DeltaSeconds);
  }
}
