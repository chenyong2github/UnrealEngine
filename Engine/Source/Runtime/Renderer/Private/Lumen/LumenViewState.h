// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenViewState.h:
=============================================================================*/

#pragma once

const static int32 NumLumenDiffuseIndirectTextures = 2;
// Must match shader
const static int32 MaxVoxelClipmapLevels = 8;

class FLumenViewState
{
public:

	// Diffuse indirect history
	FIntRect DiffuseIndirectHistoryViewRect;
	FVector4 DiffuseIndirectHistoryScreenPositionScaleBias;
	TRefCountPtr<IPooledRenderTarget> DiffuseIndirectHistoryRT[NumLumenDiffuseIndirectTextures];
	TRefCountPtr<IPooledRenderTarget> DownsampledDepthHistoryRT;
	TRefCountPtr<IPooledRenderTarget> HistoryConvergenceStateRT;

	// Voxel clipmaps
	TRefCountPtr<IPooledRenderTarget> VoxelLighting;
	TRefCountPtr<IPooledRenderTarget> MergedVoxelLighting;
	TRefCountPtr<IPooledRenderTarget> VoxelDistanceField;
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
		DiffuseIndirectHistoryRT[0].SafeRelease();
		DiffuseIndirectHistoryRT[1].SafeRelease();
		DownsampledDepthHistoryRT.SafeRelease();
		HistoryConvergenceStateRT.SafeRelease();
		VoxelLighting.SafeRelease();
		MergedVoxelLighting.SafeRelease();
		VoxelDistanceField.SafeRelease();
		TranslucencyVolume0.SafeRelease();
		TranslucencyVolume1.SafeRelease();
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardPassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, SceneTextures)
	SHADER_PARAMETER(float, OverrideDiffuseReflectivity)
	SHADER_PARAMETER(float, DiffuseReflectivityOverride)
END_GLOBAL_SHADER_PARAMETER_STRUCT()