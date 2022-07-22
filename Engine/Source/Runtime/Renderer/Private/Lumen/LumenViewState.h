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
	int32 DirectLighting;

	inline bool operator==(const FLumenGatherCvarState& Rhs)
	{
		return TraceMeshSDFs == Rhs.TraceMeshSDFs &&
			MeshSDFTraceDistance == Rhs.MeshSDFTraceDistance &&
			SurfaceBias == Rhs.SurfaceBias &&
			VoxelTracingMode == Rhs.VoxelTracingMode &&
			DirectLighting == Rhs.DirectLighting;
	}
};

class FScreenProbeGatherTemporalState
{
public:
	FIntRect DiffuseIndirectHistoryViewRect;
	FVector4f DiffuseIndirectHistoryScreenPositionScaleBias;
	TRefCountPtr<IPooledRenderTarget> DiffuseIndirectHistoryRT;
	TRefCountPtr<IPooledRenderTarget> RoughSpecularIndirectHistoryRT;
	TRefCountPtr<IPooledRenderTarget> NumFramesAccumulatedRT;
	TRefCountPtr<IPooledRenderTarget> FastUpdateModeHistoryRT;
	TRefCountPtr<IPooledRenderTarget> NormalHistoryRT;
	TRefCountPtr<IPooledRenderTarget> BSDFTileHistoryRT;
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
		DiffuseIndirectHistoryRT.SafeRelease();
		RoughSpecularIndirectHistoryRT.SafeRelease();
		NumFramesAccumulatedRT.SafeRelease();
		FastUpdateModeHistoryRT.SafeRelease();
		NormalHistoryRT.SafeRelease();
		BSDFTileHistoryRT.SafeRelease();
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
	TRefCountPtr<IPooledRenderTarget> BSDFTileHistoryRT;

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
		BSDFTileHistoryRT.SafeRelease();
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


class FRadianceCacheClipmap
{
public:
	/** World space bounds. */
	FVector Center;
	float Extent;

	FVector ProbeCoordToWorldCenterBias;
	float ProbeCoordToWorldCenterScale;

	FVector WorldPositionToProbeCoordBias;
	float WorldPositionToProbeCoordScale;

	float ProbeTMin;

	/** Offset applied to UVs so that only new or dirty areas of the volume texture have to be updated. */
	FVector VolumeUVOffset;

	/* Distance between two probes. */
	float CellSize;
};

class FRadianceCacheState
{
public:
	FRadianceCacheState()
	{}

	TArray<FRadianceCacheClipmap> Clipmaps;

	float ClipmapWorldExtent = 0.0f;
	float ClipmapDistributionBase = 0.0f;

	/** Clipmaps of probe indexes, used to lookup the probe index for a world space position. */
	TRefCountPtr<IPooledRenderTarget> RadianceProbeIndirectionTexture;

	TRefCountPtr<IPooledRenderTarget> RadianceProbeAtlasTexture;
	/** Texture containing radiance cache probes, ready for sampling with bilinear border. */
	TRefCountPtr<IPooledRenderTarget> FinalRadianceAtlas;
	TRefCountPtr<IPooledRenderTarget> FinalIrradianceAtlas;
	TRefCountPtr<IPooledRenderTarget> ProbeOcclusionAtlas;

	TRefCountPtr<IPooledRenderTarget> DepthProbeAtlasTexture;

	TRefCountPtr<FRDGPooledBuffer> ProbeAllocator;
	TRefCountPtr<FRDGPooledBuffer> ProbeFreeListAllocator;
	TRefCountPtr<FRDGPooledBuffer> ProbeFreeList;
	TRefCountPtr<FRDGPooledBuffer> ProbeLastUsedFrame;
	TRefCountPtr<FRDGPooledBuffer> ProbeLastTracedFrame;
	TRefCountPtr<FRDGPooledBuffer> ProbeWorldOffset;
	TRefCountPtr<IPooledRenderTarget> OctahedralSolidAngleTextureRT;

	void ReleaseTextures()
	{
		RadianceProbeIndirectionTexture.SafeRelease();
		RadianceProbeAtlasTexture.SafeRelease();
		FinalRadianceAtlas.SafeRelease();
		FinalIrradianceAtlas.SafeRelease();
		ProbeOcclusionAtlas.SafeRelease();
		DepthProbeAtlasTexture.SafeRelease();
		ProbeAllocator.SafeRelease();
		ProbeFreeListAllocator.SafeRelease();
		ProbeFreeList.SafeRelease();
		ProbeLastUsedFrame.SafeRelease();
		ProbeLastTracedFrame.SafeRelease();
		ProbeWorldOffset.SafeRelease();
	}
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
	TRefCountPtr<FRDGPooledBuffer>     VoxelVisBuffer;
	const FScene* VoxelVisBufferCachedScene = nullptr;
	FIntVector VoxelGridResolution;

	// Translucency
	TRefCountPtr<IPooledRenderTarget> TranslucencyVolume0;
	TRefCountPtr<IPooledRenderTarget> TranslucencyVolume1;

	FRadianceCacheState RadianceCacheState;
	FRadianceCacheState TranslucencyVolumeRadianceCacheState;

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
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
