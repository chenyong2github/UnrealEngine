// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "MaterialOptions.h"

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

	// We need a default constructor to have a slate attribute with UMaterialExporterUSDOptions type
	UMaterialExporterUSDOptions()
		: DefaultTextureSize( 128, 128 )
	{
		Properties.Add( MP_BaseColor );
	}

	/** Properties which are supposed to be baked out for the material */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = MaterialBakeSettings, meta = ( ExposeOnSpawn ) )
	TArray<FPropertyEntry> Properties;

	/** Size of the baked texture for all properties that don't have a CustomSize set */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = MaterialBakeSettings, meta = ( ExposeOnSpawn, ClampMin = "1", UIMin = "1" ) )
	FIntPoint DefaultTextureSize;

	/** Where baked textures are placed */
	UPROPERTY( EditAnywhere, BlueprintReadWrite, config, Category = Textures, meta = ( ExposeOnSpawn ) )
	FDirectoryPath TexturesDir;
};
