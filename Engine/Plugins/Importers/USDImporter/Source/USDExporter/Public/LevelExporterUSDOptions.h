// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnrealUSDWrapper.h"
#include "USDAssetOptions.h"
#include "USDStageOptions.h"

#include "AssetExportTask.h"
#include "Engine/EngineTypes.h"
#include "RHIDefinitions.h"

#include "LevelExporterUSDOptions.generated.h"

USTRUCT( BlueprintType )
struct USDEXPORTER_API FLevelExporterUSDOptionsInner
{
	GENERATED_BODY()

	/** Whether to export only the selected actors, and assets used by them */
    UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Export options" )
    bool bSelectionOnly = false;

	/** Whether to use UE actor folders as empty prims */
    UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Export options" )
    bool bExportActorFolders = false;

	/** If true, and if we have a level sequence animating the level during export, it will revert any actor or component to its unanimated state before writing to USD */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Export options" )
	bool bIgnoreSequencerAnimations = false;

	/**
	 * By default foliage instances will be exported to the same layer as the component they were placed on in the editor.
	 * Enable this to instead export the foliage instances to the same layer as the foliage actor they belong to.
	 * This is useful if those foliage instances were placed with the "Place In Current Level" option.
	 */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Export options" )
	bool bExportFoliageOnActorsLayer = false;

	/** Where to place all the generated asset files */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Asset options" )
	FDirectoryPath AssetFolder;

	/** Options to use for all exported assets when appropriate (e.g. static and skeletal meshes, materials, etc.) */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Asset options", meta = ( ShowOnlyInnerProperties ) )
	FUsdMeshAssetOptions AssetOptions;

	/** Lowest of the LOD indices to export landscapes with (use 0 for full resolution) */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Landscape options", meta = ( ClampMin = "0" ) )
	int32 LowestLandscapeLOD = 0;

	/**
	 * Highest of the LOD indices to export landscapes with. Each value above 0 halves resolution.
	 * The max value depends on the number of components and sections per component of each landscape, and may be clamped.
	 */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Landscape options", meta = ( ClampMin = "0" ) )
	int32 HighestLandscapeLOD = 0;

	/** Resolution to use when baking landscape materials into textures  */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Landscape options", meta = ( ClampMin = "1" ) )
	FIntPoint LandscapeBakeResolution = FIntPoint( 1024, 1024 );

	/** If true, will export sub-levels as separate layers (referenced as sublayers). If false, will collapse all sub-levels in a single exported root layer */
    UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Sublayers" )
    bool bExportSublayers = false;

	/** Names of levels that should be ignored when collecting actors to export (e.g. "Persistent Level", "Level1", "MySubLevel", etc.) */
    UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Sublayers" )
    TSet<FString> LevelsToIgnore;
};

/**
 * Options for exporting levels to USD format.
 */
UCLASS( Config = Editor, Blueprintable, HideCategories=Hidden )
class USDEXPORTER_API ULevelExporterUSDOptions : public UObject
{
	GENERATED_BODY()

public:
	/** Basic options about the stage to export */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Stage options", meta = ( ShowOnlyInnerProperties ) )
	FUsdStageOptions StageOptions;

	/** StartTimeCode to be used for all exported layers */
    UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Stage options" )
    float StartTimeCode = 0.0f;

	/** EndTimeCode to be used for all exported layers */
    UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Stage options" )
    float EndTimeCode = 0.0f;

	/** Inner struct that actually contains most of the export options */
    UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Export settings", meta = ( ShowOnlyInnerProperties ) )
    FLevelExporterUSDOptionsInner Inner;

public:
	// We temporarily stash our export task here as a way of passing our options down to
	// the Python exporter, that does the actual level exporting.
	// This is weak because we often use the CDO of this class directly, and we never want to
	// permanently hold on to a particular export task
	UPROPERTY( VisibleAnywhere, BlueprintReadOnly, Category = Hidden )
	TWeakObjectPtr<UAssetExportTask> CurrentTask;

private:
	UFUNCTION()
	static TArray<FString> GetUsdExtensions()
	{
		TArray<FString> Extensions = UnrealUSDWrapper::GetAllSupportedFileFormats();
		Extensions.Remove( TEXT( "usdz" ) );
		return Extensions;
	}
};
