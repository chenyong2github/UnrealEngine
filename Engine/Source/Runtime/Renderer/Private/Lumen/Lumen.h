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
	bool UseIrradianceAtlas();
	bool UseIndirectIrradianceAtlas();
	bool AnyLumenHardwareRayTracingPassEnabled();
	int32 GetGlobalDFResolution();
	float GetGlobalDFClipmapExtent();
	bool ShouldRenderLumenForViewFamily(const FScene* Scene, const FSceneViewFamily& ViewFamily);
	bool ShouldRenderLumenForViewWithoutMeshSDFs(const FScene* Scene, const FViewInfo& View);
	bool ShouldRenderLumenForView(const FScene* Scene, const FViewInfo& View);
	bool ShouldRenderLumenCardsForView(const FScene* Scene, const FViewInfo& View);
	bool ShouldVisualizeHardwareRayTracing();

	// Hardware ray-traced reflections
	bool UseHardwareRayTracedReflections();
	bool UseHardwareRayTracedScreenProbeGather();
	bool UseHardwareRayTracedShadows(const FViewInfo& View);

	enum class EHardwareRayTracingLightingMode
	{
		LightingFromSurfaceCache = 0,
		EvaluateMaterial,
		EvaluateMaterialAndDirectLighting,
		MAX
	};
	EHardwareRayTracingLightingMode GetReflectionsHardwareRayTracingLightingMode();
	EHardwareRayTracingLightingMode GetScreenProbeGatherHardwareRayTracingLightingMode();
	EHardwareRayTracingLightingMode GetVisualizeHardwareRayTracingLightingMode();

	const TCHAR* GetRayTracedLightingModeName(EHardwareRayTracingLightingMode LightingMode);
};

extern int32 GLumenFastCameraMode;
extern int32 GLumenDistantScene;

LLM_DECLARE_TAG(Lumen);
