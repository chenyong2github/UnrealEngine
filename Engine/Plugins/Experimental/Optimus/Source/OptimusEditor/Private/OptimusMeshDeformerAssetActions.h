// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"
#include "OptimusMeshDeformer.h"

class FOptimusMeshDeformerAssetActions
	: public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions overrides
	FText GetName() const override 
	{ 
		return NSLOCTEXT("AssetTypeActions", "OptimusMeshDeformerActions", "Mesh Deformer"); 
	}

	FColor GetTypeColor() const override 
	{ 
		return FColor::Blue;
	}

	UClass* GetSupportedClass() const override 
	{ 
		return UOptimusMeshDeformer::StaticClass(); 
	}

	uint32 GetCategories() override 
	{ 
		return EAssetTypeCategories::Misc;
	}
};
