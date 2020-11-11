// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"

namespace Lumen
{
	enum class ETracingPermutation
	{
		Cards,
		VoxelsAfterCards,
		Voxels,
		MAX
	};

	bool ShouldPrepareGlobalDistanceField(EShaderPlatform ShaderPlatform);
	float GetDistanceSceneNaniteLODScaleFactor();
	float GetMaxTraceDistance();
	bool UseHardwareRayTracedShadows(const FViewInfo& View);
	bool UseHardwareRayTracedScreenProbeGather();
	bool UseIrradianceAtlas();
	bool UseIndirectIrradianceAtlas();
	bool AnyLumenHardwareRayTracingPassEnabled();
	int32 GetGlobalDFResolution();
	float GetGlobalDFClipmapExtent();
	bool ShouldRenderLumenForView(const FScene* Scene, const FViewInfo& View);
	bool ShouldRenderLumenCardsForView(const FScene* Scene, const FViewInfo& View);

	// Hardware ray-traced reflections
	bool UseHardwareRayTracedReflections();
	enum class EHardwareRayTracedReflectionsLightingMode
	{
		LightingFromSurfaceCache = 0,
		EvaluateMaterial,
		EvaluateMaterialAndDirectLighting
	};
	EHardwareRayTracedReflectionsLightingMode GetHardwareRayTracedReflectionsLightingMode();
};

extern int32 GLumenFastCameraMode;
extern int32 GLumenDistantScene;

LLM_DECLARE_TAG(Lumen);
