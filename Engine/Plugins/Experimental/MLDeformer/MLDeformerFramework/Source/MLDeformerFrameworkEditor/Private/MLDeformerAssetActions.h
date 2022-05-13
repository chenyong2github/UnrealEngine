// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

namespace UE::MLDeformer
{
	class FMLDeformerAssetActions
		: public FAssetTypeActions_Base
	{
	public:
		// IAssetTypeActions overrides.
		virtual FText GetName() const override { return NSLOCTEXT("MLDeformerAssetActions", "Name", "ML Deformer"); }
		virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
		virtual FColor GetTypeColor() const override { return FColor(255, 255, 0); }
		virtual UClass* GetSupportedClass() const override;
		virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
		virtual uint32 GetCategories() override { return EAssetTypeCategories::Animation; }
		virtual const TArray<FText>& GetSubMenus() const override;
		// ~END IAssetTypeActions overrides.
	};
}	// namespace UE::MLDeformer
