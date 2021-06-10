// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/LowLevelMemTracker.h"

extern bool ShouldRenderLumenDiffuseGI(const FScene* Scene, const FViewInfo& View, bool bRequireSoftwareTracing);
extern bool ShouldRenderLumenReflections(const FViewInfo& View, bool bRequireSoftwareTracing);

inline double BoxSurfaceArea(FVector Extent)
{
	return 2.0 * (Extent.X * Extent.Y + Extent.Y * Extent.Z + Extent.Z * Extent.X);
}

namespace Lumen
{
	// Must match usf
	constexpr uint32 PhysicalPageSize = 128;
	constexpr uint32 VirtualPageSize = PhysicalPageSize - 1; // 0.5 texel border around page
	constexpr uint32 MinCardResolution = 8;
	constexpr uint32 MinResLevel = 3; // 2^3 = MinCardResolution
	constexpr uint32 MaxResLevel = 11; // 2^11 = 2048 texels
	constexpr uint32 SubAllocationResLevel = 7; // log2(PHYSICAL_PAGE_SIZE)
	constexpr uint32 NumResLevels = MaxResLevel - MinResLevel + 1;
	constexpr uint32 MaxShadowMaskChannels = 32;

	enum class ETracingPermutation
	{
		Cards,
		VoxelsAfterCards,
		Voxels,
		MAX
	};

	void DebugResetSurfaceCache();
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
	bool UseLumenSceneLightingForceFullUpdate();
	bool UseVirtualShadowMaps();

	// Hardware ray tracing
	bool UseHardwareRayTracing();
	bool UseHardwareRayTracedDirectLighting();
	bool UseHardwareRayTracedReflections();
	bool UseHardwareRayTracedScreenProbeGather();
	bool UseHardwareRayTracedRadianceCache();

	enum class EHardwareRayTracingLightingMode
	{
		LightingFromSurfaceCache = 0,
		EvaluateMaterial,
		EvaluateMaterialAndDirectLighting,
		EvaluateMaterialAndDirectLightingAndSkyLighting,
		MAX
	};
	EHardwareRayTracingLightingMode GetReflectionsHardwareRayTracingLightingMode(const FViewInfo& View);
	EHardwareRayTracingLightingMode GetScreenProbeGatherHardwareRayTracingLightingMode();
	EHardwareRayTracingLightingMode GetRadianceCacheHardwareRayTracingLightingMode();
	EHardwareRayTracingLightingMode GetVisualizeHardwareRayTracingLightingMode();

	const TCHAR* GetRayTracedLightingModeName(EHardwareRayTracingLightingMode LightingMode);
	const TCHAR* GetRayTracedNormalModeName(int NormalMode);
	float GetHardwareRayTracingPullbackBias();
};

extern int32 GLumenFastCameraMode;
extern int32 GLumenDistantScene;

LLM_DECLARE_TAG(Lumen);
