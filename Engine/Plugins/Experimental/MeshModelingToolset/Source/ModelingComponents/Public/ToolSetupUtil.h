// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UInteractiveToolManager;

/**
 * Utility functions for Tool implementations to use when doing configuration/setup
 */
namespace ToolSetupUtil
{
	/**
	 * Get the default material to use for objects in an InteractiveTool. Optionally use SourceMaterial if it is valid.
	 * @param SourceMaterial optional material to use if available
	 * @return default material to use for objects in a tool.
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultMaterial(UInteractiveToolManager* ToolManager, UMaterialInterface* SourceMaterial = nullptr);

	/**
	 * @return default material to use for "Working"/In-Progress animations
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultWorkingMaterial(UInteractiveToolManager* ToolManager);

	/**
	 * @return default material to use for brush volume indicators
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetDefaultBrushVolumeMaterial(UInteractiveToolManager* ToolManager);


	/**
	 * @return Sculpt Material 1
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetSculptMaterial1(UInteractiveToolManager* ToolManager);


	/**
	 * @return Selection Material 1
	 */
	MODELINGCOMPONENTS_API UMaterialInterface* GetSelectionMaterial(UInteractiveToolManager* ToolManager);

}