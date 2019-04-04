// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolContextInterfaces.h"

/**
 * Implementation of ToolsContext Asset management API that is suitable for use
 * inside the Editor (eg inside an FEdMode)
 */
class EDITORINTERACTIVETOOLSFRAMEWORK_API FEditorToolAssetAPI : public IToolsContextAssetAPI
{
public:
	virtual FString GetActiveAssetFolderPath() override;

	virtual FString MakePackageName(const FString& AssetName, const FString& FolderPath) override;

	virtual FString MakeUniqueAssetName(const FString& AssetName, const FString& FolderPath) override;

	// FolderPath should neither start with, nor end with, path separators
	// Generated path will be /Game/FolderPath/AssetName
	virtual UPackage* CreateNewPackage(const FString& AssetName, const FString& FolderPath) override;

	virtual void InteractiveSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage) override;

	virtual void AutoSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage) override;
};