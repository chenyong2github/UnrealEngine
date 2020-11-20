// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphResources.h"
#include "MeshPassProcessor.h"
#include "UnifiedBuffer.h"
#include "Rendering/NaniteResources.h"

class FCardRenderData;
class FVirtualShadowMapArray;
class FLumenCardPassUniformParameters;

struct FSceneTextures;
struct FDBufferTextures;

DECLARE_LOG_CATEGORY_EXTERN(LogNanite, Warning, All);

static constexpr uint32 NANITE_MAX_MATERIALS = 64;
static constexpr uint32 MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS = 12;														// must match define in NaniteDataDecode.ush
static constexpr uint32 MAX_VIEWS_PER_CULL_RASTERIZE_PASS_MASK	= ( ( 1 << MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS ) - 1 );	// must match define in NaniteDataDecode.ush
static constexpr uint32 MAX_VIEWS_PER_CULL_RASTERIZE_PASS		= ( 1 << MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS );			// must match define in NaniteDataDecode.ush

DECLARE_GPU_STAT_NAMED_EXTERN(NaniteDebug,		TEXT("Nanite Debug"));
DECLARE_GPU_STAT_NAMED_EXTERN(NaniteDepth,		TEXT("Nanite Depth"));
DECLARE_GPU_STAT_NAMED_EXTERN(NaniteEditor,		TEXT("Nanite Editor"));
DECLARE_GPU_STAT_NAMED_EXTERN(NaniteRaster,		TEXT("Nanite Raster"));
DECLARE_GPU_STAT_NAMED_EXTERN(NaniteMaterials,	TEXT("Nanite Materials"));

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteUniformParameters, )
	SHADER_PARAMETER(FIntVector4,					SOAStrides)
	SHADER_PARAMETER(FIntVector4,					MaterialConfig) // .x mode, .yz grid size, .w unused
	SHADER_PARAMETER(float,							MaterialDepth)
	SHADER_PARAMETER(uint32,						MaxNodes)
	SHADER_PARAMETER(uint32,						MaxClusters)
	SHADER_PARAMETER(uint32,						RenderFlags)
	SHADER_PARAMETER(FVector4,						RectScaleOffset) // xy: scale, zw: offset
	SHADER_PARAMETER_SRV(ByteAddressBuffer,			ClusterPageData)
	SHADER_PARAMETER_SRV(ByteAddressBuffer,			ClusterPageHeaders)
	SHADER_PARAMETER_SRV(ByteAddressBuffer,			VisibleClustersSWHW)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>,	VisibleMaterials)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint2>,		MaterialRange)
	SHADER_PARAMETER_TEXTURE(Texture2D<UlongType>,	VisBuffer64)
	SHADER_PARAMETER_TEXTURE(Texture2D<UlongType>,	DbgBuffer64)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>,		DbgBuffer32)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteVisualizeLevelInstanceParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
	SHADER_PARAMETER(FVector2D, OutputToInputScale)
	SHADER_PARAMETER(uint32, MaxClusters)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVisibleCluster>, VisibleClustersSWHW)
	SHADER_PARAMETER(FIntVector4, SOAStrides)
	SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
	SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, VisBuffer64)

	SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialHitProxyTable)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteSelectionOutlineParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
	SHADER_PARAMETER(FVector2D, OutputToInputScale)
	SHADER_PARAMETER(uint32, MaxClusters)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVisibleCluster>, VisibleClustersSWHW)
	SHADER_PARAMETER(FIntVector4, SOAStrides)
	SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageData )
	SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageHeaders )

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, VisBuffer64)

	SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialHitProxyTable)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT( FRasterParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< uint >,		OutDepthBuffer )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< UlongType >,	OutVisBuffer64 )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< UlongType >,	OutDbgBuffer64 )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< uint >,		OutDbgBuffer32 )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< uint >,		LockBuffer )
END_SHADER_PARAMETER_STRUCT()

class FNaniteCommandInfo
{
public:

	static constexpr int32 MAX_STATE_BUCKET_ID = (1 << 14) - 1; // Must match NaniteDataDecode.ush

	explicit FNaniteCommandInfo() = default;

	void SetStateBucketId(int32 InStateBucketId)
	{
		check(InStateBucketId < MAX_STATE_BUCKET_ID);
		StateBucketId = InStateBucketId;
	}

	int32 GetStateBucketId() const
	{
		check(StateBucketId < MAX_STATE_BUCKET_ID);
		return StateBucketId;
	}

	void Reset()
	{
		StateBucketId = INDEX_NONE;
	}

	uint32 GetMaterialId() const
	{
		return GetMaterialId(GetStateBucketId());
	}

	static uint32 GetMaterialId(int32 StateBucketId)
	{
		float DepthId = GetDepthId(StateBucketId);
		return *reinterpret_cast<uint32*>(&DepthId);
	}

	static float GetDepthId(int32 StateBucketId)
	{
		return float(StateBucketId + 1) / float(MAX_STATE_BUCKET_ID);
	}

private:
	// Stores the index into FScene::NaniteDrawCommands of the corresponding FMeshDrawCommand
	int32 StateBucketId = INDEX_NONE;
};

struct MeshDrawCommandKeyFuncs;

class FNaniteDrawListContext : public FMeshPassDrawListContext
{
public:
	FNaniteDrawListContext(
		FCriticalSection& InNaniteDrawCommandLock,
		FStateBucketMap& InNaniteDrawCommands
		);

	virtual FMeshDrawCommand& AddCommand(
		FMeshDrawCommand& Initializer, uint32 NumElements
		) override final;

	virtual void FinalizeCommand(
		const FMeshBatch& MeshBatch,
		int32 BatchElementIndex,
		int32 DrawPrimitiveId,
		int32 ScenePrimitiveId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode,
		FMeshDrawCommandSortKey SortKey,
		const FGraphicsMinimalPipelineStateInitializer& PipelineState,
		const FMeshProcessorShaders* ShadersForDebugging,
		FMeshDrawCommand& MeshDrawCommand
		) override final;

	FNaniteCommandInfo GetCommandInfoAndReset() 
	{ 
		FNaniteCommandInfo Ret = CommandInfo;
		CommandInfo.Reset();
		return Ret; 
	}

private:
	FCriticalSection& NaniteDrawCommandLock;
	FStateBucketMap& NaniteDrawCommands;
	FNaniteCommandInfo CommandInfo;
	FMeshDrawCommand MeshDrawCommandForStateBucketing;
};

class FNaniteShader : public FGlobalShader
{
public:
	FNaniteShader()
	{
	}

	FNaniteShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
	{
	}
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	/**
	* Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	*/
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	}
};

/** Vertex shader to draw a full screen quad at a specific depth that works on all platforms. */
class FNaniteVS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FNaniteVS);

	FNaniteVS()
	{
	}

	FNaniteVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FNaniteShader(Initializer)
	{
		NaniteUniformBuffer.Bind(Initializer.ParameterMap, TEXT("Nanite"), SPF_Mandatory);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings
		) const
	{
		ShaderBindings.Add(NaniteUniformBuffer, DrawRenderState.GetNaniteUniformBuffer());
	}

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		const FStaticFeatureLevel FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch,
		const FMeshBatchElement& BatchElement,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
	}

private:
	LAYOUT_FIELD(FShaderUniformBufferParameter, NaniteUniformBuffer);
};

class FNaniteMeshProcessor : public FMeshPassProcessor
{
public:
	FNaniteMeshProcessor(
		const FScene* InScene, 
		ERHIFeatureLevel::Type InFeatureLevel, 
		const FSceneView* InViewIfDynamicMeshCommand, 
		const FMeshPassProcessorRenderState& InDrawRenderState, 
		FMeshPassDrawListContext* InDrawListContext
		);

	virtual void AddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch, 
		uint64 BatchElementMask, 
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, 
		int32 StaticMeshId = -1
		) override final;

private:
	FMeshPassProcessorRenderState PassDrawRenderState;
};

FMeshPassProcessor* CreateNaniteMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext
	);

class FNaniteMaterialTables
{
public:
	FNaniteMaterialTables(uint32 MaxMaterials = NANITE_MAX_MATERIALS);
	~FNaniteMaterialTables();

	void Release();

	bool Begin(FRHICommandListImmediate& RHICmdList, uint32 NumPrimitives, uint32 NumPrimitiveUpdates);
	void* GetDepthTablePtr(uint32 PrimitiveIndex, uint32 EntryCount);
#if WITH_EDITOR
	void* GetHitProxyTablePtr(uint32 PrimitiveIndex, uint32 EntryCount);
#endif
	void Finish(FRHICommandListImmediate& RHICmdList);

	FRHIShaderResourceView* GetDepthTableSRV() const { return DepthTableDataBuffer.SRV; }
#if WITH_EDITOR
	FRHIShaderResourceView* GetHitProxyTableSRV() const { return HitProxyTableDataBuffer.SRV; }
#endif

private:
	uint32 MaxMaterials = 0;
	uint32 NumPrimitiveUpdates = 0;
	uint32 NumDepthTableUpdates = 0;
	uint32 NumHitProxyTableUpdates = 0;

	FScatterUploadBuffer DepthTableUploadBuffer;
	FRWByteAddressBuffer DepthTableDataBuffer;
	FScatterUploadBuffer HitProxyTableUploadBuffer;
	FRWByteAddressBuffer HitProxyTableDataBuffer;
};

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

struct FCullingPassData
{
	FRDGBufferRef CandidateClusters;
	FRDGBufferRef CandidateClustersArgs;
	FRDGBufferRef Nodes;
	FRDGBufferRef RasterizeArgsSWHW;
};

struct FPackedView
{
	FMatrix		TranslatedWorldToView;
	FMatrix		TranslatedWorldToClip;
	FMatrix		ViewToClip;
	FMatrix		ClipToWorld;
	
	FMatrix		PrevTranslatedWorldToView;
	FMatrix		PrevTranslatedWorldToClip;
	FMatrix		PrevViewToClip;
	FMatrix		PrevClipToWorld;

	FIntVector4	ViewRect;
	FVector4	ViewSizeAndInvSize;
	FVector4	ClipSpaceScaleOffset;
	FVector4	PreViewTranslation;
	FVector4	PrevPreViewTranslation;
	FVector4	WorldCameraOrigin;
	FVector4	ViewForwardAndNearPlane;
	
	FVector2D	LODScales;
	float		MinBoundsRadiusSq;
	uint32		StreamingPriorityCategory_AndFlags;
	
	FIntVector4 TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ;

	FIntVector4	HZBTestViewRect;	// In full resolution

	/**
	 * Calculates the LOD scales assuming view size and projection is already set up.
	 * TODO: perhaps more elegant/robust if this happened at construction time, and input was a non-packed NaniteView.
	 * Note: depends on the global 'GNaniteMaxPixelsPerEdge'.
	 */
	void UpdateLODScales();
};

struct FInstanceDraw
{
	uint32 InstanceId;
	uint32 ViewId;
};

struct FCullingContext
{
	uint32			DrawPassIndex;
	uint32			NumInstancesPreCull;
	uint32			RenderFlags;
	uint32			DebugFlags;
	TRefCountPtr<IPooledRenderTarget>	PrevHZB; // If non-null, HZB culling is enabled
	FIntRect		HZBBuildViewRect;
	bool			bTwoPassOcclusion;
	bool			bSupportsMultiplePasses;

	FIntVector4		SOAStrides;

	FCullingPassData	MainPass;
	FCullingPassData	PostPass;

	FRDGBufferRef	MainAndPostPassPersistentStates;
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
	FVector2D			RcpViewSize;
	FIntPoint			TextureSize;
	ERasterTechnique	RasterTechnique;
	ERasterScheduling	RasterScheduling;

	FRDGTextureRef		LockBuffer;
	FRDGTextureRef		DepthBuffer;
	FRDGTextureRef		VisBuffer64;
	FRDGTextureRef		DbgBuffer64;
	FRDGTextureRef		DbgBuffer32;

	FRasterParameters	Parameters;
};

struct FRasterResults
{
	FIntVector4		SOAStrides;
	uint32			MaxClusters;
	uint32			MaxNodes;
	uint32			RenderFlags;

	TRefCountPtr<FRDGPooledBuffer>		VisibleClustersSWHW;

	TRefCountPtr<IPooledRenderTarget>	VisBuffer64;
	TRefCountPtr<IPooledRenderTarget>	DbgBuffer64;
	TRefCountPtr<IPooledRenderTarget>	DbgBuffer32;

	TRefCountPtr<IPooledRenderTarget>	MaterialDepth;
	TRefCountPtr<IPooledRenderTarget>	NaniteMask;
	TRefCountPtr<IPooledRenderTarget>	VelocityBuffer;
};

FCullingContext	InitCullingContext(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TRefCountPtr<IPooledRenderTarget> &PrevHZB,
	const FIntRect& HZBBuildViewRect,
	bool bTwoPassOcclusion,
	bool bUpdateStreaming,
	bool bSupportsMultiplePasses,
	bool bForceHWRaster,
	bool bPrimaryContext
);

FRasterContext InitRasterContext(
	FRDGBuilder& GraphBuilder,
	FIntPoint TextureSize,
	EOutputBufferMode RasterMode = EOutputBufferMode::VisBuffer,
	bool bClearTarget = true,
	FRDGBufferSRVRef RectMinMaxBufferSRV = nullptr,
	uint32 NumRects = 0
);

struct FPackedViewParams
{
	FViewMatrices ViewMatrices;
	FViewMatrices PrevViewMatrices;
	FIntRect ViewRect;
	FIntPoint RasterContextSize;
	uint32 StreamingPriorityCategory = 0;
	float MinBoundsRadius = 0.0f;
	float LODScaleFactor = 1.0f;
	uint32 Flags = 0;

	int32 TargetLayerIndex = 0;
	int32 PrevTargetLayerIndex = INDEX_NONE;
	int32 TargetMipLevel = 0;
	int32 TargetMipCount = 1;

	FIntRect HZBTestViewRect = {0, 0, 0, 0};
};

FPackedView CreatePackedView( const FPackedViewParams& Params );

// Convenience function to pull relevant packed view parameters out of a FViewInfo
FPackedView CreatePackedViewFromViewInfo(
	const FViewInfo& View,
	FIntPoint RasterContextSize,
	uint32 Flags,
	uint32 StreamingPriorityCategory = 0,
	float MinBoundsRadius = 0.0f,
	float LODScaleFactor = 1.0f
);

struct FRasterState
{
	bool bNearClip = true;
	ERasterizerCullMode CullMode = CM_CW;
};

void CullRasterize(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
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
	FVirtualShadowMapArray &VirtualShadowMapArray,
	const TArray<FPackedView, SceneRenderingAllocator> &Views,
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState = FRasterState(),
	bool bExtractStats = false
);

void ExtractStats(
	FRDGBuilder& GraphBuilder,
	const FCullingContext& CullingContext,
	bool bVirtualTextureTarget
);

void PrintStats(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View
);

void ExtractResults(
	FRDGBuilder& GraphBuilder,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	FRasterResults& RasterResults
);

void DrawHitProxies(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FRasterResults& RasterResults,
	FRDGTextureRef HitProxyTexture,
	FRDGTextureRef HitProxyDeptTexture
);

void EmitShadowMap(
	FRDGBuilder& GraphBuilder,
	const FRasterContext& RasterContext,
	const FRDGTextureRef DepthBuffer,
	const FIntRect& SourceRect,
	const FIntPoint DestOrigin,
	const FMatrix& ProjectionMatrix,
	float DepthBias,
	bool bOrtho
);

void EmitFallbackShadowMapFromVSM(
	FRDGBuilder& GraphBuilder,
	FVirtualShadowMapArray &VirtualShadowMapArray,
	uint32 ShadowMapID,
	const FRDGTextureRef DepthBuffer,
	const FIntRect& DestRect,
	const FMatrix& ProjectionMatrix,
	float DepthBias,
	bool bOrtho
);

void EmitCubemapShadow(
	FRDGBuilder& GraphBuilder,
	const FRasterContext& RasterContext,
	const FRDGTextureRef CubemapDepthBuffer,
	const FIntRect& ViewRect,
	uint32 CubemapFaceIndex,
	bool bUseGeometryShader
);

void EmitDepthTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	TRefCountPtr<IPooledRenderTarget>& OutMaterialDepth,
	TRefCountPtr<IPooledRenderTarget>& OutNaniteMask,
	TRefCountPtr<IPooledRenderTarget>& OutVelocityBuffer
);

void DrawBasePass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FDBufferTextures& DBufferTextures,
	const FScene& Scene,
	const FViewInfo& View,
	const FRasterResults& RasterResults
);

void DrawLumenMeshCapturePass(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	FViewInfo* SharedView,
	const TArray<FCardRenderData, SceneRenderingAllocator>& CardsToRender,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	FLumenCardPassUniformParameters* PassUniformParameters,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects,
	FIntPoint ViewportSize,
	FRDGTextureRef AlbedoAtlasTexture,
	FRDGTextureRef NormalAtlasTexture,
	FRDGTextureRef EmissiveAtlasTexture,
	FRDGTextureRef DepthAtlasTexture
);

void DrawVisualization(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	const FScene& Scene,
	const FViewInfo& View,
	const FRasterResults& RasterResults
);

#if WITH_EDITOR

void GetEditorSelectionPassParameters(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FRasterResults* NaniteRasterResults,
	FNaniteSelectionOutlineParameters* OutPassParameters
);

void DrawEditorSelection(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FNaniteSelectionOutlineParameters& PassParameters
);

void GetEditorVisualizeLevelInstancePassParameters(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FRasterResults* NaniteRasterResults,
	FNaniteVisualizeLevelInstanceParameters* OutPassParameters
);

void DrawEditorVisualizeLevelInstance(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FNaniteVisualizeLevelInstanceParameters& PassParameters
);

#endif

}
