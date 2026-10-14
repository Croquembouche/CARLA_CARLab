// Owned full-resolution native alpha registry verification; no asset saves.
#include "AlphaSourceReference.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActualLeafAlphaFixture,"Carla.Lidar.ActualLeafAlphaFixture",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::EngineFilter)
bool FActualLeafAlphaFixture::RunTest(const FString& Library) {
  using namespace CarlaLidarInfrared;
  if(!TestTrue(TEXT("Full alpha fixture restricted to owned Saved files"),FPaths::IsUnderDirectory(Library,FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()))))return false;
  FString Text,Error;TSharedPtr<FJsonObject> Root;
  if(!TestTrue(TEXT("Actual source library readable"),FFileHelper::LoadFileToString(Text,*Library)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)))return false;
  const FString Original=FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json");
  struct Restore {FString P;~Restore(){FString E;CarlaLidarInfrared::Reload(P,E);}} RestoreIR{Original};
  if(!TestTrue(TEXT("Actual full-resolution resource registry reload"),Reload(Library,Error))){AddError(Error);return false;}
  const auto Materials=Root->GetArrayField(TEXT("materials"));TestEqual(TEXT("Four actual camera materials"),Materials.Num(),4);
  TestEqual(TEXT("Only two unique source masks in native atlas"),Pixels().Num(),5766820);TestTrue(TEXT("Native provenance records two immutable sources"),Status().Contains(TEXT("resources=2 resources_sha1=")));
  TArray<TSharedPtr<FJsonValue>> Rows;TMap<FString,uint32> Offsets;
  for(const auto& Item:Materials) {
    const auto Row=Item->AsObject();CarlaAlphaReference::Source Source;if(!TestTrue(TEXT("Independent source bytes hash and size"),Source.Load(Row,FPaths::GetPath(Library))))return false;
    auto* C=NewObject<UStaticMeshComponent>();C->ComponentTags.Add(FName(*(TEXT("LidarIR=")+Row->GetStringField(TEXT("name")))));const auto Material=CarlaLidarOptics::Resolve(C,nullptr);
    TArray<FVector4f> Info,UVs;Append(C,0,Material.infrared,Info,UVs);uint32 Offset,W,H;FMemory::Memcpy(&Offset,&Info[6].X,4);FMemory::Memcpy(&W,&Info[6].Y,4);FMemory::Memcpy(&H,&Info[6].Z,4);
    TestEqual(TEXT("Native dimensions retain all source pixels"),W*H,uint32(Source.Bytes.Num()));TestEqual(TEXT("Native exact width"),W,uint32(Source.Width));TestEqual(TEXT("Native exact height"),H,uint32(Source.Height));TestEqual(TEXT("Native clip equals actual material clip"),Info[6].W,Source.Clip);
    bool BytesMatch=true;for(int I=0;I<Source.Bytes.Num();++I)BytesMatch&=Pixels()[Offset+I].X==float(Source.Bytes[I])/255.f;
    TestTrue(TEXT("Every full-resolution alpha byte reaches native atlas without downsample"),BytesMatch);
    const FString Resource=Row->GetObjectField(TEXT("opacity"))->GetStringField(TEXT("data_file"));if(const uint32* Prior=Offsets.Find(Resource))TestEqual(TEXT("Shared broadleaf masks reuse immutable atlas storage"),Offset,*Prior);else Offsets.Add(Resource,Offset);
    int32 Tested=0,Open=0,Solid=0;for(int I=0;I<1024;++I) {
      const FVector2f UV(float((I*73)%1301)/1000.f-.15f,float((I*137)%1277)/1000.f-.13f);const float Alpha=Source.Alpha(UV);if(FMath::Abs(Alpha-Source.Clip)<.00001f)continue;
      const bool Expected=Alpha>=Source.Clip;TestEqual(TEXT("Native bilinear coverage matches independent raw-source oracle"),AllowsHit(Material.infrared,UV),Expected);++Tested;Solid+=Expected;Open+=!Expected;
    }
    TestTrue(TEXT("Actual source has tested solid and open geometry"),Open>0&&Solid>0);TestTrue(TEXT("Opacity leaves independent synthetic spectrum unchanged"),FMath::IsNearlyEqual(Sample(Material,FHitResult(),905,0).diffuse,.4f));
    auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("material"),Row->GetStringField(TEXT("name")));Result->SetNumberField(TEXT("width"),W);Result->SetNumberField(TEXT("height"),H);Result->SetNumberField(TEXT("atlas_offset"),Offset);Result->SetBoolField(TEXT("all_source_pixels_match"),BytesMatch);Result->SetNumberField(TEXT("sample_checks"),Tested);Result->SetNumberField(TEXT("open_samples"),Open);Result->SetNumberField(TEXT("solid_samples"),Solid);Rows.Add(MakeShared<FJsonValueObject>(Result));
  }
  auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("materials"),Rows);Report->SetStringField(TEXT("native_registry_status"),Status());Report->SetNumberField(TEXT("atlas_pixels"),Pixels().Num());Report->SetStringField(TEXT("scope"),TEXT("Actual camera source alpha full resolution, native loader/atlas/CPU sampler. Synthetic independent0.4IR coefficient, not calibrated Town profiles/currentmap/camera mip-wind acceptance."));FString JSON;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&JSON));TestTrue(TEXT("Actual-alpha native audit saved"),FFileHelper::SaveStringToFile(JSON,*(FPaths::GetPath(Library)/TEXT("native-alpha-report.json"))));return true;
}
static FAutoConsoleCommand ActualAlphaCommand(TEXT("carla.Sensors.TestActualLeafAlpha"),TEXT("Test owned full-resolution camera alpha registry path."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
  if(Args.Num()!=1)return;auto& F=FAutomationTestFramework::Get();if(F.GetCurrentTest())return;F.StartTestByName(TEXT("FActualLeafAlphaFixture ")+Args[0],0);if(!F.GetCurrentTest()){UE_LOG(LogCarla,Display,TEXT("ACTUAL_ALPHA_TEST FAILED did_not_start"));return;}FAutomationTestExecutionInfo Info;const bool Passed=F.StopTest(Info);for(const auto& E:Info.GetEntries())if(E.Event.Type==EAutomationEventType::Error)UE_LOG(LogCarla,Display,TEXT("ACTUAL_ALPHA_ERROR %s"),*E.Event.Message);UE_LOG(LogCarla,Display,TEXT("ACTUAL_ALPHA_TEST %s errors=%d"),Passed?TEXT("PASSED"):TEXT("FAILED"),Info.GetErrorTotal());
}));
