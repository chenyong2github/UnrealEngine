// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnrealUSDWrapper.h"
#include "SkeletalMeshExporterUSDOptions.h"

#include "Engine/EngineTypes.h"
#include "USDStageOptions.h"

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
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = USDSettings, meta = ( ShowOnlyInnerProperties ) )
	FUsdStageOptions StageOptions;

	/** Whether to also export the skeletal mesh data of the preview mesh */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = USDSettings )
	bool bExportPreviewMesh;

	/** Export options to use for the preview mesh, if enabled */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = USDSettings, meta = ( EditCondition=bExportPreviewMesh ) )
	FSkeletalMeshExporterUSDInnerOptions PreviewMeshOptions;
};
