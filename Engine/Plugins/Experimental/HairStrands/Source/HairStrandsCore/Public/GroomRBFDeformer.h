// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UGroomAsset;

struct HAIRSTRANDSCORE_API FGroomRBFDeformer
{
	// Return a new GroomAsset with the RBF deformation from the BindingAsset baked into it
	static UGroomAsset* GetRBFDeformedGroomAsset(const UGroomAsset* GroomAsset, const class UGroomBindingAsset* BindingAsset, const FVector& DeformationOffset);
};