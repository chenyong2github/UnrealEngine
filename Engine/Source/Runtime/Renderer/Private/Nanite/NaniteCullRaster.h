// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"

static constexpr uint32 MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS = 12;														// Must match define in NaniteDataDecode.ush
static constexpr uint32 MAX_VIEWS_PER_CULL_RASTERIZE_PASS_MASK	= ( ( 1 << MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS ) - 1 );	// Must match define in NaniteDataDecode.ush
static constexpr uint32 MAX_VIEWS_PER_CULL_RASTERIZE_PASS		= (   1 << MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS );		// Must match define in NaniteDataDecode.ush

class FVirtualShadowMapArray;

DECLARE_GPU_STAT_NAMED_EXTERN(NaniteRaster, TEXT("Nanite Raster"));

BEGIN_SHADER_PARAMETER_STRUCT(FRasterParameters,)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>,			OutDepthBuffer)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<UlongType>,	OutVisBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<UlongType>,	OutDbgBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>,			OutDbgBuffer32)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>,			LockBuffer)
END_SHADER_PARAMETER_STRUCT()

namespace Nanite
{

enum class ERasterTechnique : uint8
{
	// Use fallback lock buffer approach without 64-bit atomics (has race conditions).
	LockBufferFallback = 0,

	// Use 64-bit atomics provided by the platform.
	PlatformAtomics = 1,

	// Use 64-bit atomics provided by Nvidia vendor extension.
	NVAtomics = 2,

	// Use 64-bit atomics provided by AMD vendor extension [Direct3D 11].
	AMDAtomicsD3D11 = 3,

	// Use 64-bit atomics provided by AMD vendor extension [Direct3D 12].
	AMDAtomicsD3D12 = 4,

	// Use 32-bit atomics for depth, no payload.
	DepthOnly = 5,

	// Add before this.
	NumTechniques
};

enum class ERasterScheduling : uint8
{
	// Only rasterize using fixed function hardware.
	HardwareOnly = 0,

	// Rasterize large triangles with hardware, small triangles with software (compute).
	HardwareThenSoftware = 1,

	// Rasterize large triangles with hardware, overlapped with rasterizing small triangles with software (compute).
	HardwareAndSoftwareOverlap = 2,
};

/**
 * Used to select raster mode when creating the context.
 */
enum class EOutputBufferMode : uint8
{
	// Default mode outputting both ID and depth
	VisBuffer,

	// Rasterize only depth to 32 bit buffer
	DepthOnly,
};

enum class EPipeline : uint8
{
	Primary,
	Shadows,
	Lumen,
	HitProxy
};

struct FSharedContext
{
	FGlobalShaderMap* ShaderMap;
	ERHIFeatureLevel::Type FeatureLevel;
	EPipeline Pipeline;
};

struct FCullingContext
{
	TRefCountPtr<IPooledRenderTarget> PrevHZB; // If non-null, HZB culling is enabled

	uint32			DrawPassIndex;
	uint32			NumInstancesPreCull;
	uint32			RenderFlags;
	uint32			DebugFlags;
	FIntRect		HZBBuildViewRect;
	bool			bTwoPassOcclusion;
	bool			bSupportsMultiplePasses;

	FIntVector4		PageConstants;

	FRDGBufferRef	MainRasterizeArgsSWHW;
	FRDGBufferRef	PostRasterizeArgsSWHW;

	FRDGBufferRef	SafeMainRasterizeArgsSWHW;
	FRDGBufferRef	SafePostRasterizeArgsSWHW;

	FRDGBufferRef	QueueState;
	FRDGBufferRef	VisibleClustersSWHW;
	FRDGBufferRef	OccludedInstances;
	FRDGBufferRef	OccludedInstancesArgs;
	FRDGBufferRef	TotalPrevDrawClustersBuffer;
	FRDGBufferRef	StreamingRequests;
	FRDGBufferRef	ViewsBuffer;
	FRDGBufferRef	InstanceDrawsBuffer;
	FRDGBufferRef	StatsBuffer;
};

struct FRasterContext
{
	FVector2f			RcpViewSize;
	FIntPoint			TextureSize;
	ERasterTechnique	RasterTechnique;
	ERasterScheduling	RasterScheduling;

	FRasterParameters	Parameters;

	FRDGTextureRef		LockBuffer;
	FRDGTextureRef		DepthBuffer;
	FRDGTextureRef		VisBuffer64;
	FRDGTextureRef		DbgBuffer64;
	FRDGTextureRef		DbgBuffer32;

	uint32				VisualizeModeBitMask;
	bool				VisualizeActive;
};

struct FRasterResults
{
	FIntVector4		PageConstants;
	uint32			MaxVisibleClusters;
	uint32			MaxNodes;
	uint32			RenderFlags;

	FRDGBufferRef	ViewsBuffer{};
	FRDGBufferRef	VisibleClustersSWHW{};

	FRDGTextureRef	VisBuffer64{};
	FRDGTextureRef	DbgBuffer64{};
	FRDGTextureRef	DbgBuffer32{};

	FRDGTextureRef	MaterialDepth{};
	FRDGTextureRef	MaterialResolve{};

	TArray<FVisualizeResult, TInlineAllocator<32>> Visualizations;
};

FCullingContext	InitCullingContext(
	FRDGBuilder& GraphBuilder,
	const FSharedContext& SharedContext,
	const FScene& Scene,
	const TRefCountPtr<IPooledRenderTarget> &PrevHZB,
	const FIntRect& HZBBuildViewRect,
	bool bTwoPassOcclusion,
	bool bUpdateStreaming,
	bool bSupportsMultiplePasses,
	bool bForceHWRaster,
	bool bPrimaryContext,
	bool bDrawOnlyVSMInvalidatingGeometry = false,
	bool bIgnoreVisibleInRaster = false);

FRasterContext InitRasterContext(
	FRDGBuilder& GraphBuilder,
	const FSharedContext& SharedContext,
	FIntPoint TextureSize,
	bool bVisualize,
	EOutputBufferMode RasterMode = EOutputBufferMode::VisBuffer,
	bool bClearTarget = true,
	FRDGBufferSRVRef RectMinMaxBufferSRV = nullptr,
	uint32 NumRects = 0,
	FRDGTextureRef ExternalDepthBuffer = nullptr
);

struct FRasterState
{
	bool bNearClip = true;
	ERasterizerCullMode CullMode = CM_CW;
};

void CullRasterize(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& SceneView,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	const FSharedContext& SharedContext,
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState = FRasterState(),
	const TArray<FInstanceDraw, SceneRenderingAllocator>* OptionalInstanceDraws = nullptr,
	bool bExtractStats = false
);

/**
 * Rasterize to a virtual shadow map (set) defined by the Views array, each view must have a virtual shadow map index set and the 
 * virtual shadow map physical memory mapping must have been defined. Note that the physical backing is provided by the raster context.
 * parameter Views - One view per layer to rasterize, the 'TargetLayerIdX_AndMipLevelY.X' must be set to the correct layer.
 */
void CullRasterize(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& SceneView,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	uint32 NumPrimaryViews,	// Number of non-mip views
	const FSharedContext& SharedContext,
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState = FRasterState(),
	const TArray<FInstanceDraw, SceneRenderingAllocator>* OptionalInstanceDraws = nullptr,
	// VirtualShadowMapArray is the supplier of virtual to physical translation, probably could abstract this a bit better,
	FVirtualShadowMapArray* VirtualShadowMapArray = nullptr,
	bool bExtractStats = false
);

} // namespace Nanite
