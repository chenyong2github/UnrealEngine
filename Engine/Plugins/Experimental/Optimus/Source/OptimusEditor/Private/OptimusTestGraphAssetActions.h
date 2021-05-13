// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeCategories.h"
#include "AssetTypeActions_Base.h"
#include "OptimusTestGraph.h"

class FOptimusTestGraphAssetActions : public FAssetTypeActions_Base
{
public:
	FOptimusTestGraphAssetActions(EAssetTypeCategories::Type InAssetCategoryBit = EAssetTypeCategories::Animation);

	//~ Begin FAssetTypeActions_Base Interface.
	FText GetName() const override;
	FColor GetTypeColor() const override;
	UClass* GetSupportedClass() const override;
	bool CanFilter() override;
	bool CanLocalize() const override;
	uint32 GetCategories() override;
	//~ End FAssetTypeActions_Base Interface.

private:
	EAssetTypeCategories::Type AssetCategoryBit;
};
