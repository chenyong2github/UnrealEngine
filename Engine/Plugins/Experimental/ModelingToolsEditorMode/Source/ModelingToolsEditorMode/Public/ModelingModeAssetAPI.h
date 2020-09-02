// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetGenerationUtil.h"

class IToolsContextAssetAPI;
struct FGeneratedStaticMeshAssetConfig;

/**
 * Implementation of ToolsContext Asset management API that is suitable for use
 * inside Modeling Tools Mode. 
 */
class MODELINGTOOLSEDITORMODE_API FModelingModeAssetAPI : public IAssetGenerationAPI
{
public:
	FModelingModeAssetAPI(IToolsContextAssetAPI* InAssetAPI);

	/**
	 * Generate static mesh actor
	 */
	virtual AActor* GenerateStaticMeshActor(
		UWorld* TargetWorld,
		FTransform Transform,
		FString ObjectBaseName,
		FGeneratedStaticMeshAssetConfig&& AssetConfig) override;

	/**
	 * Save generated UTexture2D that is assumed to currently be in the Transient package
	 */
	virtual bool SaveGeneratedTexture2D(
		UTexture2D* GeneratedTexture,
		FString ObjectBaseName,
		const UObject* RelativeToAsset) override;

	/**
	 * Determines path and name for a new Actor/Asset based on current mode settings, etc
	 */
	virtual bool GetNewActorPackagePath(
		UWorld* TargetWorld,
		FString ObjectBaseName,
		const FGeneratedStaticMeshAssetConfig& AssetConfig,
		FString& PackageFolderPathOut,
		FString& ObjectBaseNameOut
		);

protected:
	IToolsContextAssetAPI* AssetAPI;
};