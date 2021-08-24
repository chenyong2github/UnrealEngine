// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PlacementPaletteItem.generated.h"

class IAssetFactoryInterface;

USTRUCT()
struct FPaletteItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Placement)
	FSoftObjectPath AssetPath;

	UPROPERTY(EditAnywhere, Category = Placement, AdvancedDisplay)
	TScriptInterface<IAssetFactoryInterface> AssetFactoryInterface;

	UPROPERTY()
	FGuid ItemGuid;
};
