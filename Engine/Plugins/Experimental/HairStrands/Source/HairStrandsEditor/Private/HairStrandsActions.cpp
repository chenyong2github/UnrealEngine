// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsActions.h"
#include "HairStrandsAsset.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FHairStrandsActions::FHairStrandsActions()
{}

bool FHairStrandsActions::CanFilter()
{
	return true;
}

void FHairStrandsActions::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}


uint32 FHairStrandsActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

FText FHairStrandsActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_HairStrands", "Hair Strands");
}

UClass* FHairStrandsActions::GetSupportedClass() const
{
	return UHairStrandsAsset::StaticClass();
}

FColor FHairStrandsActions::GetTypeColor() const
{
	return FColor::White;
}

bool FHairStrandsActions::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

void FHairStrandsActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
}

#undef LOCTEXT_NAMESPACE