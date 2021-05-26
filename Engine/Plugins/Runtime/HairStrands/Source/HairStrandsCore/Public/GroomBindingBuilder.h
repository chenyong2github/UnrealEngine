// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GroomSettings.h"
#include "HairStrandsInterface.h"
#include "HairStrandsMeshProjection.h"
#include "HairStrandsDatas.h"

struct FHairStrandsRestRootResource;

struct HAIRSTRANDSCORE_API FGroomBindingBuilder
{
	static FString GetVersion();

	// Build binding asset data
	static bool BuildBinding(class UGroomBindingAsset* BindingAsset, bool bInitResources);

	// Update the root mesh projection data with unique valid mesh section IDs, based on the projection data
	static void BuildUniqueSections(FHairStrandsRootData::FMeshProjectionLOD& LOD);

	// Extract root data from bulk data
	static void GetRootData(FHairStrandsRootData& Out, const FHairStrandsRootBulkData& In);
};