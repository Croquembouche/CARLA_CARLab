#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "InteriorOccupantComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteriorMannequinTest,"Interior.Lincoln.SeatsPoseBlinkAndReach",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FInteriorMannequinTest::RunTest(const FString& Parameters)
{
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/VehicleInteriors/Lincoln/SK_SeatedMannequin.SK_SeatedMannequin"));
    if(!TestNotNull(TEXT("Authored mannequin imported"),Mesh)) return false;
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    World->InitializeNewWorld(UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false));
    AActor* Actor=World->SpawnActor<AActor>();
    auto* C=NewObject<UInteriorOccupantComponent>(Actor);
    Actor->SetRootComponent(C);C->MannequinMesh=Mesh;C->RegisterComponent();C->BeginPlay();
    C->bAutoBlink=false;
    for(int32 I=0;I<5;++I) {C->SetSeat(I);TestTrue(TEXT("Seat position matches calibrated anchor"),C->GetRelativeLocation().Equals(C->SeatLocations[I],.01f));}
    C->SetSeat(99);TestEqual(TEXT("Invalid seat does not move occupant"),C->SeatIndex,4);
    C->SetSeat(0);
    const FVector Head=C->Body->GetBoneLocationByName("head",EBoneSpaces::ComponentSpace);
    const FVector Left=C->Body->GetBoneLocationByName("hand_l",EBoneSpaces::ComponentSpace);
    TestTrue(TEXT("FBX units: head 50-65cm above pelvis"),Head.Z>50 && Head.Z<65);
    TestTrue(TEXT("FBX handedness: left hand negative Y, forward positive X"),Left.Y<0 && Left.X>0);
    C->SetLook(35,-15);C->TickComponent(.02f,LEVELTICK_All,nullptr);
    auto H=C->Body->GetBoneRotationByName("head",EBoneSpaces::ComponentSpace);
    TestFalse(TEXT("Head pose remains finite"),H.ContainsNaN());
    C->BlinkWeight=1;C->TickComponent(.2f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Eyelid closes"),C->Body->GetBoneScaleByName("lid_l",EBoneSpaces::ComponentSpace).Equals(FVector(1),.01));
    C->BlinkWeight=0;C->TickComponent(.2f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Eyelid opens"),C->Body->GetBoneScaleByName("lid_l",EBoneSpaces::ComponentSpace).GetMax()<.01);
    C->Blink();C->TickComponent(.09f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Triggered blink reaches closure"),C->Body->GetBoneScaleByName("lid_l",EBoneSpaces::ComponentSpace).GetMin()>.95f);
    for(bool bLeft:{true,false}) {
        const FName U=bLeft?"upperarm_l":"upperarm_r",L=bLeft?"forearm_l":"forearm_r",HandName=bLeft?"hand_l":"hand_r";
        C->SetHandTarget(bLeft,FVector(1000,bLeft?-1000:1000,1000),FRotator(5,10,0));C->TickComponent(.02f,LEVELTICK_All,nullptr);
        auto S=C->Body->GetBoneLocationByName(U,EBoneSpaces::ComponentSpace);
        auto E=C->Body->GetBoneLocationByName(L,EBoneSpaces::ComponentSpace);
        auto T=C->Body->GetBoneLocationByName(HandName,EBoneSpaces::ComponentSpace);
        TestFalse(TEXT("Unreachable controller target remains finite"),T.ContainsNaN());
        TestTrue(TEXT("IK clamps physical arm reach"),FVector::Distance(S,T)<55.f);
        TestTrue(TEXT("IK retains upper and lower arm lengths"),FVector::Distance(S,E)>20 && FVector::Distance(E,T)>20);
    }
    C->EndPlay(EEndPlayReason::Destroyed);World->DestroyWorld(false);
    return true;
}
#endif
