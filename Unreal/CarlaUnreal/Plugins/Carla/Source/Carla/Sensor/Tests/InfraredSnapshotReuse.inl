// Copyright (c) 2026. Licensed under the MIT license.
#include "Engine/StaticMesh.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaInfraredSnapshotReuseTest,"Carla.Sensors.InfraredSnapshotReuse",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaInfraredSnapshotReuseTest::RunTest(const FString&) {
  using namespace CarlaGpuSensorPrivate;
  using namespace CarlaLidarInfrared;
  FString Error;const FString Original=FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json");
  const FString Dir=FPaths::ProjectSavedDir()/TEXT("InfraredSnapshotReuse478");IFileManager::Get().MakeDirectory(*Dir,true);
  const FString Path=Dir/TEXT("registry.json");
  struct FRestore {FString Original;~FRestore(){FString Error;CarlaLidarInfrared::Reload(Original,Error);}} Restore{Original};
  const FString Text=TEXT(R"({"version":1,"materials":[{"name":"reuse-a","dry_low":[0.1,0,0,0],"dry_high":[0.05,0,0,0],"mask":{"width":1,"height":1,"rgba":[1,1,1,1]}},{"name":"reuse-b","dry_low":[0.4,0,0,0],"dry_high":[0.2,0,0,0],"mask":{"width":1,"height":1,"rgba":[1,1,1,1]}}]})");
  if(!TestTrue(TEXT("Owned snapshot test registry loads"),FFileHelper::SaveStringToFile(Text,*Path)&&Reload(Path,Error)))return false;
  auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
  auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
  if(!TestNotNull(TEXT("Cube loaded"),Cube)||!TestNotNull(TEXT("Sphere loaded"),Sphere))return false;
  auto* C=NewObject<UStaticMeshComponent>();C->SetStaticMesh(Cube);C->ComponentTags.Add(TEXT("LidarIR=reuse-a"));
  const int32 Profile=Find(C,nullptr);TArray<FInfraredRequest> Requests;Requests.Add({C,0,Profile});Requests.Add({C,0,Profile});
  const auto A=BuildInfrared(Requests);const auto B=BuildInfrared(Requests);
  TestTrue(TEXT("Unchanged sections reuse the same immutable CPU/GPU snapshot"),A==B && !A->UVs.IsEmpty());
  C->SetRelativeScale3D(FVector(-2,3,1));C->SetRelativeLocation(FVector(123,456,789));
  TestTrue(TEXT("Component motion and mirrored transforms retain identical UV topology"),A==BuildInfrared(Requests));
  const auto Legacy=BuildInfrared(Requests,false);
  TestEqual(TEXT("Legacy expansion stores exactly twice the UV bytes"),Legacy->UVs.Num(),A->UVs.Num()*2);
  auto UInt=[](float F){uint32 V;FMemory::Memcpy(&V,&F,4);return V;};
  for(int32 Section=0;Section<2;++Section)for(int32 Record=0;Record<8;++Record){
    auto X=Legacy->Info[Section*8+Record],Y=A->Info[Section*8+Record];
    if(Record==5){const uint32 N=UInt(X.Z)*3;TestEqual(TEXT("All effective shader UV bytes match the legacy path"),FMemory::Memcmp(Legacy->UVs.GetData()+UInt(X.Y),A->UVs.GetData()+UInt(Y.Y),N*sizeof(FVector4f)),0);X.Y=Y.Y;}
    TestEqual(TEXT("All remaining shader metadata bytes match"),FMemory::Memcmp(&X,&Y,sizeof(X)),0);
  }
  C->SetStaticMesh(Sphere);const auto NewMesh=BuildInfrared(Requests);
  TestTrue(TEXT("Replacing mesh geometry invalidates the packed snapshot"),NewMesh!=A);
  const FGuid SavedGuid=Sphere->GetLightingGuid();Sphere->SetLightingGuid();const auto Rebuilt=BuildInfrared(Requests);Sphere->SetLightingGuid(SavedGuid);
  TestTrue(TEXT("A mesh build GUID change invalidates reused geometry"),Rebuilt!=NewMesh);
  Requests[1].Profile=1;const auto NewMaterial=BuildInfrared(Requests);
  TestTrue(TEXT("Per-section profile changes rebuild metadata"),NewMaterial!=Rebuilt && FMath::IsNearlyEqual(NewMaterial->Info[8].X,.4f));
  const float Before=A->Info[0].X;
  TestTrue(TEXT("Reloaded coefficients commit a new registry revision"),FFileHelper::SaveStringToFile(Text.Replace(TEXT("0.1"),TEXT("0.3")),*Path)&&Reload(Path,Error));
  const auto Reloaded=BuildInfrared(Requests);
  TestTrue(TEXT("Registry reload invalidates pixels and coefficient metadata"),Reloaded!=NewMaterial && FMath::IsNearlyEqual(Reloaded->Info[0].X,.3f));
  TestTrue(TEXT("Older in-flight snapshots remain immutable"),A->Info[0].X==Before && FMath::IsNearlyEqual(Before,.1f));
  CachedInfrared.Reset();CachedInfraredSignature.Empty();PackedInfraredRevision=0;
  return true;
}
static FAutoConsoleCommand TestInfraredSnapshotReuse(TEXT("carla.Sensors.TestInfraredSnapshotReuse"),
    TEXT("Validate exact IR packing reuse, geometry/profile invalidation and immutable frame leases."),FConsoleCommandDelegate::CreateLambda([]{
  auto& Framework=FAutomationTestFramework::Get();if(Framework.GetCurrentTest())return;
  Framework.StartTestByName(TEXT("FCarlaInfraredSnapshotReuseTest"),0,TEXT("Carla.Sensors.InfraredSnapshotReuse"));
  FAutomationTestExecutionInfo Info;const bool Passed=Framework.StopTest(Info);
  for(const auto& E:Info.GetEntries())UE_LOG(LogCarla,Display,TEXT("IR_SNAPSHOT_TEST_EVENT type=%d message=%s"),int32(E.Event.Type),*E.Event.Message);
  UE_LOG(LogCarla,Display,TEXT("IR_SNAPSHOT_TEST result=%s errors=%d"),Passed?TEXT("Success"):TEXT("Fail"),Info.GetErrorTotal());
}));
