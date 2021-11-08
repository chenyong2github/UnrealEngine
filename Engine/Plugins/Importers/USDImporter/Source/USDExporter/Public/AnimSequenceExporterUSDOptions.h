// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDAssetOptions.h"
#include "USDStageOptions.h"

#include "Engine/EngineTypes.h"

#include "AnimSequenceExporterUSDOptions.generated.h"

/**
 * Options for exporting skeletal mesh animations to USD format.
 */
UCLASS( Config = Editor, Blueprintable )
class USDEXPORTER_API UAnimSequenceExporterUSDOptions : public UObject
{
	GENERATED_BODY()

public:
	/** Export options to use for the layer where the animation is emitted */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Stage options", meta = ( ShowOnlyInnerProperties ) )
	FUsdStageOptions StageOptions;

	/** Whether to also export the skeletal mesh data of the preview mesh */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Mesh options" )
	bool bExportPreviewMesh;

	/** Export options to use for the preview mesh, if enabled */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Mesh options", meta = ( ShowOnlyInnerProperties, EditCondition = bExportPreviewMesh ) )
	FUsdMeshAssetOptions PreviewMeshOptions;
};
