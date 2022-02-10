// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"


// Forward Declarations
class IToolkitHost;

namespace Metasound
{
	namespace Editor
	{
		class FAssetTypeActions_MetaSound : public FAssetTypeActions_Base
		{
		public:
			// IAssetTypeActions Implementation
			virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_MetaSound", "MetaSound"); }
			virtual FColor GetTypeColor() const override { return FColor(13, 55, 13); }
			virtual UClass* GetSupportedClass() const override;
			virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
			virtual const TArray<FText>& GetSubMenus() const override;
			virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost) override;

			static void RegisterMenuActions();
		};

		class FAssetTypeActions_MetaSoundSource : public FAssetTypeActions_Base
		{
		public:
			// IAssetTypeActions Implementation
			virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_MetaSoundSource", "MetaSound Source"); }
			virtual FColor GetTypeColor() const override { return FColor(103, 214, 66); }
			virtual UClass* GetSupportedClass() const override;
			virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
			virtual const TArray<FText>& GetSubMenus() const override;
			virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost) override;

			static void RegisterMenuActions();
		};
	} // namespace Editor
} // namespace Metasound
