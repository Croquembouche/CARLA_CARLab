// Included inside the owned native stream test. No asset saves or user ticks.
  if(MaskStudy) {
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    if(!TestNotNull(TEXT("Native leaf-plane geometry available"),Plane))return false;
    auto* Leaf=World->SpawnActor<AStaticMeshActor>();auto* FC=Leaf->GetStaticMeshComponent();
    FC->SetMobility(EComponentMobility::Movable);FC->SetCollisionProfileName(TEXT("NoCollision"));FC->SetStaticMesh(Plane);FC->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));
    Leaf->SetActorTransform(FTransform(FRotator(90,0,0),FVector(900,0,0),FVector(4,4,1)));
    FC->SetMobility(EComponentMobility::Static);
    auto* LeafInstances=World->SpawnActor<AActor>();auto* FIC=NewObject<UHierarchicalInstancedStaticMeshComponent>(LeafInstances);
    LeafInstances->SetRootComponent(FIC);LeafInstances->AddInstanceComponent(FIC);FIC->SetMobility(EComponentMobility::Static);FIC->SetCollisionProfileName(TEXT("NoCollision"));FIC->SetStaticMesh(Plane);FIC->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));FIC->RegisterComponent();
    FIC->AddInstance(FTransform(FRotator(90,0,0),FVector(900,0,0),FVector(4,4,1)));FIC->SetVisibility(false);
    C->SetVisibility(true);IC->SetVisibility(false);Target->SetActorLocation(FVector(1600,0,0));
    TArray<TSharedPtr<FJsonValue>> OpacityOracles;
    for(const FString& Geometry:{FString(TEXT("static")),FString(TEXT("instance")),FString(TEXT("moving"))}) {
      FC->SetVisibility(Geometry!=TEXT("instance"));FIC->SetVisibility(Geometry==TEXT("instance"));
      FC->SetMobility(Geometry==TEXT("moving")?EComponentMobility::Movable:EComponentMobility::Static);
      UPrimitiveComponent* Foreground=Geometry==TEXT("instance")?static_cast<UPrimitiveComponent*>(FIC):static_cast<UPrimitiveComponent*>(FC);
      for(const FString& Profile:{FString(TEXT("fixture-opacity-checker")),FString(TEXT("fixture-opacity-shift")),FString(TEXT("fixture-opacity-clear")),FString(TEXT("fixture-opacity-clamp"))}) {
        Leaf->SetActorLocation(FVector(900,0,0));
        // Prime the observed transform at the current owned clock. Motion
        // interpolation in the following full scan must include this endpoint.
        Foreground->ComponentTags.Reset();Foreground->ComponentTags.Add(TEXT("LidarIR=fixture-instance-uniform"));
        ++GFrameCounter;World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
        if(Geometry==TEXT("moving"))CarlaLidarMotion::Prepare(World,{Foreground});
        for(int32 Bin=0;Bin<4;++Bin) {
          const FVector Origin(0,(Bin&1)?100.:-100.,(Bin&2)?100.:-100.);
          FCollisionQueryParams Params;Params.bTraceComplex=true;Params.bReturnFaceIndex=true;Params.AddIgnoredActor(Sensor);
          const auto Solid=CarlaLidarOptics::Trace(*Sensor,Origin,FVector(1,0,0),8000,0,Params,nullptr,nullptr,1,905,0);
          TestEqual(TEXT("Cutout oracle obtains actual foreground triangle"),Solid.Hit.GetComponent(),Foreground);
          Foreground->ComponentTags.Reset();Foreground->ComponentTags.Add(FName(*(TEXT("LidarIR=")+Profile)));++GFrameCounter;
          const auto Selected=CarlaLidarOptics::Trace(*Sensor,Origin,FVector(1,0,0),8000,0,Params,nullptr,nullptr,1,905,0);
          const bool Passed=Selected.Hit.GetComponent()==C;
          TestTrue(TEXT("Opacity selects foreground or unobstructed backdrop"),Passed||Selected.Hit.GetComponent()==Foreground);
          TestTrue(TEXT("Opacity preserves exact path length through an empty hole"),FMath::Abs(Selected.Hit.Distance-(Passed?1550.f:900.f))<.01f);
          auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("geometry"),Geometry);Row->SetStringField(TEXT("profile"),Profile);Row->SetNumberField(TEXT("bin"),Bin);Row->SetBoolField(TEXT("passes_to_backdrop"),Passed);Row->SetNumberField(TEXT("selected_range_cm"),Selected.Hit.Distance);OpacityOracles.Add(MakeShared<FJsonValueObject>(Row));
          Foreground->ComponentTags.Reset();Foreground->ComponentTags.Add(TEXT("LidarIR=fixture-instance-uniform"));++GFrameCounter;
        }
        Foreground->ComponentTags.Reset();Foreground->ComponentTags.Add(FName(*(TEXT("LidarIR=")+Profile)));
        if(GPU) {
          ++GFrameCounter;World->SendAllEndOfFrameUpdates();FlushRenderingCommands();
          TArray<FCarlaGpuRay> Rays;
          for(int32 Bin=0;Bin<4;++Bin){FCarlaGpuRay Ray{FVector(0,(Bin&1)?100.:-100.,(Bin&2)?100.:-100.),FVector(1,0,0),8000,true,0};Ray.PhysicalModel=true;Ray.WavelengthNm=905;Rays.Add(Ray);}
          bool Done=false;bool Match=true;const int32 OracleStart=OpacityOracles.Num()-4;
          CarlaGpuSensors::Submit(*Sensor,MoveTemp(Rays),[&](TArray<FCarlaGpuHit>&& Hits){Match=Hits.Num()==4;for(int32 Bin=0;Bin<Hits.Num()&&Bin<4;++Bin){const bool Pass=OpacityOracles[OracleStart+Bin]->AsObject()->GetBoolField(TEXT("passes_to_backdrop"));Match&=Hits[Bin].Hit.GetComponent()==(Pass?static_cast<UPrimitiveComponent*>(C):Foreground)&&FMath::Abs(Hits[Bin].Hit.Distance-(Pass?1550.f:900.f))<.02f;}Done=true;});
          View.Draw();const double Limit=FPlatformTime::Seconds()+45;
          while(!Done&&FPlatformTime::Seconds()<Limit){CarlaGpuSensors::Pump();FlushRenderingCommands();FPlatformProcess::Sleep(.005f);}
          if(!Done){CarlaGpuSensors::Shutdown();AddError(TEXT("Cutout hardware oracle did not finish"));return false;}
          TestTrue(TEXT("Hardware cutout rays agree with independent native CPU foreground/backdrop controls"),Match);
        }
        // Three seeds at full retained receiver rate. Every cutout scene has a
        // real background; absorbing opaque masks in earlier controls cannot
        // reveal this surface and remain a distinct regression.
        for(uint32 Seed=0;Seed<3;++Seed) {
          if(Geometry==TEXT("moving"))Leaf->SetActorLocation(FVector(902+2*Seed,0,0));
          const bool Clear=Profile==TEXT("fixture-opacity-clear");
          if(!Capture(TEXT("opacity-")+Geometry+TEXT("-")+Profile,TEXT("fixture-asphalt"),0,0,Seed,.2,Clear?15.5:9.,false))return false;
          Rows.Last()->AsObject()->SetStringField(TEXT("opacity_profile"),Profile);Rows.Last()->AsObject()->SetStringField(TEXT("opacity_geometry"),Geometry);
          Rows.Last()->AsObject()->SetNumberField(TEXT("foreground_end_m"),Geometry==TEXT("moving")?9.02+.02*Seed:9.);
          Rows.Last()->AsObject()->SetNumberField(TEXT("foreground_start_m"),Geometry==TEXT("moving")?9.+.02*Seed:9.);
        }
      }
    }
    for(const FString& Geometry:{FString(TEXT("static")),FString(TEXT("instance")),FString(TEXT("moving"))}) {
      int32 GapChecker=0,GapShift=0,GapClear=0,GapClamp=0;bool Changed=false;
      for(int32 Bin=0;Bin<4;++Bin) {
        const auto Find=[&](const TCHAR* P){for(const auto& Row:OpacityOracles){const auto Item=Row->AsObject();if(Item->GetStringField(TEXT("geometry"))==Geometry&&Item->GetStringField(TEXT("profile"))==P&&Item->GetIntegerField(TEXT("bin"))==Bin)return Item->GetBoolField(TEXT("passes_to_backdrop"));}return false;};
        const bool A=Find(TEXT("fixture-opacity-checker")),B=Find(TEXT("fixture-opacity-shift")),Z=Find(TEXT("fixture-opacity-clear"));GapChecker+=A;GapShift+=B;GapClear+=Z;GapClamp+=Find(TEXT("fixture-opacity-clamp"));Changed|=A!=B;
      }
      TestEqual(TEXT("One geometric hole in four checker quadrants"),GapChecker,1);TestEqual(TEXT("UV shift retains one geometric hole"),GapShift,1);TestEqual(TEXT("Fully clear plane exposes all four backdrop quadrants"),GapClear,4);TestTrue(TEXT("Opacity UV shift moves the geometric gap"),Changed);TestEqual(TEXT("Clamped outside UV creates two correct background quadrants"),GapClamp,2);
    }
    auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("regions"),OpacityOracles);Report->SetBoolField(TEXT("hardware_controls_executed"),GPU);Report->SetStringField(TEXT("scope"),TEXT("Native CPU triangle controls, plus48 hardware rays only in GPU process;36full native stream scans per backend, static/HISM/moving mask and UVshift/fullyclear controls. Synthetic geometric coverage separate from infrared coefficients and no actual Town02 leaf mask/camera acceptance."));
    FString JSON;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&JSON));TestTrue(TEXT("Cutout oracle saves"),FFileHelper::SaveStringToFile(JSON,*(Dir/TEXT("opacity-oracles.json"))));
  }
