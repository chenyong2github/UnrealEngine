// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

class UTexture;
class UInteractiveToolManager;

/**
 * Utility functions for Tool implementations to use when doing configuration/setup
 */
namespace ToolSetupUtil
{
	/**
	 * Get the default material for surfaces
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultMaterial();


	/**
	 * Get the default material to use for objects in an InteractiveTool. Optionally use SourceMaterial if it is valid.
	 * @param SourceMaterial optional material to use if available
	 * @return default material to use for objects in a tool.
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultMaterial(UInteractiveToolManager* ToolManager, UMaterialInterface* SourceMaterial = nullptr);

	/**
	 * @return configurable vertex color material
	 */
	MODELINGCOMPONENTS_API UMaterialInstanceDynamic* GetVertexColorMaterial(UInteractiveToolManager* ToolManager);


	/**
	 * @return default material to use for "Working"/In-Progress animations
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultWorkingMaterial(UInteractiveToolManager* ToolManager);


	/**
	 * Get a black-and-white NxN checkerboard material
	 * @param CheckerDensity Number of checks along row/column
	 * @return default material to use for uv checkerboard visualizations
	 */
	MODELINGCOMPONENTS_API UMaterialInstanceDynamic* GetUVCheckerboardMaterial(double CheckerDensity = 20.0);


	/**
	 * @return default material to use for brush volume indicators
	 */
	MODELINGCOMPONENTS_API UMaterialInstanceDynamic* GetDefaultBrushVolumeMaterial(UInteractiveToolManager* ToolManager);


	/**
	 * @return Sculpt Material 1
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultSculptMaterial(UInteractiveToolManager* ToolManager);


	/** Types of image-based material that we can create */
	enum class ImageMaterialType
	{
		DefaultBasic,
		DefaultSoft,
		TangentNormalFromView
	};

	/**
	 * @return Image-based sculpt material instance, based ImageMaterialType
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetImageBasedSculptMaterial(UInteractiveToolManager* ToolManager, ImageMaterialType Type);

	/**
	 * @return Image-based sculpt material that supports changing the image
	 */
	MODELINGCOMPONENTS_API UMaterialInstanceDynamic* GetCustomImageBasedSculptMaterial(UInteractiveToolManager* ToolManager, UTexture* SetImage);


	/**
	 * @return Selection Material 1
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetSelectionMaterial(UInteractiveToolManager* ToolManager);

	/**
	 * @return Selection Material 1 with custom color and optional depth offset (depth offset moves vertices towards the camera)
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetSelectionMaterial(const FLinearColor& UseColor, UInteractiveToolManager* ToolManager, float PercentDepthOffset = 0.0f);

	/**
	 * @return Simple material with configurable color and opacity.
	 */
	MODELINGCOMPONENTS_API UMaterialInstanceDynamic* GetSimpleCustomMaterial(UInteractiveToolManager* ToolManager, const FLinearColor& Color, float Opacity);

	/**
	 * @param bRoundPoints true for round points, false for square
	 * @return custom material suitable for use with UPointSetComponent
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultPointComponentMaterial(bool bRoundPoints, UInteractiveToolManager* ToolManager);

	/**
	 * @return custom material suitable for use with ULineSetComponent
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultLineComponentMaterial(UInteractiveToolManager* ToolManager, bool bDepthTested = true);
}