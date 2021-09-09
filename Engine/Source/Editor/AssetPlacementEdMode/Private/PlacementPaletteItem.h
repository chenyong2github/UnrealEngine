// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PlacementPaletteItem.generated.h"

class IAssetFactoryInterface;
class UEditorFactorySettingsObject;

USTRUCT()
struct FPaletteItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FSoftObjectPath AssetPath;

	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay)
	TScriptInterface<IAssetFactoryInterface> AssetFactoryInterface;

	UPROPERTY()
	FGuid ItemGuid;

	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UEditorFactorySettingsObject> SettingsObject = nullptr;
};
