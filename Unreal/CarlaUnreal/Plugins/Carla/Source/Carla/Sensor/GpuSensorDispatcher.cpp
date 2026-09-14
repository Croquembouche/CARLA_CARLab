// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "GameFramework/Actor.h"
#include "Carla.h"
#include "Carla/Sensor/LidarOptics.h"
#include "Carla/Sensor/LidarMotion.h"
#include "Carla/Sensor/LidarInfrared.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "LandscapeComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "FXRenderingUtils.h"
#include "GlobalShader.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "RendererInterface.h"
#include "ExternalRayTracingQueries.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "SceneView.h"
#include "SceneRendererInterface.h"
#include "SceneUniformBuffer.h"
#include "ShaderParameterStruct.h"
#include "RenderUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ScopeLock.h"

namespace CarlaGpuSensorPrivate
{
TAutoConsoleVariable<int32> Enabled(TEXT("carla.Sensors.GpuRayTracing"), 0,
    TEXT("Experimental render-geometry GPU LiDAR/radar. Requires patched UE, inline RT, and a rendered view. No CPU fallback."));

class FCarlaSensorTraceCS : public FGlobalShader
{
  DECLARE_GLOBAL_SHADER(FCarlaSensorTraceCS);
  SHADER_USE_PARAMETER_STRUCT(FCarlaSensorTraceCS, FGlobalShader);
  BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
    SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
    SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Rays)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, CollisionTable)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, OpticalMaterials)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InfraredInfo)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InfraredUVs)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, InfraredPixels)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, MotionInfo)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, MotionNodes)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, MotionTriangles)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, MotionLookup)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, Hits)
    SHADER_PARAMETER(uint32, RayCount)
    SHADER_PARAMETER(uint32, TableMask)
    SHADER_PARAMETER(uint32, IgnoredActor)
    SHADER_PARAMETER(uint32, HitStride)
    SHADER_PARAMETER(uint32, MotionCount)
    SHADER_PARAMETER(uint32, MotionMask)
  END_SHADER_PARAMETER_STRUCT()
  static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
  {
    return IsRayTracingEnabledForProject(P.Platform) && RHISupportsInlineRayTracing(P.Platform);
  }
  static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& P, FShaderCompilerEnvironment& E)
  {
    FGlobalShader::ModifyCompilationEnvironment(P, E);
    E.CompilerFlags.Add(CFLAG_InlineRayTracing);
    E.CompilerFlags.Add(CFLAG_Wave32);
    E.SetDefine(TEXT("RAY_TRACING_THREAD_GROUP_SIZE_X"), 32);
    E.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
  }
};
IMPLEMENT_GLOBAL_SHADER(FCarlaSensorTraceCS, "/Plugin/Carla/Private/CarlaSensorTrace.usf", "MainCS", SF_Compute);

TAutoConsoleVariable<int32> Diagnostics(TEXT("carla.Sensors.GpuDiagnostics"), 0,
    TEXT("Log vehicle and pedestrian render component collision eligibility once per component."));
TSet<uint32> DiagnosedComponents;

struct FComponentSnapshot
{
  TWeakObjectPtr<UPrimitiveComponent> Component;
  FVector Velocity;
  uint32 Actor;
  uint32 MaterialOffset, MaterialCount;
  bool OpticalOnly;
};
struct FCollisionSnapshot
{
  TMap<uint32, FComponentSnapshot> Components;
  TArray<FUintVector4> Table;
  TArray<FVector4f> Materials,InfraredInfo,InfraredUVs,InfraredPixels;
  TArray<TWeakObjectPtr<UPrimitiveComponent>> RenderComponents;
};
struct FBatch
{
  TWeakObjectPtr<AActor> Owner;
  const FSceneInterface* Scene = nullptr; // Identity only; never dereferenced.
  uint32 Actor = 0;
  double Submitted = 0;
  FRenderQueryRHIRef QueryStart, QueryEnd;
  double GpuMs=-1, Dispatched=0, Ready=0;
  TArray<FCarlaGpuRay> Rays;
  TSharedPtr<FCollisionSnapshot, ESPMode::ThreadSafe> Collision;
  TArray<FVector4f> Output;
  TUniqueFunction<void(TArray<FCarlaGpuHit>&&)> Complete;
  TUniquePtr<FRHIGPUBufferReadback> Readback;
  uint32 HitStride = 2;
  TSharedPtr<const CarlaLidarMotion::Snapshot,ESPMode::ThreadSafe> Motion;
};
using FBatchPtr = TSharedPtr<FBatch, ESPMode::ThreadSafe>;
TArray<FBatchPtr> Pending; // Render thread only.
TSet<const FSceneInterface*> QueryScenes; // Render thread only; cleared on shutdown.
TMap<const FSceneInterface*, FSphere> FrameQueryBounds; // Game thread only.
uint64 QueryBoundsFrame = MAX_uint64;
TArray<TUniqueFunction<bool()>> Readbacks; // Render thread only.
TAtomic<int32> ReadbackCount{0};
FGraphEventArray CallbackTasks; // Render thread; joined after the shutdown fence.
FCriticalSection ProfileMutex;
FGraphEventRef ProfileWriteTask; // One best-effort writer, joined on shutdown.
TQueue<FBatchPtr, EQueueMode::Mpsc> Completed;
TQueue<TUniqueFunction<void()>, EQueueMode::Mpsc> GameCallbacks;
struct FDelayedDelivery { TWeakObjectPtr<AActor> Owner; double Due; TUniqueFunction<void()> Deliver; };
TArray<FDelayedDelivery> DelayedDeliveries;
TMap<TWeakObjectPtr<AActor>, double> InFlight; // Game thread only.
TWeakObjectPtr<UWorld> CachedWorld;
uint64 CachedFrame = MAX_uint64;
TSharedPtr<FCollisionSnapshot, ESPMode::ThreadSafe> CachedCollision;
FDelegateHandle RenderHandle;
FDelegateHandle PreExitHandle;
FTSTicker::FDelegateHandle TickHandle;
bool Running = false;
double LastPoll = 0;

TSharedPtr<FCollisionSnapshot, ESPMode::ThreadSafe> Snapshot(UWorld* World)
{
  if (CachedWorld == World && CachedFrame == GFrameCounter) return CachedCollision;
  auto Result = MakeShared<FCollisionSnapshot, ESPMode::ThreadSafe>();
  // Once per world/frame, not once per ray. Capture velocity alongside IDs so
  // delayed radar readback never samples velocity from a later physics tick.
  for (TActorIterator<AActor> Actor(World); Actor; ++Actor)
  {
    TInlineComponentArray<UPrimitiveComponent*> Components;
    Actor->GetComponents(Components);
    const FVector Velocity = Actor->GetVelocity();
    // CARLA vehicle blueprints keep their sensor collision on a separate,
    // usually hidden CustomSensorCollision mesh. Their visible body deliberately
    // ignores SensorTrace. Associate render meshes with that explicit proxy,
    // just as landscapes associate their render and heightfield components.
    // An ambiguous actor with multiple proxies keeps component-local filtering.
    UPrimitiveComponent* SensorProxy = nullptr;
    int32 SensorProxyCount = 0;
    for (UPrimitiveComponent* Candidate : Components)
    {
      if (Candidate->IsRegistered() && Candidate->GetCollisionProfileName() == FName(TEXT("CustomSensorCollision")))
      {
        SensorProxy = Candidate;
        ++SensorProxyCount;
      }
    }
    if (SensorProxyCount != 1 || !SensorProxy->IsQueryCollisionEnabled() ||
        SensorProxy->GetCollisionResponseToChannel(ECC_GameTraceChannel2) != ECR_Block)
      SensorProxy = nullptr;
    for (UPrimitiveComponent* C : Components)
    {
      // Landscapes use separate render and collision components. Key the GPU
      // table by the rendered primitive, while honoring the heightfield's
      // collision settings and retaining its component for semantic decoding.
      UPrimitiveComponent* CollisionComponent = C;
      if (auto* Landscape = Cast<ULandscapeComponent>(C))
        CollisionComponent = Landscape->GetCollisionComponent();
      else if (SensorProxy && (C->IsA<USkinnedMeshComponent>() || C->IsA<UStaticMeshComponent>()) &&
               (!C->IsQueryCollisionEnabled() || C->GetCollisionResponseToChannel(ECC_GameTraceChannel2) != ECR_Block))
        CollisionComponent = SensorProxy;
      if (Diagnostics.GetValueOnGameThread() && !DiagnosedComponents.Contains(C->GetUniqueID()) &&
          (SensorProxy || Actor->GetName().Contains(TEXT("vehicle"), ESearchCase::IgnoreCase) ||
           Actor->GetName().Contains(TEXT("walker"), ESearchCase::IgnoreCase) ||
           C->IsA<USkinnedMeshComponent>()))
      {
        DiagnosedComponents.Add(C->GetUniqueID());
        if (auto* Skinned = Cast<USkinnedMeshComponent>(C))
        {
          auto* Asset = Skinned->GetSkinnedAsset();
          UE_LOG(LogCarla, Display, TEXT("GPU SENSOR SKIN actor=%s asset=%s supports_rt=%d visible_rt=%d anim_tick=%d"),
              *Actor->GetName(), *GetNameSafe(Asset), Asset && Asset->GetSupportRayTracing(),
              Skinned->bVisibleInRayTracing, int32(Skinned->VisibilityBasedAnimTickOption));
        }
        UE_LOG(LogCarla, Display, TEXT("GPU SENSOR COMPONENT actor=%s component=%s class=%s primitive=%u registered=%d query=%d lidar_response=%d collision_component=%s"),
            *Actor->GetName(), *C->GetName(), *C->GetClass()->GetName(), C->GetPrimitiveSceneId().PrimIDValue,
            C->IsRegistered(), CollisionComponent && CollisionComponent->IsQueryCollisionEnabled(),
            CollisionComponent ? int32(CollisionComponent->GetCollisionResponseToChannel(ECC_GameTraceChannel2)) : -1, *GetNameSafe(CollisionComponent));
      }
      if (!C->IsRegistered()) continue;
      const bool CollisionEligible=CollisionComponent && CollisionComponent->IsRegistered() &&
          CollisionComponent->IsQueryCollisionEnabled() &&
          CollisionComponent->GetCollisionResponseToChannel(ECC_GameTraceChannel2)==ECR_Block;
      const auto Materials=CarlaLidarOptics::Sections(C);
      // Visible optical surfaces can be non-solid (windows and painted road
      // meshes). Include them only for material-aware LiDAR, preserving the
      // existing collision predicate for radar and semantic/legacy LiDAR.
      const bool OpticalEligible=Actor->GetActorEnableCollision() && !Actor->IsHidden() && C->IsVisible() && !C->bHiddenInGame &&
          !C->ComponentHasTag(TEXT("LidarIgnore")) && Materials.ContainsByPredicate([](const CarlaLidarOptics::Material& M) {
            return M.transmission>0 || M.specular>.5f || M.retro>0;
          });
      if (!CollisionEligible && !OpticalEligible) continue;
      if (!CollisionEligible) CollisionComponent=C;
      const uint32 Id = C->GetPrimitiveSceneId().PrimIDValue;
      if (Id) {
        Result->RenderComponents.Add(C);
        const uint32 Offset=Result->Materials.Num()/2;
        uint32 SectionIndex=0;
        for (const auto& M:Materials) {
          CarlaLidarInfrared::Append(C,SectionIndex++,M.infrared,Result->InfraredInfo,Result->InfraredUVs);
          Result->Materials.Add(FVector4f(M.diffuse,M.retro,M.specular,M.transmission));
          Result->Materials.Add(FVector4f(M.ior,M.roughness,M.thinSheet,0));
        }
        Result->Components.Add(Id, {CollisionComponent, Velocity, Actor->GetUniqueID(),Offset,uint32(Materials.Num()),!CollisionEligible});
      }
    }
  }
  const uint32 Size = FMath::RoundUpToPowerOfTwo(FMath::Max(2, Result->Components.Num() * 2));
  Result->Table.SetNumZeroed(Size);
  for (const auto& Entry : Result->Components)
  {
    uint32 Slot = (Entry.Key * 2654435761u) & (Size - 1);
    while (Result->Table[Slot].X != 0) Slot = (Slot + 1) & (Size - 1);
    Result->Table[Slot] = FUintVector4(Entry.Key, Entry.Value.Actor, Entry.Value.MaterialOffset, Entry.Value.MaterialCount | (Entry.Value.OpticalOnly ? 0x80000000u : 0u));
  }
  if (Result->Materials.IsEmpty()) Result->Materials.SetNumZeroed(2);
  if(Result->InfraredInfo.IsEmpty())Result->InfraredInfo.SetNumZeroed(6);
  if(Result->InfraredUVs.IsEmpty())Result->InfraredUVs.SetNumZeroed(1);
  CarlaLidarInfrared::Prepare();Result->InfraredPixels=CarlaLidarInfrared::Pixels();
  CachedWorld = World;
  CachedFrame = GFrameCounter;
  CachedCollision = Result;
  return Result;
}

void Render(FPostOpaqueRenderParameters& P)
{
#if RHI_RAYTRACING
  const FSceneView* View = P.SceneView;
  if (!View || !View->Family || View->bIsSceneCapture || View->bIsReflectionCapture || !P.GraphBuilder || Pending.IsEmpty()) return;
  const auto* Scene = View->Family->Scene;
  if (!UE::FXRenderingUtils::RayTracing::HasRayTracingScene(Scene)) return;
  FRDGBuilder& Graph = *P.GraphBuilder;
  for (int32 Index = 0; Index < Pending.Num();)
  {
    FBatchPtr Batch = Pending[Index];
    if (Batch->Scene != Scene) { ++Index; continue; }
    Pending.RemoveAt(Index);
    TArray<FVector4f> Input;
    Input.Reserve(Batch->Rays.Num() * 4);
    for (const auto& Ray : Batch->Rays)
    {
      // Translate while still in double precision. Converting absolute world
      // positions to float first loses small distance differences on large maps.
      const FVector TranslatedOrigin = Ray.Origin + View->ViewMatrices.GetPreViewTranslation();
      Input.Add(FVector4f(FVector3f(TranslatedOrigin), Ray.Range));
      Input.Add(FVector4f(FVector3f(Ray.Direction), Ray.MaterialModel ? Ray.AtmosphericAttenuation+1.f : 0.f));
      Input.Add(FVector4f(Ray.PhysicalModel?1.f:0.f,Ray.TimeFraction,Ray.TargetMotion?1.f:0.f,0));
      Input.Add(FVector4f(Ray.WavelengthNm,Ray.Wetness,Ray.SceneTime>=0 && Batch->Motion?float(Ray.SceneTime-Batch->Motion->Time):FLT_MAX,0));
    }
    auto* Params = Graph.AllocParameters<FCarlaSensorTraceCS::FParameters>();
    Params->View = View->ViewUniformBuffer;
    Params->Scene = GetSceneUniformBufferRef(Graph, *View);
    Params->TLAS = UE::FXRenderingUtils::RayTracing::GetRayTracingSceneView(Graph.RHICmdList, Scene);
    Params->RayCount = Batch->Rays.Num();
    Params->TableMask = Batch->Collision->Table.Num() - 1;
    Params->IgnoredActor = Batch->Actor;
    Params->HitStride = Batch->HitStride;
    TArray<FVector4f> MotionInfo,MotionNodes,MotionTriangles;TArray<FUintVector4> MotionLookup;
    if (Batch->Motion) Batch->Motion->GpuBuffers(View->ViewMatrices.GetPreViewTranslation(),MotionInfo,MotionNodes,MotionTriangles,MotionLookup);
    else { MotionInfo.SetNumZeroed(8);MotionNodes.SetNumZeroed(2);MotionTriangles.SetNumZeroed(7);MotionLookup.SetNumZeroed(2); }
    Params->MotionCount=Batch->Motion?Batch->Motion->Meshes.Num():0;
    Params->MotionMask=MotionLookup.Num()-1;
    Params->MotionInfo=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.MotionInfo"),sizeof(FVector4f),MotionInfo.Num(),MotionInfo.GetData(),MotionInfo.Num()*sizeof(FVector4f)));
    Params->MotionNodes=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.MotionNodes"),sizeof(FVector4f),MotionNodes.Num(),MotionNodes.GetData(),MotionNodes.Num()*sizeof(FVector4f)));
    Params->MotionTriangles=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.MotionTriangles"),sizeof(FVector4f),MotionTriangles.Num(),MotionTriangles.GetData(),MotionTriangles.Num()*sizeof(FVector4f)));
    Params->MotionLookup=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.MotionLookup"),sizeof(FUintVector4),MotionLookup.Num(),MotionLookup.GetData(),MotionLookup.Num()*sizeof(FUintVector4)));
    Params->Rays = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("Carla.SensorRays"),
        sizeof(FVector4f), Input.Num(), Input.GetData(), Input.Num() * sizeof(FVector4f)));
    Params->CollisionTable = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("Carla.CollisionFilter"),
        sizeof(FUintVector4), Batch->Collision->Table.Num(), Batch->Collision->Table.GetData(),
        Batch->Collision->Table.Num() * sizeof(FUintVector4)));
    Params->OpticalMaterials = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("Carla.OpticalMaterials"),
        sizeof(FVector4f), Batch->Collision->Materials.Num(), Batch->Collision->Materials.GetData(),
        Batch->Collision->Materials.Num()*sizeof(FVector4f)));
    Params->InfraredInfo=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.InfraredInfo"),sizeof(FVector4f),Batch->Collision->InfraredInfo.Num(),Batch->Collision->InfraredInfo.GetData(),Batch->Collision->InfraredInfo.Num()*sizeof(FVector4f)));
    Params->InfraredUVs=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.InfraredUVs"),sizeof(FVector4f),Batch->Collision->InfraredUVs.Num(),Batch->Collision->InfraredUVs.GetData(),Batch->Collision->InfraredUVs.Num()*sizeof(FVector4f)));
    Params->InfraredPixels=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.InfraredPixels"),sizeof(FVector4f),Batch->Collision->InfraredPixels.Num(),Batch->Collision->InfraredPixels.GetData(),Batch->Collision->InfraredPixels.Num()*sizeof(FVector4f)));
    FRDGBufferRef Output = Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
        sizeof(FVector4f), Batch->Rays.Num()*Batch->HitStride), TEXT("Carla.SensorHits"));
    Params->Hits = Graph.CreateUAV(Output);
    TShaderMapRef<FCarlaSensorTraceCS> Shader(GetGlobalShaderMap(View->GetFeatureLevel()));
    if (GSupportsTimestampRenderQueries) {
      Batch->QueryStart=RHICreateRenderQuery(RQT_AbsoluteTime);
      Batch->QueryEnd=RHICreateRenderQuery(RQT_AbsoluteTime);
    }
    Batch->Dispatched=FPlatformTime::Seconds();
    Graph.AddPass(RDG_EVENT_NAME("Carla GPU sensor rays (%u)",Params->RayCount),Params,ERDGPassFlags::Compute,
      [Batch,Params,Shader](FRHICommandList& Cmd) {
        if (Batch->QueryStart) Cmd.EndRenderQuery(Batch->QueryStart);
        FComputeShaderUtils::Dispatch(Cmd,Shader,*Params,FIntVector(FMath::DivideAndRoundUp(Params->RayCount,32u),1,1));
        if (Batch->QueryEnd) Cmd.EndRenderQuery(Batch->QueryEnd);
      });
    Batch->Readback = MakeUnique<FRHIGPUBufferReadback>(TEXT("Carla.SensorReadback"));
    const uint32 Bytes = Batch->Rays.Num()*Batch->HitStride*sizeof(FVector4f);
    AddEnqueueCopyPass(Graph, Batch->Readback.Get(), Output, Bytes);
    CarlaGpuSensors::PollReadback([Batch, Bytes]()
    {
      if (!Batch->Readback->IsReady()) return false;
      Batch->Ready=FPlatformTime::Seconds();
      uint64 Start=0,End=0;
      if (Batch->QueryStart && RHIGetRenderQueryResult(Batch->QueryStart,Start,false) && RHIGetRenderQueryResult(Batch->QueryEnd,End,false) && End>=Start)
        Batch->GpuMs=double(End-Start)/1000.;
      const void* Data = Batch->Readback->Lock(Bytes);
      if (!Data) UE_LOG(LogCarla, Fatal, TEXT("GPU sensor readback mapping failed"));
      Batch->Output.SetNumUninitialized(Bytes / sizeof(FVector4f));
      FMemory::Memcpy(Batch->Output.GetData(), Data, Bytes);
      Batch->Readback->Unlock();
      Batch->Readback.Reset();
      Completed.Enqueue(Batch);
      return true;
    });
  }
#endif
}
}

namespace CarlaGpuSensors
{
using namespace CarlaGpuSensorPrivate;

void ProfileSample(const FString& Name, double Milliseconds)
{
  if (!FMath::IsFinite(Milliseconds) || Milliseconds<0) return;
  // Called from render/background tasks as well as the game thread.
  FScopeLock Lock(&ProfileMutex);
  static TMap<FString,TArray<double>> Samples;
  static double LastWrite=0;
  auto& Values=Samples.FindOrAdd(Name); Values.Add(Milliseconds);
  if (Values.Num()>120) Values.RemoveAt(0);
  const double Now=FPlatformTime::Seconds();
  if (Now-LastWrite<1 || (ProfileWriteTask.IsValid() && !ProfileWriteTask->IsComplete())) return;
  LastWrite=Now;
  FString Json=TEXT("{\"metrics\":{"); bool First=true;
  for (const auto& Pair : Samples) {
    auto Sorted=Pair.Value;Sorted.Sort();double Sum=0;for (double V : Sorted) Sum+=V;
    if (!First) Json+=TEXT(",");First=false;
    Json+=FString::Printf(TEXT("\"%s\":{\"mean\":%.4f,\"p95\":%.4f,\"samples\":%d}"),*Pair.Key,Sum/Sorted.Num(),Sorted[FMath::CeilToInt(.95*Sorted.Num())-1],Sorted.Num());
  }
  Json+=TEXT("}}");
  // Timing diagnostics must not perform filesystem I/O on a render thread
  // or while delivering a sensor callback. Coalesce to one pending write,
  // and use the user's runtime tmpfs on Linux rather than the asset disk.
  const FString RuntimeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_RUNTIME_DIR"));
  const FString Dir = RuntimeDir.IsEmpty()
      ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SensorProfiles"))
      : FPaths::Combine(RuntimeDir, TEXT("carla-sensor-profiles"));
  ProfileWriteTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
    [Json = MoveTemp(Json), Dir]()
    {
      IFileManager::Get().MakeDirectory(*Dir, true);
      const FString Path = FPaths::Combine(Dir, FString::Printf(TEXT("%u.json"), FPlatformProcess::GetCurrentProcessId()));
      if (FFileHelper::SaveStringToFile(Json, *(Path + TEXT(".tmp"))))
        IFileManager::Get().Move(*Path, *(Path + TEXT(".tmp")), true);
    }, TStatId(), nullptr, ENamedThreads::AnyBackgroundThreadNormalTask);
}

bool IsEnabled() { return Enabled.GetValueOnGameThread() != 0; }

void PollReadback(TUniqueFunction<bool()>&& Poll)
{
  check(IsInRenderingThread());
  Readbacks.Add(MoveTemp(Poll));
  ReadbackCount.Store(Readbacks.Num());
}

void DispatchReadbackCallback(TUniqueFunction<void()>&& Callback)
{
  check(IsInRenderingThread());
  CallbackTasks.RemoveAll([](const FGraphEventRef& Task) { return Task->IsComplete(); });
  CallbackTasks.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(Callback),
      TStatId(), nullptr, ENamedThreads::AnyBackgroundThreadNormalTask));
}

void ScheduleDelivery(AActor& Owner,double DelaySeconds,TUniqueFunction<void()>&& Deliver)
{
  check(IsInGameThread());
  if(DelaySeconds<=0) { Deliver();return; }
  DelayedDeliveries.Add({&Owner,FPlatformTime::Seconds()+DelaySeconds,MoveTemp(Deliver)});
}

void EnqueueGameThread(TUniqueFunction<void()>&& Callback)
{
  GameCallbacks.Enqueue(MoveTemp(Callback));
}

void Startup()
{
  check(IsInGameThread());
  if (Running) return;
  Running = true;
  // Module shutdown happens after Unreal stops the render thread. Drain GPU
  // work earlier, while mapping, unmapping and task dispatch still have their
  // normal thread ownership. ShutdownModule remains an idempotent fallback.
  PreExitHandle = FCoreDelegates::OnPreExit.AddStatic(&Shutdown);
  auto& Renderer = FModuleManager::LoadModuleChecked<IRendererModule>(TEXT("Renderer"));
  RenderHandle = Renderer.RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateStatic(&Render));
  TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
  { Pump(); return true; }));
}

void Pump()
{
  check(IsInGameThread());
  if (!Running) return;
  TUniqueFunction<void()> Callback;
  while (GameCallbacks.Dequeue(Callback)) Callback();
  for(int32 I=DelayedDeliveries.Num()-1;I>=0;--I) {
    if(!DelayedDeliveries[I].Owner.IsValid()) { DelayedDeliveries.RemoveAt(I);continue; }
    if(FPlatformTime::Seconds()>=DelayedDeliveries[I].Due) {
      auto Deliver=MoveTemp(DelayedDeliveries[I].Deliver);DelayedDeliveries.RemoveAt(I);Deliver();
    }
  }
  FBatchPtr Batch;
  while (Completed.Dequeue(Batch))
  {
    ProfileSample(TEXT("ray_gpu_ms"),Batch->GpuMs);
    ProfileSample(TEXT("ray_queue_ms"),(Batch->Dispatched-Batch->Submitted)*1000);
    ProfileSample(TEXT("ray_dispatch_to_readback_ms"),(Batch->Ready-Batch->Dispatched)*1000);
    InFlight.Remove(Batch->Owner);
    if (!Batch->Owner.IsValid()) continue;
    TArray<FCarlaGpuHit> Hits;
    Hits.SetNum(Batch->Rays.Num());
    for (int32 I = 0; I < Hits.Num(); ++I)
    {
      const FVector4f& Raw = Batch->Output[I * Batch->HitStride];
      const FVector4f& Normal = Batch->Output[I * Batch->HitStride + 1];
      Hits[I].MotionIncomplete = Batch->Motion && Batch->Rays[I].TargetMotion && (Batch->Motion->MissingGeometry>0 || (Batch->Rays[I].SceneTime>=0 && Batch->Rays[I].SceneTime<Batch->Motion->OldestCoveredTime-1.e-7));
      Hits[I].UnoccludedRange = Batch->Rays[I].PhysicalModel ? Normal.W : Batch->Rays[I].Range;
      if (Batch->Rays[I].PhysicalModel) {
        Hits[I].EchoCount=FMath::Clamp(int32(Raw.W),0,8);
        for (uint32 E=0;E<Hits[I].EchoCount;++E) {
          const auto& V=Batch->Output[I*Batch->HitStride+2+E];auto& Echo=Hits[I].Echoes[E];
          Echo.Distance=V.X;Echo.SurfaceReturn=V.Z;
          FMemory::Memcpy(&Echo.Component,&V.Y,sizeof(uint32));FMemory::Memcpy(&Echo.Flags,&V.W,sizeof(uint32));
        }
      }
      if (Raw.X == -2) UE_LOG(LogCarla, Fatal, TEXT("GPU ray exceeded 256 collision-filter retraces; refusing incomplete sensor data"));
      if (Raw.X < 0) continue;
      uint32 ComponentId;
      FMemory::Memcpy(&ComponentId, &Raw.Y, sizeof(ComponentId));
      const auto* Meta = Batch->Collision->Components.Find(ComponentId);
      if (!Meta || !Meta->Component.IsValid()) continue;
      auto* Component = Meta->Component.Get();
      const FVector Position = Batch->Rays[I].Origin + Batch->Rays[I].Direction * Raw.X;
      Hits[I].Hit = FHitResult(Component->GetOwner(), Component, Position, FVector(Normal.X, Normal.Y, Normal.Z));
      Hits[I].Hit.bBlockingHit = true;
      Hits[I].Hit.Distance = Raw.X;
      Hits[I].Hit.Location = Hits[I].Hit.ImpactPoint = Position;
      Hits[I].Hit.TraceStart = Batch->Rays[I].Origin;
      Hits[I].TargetVelocity = Meta->Velocity;
      Hits[I].SurfaceReturn = Raw.Z;
    }
    Batch->Complete(MoveTemp(Hits));
  }
  const double Now = FPlatformTime::Seconds();
  for (auto It = InFlight.CreateIterator(); It; ++It)
  {
    if (!It.Key().IsValid()) { It.RemoveCurrent(); continue; }
    if (Now - It.Value() > 120)
      UE_LOG(LogCarla, Fatal, TEXT("GPU sensor timed out: a rendered view, inline ray tracing and r.RayTracing.ExternalQueries=1 are required"));
  }
  if (Now - LastPoll < 0.002) return;
  if (ReadbackCount.Load() == 0 && InFlight.IsEmpty()) return;
  LastPoll = Now;
  ENQUEUE_RENDER_COMMAND(CarlaPollSensorReadbacks)([](FRHICommandListImmediate& Cmd)
  {
    // A synchronous client can wait for this copy before allowing another
    // frame. Submit queued RHI work without waiting for the RHI thread or GPU,
    // so the fence can complete even when normal end-of-frame submission stops.
    if (!Readbacks.IsEmpty())
      Cmd.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
    for (int32 I = 0; I < Readbacks.Num();)
      if (Readbacks[I]()) Readbacks.RemoveAt(I); else ++I;
    ReadbackCount.Store(Readbacks.Num());
  });
}

void Submit(AActor& Owner, TArray<FCarlaGpuRay>&& Rays,
    TUniqueFunction<void(TArray<FCarlaGpuHit>&&)>&& Complete)
{
  check(IsInGameThread());
  if (!GRHISupportsInlineRayTracing)
    UE_LOG(LogCarla, Fatal, TEXT("GPU sensors explicitly requested but this RHI has no inline ray tracing"));
  if (InFlight.Contains(&Owner))
    UE_LOG(LogCarla, Fatal, TEXT("GPU sensor %s still has an outstanding frame. Use synchronous mode and consume sensor frames before world.tick()."), *Owner.GetName());
  if (Rays.IsEmpty()) { Complete(TArray<FCarlaGpuHit>()); return; }
  if (Rays.Num() > 2097120)
    UE_LOG(LogCarla, Fatal, TEXT("GPU sensor batch exceeds dispatch limit; reduce points per tick"));
  // A subscription can arrive after the frame's initial demand scan. Ensure
  // this very frame has a main view before enqueueing a ray batch, otherwise
  // the synchronous worker would wait forever for a view it had disabled.
  if (auto *Queries = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.ExternalQueries")))
  {
    if (Queries->GetInt() == 0)
    {
      Queries->Set(1, ECVF_SetByConsole);
      UE_LOG(LogCarla, Log, TEXT("GPU_RAY_DEMAND active=1 (sensor submission)"));
    }
  }
  if (GEngine && GEngine->GameViewport) GEngine->GameViewport->bDisableWorldRendering = false;
  auto Batch = MakeShared<FBatch, ESPMode::ThreadSafe>();
  Batch->Owner = &Owner;
  Batch->Scene = Owner.GetWorld()->Scene;
  Batch->Actor = Owner.GetUniqueID();
  Batch->Submitted = FPlatformTime::Seconds();
  Batch->Rays = MoveTemp(Rays);
  if (Batch->Rays.ContainsByPredicate([](const FCarlaGpuRay& Ray) { return Ray.PhysicalModel; })) Batch->HitStride=10;
  if (QueryBoundsFrame != GFrameCounter)
  {
    FrameQueryBounds.Reset();
    QueryBoundsFrame = GFrameCounter;
  }
  FSphere* Sphere = FrameQueryBounds.Find(Batch->Scene);
  if (!Sphere)
    Sphere = &FrameQueryBounds.Add(Batch->Scene, FSphere(Batch->Rays[0].Origin, 0));
  for (const FCarlaGpuRay& Ray : Batch->Rays)
  {
    const double Offset = Ray.Origin == Sphere->Center ? 0.0 : FVector::Distance(Ray.Origin, Sphere->Center);
    Sphere->W = FMath::Max(Sphere->W, Offset + double(Ray.Range));
  }
  // One centimetre of outward padding protects conservative bounds from the
  // renderer's float conversion. No angular, camera-facing or min-distance cull.
  const FVector QueryCenter = Sphere->Center;
  const float QueryRadius = float(FMath::CeilToDouble(Sphere->W + 1.0));
  Batch->Collision = Snapshot(Owner.GetWorld());
  if (Batch->HitStride>2) {
    TArray<UPrimitiveComponent*> RenderComponents;
    for (const auto& C:Batch->Collision->RenderComponents) if(C.IsValid()) RenderComponents.Add(C.Get());
    Batch->Motion=CarlaLidarMotion::Prepare(Owner.GetWorld(),RenderComponents);
  }
  Batch->Complete = MoveTemp(Complete);
  InFlight.Add(&Owner, Batch->Submitted);
  ENQUEUE_RENDER_COMMAND(CarlaQueueSensorRays)([Batch, QueryCenter, QueryRadius](FRHICommandListImmediate&)
  {
    SetExternalRayTracingQueryBounds(Batch->Scene, QueryCenter, QueryRadius);
    QueryScenes.Add(Batch->Scene);
    Pending.Add(Batch);
  });
}

bool HasPendingFrames()
{
  check(IsInGameThread());
  return !InFlight.IsEmpty() || !DelayedDeliveries.IsEmpty();
}

void BeginSensorTick(AActor& Owner)
{
  Pump();
  if (InFlight.Contains(&Owner))
    UE_LOG(LogCarla, Fatal, TEXT("GPU sensor %s has an outstanding frame. Consume its frame before the next synchronous tick."), *Owner.GetName());
}

void Shutdown()
{
  if (!Running) return;
  Running = false;
  DelayedDeliveries.Empty();
  FCoreDelegates::OnPreExit.Remove(PreExitHandle);
  FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
  if (auto* Renderer = FModuleManager::GetModulePtr<IRendererModule>(TEXT("Renderer")))
    Renderer->RemovePostOpaqueRenderDelegate(RenderHandle);
  ENQUEUE_RENDER_COMMAND(CarlaDrainSensorReadbacks)([](FRHICommandListImmediate& Cmd)
  {
    Cmd.BlockUntilGPUIdle();
    for (auto& Poll : Readbacks) Poll();
    Readbacks.Empty();
    ReadbackCount.Store(0);
    Pending.Empty();
    for (const FSceneInterface* Scene : QueryScenes)
      SetExternalRayTracingQueryBounds(Scene, FVector::ZeroVector, -1);
    QueryScenes.Empty();
  });
  FlushRenderingCommands();
  FTaskGraphInterface::Get().WaitUntilTasksComplete(CallbackTasks, ENamedThreads::GameThread);
  CallbackTasks.Empty();
  FGraphEventRef Writer;
  { FScopeLock Lock(&ProfileMutex); Writer = ProfileWriteTask; }
  if (Writer.IsValid()) FTaskGraphInterface::Get().WaitUntilTaskCompletes(Writer, ENamedThreads::GameThread);
  { FScopeLock Lock(&ProfileMutex); ProfileWriteTask = nullptr; }
  // Background converters enqueue render-thread unmaps when they finish.
  FlushRenderingCommands();
  TUniqueFunction<void()> UnusedCallback;
  while (GameCallbacks.Dequeue(UnusedCallback)) {}
  FBatchPtr Unused;
  while (Completed.Dequeue(Unused)) {}
  InFlight.Empty();
  CachedCollision.Reset();
  CachedWorld.Reset();
}
}
