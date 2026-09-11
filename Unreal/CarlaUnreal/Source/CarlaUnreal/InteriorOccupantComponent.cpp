#include "InteriorOccupantComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "MotionControllerComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"
#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

UInteriorOccupantComponent::UInteriorOccupantComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    SeatLocations = {FVector(18,-40,69), FVector(18,40,69), FVector(-84,-40,69), FVector(-84,0,69), FVector(-84,40,69)};
}

void UInteriorOccupantComponent::BeginPlay()
{
    Super::BeginPlay();
    TArray<UMeshComponent*> CabinMeshes;
    GetOwner()->GetComponents(CabinMeshes);
    // The Lincoln replacement occupies the original cabin section. Hide that
    // section in every LOD so fallback shaders cannot draw over the new cabin.
    const bool bHasCabinReplacement = CabinMeshes.ContainsByPredicate([](const UMeshComponent* Mesh) {
        const auto* Static = Cast<UStaticMeshComponent>(Mesh);
        return Static && Static->GetName() == TEXT("CabinReplacement") && Static->GetStaticMesh();
    });
    if (bHasCabinReplacement) {
        for (UMeshComponent* Mesh : CabinMeshes) {
            auto* Skinned = Cast<USkinnedMeshComponent>(Mesh);
            if (Skinned && Skinned->GetName() == TEXT("VehicleMesh") &&
                GetNameSafe(Skinned->GetMaterial(2)) == TEXT("M_Hidden_StockCabin")) {
                for (int32 LOD=0; LOD<Skinned->GetNumLODs(); ++LOD) {
                    Skinned->ShowMaterialSection(2, 2, false, LOD);
                }
            }
        }
    }
    Body = NewObject<UPoseableMeshComponent>(GetOwner(), NAME_None);
    Body->SetupAttachment(this);
    Body->SetSkeletalMesh(MannequinMesh);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    Body->RegisterComponent();
    Body->AddTickPrerequisiteComponent(this);
    for (int32 I=0; I<Body->GetNumBones(); ++I) {
        const FName N = Body->GetBoneName(I);
        Rest.Add(N, Body->GetBoneTransformByName(N, EBoneSpaces::ComponentSpace));
    }
    for(const TCHAR* Name:{TEXT("head"),TEXT("spine"),TEXT("eye_l"),TEXT("eye_r"),TEXT("lid_l"),TEXT("lid_r"),TEXT("upperarm_l"),TEXT("upperarm_r"),TEXT("forearm_l"),TEXT("forearm_r"),TEXT("hand_l"),TEXT("hand_r")}) {
        if(!Rest.Contains(FName(Name))) {
            UE_LOG(LogTemp,Warning,TEXT("Interior occupant rig missing %s; retarget the replacement mesh before enabling pose controls."),Name);
            SetComponentTickEnabled(false);
            return;
        }
    }
    TrackingOrigin = NewObject<USceneComponent>(GetOwner());
    TrackingOrigin->SetupAttachment(this);
    TrackingOrigin->RegisterComponent();
    LeftController = NewObject<UMotionControllerComponent>(GetOwner());
    LeftController->SetupAttachment(TrackingOrigin);
    LeftController->SetTrackingMotionSource(FName("Left"));
    LeftController->RegisterComponent();
    RightController = NewObject<UMotionControllerComponent>(GetOwner());
    RightController->SetupAttachment(TrackingOrigin);
    RightController->SetTrackingMotionSource(FName("Right"));
    RightController->RegisterComponent();
    VRCamera = NewObject<UCameraComponent>(GetOwner());
    VRCamera->SetupAttachment(TrackingOrigin);
    VRCamera->bLockToHmd = true;
    VRCamera->bAutoActivate = false;
    VRCamera->RegisterComponent();
    VRCamera->Deactivate();
    SetSeat(SeatIndex);
    if (bTrackXR) CalibrateXR();
}

void UInteriorOccupantComponent::SetSeat(int32 Index)
{
    if (SeatLocations.IsValidIndex(Index)) {
        SeatIndex = Index;
        SetRelativeLocation(SeatLocations[Index]);
        HandActive[0] = HandActive[1] = false;
        if(Index>0) {
            SetHandTarget(true,FVector(22,-13,8),FRotator::ZeroRotator);
            SetHandTarget(false,FVector(22,13,8),FRotator::ZeroRotator);
        }
    }
}
void UInteriorOccupantComponent::Blink() { BlinkElapsed = 0.f; }
void UInteriorOccupantComponent::SetLook(float Yaw, float Pitch)
{
    HeadRotation = FRotator(FMath::Clamp(Pitch,-55.f,55.f),FMath::Clamp(Yaw,-85.f,85.f),0);
}
void UInteriorOccupantComponent::SetHandTarget(bool bLeft, FVector Position, FRotator Rotation)
{
    if (Position.ContainsNaN() || Rotation.ContainsNaN()) return;
    const int32 I = bLeft ? 0 : 1;
    HandPositions[I] = Position;
    HandRotations[I] = Rotation;
    HandActive[I] = true;
}
void UInteriorOccupantComponent::CalibrateXR()
{
    if (!TrackingOrigin || !Rest.Contains("head")) return;
    FRotator R; FVector P;
    UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(R, P);
    const FQuat Yaw = FRotator(0,-R.Yaw,0).Quaternion();
    TrackingOrigin->SetRelativeRotation(Yaw);
    const FVector Eyes=(Rest["eye_l"].GetLocation()+Rest["eye_r"].GetLocation())*.5f;
    TrackingOrigin->SetRelativeLocation(Eyes - Yaw.RotateVector(P));
}
bool UInteriorOccupantComponent::ActivateVR()
{
    if (!UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected()) return false;
    if (!UHeadMountedDisplayFunctionLibrary::EnableHMD(true)) return false;
    bTrackXR = true;
    CalibrateXR();
    if (VRCamera) {
        VRCamera->Activate();
        if (auto* PC=UGameplayStatics::GetPlayerController(this,0)) PC->SetViewTarget(GetOwner());
    }
    return true;
}
void UInteriorOccupantComponent::AimBone(FName Bone, FVector Start, FVector End, FName Child)
{
    if (!Rest.Contains(Bone) || !Rest.Contains(Child)) return;
    FTransform T=Rest[Bone];
    const FVector D=(Rest[Child].GetLocation()-T.GetLocation()).GetSafeNormal();
    T.SetRotation(FQuat::FindBetweenNormals(D,(End-Start).GetSafeNormal())*T.GetRotation());
    T.SetLocation(Start);
    Body->SetBoneTransformByName(Bone,T,EBoneSpaces::ComponentSpace);
}
void UInteriorOccupantComponent::SolveArm(bool bLeft)
{
    const int32 I=bLeft?0:1;
    if (!HandActive[I]) return;
    const FName Upper=bLeft?"upperarm_l":"upperarm_r", Lower=bLeft?"forearm_l":"forearm_r", Hand=bLeft?"hand_l":"hand_r";
    if (!Rest.Contains(Upper)||!Rest.Contains(Lower)||!Rest.Contains(Hand)) return;
    const FVector S=Rest[Upper].GetLocation()+HeadOffset*.65f;
    const float A=FVector::Distance(Rest[Upper].GetLocation(),Rest[Lower].GetLocation()), B=FVector::Distance(Rest[Lower].GetLocation(),Rest[Hand].GetLocation());
    FVector D=HandPositions[I]-S;
    const float Length=FMath::Clamp(D.Size(),FMath::Abs(A-B)+.01f,A+B-.01f);
    D=D.GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    const FVector Target=S+D*Length;
    const float Along=(A*A-B*B+Length*Length)/(2*Length);
    FVector Pole=FVector(0,bLeft?-1:1,-.65f);
    Pole=(Pole-D*FVector::DotProduct(Pole,D)).GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    const FVector Elbow=S+D*Along+Pole*FMath::Sqrt(FMath::Max(0.f,A*A-Along*Along));
    AimBone(Upper,S,Elbow,Lower);
    AimBone(Lower,Elbow,Target,Hand);
    FTransform T=Rest[Hand]; T.SetLocation(Target); T.SetRotation(HandRotations[I].Quaternion()*T.GetRotation());
    Body->SetBoneTransformByName(Hand,T,EBoneSpaces::ComponentSpace);
}
void UInteriorOccupantComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Fn)
{
    Super::TickComponent(Dt,TickType,Fn);
    if (!Body || Rest.Num()==0) return;
    Age+=Dt; BlinkElapsed+=Dt;
    if (bAutoBlink && Age>=NextBlink) {Blink(); NextBlink=Age+3.5f;}
    if (bEnableDesktopControls) if(auto* PC=UGameplayStatics::GetPlayerController(this,0)) {
        if(PC->WasInputKeyJustPressed(EKeys::F6)) Blink();
        if(PC->WasInputKeyJustPressed(EKeys::F7)) SetSeat((SeatIndex+1)%SeatLocations.Num());
        if(PC->WasInputKeyJustPressed(EKeys::F8)) CalibrateXR();
        if(PC->WasInputKeyJustPressed(EKeys::F9)) ActivateVR();
        SetLook(HeadRotation.Yaw+((PC->IsInputKeyDown(EKeys::L)?1:0)-(PC->IsInputKeyDown(EKeys::J)?1:0))*Dt*50,
                HeadRotation.Pitch+((PC->IsInputKeyDown(EKeys::I)?1:0)-(PC->IsInputKeyDown(EKeys::K)?1:0))*Dt*40);
    }
    if (bTrackXR) if (auto* PC=UGameplayStatics::GetPlayerController(this,0)) {
        if(PC->WasInputKeyJustPressed(EKeys::OculusTouch_Right_A_Click)) Blink();
        if(PC->WasInputKeyJustPressed(EKeys::OculusTouch_Left_X_Click)) SetSeat((SeatIndex+1)%SeatLocations.Num());
        if(PC->WasInputKeyJustPressed(EKeys::OculusTouch_Left_Y_Click)) CalibrateXR();
        EyeRotation=FRotator(PC->GetInputAnalogKeyState(EKeys::OculusTouch_Right_Thumbstick_Y)*25.f, PC->GetInputAnalogKeyState(EKeys::OculusTouch_Right_Thumbstick_X)*30.f,0);
    }
    bHeadTracked=bTrackXR && UHeadMountedDisplayFunctionLibrary::HasValidTrackingPosition();
    bLeftHandTracked=bTrackXR && LeftController->IsTracked();
    bRightHandTracked=bTrackXR && RightController->IsTracked();
    if(bHeadTracked) {
        FRotator R; FVector P;
        UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(R,P);
        HeadRotation=(TrackingOrigin->GetRelativeRotation().Quaternion()*R.Quaternion()).Rotator();
        const FVector Eyes=(Rest["eye_l"].GetLocation()+Rest["eye_r"].GetLocation())*.5f;
        const FVector H=Rest["head"].GetLocation();
        HeadOffset=(TrackingOrigin->GetRelativeTransform().TransformPosition(P)-H-HeadRotation.Quaternion().RotateVector(Eyes-H)).GetClampedToMaxSize(12.f);
    }
    if(bLeftHandTracked) SetHandTarget(true,GetComponentTransform().InverseTransformPosition(LeftController->GetComponentLocation()),(GetComponentQuat().Inverse()*LeftController->GetComponentQuat()).Rotator());
    if(bRightHandTracked) SetHandTarget(false,GetComponentTransform().InverseTransformPosition(RightController->GetComponentLocation()),(GetComponentQuat().Inverse()*RightController->GetComponentQuat()).Rotator());
    // Parent bones must reset before children (not TMap iteration order).
    for(int32 I=0;I<Body->GetNumBones();++I) {
        const FName N=Body->GetBoneName(I);
        if(Rest.Contains(N)) Body->SetBoneTransformByName(N,Rest[N],EBoneSpaces::ComponentSpace);
    }
    for(const TCHAR* Name:{TEXT("spine"),TEXT("upperarm_l"),TEXT("upperarm_r"),TEXT("forearm_l"),TEXT("forearm_r"),TEXT("hand_l"),TEXT("hand_r")}) {
        const FName N(Name);if(!Rest.Contains(N))continue;
        FTransform T=Rest[N];T.AddToTranslation(HeadOffset*.65f);
        Body->SetBoneTransformByName(N,T,EBoneSpaces::ComponentSpace);
    }
    SolveArm(true); SolveArm(false);
    if(Rest.Contains("head")) {
        const FTransform H=Rest["head"];
        const FQuat Q=HeadRotation.Quaternion();
        const float Close=FMath::Max(FMath::Clamp(BlinkWeight,0.f,1.f),BlinkElapsed<.18f?FMath::Sin(PI*BlinkElapsed/.18f):0.f);
        for(const TCHAR* Name:{TEXT("head"),TEXT("eye_l"),TEXT("eye_r"),TEXT("lid_l"),TEXT("lid_r")}) {
            const FName N(Name); if(!Rest.Contains(N)) continue;
            FTransform T=Rest[N];
            T.SetLocation(H.GetLocation()+HeadOffset+Q.RotateVector(T.GetLocation()-H.GetLocation()));
            T.SetRotation(Q*T.GetRotation());
            if(N=="eye_l"||N=="eye_r") T.SetRotation(Q*EyeRotation.Quaternion()*Rest[N].GetRotation());
            if(N=="lid_l"||N=="lid_r") T.SetScale3D(FVector(FMath::Max(.001f,Close)));
            Body->SetBoneTransformByName(N,T,EBoneSpaces::ComponentSpace);
        }
    }
}
