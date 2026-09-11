#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "InteriorOccupantComponent.generated.h"

class UPoseableMeshComponent;
class UMotionControllerComponent;
class UCameraComponent;
class USkeletalMesh;

/** Seated research mannequin. All public targets are seat-local UE centimeters.
 * Eye controls are synthetic inputs, never described as Quest 2 eye tracking. */
UCLASS(ClassGroup=(Interior), meta=(BlueprintSpawnableComponent))
class CARLAUNREAL_API UInteriorOccupantComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UInteriorOccupantComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") TObjectPtr<USkeletalMesh> MannequinMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") int32 SeatIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") TArray<FVector> SeatLocations;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") bool bAutoBlink = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") bool bEnableDesktopControls = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") bool bTrackXR = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") FRotator HeadRotation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") FVector HeadOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant") FRotator EyeRotation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Occupant", meta=(ClampMin="0", ClampMax="1")) float BlinkWeight = 0;
    UPROPERTY(BlueprintReadOnly, Category="Occupant") TObjectPtr<UPoseableMeshComponent> Body;
    UPROPERTY(BlueprintReadOnly, Category="Occupant") bool bHeadTracked = false;
    UPROPERTY(BlueprintReadOnly, Category="Occupant") bool bLeftHandTracked = false;
    UPROPERTY(BlueprintReadOnly, Category="Occupant") bool bRightHandTracked = false;
    UFUNCTION(BlueprintCallable, Category="Occupant") void SetSeat(int32 Index);
    UFUNCTION(BlueprintCallable, Category="Occupant") void Blink();
    UFUNCTION(BlueprintCallable, Category="Occupant") void SetLook(float Yaw, float Pitch);
    UFUNCTION(BlueprintCallable, Category="Occupant") void CalibrateXR();
    UFUNCTION(BlueprintCallable, Category="Occupant") bool ActivateVR();
    UFUNCTION(BlueprintCallable, Category="Occupant") void SetHandTarget(bool bLeft, FVector Position, FRotator Rotation);
private:
    UPROPERTY() TObjectPtr<USceneComponent> TrackingOrigin;
    UPROPERTY() TObjectPtr<UMotionControllerComponent> LeftController;
    UPROPERTY() TObjectPtr<UMotionControllerComponent> RightController;
    UPROPERTY() TObjectPtr<UCameraComponent> VRCamera;
    TMap<FName, FTransform> Rest;
    FVector HandPositions[2];
    FRotator HandRotations[2];
    bool HandActive[2] = {false, false};
    float BlinkElapsed = 1.f;
    float NextBlink = 3.f;
    float Age = 0.f;
    void SolveArm(bool bLeft);
    void AimBone(FName Bone, FVector Start, FVector End, FName Child);
};
