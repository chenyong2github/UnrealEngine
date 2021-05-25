// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LevelExporterUSDOptions.h"
#include "UnrealUSDWrapper.h"
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
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = USDSettings, meta = ( ShowOnlyInnerProperties ) )
	FUsdStageOptions StageOptions;

	/** If true, the mesh data is exported to yet another "payload" file, and referenced via a payload composition arc */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = USDSettings )
	bool bUsePayload;

	/** USD format to use for exported payload files */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = USDSettings, meta = ( EditCondition = "bUsePayload", GetOptions = "USDExporter.LevelExporterUSDOptions.GetUsdExtensions" ) )
	FString PayloadFormat;
};
