// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_ClothAsset.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "ToolMenus.h"
#include "ChaosClothAsset/ClothEditor.h"
#include "ChaosClothAsset/ClothAsset.h"

namespace UE::Chaos::ClothAsset
{
	FText FAssetTypeActions_ClothAsset::GetName() const
	{
		return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_ClothAsset", "Cloth Asset");
	}

	void FAssetTypeActions_ClothAsset::GetActions(const TArray<UObject*>& Objects, FToolMenuSection& Section)
	{
		FAssetTypeActions_Base::GetActions(Objects, Section);
	}

	FColor FAssetTypeActions_ClothAsset::GetTypeColor() const
	{
		return FColor(180, 120, 110);
	}

	UClass* FAssetTypeActions_ClothAsset::GetSupportedClass() const
	{
		return UChaosClothAsset::StaticClass();
	}

	void FAssetTypeActions_ClothAsset::OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
	{
		TArray<TObjectPtr<UObject>> ClothObjects;

		for (UObject* const Object : Objects)
		{
			if (Cast<UChaosClothAsset>(Object))
			{
				ClothObjects.Add(Object);
			}
		}

		// TODO:
		if (ClothObjects.Num() > 0)
		{
			UAssetEditorSubsystem* const AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			UChaosClothAssetEditor* const AssetEditor = NewObject<UChaosClothAssetEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
			AssetEditor->Initialize(ClothObjects);
		}
	}

	uint32 FAssetTypeActions_ClothAsset::GetCategories()
	{
		return EAssetTypeCategories::Physics;
	}

	UThumbnailInfo* FAssetTypeActions_ClothAsset::GetThumbnailInfo(UObject* Asset) const
	{
		check(Cast<UChaosClothAsset>(Asset));
		return NewObject<USceneThumbnailInfo>(Asset, NAME_None, RF_Transactional);
	}
} // End namespace UE::Chaos::ClothAsset
