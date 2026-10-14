// Development-only actual saved Town02 actor/receiver/stream capture. MIT.
// Included after the shared streaming fixture; no CARLA server or world tick.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaTown02SensorStream,"Carla.Lidar.Town02SensorStream",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaTown02SensorStream::RunTest(const FString& RequestPath) {
  FString Text;TSharedPtr<FJsonObject> Request;
  if(!TestTrue(TEXT("Town sensor request loads"),FFileHelper::LoadFileToString(Text,*RequestPath)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)))return false;
  auto* World=FindObject<UWorld>(nullptr,*Request->GetStringField(TEXT("world_path")));
  if(!TestNotNull(TEXT("Actual saved copied Town02 world loaded"),World))return false;
  if(!TestTrue(TEXT("No actual map gameplay BeginPlay"),!World->HasBegunPlay()))return false;
  const bool GPU=Request->GetBoolField(TEXT("gpu"));
  if(GPU&&!TestTrue(TEXT("Hardware backend requires rendered inline tracing"),GRHISupportsInlineRayTracing))return false;
  auto* Enabled=IConsoleManager::Get().FindConsoleVariable(TEXT("carla.Sensors.GpuRayTracing"));const int32 OldEnabled=Enabled->GetInt();Enabled->Set(GPU?1:0,ECVF_SetByCode);
  struct RestoreMode{IConsoleVariable* C;int32 V;~RestoreMode(){C->Set(V,ECVF_SetByCode);}} Mode{Enabled,OldEnabled};
  TestEqual(TEXT("Requested native backend selected"),CarlaGpuSensors::IsEnabled(),GPU);
  FString Error;const FString Registry=FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json");
  if(!TestTrue(TEXT("Actual material source registry loads"),CarlaLidarInfrared::Reload(Registry,Error)))return false;
  const FString Dir=Request->GetStringField(TEXT("output_dir"));IFileManager::Get().MakeDirectory(*Dir,true);
  TMap<UPrimitiveComponent*,TArray<FName>> Tags;
  struct RestoreTags{TMap<UPrimitiveComponent*,TArray<FName>>& All;~RestoreTags(){for(auto& I:All)I.Key->ComponentTags=I.Value;}} Restore{Tags};
  for(const auto& V:Request->GetArrayField(TEXT("optical_ignore_components"))) {
    auto* C=FindObject<UPrimitiveComponent>(nullptr,*V->AsString());if(!TestTrue(TEXT("Exact saved mechanical component exists"),C&&C->GetWorld()==World))return false;
    Tags.Add(C,C->ComponentTags);C->ComponentTags.AddUnique(TEXT("LidarIgnore"));
  }
  for(const auto& V:Request->GetArrayField(TEXT("optical_render_components"))) {
    auto* C=FindObject<UPrimitiveComponent>(nullptr,*V->AsString());
    if(!TestTrue(TEXT("Exact visible optical component exists"),C&&C->GetWorld()==World&&C->IsVisible()&&!C->bHiddenInGame&&!C->GetOwner()->IsHidden()))return false;
    Tags.Add(C,C->ComponentTags);C->ComponentTags.AddUnique(TEXT("LidarOptical"));
  }
  int32 ActorCount=0;for(TActorIterator<AActor> A(World);A;++A)++ActorCount;TestTrue(TEXT("Full Town02 map actors present"),ActorCount>100);
  auto* Episode=NewObject<UCarlaEpisode>(World);Episode->bIsPrimaryServer=false;Episode->AddToRoot();
  struct Unroot{UObject* O;~Unroot(){O->RemoveFromRoot();}} RootEpisode{Episode};
  auto* WeatherClass=LoadClass<AWeather>(nullptr,TEXT("/Game/Carla/Blueprints/Weather/BP_CarlaWeather.BP_CarlaWeather_C"));
  if(!TestNotNull(TEXT("Actual native weather class loads"),WeatherClass))return false;
  auto* Weather=World->SpawnActor<AWeather>(WeatherClass);auto* Sensor=World->SpawnActor<ARayCastLidar>();
  struct DestroyActors{UWorld* W;AActor* S;AActor* A;~DestroyActors(){CarlaGpuSensors::Pump();W->DestroyActor(S);W->DestroyActor(A);}} Destroy{World,Sensor,Weather};
  auto* WeatherProperty=FindFProperty<FObjectPropertyBase>(UCarlaEpisode::StaticClass(),TEXT("Weather"));
  if(!TestNotNull(TEXT("Native replicated episode weather binding"),WeatherProperty))return false;
  WeatherProperty->SetObjectPropertyValue_InContainer(Episode,Weather);Sensor->SetEpisode(*Episode);
  FLidarDescription D;D.Channels=32;D.Range=8000;D.PointsPerSecond=200000;D.RotationFrequency=20;D.UpperFovLimit=10;D.LowerFovLimit=-30;D.PhysicalModel=true;D.MaterialModel=true;D.OutputFormat=TEXT("extended");Sensor->Set(D);
  if(!TestTrue(TEXT("Actual physical LiDAR actor configures"),Sensor->IsConfigured()))return false;
  const uint64 OldFrame=FCarlaEngine::GetFrameCounter();const double OldTime=World->TimeSeconds,OldDelta=World->DeltaTimeSeconds;
  struct RestoreClock{UWorld* W;uint64 F;double T,D;~RestoreClock(){FCarlaEngine::ResetFrameCounter(F);W->TimeSeconds=T;W->DeltaTimeSeconds=D;}} Clock{World,OldFrame,OldTime,OldDelta};FCarlaEngine::ResetFrameCounter(20000);
  carla::streaming::Server Server("127.0.0.1",0);Server.AsyncRun(2);auto Stream=Server.MakeStream();Sensor->SetDataStream(FDataStream(Stream));
  std::mutex Mutex;std::vector<carla::Buffer> Received;carla::streaming::Client Client;const auto Token=Stream.token();
  Client.Subscribe(Token,[&](carla::Buffer B){std::lock_guard<std::mutex> Lock(Mutex);Received.emplace_back(std::move(B));});Client.AsyncRun(2);
  // Declared after Client so unsubscribe completes before callback state dies.
  struct CloseStream{carla::streaming::Client& C;carla::streaming::Server& S;decltype(Token) T;~CloseStream(){C.UnSubscribe(T);S.CloseStream(carla::streaming::detail::token_type(T).get_stream_id());}} Close{Client,Server,Token};
  const double ConnectLimit=FPlatformTime::Seconds()+10;while(!Sensor->AreClientsListening()&&FPlatformTime::Seconds()<ConnectLimit)FPlatformProcess::Sleep(.005f);
  if(!TestTrue(TEXT("Actual streaming SDK client subscribes"),Sensor->AreClientsListening()))return false;
  TUniquePtr<CarlaMaterialFixture::MainView> View;if(GPU)View=MakeUnique<CarlaMaterialFixture::MainView>(World);
  uint64 Sequence=0;double Time=0;TArray<TSharedPtr<FJsonValue>> Rows;
  const int32 Expected=Request->GetArrayField(TEXT("views")).Num()*Request->GetArrayField(TEXT("conditions")).Num()*Request->GetArrayField(TEXT("seeds")).Num();
  auto SaveReport=[&](bool Complete) {
    auto J=MakeShared<FJsonObject>();J->SetArrayField(TEXT("scans"),Rows);J->SetBoolField(TEXT("complete"),Complete);J->SetBoolField(TEXT("gpu"),GPU);J->SetNumberField(TEXT("expected_scans"),Expected);J->SetNumberField(TEXT("actual_map_actor_count"),ActorCount);J->SetNumberField(TEXT("temporary_component_tags"),Tags.Num());J->SetNumberField(TEXT("channels"),D.Channels);J->SetNumberField(TEXT("range_m"),D.Range*.01);J->SetNumberField(TEXT("points_per_second"),D.PointsPerSecond);J->SetNumberField(TEXT("rotation_frequency_hz"),D.RotationFrequency);J->SetNumberField(TEXT("upper_fov_deg"),D.UpperFovLimit);J->SetNumberField(TEXT("lower_fov_deg"),D.LowerFovLimit);J->SetStringField(TEXT("registry"),CarlaLidarInfrared::Status());J->SetStringField(TEXT("scope"),TEXT("Actual copied saved Town02 geometry, native actor scheduler/receiver/serializer and localhost streaming SDK. No map BeginPlay, CARLA RPC/GameInstance/user tick or assets saved. Synthetic leaf spectrum, uncalibrated receiver; CPU/GPU rendering and camera acceptance remain explicitly separate."));
    FString JSON;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&JSON));return FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("report.json")));
  };
  if(!TestTrue(TEXT("Initial complete-scope progress report saves"),SaveReport(false)))return false;
  const auto Vec=[](const TArray<TSharedPtr<FJsonValue>>& A){return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());};
  for(const auto& VV:Request->GetArrayField(TEXT("views"))) {
    const auto Pose=VV->AsObject();const FVector Location=Vec(Pose->GetArrayField(TEXT("location")))*100.;const auto R=Pose->GetArrayField(TEXT("rotation"));const FRotator Rotation(R[0]->AsNumber(),R[1]->AsNumber(),R[2]->AsNumber());Sensor->SetActorLocationAndRotation(Location,Rotation);
    for(const auto& CC:Request->GetArrayField(TEXT("conditions")))for(const auto& SS:Request->GetArrayField(TEXT("seeds"))) {
      const auto Condition=CC->AsObject();D.RandomSeed=SS->AsNumber();Sensor->Set(D);FWeatherParameters W;W.Cloudiness=20;W.SunAltitudeAngle=35;W.SunAzimuthAngle=215;W.Wetness=Condition->GetNumberField(TEXT("wetness"));W.Precipitation=Condition->GetNumberField(TEXT("precipitation"));W.PrecipitationDeposits=Condition->GetNumberField(TEXT("deposits"));W.FogDensity=0;W.DustStorm=0;W.WindIntensity=0;Weather->SetWeather(W);
      const double Start=Time;Time+=.05;World->TimeSeconds=Time;World->DeltaTimeSeconds=.05;Episode->SetReplicatedElapsedGameTime(Time);++GFrameCounter;FCarlaEngine::UpdateFrameCounter();World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
      const uint64 Frame=FCarlaEngine::GetFrameCounter();UE_LOG(LogCarla,Display,TEXT("TOWN02_STREAM_BEGIN sequence=%llu view=%s condition=%s seed=%d gpu=%d"),Sequence,*Pose->GetStringField(TEXT("name")),*Condition->GetStringField(TEXT("name")),D.RandomSeed,GPU);
      Sensor->Tick(.05f);Sensor->PostPhysTickInternal(World,LEVELTICK_All,.05f);if(GPU)View->Draw();
      const double Limit=FPlatformTime::Seconds()+120;while(FPlatformTime::Seconds()<Limit) {CarlaGpuSensors::Pump();FlushRenderingCommands();bool Done;{std::lock_guard<std::mutex> Lock(Mutex);Done=Received.size()>Sequence;}if(Done)break;FPlatformProcess::Sleep(.005f);}
      carla::Buffer Packet;{std::lock_guard<std::mutex> Lock(Mutex);if(Received.size()<=Sequence){AddError(TEXT("Actual Town02 sensor stream timed out"));CarlaGpuSensors::Shutdown();return false;}Packet.copy_from(Received[Sequence]);}
      using SH=carla::sensor::s11n::SensorHeaderSerializer;using PH=carla::sensor::data::PhysicalLidarHeader;using Point=carla::sensor::data::PhysicalLidarDetection;
      if(!TestTrue(TEXT("Native serialized payload has full headers"),Packet.size()>=SH::header_offset+sizeof(PH)))return false;
      SH::Header Header;PH H;FMemory::Memcpy(&Header,Packet.data(),sizeof(Header));FMemory::Memcpy(&H,Packet.data()+SH::header_offset,sizeof(H));
      TestEqual(TEXT("Actual Town capture frame"),Header.frame,Frame);TestTrue(TEXT("Capture clock and scan boundaries"),FMath::IsNearlyEqual(Header.timestamp,Time,1.e-8)&&FMath::Abs(H.scan_start-Start)<1.e-6&&FMath::Abs(H.scan_end-Time)<1.e-8);TestEqual(TEXT("Actual Town channels"),H.channels,D.Channels);TestEqual(TEXT("Actual Town sequence"),H.sequence,Sequence);TestTrue(TEXT("Retained full pulse rate"),FMath::Abs(double(H.pulse_count)-10000)<=1.00001);TestEqual(TEXT("Full point stride"),uint64(Packet.size()),uint64(SH::header_offset+sizeof(PH)+H.point_count*sizeof(Point)));
      TestTrue(TEXT("Actual saved-map capture location survives serialization"),FMath::Abs(Header.sensor_transform.location.x-Location.X*.01)<.0001&&FMath::Abs(Header.sensor_transform.location.y-Location.Y*.01)<.0001&&FMath::Abs(Header.sensor_transform.location.z-Location.Z*.01)<.0001);
      if(HasAnyErrors())return false;
      const auto* Points=reinterpret_cast<const Point*>(Packet.data()+SH::header_offset+sizeof(PH));int32 SurfaceCount=0,AtmosphericCount=0;TSet<uint64> Valid;
      for(uint32 I=0;I<H.point_count;++I){const auto& P=Points[I];Valid.Add(P.pulse_id);AtmosphericCount+=(P.flags&Atmosphere)!=0;SurfaceCount+=(P.flags&Surface)!=0;TestEqual(TEXT("Native firing channel survives actual map"),uint32(P.pulse_id%D.Channels),uint32(P.channel));}
      TestTrue(TEXT("Actual town has native surface returns"),SurfaceCount>100);if(W.Precipitation==0)TestEqual(TEXT("Dry and wet-only have no atmospheric returns"),AtmosphericCount,0);else TestTrue(TEXT("Rain has independent atmospheric returns"),AtmosphericCount>0);
      const FString Name=FString::Printf(TEXT("%02llu-%s-%s-seed%d.bin"),Sequence,*Pose->GetStringField(TEXT("name")),*Condition->GetStringField(TEXT("name")),D.RandomSeed);if(!TestTrue(TEXT("Full actual Town native packet saves"),FFileHelper::SaveArrayToFile(MakeArrayView(Packet.data(),Packet.size()),*(Dir/Name))))return false;
      FSHAHash Hash;FSHA1::HashBuffer(Packet.data(),Packet.size(),Hash.Hash);auto Row=MakeShared<FJsonObject>();Row->SetObjectField(TEXT("view"),Pose);Row->SetObjectField(TEXT("weather"),Condition);Row->SetBoolField(TEXT("gpu"),GPU);Row->SetNumberField(TEXT("seed"),D.RandomSeed);Row->SetStringField(TEXT("raw"),Name);Row->SetStringField(TEXT("sha1"),Hash.ToString());Row->SetNumberField(TEXT("frame"),Header.frame);Row->SetNumberField(TEXT("timestamp"),Header.timestamp);Row->SetNumberField(TEXT("sequence"),H.sequence);Row->SetNumberField(TEXT("scan_start"),H.scan_start);Row->SetNumberField(TEXT("scan_end"),H.scan_end);Row->SetNumberField(TEXT("pulse_count"),H.pulse_count);Row->SetNumberField(TEXT("point_count"),H.point_count);Row->SetNumberField(TEXT("surface_count"),SurfaceCount);Row->SetNumberField(TEXT("atmosphere_count"),AtmosphericCount);Row->SetNumberField(TEXT("valid_pulse_fraction"),double(Valid.Num())/H.pulse_count);Row->SetNumberField(TEXT("physical_flags"),H.flags);Row->SetNumberField(TEXT("wavelength_nm"),905);Rows.Add(MakeShared<FJsonValueObject>(Row));
      TestTrue(TEXT("Per-scan authoritative progress report saves"),SaveReport(false));UE_LOG(LogCarla,Display,TEXT("TOWN02_STREAM_SCAN sequence=%llu view=%s condition=%s points=%u surface=%d atmosphere=%d flags=%u"),Sequence,*Pose->GetStringField(TEXT("name")),*Condition->GetStringField(TEXT("name")),H.point_count,SurfaceCount,AtmosphericCount,H.flags);++Sequence;if(HasAnyErrors())return false;
    }
  }
  TestEqual(TEXT("Complete requested full-map capture batch"),Rows.Num(),Expected);TestTrue(TEXT("Completed report saves"),SaveReport(true));return true;
}
static FAutoConsoleCommand Town02SensorStreamCommand(TEXT("carla.Sensors.CaptureTown02SensorStream"),TEXT("Actual copied Town02 full-fidelity sensor stream: request JSON path."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){
  if(Args.Num()!=1)return;auto& F=FAutomationTestFramework::Get();if(F.GetCurrentTest())return;F.StartTestByName(TEXT("FCarlaTown02SensorStream ")+Args[0],0);if(!F.GetCurrentTest())return;FAutomationTestExecutionInfo Info;const bool Passed=F.StopTest(Info);for(const auto& E:Info.GetEntries())if(E.Event.Type==EAutomationEventType::Error)UE_LOG(LogCarla,Display,TEXT("TOWN02_STREAM_ERROR %s"),*E.Event.Message);UE_LOG(LogCarla,Display,TEXT("TOWN02_STREAM_TEST %s errors=%d"),Passed?TEXT("PASSED"):TEXT("FAILED"),Info.GetErrorTotal());}));
