// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessVisualizeDebugMaterial.h"
#include "EngineGlobals.h"

static bool IsPostProcessVisualizeDebugColorMaterialEnabled(const FViewInfo& View) 
{
	return View.Family->EngineShowFlags.VisualizeDebugColor && View.CurrentVisualizeDebugColorMaterialName != NAME_None;
}

static bool IsPostProcessVisualizeDebugGrayscaleMaterialEnabled(const FViewInfo& View) 
{
	return View.Family->EngineShowFlags.VisualizeDebugGrayscale && View.CurrentVisualizeDebugGrayscaleMaterialName != NAME_None;
}

static bool IsPostProcessVisualizeDebugCustomMaterialEnabled(const FViewInfo& View) 
{
	return View.Family->EngineShowFlags.VisualizeDebugCustomPostProcessMaterial && View.CurrentVisualizeDebugCustomMaterialName != NAME_None;
}

bool IsPostProcessVisualizeDebugMaterialEnabled(const FViewInfo& View) 
{
	return (IsPostProcessVisualizeDebugColorMaterialEnabled(View) || IsPostProcessVisualizeDebugGrayscaleMaterialEnabled(View) || IsPostProcessVisualizeDebugCustomMaterialEnabled(View));
}

// Returns whether the debug custom material pass needs to render on screen.
const UMaterialInterface* GetPostProcessVisualizeDebugMaterialInterface(const FViewInfo& View) 
{
	if (IsPostProcessVisualizeDebugCustomMaterialEnabled(View))
	{
		return View.FinalPostProcessSettings.DebugCustomVisualizationMaterial;
	}
	else if (IsPostProcessVisualizeDebugColorMaterialEnabled(View))
	{
		return View.FinalPostProcessSettings.DebugColorVisualizationMaterial;
	}
	else if (IsPostProcessVisualizeDebugGrayscaleMaterialEnabled(View))
	{
		return View.FinalPostProcessSettings.DebugGrayscaleVisualizationMaterial;
	}

	return NULL;
}
