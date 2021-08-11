// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeCategories.h"
#include "Modules/ModuleManager.h"

class NEURALNETWORKINFERENCEEDITOR_API INeuralNetworkInferenceEditorModule : public IModuleInterface
{
public:
	/**
	 * It returns the EAssetTypeCategories::Type for the "Machine Learning" category
	 */
	virtual EAssetTypeCategories::Type GetMLAssetCategoryBit() const = 0;
};
