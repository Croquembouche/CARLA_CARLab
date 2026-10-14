// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Sensor/LidarInfrared.h"
#include "Carla/Sensor/LidarOptics.h"
#include "Carla/Sensor/GpuSensorDispatcher.h"
#include "Carla.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLidarInfraredRegistryTest,
    "Carla.Lidar.InfraredRegistryReload",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLidarInfraredRegistryTest::RunTest(const FString& Parameters) {
  using namespace CarlaLidarInfrared;
  const FString Original=FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json");
  const FString Dir=FPaths::ProjectSavedDir()/TEXT("LidarInfraredAutomation");
  IFileManager::Get().MakeDirectory(*Dir,true);
  const FString Path=Dir/TEXT("registry.json");FString Error;
  if(!TestTrue(TEXT("Existing registry loads"),Reload(Original,Error)))return false;
  // Restore even when an assertion fails; all fixture files belong to the
  // isolated automation project, never to its shared source Content directory.
  struct FRestore { FString Path;~FRestore(){FString Error;CarlaLidarInfrared::Reload(Path,Error);} } Restore{Original};
  const FString RowA=TEXT(R"({"name":"audit-asphalt","wavelengths_nm":[905,1550],"dry_low":[0.1,0,0,0],"dry_high":[0.05,0,0,0],"wet_multipliers":[0.5,1,1,1]})");
  const FString RowB=TEXT(R"({"name":"audit-plaster","wavelengths_nm":[905,1550],"dry_low":[0.3,0,0,0],"dry_high":[0.2,0,0,0],"wet_multipliers":[0.8,1,1,1]})");
  auto Load=[&](const FString& Text) { return FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&Reload(Path,Error); };
  auto Root=[&](const FString& Rows) { return TEXT("{\"version\":1,\"calibration\":{\"status\":\"uncalibrated-test-fixture\"},\"materials\":[")+Rows+TEXT("]}"); };
  const uint64 Before=Revision();
  if(!TestTrue(TEXT("Valid replacement loads"),Load(Root(RowA+TEXT(",")+RowB))))return false;
  TestEqual(TEXT("A committed reload increments revision"),Revision(),Before+1);
  TArray<uint8> Bytes;FFileHelper::LoadFileToArray(Bytes,*Path);FSHAHash Hash;FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash.Hash);
  TestTrue(TEXT("Status identifies exact bytes"),Status().Contains(Hash.ToString()));
  TestTrue(TEXT("Status retains authored calibration provenance"),Status().Contains(TEXT("uncalibrated-test-fixture")));
  auto* C=NewObject<UStaticMeshComponent>();C->ComponentTags.Add(TEXT("LidarIR=audit-asphalt"));
  auto Base=CarlaLidarOptics::Resolve(C,nullptr);
  TestTrue(TEXT("An explicit diffuse-only IR surface participates without mechanical collision"),CarlaLidarOptics::NeedsRenderSurface(C,{Base}));
  C->ComponentTags.Add(TEXT("LidarIgnore"));
  TestFalse(TEXT("An ignored optical-only surface stays excluded"),CarlaLidarOptics::NeedsRenderSurface(C,{Base}));
  C->ComponentTags.Remove(TEXT("LidarIgnore"));
  TestEqual(TEXT("Component tag resolves an exact profile"),ProfileName(Base.infrared),FString(TEXT("audit-asphalt")));
  FHitResult Hit;
  const auto Dry=Sample(Base,Hit,905,0),Wet=Sample(Base,Hit,905,.8f),OtherBand=Sample(Base,Hit,1550,0);
  TestTrue(TEXT("905nm dry explicit reflectance"),FMath::IsNearlyEqual(Dry.diffuse,.1f));
  TestTrue(TEXT("Wetness alone changes surface optics"),FMath::IsNearlyEqual(Wet.diffuse,.06f));
  TestTrue(TEXT("The second spectral endpoint is distinct"),FMath::IsNearlyEqual(OtherBand.diffuse,.05f));
  TArray<FVector4f> OldInfo,OldUV;Append(C,0,Base.infrared,OldInfo,OldUV);
  TestEqual(TEXT("GPU section metadata contains eight records"),OldInfo.Num(),8);
  TestTrue(TEXT("GPU wet multiplier uses the same authored input"),FMath::IsNearlyEqual(OldInfo[2].X,.5f));
  const FString AcceptedStatus=Status();const uint64 AcceptedRevision=Revision();
  const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
  auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,GMaxRHIFeatureLevel,&Init);
  if(!TestNotNull(TEXT("Owned native test world created"),World))return false;
  struct FDestroyWorld { UWorld* World;~FDestroyWorld(){World->DestroyWorld(false);} } Destroy{World};
  auto* PendingOwner=World->SpawnActor<AActor>();
  CarlaGpuSensors::ScheduleDelivery(*PendingOwner,3600,[]{});
  TestTrue(TEXT("Native delivery is explicitly pending"),CarlaGpuSensors::HasPendingFrames());
  TestFalse(TEXT("Reload is rejected while a native frame is pending"),Load(Root(RowB)));
  TestEqual(TEXT("Pending-frame rejection preserves the accepted registry"),Status(),AcceptedStatus);
  PendingOwner->Destroy();PendingOwner->MarkAsGarbage();CarlaGpuSensors::Pump();
  TestFalse(TEXT("Owned delayed fixture is cleaned up"),CarlaGpuSensors::HasPendingFrames());
  auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
  if(!TestNotNull(TEXT("Native cube render geometry loaded"),Cube))return false;
  auto* Target=World->SpawnActor<AStaticMeshActor>();auto* Surface=Target->GetStaticMeshComponent();
  Surface->SetMobility(EComponentMobility::Movable);
  // Override AStaticMeshActor's inherited mesh collision profile before load
  // finishes; SetCollisionEnabled alone is overwritten by mesh compilation.
  Surface->SetCollisionProfileName(TEXT("NoCollision"));Surface->SetStaticMesh(Cube);
  Surface->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));
  Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);Surface->ComponentTags.Add(TEXT("LidarIR=audit-asphalt"));
  Target->SetActorLocation(FVector(1000,0,0));
  auto* Sensor=World->SpawnActor<AActor>();
  const FVector Origin(0,0,0),Direction(1,0,0);FCollisionQueryParams Params(SCENE_QUERY_STAT(LidarIRAutomation),true,Sensor);Params.bReturnFaceIndex=true;
  FHitResult PhysicsHit;
  TestFalse(TEXT("The optical-only fixture has no native physics hit"),World->ParallelLineTraceSingleByChannel(PhysicsHit,Origin,Origin+Direction*2000,ECC_GameTraceChannel2,Params));
  const auto NativeDry=CarlaLidarOptics::Trace(*Sensor,Origin,Direction,2000,0,Params,nullptr,nullptr,1,905,0);
  const auto NativeWet=CarlaLidarOptics::Trace(*Sensor,Origin,Direction,2000,0,Params,nullptr,nullptr,1,905,.8f);
  TestFalse(TEXT("Native optical tracing preserves the explicit no-collision profile"),Surface->IsQueryCollisionEnabled());
  TestTrue(TEXT("A diffuse-only no-collision surface produces a native optical hit"),NativeDry.Hit.bBlockingHit);
  TestTrue(TEXT("Native optical hit resolves the actual revised surface component"),NativeDry.Hit.GetComponent()==Surface);
  TestTrue(TEXT("Native optical hit carries a triangle face index"),NativeDry.Hit.FaceIndex>=0);
  TestTrue(TEXT("Render geometry resolves the measured front face"),FMath::IsNearlyEqual(NativeDry.Hit.Distance,950.f,.1f));
  TestTrue(TEXT("Wetness changes native optical return without any airborne-rain input"),NativeDry.SurfaceReturn>0&&FMath::IsNearlyEqual(NativeWet.SurfaceReturn/NativeDry.SurfaceReturn,.6f,.001f));
  UE_LOG(LogCarla,Display,TEXT("LIDAR_IR_NATIVE_HIT profile=%s component=%s face=%d range_cm=%.6f dry=%.8f wet=%.8f"),*ProfileName(CarlaLidarOptics::Resolve(Surface,Surface->GetMaterial(0)).infrared),*Surface->GetPathName(),NativeDry.Hit.FaceIndex,NativeDry.Hit.Distance,NativeDry.SurfaceReturn,NativeWet.SurfaceReturn);
  for(const FString& Bad:{FString(TEXT("{")),FString(TEXT("{\"materials\":[]}")),Root(RowA+TEXT(",")+RowA),Root(TEXT("4")),Root(TEXT("{\"name\":\"invalid\",\"dry_low\":[2,0,0,0],\"dry_high\":[0,0,0,0]}")),Root(TEXT("{\"name\":\"bad-optional\",\"dry_low\":[0.1,0,0,0],\"dry_high\":[0.1,0,0,0],\"wet_multipliers\":\"wrong-type\"}")),Root(TEXT("{\"name\":\"bad-mask\",\"dry_low\":[0.1,0,0,0],\"dry_high\":[0.1,0,0,0],\"mask\":4}"))}) {
    TestFalse(TEXT("Malformed candidate is rejected atomically"),Load(Bad));
    TestEqual(TEXT("Failure preserves revision"),Revision(),AcceptedRevision);
    TestEqual(TEXT("Failure preserves source/hash/provenance"),Status(),AcceptedStatus);
    TestTrue(TEXT("Failure preserves native optical response"),FMath::IsNearlyEqual(Sample(Base,Hit,905,.8f).diffuse,.06f));
  }
  TestFalse(TEXT("Missing file is rejected"),Reload(Dir/TEXT("does-not-exist.json"),Error));
  TestEqual(TEXT("Missing file retains accepted state"),Status(),AcceptedStatus);
  TestTrue(TEXT("Changed profile order reloads"),Load(Root(RowB+TEXT(",")+RowA)));
  auto Updated=CarlaLidarOptics::Resolve(C,nullptr);
  TestTrue(TEXT("New resolve refreshes changed profile indices"),Updated.infrared!=Base.infrared);
  TestEqual(TEXT("Refreshed index still resolves the same surface name"),ProfileName(Updated.infrared),FString(TEXT("audit-asphalt")));
  TestTrue(TEXT("Fresh native resolution retains its wet response"),FMath::IsNearlyEqual(Sample(Updated,Hit,905,.8f).diffuse,.06f));
  TestTrue(TEXT("Previously packed GPU metadata is immutable after reload"),FMath::IsNearlyEqual(OldInfo[0].X,.1f)&&FMath::IsNearlyEqual(OldInfo[2].X,.5f));
  C->ComponentTags[0]=TEXT("LidarIR=audit-plaster");auto Plaster=CarlaLidarOptics::Resolve(C,nullptr);
  TestTrue(TEXT("Different explicit material has a different dry response"),FMath::IsNearlyEqual(Sample(Plaster,Hit,905,0).diffuse,.3f));
  TestTrue(TEXT("Wet response is material-specific"),FMath::IsNearlyEqual(Sample(Plaster,Hit,905,.8f).diffuse,.252f,1.e-6f));
  Surface->ComponentTags[0]=TEXT("LidarIR=audit-plaster");
  const auto NativePlaster=CarlaLidarOptics::Trace(*Sensor,Origin,Direction,2000,0,Params,nullptr,nullptr,1,905,0);
  TestTrue(TEXT("Different material changes a matched native hit"),NativePlaster.Hit.GetComponent()==Surface&&FMath::IsNearlyEqual(NativePlaster.SurfaceReturn/NativeDry.SurfaceReturn,3.f,.001f));
  TestTrue(TEXT("An intentionally empty registry is valid"),Load(Root(TEXT(""))));
  const auto Removed=CarlaLidarOptics::Trace(*Sensor,Origin,Direction,2000,0,Params,nullptr,nullptr,1,905,0);
  UE_LOG(LogCarla,Display,TEXT("LIDAR_IR_REMOVED_HIT blocking=%d component=%s infrared=%d optical_eligible=%d query_collision=%d range_cm=%.6f strength=%.8f"),Removed.Hit.bBlockingHit,*GetPathNameSafe(Removed.Hit.GetComponent()),CarlaLidarOptics::Resolve(Surface,Surface->GetMaterial(0)).infrared,CarlaLidarOptics::NeedsRenderSurface(Surface,CarlaLidarOptics::Sections(Surface)),Surface->IsQueryCollisionEnabled(),Removed.Hit.Distance,Removed.SurfaceReturn);
  TestFalse(TEXT("Same-frame reload invalidates optical eligibility cache"),Removed.Hit.bBlockingHit);
  TestTrue(TEXT("Reinstating the registry is valid"),Load(Root(RowB+TEXT(",")+RowA)));
  const auto Reinstated=CarlaLidarOptics::Trace(*Sensor,Origin,Direction,2000,0,Params,nullptr,nullptr,1,905,.8f);
  TestTrue(TEXT("Same-frame profile restoration rebuilds optical geometry selection"),Reinstated.Hit.GetComponent()==Surface&&FMath::IsNearlyEqual(Reinstated.SurfaceReturn/NativeDry.SurfaceReturn,2.52f,.001f));
  FString Values;for(int32 I=0;I<512;++I){if(I)Values+=TEXT(",");Values+=I<256?TEXT("0"):TEXT("1");}
  const FString OpacityRow=TEXT("{\"name\":\"audit-opacity\",\"dry_low\":[0.4,0,0,0],\"dry_high\":[0.4,0,0,0],\"opacity\":{\"width\":512,\"height\":1,\"clip_value\":0.5,\"address_x\":\"clamp\",\"address_y\":\"clamp\",\"values\":[")+Values+TEXT("]}}");
  TestTrue(TEXT("Full512pixel opacity dimension is accepted without256downsample"),Load(Root(OpacityRow)));
  C->ComponentTags.Reset();C->ComponentTags.Add(TEXT("LidarIR=audit-opacity"));const auto Opacity=CarlaLidarOptics::Resolve(C,nullptr);
  TestTrue(TEXT("Opacity profile exposes independent geometric coverage"),HasOpacity(Opacity.infrared));
  TestFalse(TEXT("Clamped source alpha gap remains open outside UV0"),AllowsHit(Opacity.infrared,FVector2f(-.25f,.5f)));
  TestTrue(TEXT("Clamped source alpha remains opaque beyond UV1"),AllowsHit(Opacity.infrared,FVector2f(1.25f,.5f)));
  TestTrue(TEXT("Bilinear exact clip boundary survives"),AllowsHit(Opacity.infrared,FVector2f(.5f,.5f)));
  TestFalse(TEXT("Bilinear below clip boundary is rejected"),AllowsHit(Opacity.infrared,FVector2f(.499f,.5f)));
  TestTrue(TEXT("Opacity does not replace authored near-IR reflectance"),FMath::IsNearlyEqual(Sample(Opacity,Hit,905,0).diffuse,.4f));
  // Compare effective shader records and every triangle UV against the legacy
  // append path, including different component transforms and profile metadata.
  auto* Instance=NewObject<UStaticMeshComponent>();Instance->SetStaticMesh(Cube);
  C->SetStaticMesh(Cube);
  Instance->SetRelativeScale3D(FVector(-2,3,1));
  SnapshotUVCache Shared;TArray<FVector4f> SharedInfo,SharedUV,LegacyInfo,LegacyUV;
  Append(C,0,Opacity.infrared,LegacyInfo,LegacyUV);
  Append(Instance,0,Opacity.infrared,LegacyInfo,LegacyUV);
  Append(C,0,Opacity.infrared,SharedInfo,SharedUV,&Shared);
  Append(Instance,0,Opacity.infrared,SharedInfo,SharedUV,&Shared);
  TestTrue(TEXT("Repeated mesh section stores one exact UV segment"),SharedUV.Num()>0 && LegacyUV.Num()==2*SharedUV.Num() && Shared.Num()==1);
  auto UInt=[](float F){uint32 I;FMemory::Memcpy(&I,&F,4);return I;};
  for(int Section=0;Section<2;++Section) {
    for(int Record=0;Record<8;++Record) {
      auto A=LegacyInfo[Section*8+Record],B=SharedInfo[Section*8+Record];
      if(Record==5) {const uint32 LA=UInt(A.Y),LB=UInt(B.Y),N=UInt(A.Z)*3;
        TestEqual(TEXT("Every effective triangle UV remains byte identical"),FMemory::Memcmp(LegacyUV.GetData()+LA,SharedUV.GetData()+LB,N*sizeof(FVector4f)),0);A.Y=B.Y;}
      TestEqual(TEXT("All other shader metadata remains byte identical"),FMemory::Memcmp(&A,&B,sizeof(A)),0);
    }
  }
  TArray<FVector4f> EmptyInfo,EmptyUV;SnapshotUVCache NextSnapshot;
  Append(C,0,Opacity.infrared,EmptyInfo,EmptyUV,&NextSnapshot);
  TestEqual(TEXT("A new snapshot never reuses stale UV offsets"),UInt(EmptyInfo[5].Y),0u);
  TestEqual(TEXT("A new snapshot preserves all current UV bytes"),FMemory::Memcmp(EmptyUV.GetData(),SharedUV.GetData(),SharedUV.Num()*sizeof(FVector4f)),0);
  const uint64 OpacityRevision=Revision();const FString OpacityStatus=Status();
  for(const FString& Bad:{OpacityRow.Replace(TEXT("512"),TEXT("4097")),OpacityRow.Replace(TEXT("\"clamp\""),TEXT("\"mirror\"")),OpacityRow.Replace(TEXT("\"clip_value\":0.5"),TEXT("\"clip_value\":1.5")),OpacityRow.Replace(TEXT("\"values\":["),TEXT("\"values\":[2,"))}) {
    TestFalse(TEXT("Malformed opacity candidate is rejected atomically"),Load(Root(Bad)));TestEqual(TEXT("Opacity rejection retains exact accepted status"),Status(),OpacityStatus);TestEqual(TEXT("Opacity rejection retains revision"),Revision(),OpacityRevision);
  }
  TestTrue(TEXT("Wrapped opacity reload accepted"),Load(Root(OpacityRow.Replace(TEXT("\"clamp\""),TEXT("\"wrap\"")))));
  const auto Wrapped=CarlaLidarOptics::Resolve(C,nullptr);
  TestTrue(TEXT("Wrapped source alpha repeats outside UV0"),AllowsHit(Wrapped.infrared,FVector2f(-.25f,.5f)));
  TestFalse(TEXT("Wrapped source alpha repeats beyond UV1"),AllowsHit(Wrapped.infrared,FVector2f(1.25f,.5f)));
  TArray<uint8> BinaryAlpha;BinaryAlpha.SetNum(512);for(int I=0;I<512;++I)BinaryAlpha[I]=I<256?0:255;
  const FString AlphaPath=Dir/TEXT("coverage.alpha8");TestTrue(TEXT("Owned alpha8 saved"),FFileHelper::SaveArrayToFile(BinaryAlpha,*AlphaPath));FSHAHash AlphaHash;FSHA1::HashBuffer(BinaryAlpha.GetData(),BinaryAlpha.Num(),AlphaHash.Hash);
  const FString InlineValues=TEXT("\"values\":[")+Values+TEXT("]");
  const FString BinaryFields=TEXT("\"data_file\":\"coverage.alpha8\",\"sha1\":\"")+AlphaHash.ToString().ToLower()+TEXT("\"");
  const FString BinaryRow=OpacityRow.Replace(*InlineValues,*BinaryFields);
  TestTrue(TEXT("Hash-validated binary alpha8 reload"),Load(Root(BinaryRow)));
  const auto BinaryProfile=CarlaLidarOptics::Resolve(C,nullptr);TestTrue(TEXT("Binary alpha8 reproduces opaque sample"),AllowsHit(BinaryProfile.infrared,FVector2f(.75f,.5f)));TestFalse(TEXT("Binary alpha8 reproduces clear sample"),AllowsHit(BinaryProfile.infrared,FVector2f(.25f,.5f)));
  const uint64 BinaryRevision=Revision();const FString BinaryStatus=Status();TestTrue(TEXT("Binary source provenance recorded separately from JSON hash"),BinaryStatus.Contains(TEXT("resources=1 resources_sha1=")));
  // File mutation cannot change a committed snapshot. A new load must detect
  // the changed bytes before modifying profiles, atlas or provenance.
  BinaryAlpha[0]=255;FFileHelper::SaveArrayToFile(BinaryAlpha,*AlphaPath);
  TestFalse(TEXT("Committed binary alpha is immutable after file mutation"),AllowsHit(BinaryProfile.infrared,FVector2f(.5f/512,.5f)));
  TestFalse(TEXT("Changed alpha bytes reject stale authored hash"),Load(Root(BinaryRow)));TestEqual(TEXT("Resource rejection retains snapshot revision"),Revision(),BinaryRevision);TestEqual(TEXT("Resource rejection retains full provenance"),Status(),BinaryStatus);
  BinaryAlpha[0]=0;FFileHelper::SaveArrayToFile(BinaryAlpha,*AlphaPath);
  for(const FString& Bad:{BinaryRow.Replace(TEXT("coverage.alpha8"),TEXT("../coverage.alpha8")),BinaryRow.Replace(TEXT("coverage.alpha8"),TEXT("/tmp/coverage.alpha8")),BinaryRow.Replace(TEXT("coverage.alpha8"),TEXT("missing.alpha8")),BinaryRow.Replace(TEXT("\"width\":512"),TEXT("\"width\":511")),BinaryRow.Replace(*AlphaHash.ToString().ToLower(),TEXT("not-a-hash")),BinaryRow.Replace(TEXT("\"data_file\":"),TEXT("\"values\":[0],\"data_file\":"))}) {
    TestFalse(TEXT("Invalid binary resource candidate rejects atomically"),Load(Root(Bad)));TestEqual(TEXT("Invalid resource preserves accepted status"),Status(),BinaryStatus);TestEqual(TEXT("Invalid resource preserves accepted revision"),Revision(),BinaryRevision);
  }
  TArray<uint8> Truncated=BinaryAlpha;Truncated.SetNum(511);FFileHelper::SaveArrayToFile(Truncated,*AlphaPath);TestFalse(TEXT("Truncated alpha8 rejects before atlas commit"),Load(Root(BinaryRow)));TestEqual(TEXT("Truncated rejection retains snapshot"),Status(),BinaryStatus);FFileHelper::SaveArrayToFile(BinaryAlpha,*AlphaPath);
  return true;
}

// A direct console runner avoids the editor AutomationController FPS gate
// during headless NullRHI validation, without starting a simulation tick owner.
static FAutoConsoleCommand TestCommand(TEXT("carla.Sensors.TestInfraredRegistry"),TEXT("Run native atomic infrared reload and dry/wet mapping tests."),FConsoleCommandDelegate::CreateLambda([] {
  auto& Framework=FAutomationTestFramework::Get();
  if(Framework.GetCurrentTest()){UE_LOG(LogCarla,Warning,TEXT("LIDAR_IR_TEST busy"));return;}
  Framework.StartTestByName(TEXT("FLidarInfraredRegistryTest"),0,TEXT("Carla.Lidar.InfraredRegistryReload"));
  if(!Framework.GetCurrentTest()){UE_LOG(LogCarla,Warning,TEXT("LIDAR_IR_TEST FAILED test_did_not_start"));return;}
  FAutomationTestExecutionInfo Info;const bool Passed=Framework.StopTest(Info);
  for(const auto& Entry:Info.GetEntries())
    UE_LOG(LogCarla,Display,TEXT("LIDAR_IR_TEST_EVENT type=%d message=%s"),int32(Entry.Event.Type),*Entry.Event.Message);
  UE_LOG(LogCarla,Display,TEXT("LIDAR_IR_TEST %s errors=%d"),Passed?TEXT("PASSED"):TEXT("FAILED"),Info.GetErrorTotal());
}));
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "MaterialWeatherGraphAudit.inl"
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "ActualLeafAlphaFixture.inl"
#endif
