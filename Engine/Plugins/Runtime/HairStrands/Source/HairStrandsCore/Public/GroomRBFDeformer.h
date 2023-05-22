// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UGroomAsset;
class UGroomBindingAsset;
struct FTextureSource;

struct HAIRSTRANDSCORE_API FGroomRBFDeformer
{
	// Return a new GroomAsset with the RBF deformation from the BindingAsset baked into it
	void GetRBFDeformedGroomAsset(const UGroomAsset* InGroomAsset, const UGroomBindingAsset* BindingAsset, FTextureSource* MaskTextureSource, const float MaskScale, UGroomAsset* OutGroomAsset);
};