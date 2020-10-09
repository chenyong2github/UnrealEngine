// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"

class FAssetTypeActions_LevelSnapshot : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override;
	virtual uint32 GetCategories() override;
	virtual UClass* GetSupportedClass() const override;
	virtual FColor GetTypeColor() const override;
};