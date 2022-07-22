// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DistanceFieldLightingShared.h"
#include "HAL/LowLevelMemTracker.h"

extern bool ShouldRenderLumenDiffuseGI(const FScene* Scene, const FSceneView& View, bool bSkipTracingDataCheck = false, bool bSkipProjectCheck = false);
extern bool ShouldRenderLumenReflections(const FViewInfo& View, bool bSkipTracingDataCheck = false, bool bSkipProjectCheck = false);
extern bool ShouldRenderLumenDirectLighting(const FScene* Scene, const FSceneView& View);

class FLumenSceneData;

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
	constexpr uint32 CardTileSize = 8;

	constexpr float MaxTracingEndDistanceFromCamera = 0.5f * UE_OLD_WORLD_MAX;
	constexpr float MaxTraceDistance = 0.5f * UE_OLD_WORLD_MAX;

	enum class ETracingPermutation
	{
		Cards,
		VoxelsAfterCards,
		Voxels,
		MAX
	};

	void DebugResetSurfaceCache();
	void DebugResetVoxelLighting();

	bool UseMeshSDFTracing(const FSceneViewFamily& ViewFamily);
	bool UseGlobalSDFTracing(const FSceneViewFamily& ViewFamily);
	bool UseHeightfieldTracing(const FSceneViewFamily& ViewFamily, const FLumenSceneData& LumenSceneData);
	bool UseHeightfieldTracingForVoxelLighting(const FLumenSceneData& LumenSceneData);
	int32 GetHeightfieldMaxTracingSteps();
	float GetMaxTraceDistance(const FViewInfo& View);
	bool AnyLumenHardwareRayTracingPassEnabled(const FScene* Scene, const FViewInfo& View);
	bool AnyLumenHardwareInlineRayTracingPassEnabled(const FScene* Scene, const FViewInfo& View);
	bool IsSoftwareRayTracingSupported();
	bool IsLumenFeatureAllowedForView(const FScene* Scene, const FSceneView& View, bool bSkipTracingDataCheck = false, bool bSkipProjectCheck = false);
	bool ShouldVisualizeScene(const FSceneViewFamily& ViewFamily);
	bool ShouldVisualizeHardwareRayTracing(const FSceneViewFamily& ViewFamily);
	bool ShouldHandleSkyLight(const FScene* Scene, const FSceneViewFamily& ViewFamily);

	void ExpandDistanceFieldUpdateTrackingBounds(const FSceneViewState* ViewState, DistanceField::FUpdateTrackingBounds& UpdateTrackingBounds);
	float GetDistanceSceneNaniteLODScaleFactor();

	bool ShouldUpdateLumenSceneViewOrigin();
	int32 GetGlobalDFResolution();
	float GetGlobalDFClipmapExtent();
	float GetFirstClipmapWorldExtent();

	// Features
	bool UseThreadGroupSize32();
	bool IsRadiosityEnabled(const FSceneViewFamily& ViewFamily);
	uint32 GetRadiosityAtlasDownsampleFactor();

	// Surface cache
	bool IsSurfaceCacheFrozen();
	bool IsSurfaceCacheUpdateFrameFrozen();

	// Software ray tracing
	bool UseVoxelLighting(const FSceneViewFamily& ViewFamily);

	// Hardware ray tracing
	bool UseHardwareRayTracing(const FSceneViewFamily& ViewFamily);
	bool UseHardwareRayTracedSceneLighting(const FSceneViewFamily& ViewFamily);
	bool UseHardwareRayTracedDirectLighting(const FSceneViewFamily& ViewFamily);
	bool UseHardwareRayTracedReflections(const FSceneViewFamily& ViewFamily);
	bool UseHardwareRayTracedScreenProbeGather(const FSceneViewFamily& ViewFamily);
	bool UseHardwareRayTracedRadianceCache(const FSceneViewFamily& ViewFamily);
	bool UseHardwareRayTracedRadiosity(const FSceneViewFamily& ViewFamily);
	bool UseHardwareRayTracedVisualize(const FSceneViewFamily& ViewFamily);

	bool ShouldRenderRadiosityHardwareRayTracing(const FSceneViewFamily& ViewFamily);
	bool ShouldVisualizeHardwareRayTracing(const FSceneViewFamily& ViewFamily);

	int32 GetMaxTranslucentSkipCount();
	bool UseHardwareInlineRayTracing(const FSceneViewFamily& ViewFamily);

	enum class EHardwareRayTracingLightingMode
	{
		LightingFromSurfaceCache = 0,
		EvaluateMaterial,
		EvaluateMaterialAndDirectLighting,
		EvaluateMaterialAndDirectLightingAndSkyLighting,
		MAX
	};
	EHardwareRayTracingLightingMode GetHardwareRayTracingLightingMode(const FViewInfo& View);
	EHardwareRayTracingLightingMode GetRadianceCacheHardwareRayTracingLightingMode();

	enum class ESurfaceCacheSampling
	{
		AlwaysResidentPagesWithoutFeedback,
		AlwaysResidentPages,
		HighResPages,
	};

	const TCHAR* GetRayTracedLightingModeName(EHardwareRayTracingLightingMode LightingMode);
	const TCHAR* GetRayTracedNormalModeName(int NormalMode);
	float GetHardwareRayTracingPullbackBias();

	bool UseFarField(const FSceneViewFamily& ViewFamily);
	float GetFarFieldMaxTraceDistance();
	float GetFarFieldDitheredStartDistanceFactor();
	FVector GetFarFieldReferencePos();

	float GetHeightfieldReceiverBias();
	void Shutdown();
};

namespace LumenHardwareRayTracing
{
	float GetFarFieldBias();
	uint32 GetMaxTraversalIterations();
};

extern int32 GLumenFastCameraMode;
extern int32 GLumenDistantScene;

LLM_DECLARE_TAG(Lumen);
