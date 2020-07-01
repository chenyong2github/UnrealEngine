// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/Experimental/AssetTypeActions_ChaosPhysicalMaterial.h"
#include "Chaos/ChaosPhysicalMaterial.h"

UClass* FAssetTypeActions_ChaosPhysicalMaterial::GetSupportedClass() const
{
	return UChaosPhysicalMaterial::StaticClass();
}
