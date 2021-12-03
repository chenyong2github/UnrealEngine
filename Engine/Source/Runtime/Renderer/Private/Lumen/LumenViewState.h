// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenViewState.h:
=============================================================================*/

#pragma once

const static int32 NumLumenDiffuseIndirectTextures = 2;
// Must match shader
const static int32 MaxVoxelClipmapLevels = 8;

class FLumenGatherCvarState
{
public:

	FLumenGatherCvarState();

	int32 TraceMeshSDFs;
	float MeshSDFTraceDistance;
	float SurfaceBias;
	int32 VoxelTracingMode;

	inline bool operator==(const FLumenGatherCvarState& Rhs)
	{
		return TraceMeshSDFs == Rhs.TraceMeshSDFs &&
			MeshSDFTraceDistance == Rhs.MeshSDFTraceDistance &&
			SurfaceBias == Rhs.SurfaceBias &&
			VoxelTracingMode == Rhs.VoxelTracingMode;
	}
};

class FScreenProbeGatherTemporalState
{
public:
	FIntRect DiffuseIndirectHistoryViewRect;
	FVector4f DiffuseIndirectHistoryScreenPositionScaleBias;
	TRefCountPtr<IPooledRenderTarget> DiffuseIndirectHistoryRT[4];
	TRefCountPtr<IPooledRenderTarget> RoughSpecularIndirectHistoryRT;
	TRefCountPtr<IPooledRenderTarget> NumFramesAccumulatedRT;
	TRefCountPtr<IPooledRenderTarget> FastUpdateModeHistoryRT;
	TRefCountPtr<IPooledRenderTarget> OctahedralSolidAngleTextureRT;
	FIntRect ProbeHistoryViewRect;
	FVector4f ProbeHistoryScreenPositionScaleBias;
	TRefCountPtr<IPooledRenderTarget> HistoryScreenProbeSceneDepth;
	TRefCountPtr<IPooledRenderTarget> HistoryScreenProbeTranslatedWorldPosition;
	TRefCountPtr<IPooledRenderTarget> ProbeHistoryScreenProbeRadiance;
	TRefCountPtr<IPooledRenderTarget> ImportanceSamplingHistoryScreenProbeRadiance;
	FLumenGatherCvarState LumenGatherCvars;

	FScreenProbeGatherTemporalState()
	{
		DiffuseIndirectHistoryViewRect = FIntRect(0, 0, 0, 0);
		DiffuseIndirectHistoryScreenPositionScaleBias = FVector4f(0, 0, 0, 0);
		ProbeHistoryViewRect = FIntRect(0, 0, 0, 0);
		ProbeHistoryScreenPositionScaleBias = FVector4f(0, 0, 0, 0);
	}

	void SafeRelease()
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(DiffuseIndirectHistoryRT); i++)
		{
			DiffuseIndirectHistoryRT[i].SafeRelease();
		}
		
		RoughSpecularIndirectHistoryRT.SafeRelease();
		NumFramesAccumulatedRT.SafeRelease();
		FastUpdateModeHistoryRT.SafeRelease();
		OctahedralSolidAngleTextureRT.SafeRelease();
		HistoryScreenProbeSceneDepth.SafeRelease();
		HistoryScreenProbeTranslatedWorldPosition.SafeRelease();
		ProbeHistoryScreenProbeRadiance.SafeRelease();
		ImportanceSamplingHistoryScreenProbeRadiance.SafeRelease();
	}
};


class FReflectionTemporalState
{
public:
	FIntRect HistoryViewRect;
	FVector4f HistoryScreenPositionScaleBias;
	TRefCountPtr<IPooledRenderTarget> SpecularIndirectHistoryRT;
	TRefCountPtr<IPooledRenderTarget> NumFramesAccumulatedRT;
	TRefCountPtr<IPooledRenderTarget> ResolveVarianceHistoryRT;

	FReflectionTemporalState()
	{
		HistoryViewRect = FIntRect(0, 0, 0, 0);
		HistoryScreenPositionScaleBias = FVector4f(0, 0, 0, 0);
	}

	void SafeRelease()
	{
		SpecularIndirectHistoryRT.SafeRelease();
		NumFramesAccumulatedRT.SafeRelease();
		ResolveVarianceHistoryRT.SafeRelease();
	}
};

class FLumenVoxelLightingClipmapState
{
public:
	FIntVector FullUpdateOriginInTiles = FIntVector(0);
	FIntVector LastPartialUpdateOriginInTiles = FIntVector(0);
	FIntVector ScrollOffsetInTiles = FIntVector(0);

	FVector Center = FVector(0.0f);
	FVector Extent = FVector(0.0f);
	FVector VoxelSize = FVector(0.0f);
	float VoxelRadius = 0.0f;
	float MeshSDFRadiusThreshold = 0.0f;
	FVector VoxelCoordToUVScale = FVector(0.0f);
	FVector VoxelCoordToUVBias = FVector(0.0f);

	TArray<FRenderBounds> PrimitiveModifiedBounds;
};

class FLumenViewState
{
public:

	FScreenProbeGatherTemporalState ScreenProbeGatherState;
	FReflectionTemporalState ReflectionState;
	TRefCountPtr<IPooledRenderTarget> DepthHistoryRT;

	// Voxel clipmaps
	int32 NumClipmapLevels = 0;
	FLumenVoxelLightingClipmapState VoxelLightingClipmapState[MaxVoxelClipmapLevels];
	TRefCountPtr<IPooledRenderTarget> VoxelLighting;
	TRefCountPtr<IPooledRenderTarget> VoxelVisBuffer;
	const FScene* VoxelVisBufferCachedScene = nullptr;
	FIntVector VoxelGridResolution;

	// Translucency
	TRefCountPtr<IPooledRenderTarget> TranslucencyVolume0;
	TRefCountPtr<IPooledRenderTarget> TranslucencyVolume1;

	void SafeRelease()
	{
		ScreenProbeGatherState.SafeRelease();
		ReflectionState.SafeRelease();
		DepthHistoryRT.SafeRelease();

		VoxelLighting.SafeRelease();
		VoxelVisBuffer.SafeRelease();
		TranslucencyVolume0.SafeRelease();
		TranslucencyVolume1.SafeRelease();
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardPassUniformParameters, RENDERER_API)
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER(float, OverrideDiffuseReflectivity)
	SHADER_PARAMETER(float, DiffuseReflectivityOverride)
END_GLOBAL_SHADER_PARAMETER_STRUCT()