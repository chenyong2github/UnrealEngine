// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDAssetOptions.h"

#include "Engine/EngineTypes.h"

#include "MaterialExporterUSDOptions.generated.h"

/**
 * Options for exporting materials to USD format.
 * We use a dedicated object instead of reusing the MaterialBaking module as automated export tasks
 * can only have one options object, and we need to also provide the textures directory.
 */
UCLASS( Config = Editor, Blueprintable )
class USDEXPORTER_API UMaterialExporterUSDOptions : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Material baking options", meta = ( ShowOnlyInnerProperties ))
	FUsdMaterialBakingOptions MaterialBakingOptions;
};
