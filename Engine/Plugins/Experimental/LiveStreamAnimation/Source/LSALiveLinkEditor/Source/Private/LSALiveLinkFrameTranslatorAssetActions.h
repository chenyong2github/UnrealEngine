// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"
#include "LSALiveLinkFrameTranslator.h"

class FLSALiveLinkFrameTranslatorAssetActions : public FAssetTypeActions_Base
{
public:
	//~ Begin IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("LSALiveLink", "LiveLinkFrameTranslatorAssetActions_Name", "Live Stream Animation Live Link Frame Translator"); }
	virtual FColor GetTypeColor() const override { return FColor(212, 97, 85); }
	virtual UClass* GetSupportedClass() const override { return ULSALiveLinkFrameTranslator::StaticClass(); }
	virtual bool CanFilter() override { return true; }
	virtual bool IsImportedAsset() const override { return false; }
	virtual TSharedPtr<class SWidget> GetThumbnailOverlay(const FAssetData& AssetData) const override { return nullptr; }
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Animation; }
	//~ End IAssetTypeActions Implementation
};
