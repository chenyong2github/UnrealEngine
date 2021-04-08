// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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
};
