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
	FVector4 DiffuseIndirectHistoryScreenPositionScaleBias;
	TRefCountPtr<IPooledRenderTarget> DiffuseIndirectHistoryRT[4];
	TRefCountPtr<IPooledRenderTarget> RoughSpecularIndirectHistoryRT;
	TRefCountPtr<IPooledRenderTarget> DownsampledDepthHistoryRT;
	TRefCountPtr<IPooledRenderTarget> HistoryConvergenceStateRT;
	TRefCountPtr<IPooledRenderTarget> OctahedralSolidAngleTextureRT;
	FIntRect ImportanceSamplingHistoryViewRect;
	FVector4 ImportanceSamplingHistoryScreenPositionScaleBias;
	TRefCountPtr<IPooledRenderTarget> ImportanceSamplingHistoryScreenProbeSceneDepth;
	TRefCountPtr<IPooledRenderTarget> ImportanceSamplingHistoryScreenProbeRadiance;
	FLumenGatherCvarState LumenGatherCvars;

	FScreenProbeGatherTemporalState()
	{
		DiffuseIndirectHistoryViewRect = FIntRect(0, 0, 0, 0);
		DiffuseIndirectHistoryScreenPositionScaleBias = FVector4(0, 0, 0, 0);
		ImportanceSamplingHistoryViewRect = FIntRect(0, 0, 0, 0);
		ImportanceSamplingHistoryScreenPositionScaleBias = FVector4(0, 0, 0, 0);
	}

	void SafeRelease()
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(DiffuseIndirectHistoryRT); i++)
		{
			DiffuseIndirectHistoryRT[i].SafeRelease();
		}
		
		RoughSpecularIndirectHistoryRT.SafeRelease();
		DownsampledDepthHistoryRT.SafeRelease();
		HistoryConvergenceStateRT.SafeRelease();
		OctahedralSolidAngleTextureRT.SafeRelease();
		ImportanceSamplingHistoryScreenProbeSceneDepth.SafeRelease();
		ImportanceSamplingHistoryScreenProbeRadiance.SafeRelease();
	}
};


class FReflectionTemporalState
{
public:
	FIntRect HistoryViewRect;
	FVector4 HistoryScreenPositionScaleBias;
	TRefCountPtr<IPooledRenderTarget> SpecularIndirectHistoryRT;
	
	FReflectionTemporalState()
	{
		HistoryViewRect = FIntRect(0, 0, 0, 0);
		HistoryScreenPositionScaleBias = FVector4(0, 0, 0, 0);
	}

	void SafeRelease()
	{
		SpecularIndirectHistoryRT.SafeRelease();
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

	TArray<FBox> PrimitiveModifiedBounds;
};

class FLumenViewState
{
public:

	FScreenProbeGatherTemporalState ScreenProbeGatherState;
	FReflectionTemporalState ReflectionState;

	// Voxel clipmaps
	int32 NumClipmapLevels;
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

		VoxelLighting.SafeRelease();
		VoxelVisBuffer.SafeRelease();
		TranslucencyVolume0.SafeRelease();
		TranslucencyVolume1.SafeRelease();
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardPassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER(float, OverrideDiffuseReflectivity)
	SHADER_PARAMETER(float, DiffuseReflectivityOverride)
END_GLOBAL_SHADER_PARAMETER_STRUCT()