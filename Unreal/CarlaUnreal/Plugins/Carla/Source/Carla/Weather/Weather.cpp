// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla/Weather/Weather.h"
#include "Carla/Weather/Sky.h"
#include "Carla.h"
#include "Carla/Game/CarlaStatics.h"
#include "Carla/Recorder/CarlaRecorder.h"
#include "Carla/Recorder/CarlaRecorderWeather.h"
#include "Carla/Sensor/SceneCaptureCamera.h"

#include <util/ue-header-guard-begin.h>
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureCube.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Curves/CurveFloat.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"
#include <util/ue-header-guard-end.h>

namespace
{
// UE5 maps keep their lighting components on a separately loaded sky actor.
// A weather controller spawned by the game mode can precede that actor. Resolve
// the authored blueprint reference when applying weather, after map loading.
void ResolveWeatherSky(AWeather* WeatherActor)
{
    FObjectPropertyBase* SkyProperty = FindFProperty<FObjectPropertyBase>(
        WeatherActor->GetClass(), TEXT("BP_CarlaSky"));
    if (SkyProperty && SkyProperty->PropertyClass->IsChildOf(AActor::StaticClass()) &&
        !IsValid(SkyProperty->GetObjectPropertyValue_InContainer(WeatherActor)))
    {
        AActor* Sky = UGameplayStatics::GetActorOfClass(
            WeatherActor->GetWorld(), SkyProperty->PropertyClass);
        if (Sky)
        {
            SkyProperty->SetObjectPropertyValue_InContainer(WeatherActor, Sky);
            UE_LOG(LogCarla, Log, TEXT("Weather controller linked to loaded sky %s"), *Sky->GetName());
        }
    }
}

void ApplyLoadedSkyWeather(AWeather* WeatherActor, const FWeatherParameters& Parameters)
{
    TArray<AActor*> Skies;
    UGameplayStatics::GetAllActorsOfClass(WeatherActor->GetWorld(), ASkyBase::StaticClass(), Skies);
    const auto CurveValue = [WeatherActor](const TCHAR* PropertyName, float Input, float Fallback)
    {
        const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(WeatherActor->GetClass(), PropertyName);
        const UCurveFloat* Curve = Property ? Cast<UCurveFloat>(Property->GetObjectPropertyValue_InContainer(WeatherActor)) : nullptr;
        return Curve ? Curve->GetFloatValue(Input) : Fallback;
    };
    // The UE5 port's legacy weather graph updates material parameters but does
    // not drive the new sky's component lights. Use the supplied weather curves
    // and update the actual movable components before sensor rendering.
    const bool bDay = Parameters.SunAltitudeAngle >= 0.f;
    const float SunIntensity = bDay ? CurveValue(TEXT("SunIntensity_Curve"), Parameters.SunAltitudeAngle, 100000.f) : 0.f;
    const float PeakSunIntensity = FMath::Max(1.f, CurveValue(TEXT("SunIntensity_Curve"), 75.f, 100000.f));
    // Preserve each map's authored light scale; UE4 weather curves use lux,
    // while this UE5 map was authored with a much lower cinematic intensity.
    static TMap<TWeakObjectPtr<UDirectionalLightComponent>, float> OriginalSunIntensities;
    static TMap<TWeakObjectPtr<UDirectionalLightComponent>, FLightingChannels> OriginalSunChannels;
    for (auto It = OriginalSunIntensities.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) It.RemoveCurrent();
    for (auto It = OriginalSunChannels.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) It.RemoveCurrent();
    const float SkyIntensity = CurveValue(TEXT("SkyIntensity_Curve"), Parameters.SunAltitudeAngle, bDay ? 1.f : 0.f);
    // Keep the atmospheric sun illuminated. Clouds shadow surfaces through
    // the volumetric cloud shadow map, rather than extinguishing the light
    // that also supplies daytime sky scattering.
    const float SunIntensityScale = 1.f - .008f * Parameters.Cloudiness;
    for (AActor* Sky : Skies)
    {
        TArray<UDirectionalLightComponent*> Lights;
        Sky->GetComponents(Lights);
        for (UDirectionalLightComponent* Light : Lights)
        {
            const bool bSun = Light->GetName().Contains(TEXT("Sun"));
            Light->SetWorldRotation(FRotator(bSun ? -Parameters.SunAltitudeAngle : Parameters.SunAltitudeAngle,
                Parameters.SunAzimuthAngle + (bSun ? 0.f : 180.f), 0.f));
            float MapSunIntensity = 0.f;
            if (bSun)
            {
                const TWeakObjectPtr<UDirectionalLightComponent> Key(Light);
                if (!OriginalSunIntensities.Contains(Key)) OriginalSunIntensities.Add(Key, Light->Intensity);
                if (!OriginalSunChannels.Contains(Key)) OriginalSunChannels.Add(Key, Light->LightingChannels);
                MapSunIntensity = OriginalSunIntensities[Key] * SunIntensity / PeakSunIntensity;
                // Dense overcast is a diffuse-light condition. Exclude its
                // direct sun from surface channels, while retaining the same
                // atmospheric light for daytime sky/cloud scattering. The
                // authored channel mask is restored as the clouds clear.
                const bool bDirectSurfaceSun = Parameters.Cloudiness < 90.f;
                const auto& Channels = OriginalSunChannels[Key];
                Light->SetLightingChannels(bDirectSurfaceSun && Channels.bChannel0,
                    bDirectSurfaceSun && Channels.bChannel1, bDirectSurfaceSun && Channels.bChannel2);
            }
            if (bSun)
            {
                Light->bCastCloudShadows = Parameters.Cloudiness > 0.f;
                Light->CloudShadowStrength = 1.f;
                Light->CloudShadowOnSurfaceStrength = 1.f;
                Light->CloudShadowExtent = 5.f; // kilometres; covers the local road scene
                Light->MarkRenderStateDirty();
            }
            Light->SetIntensity(bSun ? MapSunIntensity * SunIntensityScale : (bDay ? 0.f : .05f));
        }
        if (USkyLightComponent* Light = Sky->FindComponentByClass<USkyLightComponent>())
            Light->SetIntensity(SkyIntensity);
        if (UExponentialHeightFogComponent* Fog = Sky->FindComponentByClass<UExponentialHeightFogComponent>())
        {
            Fog->SetFogDensity(Parameters.FogDensity * .001f);
            Fog->SetFogHeightFalloff(Parameters.FogFalloff);
            Fog->SetStartDistance(Parameters.FogDistance * 100.f); // API metres -> UE centimetres.
        }
        if (UVolumetricCloudComponent* Cloud = Sky->FindComponentByClass<UVolumetricCloudComponent>())
        {
            Cloud->SetVisibility(Parameters.Cloudiness > 0.f);
            UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Cloud->GetMaterial());
            if (!Dynamic && Cloud->GetMaterial())
            {
                Dynamic = UMaterialInstanceDynamic::Create(Cloud->GetMaterial(), Cloud);
                Cloud->SetMaterial(Dynamic);
            }
            if (Dynamic && Dynamic->Parent)
            {
                float Extinction = 1.f;
                Dynamic->Parent->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ExtinctionScale")), Extinction);
                Dynamic->SetScalarParameterValue(TEXT("ExtinctionScale"), Extinction * Parameters.Cloudiness / 100.f);
            }
        }
        if (USkyLightComponent* Light = Sky->FindComponentByClass<USkyLightComponent>())
        {
            // Sensor-only render workers have no regular viewport. UE skips
            // realtime skylight capture for scene-capture views, leaving their
            // diffuse environment black. Capture explicitly on weather changes
            // instead of paying for an otherwise unused viewport every frame.
            UE_LOG(LogCarla, Log, TEXT("Weather skylight: name=%s visible=%d affects_world=%d realtime=%d source=%d cubemap=%s intensity=%.3f"),
                *Light->GetName(), Light->GetVisibleFlag(), Light->bAffectsWorld,
                Light->bRealTimeCapture, int32(Light->SourceType), *GetNameSafe(Light->Cubemap.Get()), Light->Intensity);
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetVisibility(true);
            Light->bAffectsWorld = true;
            Light->SourceType = SLS_CapturedScene;
            Light->SetRealTimeCapture(false);
            Light->RecaptureSky();
        }
        UE_LOG(LogCarla, Log, TEXT("Sky components updated: altitude=%.1f azimuth=%.1f sun_curve=%.1f sky=%.2f cloud=%.1f fog=%.1f sun_intensity_scale=%.3f direct_surface_sun=%d"),
            Parameters.SunAltitudeAngle, Parameters.SunAzimuthAngle, SunIntensity, SkyIntensity, Parameters.Cloudiness, Parameters.FogDensity, SunIntensityScale, Parameters.Cloudiness < 90.f);
    }
}

}

AWeather::AWeather(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrecipitationPostProcessMaterial = ConstructorHelpers::FObjectFinder<UMaterial>(
        TEXT("Material'/Game/Carla/Static/GenericMaterials/FX/ScreenDust/M_screenDrops.M_screenDrops'")).Object;

    DustStormPostProcessMaterial = ConstructorHelpers::FObjectFinder<UMaterial>(
        TEXT("Material'/Game/Carla/Static/GenericMaterials/FX/ScreenDust/M_screenDust_wind.M_screenDust_wind'")).Object;

    PrimaryActorTick.bCanEverTick = false;
    RootComponent = ObjectInitializer.CreateDefaultSubobject<USceneComponent>(this, TEXT("RootComponent"));
}

void AWeather::CheckWeatherPostProcessEffects()
{
    if (Weather.Precipitation > 0.0f)
        ActiveBlendables.Add(MakeTuple(PrecipitationPostProcessMaterial, Weather.Precipitation / 100.0f));
    else
        ActiveBlendables.Remove(PrecipitationPostProcessMaterial);

    if (Weather.DustStorm > 0.0f)
        ActiveBlendables.Add(MakeTuple(DustStormPostProcessMaterial, Weather.DustStorm / 100.0f));
    else
        ActiveBlendables.Remove(DustStormPostProcessMaterial);

    TArray<AActor*> SensorActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASceneCaptureCamera::StaticClass(), SensorActors);
    for (AActor* SensorActor : SensorActors)
    {
        ASceneCaptureCamera* Sensor = Cast<ASceneCaptureCamera>(SensorActor);
        // Removing a material from ActiveBlendables does not remove the copy
        // already installed on a camera. Replace only weather-owned effects
        // so rain/dust transitions to zero also reach existing sensor views.
        auto& Settings = Sensor->GetCaptureComponent2D()->PostProcessSettings;
        Settings.RemoveBlendable(PrecipitationPostProcessMaterial);
        Settings.RemoveBlendable(DustStormPostProcessMaterial);
        for (auto& ActiveBlendable : ActiveBlendables)
            Settings.AddBlendable(ActiveBlendable.Key, ActiveBlendable.Value);
    }
}

void AWeather::ApplyWeather(const FWeatherParameters& InWeather)
{
    SetWeather(InWeather);
    CheckWeatherPostProcessEffects();

#ifdef CARLA_WEATHER_EXTRA_LOG
    UE_LOG(LogCarla, Log, TEXT("Changing weather:"));
    UE_LOG(LogCarla, Log, TEXT("  - Cloudiness = %.2f"), Weather.Cloudiness);
    UE_LOG(LogCarla, Log, TEXT("  - Precipitation = %.2f"), Weather.Precipitation);
    UE_LOG(LogCarla, Log, TEXT("  - PrecipitationDeposits = %.2f"), Weather.PrecipitationDeposits);
    UE_LOG(LogCarla, Log, TEXT("  - WindIntensity = %.2f"), Weather.WindIntensity);
    UE_LOG(LogCarla, Log, TEXT("  - SunAzimuthAngle = %.2f"), Weather.SunAzimuthAngle);
    UE_LOG(LogCarla, Log, TEXT("  - SunAltitudeAngle = %.2f"), Weather.SunAltitudeAngle);
    UE_LOG(LogCarla, Log, TEXT("  - FogDensity = %.2f"), Weather.FogDensity);
    UE_LOG(LogCarla, Log, TEXT("  - FogDistance = %.2f"), Weather.FogDistance);
    UE_LOG(LogCarla, Log, TEXT("  - FogFalloff = %.2f"), Weather.FogFalloff);
    UE_LOG(LogCarla, Log, TEXT("  - Wetness = %.2f"), Weather.Wetness);
    UE_LOG(LogCarla, Log, TEXT("  - ScatteringIntensity = %.2f"), Weather.ScatteringIntensity);
    UE_LOG(LogCarla, Log, TEXT("  - MieScatteringScale = %.2f"), Weather.MieScatteringScale);
    UE_LOG(LogCarla, Log, TEXT("  - RayleighScatteringScale = %.2f"), Weather.RayleighScatteringScale);
    UE_LOG(LogCarla, Log, TEXT("  - DustStorm = %.2f"), Weather.DustStorm);
#endif // CARLA_WEATHER_EXTRA_LOG

    // Call the blueprint that actually changes the weather.
    ResolveWeatherSky(this);
    RefreshWeather(Weather);
    ApplyLoadedSkyWeather(this, Weather);

    // record the weather event
    ACarlaRecorder *Recorder = UCarlaStatics::GetRecorder(GetWorld());
    if (Recorder && Recorder->IsEnabled())
    {
        CarlaRecorderWeather RecorderWeather;
        RecorderWeather.Cloudiness              = InWeather.Cloudiness;
        RecorderWeather.Precipitation           = InWeather.Precipitation;
        RecorderWeather.PrecipitationDeposits   = InWeather.PrecipitationDeposits;
        RecorderWeather.WindIntensity           = InWeather.WindIntensity;
        RecorderWeather.SunAzimuthAngle         = InWeather.SunAzimuthAngle;
        RecorderWeather.SunAltitudeAngle        = InWeather.SunAltitudeAngle;
        RecorderWeather.FogDensity              = InWeather.FogDensity;
        RecorderWeather.FogDistance             = InWeather.FogDistance;
        RecorderWeather.FogFalloff              = InWeather.FogFalloff;
        RecorderWeather.Wetness                 = InWeather.Wetness;
        RecorderWeather.ScatteringIntensity     = InWeather.ScatteringIntensity;
        RecorderWeather.MieScatteringScale      = InWeather.MieScatteringScale;
        RecorderWeather.RayleighScatteringScale = InWeather.RayleighScatteringScale;
        RecorderWeather.DustStorm               = InWeather.DustStorm;
        Recorder->AddWeather(RecorderWeather);
    }
}

void AWeather::NotifyWeather(ASensor* Sensor)
{
    CheckWeatherPostProcessEffects();

    // Call the blueprint that actually changes the weather.
    ResolveWeatherSky(this);
    RefreshWeather(Weather);
    ApplyLoadedSkyWeather(this, Weather);
}

void AWeather::SetWeather(const FWeatherParameters& InWeather)
{
    Weather = InWeather;
}

void AWeather::SetDayNightCycle(const bool& active)
{
    DayNightCycle = active;
}

#if WITH_EDITOR
void AWeather::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    ApplyWeather(Weather);
}
#endif
