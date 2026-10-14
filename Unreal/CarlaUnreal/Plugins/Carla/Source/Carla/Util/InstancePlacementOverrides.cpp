// Copyright (c) 2026. Licensed under the MIT license.
#include "Carla/Util/InstancePlacementOverrides.h"
#include "Carla.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Containers/UnrealString.h"

namespace CarlaInstancePlacement
{
namespace
{
constexpr const TCHAR* Prefix = TEXT("carla.instance-placement:");
constexpr double PositionToleranceCm = 2.0;
struct FEdit
{
  UInstancedStaticMeshComponent* Component = nullptr;
  int32 Index = INDEX_NONE;
  FTransform Transform;
};

bool ParseVector(const FString& Text, FVector& Out)
{
  TArray<FString> Fields;
  Text.ParseIntoArray(Fields, TEXT(","), false);
  if (Fields.Num() != 3) return false;
  double Values[3];
  for (int32 I=0; I<3; ++I)
    if (Fields[I].IsEmpty() || !Fields[I].IsNumeric() ||
        !LexTryParseString(Values[I], *Fields[I]) || !FMath::IsFinite(Values[I])) return false;
  Out = FVector(Values[0],Values[1],Values[2]);
  return true;
}

// Planning is read-only. Invalid or ambiguous metadata never partially changes
// an actor. An already applied override is accepted on subsequent registrations.
bool Plan(const TArray<FName>& Tags, const TArray<UInstancedStaticMeshComponent*>& Components,
          TArray<FEdit>& Edits, int32& Already, FString& Error)
{
  Edits.Reset(); Already=0; Error.Reset();
  TMap<UInstancedStaticMeshComponent*,TSet<int32>> Claimed;
  for (const auto& Tag : Tags)
  {
    FString Text=Tag.ToString();
    if (!Text.RemoveFromStart(Prefix, ESearchCase::CaseSensitive)) continue;
    TArray<FString> Positions;
    Text.ParseIntoArray(Positions,TEXT(":"),false);
    FVector Old, Target;
    if (Positions.Num()!=2 || !ParseVector(Positions[0],Old) || !ParseVector(Positions[1],Target))
    { Error=TEXT("invalid finite old/target coordinates"); return false; }
    TArray<FEdit> OldMatches, TargetMatches;
    for (auto* Component : Components)
      if (Component)
        for (int32 I=0; I<Component->GetInstanceCount(); ++I)
        {
          FTransform T;
          if (!Component->GetInstanceTransform(I,T,true))
          { Error=TEXT("unable to read an instance transform"); return false; }
          if (T.GetLocation().Equals(Old,PositionToleranceCm)) OldMatches.Add({Component,I,T});
          if (T.GetLocation().Equals(Target,PositionToleranceCm)) TargetMatches.Add({Component,I,T});
        }
    FEdit Edit;
    bool IsAlready=false;
    if (OldMatches.Num()==1 && (TargetMatches.Num()==0 || Old.Equals(Target,PositionToleranceCm)))
      Edit=OldMatches[0];
    else if (OldMatches.Num()==0 && TargetMatches.Num()==1)
    { Edit=TargetMatches[0]; IsAlready=true; }
    else
    { Error=FString::Printf(TEXT("ambiguous/stale placement old=%d target=%d"),OldMatches.Num(),TargetMatches.Num()); return false; }
    auto& Indices=Claimed.FindOrAdd(Edit.Component);
    if (Indices.Contains(Edit.Index))
    { Error=TEXT("multiple overrides claim the same instance"); return false; }
    Indices.Add(Edit.Index);
    if (IsAlready || Old.Equals(Target,PositionToleranceCm)) ++Already;
    else
    {
      Edit.Transform.SetLocation(Target);
      Edits.Add(Edit);
    }
  }
  return true;
}
}

void Apply(const TArray<AActor*>& Actors)
{
  int32 TaggedActors=0, Applied=0, Already=0, Invalid=0;
  for (auto* Actor : Actors)
  {
    if (!Actor || !Actor->Tags.ContainsByPredicate([](const FName& Tag)
        { return Tag.ToString().StartsWith(Prefix,ESearchCase::CaseSensitive); })) continue;
    ++TaggedActors;
    TArray<UInstancedStaticMeshComponent*> Components;
    Actor->GetComponents<UInstancedStaticMeshComponent>(Components);
    TArray<FEdit> Edits; int32 Previous=0; FString Error;
    if (!Plan(Actor->Tags,Components,Edits,Previous,Error))
    {
      ++Invalid;
      UE_LOG(LogCarla,Error,TEXT("INSTANCE_PLACEMENT_OVERRIDES actor=%s rejected=%s"),*Actor->GetName(),*Error);
      continue;
    }
    for (const auto& Edit : Edits)
      if (!Edit.Component->UpdateInstanceTransform(Edit.Index,Edit.Transform,true,true,true))
        UE_LOG(LogCarla,Fatal,TEXT("Failed to apply a validated instance placement on %s"),*Actor->GetName());
    Applied+=Edits.Num(); Already+=Previous;
    UE_LOG(LogCarla,Display,TEXT("INSTANCE_PLACEMENT_OVERRIDES actor=%s applied=%d already=%d"),
        *Actor->GetName(),Edits.Num(),Previous);
  }
  if (TaggedActors)
    UE_LOG(LogCarla,Display,TEXT("MAP_INSTANCE_PLACEMENTS actors=%d applied=%d already=%d invalid=%d"),
        TaggedActors,Applied,Already,Invalid);
}

#if WITH_DEV_AUTOMATION_TESTS
}
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaInstancePlacementTest,"Carla.Map.InstancePlacementOverrides",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FCarlaInstancePlacementTest::RunTest(const FString&)
{
  using namespace CarlaInstancePlacement;
  FVector Parsed;
  TestTrue(TEXT("Signed decimal coordinates"),ParseVector(TEXT("+1.25,-.5,0.0"),Parsed));
  TestTrue(TEXT("Decimal values preserved"),Parsed.Equals(FVector(1.25,-.5,0)));
  auto* Component=NewObject<UInstancedStaticMeshComponent>(GetTransientPackage());
  const FQuat Rotation(FVector::UpVector,.37);
  const FVector Scale(1.5,.75,2);
  for (int32 I=0; I<10; ++I)
    Component->AddInstance(FTransform(Rotation,FVector(100*I,200,300),Scale));
  TArray<UInstancedStaticMeshComponent*> Components{Component};
  TArray<FEdit> Edits; int32 Already; FString Error;
  const TArray<FName> Valid{TEXT("ordinary-material-tag"),TEXT("carla.instance-placement:100,200,300:100,700,310")};
  TestTrue(TEXT("A unique placement can be planned"),Plan(Valid,Components,Edits,Already,Error));
  TestEqual(TEXT("One placement"),Edits.Num(),1);
  TestEqual(TEXT("No prior placement"),Already,0);
  FTransform Before;Component->GetInstanceTransform(1,Before,true);
  TestTrue(TEXT("Planning does not mutate the component"),Before.GetLocation().Equals(FVector(100,200,300)));
  if (Edits.Num()!=1) return false;
  TestTrue(TEXT("Rotation preserved"),Edits[0].Transform.GetRotation().Equals(Rotation));
  TestTrue(TEXT("Scale preserved"),Edits[0].Transform.GetScale3D().Equals(Scale));
  TestTrue(TEXT("Application succeeds"),Component->UpdateInstanceTransform(1,Edits[0].Transform,true,true,true));
  TestEqual(TEXT("All instances retained"),Component->GetInstanceCount(),10);
  for (int32 I=0;I<10;++I)
  {
    FTransform T;Component->GetInstanceTransform(I,T,true);
    TestTrue(TEXT("Only the selected instance is translated"),T.GetLocation().Equals(I==1?FVector(100,700,310):FVector(100*I,200,300)));
  }
  TestTrue(TEXT("Repeated registration is accepted"),Plan(Valid,Components,Edits,Already,Error));
  TestEqual(TEXT("No second mutation"),Edits.Num(),0);
  TestEqual(TEXT("One already applied override"),Already,1);
  for (const TCHAR* Bad : {TEXT("carla.instance-placement:1,2:3,4,5"),
       TEXT("carla.instance-placement:1,2,3:nan,4,5"),TEXT("carla.instance-placement:1,2,3:inf,4,5"),
       TEXT("carla.instance-placement:1,2,3:4,,6"),TEXT("carla.instance-placement:1,2,3:4junk,5,6"),
       TEXT("carla.instance-placement:1,2,3:.,5,6"),TEXT("carla.instance-placement:999,2,3:4,5,6")})
    TestFalse(TEXT("Invalid/stale metadata is rejected"),Plan({Bad},Components,Edits,Already,Error));
  TestFalse(TEXT("Two overrides cannot claim one instance"),Plan({Valid[1],Valid[1]},Components,Edits,Already,Error));
  Component->AddInstance(FTransform(Rotation,FVector(0,200,300),Scale));
  TestFalse(TEXT("Duplicate source positions are rejected"),Plan({TEXT("carla.instance-placement:0,200,300:0,800,300")},Components,Edits,Already,Error));
  TestFalse(TEXT("Mixed valid/invalid tags are rejected before application"),
      Plan({TEXT("carla.instance-placement:200,200,300:200,800,300"),TEXT("carla.instance-placement:bad")},Components,Edits,Already,Error));
  FTransform Untouched;Component->GetInstanceTransform(2,Untouched,true);
  TestTrue(TEXT("Rejected actor remains unmodified"),Untouched.GetLocation().Equals(FVector(200,200,300)));
  return true;
}
static FAutoConsoleCommand TestInstancePlacements(TEXT("carla.Map.TestInstancePlacements"),
    TEXT("Run CPU placement parsing, ownership, preservation and idempotence tests."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
      auto& Framework=FAutomationTestFramework::Get();
      if (Framework.GetCurrentTest()) { UE_LOG(LogCarla,Warning,TEXT("INSTANCE_PLACEMENT_TEST busy"));return; }
      Framework.StartTestByName(TEXT("FCarlaInstancePlacementTest"),0,TEXT("Carla.Map.InstancePlacementOverrides"));
      if (!Framework.GetCurrentTest()) { UE_LOG(LogCarla,Warning,TEXT("INSTANCE_PLACEMENT_TEST result=Fail test_did_not_start"));return; }
      FAutomationTestExecutionInfo Info;
      const bool Passed=Framework.StopTest(Info);
      UE_LOG(LogCarla,Display,TEXT("INSTANCE_PLACEMENT_TEST result=%s"),Passed?TEXT("Success"):TEXT("Fail"));
    }));
namespace CarlaInstancePlacement
{
#endif
}
