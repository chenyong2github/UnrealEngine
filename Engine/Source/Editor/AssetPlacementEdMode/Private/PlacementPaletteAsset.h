// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Factories/Factory.h"
#include "PlacementPaletteItem.h"

#include "PlacementPaletteAsset.generated.h"

class UPlacementPaletteAsset;

UCLASS(NotPlaceable, config = EditorPerProjectUserSettings)
class UPlacementPaletteAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category=Items)
	TArray<FPaletteItem> PaletteItems;
};

UCLASS(hideCategories=Object)
class UPlacementPaletteAssetFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

public:
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
};
