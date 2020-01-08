// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Developer/AssetTools/Public/AssetTypeCategories.h"
#include "Modules/ModuleInterface.h"

class IDataprepCoreModule : public IModuleInterface
{
public:
	/** Category bit associated with DataPrep related content */
	static DATAPREPCORE_API EAssetTypeCategories::Type DataprepCategoryBit;
};
