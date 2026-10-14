// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Carla/Sensor/ReadbackPollGate.h"
#include "GameFramework/Actor.h"
#include "Carla.h"
#include "Carla/Sensor/LidarOptics.h"
#include "Carla/Sensor/LidarMotion.h"
#include "Carla/Sensor/LidarInfrared.h"
#include "Async/TaskGraphInterfaces.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
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

#include <atomic>

namespace CarlaGpuSensorPrivate
{
using FSharedPackedRayInputs = TSharedPtr<TArray<FVector4f>, ESPMode::ThreadSafe>;
TAutoConsoleVariable<int32> ReusePackedRayInputs(TEXT("carla.Sensors.ReusePackedRayInputs"), 0,
    TEXT("Reuse CPU ray input arrays only after all batch and RDG upload leases retire."));

TAutoConsoleVariable<int32> ParallelRayValidation(TEXT("carla.Sensors.ParallelRayValidation"), 0,
    TEXT("Validate every physical GPU ray in bounded CPU blocks before delivery."));

TAutoConsoleVariable<int32> SkipUnrenderedSnapshotComponents(TEXT("carla.Sensors.SkipUnrenderedSnapshotComponents"), 0,
    TEXT("Skip material classification for registered components without a render primitive ID; snapshot output is unchanged."));

bool HasCollisionRetraceOverflow(TConstArrayView<FVector4f> Encoded, int32 RayCount, int32 HitStride, bool Parallel)
{
  check(HitStride>0 && RayCount>=0 && RayCount<=Encoded.Num()/HitStride);
  constexpr int32 BlockSize=4096;
  if (!Parallel || RayCount<=BlockSize)
  {
    for (int32 I=0;I<RayCount;++I)
      if (Encoded[I*HitStride].X==-2) return true;
    return false;
  }
  std::atomic<bool> Overflow{false};
  const int32 Blocks=RayCount/BlockSize+int32(RayCount%BlockSize!=0);
  ParallelFor(TEXT("CarlaValidatePhysicalRays"),Blocks,1,[&](int32 Block)
  {
    bool LocalOverflow=false;
    const int32 Begin=Block*BlockSize;
    const int32 End=Begin+FMath::Min(BlockSize,RayCount-Begin);
    for (int32 I=Begin;I<End;++I)
      LocalOverflow|=Encoded[I*HitStride].X==-2;
    if (LocalOverflow) Overflow.store(true,std::memory_order_relaxed);
  },EParallelForFlags::Unbalanced);
  // ParallelFor joins before the readback owner can unmap or retire the batch.
  return Overflow.load(std::memory_order_relaxed);
}

FSharedPackedRayInputs AcquirePackedRayInputs(FSharedPackedRayInputs& Storage)
{
  // The game-thread owner alone publishes references. Batches and RDG uploads
  // keep strong leases; a remaining borrower always gets independent storage.
  if (Storage.IsValid() && Storage.IsUnique())
    std::atomic_thread_fence(std::memory_order_acquire);
  else
    Storage = MakeShared<TArray<FVector4f>, ESPMode::ThreadSafe>();
  return Storage;
}
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
TAutoConsoleVariable<int32> MotionQueryScope(TEXT("carla.Sensors.MotionQueryBounds"), 0,
    TEXT("After full motion history preparation, restrict GPU motion meshes to conservative optical query bounds when material IORs are at least one."));
TSet<uint32> DiagnosedComponents;

struct FComponentSnapshot
{
  TWeakObjectPtr<UPrimitiveComponent> Component;
  FVector Velocity;
  uint32 Actor;
  uint32 MaterialOffset, MaterialCount;
  bool OpticalOnly;
  bool IgnoreOpticalCollision;
};
struct FInfraredSnapshot {
  TArray<FVector4f> Info,UVs,Pixels;
  // Only the render thread accesses these pooled GPU leases.
  TRefCountPtr<FRDGPooledBuffer> InfoBuffer,UVBuffer,PixelBuffer;
};
struct FInfraredRequest {
  TWeakObjectPtr<UPrimitiveComponent> Component;
  uint32 Section;
  int32 Profile;
};
struct FInfraredSignature {
  CarlaLidarInfrared::GeometryKey Geometry;
  int32 Profile;
  bool operator==(const FInfraredSignature& B)const{return Profile==B.Profile && Geometry==B.Geometry;}
};
TSharedPtr<FInfraredSnapshot,ESPMode::ThreadSafe> CachedInfrared;
TArray<FInfraredSignature> CachedInfraredSignature;
uint64 PackedInfraredRevision=0;
TSharedPtr<FInfraredSnapshot,ESPMode::ThreadSafe> BuildInfrared(const TArray<FInfraredRequest>& Requests,bool Shared=true) {
  check(IsInGameThread());CarlaLidarInfrared::Prepare();
  TArray<FInfraredSignature> Signature;Signature.Reserve(Requests.Num());
  for(const auto& R:Requests)Signature.Add({CarlaLidarInfrared::Key(R.Component.Get(),R.Section),R.Profile});
  const uint64 Revision=CarlaLidarInfrared::Revision();
  if(Shared && CachedInfrared && PackedInfraredRevision==Revision && Signature==CachedInfraredSignature) {
    CarlaGpuSensors::ProfileSample(TEXT("infrared_snapshot_reused"),1);return CachedInfrared;
  }
  auto Result=MakeShared<FInfraredSnapshot,ESPMode::ThreadSafe>();CarlaLidarInfrared::SnapshotUVCache Cache;
  for(const auto& R:Requests)CarlaLidarInfrared::Append(R.Component.Get(),R.Section,R.Profile,Result->Info,Result->UVs,Shared?&Cache:nullptr);
  if(Result->Info.IsEmpty())Result->Info.SetNumZeroed(8);
  if(Result->UVs.IsEmpty())Result->UVs.SetNumZeroed(1);
  Result->Pixels=CarlaLidarInfrared::Pixels();
  if(Shared){CachedInfrared=Result;CachedInfraredSignature=MoveTemp(Signature);PackedInfraredRevision=Revision;}
  CarlaGpuSensors::ProfileSample(TEXT("infrared_snapshot_reused"),0);return Result;
}
struct FCollisionSnapshot
{
  TMap<uint32, FComponentSnapshot> Components;
  TArray<FUintVector4> Table;
  TArray<FVector4f> Materials;
  TSharedPtr<FInfraredSnapshot,ESPMode::ThreadSafe> Infrared;
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
  TSharedRef<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe> RayStorage;
  TArray<FCarlaGpuRay>& Rays;
  FSharedPackedRayInputs PackedInputStorage;
  explicit FBatch(TSharedRef<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe> InRays)
      : RayStorage(MoveTemp(InRays)), Rays(*RayStorage) {}

  TSharedPtr<FCollisionSnapshot, ESPMode::ThreadSafe> Collision;
  TArray<FVector4f> Output;
  TUniqueFunction<void(TArray<FCarlaGpuHit>&&)> Complete;
  TUniqueFunction<void(const CarlaGpuSensors::FPhysicalHitView&)> PhysicalComplete;
  bool BuildHitResults=true;
  TUniquePtr<FRHIGPUBufferReadback> Readback;
  const FVector4f* MappedOutput=nullptr;
  ~FBatch() {
    if(!MappedOutput)return;
    // Physical echo views borrow this mapping until their synchronous receiver
    // has joined all tasks. Owner destruction and shutdown use the same path.
    check(Readback);
    if(IsInRenderingThread())Readback->Unlock();
    else {
      ENQUEUE_RENDER_COMMAND(CarlaUnmapRayReadback)(
        [MappedReadback=MoveTemp(Readback)](FRHICommandListImmediate&) {
          MappedReadback->Unlock();
        });
    }
  }

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
FCarlaReadbackPollGate ReadbackPollGate;
TAutoConsoleVariable<int32> CoalesceReadbackPolls(TEXT("carla.Sensors.CoalesceReadbackPolls"),0,
    TEXT("Bound render-thread readback polling to one outstanding command; no change to readbacks or sensor frames."));
TAutoConsoleVariable<int32> ReadbackPollAudit(TEXT("carla.Sensors.ReadbackPollAudit"),0,
    TEXT("Collect cumulative readback polling counters and queue latency; diagnostic only."));
uint64 PollEnqueued=0,PollCoalesced=0,PollPeakOutstanding=0; // Game thread only.
TAtomic<uint64> PollExecuted{0},PollEmpty{0},PollQueueMicroseconds{0};
FAutoConsoleCommand DumpReadbackPollStats(TEXT("carla.Sensors.DumpReadbackPollStats"),
    TEXT("Log bounded readback polling counters without waiting or advancing the world."),
    FConsoleCommandDelegate::CreateLambda([] {
      check(IsInGameThread());
      UE_LOG(LogCarla,Log,TEXT("CARLA_READBACK_POLL_STATS mode=%d audit=%d enqueued=%llu executed=%llu coalesced=%llu empty=%llu peak=%llu outstanding=%d queue_us=%llu"),
          CoalesceReadbackPolls.GetValueOnGameThread(),ReadbackPollAudit.GetValueOnGameThread(),
          PollEnqueued,PollExecuted.Load(),PollCoalesced,PollEmpty.Load(),PollPeakOutstanding,
          ReadbackPollGate.NumOutstanding(),PollQueueMicroseconds.Load());
    }));
FAutoConsoleCommand ResetReadbackPollStats(TEXT("carla.Sensors.ResetReadbackPollStats"),
    TEXT("Reset polling counters only after all queued poll commands have retired."),
    FConsoleCommandDelegate::CreateLambda([] {
      check(IsInGameThread());
      if (ReadbackPollGate.NumOutstanding()!=0)
      {
        UE_LOG(LogCarla,Warning,TEXT("CARLA_READBACK_POLL_RESET refused=pending"));
        return;
      }
      PollEnqueued=PollCoalesced=PollPeakOutstanding=0;
      PollExecuted.Store(0);PollEmpty.Store(0);PollQueueMicroseconds.Store(0);
      UE_LOG(LogCarla,Log,TEXT("CARLA_READBACK_POLL_RESET passed=1"));
    }));
FGraphEventArray CallbackTasks; // Render thread; joined after the shutdown fence.
FCriticalSection ProfileMutex;
FGraphEventRef ProfileWriteTask; // One best-effort writer, joined on shutdown.
TQueue<FBatchPtr, EQueueMode::Mpsc> Completed;
TQueue<TUniqueFunction<void()>, EQueueMode::Mpsc> GameCallbacks;
struct FDelayedDelivery { TWeakObjectPtr<AActor> Owner; double Due; TUniqueFunction<void()> Deliver; };
TArray<FDelayedDelivery> DelayedDeliveries;
TMap<TWeakObjectPtr<AActor>, double> InFlight; // Game thread only.
TMap<TWeakObjectPtr<AActor>, FSharedPackedRayInputs> PackedInputFrames; // Game thread only.
TWeakObjectPtr<UWorld> CachedWorld;
uint64 CachedFrame = MAX_uint64;
uint64 CachedInfraredRevision=MAX_uint64;
TSharedPtr<FCollisionSnapshot, ESPMode::ThreadSafe> CachedCollision;
FDelegateHandle RenderHandle;
FDelegateHandle PreExitHandle;
FTSTicker::FDelegateHandle TickHandle;
bool Running = false;
double LastPoll = 0;

struct FSnapshotBuildCounts
{
  int32 Registered=0, Unrendered=0, MaterialQueries=0;
};

TSharedPtr<FCollisionSnapshot, ESPMode::ThreadSafe> BuildSnapshot(UWorld* World, bool SkipUnrendered,
    FSnapshotBuildCounts* Counts=nullptr, bool ShareInfraredUVs=true)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(CarlaRayCollisionSnapshot);
  auto Result = MakeShared<FCollisionSnapshot, ESPMode::ThreadSafe>();
  CarlaLidarOptics::SnapshotMaterialCache MaterialCache;
  TArray<FInfraredRequest> InfraredRequests;
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
      uint32 Id=0;
      if (SkipUnrendered || Counts)
      {
        Id=C->GetPrimitiveSceneId().PrimIDValue;
        if (Counts) { ++Counts->Registered; Counts->Unrendered+=int32(Id==0); }
        // The original path never adds ID zero to any snapshot output. Do not
        // classify its materials, but keep all registered render primitives,
        // including non-solid optical surfaces and collision-proxy associations.
        if (SkipUnrendered && !Id) continue;
      }
      const bool CollisionEligible=CollisionComponent && CollisionComponent->IsRegistered() &&
          CollisionComponent->IsQueryCollisionEnabled() &&
          CollisionComponent->GetCollisionResponseToChannel(ECC_GameTraceChannel2)==ECR_Block;
      if (Counts) ++Counts->MaterialQueries;
      const auto Materials=CarlaLidarOptics::Sections(C,&MaterialCache);
      // Visible optical surfaces can be non-solid (windows and painted road
      // meshes). Include them only for material-aware LiDAR, preserving the
      // existing collision predicate for radar and semantic/legacy LiDAR.
      const bool OpticalEligible=Actor->GetActorEnableCollision() && !Actor->IsHidden() && C->IsVisible() && !C->bHiddenInGame &&
          CarlaLidarOptics::NeedsRenderSurface(C,Materials);
      if (!CollisionEligible && !OpticalEligible) continue;
      if (!CollisionEligible) CollisionComponent=C;
      if (!SkipUnrendered) Id=C->GetPrimitiveSceneId().PrimIDValue;
      if (Id) {
        Result->RenderComponents.Add(C);
        const uint32 Offset=Result->Materials.Num()/2;
        uint32 SectionIndex=0;
        for (const auto& M:Materials) {
          InfraredRequests.Add({C,SectionIndex++,M.infrared});
          Result->Materials.Add(FVector4f(M.diffuse,M.retro,M.specular,M.transmission));
          Result->Materials.Add(FVector4f(M.ior,M.roughness,M.thinSheet,0));
        }
        Result->Components.Add(Id, {CollisionComponent, Velocity, Actor->GetUniqueID(),Offset,uint32(Materials.Num()),!CollisionEligible,CarlaLidarOptics::IgnoresOpticalCollision(C) || CarlaLidarOptics::IgnoresOpticalCollision(CollisionComponent)});
      }
    }
  }
  const uint32 Size = FMath::RoundUpToPowerOfTwo(FMath::Max(2, Result->Components.Num() * 2));
  Result->Table.SetNumZeroed(Size);
  for (const auto& Entry : Result->Components)
  {
    uint32 Slot = (Entry.Key * 2654435761u) & (Size - 1);
    while (Result->Table[Slot].X != 0) Slot = (Slot + 1) & (Size - 1);
    Result->Table[Slot] = FUintVector4(Entry.Key, Entry.Value.Actor, Entry.Value.MaterialOffset, Entry.Value.MaterialCount | (Entry.Value.OpticalOnly ? 0x80000000u : 0u) | (Entry.Value.IgnoreOpticalCollision ? 0x40000000u : 0u));
  }
  if (Result->Materials.IsEmpty()) Result->Materials.SetNumZeroed(2);
  Result->Infrared=BuildInfrared(InfraredRequests,ShareInfraredUVs);
  CarlaGpuSensors::ProfileSample(TEXT("infrared_uv_mb"),Result->Infrared->UVs.Num()*sizeof(FVector4f)/1048576.0);
  CarlaGpuSensors::ProfileSample(TEXT("infrared_atlas_mb"),Result->Infrared->Pixels.Num()*sizeof(FVector4f)/1048576.0);
  return Result;
}

TSharedPtr<FCollisionSnapshot, ESPMode::ThreadSafe> Snapshot(UWorld* World)
{
  CarlaLidarInfrared::Prepare();
  const uint64 InfraredRevision=CarlaLidarInfrared::Revision();
  if (CachedWorld == World && CachedFrame == GFrameCounter && CachedInfraredRevision==InfraredRevision) return CachedCollision;
  auto Result=BuildSnapshot(World,SkipUnrenderedSnapshotComponents.GetValueOnGameThread()!=0);
  CachedWorld = World;
  CachedFrame = GFrameCounter;
  CachedInfraredRevision=InfraredRevision;
  CachedCollision = Result;
  return Result;
}

// Render-thread-owned immutable identities keep atlas offsets valid even when
// an actor stops or a component disappears. RDG owns every old GPU buffer used
// by a copy pass; publishing its replacement never mutates a borrowed buffer.
using FMotionGeometry = TSharedPtr<const CarlaLidarMotion::MeshGeometry,ESPMode::ThreadSafe>;
TArray<FMotionGeometry> MotionGeometryLayout;
TArray<FIntPoint> MotionGeometryOffsets;
TRefCountPtr<FRDGPooledBuffer> MotionNodeBuffer,MotionTriangleBuffer;
TAutoConsoleVariable<int32> CacheMotionGeometry(TEXT("carla.Sensors.CacheMotionGeometry"),1,
    TEXT("Reuse unchanged physical LiDAR motion geometry GPU buffers; transforms and sample times always refresh."));
TAutoConsoleVariable<int32> AppendMotionGeometry(TEXT("carla.Sensors.AppendMotionGeometry"),0,
    TEXT("Append new immutable LiDAR geometry using GPU prefix copies, bounded to 512 MiB before full active-set compaction."),ECVF_RenderThreadSafe);

struct FMotionAtlasPlan
{
  bool Reuse=false,Append=false;
  uint32 NodeElements=0,TriangleElements=0;
  TArray<FIntPoint> CurrentOffsets,MissingOffsets;
  TArray<FMotionGeometry> Missing;
};

FMotionAtlasPlan PlanMotionAtlas(const TArray<FMotionGeometry>& Layout,const TArray<FIntPoint>& Offsets,
    const TArray<FMotionGeometry>& Current,uint32 OldNodeElements,uint32 OldTriangleElements,
    bool AllowAppend,uint64 BudgetBytes)
{
  FMotionAtlasPlan Plan;
  if(Layout.IsEmpty() || Layout.Num()!=Offsets.Num())return Plan;
  uint64 Nodes=0,Triangles=0;
  // Only densely packed immutable layouts can be extended. Cold/dummy buffers
  // and any inconsistent descriptor take the complete legacy upload path.
  for(int32 I=0;I<Layout.Num();++I)
  {
    const auto& G=Layout[I];
    if(!G || G->PackedNodes.IsEmpty() || G->PackedTriangles.IsEmpty() ||
        G->PackedNodes.Num()%2 || G->PackedTriangles.Num()%7 ||
        Offsets[I]!=FIntPoint(Nodes/2,Triangles/7))return Plan;
    Nodes+=G->PackedNodes.Num();Triangles+=G->PackedTriangles.Num();
  }
  if(Nodes!=OldNodeElements || Triangles!=OldTriangleElements)return Plan;
  Plan.CurrentOffsets.Reserve(Current.Num());
  for(const auto& G:Current)
  {
    if(!G || G->PackedNodes.IsEmpty() || G->PackedTriangles.IsEmpty() ||
        G->PackedNodes.Num()%2 || G->PackedTriangles.Num()%7)return FMotionAtlasPlan();
    const int32 Existing=Layout.IndexOfByKey(G);
    if(Existing!=INDEX_NONE){Plan.CurrentOffsets.Add(Offsets[Existing]);continue;}
    const int32 Missing=Plan.Missing.IndexOfByKey(G);
    if(Missing!=INDEX_NONE){Plan.CurrentOffsets.Add(Plan.MissingOffsets[Missing]);continue;}
    if(!AllowAppend)return FMotionAtlasPlan();
    Plan.Missing.Add(G);Plan.MissingOffsets.Add(FIntPoint(Nodes/2,Triangles/7));
    Plan.CurrentOffsets.Add(Plan.MissingOffsets.Last());
    Nodes+=G->PackedNodes.Num();Triangles+=G->PackedTriangles.Num();
    if(Nodes>MAX_int32 || Triangles>MAX_int32 ||
        (Nodes+Triangles)*sizeof(FVector4f)>BudgetBytes)return FMotionAtlasPlan();
  }
  Plan.Reuse=Plan.Missing.IsEmpty();Plan.Append=!Plan.Reuse;
  Plan.NodeElements=Nodes;Plan.TriangleElements=Triangles;
  return Plan;
}

void Render(FPostOpaqueRenderParameters& P)
{
#if RHI_RAYTRACING
  const FSceneView* View = P.SceneView;
  if (!View || !View->Family || View->bIsSceneCapture || View->bIsReflectionCapture || !P.GraphBuilder || Pending.IsEmpty()) return;
  const auto* Scene = View->Family->Scene;
  if (!UE::FXRenderingUtils::RayTracing::HasRayTracingScene(Scene)) return;
  TRACE_CPUPROFILER_EVENT_SCOPE(CarlaGpuRayRender);
  FRDGBuilder& Graph = *P.GraphBuilder;
  for (int32 Index = 0; Index < Pending.Num();)
  {
    FBatchPtr Batch = Pending[Index];
    if (Batch->Scene != Scene) { ++Index; continue; }
    Pending.RemoveAt(Index);
    const double RenderSetupStarted=FPlatformTime::Seconds();
    TArray<FVector4f> LocalInput;
    TArray<FVector4f>& Input = Batch->PackedInputStorage.IsValid() ? *Batch->PackedInputStorage : LocalInput;
    {
    TRACE_CPUPROFILER_EVENT_SCOPE(CarlaRayInputPack);
    CarlaGpuSensors::PackRayInputs(Batch->Rays,View->ViewMatrices.GetPreViewTranslation(),
        Batch->Motion.IsValid(),Batch->Motion?Batch->Motion->Time:0,Input);
    }
    CarlaGpuSensors::ProfileSample(TEXT("ray_input_pack_ms"),(FPlatformTime::Seconds()-RenderSetupStarted)*1000);
    auto* Params = Graph.AllocParameters<FCarlaSensorTraceCS::FParameters>();
    Params->View = View->ViewUniformBuffer;
    Params->Scene = GetSceneUniformBufferRef(Graph, *View);
    Params->TLAS = UE::FXRenderingUtils::RayTracing::GetRayTracingSceneView(Graph.RHICmdList, Scene);
    Params->RayCount = Batch->Rays.Num();
    Params->TableMask = Batch->Collision->Table.Num() - 1;
    Params->IgnoredActor = Batch->Actor;
    Params->HitStride = Batch->HitStride;
    TArray<FVector4f> MotionInfo,MotionNodes,MotionTriangles;TArray<FUintVector4> MotionLookup;
    const double MotionPackStarted=FPlatformTime::Seconds();
    const int32 MeshCount=Batch->Motion?Batch->Motion->Meshes.Num():0;
    FMotionAtlasPlan AtlasPlan;
    if(CacheMotionGeometry.GetValueOnRenderThread()!=0 && MotionNodeBuffer.IsValid() && MotionTriangleBuffer.IsValid())
    {
      TArray<FMotionGeometry> Current;Current.Reserve(MeshCount);
      if(Batch->Motion)for(const auto& Mesh:Batch->Motion->Meshes)Current.Add(Mesh.Geometry);
      AtlasPlan=PlanMotionAtlas(MotionGeometryLayout,MotionGeometryOffsets,Current,
          MotionNodeBuffer->Desc.NumElements,MotionTriangleBuffer->Desc.NumElements,
          AppendMotionGeometry.GetValueOnRenderThread()!=0,512ull*1024*1024);
    }
    const bool ReuseGeometry=AtlasPlan.Reuse,AppendGeometry=AtlasPlan.Append;
    const bool HasGeometry=ReuseGeometry || AppendGeometry;
    if(AppendGeometry)
      for(const auto& G:AtlasPlan.Missing)
      {
        MotionNodes.Append(G->PackedNodes);MotionTriangles.Append(G->PackedTriangles);
      }
    // Stopping or reordering an actor changes the active mesh set, not its
    // immutable geometry. Keep the last atlas when it contains this subset.
    if (Batch->Motion) Batch->Motion->GpuBuffers(View->ViewMatrices.GetPreViewTranslation(),MotionInfo,MotionNodes,MotionTriangles,MotionLookup,!HasGeometry,HasGeometry?&AtlasPlan.CurrentOffsets:nullptr);
    else { MotionInfo.SetNumZeroed(8);MotionNodes.SetNumZeroed(2);MotionTriangles.SetNumZeroed(7);MotionLookup.SetNumZeroed(2); }
    CarlaGpuSensors::ProfileSample(TEXT("motion_gpu_pack_ms"),(FPlatformTime::Seconds()-MotionPackStarted)*1000);
    CarlaGpuSensors::ProfileSample(TEXT("motion_geometry_upload_mb"),ReuseGeometry?0:(MotionNodes.Num()+double(MotionTriangles.Num()))*sizeof(FVector4f)/(1024*1024));
    CarlaGpuSensors::ProfileSample(TEXT("motion_geometry_append"),AppendGeometry?1:0);
    CarlaGpuSensors::ProfileSample(TEXT("motion_geometry_copy_mb"),AppendGeometry?
        (MotionNodeBuffer->Desc.NumElements+double(MotionTriangleBuffer->Desc.NumElements))*sizeof(FVector4f)/(1024*1024):0);
    Params->MotionCount=MeshCount;
    Params->MotionMask=MotionLookup.Num()-1;
    Params->MotionInfo=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.MotionInfo"),sizeof(FVector4f),MotionInfo.Num(),MotionInfo.GetData(),MotionInfo.Num()*sizeof(FVector4f)));
    FRDGBufferRef NodesBuffer,TrianglesBuffer;
    if(ReuseGeometry) {
      NodesBuffer=Graph.RegisterExternalBuffer(MotionNodeBuffer);
      TrianglesBuffer=Graph.RegisterExternalBuffer(MotionTriangleBuffer);
    } else if(AppendGeometry) {
      auto OldNodes=Graph.RegisterExternalBuffer(MotionNodeBuffer);
      auto OldTriangles=Graph.RegisterExternalBuffer(MotionTriangleBuffer);
      NodesBuffer=Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),AtlasPlan.NodeElements),TEXT("Carla.MotionNodes"));
      TrianglesBuffer=Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f),AtlasPlan.TriangleElements),TEXT("Carla.MotionTriangles"));
      const uint64 NodeBytes=uint64(MotionNodeBuffer->Desc.NumElements)*sizeof(FVector4f);
      const uint64 TriangleBytes=uint64(MotionTriangleBuffer->Desc.NumElements)*sizeof(FVector4f);
      AddCopyBufferPass(Graph,NodesBuffer,0,OldNodes,0,NodeBytes);
      AddCopyBufferPass(Graph,TrianglesBuffer,0,OldTriangles,0,TriangleBytes);
      auto NewNodes=CreateStructuredBuffer(Graph,TEXT("Carla.MotionNodeSuffix"),MoveTemp(MotionNodes));
      auto NewTriangles=CreateStructuredBuffer(Graph,TEXT("Carla.MotionTriangleSuffix"),MoveTemp(MotionTriangles));
      AddCopyBufferPass(Graph,NodesBuffer,NodeBytes,NewNodes,0,NewNodes->Desc.GetSize());
      AddCopyBufferPass(Graph,TrianglesBuffer,TriangleBytes,NewTriangles,0,NewTriangles->Desc.GetSize());
      MotionNodeBuffer=Graph.ConvertToExternalBuffer(NodesBuffer);
      MotionTriangleBuffer=Graph.ConvertToExternalBuffer(TrianglesBuffer);
      MotionGeometryLayout.Append(AtlasPlan.Missing);MotionGeometryOffsets.Append(AtlasPlan.MissingOffsets);
    } else {
      NodesBuffer=CreateStructuredBuffer(Graph,TEXT("Carla.MotionNodes"),MoveTemp(MotionNodes));
      TrianglesBuffer=CreateStructuredBuffer(Graph,TEXT("Carla.MotionTriangles"),MoveTemp(MotionTriangles));
      MotionNodeBuffer=Graph.ConvertToExternalBuffer(NodesBuffer);
      MotionTriangleBuffer=Graph.ConvertToExternalBuffer(TrianglesBuffer);
      MotionGeometryLayout.Reset(MeshCount);MotionGeometryOffsets.Reset(MeshCount);
      FIntPoint Offset(0,0);
      if(Batch->Motion) for(const auto& Mesh:Batch->Motion->Meshes) {
        if(MotionGeometryLayout.Contains(Mesh.Geometry))continue;
        MotionGeometryLayout.Add(Mesh.Geometry);MotionGeometryOffsets.Add(Offset);
        Offset.X+=Mesh.Geometry->PackedNodes.Num()/2;Offset.Y+=Mesh.Geometry->PackedTriangles.Num()/7;
      }
    }
    Params->MotionNodes=Graph.CreateSRV(NodesBuffer);
    Params->MotionTriangles=Graph.CreateSRV(TrianglesBuffer);
    Params->MotionLookup=Graph.CreateSRV(CreateStructuredBuffer(Graph,TEXT("Carla.MotionLookup"),sizeof(FUintVector4),MotionLookup.Num(),MotionLookup.GetData(),MotionLookup.Num()*sizeof(FUintVector4)));
    // Transfer ownership to RDG; its TArray overload retains the upload data
    // until execution and avoids another full copy of the packed ray array.
    if (Batch->PackedInputStorage.IsValid())
    {
      // NoCopy requires storage through graph execution. The graph holds its
      // own lease in addition to the batch retained through GPU readback.
      auto* UploadLease = Graph.AllocObject<FSharedPackedRayInputs>(Batch->PackedInputStorage);
      Params->Rays = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("Carla.SensorRays"), **UploadLease, ERDGInitialDataFlags::NoCopy));
    }
    else
      Params->Rays = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("Carla.SensorRays"), MoveTemp(LocalInput)));
    Params->CollisionTable = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("Carla.CollisionFilter"),
        sizeof(FUintVector4), Batch->Collision->Table.Num(), Batch->Collision->Table.GetData(),
        Batch->Collision->Table.Num() * sizeof(FUintVector4)));
    Params->OpticalMaterials = Graph.CreateSRV(CreateStructuredBuffer(Graph, TEXT("Carla.OpticalMaterials"),
        sizeof(FVector4f), Batch->Collision->Materials.Num(), Batch->Collision->Materials.GetData(),
        Batch->Collision->Materials.Num()*sizeof(FVector4f)));
    auto& IR=*Batch->Collision->Infrared;
    auto UploadInfrared=[&](const TCHAR* Name,const TArray<FVector4f>& Data,TRefCountPtr<FRDGPooledBuffer>& Buffer) {
      if(Buffer)return Graph.RegisterExternalBuffer(Buffer);
      // RDG owns a CPU snapshot lease through execution. Published arrays never
      // mutate; a geometry/profile revision creates a separate CPU/GPU snapshot.
      Graph.AllocObject<TSharedPtr<FInfraredSnapshot,ESPMode::ThreadSafe>>(Batch->Collision->Infrared);
      auto NewBuffer=CreateStructuredBuffer(Graph,Name,Data,ERDGInitialDataFlags::NoCopy);
      Buffer=Graph.ConvertToExternalBuffer(NewBuffer);
      CarlaGpuSensors::ProfileSample(TEXT("infrared_upload_mb"),Data.Num()*sizeof(FVector4f)/1048576.0);
      return NewBuffer;
    };
    Params->InfraredInfo=Graph.CreateSRV(UploadInfrared(TEXT("Carla.InfraredInfo"),IR.Info,IR.InfoBuffer));
    Params->InfraredUVs=Graph.CreateSRV(UploadInfrared(TEXT("Carla.InfraredUVs"),IR.UVs,IR.UVBuffer));
    Params->InfraredPixels=Graph.CreateSRV(UploadInfrared(TEXT("Carla.InfraredPixels"),IR.Pixels,IR.PixelBuffer));
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
    CarlaGpuSensors::ProfileSample(TEXT("ray_render_setup_ms"),(FPlatformTime::Seconds()-RenderSetupStarted)*1000);
    CarlaGpuSensors::PollReadback([Batch, Bytes]()
    {
      if (!Batch->Readback->IsReady()) return false;
      Batch->Ready=FPlatformTime::Seconds();
      uint64 Start=0,End=0;
      if (Batch->QueryStart && RHIGetRenderQueryResult(Batch->QueryStart,Start,false) && RHIGetRenderQueryResult(Batch->QueryEnd,End,false) && End>=Start)
        Batch->GpuMs=double(End-Start)/1000.;
      TRACE_CPUPROFILER_EVENT_SCOPE(CarlaRayReadbackCopy);
      const double ReadbackCopyStarted=FPlatformTime::Seconds();
      const void* Data = Batch->Readback->Lock(Bytes);
      if (!Data) UE_LOG(LogCarla, Fatal, TEXT("GPU sensor readback mapping failed"));
      if(Batch->PhysicalComplete) {
        // Vulkan readback memory is host cached. Keep it mapped while the
        // game-thread callback and its parallel receiver consume the echoes.
        // FBatch releases the mapping on the render thread after consumption.
        Batch->MappedOutput=static_cast<const FVector4f*>(Data);
      } else {
        Batch->Output.SetNumUninitialized(Bytes / sizeof(FVector4f));
        FMemory::Memcpy(Batch->Output.GetData(), Data, Bytes);
        Batch->Readback->Unlock();
        Batch->Readback.Reset();
      }
      CarlaGpuSensors::ProfileSample(TEXT("ray_readback_copied_mb"),Batch->PhysicalComplete?0:Bytes/(1024.*1024.));
      CarlaGpuSensors::ProfileSample(TEXT("ray_readback_copy_ms"),(FPlatformTime::Seconds()-ReadbackCopyStarted)*1000);
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

void PackRayInputs(TConstArrayView<FCarlaGpuRay> Rays,const FVector& PreViewTranslation,
    bool HasMotion,double MotionTime,TArray<FVector4f>& Input)
{
  Input.SetNumUninitialized(Rays.Num()*4, EAllowShrinking::No);
  ParallelFor(TEXT("CarlaPackRayInputs"),Rays.Num(),1024,[&](int32 I) {
    const auto& Ray=Rays[I];
    // Translate in double precision before converting to GPU floats, including
    // on large maps. Preserve the prior arithmetic and all sentinel values.
    const FVector TranslatedOrigin=Ray.Origin+PreViewTranslation;
    Input[I*4]=FVector4f(FVector3f(TranslatedOrigin),Ray.Range);
    Input[I*4+1]=FVector4f(FVector3f(Ray.Direction),Ray.MaterialModel?Ray.AtmosphericAttenuation+1.f:0.f);
    Input[I*4+2]=FVector4f(Ray.PhysicalModel?1.f:0.f,Ray.TimeFraction,Ray.TargetMotion?1.f:0.f,0);
    Input[I*4+3]=FVector4f(Ray.WavelengthNm,Ray.Wetness,Ray.SceneTime>=0 && HasMotion?float(Ray.SceneTime-MotionTime):FLT_MAX,0);
  });
}

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
    Json+=FString::Printf(TEXT("\"%s\":{\"mean\":%.4f,\"p95\":%.4f,\"max\":%.4f,\"samples\":%d}"),*Pair.Key,Sum/Sorted.Num(),Sorted[FMath::CeilToInt(.95*Sorted.Num())-1],Sorted.Last(),Sorted.Num());
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

void DecodePhysicalEchoes(FCarlaPhysicalGpuHit& Hit,const FVector4f* Output)
{
  Hit.EchoCount=FMath::Clamp(int32(Output[0].W),0,8);
  for(uint32 E=0;E<Hit.EchoCount;++E) {
    const auto& V=Output[2+E];auto& Echo=Hit.Echoes[E];
    Echo.Distance=V.X;Echo.SurfaceReturn=V.Z;
    FMemory::Memcpy(&Echo.Component,&V.Y,sizeof(uint32));
    FMemory::Memcpy(&Echo.Flags,&V.W,sizeof(uint32));
  }
}

bool FPhysicalHitView::MotionIncompleteAt(int32 Index) const
{
  if(Output.IsEmpty())return Decoded[Index].MotionIncomplete;
  const auto& Ray=Rays[Index];
  return HasMotion && Ray.TargetMotion && (MissingGeometry ||
      (Ray.SceneTime>=0 && Ray.SceneTime<OldestCoveredTime-1.e-7));
}

bool FPhysicalHitView::HasIncompleteMotion() const
{
  for(int32 I=0;I<Num();++I)if(MotionIncompleteAt(I)) {
    if(!Output.IsEmpty())
      UE_LOG(LogCarla,Warning,TEXT("PHYSICAL_MOTION_INCOMPLETE missing_geometry=%d ray_time=%.12f covered_from=%.12f ray_index=%d rays=%d"),
          int32(MissingGeometry),Rays[I].SceneTime,OldestCoveredTime,I,Num());
    return true;
  }
  return false;
}

FCarlaPhysicalGpuHit FPhysicalHitView::operator[](int32 Index) const
{
  check(IsValidIndex(Index));
  if(Output.IsEmpty())return Decoded[Index];
  FCarlaPhysicalGpuHit Hit;
  const auto* Data=&Output[Index*Stride];
  Hit.UnoccludedRange=Data[1].W;
  Hit.MotionIncomplete=MotionIncompleteAt(Index);
  DecodePhysicalEchoes(Hit,Data);
  return Hit;
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
    const double DecodeStarted=FPlatformTime::Seconds();
    ProfileSample(TEXT("ray_ready_to_game_ms"),(DecodeStarted-Batch->Ready)*1000);
    if(Batch->PhysicalComplete) {
      check(Batch->MappedOutput);
      TConstArrayView<FVector4f> Encoded(Batch->MappedOutput,Batch->Rays.Num()*Batch->HitStride);
      // Validate every ray even if the receiver subsequently models packet loss.
      if(HasCollisionRetraceOverflow(Encoded,Batch->Rays.Num(),Batch->HitStride,
          ParallelRayValidation.GetValueOnGameThread()!=0))
        UE_LOG(LogCarla,Fatal,TEXT("GPU ray exceeded 256 collision-filter retraces; refusing incomplete sensor data"));
      FPhysicalHitView Hits(Encoded,Batch->Rays,Batch->HitStride,
          Batch->Motion.IsValid(),Batch->Motion && Batch->Motion->MissingGeometry>0,
          Batch->Motion?Batch->Motion->OldestCoveredTime:0);
      ProfileSample(TEXT("ray_allocate_hits_ms"),0);
      ProfileSample(TEXT("ray_decode_ms"),(FPlatformTime::Seconds()-DecodeStarted)*1000);
      // Batch owns the mapping and rays until the callback (including its ParallelFor)
      // returns. The generic radar/semantic hit path retains owned hit arrays.
      Batch->PhysicalComplete(Hits);
      continue;
    }
    TArray<FCarlaGpuHit> Hits;
    Hits.SetNum(Batch->Rays.Num());
    ProfileSample(TEXT("ray_allocate_hits_ms"),(FPlatformTime::Seconds()-DecodeStarted)*1000);
    for (int32 I = 0; I < Hits.Num(); ++I)
    {
      const FVector4f& Raw = Batch->Output[I * Batch->HitStride];
      const FVector4f& Normal = Batch->Output[I * Batch->HitStride + 1];
      Hits[I].MotionIncomplete = Batch->Motion && Batch->Rays[I].TargetMotion && (Batch->Motion->MissingGeometry>0 || (Batch->Rays[I].SceneTime>=0 && Batch->Rays[I].SceneTime<Batch->Motion->OldestCoveredTime-1.e-7));
      Hits[I].UnoccludedRange = Batch->Rays[I].PhysicalModel ? Normal.W : Batch->Rays[I].Range;
      if (Batch->Rays[I].PhysicalModel) {
        DecodePhysicalEchoes(Hits[I],&Batch->Output[I*Batch->HitStride]);
      }
      if (Raw.X == -2) UE_LOG(LogCarla, Fatal, TEXT("GPU ray exceeded 256 collision-filter retraces; refusing incomplete sensor data"));
      if (Raw.X < 0 || !Batch->BuildHitResults) continue;
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
    ProfileSample(TEXT("ray_decode_ms"),(FPlatformTime::Seconds()-DecodeStarted)*1000);
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
  const bool Audit=ReadbackPollAudit.GetValueOnGameThread()!=0;
  if (!ReadbackPollGate.TryQueue(CoalesceReadbackPolls.GetValueOnGameThread()!=0))
  {
    if (Audit) ++PollCoalesced;
    return;
  }
  if (Audit)
  {
    ++PollEnqueued;
    PollPeakOutstanding=FMath::Max(PollPeakOutstanding,uint64(ReadbackPollGate.NumOutstanding()));
  }
  LastPoll = Now;
  ENQUEUE_RENDER_COMMAND(CarlaPollSensorReadbacks)([Audit,QueuedAt=Now](FRHICommandListImmediate& Cmd)
  {
    if (Audit)
    {
      ++PollExecuted;
      if (Readbacks.IsEmpty()) ++PollEmpty;
      // The render thread is the sole accumulator; atomic publication permits
      // a game-thread snapshot without a cross-thread lock.
      PollQueueMicroseconds.Store(PollQueueMicroseconds.Load()+uint64(FMath::Max(0.0,FPlatformTime::Seconds()-QueuedAt)*1000000.0));
    }
    // A synchronous client can wait for this copy before allowing another
    // frame. Submit queued RHI work without waiting for the RHI thread or GPU,
    // so the fence can complete even when normal end-of-frame submission stops.
    if (!Readbacks.IsEmpty())
      Cmd.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
    for (int32 I = 0; I < Readbacks.Num();)
      if (Readbacks[I]()) Readbacks.RemoveAt(I); else ++I;
    ReadbackCount.Store(Readbacks.Num());
    // Release after polling and publishing the remaining count. The regular
    // game-thread pump retries pending/new work; no readback or wakeup is lost.
    ReadbackPollGate.Complete();
  });
}

static void SubmitImpl(AActor& Owner,
    TSharedRef<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe> RayStorage,
    TUniqueFunction<void(TArray<FCarlaGpuHit>&&)>&& Complete,bool BuildHitResults,
    TUniqueFunction<void(const CarlaGpuSensors::FPhysicalHitView&)>&& PhysicalComplete)
{
  check(IsInGameThread());
  if (!GRHISupportsInlineRayTracing)
    UE_LOG(LogCarla, Fatal, TEXT("GPU sensors explicitly requested but this RHI has no inline ray tracing"));
  if (InFlight.Contains(&Owner))
    UE_LOG(LogCarla, Fatal, TEXT("GPU sensor %s still has an outstanding frame. Use synchronous mode and consume sensor frames before world.tick()."), *Owner.GetName());
  auto& Rays=*RayStorage;
  if(Rays.IsEmpty()) {
    if(PhysicalComplete)PhysicalComplete(FPhysicalHitView());
    else Complete(TArray<FCarlaGpuHit>());
    return;
  }
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
  auto Batch = MakeShared<FBatch, ESPMode::ThreadSafe>(MoveTemp(RayStorage));
  Batch->Owner = &Owner;
  for (auto It = PackedInputFrames.CreateIterator(); It; ++It)
    if (!It.Key().IsValid()) It.RemoveCurrent();
  if (ReusePackedRayInputs.GetValueOnGameThread() != 0)
  {
    auto& Storage = PackedInputFrames.FindOrAdd(&Owner);
    const bool bReused = Storage.IsValid() && Storage.IsUnique();
    Batch->PackedInputStorage = AcquirePackedRayInputs(Storage);
    ProfileSample(TEXT("ray_input_storage_reused"), bReused ? 1 : 0);
  }
  else
    PackedInputFrames.Remove(&Owner);
  Batch->BuildHitResults=BuildHitResults;
  check(BuildHitResults || !Rays.ContainsByPredicate([](const FCarlaGpuRay& Ray) { return !Ray.PhysicalModel; }));
  Batch->Scene = Owner.GetWorld()->Scene;
  Batch->Actor = Owner.GetUniqueID();
  Batch->Submitted = FPlatformTime::Seconds();
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
  const double CollisionSnapshotStarted=FPlatformTime::Seconds();
  Batch->Collision = Snapshot(Owner.GetWorld());
  ProfileSample(TEXT("collision_snapshot_ms"),(FPlatformTime::Seconds()-CollisionSnapshotStarted)*1000);
  if (Batch->HitStride>2) {
    TArray<UPrimitiveComponent*> RenderComponents;
    for (const auto& C:Batch->Collision->RenderComponents) if(C.IsValid()) RenderComponents.Add(C.Get());
    Batch->Motion=CarlaLidarMotion::Prepare(Owner.GetWorld(),RenderComponents);
    ProfileSample(TEXT("motion_meshes_full"),Batch->Motion->Meshes.Num());
    if(MotionQueryScope.GetValueOnGameThread()!=0) {
      // Optical length bounds geometric path length only when all possible
      // refractive indices are >=1. Preserve the full query for authored IORs
      // below one or invalid material values; never silently clip those paths.
      bool Bounded=true;
      for(int32 I=0;I+1<Batch->Collision->Materials.Num();I+=2) {
        const auto& M=Batch->Collision->Materials[I];const auto& P=Batch->Collision->Materials[I+1];
        if(!FMath::IsFinite(M.W) || !FMath::IsFinite(P.X) || (M.W>0 && P.X<1)) { Bounded=false;break; }
      }
      if(Bounded)Batch->Motion=CarlaLidarMotion::WithinSphere(Batch->Motion,FSphere(QueryCenter,QueryRadius));
    }
    ProfileSample(TEXT("motion_meshes_query"),Batch->Motion->Meshes.Num());
  }
  Batch->Complete = MoveTemp(Complete);
  Batch->PhysicalComplete = MoveTemp(PhysicalComplete);
  InFlight.Add(&Owner, Batch->Submitted);
  ENQUEUE_RENDER_COMMAND(CarlaQueueSensorRays)([Batch, QueryCenter, QueryRadius](FRHICommandListImmediate&)
  {
    SetExternalRayTracingQueryBounds(Batch->Scene, QueryCenter, QueryRadius);
    QueryScenes.Add(Batch->Scene);
    Pending.Add(Batch);
  });
}

void Submit(AActor& Owner,TArray<FCarlaGpuRay>&& Rays,
    TUniqueFunction<void(TArray<FCarlaGpuHit>&&)>&& Complete,bool BuildHitResults)
{
  SubmitImpl(Owner,MakeShared<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe>(MoveTemp(Rays)),
      MoveTemp(Complete),BuildHitResults,{});
}

void SubmitPhysical(AActor& Owner,TSharedRef<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe> Rays,
    TUniqueFunction<void(const CarlaGpuSensors::FPhysicalHitView&)>&& Complete)
{
  SubmitImpl(Owner,MoveTemp(Rays),{},false,MoveTemp(Complete));
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
    MotionGeometryLayout.Empty();MotionGeometryOffsets.Empty();MotionNodeBuffer.SafeRelease();MotionTriangleBuffer.SafeRelease();
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
  Unused.Reset();
  // Dropped completed physical batches can enqueue their final unmaps above.
  FlushRenderingCommands();
  InFlight.Empty();
  PackedInputFrames.Empty();
  CachedCollision.Reset();
  CachedInfrared.Reset();CachedInfraredSignature.Empty();PackedInfraredRevision=0;
  CachedWorld.Reset();
}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaCollisionSnapshotTest,"Carla.Sensors.CollisionSnapshotEquivalence",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaCollisionSnapshotTest::RunTest(const FString&)
{
  using namespace CarlaGpuSensorPrivate;
  UWorld* World=GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
  if (!TestNotNull(TEXT("A live native scene is required"),World)) return false;
  const auto EqualBytes=[](const auto& A,const auto& B)
  {
    return A.Num()==B.Num() && (A.Num()==0 || FMemory::Memcmp(A.GetData(),B.GetData(),A.Num()*sizeof(A[0]))==0);
  };
  double ReferenceMs=0, OptimizedMs=0;
  FSnapshotBuildCounts ReferenceCounts,OptimizedCounts;
  for (int32 Iteration=0;Iteration<7;++Iteration)
  {
    TSharedPtr<FCollisionSnapshot,ESPMode::ThreadSafe> Reference,Optimized;
    const auto Build=[&](bool Skip)
    {
      FSnapshotBuildCounts Counts;
      const double Started=FPlatformTime::Seconds();
      auto Result=BuildSnapshot(World,Skip,&Counts);
      const double Ms=(FPlatformTime::Seconds()-Started)*1000;
      if (Skip) { Optimized=Result; OptimizedCounts=Counts; if (Iteration) OptimizedMs+=Ms; }
      else { Reference=Result; ReferenceCounts=Counts; if (Iteration) ReferenceMs+=Ms; }
    };
    // Exclude the initial material/IR warmup and alternate comparison order.
    if (Iteration%2) { Build(true);Build(false); } else { Build(false);Build(true); }
    TestEqual(TEXT("Every component metadata entry remains present"),Reference->Components.Num(),Optimized->Components.Num());
    for (const auto& Entry:Reference->Components)
    {
      const auto* B=Optimized->Components.Find(Entry.Key);const auto& A=Entry.Value;
      if (!TestNotNull(TEXT("Matching native primitive identity"),B)) continue;
      TestTrue(TEXT("Collision association, velocity, actor, materials and optical predicate are unchanged"),
          A.Component==B->Component && A.Velocity==B->Velocity && A.Actor==B->Actor &&
          A.MaterialOffset==B->MaterialOffset && A.MaterialCount==B->MaterialCount && A.OpticalOnly==B->OpticalOnly && A.IgnoreOpticalCollision==B->IgnoreOpticalCollision);
    }
    TestTrue(TEXT("Complete collision table is byte-identical"),EqualBytes(Reference->Table,Optimized->Table));
    TestTrue(TEXT("Complete optical materials are byte-identical"),EqualBytes(Reference->Materials,Optimized->Materials));
    TestTrue(TEXT("Complete IR information is byte-identical"),EqualBytes(Reference->Infrared->Info,Optimized->Infrared->Info));
    TestTrue(TEXT("Complete IR UVs are byte-identical"),EqualBytes(Reference->Infrared->UVs,Optimized->Infrared->UVs));
    TestTrue(TEXT("Complete IR pixels are byte-identical"),EqualBytes(Reference->Infrared->Pixels,Optimized->Infrared->Pixels));
    TestTrue(TEXT("All render component leases remain identical and ordered"),Reference->RenderComponents==Optimized->RenderComponents);
    TestEqual(TEXT("Only material queries for absent render IDs are skipped"),
        ReferenceCounts.MaterialQueries-OptimizedCounts.MaterialQueries,ReferenceCounts.Unrendered);
  }
  UE_LOG(LogCarla,Display,TEXT("COLLISION_SNAPSHOT_EQUIVALENCE_STATS registered=%d unrendered=%d material_queries_before=%d material_queries_after=%d reference_mean_ms=%.6f optimized_mean_ms=%.6f"),
      ReferenceCounts.Registered,ReferenceCounts.Unrendered,ReferenceCounts.MaterialQueries,OptimizedCounts.MaterialQueries,
      ReferenceMs/6,OptimizedMs/6);
  return true;
}
static FAutoConsoleCommand TestCollisionSnapshot(TEXT("carla.Sensors.TestCollisionSnapshot"),
    TEXT("Compare complete native scene collision/optical/IR snapshots without advancing the world."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
      auto& Framework=FAutomationTestFramework::Get();
      if (Framework.GetCurrentTest()) { UE_LOG(LogCarla,Warning,TEXT("COLLISION_SNAPSHOT_EQUIVALENCE_TEST busy"));return; }
      Framework.StartTestByName(TEXT("FCarlaCollisionSnapshotTest"),0,TEXT("Carla.Sensors.CollisionSnapshotEquivalence"));
      if (!Framework.GetCurrentTest()) { UE_LOG(LogCarla,Warning,TEXT("COLLISION_SNAPSHOT_EQUIVALENCE_TEST result=Fail test_did_not_start"));return; }
      FAutomationTestExecutionInfo Info;const bool Passed=Framework.StopTest(Info);
      UE_LOG(LogCarla,Display,TEXT("COLLISION_SNAPSHOT_EQUIVALENCE_TEST result=%s"),Passed?TEXT("Success"):TEXT("Fail"));
    }));
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaPackedRayInputTest, "Carla.Sensors.PackedRayInputStorage",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaPackedRayInputTest::RunTest(const FString&)
{
  using namespace CarlaGpuSensorPrivate;
  FSharedPackedRayInputs Storage;
  auto BatchLease = AcquirePackedRayInputs(Storage);
  BatchLease->SetNum(16);
  FMemory::Memset(BatchLease->GetData(), 0xa5, BatchLease->Num() * sizeof(FVector4f));
  auto UploadLease = BatchLease;
  const auto* BorrowedAllocation = UploadLease->GetData();
  BatchLease.Reset();
  auto Next = AcquirePackedRayInputs(Storage);
  TestTrue(TEXT("RDG upload lease prevents array reuse"), Next.Get() != UploadLease.Get());
  Next->SetNum(16);
  FMemory::Memzero(Next->GetData(), Next->Num() * sizeof(FVector4f));
  for (int32 I = 0; I < 16 * int32(sizeof(FVector4f)); ++I)
    if (reinterpret_cast<const uint8*>(BorrowedAllocation)[I] != 0xa5)
    { AddError(TEXT("A pending upload was overwritten")); break; }
  auto* NextArray = Next.Get();
  auto* NextAllocation = Next->GetData();
  Next.Reset();
  auto Reused = AcquirePackedRayInputs(Storage);
  TestTrue(TEXT("Completed array is reused"), Reused.Get() == NextArray);
  TestTrue(TEXT("Completed array retains its allocation"), Reused->GetData() == NextAllocation);
  UploadLease.Reset();

  TArray<FCarlaGpuRay> Rays;
  Rays.SetNum(4097);
  const FVector Translation(-1000000000.0, 2000000000.0, -3000000000.0);
  for (int32 I = 0; I < Rays.Num(); ++I)
  {
    auto& Ray = Rays[I];
    Ray.Origin = -Translation + FVector(.125 * I, .03125 * I, -.0625 * I);
    Ray.Direction = FVector(.125, -.25, .5);
    Ray.Range = 15000.f - I * .01f;
    Ray.MaterialModel = I % 2; Ray.PhysicalModel = I % 3; Ray.TargetMotion = I % 5;
    Ray.AtmosphericAttenuation = .004f; Ray.TimeFraction = I / 4096.f;
    Ray.WavelengthNm = 905.f + I % 7; Ray.Wetness = (I % 4) * .125f;
    Ray.SceneTime = I % 2 ? 12345678.125 + I * .000001 : -1;
  }
  Reused->SetNumUninitialized(Rays.Num() * 4);
  const auto* MaximumAllocation = Reused->GetData();
  for (bool HasMotion : {false, true})
    for (int32 Count : {4097, 3, 0, 4097})
    {
      Reused->SetNumUninitialized(Rays.Num() * 4, EAllowShrinking::No);
      FMemory::Memset(Reused->GetData(), 0xa5, Reused->Num() * sizeof(FVector4f));
      CarlaGpuSensors::PackRayInputs(MakeArrayView(Rays.GetData(), Count), Translation, HasMotion, 12345678.0, *Reused);
      TestEqual(TEXT("Shrinking and empty batches retain exact counts"), Reused->Num(), Count * 4);
      TestTrue(TEXT("Shrinking and regrowing retain capacity"), Reused->GetData() == MaximumAllocation);
      TArray<FVector4f> Reference;
      for (int32 I = 0; I < Count; ++I)
      {
        const auto& Ray = Rays[I];
        Reference.Add(FVector4f(FVector3f(Ray.Origin + Translation), Ray.Range));
        Reference.Add(FVector4f(FVector3f(Ray.Direction), Ray.MaterialModel ? Ray.AtmosphericAttenuation + 1.f : 0.f));
        Reference.Add(FVector4f(Ray.PhysicalModel ? 1.f : 0.f, Ray.TimeFraction, Ray.TargetMotion ? 1.f : 0.f, 0));
        Reference.Add(FVector4f(Ray.WavelengthNm, Ray.Wetness, Ray.SceneTime >= 0 && HasMotion ? float(Ray.SceneTime - 12345678.0) : FLT_MAX, 0));
      }
      TestTrue(TEXT("Poisoned reused storage matches serial encoding bit for bit"),
          Count == 0 || FMemory::Memcmp(Reused->GetData(), Reference.GetData(), Reference.Num() * sizeof(FVector4f)) == 0);
    }
  return true;
}
static FAutoConsoleCommand TestPackedRayInputStorage(
    TEXT("carla.Sensors.TestPackedRayInputs"), TEXT("Run CPU packed ray ownership and encoding tests."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
      auto& Framework = FAutomationTestFramework::Get();
      if (Framework.GetCurrentTest()) { UE_LOG(LogCarla, Warning, TEXT("PACKED_RAY_INPUT_TEST busy")); return; }
      Framework.StartTestByName(TEXT("FCarlaPackedRayInputTest"), 0, TEXT("Carla.Sensors.PackedRayInputStorage"));
      if (!Framework.GetCurrentTest()) { UE_LOG(LogCarla, Warning, TEXT("PACKED_RAY_INPUT_TEST result=Fail test_did_not_start")); return; }
      FAutomationTestExecutionInfo Info;
      const bool Passed = Framework.StopTest(Info);
      UE_LOG(LogCarla, Display, TEXT("PACKED_RAY_INPUT_TEST result=%s"), Passed ? TEXT("Success") : TEXT("Fail"));
    }));
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaRayValidationTest,"Carla.Sensors.PhysicalRayValidation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaRayValidationTest::RunTest(const FString&)
{
  using namespace CarlaGpuSensorPrivate;
  for (int32 Stride : {2,10,16})
    for (int32 Count : {0,1,4096,4097,65536,560001})
    {
      TArray<FVector4f> Data;Data.SetNumUninitialized(Count*Stride+1);
      for (auto& V : Data) V=FVector4f(-2,3,4,5);
      for (int32 I=0;I<Count;++I) Data[I*Stride].X=I%2?-1.f:12.5f;
      const TArray<FVector4f> Before=Data;
      for (bool Parallel : {false,true})
      {
        TestFalse(TEXT("Misses and padding overflow markers are valid"),
            HasCollisionRetraceOverflow(Data,Count,Stride,Parallel));
        TestTrue(TEXT("Validation leaves all readback bytes unchanged"),
            FMemory::Memcmp(Data.GetData(),Before.GetData(),Data.Num()*sizeof(FVector4f))==0);
        if (Count)
          for (int32 Index : {0,Count/2,Count-1})
          {
            const float Saved=Data[Index*Stride].X;
            Data[Index*Stride].X=-2;
            TestTrue(TEXT("First, middle and last overflowing rays are rejected"),
                HasCollisionRetraceOverflow(Data,Count,Stride,Parallel));
            Data[Index*Stride].X=Saved;
          }
      }
    }
  return true;
}
static FAutoConsoleCommand TestRayValidation(TEXT("carla.Sensors.TestRayValidation"),
    TEXT("Test bounded physical-ray validation, overflow detection and readback immutability."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
      auto& Framework=FAutomationTestFramework::Get();
      if(Framework.GetCurrentTest()){UE_LOG(LogCarla,Warning,TEXT("RAY_VALIDATION_TEST busy"));return;}
      Framework.StartTestByName(TEXT("FCarlaRayValidationTest"),0,TEXT("Carla.Sensors.PhysicalRayValidation"));
      if(!Framework.GetCurrentTest()){UE_LOG(LogCarla,Warning,TEXT("RAY_VALIDATION_TEST result=Fail test_did_not_start"));return;}
      FAutomationTestExecutionInfo Info;const bool Passed=Framework.StopTest(Info);
      UE_LOG(LogCarla,Display,TEXT("RAY_VALIDATION_TEST result=%s"),Passed?TEXT("Success"):TEXT("Fail"));
    }));
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaMotionAtlasTest,"Carla.Sensors.MotionAtlasAppend",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaMotionAtlasTest::RunTest(const FString&)
{
  using namespace CarlaGpuSensorPrivate;
  auto Geometry=[](int32 Nodes,int32 Triangles,float Tag)->FMotionGeometry
  {
    auto G=MakeShared<CarlaLidarMotion::MeshGeometry,ESPMode::ThreadSafe>();
    G->Nodes.SetNum(Nodes);G->Triangles.SetNum(Triangles);
    for(int32 I=0;I<Nodes*2;++I)G->PackedNodes.Add(FVector4f(Tag,I,31,47));
    for(int32 I=0;I<Triangles*7;++I)G->PackedTriangles.Add(FVector4f(Tag,I,59,61));
    return G;
  };
  const auto A=Geometry(2,3,101),B=Geometry(4,2,202),C=Geometry(3,5,303);
  const TArray<FMotionGeometry> Layout={A,B};const TArray<FIntPoint> Offsets={FIntPoint(0,0),FIntPoint(2,3)};
  constexpr uint64 Limit=512ull*1024*1024;
  auto Plan=[&](const TArray<FMotionGeometry>& Current,bool Allow=true,uint64 Budget=Limit)
  {return PlanMotionAtlas(Layout,Offsets,Current,12,35,Allow,Budget);};
  for(const TArray<FMotionGeometry>& Current:{TArray<FMotionGeometry>{A,B},TArray<FMotionGeometry>{B,A,B},TArray<FMotionGeometry>{B},TArray<FMotionGeometry>{}})
  {
    const auto P=Plan(Current,false);
    TestTrue(TEXT("Unchanged, reordered, repeated, stopped and empty sets reuse"),P.Reuse && !P.Append && P.Missing.IsEmpty());
    for(int32 I=0;I<Current.Num();++I)TestEqual(TEXT("Cached offset unchanged"),P.CurrentOffsets[I],Offsets[Layout.IndexOfByKey(Current[I])]);
  }
  auto P=Plan({C,B,C,A});
  TestTrue(TEXT("Only one missing immutable identity is appended"),P.Append && !P.Reuse && P.Missing.Num()==1 && P.Missing[0]==C);
  TestEqual(TEXT("New offset follows existing prefix"),P.CurrentOffsets[0],FIntPoint(6,5));
  TestEqual(TEXT("Duplicate uses same appended offset"),P.CurrentOffsets[2],P.CurrentOffsets[0]);
  const uint64 Bytes=uint64(P.NodeElements+P.TriangleElements)*sizeof(FVector4f);
  TestTrue(TEXT("Exact byte budget permits append"),Plan({C},true,Bytes).Append);
  TestFalse(TEXT("One byte over budget compacts instead"),Plan({C},true,Bytes-1).Append);
  TestFalse(TEXT("Disabled append takes full upload on a new identity"),Plan({C},false).Append);
  TestFalse(TEXT("Cold atlas uses full upload"),PlanMotionAtlas({}, {}, {A},0,0,true,Limit).Append);
  TestFalse(TEXT("Mismatched buffer descriptor uses full upload"),PlanMotionAtlas(Layout,Offsets,{C},13,35,true,Limit).Append);
  auto WrongOffsets=Offsets;WrongOffsets[1].X++;
  TestFalse(TEXT("Non-dense offsets use full upload"),PlanMotionAtlas(Layout,WrongOffsets,{C},12,35,true,Limit).Append);
  TestFalse(TEXT("Invalid current geometry uses full upload"),Plan({FMotionGeometry()}).Append);
  const auto SameBytes=Geometry(2,3,101);
  TestTrue(TEXT("New/deforming identities never alias an old offset by content"),Plan({SameBytes}).Append);
  TestEqual(TEXT("Original layout unchanged by planning"),Layout.Num(),2);
  TestEqual(TEXT("Original geometry remains unchanged"),A->PackedNodes[0],FVector4f(101,0,31,47));

  // Emulate the two GPU copies on CPU, then compare every referenced geometry
  // byte and every per-frame transform/time/lookup field to the full uploader.
  TArray<FMotionGeometry> Atlas=Layout;TArray<FIntPoint> AtlasOffsets=Offsets;
  TArray<FVector4f> Nodes=A->PackedNodes,Triangles=A->PackedTriangles;
  Nodes.Append(B->PackedNodes);Triangles.Append(B->PackedTriangles);
  for(int32 Generation=0;Generation<40;++Generation)
  {
    const auto New=Geometry(1+Generation%5,1+Generation%7,float(Generation+400));
    const TArray<FMotionGeometry> Current={New,Atlas.Last(),A,New};
    const auto Next=PlanMotionAtlas(Atlas,AtlasOffsets,Current,Nodes.Num(),Triangles.Num(),true,Limit);
    TestTrue(TEXT("Mixed new/current/history set extends atlas"),Next.Append);
    for(const auto& G:Next.Missing){Nodes.Append(G->PackedNodes);Triangles.Append(G->PackedTriangles);}
    Atlas.Append(Next.Missing);AtlasOffsets.Append(Next.MissingOffsets);
    TestEqual(TEXT("Node descriptor covers all bytes"),uint32(Nodes.Num()),Next.NodeElements);
    TestEqual(TEXT("Triangle descriptor covers all bytes"),uint32(Triangles.Num()),Next.TriangleElements);
    CarlaLidarMotion::Snapshot Snapshot;Snapshot.Time=17.75;
    for(int32 I=0;I<Current.Num();++I)
    {
      CarlaLidarMotion::Mesh Mesh;Mesh.Geometry=Current[I];Mesh.Primitive=23+I;
      Mesh.Before=FTransform(FRotator(7,I*13,3),FVector(100+I,-300,17),FVector(1.2,.7,1.5));
      Mesh.After=FTransform(FRotator(-5,I*19,11),FVector(900+I,400,29),FVector(.8,1.4,.9));
      Mesh.BeforeTime=17.5;Mesh.AfterTime=17.9;Mesh.Center=FVector(501,52,26);Mesh.Radius=771;
      Snapshot.Meshes.Add(Mesh);
      const auto O=Next.CurrentOffsets[I];
      TestTrue(TEXT("Every appended node byte matches its original geometry"),
          FMemory::Memcmp(Nodes.GetData()+O.X*2,Current[I]->PackedNodes.GetData(),Current[I]->PackedNodes.Num()*sizeof(FVector4f))==0);
      TestTrue(TEXT("Every appended triangle byte matches its original geometry"),
          FMemory::Memcmp(Triangles.GetData()+O.Y*7,Current[I]->PackedTriangles.GetData(),Current[I]->PackedTriangles.Num()*sizeof(FVector4f))==0);
    }
    TArray<FVector4f> FullInfo,FullNodes,FullTriangles,AtlasInfo,NoNodes,NoTriangles;
    TArray<FUintVector4> FullLookup,AtlasLookup;
    Snapshot.GpuBuffers(FVector(100000,-250000,350000),FullInfo,FullNodes,FullTriangles,FullLookup);
    Snapshot.GpuBuffers(FVector(100000,-250000,350000),AtlasInfo,NoNodes,NoTriangles,AtlasLookup,false,&Next.CurrentOffsets);
    for(int32 I=0;I<Current.Num();++I)
    {
      const uint32 NodeBase=Next.CurrentOffsets[I].X,TriangleBase=Next.CurrentOffsets[I].Y;
      FMemory::Memcpy(&FullInfo[I*8].W,&NodeBase,sizeof(NodeBase));
      FMemory::Memcpy(&FullInfo[I*8+4].W,&TriangleBase,sizeof(TriangleBase));
    }
    TestTrue(TEXT("Transforms, times, counts and metadata match full uploader bit for bit"),
        FullInfo.Num()==AtlasInfo.Num() && FMemory::Memcmp(FullInfo.GetData(),AtlasInfo.GetData(),FullInfo.Num()*sizeof(FVector4f))==0);
    TestTrue(TEXT("Primitive lookup matches full uploader bit for bit"),
        FullLookup.Num()==AtlasLookup.Num() && FMemory::Memcmp(FullLookup.GetData(),AtlasLookup.GetData(),FullLookup.Num()*sizeof(FUintVector4))==0);
    TestTrue(TEXT("Existing atlas is reused after publication"),
        PlanMotionAtlas(Atlas,AtlasOffsets,Current,Nodes.Num(),Triangles.Num(),true,Limit).Reuse);
  }
  return true;
}
static FAutoConsoleCommand TestMotionAtlas(TEXT("carla.Sensors.TestMotionAtlas"),
    TEXT("Test incremental geometry identity, offsets, byte bounds and complete per-frame encoding."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
      auto& Framework=FAutomationTestFramework::Get();
      if(Framework.GetCurrentTest()){UE_LOG(LogCarla,Warning,TEXT("MOTION_ATLAS_TEST busy"));return;}
      Framework.StartTestByName(TEXT("FCarlaMotionAtlasTest"),0,TEXT("Carla.Sensors.MotionAtlasAppend"));
      if(!Framework.GetCurrentTest()){UE_LOG(LogCarla,Warning,TEXT("MOTION_ATLAS_TEST result=Fail test_did_not_start"));return;}
      FAutomationTestExecutionInfo Info;const bool Passed=Framework.StopTest(Info);
      UE_LOG(LogCarla,Display,TEXT("MOTION_ATLAS_TEST result=%s"),Passed?TEXT("Success"):TEXT("Fail"));
    }));
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/InfraredSnapshotReuse.inl"
#include "Tests/ReadbackPollGate.inl"
#endif
