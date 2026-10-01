// Copyright (c) 2026. Licensed under the MIT license.
#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Templates/Function.h"
#include "Containers/StaticArray.h"

class FRHICommandListImmediate;
class AActor;

// Positions and distances are in Unreal centimetres. One hardware ray query
// runs per compute thread. The result is against render geometry, not Chaos.
struct FCarlaGpuRay
{
  FVector Origin;
  FVector Direction;
  float Range;
  bool MaterialModel = false;
  float AtmosphericAttenuation = 0.004f;
  bool PhysicalModel = false;
  float TimeFraction = 1.f;
  bool TargetMotion = false;
  float WavelengthNm=905.f, Wetness=0.f;
  double SceneTime=-1;
};

struct FCarlaGpuEcho {
  float Distance = 0, SurfaceReturn = 0;
  uint32 Component = 0, Flags = 0;
};

// Physical receivers consume optical echoes, without an Unreal collision hit.
struct FCarlaPhysicalGpuHit
{
  TStaticArray<FCarlaGpuEcho,8> Echoes;
  uint32 EchoCount = 0;
  float UnoccludedRange = 0;
  bool MotionIncomplete = false;
};

struct FCarlaGpuHit : FCarlaPhysicalGpuHit
{
  FHitResult Hit;
  float SurfaceReturn = 1.f;
  FVector TargetVelocity = FVector::ZeroVector;
};

namespace CarlaGpuSensors
{
  // Borrowed for the synchronous callback only. Decode optical echoes lazily
  // inside the parallel receiver, without a second full-size hit allocation.
  class FPhysicalHitView {
    TConstArrayView<FVector4f> Output;
    TConstArrayView<FCarlaGpuRay> Rays;
    TConstArrayView<FCarlaPhysicalGpuHit> Decoded;
    uint32 Stride=10;
    bool HasMotion=false,MissingGeometry=false;
    double OldestCoveredTime=0;
    bool MotionIncompleteAt(int32 Index) const;
  public:
    FPhysicalHitView()=default;
    explicit FPhysicalHitView(TConstArrayView<FCarlaPhysicalGpuHit> InDecoded):Decoded(InDecoded) {}
    FPhysicalHitView(TConstArrayView<FVector4f> InOutput,TConstArrayView<FCarlaGpuRay> InRays,
        uint32 InStride,bool InHasMotion,bool InMissingGeometry,double InOldestCoveredTime)
        :Output(InOutput),Rays(InRays),Stride(InStride),HasMotion(InHasMotion),
         MissingGeometry(InMissingGeometry),OldestCoveredTime(InOldestCoveredTime) {
      check(Stride>=10 && Output.Num()==Rays.Num()*int32(Stride));
    }
    int32 Num() const { return Output.IsEmpty()?Decoded.Num():Rays.Num(); }
    bool IsValidIndex(int32 Index) const { return Index>=0 && Index<Num(); }
    bool HasIncompleteMotion() const;
    FCarlaPhysicalGpuHit operator[](int32 Index) const;
  };

  // Pure packing; each worker writes four disjoint entries and joins before return.
  void PackRayInputs(TConstArrayView<FCarlaGpuRay> Rays,const FVector& PreViewTranslation,
      bool HasMotion,double MotionTime,TArray<FVector4f>& Input);
  void ProfileSample(const FString& Name, double Milliseconds);
  void Startup();
  void Shutdown();
  bool IsEnabled();
  // Called both by the core ticker and CARLA's synchronous RPC wait loop.
  // Completion is pumped on the game thread even when the client awaits a
  // sensor frame before requesting its next simulation tick.
  void Pump();
  void ScheduleDelivery(AActor& Owner,double DelaySeconds,TUniqueFunction<void()>&& Deliver);
  // Game-thread replica backpressure, before applying the next primary frame.
  bool HasPendingFrames();
  void BeginSensorTick(AActor& Owner);
  void Submit(AActor& Owner, TArray<FCarlaGpuRay>&& Rays,
      TUniqueFunction<void(TArray<FCarlaGpuHit>&&)>&& Complete,bool BuildHitResults=true);
  // Shared immutable rays outlive asynchronous readback and the receiver.
  void SubmitPhysical(AActor& Owner,
      TSharedRef<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe> Rays,
      TUniqueFunction<void(const FPhysicalHitView&)>&& Complete);
  void DecodePhysicalEchoes(FCarlaPhysicalGpuHit& Hit,const FVector4f* Output);


  // Render-thread only. A single shared polling queue replaces per-image
  // blocking queries and sleeping task-graph workers. Return true to retire.
  void PollReadback(TUniqueFunction<bool()>&& Poll);
  void DispatchReadbackCallback(TUniqueFunction<void()>&& Callback);
  // Image conversion can run in the background; UObject access and delivery
  // return to the game thread, including during synchronous RPC waits.
  void EnqueueGameThread(TUniqueFunction<void()>&& Callback);
}
