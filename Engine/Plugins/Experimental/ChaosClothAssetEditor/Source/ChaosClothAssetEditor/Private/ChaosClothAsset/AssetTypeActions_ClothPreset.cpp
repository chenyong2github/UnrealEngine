// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_ClothPreset.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "ToolMenus.h"
#include "ChaosClothAsset/ClothPreset.h"

namespace UE::Chaos::ClothAsset
{
	FText FAssetTypeActions_ClothPreset::GetName() const
	{
		return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_ClothPreset", "Cloth Preset");
	}

	void FAssetTypeActions_ClothPreset::GetActions(const TArray<UObject*>& Objects, FToolMenuSection& Section)
	{
		FAssetTypeActions_Base::GetActions(Objects, Section);
	}

	FColor FAssetTypeActions_ClothPreset::GetTypeColor() const
	{
		return FColor(180, 120, 110);
	}

	UClass* FAssetTypeActions_ClothPreset::GetSupportedClass() const
	{
		return UChaosClothPreset::StaticClass();
	}

	void FAssetTypeActions_ClothPreset::OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
	{
		// TODO: open the cloth editor
	}

	uint32 FAssetTypeActions_ClothPreset::GetCategories()
	{
		return EAssetTypeCategories::Physics;
	}

	UThumbnailInfo* FAssetTypeActions_ClothPreset::GetThumbnailInfo(UObject* Asset) const
	{
		check(Cast<UChaosClothPreset>(Asset));
		return NewObject<USceneThumbnailInfo>(Asset, NAME_None, RF_Transactional);
	}
} // End namespace UE::Chaos::ClothAsset
