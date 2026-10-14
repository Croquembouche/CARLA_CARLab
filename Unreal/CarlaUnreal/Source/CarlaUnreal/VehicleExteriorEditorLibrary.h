#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VehicleExteriorEditorLibrary.generated.h"

class UBlueprint;

/** Asset authoring helpers. No runtime vehicle behavior is changed. */
UCLASS()
class CARLAUNREAL_API UVehicleExteriorEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Persist a detail component's socket in the Blueprint construction tree. */
    UFUNCTION(BlueprintCallable, Category="Vehicle Exterior|Editor")
    static bool SetDetailSocket(UBlueprint* Blueprint, FName ComponentName, FName SocketName);
};
