// Editor-only full-scope probe; excluded from non-editor development builds.
#if WITH_EDITOR
// Development-only native saved-map optical audit; never saves assets.
#include "AlphaSourceReference.h"
#include "LandscapeComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
// Development-only observations. Never changes material state or hit geometry.
static TSharedPtr<FJsonObject> Town02ProbeMaterialIdentity(UMaterialInterface* Material) {
  auto J=MakeShared<FJsonObject>();
  J->SetStringField(TEXT("material"),Material?Material->GetPathName():TEXT("null"));
  J->SetStringField(TEXT("class"),Material?Material->GetClass()->GetPathName():TEXT("null"));
  J->SetStringField(TEXT("base_material"),Material&&Material->GetMaterial()?Material->GetMaterial()->GetPathName():TEXT("null"));
  TArray<TSharedPtr<FJsonValue>> Parents,Scalars;TSet<UMaterialInterface*> Seen;
  UMaterialInterface* Current=Material;
  for(int32 Depth=0;Current && Depth<32;++Depth) {
    if(Seen.Contains(Current))break;Seen.Add(Current);Parents.Add(MakeShared<FJsonValueString>(Current->GetPathName()));
    auto* Instance=Cast<UMaterialInstance>(Current);Current=Instance?Instance->Parent.Get():nullptr;
  }
  J->SetBoolField(TEXT("parent_chain_complete"),Current==nullptr);J->SetArrayField(TEXT("parent_chain"),Parents);
  if(Material) {
    TArray<FMaterialParameterInfo> Infos;TArray<FGuid> Ids;Material->GetAllScalarParameterInfo(Infos,Ids);
    for(const auto& Info:Infos) {
      auto Parameter=MakeShared<FJsonObject>();float Value=0;
      const bool Resolved=Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Info),Value);
      Parameter->SetStringField(TEXT("name"),Info.Name.ToString());Parameter->SetNumberField(TEXT("association"),int32(Info.Association));Parameter->SetNumberField(TEXT("index"),Info.Index);Parameter->SetBoolField(TEXT("resolved"),Resolved);
      if(Resolved)Parameter->SetNumberField(TEXT("value"),Value);Scalars.Add(MakeShared<FJsonValueObject>(Parameter));
    }
  }
  J->SetArrayField(TEXT("scalar_parameters"),Scalars);return J;
}
static void Town02ProbeHitGeometry(const TSharedPtr<FJsonObject>& J,const FHitResult& H) {
  J->SetBoolField(TEXT("hit"),H.bBlockingHit);J->SetStringField(TEXT("component"),H.GetComponent()?H.GetComponent()->GetPathName():TEXT("null"));
  J->SetStringField(TEXT("actor"),H.GetActor()?H.GetActor()->GetPathName():TEXT("null"));
  J->SetNumberField(TEXT("range_m"),H.Distance*.01);J->SetNumberField(TEXT("face"),H.FaceIndex);J->SetNumberField(TEXT("instance"),H.Item);
  const auto Vector=[](const FVector& V){TArray<TSharedPtr<FJsonValue>> A;A.Add(MakeShared<FJsonValueNumber>(V.X));A.Add(MakeShared<FJsonValueNumber>(V.Y));A.Add(MakeShared<FJsonValueNumber>(V.Z));return A;};
  J->SetArrayField(TEXT("impact_point_cm"),Vector(H.ImpactPoint));J->SetArrayField(TEXT("impact_normal"),Vector(H.ImpactNormal));
}

// Fixture-only UV reconstruction from the reported native render triangle.
// Collision faces cannot be interpreted as render triangle indices.
static bool Town02ProbeUV(const FHitResult& H,uint32 Geometry,FVector2f& UV) {
  if(Geometry!=1)return false;
  auto* C=Cast<UStaticMeshComponent>(H.GetComponent());
  if(!C||!C->GetStaticMesh()||!C->GetStaticMesh()->GetRenderData()||C->GetStaticMesh()->GetRenderData()->LODResources.IsEmpty()||H.FaceIndex<0)return false;
  const auto& L=C->GetStaticMesh()->GetRenderData()->LODResources[0];const auto I=L.IndexBuffer.GetArrayView();const uint32 K=uint32(H.FaceIndex)*3;
  if(K+2>=uint32(I.Num())||!L.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords())return false;
  FTransform T=C->GetComponentTransform();if(auto* Instances=Cast<UInstancedStaticMeshComponent>(C))if(!Instances->GetInstanceTransform(H.Item,T,true))return false;
  const FVector A(L.VertexBuffers.PositionVertexBuffer.VertexPosition(I[K])),B(L.VertexBuffers.PositionVertexBuffer.VertexPosition(I[K+1])),D(L.VertexBuffers.PositionVertexBuffer.VertexPosition(I[K+2]));
  const FVector P=T.InverseTransformPosition(H.ImpactPoint),E=B-A,F=D-A,Q=P-A;
  const double EE=E|E,EF=E|F,FF=F|F,Det=EE*FF-EF*EF;if(FMath::Abs(Det)<1.e-12)return false;
  const double V=((Q|E)*FF-(Q|F)*EF)/Det,W=((Q|F)*EE-(Q|E)*EF)/Det;
  if(V<-.001||W<-.001||V+W>1.001)return false;
  UV=L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K],0)*float(1-V-W)+L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K+1],0)*float(V)+L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I[K+2],0)*float(W);return true;
}

// Read-only native geometry snapshot. Names/counts alone do not establish
// identical reconstructed spline instances or collision/render transforms.
static TArray<TSharedPtr<FJsonValue>> Town02ProbeTransform(const FTransform& T) {
  const FVector P=T.GetTranslation(),S=T.GetScale3D();const FQuat Q=T.GetRotation();
  const double Values[]={P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z};
  TArray<TSharedPtr<FJsonValue>> A;for(double V:Values)A.Add(MakeShared<FJsonValueNumber>(V));return A;
}
static TSharedPtr<FJsonObject> Town02ProbeComponentGeometry(UPrimitiveComponent* C) {
  auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("component"),C?C->GetPathName():TEXT("null"));J->SetBoolField(TEXT("found"),C!=nullptr);if(!C)return J;
  J->SetBoolField(TEXT("physics_state_created"),C->IsPhysicsStateCreated());J->SetBoolField(TEXT("physics_state_valid"),C->HasValidPhysicsState());
  const FBodyInstance* Body=C->GetBodyInstance();J->SetBoolField(TEXT("physics_body_present"),Body!=nullptr);J->SetBoolField(TEXT("physics_body_valid"),Body&&Body->IsValidBodyInstance());
  if(auto* Hierarchy=Cast<UHierarchicalInstancedStaticMeshComponent>(C)){J->SetBoolField(TEXT("hism_tree_fully_built"),Hierarchy->IsTreeFullyBuilt());J->SetBoolField(TEXT("hism_async_building"),Hierarchy->IsAsyncBuilding());}
  J->SetStringField(TEXT("class"),C->GetClass()->GetPathName());J->SetArrayField(TEXT("world_transform"),Town02ProbeTransform(C->GetComponentTransform()));
  if(C->GetOwner())J->SetArrayField(TEXT("actor_transform"),Town02ProbeTransform(C->GetOwner()->GetActorTransform()));
  J->SetBoolField(TEXT("registered"),C->IsRegistered());J->SetBoolField(TEXT("visible"),C->IsVisible());J->SetBoolField(TEXT("hidden_in_game"),C->bHiddenInGame);J->SetBoolField(TEXT("actor_hidden"),C->GetOwner()&&C->GetOwner()->IsHidden());J->SetBoolField(TEXT("actor_collision"),C->GetOwner()&&C->GetOwner()->GetActorEnableCollision());J->SetNumberField(TEXT("collision_enabled"),int32(C->GetCollisionEnabled()));J->SetNumberField(TEXT("channel2_response"),int32(C->GetCollisionResponseToChannel(ECC_GameTraceChannel2)));
  const FBox Bounds=C->Bounds.GetBox();TArray<TSharedPtr<FJsonValue>> B;for(double V:{Bounds.Min.X,Bounds.Min.Y,Bounds.Min.Z,Bounds.Max.X,Bounds.Max.Y,Bounds.Max.Z})B.Add(MakeShared<FJsonValueNumber>(V));J->SetArrayField(TEXT("world_bounds_min_max"),B);
  if(auto* Static=Cast<UStaticMeshComponent>(C)) {
    UStaticMesh* Mesh=Static->GetStaticMesh();J->SetBoolField(TEXT("mesh_compiling"),Mesh&&Mesh->IsCompiling());J->SetStringField(TEXT("mesh"),Mesh?Mesh->GetPathName():TEXT("null"));TArray<TSharedPtr<FJsonValue>> Slots;
    for(auto* M:Static->GetMaterials())Slots.Add(MakeShared<FJsonValueString>(M?M->GetPathName():TEXT("null")));J->SetArrayField(TEXT("material_slots"),Slots);
    if(Mesh&&Mesh->GetRenderData()&&!Mesh->GetRenderData()->LODResources.IsEmpty()) {
      const auto& L=Mesh->GetRenderData()->LODResources[0];J->SetNumberField(TEXT("lod0_vertices"),L.VertexBuffers.PositionVertexBuffer.GetNumVertices());J->SetNumberField(TEXT("lod0_indices"),L.IndexBuffer.GetNumIndices());J->SetNumberField(TEXT("lod0_sections"),L.Sections.Num());
    }
  }
  if(auto* Instances=Cast<UInstancedStaticMeshComponent>(C)) {
    TArray<TSharedPtr<FJsonValue>> Transforms;const int32 N=Instances->GetInstanceCount();J->SetNumberField(TEXT("instance_count"),N);
    for(int32 I=0;I<N;++I) {auto Row=MakeShared<FJsonObject>();FTransform T;const bool Valid=Instances->GetInstanceTransform(I,T,true);Row->SetNumberField(TEXT("index"),I);Row->SetBoolField(TEXT("valid"),Valid);const FBodyInstance* InstanceBody=Instances->GetBodyInstance(NAME_None,true,I);Row->SetBoolField(TEXT("physics_body_present"),InstanceBody!=nullptr);Row->SetBoolField(TEXT("physics_body_valid"),InstanceBody&&InstanceBody->IsValidBodyInstance());if(Valid)Row->SetArrayField(TEXT("world_transform"),Town02ProbeTransform(T));Transforms.Add(MakeShared<FJsonValueObject>(Row));}
    J->SetArrayField(TEXT("instances"),Transforms);
  }
  return J;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaTown02MaterialProbe,"Carla.Lidar.Town02MaterialProbe",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaTown02MaterialProbe::RunTest(const FString& RequestPath) {
  FString Text;if(!TestTrue(TEXT("Native probe request loads"),FFileHelper::LoadFileToString(Text,*RequestPath)))return false;
  TSharedPtr<FJsonObject> Request;if(!TestTrue(TEXT("Native probe request parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)))return false;
  auto* World=FindObject<UWorld>(nullptr,*Request->GetStringField(TEXT("world_path")));
  if(!TestNotNull(TEXT("Actual copied Town02 world is loaded"),World))return false;
  TestTrue(TEXT("Map inspection has no gameplay BeginPlay"),!World->HasBegunPlay());
  if(!TestTrue(TEXT("Legacy audit limited to actual editor world"),IsInGameThread() && World->WorldType==EWorldType::Editor))return false;
  bool LegacyLookup=false;Request->TryGetBoolField(TEXT("legacy_collision_lookup_for_test"),LegacyLookup);
  int32 VerifiedLookupCalls=0,VerifiedNullAuditCalls=0;
  auto* Sensor=World->SpawnActor<AActor>();Sensor->SetActorEnableCollision(false);
  struct RestoreActor{UWorld* W;AActor* A;~RestoreActor(){W->DestroyActor(A);}} Destroy{World,Sensor};
  TMap<UPrimitiveComponent*,TArray<FName>> Tags;
  struct RestoreTags{TMap<UPrimitiveComponent*,TArray<FName>>& All;~RestoreTags(){for(auto& I:All)I.Key->ComponentTags=I.Value;}} RestoreComponentTags{Tags};
  const FString Original=FPaths::ProjectContentDir()/TEXT("Carla/Config/Lidar/materials.json");FString Error;
  struct RestoreIR{FString P;~RestoreIR(){FString E;CarlaLidarInfrared::Reload(P,E);}} Restore{Original};
  TArray<TSharedPtr<FJsonValue>> Rows,Selected;
  int32 Actors=0,Components=0;
  for(TActorIterator<AActor> A(World);A;++A) {
    ++Actors;TInlineComponentArray<UStaticMeshComponent*> All;A->GetComponents(All);
    for(auto* C:All) {
      ++Components;UStaticMesh* Mesh=C->GetStaticMesh();if(!Mesh)continue;
      const FString Name=(A->GetName()+TEXT(" ")+Mesh->GetPathName()).ToLower();
      if(Name.Contains(TEXT("roof")) || Name.Contains(TEXT("coping")) || Name.Contains(TEXT("parapet")) || Name.Contains(TEXT("_top")) || Name.Contains(TEXT("grass")) || Name.Contains(TEXT("turf")) || Name.Contains(TEXT("lawn")) || Name.Contains(TEXT("groundcover")))continue;
      if(!C->IsRegistered() || !C->IsVisible() || C->bHiddenInGame || A->IsHidden())continue;
      bool Owned=false;for(auto* M:C->GetMaterials())if(M && M->GetPathName().StartsWith(TEXT("/Game/TownRealism/Town02/")))Owned=true;
      if(!Owned)continue;Tags.Add(C,C->ComponentTags);
      auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("component"),C->GetPathName());Row->SetStringField(TEXT("mesh"),Mesh->GetPathName());Row->SetBoolField(TEXT("actor_collision"),A->GetActorEnableCollision());Row->SetNumberField(TEXT("collision_enabled"),int32(C->GetCollisionEnabled()));
      TArray<TSharedPtr<FJsonValue>> Materials;for(auto* M:C->GetMaterials())Materials.Add(MakeShared<FJsonValueString>(M?M->GetPathName():TEXT("null")));Row->SetArrayField(TEXT("material_slots"),Materials);Selected.Add(MakeShared<FJsonValueObject>(Row));
    }
  }
  TMap<UPrimitiveComponent*,TArray<FName>> IgnoredTags;
  RestoreTags RestoreIgnored{IgnoredTags};
  const TArray<TSharedPtr<FJsonValue>>* IgnoreComponents=nullptr;
  if(Request->TryGetArrayField(TEXT("optical_ignore_components"),IgnoreComponents))for(const auto& Path:*IgnoreComponents) {
    auto* C=FindObject<UPrimitiveComponent>(nullptr,*Path->AsString());
    if(!TestTrue(TEXT("Exact retained collision component exists in copied world"),C && C->GetWorld()==World))return false;
    IgnoredTags.Add(C,C->ComponentTags);C->ComponentTags.AddUnique(TEXT("LidarIgnore"));
  }
  const TArray<TSharedPtr<FJsonValue>>* RenderComponents=nullptr;
  if(Request->TryGetArrayField(TEXT("optical_render_components"),RenderComponents))for(const auto& Path:*RenderComponents) {
    auto* C=FindObject<UPrimitiveComponent>(nullptr,*Path->AsString());
    if(!TestTrue(TEXT("Exact visible revised component is already eligible for scoped diagnostic"),C && Tags.Contains(C)))return false;
    C->ComponentTags.AddUnique(TEXT("LidarOptical"));
  }
  TestTrue(TEXT("Copied Town02 has actual map geometry"),Actors>100 && Components>100);
  TestTrue(TEXT("Copied Town02 contains revised road-visible components"),Tags.Num()>100);
  const TArray<TSharedPtr<FJsonValue>>* GeometryComponents=nullptr;Request->TryGetArrayField(TEXT("geometry_audit_components"),GeometryComponents);
  const auto CaptureGeometry=[&]() {
    TArray<TSharedPtr<FJsonValue>> Out;
    if(GeometryComponents)for(const auto& Path:*GeometryComponents) {
      auto* C=FindObject<UPrimitiveComponent>(nullptr,*Path->AsString());
      TestTrue(TEXT("Requested geometry audit component belongs to actual copied world"),C&&C->GetWorld()==World);
      auto J=Town02ProbeComponentGeometry(C);J->SetStringField(TEXT("requested_component"),Path->AsString());Out.Add(MakeShared<FJsonValueObject>(J));
    }
    return Out;
  };
  const auto GeometryBefore=CaptureGeometry();
  const auto ReadVector=[](const TSharedPtr<FJsonObject>& J,const TCHAR* Key){const auto& V=J->GetArrayField(Key);return FVector(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber());};
  FString SolidRegistry;const bool AlphaControl=Request->TryGetStringField(TEXT("alpha_solid_registry"),SolidRegistry);
  TMap<FString,CarlaAlphaReference::Source> AlphaSources;
  if(AlphaControl) {
    FString SourceText;TSharedPtr<FJsonObject> SourceRoot;
    if(!TestTrue(TEXT("Actual source alpha registry loads"),FFileHelper::LoadFileToString(SourceText,*Original)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceText),SourceRoot)))return false;
    for(const auto& V:SourceRoot->GetArrayField(TEXT("materials"))) {
      const auto Row=V->AsObject();if(!Row->HasField(TEXT("opacity")))continue;
      CarlaAlphaReference::Source Source;if(!TestTrue(TEXT("Independent actual raw camera alpha and hash loads"),Source.Load(Row,FPaths::GetPath(Original))))return false;
      AlphaSources.Add(Row->GetStringField(TEXT("name")),MoveTemp(Source));
    }
    TestEqual(TEXT("Four actual camera alpha profiles"),AlphaSources.Num(),4);
  }
  for(int32 Pass=0;Pass<(AlphaControl?3:2);++Pass) {
    const FString Registry=AlphaControl?(Pass==1?SolidRegistry:Original):(Pass?Request->GetStringField(TEXT("diagnostic_registry")):Original);
    if(!TestTrue(TEXT("Native map probe registry loads"),CarlaLidarInfrared::Reload(Registry,Error)))return false;
    if(!AlphaControl && Pass)for(auto& Item:Tags)Item.Key->ComponentTags.Add(TEXT("LidarIR=diagnostic-coverage"));
    for(const auto& V:Request->GetArrayField(TEXT("rays"))) {
      const auto Ray=V->AsObject();const FVector Origin=ReadVector(Ray,TEXT("origin_cm")),Direction=ReadVector(Ray,TEXT("direction")).GetSafeNormal();
      FCollisionQueryParams Params;Params.bTraceComplex=true;Params.bReturnFaceIndex=true;Params.AddIgnoredActor(Sensor);
      for(float Wet:{0.f,.8f}) {
        FCarlaLidarSurfaceAudit Audit;
        if(LegacyLookup)Audit.LegacyCollisionLookupForTest=true;
        const auto Hit=CarlaLidarOptics::Trace(*Sensor,Origin,Direction,8000,0,Params,nullptr,nullptr,1,905,Wet,-1,&Audit);
        if(TestEqual(TEXT("Actual per-call lookup mode"),Audit.LegacyCollisionLookupUsedForTest,LegacyLookup))++VerifiedLookupCalls;
        if(!LegacyLookup) {
          const auto Production=CarlaLidarOptics::Trace(*Sensor,Origin,Direction,8000,0,Params,nullptr,nullptr,1,905,Wet,-1,nullptr);
          const auto& A=Hit.Hit;const auto& B=Production.Hit;
          const bool Equal=A.bBlockingHit==B.bBlockingHit && A.GetComponent()==B.GetComponent() &&
              A.GetActor()==B.GetActor() && A.FaceIndex==B.FaceIndex && A.Item==B.Item &&
              A.Distance==B.Distance && A.ImpactPoint==B.ImpactPoint && A.ImpactNormal==B.ImpactNormal &&
              Hit.SurfaceReturn==Production.SurfaceReturn && Hit.OpticalDepth==Production.OpticalDepth;
          if(TestTrue(TEXT("Null-audit production Trace exactly matches default repaired audited Trace"),Equal))++VerifiedNullAuditCalls;
        }
        auto Serial=MakeShared<FJsonObject>();FHitResult SerialHit;
        World->LineTraceSingleByChannel(SerialHit,Origin,Origin+Direction*8000,ECC_GameTraceChannel2,Params,FCollisionResponseParams::DefaultResponseParam);Town02ProbeHitGeometry(Serial,SerialHit);
        auto Ordinary=MakeShared<FJsonObject>();FHitResult OrdinaryHit;
        World->ParallelLineTraceSingleByChannel(OrdinaryHit,Origin,Origin+Direction*8000,ECC_GameTraceChannel2,Params,FCollisionResponseParams::DefaultResponseParam);
        Town02ProbeHitGeometry(Ordinary,OrdinaryHit);
        auto Row=MakeShared<FJsonObject>();Row->SetObjectField(TEXT("ordinary_channel2"),Ordinary);Row->SetObjectField(TEXT("ordinary_serial_channel2"),Serial);Row->SetObjectField(TEXT("resolved_material_identity"),Town02ProbeMaterialIdentity(Audit.Material));
        auto OpticalGeometry=MakeShared<FJsonObject>();Town02ProbeHitGeometry(OpticalGeometry,Hit.Hit);Row->SetObjectField(TEXT("optical_geometry"),OpticalGeometry);
        Row->SetStringField(TEXT("ray_id"),Ray->GetStringField(TEXT("id")));Row->SetStringField(TEXT("view"),Ray->GetStringField(TEXT("view")));Row->SetNumberField(TEXT("pass"),Pass);Row->SetStringField(TEXT("pass_label"),AlphaControl?(Pass==1?TEXT("solid-alpha-control"):(Pass==2?TEXT("actual-alpha-repeat"):TEXT("actual-alpha"))):(Pass?TEXT("uniform-diagnostic"):TEXT("candidate")));Row->SetNumberField(TEXT("wetness_fraction"),Wet);Row->SetNumberField(TEXT("strength"),Hit.SurfaceReturn);Row->SetBoolField(TEXT("hit"),Hit.Hit.bBlockingHit);
        if(auto* C=Hit.Hit.GetComponent()) {
          if(auto* Heightfield=Cast<ULandscapeHeightfieldCollisionComponent>(C)) {
            auto Landscape=MakeShared<FJsonObject>();int32 LegacySection=0;auto* Legacy=Heightfield->GetMaterialFromCollisionFaceIndex(Hit.Hit.FaceIndex,LegacySection);auto* Render=Heightfield->GetRenderComponent();auto* RenderMaterial=Render?Render->GetMaterial(0):nullptr;
            Landscape->SetStringField(TEXT("collision_component"),Heightfield->GetPathName());Landscape->SetStringField(TEXT("render_component"),Render?Render->GetPathName():TEXT("null"));Landscape->SetBoolField(TEXT("render_same_world"),Render&&Render->GetWorld()==World);Landscape->SetStringField(TEXT("legacy_collision_material"),Legacy?Legacy->GetPathName():TEXT("null"));Landscape->SetNumberField(TEXT("legacy_section"),LegacySection);Landscape->SetObjectField(TEXT("render_material_identity"),Town02ProbeMaterialIdentity(RenderMaterial));Landscape->SetBoolField(TEXT("resolved_matches_render_material"),Audit.Material && Audit.Material==RenderMaterial);Landscape->SetBoolField(TEXT("local_layer_weights_sampled"),false);Row->SetObjectField(TEXT("landscape_identity"),Landscape);
          }
          const FString Name=(C->GetOwner()->GetName()+TEXT(" ")+C->GetName()).ToLower();
          if(Name.Contains(TEXT("roof")) || Name.Contains(TEXT("coping")) || Name.Contains(TEXT("parapet"))) {Row->SetBoolField(TEXT("excluded_roof_hit"),true);Rows.Add(MakeShared<FJsonValueObject>(Row));continue;}
          if(AlphaControl && Audit.Material)if(const auto* Source=AlphaSources.Find(Audit.Material->GetPathName())) {
            FVector2f UV;const bool Valid=Town02ProbeUV(Hit.Hit,Audit.GeometrySource,UV);Row->SetBoolField(TEXT("source_uv_valid"),Valid);
            if(Valid) {const float Alpha=Source->Alpha(UV);Row->SetNumberField(TEXT("uv0_u"),UV.X);Row->SetNumberField(TEXT("uv0_v"),UV.Y);Row->SetNumberField(TEXT("source_alpha"),Alpha);Row->SetNumberField(TEXT("source_clip"),Source->Clip);Row->SetBoolField(TEXT("source_allows_hit"),Alpha>=Source->Clip);}
          }
          Row->SetStringField(TEXT("component"),C->GetPathName());Row->SetStringField(TEXT("actor"),C->GetOwner()->GetPathName());Row->SetStringField(TEXT("material"),Audit.Material?Audit.Material->GetPathName():TEXT("null"));Row->SetStringField(TEXT("infrared_profile"),CarlaLidarInfrared::ProfileName(Audit.Optical.infrared));Row->SetNumberField(TEXT("infrared_index"),Audit.Optical.infrared);Row->SetNumberField(TEXT("geometry_source"),Audit.GeometrySource);Row->SetNumberField(TEXT("range_m"),Hit.Hit.Distance*.01);Row->SetNumberField(TEXT("face"),Hit.Hit.FaceIndex);Row->SetNumberField(TEXT("instance"),Hit.Hit.Item);Row->SetBoolField(TEXT("visible"),C->IsVisible()&&!C->bHiddenInGame&&!C->GetOwner()->IsHidden());Row->SetBoolField(TEXT("actor_collision"),C->GetOwner()->GetActorEnableCollision());Row->SetNumberField(TEXT("collision_enabled"),int32(C->GetCollisionEnabled()));Row->SetNumberField(TEXT("diffuse"),Audit.Optical.diffuse);Row->SetNumberField(TEXT("retro"),Audit.Optical.retro);Row->SetNumberField(TEXT("specular"),Audit.Optical.specular);Row->SetNumberField(TEXT("transmission"),Audit.Optical.transmission);
        }
        Rows.Add(MakeShared<FJsonValueObject>(Row));
      }
    }
  }
  // Inventory order is not geometry: retain every row, keyed canonically by component.
  Selected.Sort([](const TSharedPtr<FJsonValue>& A,const TSharedPtr<FJsonValue>& B){return A->AsObject()->GetStringField(TEXT("component"))<B->AsObject()->GetStringField(TEXT("component"));});
  auto Report=MakeShared<FJsonObject>();Report->SetArrayField(TEXT("geometry_before"),GeometryBefore);Report->SetArrayField(TEXT("geometry_after"),CaptureGeometry());Report->SetBoolField(TEXT("alpha_only_control"),AlphaControl);Report->SetArrayField(TEXT("rays"),Rows);Report->SetArrayField(TEXT("diagnostic_components"),Selected);Report->SetNumberField(TEXT("actor_count"),Actors);Report->SetNumberField(TEXT("static_component_count"),Components);
  Report->SetStringField(TEXT("scope"),AlphaControl?TEXT("One copied saved Town02 editor world: actual alpha, solid-alpha-only control and actual-alpha repeat. Identical coefficients, geometry policy and dry/wet inputs. Raw source alpha oracle on exact native render triangle/UV0. No saves, gameplay or live receiver/camera acceptance."):TEXT("Copied saved Town02 native CPU material/uniform diagnostic; no saves or live acceptance."));
  Report->SetStringField(TEXT("lookup_mode"),LegacyLookup?TEXT("legacy-editor-test"):TEXT("default-production-repaired"));
  Report->SetNumberField(TEXT("verified_lookup_calls"),VerifiedLookupCalls);
  Report->SetNumberField(TEXT("verified_null_audit_calls"),VerifiedNullAuditCalls);
  Report->SetBoolField(TEXT("legacy_control_requested"),LegacyLookup);
  UE_LOG(LogCarla,Display,TEXT("TOWN02_LOOKUP_MODE legacy=%d verified=%d rows=%d"),int32(LegacyLookup),VerifiedLookupCalls,Rows.Num());
  UE_LOG(LogCarla,Display,TEXT("TOWN02_NULL_AUDIT_PRODUCTION verified=%d"),VerifiedNullAuditCalls);
  FString JSON;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&JSON));TestTrue(TEXT("Native Town02 probe report saves"),FFileHelper::SaveStringToFile(JSON,*Request->GetStringField(TEXT("output"))));UE_LOG(LogCarla,Display,TEXT("TOWN02_MATERIAL_PROBE actors=%d components=%d diagnostic=%d rows=%d"),Actors,Components,Tags.Num(),Rows.Num());return true;
}
static FAutoConsoleCommand Town02MaterialProbeCommand(TEXT("carla.Sensors.ProbeTown02Materials"),TEXT("Read-only copied editor-map probe; request JSON path."),FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){
  if(Args.Num()!=1)return;auto& F=FAutomationTestFramework::Get();if(F.GetCurrentTest())return;F.StartTestByName(TEXT("FCarlaTown02MaterialProbe ")+Args[0],0);if(!F.GetCurrentTest())return;FAutomationTestExecutionInfo Info;const bool Passed=F.StopTest(Info);
  for(const auto& E:Info.GetEntries())if(E.Event.Type==EAutomationEventType::Error)UE_LOG(LogCarla,Display,TEXT("TOWN02_MATERIAL_ERROR %s"),*E.Event.Message);
  UE_LOG(LogCarla,Display,TEXT("TOWN02_MATERIAL_TEST %s errors=%d"),Passed?TEXT("PASSED"):TEXT("FAILED"),Info.GetErrorTotal());}));

#endif // WITH_EDITOR
