#include "Carla/Actor/ActorDispatcher.h"
#include "Carla/Actor/ActorDefinition.h"
#include "Carla/Actor/ActorDescription.h"
#include "Engine/StaticMeshActor.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Carla.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorDefinitionLazyClassTest,
    "Carla.ActorDefinitions.LazyClasses",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FActorDefinitionLazyClassTest::RunTest(const FString& Parameters)
{
  TStrongObjectPtr<UActorDispatcher> Dispatcher(NewObject<UActorDispatcher>());
  FActorDefinition Lazy;
  Lazy.Id = TEXT("test.actor.lazy");
  Lazy.Tags = TEXT("test");
  Lazy.SoftClass = TSoftClassPtr<AActor>(FSoftObjectPath(AActor::StaticClass()));
  auto EmptySpawn = [](const FTransform&, const FActorDescription&) { return FActorSpawnResult(); };
  Dispatcher->Bind(Lazy, EmptySpawn);
  TestEqual(TEXT("A deferred definition remains in the catalogue"), Dispatcher->GetActorDefinitions().Num(), 1);
  TestNull(TEXT("Catalogue registration does not resolve the class"), Dispatcher->Classes.FindChecked(Lazy.Id).Get());
  TestTrue(TEXT("First use resolves the requested target"), Dispatcher->ResolveActorClass(Lazy.Id) == AActor::StaticClass());
  TestTrue(TEXT("Resolved class is retained for later spawns"), Dispatcher->Classes.FindChecked(Lazy.Id).Get() == AActor::StaticClass());
  Dispatcher->SoftClasses.FindChecked(Lazy.Id) = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Script/Engine.DoesNotExist_Verification")));
  TestTrue(TEXT("A cached target is reused without another load"), Dispatcher->ResolveActorClass(Lazy.Id) == AActor::StaticClass());
  TestNotNull(TEXT("Resolved target map participates in garbage collection"), FindFProperty<FMapProperty>(UActorDispatcher::StaticClass(), TEXT("Classes")));

  FActorDefinition Eager;
  Eager.Id = Lazy.Id;
  Eager.Tags = Lazy.Tags;
  Eager.Class = AStaticMeshActor::StaticClass();
  Dispatcher->Bind(Eager, EmptySpawn);
  TestFalse(TEXT("A new eager binding removes the obsolete deferred target"), Dispatcher->SoftClasses.Contains(Lazy.Id));
  TestTrue(TEXT("Latest binding retains existing class selection semantics"), Dispatcher->ResolveActorClass(Lazy.Id) == AStaticMeshActor::StaticClass());
  TestNull(TEXT("Unknown IDs do not acquire a target"), Dispatcher->ResolveActorClass(TEXT("test.actor.unknown")));

  FActorDefinition Optional;
  Optional.Id = TEXT("test.actor.optional");
  Optional.Tags = TEXT("test");
  Dispatcher->Bind(Optional, EmptySpawn);
  TestNull(TEXT("Existing class-optional factories remain supported"), Dispatcher->ResolveActorClass(Optional.Id));
  TestFalse(TEXT("An optional class is not treated as a failed deferred load"), Dispatcher->SoftClasses.Contains(Optional.Id));

  FActorDefinition Missing;
  Missing.Id = TEXT("test.actor.missing");
  Missing.Tags = TEXT("test");
  Missing.SoftClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Script/Engine.DoesNotExist_Verification")));
  bool bFactoryInvoked = false;
  Dispatcher->Bind(Missing, [&bFactoryInvoked](const FTransform&, const FActorDescription&) {
    bFactoryInvoked = true; return FActorSpawnResult();
  });
  FActorDescription Description;
  Description.Id = Missing.Id;
  Description.UId = Dispatcher->GetActorDefinitions().Last().UId;
  AddExpectedError(TEXT("DoesNotExist_Verification"), EAutomationExpectedErrorFlags::Contains, 0);
  AddExpectedError(TEXT("Cannot resolve actor class"), EAutomationExpectedErrorFlags::Contains, 2);
  const auto Result = Dispatcher->SpawnActor(FTransform::Identity, Description);
  TestTrue(TEXT("Unresolvable class reports InvalidDescription"), Result.Key == EActorSpawnResultStatus::InvalidDescription);
  TestNull(TEXT("Failed deferred spawn creates no actor"), Result.Value);
  TestNull(TEXT("Failed deferred dormant respawn creates no actor"), Dispatcher->ReSpawnActor(FTransform::Identity, Description));
  TestFalse(TEXT("A factory never receives a failed deferred target"), bFactoryInvoked);
  return true;
}
// Run the CPU-only case without the interactive AutomationController FPS gate.
static FAutoConsoleCommand CCmdTestLazyActorClasses(
    TEXT("carla.ActorDefinitions.TestLazyClasses"),
    TEXT("Run deferred actor target resolution tests."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
      auto& Framework = FAutomationTestFramework::Get();
      if (Framework.GetCurrentTest())
      {
        UE_LOG(LogCarla, Warning, TEXT("ACTOR_DEFINITION_LAZY_TEST busy"));
        return;
      }
      Framework.StartTestByName(TEXT("FActorDefinitionLazyClassTest"), 0, TEXT("Carla.ActorDefinitions.LazyClasses"));
      if (!Framework.GetCurrentTest())
      {
        UE_LOG(LogCarla, Warning, TEXT("ACTOR_DEFINITION_LAZY_TEST result=Fail test_did_not_start"));
        return;
      }
      FAutomationTestExecutionInfo Info;
      const bool bPassed = Framework.StopTest(Info);
      UE_LOG(LogCarla, Display, TEXT("ACTOR_DEFINITION_LAZY_TEST result=%s"), bPassed ? TEXT("Success") : TEXT("Fail"));
    }));
#endif
