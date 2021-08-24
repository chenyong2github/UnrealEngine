// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlacementPaletteAsset.h"

UPlacementPaletteAssetFactory::UPlacementPaletteAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UPlacementPaletteAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UPlacementPaletteAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UPlacementPaletteAsset>(InParent, InClass, InName, Flags, Context);
}

bool UPlacementPaletteAssetFactory::ShouldShowInNewMenu() const
{
	return true;
}
