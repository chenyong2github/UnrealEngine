// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyActorFactory.h"
#include "WaterBodyActor.h"
#include "WaterEditorSettings.h"
#include "WaterBodyRiverActor.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyLakeActor.h"
#include "WaterBodyCustomActor.h"
#include "WaterSplineComponent.h"
#include "WaterWaves.h"

#define LOCTEXT_NAMESPACE "WaterBodyActorFactory"

// --------------------------------------------------
// WaterBody Factory
// --------------------------------------------------
UWaterBodyActorFactory::UWaterBodyActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseSurfaceOrientation = true;
}

void UWaterBodyActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	AWaterBody* WaterBody = CastChecked<AWaterBody>(NewActor);

	if (const FWaterBrushActorDefaults* WaterBrushActorDefaults = GetWaterBrushActorDefaults())
	{
		WaterBody->CurveSettings = WaterBrushActorDefaults->CurveSettings;
		WaterBody->WaterHeightmapSettings = WaterBrushActorDefaults->HeightmapSettings;
		WaterBody->LayerWeightmapSettings = WaterBrushActorDefaults->LayerWeightmapSettings;
	}

	if (const FWaterBodyDefaults* WaterBodyDefaults = GetWaterBodyDefaults())
	{
		WaterBody->SetWaterMaterial(WaterBodyDefaults->GetWaterMaterial());
		WaterBody->SetUnderwaterPostProcessMaterial(WaterBodyDefaults->GetUnderwaterPostProcessMaterial());

		UWaterSplineComponent* WaterSpline = WaterBody->GetWaterSpline();
		if (ShouldOverrideWaterSplineDefaults(WaterSpline))
		{
			WaterSpline->WaterSplineDefaults = WaterBodyDefaults->SplineDefaults;
		}
	}
}

// If WaterSpline's owning actor class is a BP class, don't allow to override WaterSplineDefaults
bool UWaterBodyActorFactory::ShouldOverrideWaterSplineDefaults(const UWaterSplineComponent* WaterSpline) const
{
	check(WaterSpline);
	AWaterBody* OwningBody = WaterSpline->GetTypedOuter<AWaterBody>();
	return OwningBody && OwningBody->GetClass()->ClassGeneratedBy == nullptr;
}

// --------------------------------------------------
// WaterBodyRiver Factory
// --------------------------------------------------
UWaterBodyRiverActorFactory::UWaterBodyRiverActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("WaterBodyRiverActorDisplayName", "Water Body River");
	NewActorClass = AWaterBodyRiver::StaticClass();
}

const FWaterBodyDefaults* UWaterBodyRiverActorFactory::GetWaterBodyDefaults() const
{
	return &GetDefault<UWaterEditorSettings>()->WaterBodyRiverDefaults;
}

const FWaterBrushActorDefaults* UWaterBodyRiverActorFactory::GetWaterBrushActorDefaults() const
{
	return &GetDefault<UWaterEditorSettings>()->WaterBodyRiverDefaults.BrushDefaults;
}

void UWaterBodyRiverActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);
	
	AWaterBodyRiver* WaterBodyRiver = CastChecked<AWaterBodyRiver>(NewActor);
	WaterBodyRiver->SetLakeTransitionMaterial(GetDefault<UWaterEditorSettings>()->WaterBodyRiverDefaults.GetRiverToLakeTransitionMaterial());
	WaterBodyRiver->SetOceanTransitionMaterial(GetDefault<UWaterEditorSettings>()->WaterBodyRiverDefaults.GetRiverToOceanTransitionMaterial());

	UWaterSplineComponent* WaterSpline = WaterBodyRiver->GetWaterSpline();
	WaterSpline->ResetSpline({ FVector(0, 0, 0), FVector(5000, 0, 0), FVector(10000, 5000, 0) });
}

// --------------------------------------------------
// WaterBodyOcean Factory
// --------------------------------------------------
UWaterBodyOceanActorFactory::UWaterBodyOceanActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("WaterBodyOceanActorDisplayName", "Water Body Ocean");
	NewActorClass = AWaterBodyOcean::StaticClass();
}

const FWaterBodyDefaults* UWaterBodyOceanActorFactory::GetWaterBodyDefaults() const
{
	return &GetDefault<UWaterEditorSettings>()->WaterBodyOceanDefaults;
}

const FWaterBrushActorDefaults* UWaterBodyOceanActorFactory::GetWaterBrushActorDefaults() const
{
	return &GetDefault<UWaterEditorSettings>()->WaterBodyOceanDefaults.BrushDefaults;
}

void UWaterBodyOceanActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	AWaterBodyOcean* WaterBodyOcean = CastChecked<AWaterBodyOcean>(NewActor);
	if (const UWaterWavesBase* DefaultWaterWaves = GetDefault<UWaterEditorSettings>()->WaterBodyOceanDefaults.WaterWaves)
	{
		UWaterWavesBase* WaterWaves = DuplicateObject(DefaultWaterWaves, NewActor, MakeUniqueObjectName(NewActor, DefaultWaterWaves->GetClass(), TEXT("OceanWaterWaves")));
		WaterBodyOcean->SetWaterWaves(WaterWaves);
	}

	UWaterSplineComponent* WaterSpline = WaterBodyOcean->GetWaterSpline();
	WaterSpline->ResetSpline({ FVector(10000, -10000, 0), FVector(10000,  10000, 0), FVector(-10000,  10000, 0), FVector(-10000, -10000, 0) });
}

// --------------------------------------------------
// WaterBodyLake Factory
// --------------------------------------------------
UWaterBodyLakeActorFactory::UWaterBodyLakeActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("WaterBodyLakeActorDisplayName", "Water Body Lake");
	NewActorClass = AWaterBodyLake::StaticClass();
}

const FWaterBodyDefaults* UWaterBodyLakeActorFactory::GetWaterBodyDefaults() const
{
	return &GetDefault<UWaterEditorSettings>()->WaterBodyLakeDefaults;
}

const FWaterBrushActorDefaults* UWaterBodyLakeActorFactory::GetWaterBrushActorDefaults() const
{
	return &GetDefault<UWaterEditorSettings>()->WaterBodyLakeDefaults.BrushDefaults;
}

void UWaterBodyLakeActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	AWaterBodyLake* WaterBodyLake = CastChecked<AWaterBodyLake>(NewActor);
	if (const UWaterWavesBase* DefaultWaterWaves = GetDefault<UWaterEditorSettings>()->WaterBodyLakeDefaults.WaterWaves)
	{
		UWaterWavesBase* WaterWaves = DuplicateObject(DefaultWaterWaves, NewActor, MakeUniqueObjectName(NewActor, DefaultWaterWaves->GetClass(), TEXT("LakeWaterWaves")));
		WaterBodyLake->SetWaterWaves(WaterWaves);
	}

	UWaterSplineComponent* WaterSpline = WaterBodyLake->GetWaterSpline();
	WaterSpline->ResetSpline({ FVector(0, 0, 0), FVector(7000, -3000, 0),  FVector(6500, 6500, 0) });
}

// --------------------------------------------------
// WaterBodyCustom Factory
// --------------------------------------------------
UWaterBodyCustomActorFactory::UWaterBodyCustomActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("WaterBodyCustomActorDisplayName", "Water Body Custom");
	NewActorClass = AWaterBodyCustom::StaticClass();
}

const FWaterBodyDefaults* UWaterBodyCustomActorFactory::GetWaterBodyDefaults() const
{
	return &GetDefault<UWaterEditorSettings>()->WaterBodyCustomDefaults;
}

void UWaterBodyCustomActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	AWaterBodyCustom* WaterBodyCustom = CastChecked<AWaterBodyCustom>(NewActor);
	WaterBodyCustom->SetWaterMeshOverride(GetDefault<UWaterEditorSettings>()->WaterBodyCustomDefaults.GetWaterMesh());

	UWaterSplineComponent* WaterSpline = WaterBodyCustom->GetWaterSpline();
	WaterSpline->ResetSpline({ FVector(0, 0, 0) });
}

#undef LOCTEXT_NAMESPACE