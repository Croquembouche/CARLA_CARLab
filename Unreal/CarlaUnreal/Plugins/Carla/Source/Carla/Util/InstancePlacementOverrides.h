// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include "CoreMinimal.h"
class AActor;

namespace CarlaInstancePlacement
{
// Apply authored, decimal world-space centimetre overrides after construction and
// before environment bounds are registered. Meshes and collisions stay intact.
void Apply(const TArray<AActor*>& Actors);
}
