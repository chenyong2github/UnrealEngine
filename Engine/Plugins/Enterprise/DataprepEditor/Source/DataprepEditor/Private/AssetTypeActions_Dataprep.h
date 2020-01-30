// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

/** Asset type actions for UDataprepRecipe class */
class FAssetTypeActions_Dataprep : public FAssetTypeActions_Base
{
public:
	// Begin IAssetTypeActions interface
	virtual uint32 GetCategories() override;
	virtual FText GetName() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual FColor GetTypeColor() const override;
	// End IAssetTypeActions interface
};
