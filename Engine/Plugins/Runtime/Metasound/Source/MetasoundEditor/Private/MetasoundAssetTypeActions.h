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
		class FAssetTypeActions_Metasound : public FAssetTypeActions_Base
		{
		public:
			// IAssetTypeActions Implementation
			virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_MetaSound", "MetaSound"); }
			virtual FColor GetTypeColor() const override { return FColor(103, 214, 66); }
			virtual UClass* GetSupportedClass() const override;
			virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
			virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost) override;

			virtual const TArray<FText>& GetSubMenus() const override;
		};

		class FAssetTypeActions_MetasoundSource : public FAssetTypeActions_Base
		{
		public:
			// IAssetTypeActions Implementation
			virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_MetaSoundSource", "MetaSoundSource"); }
			virtual FColor GetTypeColor() const override { return FColor(103, 214, 66); }
			virtual UClass* GetSupportedClass() const override;
			virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
			virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost) override;

			virtual const TArray<FText>& GetSubMenus() const override;
		};
	} // namespace Editor
} // namespace Metasound
