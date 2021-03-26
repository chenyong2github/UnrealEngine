// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"

extern bool ShouldRenderLumenDiffuseGI(const FScene* Scene, const FViewInfo& View, bool bRequireSoftwareTracing);
extern bool ShouldRenderLumenReflections(const FViewInfo& View, bool bRequireSoftwareTracing);

namespace Lumen
{
	enum class ETracingPermutation
	{
		Cards,
		VoxelsAfterCards,
		Voxels,
		MAX
	};

	float GetDistanceSceneNaniteLODScaleFactor();
	bool UseMeshSDFTracing();
	float GetMaxTraceDistance();
	bool UseIrradianceAtlas(const FViewInfo& View);
	bool UseIndirectIrradianceAtlas(const FViewInfo& View);
	bool AnyLumenHardwareRayTracingPassEnabled(const FScene* Scene, const FViewInfo& View);
	int32 GetGlobalDFResolution();
	float GetGlobalDFClipmapExtent();
	bool IsLumenFeatureAllowedForView(const FScene* Scene, const FViewInfo& View, bool bRequireSoftwareTracing);
	bool ShouldVisualizeHardwareRayTracing();
	bool ShouldHandleSkyLight(const FScene* Scene, const FSceneViewFamily& ViewFamily);
	bool IsPrimitiveToDFObjectMappingRequired();

	// Hardware ray-traced reflections
	bool UseHardwareRayTracing();
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
	EHardwareRayTracingLightingMode GetReflectionsHardwareRayTracingLightingMode(const FViewInfo& View);
	EHardwareRayTracingLightingMode GetScreenProbeGatherHardwareRayTracingLightingMode();
	EHardwareRayTracingLightingMode GetVisualizeHardwareRayTracingLightingMode();

	const TCHAR* GetRayTracedLightingModeName(EHardwareRayTracingLightingMode LightingMode);
	const TCHAR* GetRayTracedNormalModeName(int NormalMode);
	void SetupViewUniformBufferParameters(FScene* Scene, FViewUniformShaderParameters& ViewUniformShaderParameters);
	float GetHardwareRayTracingPullbackBias();
};

extern int32 GLumenFastCameraMode;
extern int32 GLumenDistantScene;

LLM_DECLARE_TAG(Lumen);
