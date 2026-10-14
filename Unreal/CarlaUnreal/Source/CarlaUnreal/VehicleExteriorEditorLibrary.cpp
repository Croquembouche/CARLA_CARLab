#include "VehicleExteriorEditorLibrary.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#endif

bool UVehicleExteriorEditorLibrary::SetDetailSocket(UBlueprint* Blueprint, FName ComponentName, FName SocketName)
{
#if WITH_EDITOR
    if (!Blueprint || !Blueprint->SimpleConstructionScript || ComponentName.IsNone() || SocketName.IsNone())
    {
        return false;
    }
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName() == ComponentName && Cast<UStaticMeshComponent>(Node->ComponentTemplate))
        {
            Blueprint->Modify();
            Node->Modify();
            Node->AttachToName = SocketName;
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            return Node->AttachToName == SocketName;
        }
    }
#endif
    return false;
}
