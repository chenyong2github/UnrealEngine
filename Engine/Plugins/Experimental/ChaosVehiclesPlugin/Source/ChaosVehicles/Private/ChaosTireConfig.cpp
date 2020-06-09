// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosTireConfig.h"
#include "EngineDefines.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#include "ChaosVehicleManager.h"

TArray<TWeakObjectPtr<UChaosTireConfig>> UChaosTireConfig::AllTireConfigs;

UChaosTireConfig::UChaosTireConfig()
{
	// Property initialization
	FrictionScale = 1.0f;
}

void UChaosTireConfig::SetFrictionScale(float NewFrictionScale)
{
	if (NewFrictionScale != FrictionScale)
	{
		FrictionScale = NewFrictionScale;

		NotifyTireFrictionUpdated();
	}
}

void UChaosTireConfig::SetPerMaterialFrictionScale(UPhysicalMaterial* PhysicalMaterial, float NewFrictionScale)
{
	// See if we already have an entry for this material
	bool bFoundEntry = false;
	for (FTireFrictionPerMaterial MatFriction : TireFrictionScales)
	{
		if (MatFriction.PhysicalMaterial == PhysicalMaterial)
		{
			// We do, update it
			MatFriction.FrictionScale = NewFrictionScale;
			bFoundEntry = true;
			break;
		}
	}

	// We don't have an entry, add one
	if (!bFoundEntry)
	{
		FTireFrictionPerMaterial MatFriction;
		MatFriction.PhysicalMaterial = PhysicalMaterial;
		MatFriction.FrictionScale = NewFrictionScale;
		TireFrictionScales.Add(MatFriction);
	}

	// Update friction table
	NotifyTireFrictionUpdated();
}


void UChaosTireConfig::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Set our TireConfigID - either by finding an available slot or creating a new one
		int32 TireConfigIndex = AllTireConfigs.Find(NULL);

		if (TireConfigIndex == INDEX_NONE)
		{
			TireConfigIndex = AllTireConfigs.Add(this);
		}
		else
		{
			AllTireConfigs[TireConfigIndex] = this;
		}

		TireConfigID = (int32)TireConfigIndex;

		NotifyTireFrictionUpdated();
	}

	Super::PostInitProperties();
}

void UChaosTireConfig::BeginDestroy()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// free our TireTypeID
		check(AllTireConfigs.IsValidIndex(TireConfigID));
		check(AllTireConfigs[TireConfigID] == this);
		AllTireConfigs[TireConfigID] = NULL;

		NotifyTireFrictionUpdated();
	}

	Super::BeginDestroy();
}

void UChaosTireConfig::NotifyTireFrictionUpdated()
{
//	FChaosVehicleManager::UpdateTireFrictionTable();
}

#if WITH_EDITOR
void UChaosTireConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	NotifyTireFrictionUpdated();
}
#endif //WITH_EDITOR

float UChaosTireConfig::GetTireFriction(UPhysicalMaterial* PhysicalMaterial)
{
	// Get friction from tire config
	float Friction = (PhysicalMaterial != nullptr) ? PhysicalMaterial->Friction : 1.f;

	// Scale by tire config scale
	Friction *= FrictionScale;

	// See if we have a material-specific scale as well
	for (FTireFrictionPerMaterial MatFriction : TireFrictionScales)
	{
		if (MatFriction.PhysicalMaterial == PhysicalMaterial)
		{
			Friction *= MatFriction.FrictionScale;
			break;
		}
	}

	return Friction;
}


