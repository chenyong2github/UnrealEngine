// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolContextInterfaces.h"

/**
 * Implementation of ToolsContext Asset management API that is suitable for use
 * inside the Editor (eg inside an FEdMode)
 */
class UNREALED_API FEditorToolAssetAPI : public IToolsContextAssetAPI
{
public:
	virtual FString GetActiveAssetFolderPath() override;

	virtual UPackage* MakeNewAssetPackage(const FString& FolderPath, const FString& AssetBaseName, FString& UniqueAssetName);

	virtual void InteractiveSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage) override;

	virtual void AutoSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage) override;
};