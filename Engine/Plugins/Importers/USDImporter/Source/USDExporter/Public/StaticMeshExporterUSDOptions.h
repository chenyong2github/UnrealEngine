// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDAssetOptions.h"
#include "USDStageOptions.h"

#include "Engine/EngineTypes.h"

#include "StaticMeshExporterUSDOptions.generated.h"

/**
 * Options for exporting static meshes to USD format.
 */
UCLASS( Config = Editor, Blueprintable )
class USDEXPORTER_API UStaticMeshExporterUSDOptions : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Stage options", meta = ( ShowOnlyInnerProperties ) )
	FUsdStageOptions StageOptions;

	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Mesh options", meta = ( ShowOnlyInnerProperties ) )
	FUsdMeshAssetOptions MeshAssetOptions;
};
