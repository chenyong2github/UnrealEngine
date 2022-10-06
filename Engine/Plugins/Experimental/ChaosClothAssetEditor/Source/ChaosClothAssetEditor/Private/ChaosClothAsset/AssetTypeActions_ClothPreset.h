// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

namespace UE::Chaos::ClothAsset
{
	class FAssetTypeActions_ClothPreset : public FAssetTypeActions_Base
	{
	public:
		// IAssetTypeActions Implementation
		virtual FText GetName() const override;
		virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
		virtual FColor GetTypeColor() const override;
		virtual UClass* GetSupportedClass() const override;
		virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
		virtual uint32 GetCategories() override;
		virtual UThumbnailInfo* GetThumbnailInfo(UObject* Asset) const override;
	};
}  // End namespace UE::Chaos::ClothAsset
