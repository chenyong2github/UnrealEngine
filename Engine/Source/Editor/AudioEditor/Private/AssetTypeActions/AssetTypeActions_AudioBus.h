// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"
#include "Sound/AudioBus.h"

class FMenuBuilder;
class UAudiobus;

class FAssetTypeActions_AudioBus : public FAssetTypeActions_Base
{
public:
	//~ Begin IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AudioBus", "Audio Bus"); }
	virtual FColor GetTypeColor() const override { return FColor(97, 97, 85); }
	virtual const TArray<FText>& GetSubMenus() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
	virtual UClass* GetSupportedClass() const override;
	virtual bool CanFilter() override { return true; }
	virtual bool IsImportedAsset() const override { return false; }
	virtual TSharedPtr<class SWidget> GetThumbnailOverlay(const FAssetData& AssetData) const override { return nullptr; }
	//~ End IAssetTypeActions Implementation
};
