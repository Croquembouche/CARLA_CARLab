// Native actor/scheduler/serialization/streaming fixture. MIT license.
// Included after PhysicalLidarMaterialFixture.inl; uses its owned main view.
#include "Carla/Sensor/RayCastLidar.h"
#include "AlphaSourceReference.h"
#include "EngineUtils.h"
#include "Carla/Game/CarlaEpisode.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "UObject/UnrealType.h"
#include <util/disable-ue4-macros.h>
#include <carla/streaming/Client.h>
#include <carla/streaming/Server.h>
#include <carla/sensor/s11n/SensorHeaderSerializer.h>
#include <mutex>
#include <util/enable-ue4-macros.h>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaPhysicalSensorStreamFixture,"Carla.Lidar.PhysicalSensorStreamFixture",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaPhysicalSensorStreamFixture::RunTest(const FString& Parameters) {
  if(!TestTrue(TEXT("Explicit backend parameter required"),Parameters==TEXT("cpu") || Parameters==TEXT("gpu") || Parameters==TEXT("cpu-mask") || Parameters==TEXT("gpu-mask")))return false;
  const bool GPU=Parameters.StartsWith(TEXT("gpu")),MaskStudy=Parameters.EndsWith(TEXT("-mask"));
  if(!TestTrue(TEXT("Rendered fixture supports hardware inline tracing"),GRHISupportsInlineRayTracing))return false;
  auto* Enabled=IConsoleManager::Get().FindConsoleVariable(TEXT("carla.Sensors.GpuRayTracing"));
  const int32 OldEnabled=Enabled->GetInt();Enabled->Set(GPU?1:0,ECVF_SetByCode);
  struct RestoreEnabled {IConsoleVariable* C;int32 V;~RestoreEnabled(){C->Set(V,ECVF_SetByCode);}} RestoreMode{Enabled,OldEnabled};
  TestEqual(TEXT("Requested production backend is selected"),CarlaGpuSensors::IsEnabled(),GPU);
  const FString Dir=FPaths::ProjectSavedDir()/TEXT("PhysicalSensorFixture")/Parameters;IFileManager::Get().MakeDirectory(*Dir,true);
  const FString Original=FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json");FString Error;
  if(!TestTrue(TEXT("Original registry available"),CarlaLidarInfrared::Reload(Original,Error)))return false;
  struct RestoreIR {FString P;~RestoreIR(){FString E;CarlaLidarInfrared::Reload(P,E);}} Restore{Original};
  const FString Registry=Dir/TEXT("materials.json");
  const FString Text=TEXT(R"({"version":1,"calibration":{"status":"uncalibrated-native-stream-fixture"},"materials":[{"name":"fixture-asphalt","wavelengths_nm":[905,1550],"dry_low":[0.1,0,0,0],"dry_high":[0.05,0,0,0],"wet_multipliers":[0.5,1,1,1]},{"name":"fixture-plaster","wavelengths_nm":[905,1550],"dry_low":[0.3,0,0,0],"dry_high":[0.2,0,0,0],"wet_multipliers":[0.8,1,1,1]},{"name":"fixture-instance-mask","dry_low":[0.4,0,0,0],"dry_high":[0.4,0,0,0],"mask":{"width":2,"height":2,"rgba":[0.1,1,1,1,0.3,1,1,1,0.6,1,1,1,1,1,1,1]}},{"name":"fixture-instance-uniform","dry_low":[0.4,0,0,0],"dry_high":[0.4,0,0,0]},{"name":"fixture-instance-shift","dry_low":[0.4,0,0,0],"dry_high":[0.4,0,0,0],"uv_offset":[0.5,0.5],"mask":{"width":2,"height":2,"rgba":[0.1,1,1,1,0.3,1,1,1,0.6,1,1,1,1,1,1,1]}},{"name":"fixture-instance-zero","dry_low":[0.4,0,0,0],"dry_high":[0.4,0,0,0],"mask":{"width":2,"height":2,"rgba":[0,0,0,0,0.3,1,1,1,0.6,1,1,1,1,1,1,1]}},{"name":"fixture-opacity-checker","dry_low":[0.4,0,0,0],"dry_high":[0.4,0,0,0],"opacity":{"width":2,"height":2,"values":[0,1,1,1],"clip_value":0.5,"uv_offset":[0,0]}},{"name":"fixture-opacity-shift","dry_low":[0.4,0,0,0],"dry_high":[0.4,0,0,0],"opacity":{"width":2,"height":2,"values":[0,1,1,1],"clip_value":0.5,"uv_offset":[0.5,0.5]}},{"name":"fixture-opacity-clear","dry_low":[0.4,0,0,0],"dry_high":[0.4,0,0,0],"opacity":{"width":2,"height":2,"values":[0,0,0,0],"clip_value":0.5,"uv_offset":[0,0]}},{"name":"fixture-opacity-clamp","dry_low":[0.4,0,0,0],"dry_high":[0.4,0,0,0],"opacity":{"width":2,"height":2,"values":[0,1,1,1],"clip_value":0.5,"uv_offset":[-1,0],"address_x":"clamp","address_y":"clamp"}}]})");
  FString EffectiveText=Text;
  const FString ActualLibrary=FPlatformMisc::GetEnvironmentVariable(TEXT("TOWN02_ACTUAL_ALPHA_LIBRARY"));
  if(!ActualLibrary.IsEmpty()) {
    FString ActualText;TSharedPtr<FJsonObject> Actual,Combined;
    if(!TestTrue(TEXT("Actual alpha registry library readable"),FFileHelper::LoadFileToString(ActualText,*ActualLibrary)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ActualText),Actual)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Combined)))return false;
    auto Materials=Combined->GetArrayField(TEXT("materials"));Materials.Append(Actual->GetArrayField(TEXT("materials")));Combined->SetArrayField(TEXT("materials"),Materials);FJsonSerializer::Serialize(Combined.ToSharedRef(),TJsonWriterFactory<>::Create(&EffectiveText));
  }
  FFileHelper::SaveStringToFile(EffectiveText,*Registry,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  if(!TestTrue(TEXT("Fixture registry loaded"),CarlaLidarInfrared::Reload(Registry,Error)))return false;
  const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
  auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,GMaxRHIFeatureLevel,&Init);
  struct DestroyWorld {UWorld* W;~DestroyWorld(){W->DestroyWorld(false);FlushRenderingCommands();}} Destroy{World};
  auto* Episode=NewObject<UCarlaEpisode>(World);Episode->bIsPrimaryServer=false;Episode->AddToRoot();
  struct Unroot {UObject* O;~Unroot(){O->RemoveFromRoot();}} RootEpisode{Episode};
  auto* WeatherClass=LoadClass<AWeather>(nullptr,TEXT("/Game/Carla/Blueprints/Weather/BP_CarlaWeather.BP_CarlaWeather_C"));
  if(!TestNotNull(TEXT("Native weather blueprint is available"),WeatherClass))return false;
  auto* Weather=World->SpawnActor<AWeather>(WeatherClass);
  auto* WeatherProperty=FindFProperty<FObjectPropertyBase>(UCarlaEpisode::StaticClass(),TEXT("Weather"));
  if(!TestNotNull(TEXT("Native episode weather property"),WeatherProperty))return false;
  WeatherProperty->SetObjectPropertyValue_InContainer(Episode,Weather);
  TestEqual(TEXT("Owned episode resolves its native weather actor"),Episode->GetWeather(),Weather);
  // Only the owned fixture object's declared property is assigned. No CARLA
  // game instance, engine/server initialization, map BeginPlay or RPC is used.
  auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
  auto* Target=World->SpawnActor<AStaticMeshActor>();auto* C=Target->GetStaticMeshComponent();
  C->SetMobility(EComponentMobility::Movable);C->SetCollisionProfileName(TEXT("NoCollision"));C->SetStaticMesh(Cube);C->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));
  Target->SetActorLocation(FVector(1000,0,0));Target->SetActorScale3D(FVector(1,100,100));
  auto* Legacy=World->SpawnActor<AStaticMeshActor>();auto* LC=Legacy->GetStaticMeshComponent();
  LC->SetMobility(EComponentMobility::Movable);LC->SetStaticMesh(Cube);LC->SetCollisionProfileName(TEXT("BlockAll"));LC->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);
  Legacy->SetActorLocation(FVector(1100,0,0));Legacy->SetActorScale3D(FVector(1,100,100));LC->SetVisibility(false);LC->SetHiddenInGame(true);
  auto* InstancesActor=World->SpawnActor<AActor>();auto* IC=NewObject<UHierarchicalInstancedStaticMeshComponent>(InstancesActor);
  InstancesActor->SetRootComponent(IC);InstancesActor->AddInstanceComponent(IC);IC->SetMobility(EComponentMobility::Static);IC->SetCollisionProfileName(TEXT("NoCollision"));IC->SetStaticMesh(Cube);
  IC->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));IC->ComponentTags.Add(TEXT("LidarIR=fixture-asphalt"));IC->RegisterComponent();
  // The root/component transform is identity; the actual instance is at13m.
  IC->AddInstance(FTransform(FQuat::Identity,FVector(1300,0,0),FVector(1,100,100)));
  IC->AddInstance(FTransform(FRotator(0,30,0),FVector(1800,15000,0),FVector(1,5,10)));IC->SetVisibility(false);
  CarlaMaterialFixture::MainView View(World);World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
  auto* Sensor=World->SpawnActor<ARayCastLidar>();Sensor->SetEpisode(*Episode);
  FLidarDescription D;D.Channels=32;D.Range=8000;D.PointsPerSecond=200000;D.RotationFrequency=20;D.PhysicalModel=true;D.MaterialModel=true;D.OutputFormat=TEXT("extended");
  Sensor->Set(D);if(!TestTrue(TEXT("Actual physical LiDAR actor configures"),Sensor->IsConfigured()))return false;
  C->ComponentTags.Add(TEXT("LidarIR=fixture-asphalt"));
  FCollisionQueryParams TieParams;TieParams.bTraceComplex=true;TieParams.bReturnFaceIndex=true;TieParams.AddIgnoredActor(Sensor);
  Legacy->SetActorLocation(FVector(999.99,0,0));
  const auto NearHidden=CarlaLidarOptics::Trace(*Sensor,FVector::ZeroVector,FVector(1,0,0),8000,0,TieParams,nullptr,nullptr,1,905,0);
  TestTrue(TEXT("Near-coincident hidden collider resolves visible revised optical surface"),NearHidden.Hit.GetComponent()==C);
  TestTrue(TEXT("Near-coincident tie preserves actual visible surface range"),FMath::Abs(NearHidden.Hit.Distance-950.f)<.001f);
  Legacy->SetActorLocation(FVector(999,0,0));
  const auto FarHidden=CarlaLidarOptics::Trace(*Sensor,FVector::ZeroVector,FVector(1,0,0),8000,0,TieParams,nullptr,nullptr,1,905,0);
  TestTrue(TEXT("Hidden tie tolerance does not cross a1cm geometry separation"),FarHidden.Hit.GetComponent()==LC);
  LC->ComponentTags.Add(TEXT("LidarIgnore"));
  const auto IgnoredHidden=CarlaLidarOptics::Trace(*Sensor,FVector::ZeroVector,FVector(1,0,0),8000,0,TieParams,nullptr,nullptr,1,905,0);
  TestTrue(TEXT("Explicit optical exclusion crosses retained hidden collider separation"),IgnoredHidden.Hit.GetComponent()==C);
  FHitResult MechanicalHit;
  const bool MechanicalFound=World->ParallelLineTraceSingleByChannel(MechanicalHit,FVector::ZeroVector,FVector(8000,0,0),ECC_GameTraceChannel2,TieParams);
  TestTrue(TEXT("Optical exclusion preserves ordinary native collision"),MechanicalFound && MechanicalHit.GetComponent()==LC);
  LC->ComponentTags.Remove(TEXT("LidarIgnore"));
  Legacy->SetActorLocation(FVector(999.99,0,0));LC->SetVisibility(true);LC->SetHiddenInGame(false);
  const auto VisibleBlocker=CarlaLidarOptics::Trace(*Sensor,FVector::ZeroVector,FVector(1,0,0),8000,0,TieParams,nullptr,nullptr,1,905,0);
  TestTrue(TEXT("Ordinary visible occluder keeps exact nearest-surface selection"),VisibleBlocker.Hit.GetComponent()==LC);
  Legacy->SetActorLocation(FVector(1100,0,0));LC->SetVisibility(false);LC->SetHiddenInGame(true);
  // A localhost-only ephemeral streaming endpoint is not a CARLA RPC server.
  carla::streaming::Server Server("127.0.0.1",0);Server.AsyncRun(2);auto Stream=Server.MakeStream();Sensor->SetDataStream(FDataStream(Stream));
  std::mutex Mutex;std::vector<carla::Buffer> Received;
  carla::streaming::Client Client;const auto Token=Stream.token();
  Client.Subscribe(Token,[&](carla::Buffer B){std::lock_guard<std::mutex> Lock(Mutex);Received.emplace_back(std::move(B));});Client.AsyncRun(2);
  const double ConnectLimit=FPlatformTime::Seconds()+10;
  while(!Sensor->AreClientsListening() && FPlatformTime::Seconds()<ConnectLimit)FPlatformProcess::Sleep(.005f);
  if(!TestTrue(TEXT("Actual SDK streaming client subscribes to sensor"),Sensor->AreClientsListening()))return false;
  const uint64 OldFrame=FCarlaEngine::GetFrameCounter();FCarlaEngine::ResetFrameCounter(10000);
  struct RestoreFrame {uint64 F;~RestoreFrame(){FCarlaEngine::ResetFrameCounter(F);}} Counter{OldFrame};
  TArray<TSharedPtr<FJsonValue>> Rows;double Time=0;uint64 ExpectedSequence=0;
  auto Capture=[&](const FString& Kind,const FString& Material,double Wet,double Rain,uint32 Seed,double Delta,double ExpectedFront,bool Moving) {
    D.RandomSeed=Seed;Sensor->Set(D);C->ComponentTags.Reset();C->ComponentTags.Add(FName(*(TEXT("LidarIR=")+Material)));
    FWeatherParameters W;W.Cloudiness=20;W.SunAltitudeAngle=35;W.Wetness=Wet;W.Precipitation=Rain;W.FogDensity=0;W.DustStorm=0;Weather->SetWeather(W);
    const double Start=Time;Time+=Delta;World->TimeSeconds=Time;World->DeltaTimeSeconds=Delta;Episode->SetReplicatedElapsedGameTime(Time);
    ++GFrameCounter;FCarlaEngine::UpdateFrameCounter();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
    const uint64 Frame=FCarlaEngine::GetFrameCounter();const size_t ExpectedCount=ExpectedSequence+1;
    Sensor->Tick(float(Delta));Sensor->PostPhysTickInternal(World,LEVELTICK_All,float(Delta));
    if(GPU)TestTrue(TEXT("Actual actor hardware frame is pending"),CarlaGpuSensors::HasPendingFrames());
    View.Draw();const double Limit=FPlatformTime::Seconds()+45;
    while(FPlatformTime::Seconds()<Limit) {
      CarlaGpuSensors::Pump();FlushRenderingCommands();bool Ready;{std::lock_guard<std::mutex> Lock(Mutex);Ready=Received.size()>=ExpectedCount;}
      if(Ready)break;FPlatformProcess::Sleep(.005f);
    }
    carla::Buffer Packet;{std::lock_guard<std::mutex> Lock(Mutex);if(Received.size()<ExpectedCount){AddError(TEXT("Actual actor stream did not deliver packet"));CarlaGpuSensors::Shutdown();return false;}Packet.copy_from(Received[ExpectedSequence]);}
    using SH=carla::sensor::s11n::SensorHeaderSerializer;using PH=carla::sensor::data::PhysicalLidarHeader;using Point=carla::sensor::data::PhysicalLidarDetection;
    SH::Header Header;PH Physical;FMemory::Memcpy(&Header,Packet.data(),sizeof(Header));FMemory::Memcpy(&Physical,Packet.data()+SH::header_offset,sizeof(Physical));
    TestEqual(TEXT("Native capture frame survives asynchronous delivery"),Header.frame,Frame);
    TestTrue(TEXT("Native capture timestamp is episode clock"),FMath::IsNearlyEqual(Header.timestamp,Time,1.e-8));
    TestTrue(TEXT("Native sensor header retains capture transform"),FMath::Abs(Header.sensor_transform.location.x)<1.e-7);
    TestTrue(TEXT("Production scheduler uses consecutive scan boundaries"),FMath::Abs(Physical.scan_start-Start)<1.e-6 && FMath::Abs(Physical.scan_end-Time)<1.e-8);
    // Integer firing epochs and a float actor delta can straddle one boundary
    // pulse. Retain the actual count, with at most one pulse of quantization.
    TestTrue(TEXT("Production pulse count preserves requested rate within one boundary pulse"),FMath::Abs(double(Physical.pulse_count)-Delta*D.PointsPerSecond)<=1.00001);
    TestEqual(TEXT("Production header channels"),Physical.channels,D.Channels);TestEqual(TEXT("Production sequence"),Physical.sequence,ExpectedSequence);
    TestTrue(TEXT("Production header angle is populated"),FMath::Abs(Physical.horizontal_angle)>0.01);
    TestEqual(TEXT("Actual serialized point count/stride"),uint64(Packet.size()),uint64(SH::header_offset+sizeof(PH)+Physical.point_count*sizeof(Point)));
    int32 ROI=0,Atmospheric=0;double Sum=0;TArray<double> MotionErrors,EndpointErrors;
    int32 SpatialCount[4]={0,0,0,0};double SpatialSum[4]={0,0,0,0};
    const auto* Points=reinterpret_cast<const Point*>(Packet.data()+SH::header_offset+sizeof(PH));
    for(uint32 I=0;I<Physical.point_count;++I){const auto& P=Points[I];Atmospheric+=(P.flags&Atmosphere)!=0;
      TestEqual(TEXT("Scheduled pulse id preserves channel"),uint32(P.pulse_id%D.Channels),uint32(P.channel));
      if((P.flags&Surface) && !(P.flags&(Atmosphere|FalseAlarm)) && FMath::Abs(P.y)<2 && FMath::Abs(P.z)<2) {
        const double Wanted=Moving?ExpectedFront+.2*P.time_offset/Delta:ExpectedFront;
        if(FMath::Abs(P.x-Wanted)<.1){++ROI;Sum+=P.intensity;MotionErrors.Add(FMath::Abs(P.x-Wanted));EndpointErrors.Add(FMath::Abs(P.x-(ExpectedFront+.2)));
          // Exclude the quadrant boundaries by50cm. Beam footprints are much
          // smaller here, so these bins do not mix adjacent mask pixels.
          if(FMath::Abs(P.y)>.5 && FMath::Abs(P.z)>.5){const int32 Bin=(P.y>0?1:0)+(P.z>0?2:0);++SpatialCount[Bin];SpatialSum[Bin]+=P.intensity;}
        }
      }
    }
    TestTrue(TEXT("Returns hit actual revised/instanced surface rather than legacy collider"),ROI>100);
    if(Moving && MotionErrors.Num()){MotionErrors.Sort();EndpointErrors.Sort();TestTrue(TEXT("Moving-mesh returns follow production sub-frame interpolation"),MotionErrors[MotionErrors.Num()/2]<.005);TestTrue(TEXT("Moving return differs measurably from endpoint-only geometry"),EndpointErrors[EndpointErrors.Num()/2]>.02);TestEqual(TEXT("Moving geometry has complete coverage"),Physical.flags&8u,0u);}
    if(Rain==0)TestEqual(TEXT("No atmosphere in dry/wet-only production actor controls"),Atmospheric,0);
    else TestTrue(TEXT("Production rainy actor adds atmospheric returns"),Atmospheric>0);
    const FString Name=FString::Printf(TEXT("%02llu-%s-%s-seed%u.bin"),ExpectedSequence,*Kind,*Material,Seed);const auto Bytes=MakeArrayView(Packet.data(),Packet.size());
    TestTrue(TEXT("Full native stream packet saved"),FFileHelper::SaveArrayToFile(Bytes,*(Dir/Name)));FSHAHash Hash;FSHA1::HashBuffer(Packet.data(),Packet.size(),Hash.Hash);
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),Kind);Row->SetStringField(TEXT("material"),Material);Row->SetStringField(TEXT("raw"),Name);Row->SetStringField(TEXT("sha1"),Hash.ToString());Row->SetBoolField(TEXT("gpu"),GPU);
    Row->SetNumberField(TEXT("seed"),Seed);Row->SetNumberField(TEXT("wetness"),Wet);Row->SetNumberField(TEXT("precipitation"),Rain);Row->SetNumberField(TEXT("frame"),Header.frame);Row->SetNumberField(TEXT("timestamp"),Header.timestamp);Row->SetNumberField(TEXT("sensor_type"),Header.sensor_type);Row->SetNumberField(TEXT("sensor_header_bytes"),SH::header_offset);
    Row->SetNumberField(TEXT("scan_start"),Physical.scan_start);Row->SetNumberField(TEXT("scan_end"),Physical.scan_end);Row->SetNumberField(TEXT("sequence"),Physical.sequence);Row->SetNumberField(TEXT("pulse_count"),Physical.pulse_count);Row->SetNumberField(TEXT("point_count"),Physical.point_count);Row->SetNumberField(TEXT("physical_flags"),Physical.flags);Row->SetNumberField(TEXT("roi_count"),ROI);Row->SetNumberField(TEXT("roi_mean_intensity"),ROI?Sum/ROI:0);Row->SetNumberField(TEXT("atmosphere_count"),Atmospheric);Row->SetNumberField(TEXT("expected_front_m"),ExpectedFront);Row->SetNumberField(TEXT("delta_seconds"),Delta);
    if(Kind.StartsWith(TEXT("spatial-"))) {
      TArray<TSharedPtr<FJsonValue>> Bins;
      for(int32 Bin=0;Bin<4;++Bin){auto Item=MakeShared<FJsonObject>();Item->SetNumberField(TEXT("bin"),Bin);Item->SetNumberField(TEXT("count"),SpatialCount[Bin]);Item->SetNumberField(TEXT("mean_intensity"),SpatialCount[Bin]?SpatialSum[Bin]/SpatialCount[Bin]:0);Bins.Add(MakeShared<FJsonValueObject>(Item));}
      Row->SetArrayField(TEXT("spatial_bins"),Bins);
    }
    if(Moving && MotionErrors.Num()){Row->SetNumberField(TEXT("motion_median_error_m"),MotionErrors[MotionErrors.Num()/2]);Row->SetNumberField(TEXT("endpoint_median_error_m"),EndpointErrors[EndpointErrors.Num()/2]);}
    Rows.Add(MakeShared<FJsonValueObject>(Row));UE_LOG(LogCarla,Display,TEXT("PHYSICAL_STREAM_SCAN gpu=%d sequence=%llu kind=%s points=%u roi=%d mean=%.8f flags=%u"),GPU,ExpectedSequence,*Kind,Physical.point_count,ROI,ROI?Sum/ROI:0,Physical.flags);++ExpectedSequence;return true;
  };
  for(const FString& Material:{FString(TEXT("fixture-asphalt")),FString(TEXT("fixture-plaster"))})for(int32 Condition=0;Condition<3;++Condition)for(uint32 Seed=0;Seed<3;++Seed)
    if(!Capture(Condition==0?TEXT("dry"):Condition==1?TEXT("wet-only"):TEXT("rain-wet"),Material,Condition?80:0,Condition==2?70:0,Seed,.05,9.5,false))return false;
  Legacy->SetActorLocation(FVector(999,0,0));LC->ComponentTags.Add(TEXT("LidarIgnore"));
  for(bool Hidden:{false,true}) {
    LC->SetVisibility(!Hidden);LC->SetHiddenInGame(Hidden);
    for(uint32 Seed=0;Seed<3;++Seed)if(!Capture(Hidden?TEXT("ignored-hidden-collision"):TEXT("ignored-visible-collision"),TEXT("fixture-asphalt"),0,0,Seed,.05,9.5,false))return false;
  }
  // Hardware ray controls: the same explicit exclusion must leave legacy
  // collision-filtered sensor queries intact, even for a rendered collider.
  bool GpuOpticalIgnore=true,GpuLegacyPreserved=true;
  if(GPU) {
    LC->SetVisibility(true);LC->SetHiddenInGame(false);++GFrameCounter;World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
    TArray<FCarlaGpuRay> Rays;
    FCarlaGpuRay Optical{FVector::ZeroVector,FVector(1,0,0),8000,true,0};Optical.PhysicalModel=true;Optical.WavelengthNm=905;Rays.Add(Optical);
    FCarlaGpuRay Ordinary{FVector::ZeroVector,FVector(1,0,0),8000,false,0};Rays.Add(Ordinary);
    bool Done=false;CarlaGpuSensors::Submit(*Sensor,MoveTemp(Rays),[&](TArray<FCarlaGpuHit>&& Hits) {
      GpuOpticalIgnore=Hits.Num()==2 && Hits[0].Hit.GetComponent()==C;
      GpuLegacyPreserved=Hits.Num()==2 && Hits[1].Hit.GetComponent()==LC;Done=true;
    });
    View.Draw();const double Limit=FPlatformTime::Seconds()+45;
    while(!Done && FPlatformTime::Seconds()<Limit){CarlaGpuSensors::Pump();FlushRenderingCommands();FPlatformProcess::Sleep(.005f);}
    if(!Done){CarlaGpuSensors::Shutdown();AddError(TEXT("Explicit-ignore hardware controls did not complete"));return false;}
    TestTrue(TEXT("Hardware optical query excludes tagged rendered collision"),GpuOpticalIgnore);
    TestTrue(TEXT("Hardware ordinary query retains tagged collision"),GpuLegacyPreserved);
  }
  LC->ComponentTags.Remove(TEXT("LidarIgnore"));LC->SetVisibility(false);LC->SetHiddenInGame(true);Legacy->SetActorLocation(FVector(1100,0,0));
  Target->SetActorLocation(FVector(1020,0,0));if(!Capture(TEXT("moving"),TEXT("fixture-plaster"),0,0,0,.05,9.5,true))return false;
  C->SetVisibility(false);IC->SetVisibility(true);LC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  if(!Capture(TEXT("positioned-instance"),TEXT("fixture-asphalt"),0,0,0,.2,12.5,false))return false;
  // Check spatial IR coverage on the same native instance hit. Independent
  // barycentric UVs use the authored instance-local surface, not its root.
  IC->ComponentTags.Reset();IC->ComponentTags.Add(TEXT("LidarIR=fixture-instance-mask"));
  TArray<TSharedPtr<FJsonValue>> UVRows;
  FTransform InstanceTransform;IC->GetInstanceTransform(0,InstanceTransform,true);
  const auto& LOD=Cube->GetRenderData()->LODResources[0];const auto Indices=LOD.IndexBuffer.GetArrayView();
  for(double Y:{-2200.,-1200.,1200.,2200.})for(double Z:{-2200.,1200.}) {
    FCollisionQueryParams Params;Params.bTraceComplex=true;Params.bReturnFaceIndex=true;
    const auto Hit=CarlaLidarOptics::Trace(*Sensor,FVector(0,Y,Z),FVector(1,0,0),8000,0,Params,nullptr,nullptr,1,905,0);
    TestEqual(TEXT("Instance intersection preserves actual component"),Hit.Hit.GetComponent(),static_cast<UPrimitiveComponent*>(IC));
    TestEqual(TEXT("Instance intersection preserves index"),Hit.Hit.Item,0);
    if(Hit.Hit.FaceIndex<0){AddError(TEXT("Instance triangle missing"));continue;}
    const int32 K=Hit.Hit.FaceIndex*3;
    const FVector A(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[K])),B(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[K+1])),V(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[K+2]));
    const FVector Local=InstanceTransform.InverseTransformPosition(Hit.Hit.ImpactPoint);
    const FVector Bary=FMath::ComputeBaryCentric2D(Local,A,B,V);
    const FVector2f UV=LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Indices[K],0)*float(Bary.X)+LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Indices[K+1],0)*float(Bary.Y)+LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Indices[K+2],0)*float(Bary.Z);
    const auto Base=CarlaLidarOptics::Resolve(IC,IC->GetMaterial(0));
    const auto Actual=CarlaLidarInfrared::Sample(Base,Hit.Hit,905,0);
    const auto Expected=CarlaLidarInfrared::Sample(Base,Hit.Hit,905,0,&UV);
    TestTrue(TEXT("Spatial IR mask uses positioned instance UV"),FMath::Abs(Actual.diffuse-Expected.diffuse)<1.e-6);
    auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("y_cm"),Y);Row->SetNumberField(TEXT("z_cm"),Z);Row->SetNumberField(TEXT("instance"),Hit.Hit.Item);Row->SetNumberField(TEXT("diffuse"),Actual.diffuse);Row->SetNumberField(TEXT("expected_diffuse"),Expected.diffuse);Row->SetNumberField(TEXT("u"),UV.X);Row->SetNumberField(TEXT("v"),UV.Y);UVRows.Add(MakeShared<FJsonValueObject>(Row));
  }
  if(MaskStudy) {
    TArray<TSharedPtr<FJsonValue>> SpatialOracles;
    for(const FString& Profile:{FString(TEXT("fixture-instance-uniform")),FString(TEXT("fixture-instance-mask")),FString(TEXT("fixture-instance-shift")),FString(TEXT("fixture-instance-zero"))}) {
      IC->ComponentTags.Reset();IC->ComponentTags.Add(FName(*(TEXT("LidarIR=")+Profile)));
      for(int32 Bin=0;Bin<4;++Bin) {
        const double Y=(Bin&1)?125.:-125.,Z=(Bin&2)?125.:-125.;
        // Obtain geometry under the uniform profile, then sample the requested
        // profile at that same native hit. An absorbing mask pixel correctly
        // has no selected optical return, so it cannot locate its own triangle.
        IC->ComponentTags.Reset();IC->ComponentTags.Add(TEXT("LidarIR=fixture-instance-uniform"));
        FCollisionQueryParams Params;Params.bTraceComplex=true;Params.bReturnFaceIndex=true;
        const auto Hit=CarlaLidarOptics::Trace(*Sensor,FVector(0,Y,Z),FVector(1,0,0),8000,0,Params,nullptr,nullptr,1,905,0);
        IC->ComponentTags.Reset();IC->ComponentTags.Add(FName(*(TEXT("LidarIR=")+Profile)));
        TestEqual(TEXT("Spatial oracle selects actual instance"),Hit.Hit.GetComponent(),static_cast<UPrimitiveComponent*>(IC));
        TestEqual(TEXT("Spatial oracle preserves instance index"),Hit.Hit.Item,0);
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("profile"),Profile);Item->SetNumberField(TEXT("bin"),Bin);Item->SetNumberField(TEXT("y_cm"),Y);Item->SetNumberField(TEXT("z_cm"),Z);Item->SetNumberField(TEXT("diffuse"),CarlaLidarInfrared::Sample(CarlaLidarOptics::Resolve(IC,IC->GetMaterial(0)),Hit.Hit,905,0).diffuse);SpatialOracles.Add(MakeShared<FJsonValueObject>(Item));
      }
      for(uint32 Seed=0;Seed<3;++Seed)if(!Capture(TEXT("spatial-")+Profile,Profile,0,0,Seed,.2,12.5,false))return false;
    }
    auto Oracle=MakeShared<FJsonObject>();Oracle->SetArrayField(TEXT("regions"),SpatialOracles);Oracle->SetStringField(TEXT("scope"),TEXT("Native CPU optical controls, including in hardware GPU process. Full GPU spatial point-cloud response is validated separately using native stream packets."));FString JSON;FJsonSerializer::Serialize(Oracle,TJsonWriterFactory<>::Create(&JSON));TestTrue(TEXT("Spatial oracle saves"),FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("spatial-oracles.json"))));
  }
#include "LeafOpacityStreamFixture.inl"
#include "ActualLeafStreamFixture.inl"
  Client.UnSubscribe(Token);Server.CloseStream(carla::streaming::detail::token_type(Token).get_stream_id());
  auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("scans"),Rows);Report->SetArrayField(TEXT("instance_uv_checks"),UVRows);Report->SetBoolField(TEXT("gpu"),GPU);Report->SetNumberField(TEXT("stream_port"),Server.GetLocalEndpoint().port());
  auto Tie=MakeShared<FJsonObject>();Tie->SetBoolField(TEXT("near_hidden_selects_visible"),NearHidden.Hit.GetComponent()==C);Tie->SetNumberField(TEXT("near_hidden_range_cm"),NearHidden.Hit.Distance);Tie->SetBoolField(TEXT("far_hidden_keeps_nearest"),FarHidden.Hit.GetComponent()==LC);Tie->SetBoolField(TEXT("visible_blocker_keeps_nearest"),VisibleBlocker.Hit.GetComponent()==LC);Tie->SetBoolField(TEXT("explicit_ignore_selects_visible"),IgnoredHidden.Hit.GetComponent()==C);Tie->SetBoolField(TEXT("mechanical_collision_retained"),MechanicalFound && MechanicalHit.GetComponent()==LC);Tie->SetBoolField(TEXT("gpu_explicit_ignore"),GpuOpticalIgnore);Tie->SetBoolField(TEXT("gpu_legacy_collision_retained"),GpuLegacyPreserved);Tie->SetNumberField(TEXT("hidden_tie_limit_cm"),.1);Tie->SetStringField(TEXT("scope"),TEXT("CPU native optical trace controls even in GPU process; full delivered GPU scans are separate."));Report->SetObjectField(TEXT("coincident_checks"),Tie);
  Report->SetStringField(TEXT("scope"),TEXT("Actual ARayCastLidar Tick/PostPhysTick, production physical scheduler/receiver/header, native stream serialization, localhost ephemeral streaming client delivery. Owned replicated clock/weather binding; no CARLA engine RPC, map BeginPlay, current Town02 mapping, calibrated material or perception acceptance."));
  FString JSON;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&JSON));FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("report.json")));return true;
}
static FAutoConsoleCommand PhysicalSensorStreamFixtureCommand(TEXT("carla.Sensors.TestPhysicalSensorStream"),TEXT("Run owned native actor/streaming fixture: cpu|gpu|cpu-mask|gpu-mask."),
  FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){if(Args.Num()!=1 || (Args[0]!=TEXT("cpu") && Args[0]!=TEXT("gpu") && Args[0]!=TEXT("cpu-mask") && Args[0]!=TEXT("gpu-mask")))return;
    auto& F=FAutomationTestFramework::Get();if(F.GetCurrentTest())return;F.StartTestByName(TEXT("FCarlaPhysicalSensorStreamFixture ")+Args[0],0);
    if(!F.GetCurrentTest()){UE_LOG(LogCarla,Display,TEXT("PHYSICAL_STREAM_TEST FAILED did_not_start"));return;}FAutomationTestExecutionInfo Info;const bool Passed=F.StopTest(Info);
    for(const auto& E:Info.GetEntries())if(E.Event.Type==EAutomationEventType::Error)UE_LOG(LogCarla,Display,TEXT("PHYSICAL_STREAM_ERROR %s"),*E.Event.Message);
    UE_LOG(LogCarla,Display,TEXT("PHYSICAL_STREAM_TEST mode=%s %s errors=%d"),*Args[0],Passed?TEXT("PASSED"):TEXT("FAILED"),Info.GetErrorTotal());}));
