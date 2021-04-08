// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnrealUSDWrapper.h"

#include "Engine/EngineTypes.h"
#include "USDStageOptions.h"

#include "SkeletalMeshExporterUSDOptions.generated.h"

/**
 * Options for exporting skeletal meshes to USD format.
 */
UCLASS( Config = Editor, Blueprintable )
class USDEXPORTER_API USkeletalMeshExporterUSDOptions : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = USDSettings, meta = ( ShowOnlyInnerProperties ) )
	FUsdStageOptions StageOptions;
};
