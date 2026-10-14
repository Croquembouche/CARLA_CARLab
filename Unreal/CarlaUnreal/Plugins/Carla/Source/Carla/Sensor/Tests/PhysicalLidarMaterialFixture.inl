// Native optical-to-production-receiver integration fixture. MIT license.
// Included inside PhysicalLidar.cpp to use the production preparation/receiver
// functions rather than a duplicate model. This is not a CARLA RPC sensor test.
#include "Carla/Sensor/LidarInfrared.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "SceneView.h"
#include "LegacyScreenPercentageDriver.h"
#include "SceneTypes.h"
#include "CanvasTypes.h"
#include "RendererInterface.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Math/PerspectiveMatrix.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"

namespace CarlaMaterialFixture {
struct MainView {
  UWorld* World;
  UTextureRenderTarget2D* Target;
  FSceneViewStateReference State;
  explicit MainView(UWorld* W):World(W) {
    Target=NewObject<UTextureRenderTarget2D>();Target->AddToRoot();
    Target->InitCustomFormat(1280,800,PF_B8G8R8A8,false);Target->UpdateResourceImmediate(true);
    State.Allocate(GMaxRHIFeatureLevel);
  }
  ~MainView(){State.Destroy();Target->RemoveFromRoot();FlushRenderingCommands();}
  void Draw() {
    World->SendAllEndOfFrameUpdates();
    auto* Resource=Target->GameThread_GetRenderTargetResource();
    FEngineShowFlags Flags(ESFIM_Game);
    FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Resource,World->Scene,Flags).SetRealtimeUpdate(true));
    Family.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(Family,1.0f));
    FSceneViewInitOptions Options;Options.ViewFamily=&Family;
    Options.SetViewRectangle(FIntRect(0,0,1280,800));Options.ViewOrigin=FVector::ZeroVector;
    Options.ViewRotationMatrix=FMatrix(FPlane(0,0,1,0),FPlane(1,0,0,0),FPlane(0,1,0,0),FPlane(0,0,0,1));
    Options.ProjectionMatrix=FReversedZPerspectiveMatrix(FMath::DegreesToRadians(35.0),1280.0,800.0,10.0);
    Options.SceneViewStateInterface=State.GetReference();
    auto* View=new FSceneView(Options);Family.Views.Add(View);
    View->StartFinalPostprocessSettings(Options.ViewOrigin);View->EndFinalPostprocessSettings(Options);
    FCanvas Canvas(Resource,nullptr,World,GMaxRHIFeatureLevel);
    FModuleManager::LoadModuleChecked<IRendererModule>(TEXT("Renderer")).BeginRenderingViewFamily(&Canvas,&Family);
    Canvas.Flush_GameThread();FlushRenderingCommands();
  }
};
struct Scan {
  FString Material,Condition;
  uint32 Seed=0;
  double Wetness=0,Rain=0;
  TArray<Firing> Pulses;
  TSharedRef<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe> Rays=MakeShared<TArray<FCarlaGpuRay>,ESPMode::ThreadSafe>();
  TArray<FCarlaPhysicalGpuHit> Hits;
  carla::sensor::data::PhysicalLidarData Data;
};
TSharedPtr<FJsonObject> Save(const FString& Dir,const Scan& S,const FPhysicalLidarProfile& P,const FLidarDescription& D,bool GPU,uint64 Sequence) {
  TArray<uint8> Bytes;Bytes.Append(reinterpret_cast<const uint8*>(&S.Data.header),sizeof(S.Data.header));
  if(!S.Data.points.empty())Bytes.Append(reinterpret_cast<const uint8*>(S.Data.points.data()),S.Data.points.size()*sizeof(S.Data.points[0]));
  const FString Name=FString::Printf(TEXT("%s-%s-seed%u.bin"),*S.Material,*S.Condition,S.Seed);
  FFileHelper::SaveArrayToFile(Bytes,*(Dir/Name));FSHAHash Hash;FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash.Hash);
  TSet<uint64> Valid;int32 Atmospheric=0,False=0,SurfaceCount=0,ROI=0;double Sum=0,Signal=0,Range=0;
  for(const auto& V:S.Data.points) {
    Valid.Add(V.pulse_id);Atmospheric+=(V.flags&Atmosphere)!=0;False+=(V.flags&FalseAlarm)!=0;
    if((V.flags&Surface) && !(V.flags&(Atmosphere|FalseAlarm))) {
      ++SurfaceCount;
      if(FMath::Abs(V.x-9.5f)<.1f && FMath::Abs(V.y)<2 && FMath::Abs(V.z)<2) {++ROI;Sum+=V.intensity;Signal+=V.signal;Range+=V.range;}
    }
  }
  auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("material"),S.Material);O->SetStringField(TEXT("condition"),S.Condition);
  O->SetNumberField(TEXT("seed"),S.Seed);O->SetNumberField(TEXT("wetness"),S.Wetness);O->SetNumberField(TEXT("precipitation"),S.Rain);
  O->SetStringField(TEXT("raw"),Name);O->SetStringField(TEXT("sha1"),Hash.ToString());O->SetNumberField(TEXT("sequence"),Sequence);
  O->SetNumberField(TEXT("pulse_count"),S.Pulses.Num());O->SetNumberField(TEXT("subray_count"),S.Rays->Num());O->SetNumberField(TEXT("point_count"),S.Data.points.size());
  O->SetNumberField(TEXT("valid_pulse_fraction"),double(Valid.Num())/S.Pulses.Num());O->SetNumberField(TEXT("surface_count"),SurfaceCount);
  O->SetNumberField(TEXT("atmosphere_count"),Atmospheric);O->SetNumberField(TEXT("false_alarm_count"),False);O->SetNumberField(TEXT("roi_count"),ROI);
  O->SetNumberField(TEXT("roi_mean_intensity"),ROI?Sum/ROI:0);O->SetNumberField(TEXT("roi_mean_signal"),ROI?Signal/ROI:0);O->SetNumberField(TEXT("roi_mean_range_m"),ROI?Range/ROI:0);
  O->SetStringField(TEXT("registry"),CarlaLidarInfrared::Status());O->SetStringField(TEXT("profile"),P.Name);
  O->SetNumberField(TEXT("receiver_crc"),P.Crc);O->SetNumberField(TEXT("wavelength_nm"),P.Receiver.wavelength_nm);
  O->SetBoolField(TEXT("gpu"),GPU);O->SetNumberField(TEXT("channels"),D.Channels);O->SetNumberField(TEXT("range_m"),D.Range*.01);
  O->SetNumberField(TEXT("points_per_second"),D.PointsPerSecond);O->SetNumberField(TEXT("beam_samples"),P.BeamSamples);
  UE_LOG(LogCarla,Display,TEXT("MATERIAL_RECEIVER_SCAN gpu=%d material=%s condition=%s seed=%u pulses=%d rays=%d points=%llu roi=%d intensity=%.8f atmosphere=%d"),GPU,*S.Material,*S.Condition,S.Seed,S.Pulses.Num(),S.Rays->Num(),uint64(S.Data.points.size()),ROI,ROI?Sum/ROI:0,Atmospheric);
  return O;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaMaterialReceiverFixture,"Carla.Lidar.MaterialReceiverFixture",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaMaterialReceiverFixture::RunTest(const FString& Parameters) {
  using namespace CarlaMaterialFixture;
  if(!TestTrue(TEXT("Fixture receives an explicit backend parameter"),Parameters==TEXT("cpu") || Parameters==TEXT("gpu")))return false;
  const bool GPU=Parameters==TEXT("gpu");
  if(GPU && !TestTrue(TEXT("GPU fixture requires actual inline hardware ray tracing"),GRHISupportsInlineRayTracing))return false;
  const FString Dir=FPaths::ProjectSavedDir()/TEXT("MaterialReceiverFixture")/(GPU?TEXT("gpu"):TEXT("cpu"));
  IFileManager::Get().MakeDirectory(*Dir,true);
  const FString Original=FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json");FString Error;
  if(!TestTrue(TEXT("Original IR registry available"),CarlaLidarInfrared::Reload(Original,Error)))return false;
  struct Restore {FString Path;~Restore(){FString E;CarlaLidarInfrared::Reload(Path,E);}} RestoreIR{Original};
  const FString Registry=Dir/TEXT("materials.json");
  const FString Text=TEXT(R"({"version":1,"calibration":{"status":"uncalibrated-native-fixture"},"materials":[{"name":"fixture-asphalt","wavelengths_nm":[905,1550],"dry_low":[0.1,0,0,0],"dry_high":[0.05,0,0,0],"wet_multipliers":[0.5,1,1,1]},{"name":"fixture-plaster","wavelengths_nm":[905,1550],"dry_low":[0.3,0,0,0],"dry_high":[0.2,0,0,0],"wet_multipliers":[0.8,1,1,1]}]})");
  FFileHelper::SaveStringToFile(Text,*Registry,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  if(!TestTrue(TEXT("Synthetic material registry loaded"),CarlaLidarInfrared::Reload(Registry,Error)))return false;
  const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
  auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,GMaxRHIFeatureLevel,&Init);
  if(!TestNotNull(TEXT("Owned native world"),World))return false;
  struct Destroy {UWorld* W;~Destroy(){W->DestroyWorld(false);FlushRenderingCommands();}} DestroyWorld{World};
  auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
  if(!TestNotNull(TEXT("Native cube mesh"),Cube))return false;
  auto* Target=World->SpawnActor<AStaticMeshActor>();auto* C=Target->GetStaticMeshComponent();
  C->SetMobility(EComponentMobility::Movable);C->SetCollisionProfileName(TEXT("NoCollision"));C->SetStaticMesh(Cube);
  C->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));Target->SetActorLocation(FVector(1000,0,0));Target->SetActorScale3D(FVector(1,100,100));
  auto* Owner=World->SpawnActor<AActor>();
  TUniquePtr<MainView> View;if(GPU)View=MakeUnique<MainView>(World);
  FLidarDescription D;D.Channels=32;D.Range=8000;D.PointsPerSecond=200000;D.RotationFrequency=20;
  D.HorizontalFov=360;D.UpperFovLimit=10;D.LowerFovLimit=-30;D.PhysicalModel=true;D.MaterialModel=true;D.OutputFormat=TEXT("extended");
  FPhysicalLidarProfile Profile;
  if(!TestTrue(TEXT("Production generic receiver profile loads"),FPhysicalLidarProfile::Load(TEXT("generic"),D.Channels,D.Range,Profile,Error)))return false;
  TestTrue(TEXT("Retained receiver has shot noise"),Profile.Receiver.shot_noise);
  TestEqual(TEXT("Retained beam footprint has seven samples"),Profile.BeamSamples,7);
  const auto Samples=beam_samples(Profile.BeamSamples);TArray<TSharedPtr<FJsonValue>> Rows;uint64 Sequence=0;
  for(const FString& Material:{FString(TEXT("fixture-asphalt")),FString(TEXT("fixture-plaster"))}) {
    C->ComponentTags.Reset();C->ComponentTags.Add(FName(*(TEXT("LidarIR=")+Material)));
    // Component tags are set before a new scene frame/snapshot, as in authoring.
    ++GFrameCounter;World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
    TestFalse(TEXT("Fixture remains mechanically non-colliding after mesh load"),C->IsQueryCollisionEnabled());
    auto Effective=CarlaLidarOptics::Resolve(C,C->GetMaterial(0));
    TestEqual(TEXT("Actual component maps to named IR profile"),CarlaLidarInfrared::ProfileName(Effective.infrared),Material);
    for(int Condition=0;Condition<3;++Condition)for(uint32 Seed=0;Seed<3;++Seed) {
      Scan S;S.Material=Material;S.Condition=Condition==0?TEXT("dry"):Condition==1?TEXT("wet-only"):TEXT("rain-wet");
      S.Wetness=Condition?80:0;S.Rain=Condition==2?70:0;S.Seed=Seed;D.RandomSeed=Seed;
      FWeatherParameters Weather;Weather.Wetness=S.Wetness;Weather.Precipitation=S.Rain;Weather.FogDensity=0;Weather.DustStorm=0;Weather.Cloudiness=20;Weather.SunAltitudeAngle=35;
      for(uint32 I=0;I<10000;++I){Firing F;F.Id=I;F.Channel=I%D.Channels;F.Time=double(I)/D.PointsPerSecond;S.Pulses.Add(F);}
      PrepareRays(S.Pulses,*S.Rays,Profile,D,Samples,FTransform::Identity,FTransform::Identity,Weather,0,0,.05,.05,false,true);
      TestEqual(TEXT("Full32channel200k pulse schedule retained"),S.Pulses.Num(),10000);
      TestEqual(TEXT("All seven sub-beams retained"),S.Rays->Num(),70000);
      bool Done=false;TArray<uint32> OpticalWords;int32 WrongComponents=0,WrongResponse=0,FrontChecks=0;
      auto Receive=[&](const CarlaGpuSensors::FPhysicalHitView& Hits) {
        const uint32 ExpectedComponent=GPU?C->GetPrimitiveSceneId().PrimIDValue:C->GetUniqueID();
        const float Coefficient=(Material==TEXT("fixture-asphalt")?.1f:.3f)*(1-float(S.Wetness*.01)*(Material==TEXT("fixture-asphalt")?.5f:.2f));
        for(int32 I=0;I<Hits.Num();++I) {
          const auto H=Hits[I];const auto& R=(*S.Rays)[I];
          for(uint32 E=0;E<H.EchoCount;++E) {
            const auto& V=H.Echoes[E];WrongComponents+=V.Component!=ExpectedComponent;
            uint32 RangeBits,StrengthBits;FMemory::Memcpy(&RangeBits,&V.Distance,4);FMemory::Memcpy(&StrengthBits,&V.SurfaceReturn,4);
            const uint32 Record[]={uint32(I),RangeBits,StrengthBits,V.Component,V.Flags};OpticalWords.Append(Record,5);
            const FVector Hit=R.Origin+R.Direction*V.Distance;
            if(FMath::Abs(Hit.X-950)<.03 && R.Direction.X>0) {
              ++FrontChecks;WrongResponse+=!FMath::IsNearlyEqual(V.SurfaceReturn,Coefficient*float(R.Direction.X),5.e-5f);
            }
          }
        }
        S.Data.Reset(D.Channels,0,.05,Sequence);S.Data.header.pulse_count=S.Pulses.Num();
        S.Data.header.profile_crc=Profile.Crc;S.Data.header.wavelength_nm=Profile.Receiver.wavelength_nm;
        S.Data.points=ReceivePhysicalPoints(S.Pulses,*S.Rays,Hits,Samples,Profile,D,{},Weather,0,1,false,1);
        S.Data.header.point_count=S.Data.points.size();Done=true;
      };
      ++GFrameCounter;
      if(GPU) {
        CarlaGpuSensors::SubmitPhysical(*Owner,S.Rays,Receive);
        TestTrue(TEXT("Hardware batch is pending before readback"),CarlaGpuSensors::HasPendingFrames());
        TestFalse(TEXT("IR reload is rejected during actual GPU frame"),CarlaLidarInfrared::Reload(Registry,Error));
        View->Draw();
        const double Limit=FPlatformTime::Seconds()+45;
        while(!Done && FPlatformTime::Seconds()<Limit) {CarlaGpuSensors::Pump();FlushRenderingCommands();FPlatformProcess::Sleep(.005f);}
        if(!TestTrue(TEXT("Actual GPU hardware/readback/receiver completes"),Done)) {CarlaGpuSensors::Shutdown();return false;}
        TestFalse(TEXT("Actual GPU delivery is complete"),CarlaGpuSensors::HasPendingFrames());
      } else {
        CarlaLidarOptics::Prepare(World);S.Hits.SetNum(S.Rays->Num());
        ParallelFor(S.Rays->Num(),[&](int32 I) {
          const auto& R=(*S.Rays)[I];FCollisionQueryParams Q(SCENE_QUERY_STAT(MaterialReceiverFixture),true,Owner);Q.bReturnFaceIndex=true;
          FCarlaLidarTrace Trace;CarlaLidarOptics::Trace(*Owner,R.Origin,R.Direction,R.Range,0,Q,&Trace,nullptr,R.TimeFraction,R.WavelengthNm,R.Wetness,R.SceneTime);
          auto& H=S.Hits[I];H.UnoccludedRange=Trace.UnoccludedRange;
          for(const auto& E:Trace.Echoes) {if(H.EchoCount==8)break;auto& V=H.Echoes[H.EchoCount++];V.Distance=E.Hit.Distance;V.SurfaceReturn=E.SurfaceReturn;V.Component=E.Hit.GetComponent()?E.Hit.GetComponent()->GetUniqueID():0;V.Flags=E.OpticalDepth?Surface|Multipath:Surface;}
        });Receive(CarlaGpuSensors::FPhysicalHitView(S.Hits));
      }
      TestFalse(TEXT("Tracing preserves mechanical collision state"),C->IsQueryCollisionEnabled());
      TestEqual(TEXT("Actual native echoes identify the exact fixture component"),WrongComponents,0);
      TestTrue(TEXT("Many actual front-face echoes validate IR response"),FrontChecks>1000);
      TestEqual(TEXT("Actual native echoes use the mapped diffuse/wet profile"),WrongResponse,0);
      TestTrue(TEXT("Native receiver produces point records"),!S.Data.points.empty());
      auto Row=Save(Dir,S,Profile,D,GPU,Sequence++);Rows.Add(MakeShared<FJsonValueObject>(Row));
      const FString OpticalName=FString::Printf(TEXT("%s-%s-seed%u-optical.bin"),*S.Material,*S.Condition,S.Seed);
      const auto OpticalBytes=MakeArrayView(reinterpret_cast<const uint8*>(OpticalWords.GetData()),OpticalWords.Num()*4);
      TestTrue(TEXT("Optical echo provenance file saved"),FFileHelper::SaveArrayToFile(OpticalBytes,*(Dir/OpticalName)));
      FSHAHash OpticalHash;FSHA1::HashBuffer(OpticalBytes.GetData(),OpticalBytes.Num(),OpticalHash.Hash);
      Row->SetStringField(TEXT("optical_raw"),OpticalName);Row->SetStringField(TEXT("optical_sha1"),OpticalHash.ToString());
      Row->SetNumberField(TEXT("optical_records"),OpticalWords.Num()/5);Row->SetNumberField(TEXT("front_face_response_checks"),FrontChecks);
      Row->SetNumberField(TEXT("wrong_component_count"),WrongComponents);Row->SetNumberField(TEXT("wrong_response_count"),WrongResponse);
      TestTrue(TEXT("Matched front-face ROI contains native surface returns"),Row->GetNumberField(TEXT("roi_count"))>100);
      if(Condition<2)TestEqual(TEXT("No airborne-rain returns in dry or wet-only controls"),int32(Row->GetNumberField(TEXT("atmosphere_count"))),0);
      if(Condition==2)TestTrue(TEXT("Separate rainy medium produces atmosphere returns"),Row->GetNumberField(TEXT("atmosphere_count"))>0);
    }
  }
  auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("scans"),Rows);Report->SetBoolField(TEXT("gpu"),GPU);
  auto Receiver=MakeShared<FJsonObject>();
#define FIXTURE_RECEIVER_FIELD(Name) Receiver->SetNumberField(TEXT(#Name),Profile.Receiver.Name)
  FIXTURE_RECEIVER_FIELD(wavelength_nm);FIXTURE_RECEIVER_FIELD(pulse_energy_nj);FIXTURE_RECEIVER_FIELD(aperture_diameter_m);FIXTURE_RECEIVER_FIELD(efficiency);
  FIXTURE_RECEIVER_FIELD(pulse_fwhm_ns);FIXTURE_RECEIVER_FIELD(overlap_distance_m);FIXTURE_RECEIVER_FIELD(minimum_range_m);FIXTURE_RECEIVER_FIELD(maximum_range_m);
  FIXTURE_RECEIVER_FIELD(background_photons);FIXTURE_RECEIVER_FIELD(electronic_noise_photons);FIXTURE_RECEIVER_FIELD(detection_sigma);FIXTURE_RECEIVER_FIELD(minimum_signal_photons);
  FIXTURE_RECEIVER_FIELD(saturation_photons);FIXTURE_RECEIVER_FIELD(range_noise_floor_m);FIXTURE_RECEIVER_FIELD(range_bias_m);FIXTURE_RECEIVER_FIELD(time_walk_m);
  FIXTURE_RECEIVER_FIELD(saturation_bias_m);FIXTURE_RECEIVER_FIELD(range_quantization_m);FIXTURE_RECEIVER_FIELD(echo_separation_m);FIXTURE_RECEIVER_FIELD(temperature_c);
  FIXTURE_RECEIVER_FIELD(reference_temperature_c);FIXTURE_RECEIVER_FIELD(temperature_bias_m_per_c);FIXTURE_RECEIVER_FIELD(packet_loss_probability);
  FIXTURE_RECEIVER_FIELD(latency_ms);FIXTURE_RECEIVER_FIELD(latency_jitter_ms);FIXTURE_RECEIVER_FIELD(max_returns);FIXTURE_RECEIVER_FIELD(return_order);
#undef FIXTURE_RECEIVER_FIELD
  Receiver->SetBoolField(TEXT("shot_noise"),Profile.Receiver.shot_noise);Receiver->SetBoolField(TEXT("false_alarms"),Profile.Receiver.false_alarms);
  Report->SetObjectField(TEXT("effective_receiver"),Receiver);Report->SetNumberField(TEXT("base_atmospheric_attenuation_per_m"),D.AtmospAttenRate);
  Report->SetStringField(TEXT("optical_record_layout"),TEXT("20-byte little-endian records: uint32 ray_index, float32 range_cm, float32 surface_strength, uint32 component_id, uint32 flags. GPU IDs are primitive scene IDs; CPU IDs are UObject unique IDs. Decoded native optical echoes before production receiver."));
  Report->SetStringField(TEXT("scope"),TEXT("Native mesh echoes, production ray preparation and ReceivePhysicalPoints; manual fixture pulse schedule/header. No CARLA episode clock, RPC/SDK sensor publication, Town02 mapping or real-sensor calibration acceptance."));
  Report->SetStringField(TEXT("surface_component"),C->GetPathName());Report->SetStringField(TEXT("surface_mesh"),Cube->GetPathName());
  FString JSON;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&JSON));FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("report.json")));
  return true;
}
static FAutoConsoleCommand MaterialReceiverFixtureCommand(TEXT("carla.Sensors.TestMaterialReceiver"),TEXT("Run owned native CPU or GPU material-to-receiver fixture: cpu|gpu."),
  FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
    if(Args.Num()!=1 || (Args[0]!=TEXT("cpu") && Args[0]!=TEXT("gpu")))return;
    auto& F=FAutomationTestFramework::Get();if(F.GetCurrentTest())return;
    F.StartTestByName(TEXT("FCarlaMaterialReceiverFixture ")+Args[0],0);
    if(!F.GetCurrentTest()){UE_LOG(LogCarla,Display,TEXT("MATERIAL_RECEIVER_TEST FAILED did_not_start"));return;}
    FAutomationTestExecutionInfo Info;const bool Passed=F.StopTest(Info);
    for(const auto& E:Info.GetEntries())if(E.Event.Type==EAutomationEventType::Error)UE_LOG(LogCarla,Display,TEXT("MATERIAL_RECEIVER_ERROR %s"),*E.Event.Message);
    UE_LOG(LogCarla,Display,TEXT("MATERIAL_RECEIVER_TEST mode=%s %s errors=%d"),*Args[0],Passed?TEXT("PASSED"):TEXT("FAILED"),Info.GetErrorTotal());
  }));
