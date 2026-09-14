// Copyright (c) 2026. Licensed under the MIT license.
#pragma once
#include "CoreMinimal.h"
#include "Carla/Sensor/LidarOpticsModel.h"
#include "Engine/HitResult.h"
class UPrimitiveComponent;
class UMaterialInterface;
namespace CarlaLidarInfrared {
// Explicit spectral coefficients and linear near-IR masks, independent of RGB.
void Prepare();
int32 Find(UPrimitiveComponent* Component,UMaterialInterface* Material);
CarlaLidarOptics::Material Sample(CarlaLidarOptics::Material Base,const FHitResult& Hit,
    float WavelengthNm,float Wetness,const FVector2f* MotionUV=nullptr);
// Append six float4 metadata records per render section, plus only the UVs
// required by authored masks. Triangle indices are relative to each section.
void Append(UPrimitiveComponent* Component,uint32 Section,int32 Profile,
    TArray<FVector4f>& Info,TArray<FVector4f>& UVs);
const TArray<FVector4f>& Pixels();
}
