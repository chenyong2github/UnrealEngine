// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"


// Forward Declarations
class IToolkitHost;


class FAssetTypeActions_Metasound : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_Metasound", "Metasound"); }
	virtual FColor GetTypeColor() const override { return FColor(220, 160, 0); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost) override;

	virtual const TArray<FText>& GetSubMenus() const override;
};
