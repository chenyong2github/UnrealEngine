// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"

class IToolkitHost;

class FAssetTypeActions_LensFile : public FAssetTypeActions_Base
{
public:
	//~ Begin IAssetTypeActions interface
	virtual FColor GetTypeColor() const override { return FColor(100, 255, 100); }
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return false; }
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Misc; }
	virtual bool IsImportedAsset() const override { return false; }
	virtual FText GetName() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	//~ End IAssetTypeActions interface
};