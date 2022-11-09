// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"

#include "USDProjectSettings.generated.h"

UENUM( BlueprintType )
enum class EUsdSaveDialogBehavior : uint8
{
	NeverSave,
	AlwaysSave,
	ShowPrompt
};

// USDImporter and defaultconfig here so this ends up at DefaultUSDImporter.ini in the editor, and is sent to the
// packaged game as well
UCLASS(config=USDImporter, defaultconfig, meta=(DisplayName=USDImporter), MinimalAPI)
class UUsdProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Additional paths to check for USD plugins
	UPROPERTY( config, EditAnywhere, Category = USD )
	TArray<FDirectoryPath> AdditionalPluginDirectories;

	// Material purposes to show on drop-downs in addition to the standard "preview" and "full"
	UPROPERTY( config, EditAnywhere, Category = USD )
	TArray<FName> AdditionalMaterialPurposes;

	UPROPERTY( config, EditAnywhere, Category = "USD|Dialogs" )
	bool bShowConfirmationWhenClearingLayers = true;

	UPROPERTY( config, EditAnywhere, Category = "USD|Dialogs" )
	bool bShowConfirmationWhenMutingDirtyLayers = true;

	// Whether to show the warning dialog when authoring opinions that could have no effect on the composed stage
	UPROPERTY( config, EditAnywhere, Category = "USD|Dialogs" )
	bool bShowOverriddenOpinionsWarning = true;

	// Whether to show a warning whenever the "Duplicate All Local Layer Specs" option is picked, and the duplicated
	// prim has some specs outside the local layer stack that will not be duplicated.
	UPROPERTY( config, EditAnywhere, Category = "USD|Dialogs" )
	bool bShowWarningOnIncompleteDuplication = true;

	// Whether to display the pop up dialog asking what to do about dirty USD layers when saving the UE level
	UPROPERTY(config, EditAnywhere, Category = "USD|Dialogs" )
	EUsdSaveDialogBehavior ShowSaveLayersDialogWhenSaving = EUsdSaveDialogBehavior::ShowPrompt;

	// Whether to display the pop up dialog asking what to do about dirty USD layers when closing USD stages
	UPROPERTY(config, EditAnywhere, Category = "USD|Dialogs" )
	EUsdSaveDialogBehavior ShowSaveLayersDialogWhenClosing = EUsdSaveDialogBehavior::ShowPrompt;

	// Note that the below properties being FSoftObjectPath ensure that these materials are cooked into packaged games

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|UsdPreviewSurface", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BasePreviewSurfaceMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/UsdPreviewSurface.UsdPreviewSurface" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|UsdPreviewSurface", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BasePreviewSurfaceTranslucentMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTranslucent.UsdPreviewSurfaceTranslucent" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|UsdPreviewSurface", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BasePreviewSurfaceTwoSidedMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTwoSided.UsdPreviewSurfaceTwoSided" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|UsdPreviewSurface", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BasePreviewSurfaceTranslucentTwoSidedMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTranslucentTwoSided.UsdPreviewSurfaceTranslucentTwoSided" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|UsdPreviewSurface with Virtual Textures", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BasePreviewSurfaceVTMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/UsdPreviewSurfaceVT.UsdPreviewSurfaceVT" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|UsdPreviewSurface with Virtual Textures", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BasePreviewSurfaceTranslucentVTMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTranslucentVT.UsdPreviewSurfaceTranslucentVT" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|UsdPreviewSurface with Virtual Textures", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BasePreviewSurfaceTwoSidedVTMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTwoSidedVT.UsdPreviewSurfaceTwoSidedVT" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|UsdPreviewSurface with Virtual Textures", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BasePreviewSurfaceTranslucentTwoSidedVTMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTranslucentTwoSidedVT.UsdPreviewSurfaceTranslucentTwoSidedVT" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|DisplayColor", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BaseDisplayColorMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/DisplayColor.DisplayColor" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|DisplayColor", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BaseDisplayColorAndOpacityMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/DisplayColorAndOpacity.DisplayColorAndOpacity" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|DisplayColor", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BaseDisplayColorTwoSidedMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/DisplayColorTwoSided.DisplayColorTwoSided" ) };

	/**
	 * What material to use as base material when creating material instances from USD materials.
	 * You can swap these with your own materials, but make sure that the replacement materials have parameters with
	 * the same names and types as the ones provided by the default material, otherwise the instances will not have
	 * the parameters filled with values extracted from the USD material when parsing.
	 */
	UPROPERTY( config, EditAnywhere, Category = "USD|Base Materials|DisplayColor", meta = ( AllowedClasses = "/Script/Engine.MaterialInterface" ) )
	FSoftObjectPath BaseDisplayColorAndOpacityTwoSidedMaterial = FSoftObjectPath{ TEXT( "/USDImporter/Materials/DisplayColorAndOpacityTwoSided.DisplayColorAndOpacityTwoSided" ) };
};