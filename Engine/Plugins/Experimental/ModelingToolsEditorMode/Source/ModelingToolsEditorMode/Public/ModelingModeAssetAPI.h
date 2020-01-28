// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/EditorToolAssetAPI.h"

/**
 * Implementation of ToolsContext Asset management API that is suitable for use
 * inside Modeling Tools Mode. 
 */
class MODELINGTOOLSEDITORMODE_API FModelingModeAssetAPI : public FEditorToolAssetAPI
{
public:
	/**
	 * Generate static mesh actor
	 */
	virtual AActor* GenerateStaticMeshActor(
		UWorld* TargetWorld,
		FTransform Transform,
		FString ObjectBaseName,
		FGeneratedStaticMeshAssetConfig&& AssetConfig) override;
};