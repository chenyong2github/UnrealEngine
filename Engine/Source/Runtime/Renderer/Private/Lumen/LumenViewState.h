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

	int32 TraceCards;
	float CardTraceDistance;
	float SurfaceBias;
	int32 VoxelTracingMode;

	inline bool operator==(const FLumenGatherCvarState& Rhs)
	{
		return TraceCards == Rhs.TraceCards &&
			CardTraceDistance == Rhs.CardTraceDistance &&
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
	TRefCountPtr<IPooledRenderTarget> ImportanceSamplingHistoryDownsampledDepth;
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
		ImportanceSamplingHistoryDownsampledDepth.SafeRelease();
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

class FLumenViewState
{
public:

	FScreenProbeGatherTemporalState ScreenProbeGatherState;
	FReflectionTemporalState ReflectionState;

	// Voxel clipmaps
	TRefCountPtr<IPooledRenderTarget> VoxelLighting;
	FIntVector VoxelGridResolution;
	int32 NumClipmapLevels;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldToUVScale;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldToUVBias;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldCenter;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldExtent;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldSamplingExtent;
	TStaticArray<FVector4, MaxVoxelClipmapLevels> ClipmapVoxelSizeAndRadius;

	// Translucency
	TRefCountPtr<IPooledRenderTarget> TranslucencyVolume0;
	TRefCountPtr<IPooledRenderTarget> TranslucencyVolume1;

	void SafeRelease()
	{
		ScreenProbeGatherState.SafeRelease();
		ReflectionState.SafeRelease();

		VoxelLighting.SafeRelease();
		TranslucencyVolume0.SafeRelease();
		TranslucencyVolume1.SafeRelease();
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardPassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, SceneTextures)
	SHADER_PARAMETER(float, OverrideDiffuseReflectivity)
	SHADER_PARAMETER(float, DiffuseReflectivityOverride)
END_GLOBAL_SHADER_PARAMETER_STRUCT()