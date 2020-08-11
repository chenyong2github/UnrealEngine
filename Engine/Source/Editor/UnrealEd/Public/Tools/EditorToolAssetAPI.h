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
	virtual FString GetWorldRelativeAssetRootPath(const UWorld* World) override;
	virtual FString InteractiveSelectAssetPath(const FString& DefaultAssetName, const FText& DialogTitleMessage) override;


	virtual UPackage* MakeNewAssetPackage(const FString& FolderPath, const FString& AssetBaseName, FString& UniqueAssetName);
	virtual FString MakeUniqueAssetName(const FString& FolderPath, const FString& AssetBaseName);

	virtual void InteractiveSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage) override;
	virtual void AutoSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage) override;
	virtual void NotifyGeneratedAssetModified(UObject* Asset, UPackage* AssetPackage) override;

	virtual AActor* GenerateStaticMeshActor(
		UWorld* TargetWorld,
		FTransform Transform,
		FString ObjectBaseName,
		FGeneratedStaticMeshAssetConfig&& AssetConfig) override
	{
		check(false);		// currently not supported in default editor tool API. 
							// ModelingModeAssetAPI contains an experimental implementation
		return nullptr;
	}
};