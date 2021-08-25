// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

#include "OptimusDeformer.h"

class FOptimusDeformerAssetActions
	: public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions overrides
	FText GetName() const override 
	{ 
		return NSLOCTEXT("AssetTypeActions", "OptimusDeformerActions", "Deformer Graph"); 
	}

	FColor GetTypeColor() const override 
	{ 
		return FColor::Blue;
	}

	UClass* GetSupportedClass() const override 
	{ 
		return UOptimusDeformer::StaticClass(); 
	}

	void OpenAssetEditor(
		const TArray<UObject*>& InObjects, 
		TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()
	) override;

	uint32 GetCategories() override 
	{ 
		return EAssetTypeCategories::Misc;
	}

	TSharedPtr<SWidget> GetThumbnailOverlay(
		const FAssetData& AssetData
	) const override;

};
