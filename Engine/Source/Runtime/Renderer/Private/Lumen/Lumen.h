#pragma once

namespace Lumen
{
	enum class ETracingPermutation
	{
		Cards,
		VoxelsAfterCards,
		Voxels,
		MAX
	};

	constexpr uint32 VoxelTracingModeCount = 2;

	bool ShouldPrepareGlobalDistanceField(EShaderPlatform ShaderPlatform);
	float GetDistanceSceneNaniteLODScaleFactor();
	uint32 GetVoxelTracingMode();
	float GetMaxTraceDistance();
	bool UseHardwareRayTracedShadows(const FViewInfo& View);
	bool UseHardwareRayTracedScreenProbeGather();
	bool AnyLumenHardwareRayTracingPassEnabled();
	int32 GetGlobalDFResolution();
	float GetGlobalDFClipmapExtent();
	bool ShouldRenderLumenForView(const FScene* Scene, const FViewInfo& View);
	bool ShouldRenderLumenCardsForView(const FScene* Scene, const FViewInfo& View);
};

extern int32 GLumenFastCameraMode;
extern int32 GLumenDistantScene;