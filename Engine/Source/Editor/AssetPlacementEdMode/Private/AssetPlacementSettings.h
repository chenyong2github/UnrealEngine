// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

#include "AssetPlacementSettings.generated.h"

class IAssetFactoryInterface;

USTRUCT()
struct FPaletteItem
{
	GENERATED_BODY()

	UPROPERTY()
	FAssetData AssetData;

	UPROPERTY()
	TScriptInterface<IAssetFactoryInterface> FactoryOverride;

	UPROPERTY()
	bool bIsEnabled = true;
};

UCLASS(config = EditorPerProjectUserSettings)
class UAssetPlacementSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bLandscape = true;

	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bStaticMeshes = true;

	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bBSP = true;

	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bFoliage = false;

	UPROPERTY(config, EditAnywhere, Category = "Filters")
	bool bTranslucent = false;

	UPROPERTY(config, EditAnywhere, Category = "Placement")
	bool bAllowRandomRotation = true;

	UPROPERTY(config, EditAnywhere, Category = "Placement")
	bool bAllowAlignToNormal = true;

	UPROPERTY(config, EditAnywhere, Category = "Placement")
	bool bAllowRandomScale = true;

	// todo: asset data does not serialize out correctly
	// maybe save soft object pointers, and convert in the UI to asset data?
	//UPROPERTY(config)
	TArray<FPaletteItem> PaletteItems;
};
