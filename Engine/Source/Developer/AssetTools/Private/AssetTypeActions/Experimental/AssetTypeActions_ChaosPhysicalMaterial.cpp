// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/Experimental/AssetTypeActions_ChaosPhysicalMaterial.h"
#include "PhysicalMaterials/Experimental/ChaosPhysicalMaterial.h"

UClass* FAssetTypeActions_ChaosPhysicalMaterial::GetSupportedClass() const
{
	return UChaosPhysicalMaterial::StaticClass();
}
