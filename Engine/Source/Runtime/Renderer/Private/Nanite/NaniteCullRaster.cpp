// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteCullRaster.h"
#include "NaniteVisualizationData.h"
#include "RHI.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "GPUScene.h"
#include "RendererModule.h"
#include "Rendering/NaniteStreamingManager.h"
#include "ComponentRecreateRenderStateContext.h"
#include "VirtualShadowMaps/VirtualShadowMapCacheManager.h"
#include "SceneTextureReductions.h"
#include "RenderGraphUtils.h"

DEFINE_GPU_STAT(NaniteRaster);

DECLARE_DWORD_COUNTER_STAT(TEXT("CullingContexts"), STAT_NaniteCullingContexts, STATGROUP_Nanite);

DECLARE_GPU_STAT_NAMED(NaniteInstanceCull, TEXT("Nanite Instance Cull"));
DECLARE_GPU_STAT_NAMED(NaniteInstanceCullVSM, TEXT("Nanite Instance Cull VSM"));
DECLARE_GPU_STAT_NAMED(NaniteClusterCull, TEXT("Nanite Cluster Cull"));

#define CULLING_PASS_NO_OCCLUSION		0
#define CULLING_PASS_OCCLUSION_MAIN		1
#define CULLING_PASS_OCCLUSION_POST		2
#define CULLING_PASS_EXPLICIT_LIST		3

#define RENDER_FLAG_HAVE_PREV_DRAW_DATA				0x1
#define RENDER_FLAG_FORCE_HW_RASTER					0x2
#define RENDER_FLAG_PRIMITIVE_SHADER				0x4
#define RENDER_FLAG_MESH_SHADER						0x8
#define RENDER_FLAG_OUTPUT_STREAMING_REQUESTS		0x10
#define RENDER_FLAG_REVERSE_CULLING					0x20
#define RENDER_FLAG_IGNORE_VISIBLE_IN_RASTER		0x40

// Only available with the DEBUG_FLAGS permutation active.
#define DEBUG_FLAG_WRITE_STATS						0x1
#define DEBUG_FLAG_DISABLE_CULL_HZB_BOX				0x2
#define DEBUG_FLAG_DISABLE_CULL_HZB_SPHERE			0x4
#define DEBUG_FLAG_DISABLE_CULL_FRUSTUM_BOX			0x8
#define DEBUG_FLAG_DISABLE_CULL_FRUSTUM_SPHERE		0x10
#define DEBUG_FLAG_DRAW_ONLY_VSM_INVALIDATING		0x20

static_assert(1 + NUM_CULLING_FLAG_BITS + MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS + MAX_INSTANCES_BITS + MAX_GPU_PAGES_BITS + MAX_CLUSTERS_PER_PAGE_BITS <= 64, "FVisibleCluster fields don't fit in 64bits");

static_assert(1 + NUM_CULLING_FLAG_BITS + MAX_INSTANCES_BITS <= 32, "FCandidateNode.x fields don't fit in 32bits");
static_assert(1 + MAX_NODES_PER_PRIMITIVE_BITS + MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS <= 32, "FCandidateNode.y fields don't fit in 32bits");
static_assert(1 + MAX_BVH_NODES_PER_GROUP <= 32, "FCandidateNode.z fields don't fit in 32bits");

int32 GNaniteAsyncRasterization = 1;
static FAutoConsoleVariableRef CVarNaniteEnableAsyncRasterization(
	TEXT("r.Nanite.AsyncRasterization"),
	GNaniteAsyncRasterization,
	TEXT("")
);

int32 GNaniteAtomicRasterization = 1;
FAutoConsoleVariableRef CVarNaniteEnableAtomicRasterization(
	TEXT("r.Nanite.AtomicRasterization"),
	GNaniteAtomicRasterization,
	TEXT("")
);

int32 GNaniteComputeRasterization = 1;
static FAutoConsoleVariableRef CVarNaniteComputeRasterization(
	TEXT("r.Nanite.ComputeRasterization"),
	GNaniteComputeRasterization,
	TEXT("")
);

#if PLATFORM_WINDOWS
// TODO: Off by default on Windows due to lack of atomic64 vendor extension interop.
int32 GNaniteMeshShaderRasterization = 0;
#else
int32 GNaniteMeshShaderRasterization = 1;
#endif
FAutoConsoleVariableRef CVarNaniteMeshShaderRasterization(
	TEXT("r.Nanite.MeshShaderRasterization"),
	GNaniteMeshShaderRasterization,
	TEXT("")
);

// TODO: Temporary workaround for broken raster on some platforms
// Support disabling mesh shader raster for VSMs
int32 GNaniteVSMMeshShaderRasterization = 0;
FAutoConsoleVariableRef CVarNaniteVSMMeshShaderRasterization(
	TEXT("r.Nanite.VSMMeshShaderRasterization"),
	GNaniteVSMMeshShaderRasterization,
	TEXT("")
);

int32 GNanitePrimShaderRasterization = 1;
FAutoConsoleVariableRef CVarNanitePrimShaderRasterization(
	TEXT("r.Nanite.PrimShaderRasterization"),
	GNanitePrimShaderRasterization,
	TEXT("")
);

int32 GNaniteAutoShaderCulling = 0;
FAutoConsoleVariableRef CVarNaniteAutoShaderCulling(
	TEXT("r.Nanite.AutoShaderCulling"),
	GNaniteAutoShaderCulling,
	TEXT("")
);

float GNaniteMaxPixelsPerEdge = 1.0f;
FAutoConsoleVariableRef CVarNaniteMaxPixelsPerEdge(
	TEXT("r.Nanite.MaxPixelsPerEdge"),
	GNaniteMaxPixelsPerEdge,
	TEXT("")
	);

int32 GNaniteImposterMaxPixels = 5;
FAutoConsoleVariableRef CVarNaniteImposterMaxPixels(
	TEXT("r.Nanite.ImposterMaxPixels"),
	GNaniteImposterMaxPixels,
	TEXT("")
);

float GNaniteMinPixelsPerEdgeHW = 18.0f;
FAutoConsoleVariableRef CVarNaniteMinPixelsPerEdgeHW(
	TEXT("r.Nanite.MinPixelsPerEdgeHW"),
	GNaniteMinPixelsPerEdgeHW,
	TEXT("")
);

// TODO: WIP
int32 GNaniteMSInterp = 0;
static FAutoConsoleVariableRef CVarNaniteMSInterp(
	TEXT("r.Nanite.MSInterp"),
	GNaniteMSInterp,
	TEXT("")
);

// Specifies if Nanite should require atomic64 support, or fallback to traditional mesh rendering using the proxies.
// 0: Nanite will run without atomic support, but use the lockbuffer fallback, with known race conditions and corruption. (unshippable, but useful for debugging and platform bring-up).
// 1: Nanite will not run without atomic support, instead causing legacy scene proxies to be created instead.
int32 GNaniteRequireAtomic64Support = 1;
FAutoConsoleVariableRef CVarNaniteRequireAtomic64Support(
	TEXT("r.Nanite.RequireAtomic64Support"),
	GNaniteRequireAtomic64Support,
	TEXT(""),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	})
);

int32 GNaniteClusterPerPage = 1;
static FAutoConsoleVariableRef CVarNaniteClusterPerPage(
	TEXT("r.Nanite.ClusterPerPage"),
	GNaniteClusterPerPage,
	TEXT("")
);

int32 GNaniteBoxCullingHZB = 1;
static FAutoConsoleVariableRef CVarNaniteBoxCullingHZB(
	TEXT("r.Nanite.BoxCullingHZB"),
	GNaniteBoxCullingHZB,
	TEXT("")
);

int32 GNaniteBoxCullingFrustum = 1;
static FAutoConsoleVariableRef CVarNaniteBoxCullingFrustum(
	TEXT("r.Nanite.BoxCullingFrustum"),
	GNaniteBoxCullingFrustum,
	TEXT("")
);

int32 GNaniteSphereCullingHZB = 1;
static FAutoConsoleVariableRef CVarNaniteSphereCullingHZB(
	TEXT("r.Nanite.SphereCullingHZB"),
	GNaniteSphereCullingHZB,
	TEXT("")
);

int32 GNaniteSphereCullingFrustum = 1;
static FAutoConsoleVariableRef CVarNaniteSphereCullingFrustum(
	TEXT("r.Nanite.SphereCullingFrustum"),
	GNaniteSphereCullingFrustum,
	TEXT("")
);

static TAutoConsoleVariable<int32> CVarLargePageRectThreshold(
	TEXT("r.Nanite.LargePageRectThreshold"),
	128,
	TEXT("Threshold for the size in number of virtual pages overlapped of a candidate cluster to be recorded as large in the stats."),
	ECVF_RenderThreadSafe
);

int32 GNaniteDisocclusionHack = 0;
static FAutoConsoleVariableRef CVarNaniteDisocclusionHack(
	TEXT("r.Nanite.DisocclusionHack"),
	GNaniteDisocclusionHack,
	TEXT("HACK that lowers LOD level of disoccluded instances to mitigate performance spikes"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarCompactVSMViews(
	TEXT("r.Nanite.CompactVSMViews"),
	1,
	TEXT(""),
	ECVF_RenderThreadSafe
);

extern int32 GNaniteShowStats;

static bool UseMeshShader(Nanite::EPipeline Pipeline)
{
	// We require tier1 support to utilize primitive attributes
	const bool bSupported = GNaniteMeshShaderRasterization != 0 && GRHISupportsMeshShadersTier1;
	return bSupported && (GNaniteVSMMeshShaderRasterization != 0 || Pipeline != Nanite::EPipeline::Shadows);
}

static bool UsePrimitiveShader()
{
	return GNanitePrimShaderRasterization != 0 && GRHISupportsPrimitiveShaders;
}

struct FCompactedViewInfo
{
	uint32 StartOffset;
	uint32 NumValidViews;
};

BEGIN_SHADER_PARAMETER_STRUCT( FCullingParameters, )
	SHADER_PARAMETER( FIntVector4,	PageConstants )
	SHADER_PARAMETER( uint32,		MaxCandidateClusters )
	SHADER_PARAMETER( uint32,		MaxVisibleClusters )
	SHADER_PARAMETER( uint32,		RenderFlags )
	SHADER_PARAMETER( uint32,		DebugFlags )
	SHADER_PARAMETER( uint32,		NumViews )
	SHADER_PARAMETER( uint32,		NumPrimaryViews )
	SHADER_PARAMETER( float,		DisocclusionLodScaleFactor )

	SHADER_PARAMETER( FVector2f,	HZBSize )

	SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	HZBTexture )
	SHADER_PARAMETER_SAMPLER( SamplerState,		HZBSampler )
	
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FCompactedViewInfo >, CompactedViewInfo)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, CompactedViewsAllocation)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT( FGPUSceneParameters, )
	SHADER_PARAMETER_SRV( StructuredBuffer<float4>,	GPUSceneInstanceSceneData)
	SHADER_PARAMETER_SRV( StructuredBuffer<float4>,	GPUSceneInstancePayloadData)
	SHADER_PARAMETER_SRV( StructuredBuffer<float4>,	GPUScenePrimitiveSceneData)
	SHADER_PARAMETER( uint32,						GPUSceneFrameNumber)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT( FVirtualTargetParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER( FVirtualShadowMapUniformParameters, VirtualShadowMap )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >, PageFlags )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >, HPageFlags )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint4 >, PageRectBounds )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,	ShadowHZBPageTable )
END_SHADER_PARAMETER_STRUCT()

class FRasterTechnique
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters, int32 RasterTechnique)
	{
		if (RasterTechnique == int32(Nanite::ERasterTechnique::PlatformAtomics) &&
			!FDataDrivenShaderPlatformInfo::GetSupportsUInt64ImageAtomics(Parameters.Platform))
		{
			// Only some platforms support native 64-bit atomics.
			return false;
		}

		if ((RasterTechnique == int32(Nanite::ERasterTechnique::NVAtomics) ||
			 RasterTechnique == int32(Nanite::ERasterTechnique::AMDAtomicsD3D11) ||
			 RasterTechnique == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
			 && !FDataDrivenShaderPlatformInfo::GetRequiresVendorExtensionsForAtomics(Parameters.Platform))
		{
			// Only supporting vendor extensions on PC D3D SM5+
			return false;
		}

		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment, int32 RasterTechnique)
	{
		if (RasterTechnique == int32(Nanite::ERasterTechnique::NVAtomics) ||
			RasterTechnique == int32(Nanite::ERasterTechnique::AMDAtomicsD3D11) ||
			RasterTechnique == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
		{
			// Need to force optimization for driver injection to work correctly.
			// https://developer.nvidia.com/unlocking-gpu-intrinsics-hlsl
			// https://gpuopen.com/gcn-shader-extensions-for-direct3d-and-vulkan/
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}

		if (RasterTechnique == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
		{
			// Force shader model 6.0+
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		}
	}
};

class FInstanceCull_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInstanceCull_CS );
	SHADER_USE_PARAMETER_STRUCT( FInstanceCull_CS, FNaniteShader );

	class FCullingPassDim : SHADER_PERMUTATION_SPARSE_INT("CULLING_PASS", CULLING_PASS_NO_OCCLUSION, CULLING_PASS_OCCLUSION_MAIN, CULLING_PASS_OCCLUSION_POST, CULLING_PASS_EXPLICIT_LIST);
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FDebugFlagsDim : SHADER_PERMUTATION_BOOL("DEBUG_FLAGS");
	class FRasterTechniqueDim : SHADER_PERMUTATION_INT("RASTER_TECHNIQUE", (int32)Nanite::ERasterTechnique::NumTechniques);
	using FPermutationDomain = TShaderPermutationDomain<FCullingPassDim, FMultiViewDim, FNearClipDim, FDebugFlagsDim, FRasterTechniqueDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!DoesPlatformSupportNanite(Parameters.Platform))
		{
			return false;
		}
		
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		
		if( !FRasterTechnique::ShouldCompilePermutation( Parameters, PermutationVector.Get<FRasterTechniqueDim>() ) )
			return false;

		return FNaniteShader::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FRasterTechnique::ModifyCompilationEnvironment(Parameters, OutEnvironment, PermutationVector.Get<FRasterTechniqueDim>());

		FVirtualShadowMapArray::SetShaderDefines( OutEnvironment );	// Still needed for shader to compile

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( uint32, NumInstances )
		SHADER_PARAMETER( uint32, MaxNodes )
		SHADER_PARAMETER( int32,  ImposterMaxPixels )
		SHADER_PARAMETER( int32,  OnlyCastShadowsPrimitives )
		
		SHADER_PARAMETER_STRUCT_INCLUDE( FCullingParameters, CullingParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE( FRasterParameters, RasterParameters )

		SHADER_PARAMETER_SRV( ByteAddressBuffer, ImposterAtlas )
		
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FInstanceDraw >, InInstanceDraws )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer, OutMainAndPostNodesAndClusterBatches )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FInstanceDraw >, OutOccludedInstances )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FQueueState >, OutQueueState )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OutOccludedInstancesArgs )

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FNaniteStats>, OutStatsBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, InOccludedInstancesArgs )

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)

	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInstanceCull_CS, "/Engine/Private/Nanite/InstanceCulling.usf", "InstanceCull", SF_Compute);


class FCompactViewsVSM_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FCompactViewsVSM_CS);
	SHADER_USE_PARAMETER_STRUCT(FCompactViewsVSM_CS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("CULLING_PASS"), CULLING_PASS_NO_OCCLUSION);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCullingParameters, CullingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUSceneParameters, GPUSceneParameters)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FPackedNaniteView >, CompactedViewsOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FCompactedViewInfo >, CompactedViewInfoOut)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, CompactedViewsAllocationOut)
		//SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >, PageRectBounds)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualTargetParameters, VirtualShadowMap)
		END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCompactViewsVSM_CS, "/Engine/Private/Nanite/InstanceCulling.usf", "CompactViewsVSM_CS", SF_Compute);


class FInstanceCullVSM_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInstanceCullVSM_CS );
	SHADER_USE_PARAMETER_STRUCT( FInstanceCullVSM_CS, FNaniteShader );

	class FNearClipDim : SHADER_PERMUTATION_BOOL( "NEAR_CLIP" );
	class FDebugFlagsDim : SHADER_PERMUTATION_BOOL( "DEBUG_FLAGS" );
	class FUseCompactedViewsDim : SHADER_PERMUTATION_BOOL( "USE_COMPACTED_VIEWS" );

	using FPermutationDomain = TShaderPermutationDomain<FNearClipDim, FDebugFlagsDim, FUseCompactedViewsDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FNaniteShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );

		FVirtualShadowMapArray::SetShaderDefines( OutEnvironment );

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine( TEXT( "USE_GLOBAL_GPU_SCENE_DATA" ), 1 );
		OutEnvironment.SetDefine( TEXT( "NANITE_MULTI_VIEW" ), 1 );
		OutEnvironment.SetDefine( TEXT( "CULLING_PASS" ), CULLING_PASS_NO_OCCLUSION );
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( uint32, NumInstances )
		SHADER_PARAMETER( uint32, MaxNodes )
		
		SHADER_PARAMETER_STRUCT_INCLUDE( FCullingParameters, CullingParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer, OutMainAndPostNodesAndClusterBatches )
	
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FQueueState >, OutQueueState )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer<FNaniteStats>, OutStatsBuffer )

		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FInstanceDraw >, InOccludedInstances )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, InOccludedInstancesArgs )

		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)

		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualTargetParameters, VirtualShadowMap )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER( FInstanceCullVSM_CS, "/Engine/Private/Nanite/InstanceCulling.usf", "InstanceCullVSM", SF_Compute );


class FPersistentClusterCull_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FPersistentClusterCull_CS );
	SHADER_USE_PARAMETER_STRUCT( FPersistentClusterCull_CS, FNaniteShader );

	class FCullingPassDim : SHADER_PERMUTATION_SPARSE_INT("CULLING_PASS", CULLING_PASS_NO_OCCLUSION, CULLING_PASS_OCCLUSION_MAIN, CULLING_PASS_OCCLUSION_POST);
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	class FDebugFlagsDim : SHADER_PERMUTATION_BOOL("DEBUG_FLAGS");

	using FPermutationDomain = TShaderPermutationDomain<FCullingPassDim, FMultiViewDim, FNearClipDim, FVirtualTextureTargetDim, FClusterPerPageDim, FDebugFlagsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE( FCullingParameters, CullingParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters)

		SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageData )
		SHADER_PARAMETER_SRV( ByteAddressBuffer,				HierarchyBuffer )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FUintVector2 >,		InTotalPrevDrawClusters )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >,						OffsetClustersArgsSWHW )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FQueueState >,		QueueState )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					MainAndPostNodesAndClusterBatches )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					MainAndPostCandididateClusters )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					OutVisibleClustersSWHW )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer<FStreamingRequest>,	OutStreamingRequests )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, VisibleClustersArgsSWHW )

		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualTargetParameters, VirtualShadowMap )

		SHADER_PARAMETER(uint32,												MaxNodes)
		SHADER_PARAMETER(uint32,												LargePageRectThreshold)
		SHADER_PARAMETER(uint32,												StreamingRequestsBufferVersion)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FNaniteStats>, OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!DoesPlatformSupportNanite(Parameters.Platform))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if( PermutationVector.Get<FVirtualTextureTargetDim>() &&
			!PermutationVector.Get<FMultiViewDim>() )
		{
			return false;
		}

		if( PermutationVector.Get<FClusterPerPageDim>() &&
			!PermutationVector.Get<FVirtualTextureTargetDim>() )
		{
			return false;
		}

		return FNaniteShader::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);

		// The routing requires access to page table data structures, only for 'VIRTUAL_TEXTURE_TARGET' really...
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FPersistentClusterCull_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "PersistentClusterCull", SF_Compute);

class FInitClusterBatches_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInitClusterBatches_CS );
	SHADER_USE_PARAMETER_STRUCT( FInitClusterBatches_CS, FNaniteShader );

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,	OutMainAndPostNodesAndClusterBatches )
		SHADER_PARAMETER( uint32,								MaxCandidateClusters )
		SHADER_PARAMETER( uint32,								MaxNodes )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitClusterBatches_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "InitClusterBatches", SF_Compute);

class FInitCandidateNodes_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInitCandidateNodes_CS );
	SHADER_USE_PARAMETER_STRUCT( FInitCandidateNodes_CS, FNaniteShader );

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,	OutMainAndPostNodesAndClusterBatches )
		SHADER_PARAMETER( uint32,								MaxCandidateClusters )
		SHADER_PARAMETER( uint32,								MaxNodes )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitCandidateNodes_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "InitCandidateNodes", SF_Compute);

class FInitArgs_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInitArgs_CS );
	SHADER_USE_PARAMETER_STRUCT( FInitArgs_CS, FNaniteShader );

	class FOcclusionCullingDim : SHADER_PERMUTATION_BOOL( "OCCLUSION_CULLING" );
	class FDrawPassIndexDim : SHADER_PERMUTATION_INT( "DRAW_PASS_INDEX", 3 );	// 0: no, 1: set, 2: add
	using FPermutationDomain = TShaderPermutationDomain<FOcclusionCullingDim, FDrawPassIndexDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER(uint32, RenderFlags)

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FQueueState >,		OutQueueState )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FUintVector2 >,	InOutTotalPrevDrawClusters )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >,						InOutMainPassRasterizeArgsSWHW )
		
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OutOccludedInstancesArgs )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, InOutPostPassRasterizeArgsSWHW )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitArgs_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "InitArgs", SF_Compute);

class FCalculateSafeRasterizerArgs_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FCalculateSafeRasterizerArgs_CS);
	SHADER_USE_PARAMETER_STRUCT(FCalculateSafeRasterizerArgs_CS, FNaniteShader);

	class FHasPrevDrawData : SHADER_PERMUTATION_BOOL("HAS_PREV_DRAW_DATA");
	class FIsPostPass : SHADER_PERMUTATION_BOOL("IS_POST_PASS");
	using FPermutationDomain = TShaderPermutationDomain<FHasPrevDrawData, FIsPostPass>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FUintVector2 >,	InTotalPrevDrawClusters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer< uint >,						OffsetClustersArgsSWHW)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer< uint >,						InRasterizerArgsSWHW)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer< uint >,					OutSafeRasterizerArgsSWHW)

		SHADER_PARAMETER(uint32,											MaxVisibleClusters)
		SHADER_PARAMETER(uint32,											RenderFlags)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCalculateSafeRasterizerArgs_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "CalculateSafeRasterizerArgs", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT( FRasterizePassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters )
	SHADER_PARAMETER_STRUCT_INCLUDE( FRasterParameters, RasterParameters )

	SHADER_PARAMETER( FIntVector4,	PageConstants )
	SHADER_PARAMETER( uint32,		MaxVisibleClusters )
	SHADER_PARAMETER( uint32,		RenderFlags )
	SHADER_PARAMETER( uint32,		VisualizeModeBitMask )
	
	SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageData )

	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FPackedView >,	InViews )
	SHADER_PARAMETER_RDG_BUFFER_SRV( ByteAddressBuffer,					VisibleClustersSWHW )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FUintVector2 >,	InTotalPrevDrawClusters )

	SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, InClusterOffsetSWHW )

	RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)

	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualTargetParameters, VirtualShadowMap)
END_SHADER_PARAMETER_STRUCT()

class FMicropolyRasterizeCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FMicropolyRasterizeCS );
	SHADER_USE_PARAMETER_STRUCT( FMicropolyRasterizeCS, FNaniteShader );

	class FAddClusterOffset : SHADER_PERMUTATION_BOOL("ADD_CLUSTER_OFFSET");
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FHasPrevDrawData : SHADER_PERMUTATION_BOOL( "HAS_PREV_DRAW_DATA");
	class FRasterTechniqueDim : SHADER_PERMUTATION_INT("RASTER_TECHNIQUE", (int32)Nanite::ERasterTechnique::NumTechniques);
	class FVisualizeDim : SHADER_PERMUTATION_BOOL("VISUALIZE");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	using FPermutationDomain = TShaderPermutationDomain<FAddClusterOffset, FMultiViewDim, FHasPrevDrawData, FRasterTechniqueDim, FVisualizeDim, FNearClipDim, FVirtualTextureTargetDim, FClusterPerPageDim>;

	using FParameters = FRasterizePassParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!DoesPlatformSupportNanite(Parameters.Platform))
		{
			return false;
		}
		
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		
		if( !FRasterTechnique::ShouldCompilePermutation( Parameters, PermutationVector.Get<FRasterTechniqueDim>() ) )
			return false;

		if (PermutationVector.Get<FRasterTechniqueDim>() == (int32)Nanite::ERasterTechnique::DepthOnly &&
			PermutationVector.Get<FVisualizeDim>() )
		{
			// Visualization not supported with depth only
			return false;
		}

		if( PermutationVector.Get<FVirtualTextureTargetDim>() &&
			( !PermutationVector.Get<FMultiViewDim>() || PermutationVector.Get<FRasterTechniqueDim>() != (int32)Nanite::ERasterTechnique::DepthOnly ) )
		{
			return false;
		}

		if( PermutationVector.Get<FClusterPerPageDim>() &&
			!PermutationVector.Get<FVirtualTextureTargetDim>() )
		{
			return false;
		}

		return FNaniteShader::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FRasterTechnique::ModifyCompilationEnvironment(Parameters, OutEnvironment, PermutationVector.Get<FRasterTechniqueDim>());

		OutEnvironment.SetDefine(TEXT("SOFTWARE_RASTER"), 1);

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);

		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMicropolyRasterizeCS, "/Engine/Private/Nanite/Rasterizer.usf", "MicropolyRasterize", SF_Compute);

class FHWRasterizeVS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FHWRasterizeVS );
	SHADER_USE_PARAMETER_STRUCT( FHWRasterizeVS, FNaniteShader );

	class FRasterTechniqueDim : SHADER_PERMUTATION_INT("RASTER_TECHNIQUE", (int32)Nanite::ERasterTechnique::NumTechniques);
	class FAddClusterOffset : SHADER_PERMUTATION_BOOL("ADD_CLUSTER_OFFSET");
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FPrimShaderDim : SHADER_PERMUTATION_BOOL("NANITE_PRIM_SHADER");
	class FAutoShaderCullDim : SHADER_PERMUTATION_BOOL("NANITE_AUTO_SHADER_CULL");
	class FHasPrevDrawData : SHADER_PERMUTATION_BOOL("HAS_PREV_DRAW_DATA");
	class FVisualizeDim : SHADER_PERMUTATION_BOOL("VISUALIZE");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	using FPermutationDomain = TShaderPermutationDomain<FRasterTechniqueDim, FAddClusterOffset, FMultiViewDim, FPrimShaderDim, FAutoShaderCullDim, FHasPrevDrawData, FVisualizeDim, FNearClipDim, FVirtualTextureTargetDim, FClusterPerPageDim>;

	using FParameters = FRasterizePassParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!DoesPlatformSupportNanite(Parameters.Platform))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		
		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::PlatformAtomics) &&
			!FDataDrivenShaderPlatformInfo::GetSupportsUInt64ImageAtomics(Parameters.Platform))
		{
			// Only some platforms support native 64-bit atomics.
			return false;
		}

		if ((PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::NVAtomics) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D11) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
			&& !FDataDrivenShaderPlatformInfo::GetRequiresVendorExtensionsForAtomics(Parameters.Platform))
		{
			// Only supporting vendor extensions on PC D3D SM5+
			return false;
		}

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::DepthOnly) &&
			PermutationVector.Get<FVisualizeDim>())
		{
			// Visualization not supported with depth only
			return false;
		}

		if ((PermutationVector.Get<FPrimShaderDim>() || PermutationVector.Get<FAutoShaderCullDim>()) &&
			!FDataDrivenShaderPlatformInfo::GetSupportsPrimitiveShaders(Parameters.Platform))
		{
			// Only some platforms support primitive shaders.
			return false;
		}

		if (PermutationVector.Get<FPrimShaderDim>() && PermutationVector.Get<FAutoShaderCullDim>())
		{
			// Mutually exclusive.
			return false;
		}

		if (PermutationVector.Get<FVirtualTextureTargetDim>() && !PermutationVector.Get<FMultiViewDim>())
		{
			return false;
		}

		if (PermutationVector.Get<FClusterPerPageDim>() && !PermutationVector.Get<FVirtualTextureTargetDim>())
		{
			return false;
		}

		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SOFTWARE_RASTER"), 0);

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);

		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		const bool bIsPrimitiveShader = PermutationVector.Get<FPrimShaderDim>();
		
		if (bIsPrimitiveShader)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_VertexToPrimitiveShader);
		}
		else if (PermutationVector.Get<FAutoShaderCullDim>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_VertexUseAutoCulling);
		}

		OutEnvironment.SetDefine(TEXT("NANITE_HW_COUNTER_INDEX"), bIsPrimitiveShader ? 4 : 5); // Mesh and primitive shaders use an index of 4 instead of 5

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::NVAtomics) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D11) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
		{
			// Need to force optimization for driver injection to work correctly.
			// https://developer.nvidia.com/unlocking-gpu-intrinsics-hlsl
			// https://gpuopen.com/gcn-shader-extensions-for-direct3d-and-vulkan/
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
		{
			// Force shader model 6.0+
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		}
	}
};
IMPLEMENT_GLOBAL_SHADER(FHWRasterizeVS, "/Engine/Private/Nanite/Rasterizer.usf", "HWRasterizeVS", SF_Vertex);

// TODO: Consider making a common base shader class for VS and MS (where possible)
class FHWRasterizeMS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FHWRasterizeMS);
	SHADER_USE_PARAMETER_STRUCT(FHWRasterizeMS, FNaniteShader);

	class FInterpOptDim : SHADER_PERMUTATION_BOOL("NANITE_MESH_SHADER_INTERP");
	class FRasterTechniqueDim : SHADER_PERMUTATION_INT("RASTER_TECHNIQUE", (int32)Nanite::ERasterTechnique::NumTechniques);
	class FAddClusterOffset : SHADER_PERMUTATION_BOOL("ADD_CLUSTER_OFFSET");
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FHasPrevDrawData : SHADER_PERMUTATION_BOOL("HAS_PREV_DRAW_DATA");
	class FVisualizeDim : SHADER_PERMUTATION_BOOL("VISUALIZE");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	using FPermutationDomain = TShaderPermutationDomain<FInterpOptDim, FRasterTechniqueDim, FAddClusterOffset, FMultiViewDim, FHasPrevDrawData, FVisualizeDim, FNearClipDim, FVirtualTextureTargetDim, FClusterPerPageDim>;

	using FParameters = FRasterizePassParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!DoesPlatformSupportNanite(Parameters.Platform))
		{
			return false;
		}

		if (!FDataDrivenShaderPlatformInfo::GetSupportsMeshShadersTier1(Parameters.Platform))
		{
			// Only some platforms support mesh shaders with tier1 support
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::PlatformAtomics) &&
			!FDataDrivenShaderPlatformInfo::GetSupportsUInt64ImageAtomics(Parameters.Platform))
		{
			// Only some platforms support native 64-bit atomics.
			return false;
		}

		if ((PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::NVAtomics) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D11) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
			&& !FDataDrivenShaderPlatformInfo::GetRequiresVendorExtensionsForAtomics(Parameters.Platform))
		{
			// Only supporting vendor extensions on PC D3D SM5+
			return false;
		}

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::DepthOnly) &&
			PermutationVector.Get<FVisualizeDim>())
		{
			// Visualization not supported with depth only
			return false;
		}

		if (PermutationVector.Get<FVirtualTextureTargetDim>() && !PermutationVector.Get<FMultiViewDim>())
		{
			return false;
		}

		if (PermutationVector.Get<FClusterPerPageDim>() && !PermutationVector.Get<FVirtualTextureTargetDim>())
		{
			return false;
		}

		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SOFTWARE_RASTER"), 0);

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);

		OutEnvironment.SetDefine(TEXT("NANITE_MESH_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_HW_COUNTER_INDEX"), 4); // Mesh and primitive shaders use an index of 4 instead of 5

		const uint32 MSThreadGroupSize = FDataDrivenShaderPlatformInfo::GetMaxMeshShaderThreadGroupSize(Parameters.Platform);
		check(MSThreadGroupSize == 128 || MSThreadGroupSize == 256);
		OutEnvironment.SetDefine(TEXT("NANITE_MESH_SHADER_TG_SIZE"), MSThreadGroupSize);

		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::NVAtomics) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D11) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
		{
			// Need to force optimization for driver injection to work correctly.
			// https://developer.nvidia.com/unlocking-gpu-intrinsics-hlsl
			// https://gpuopen.com/gcn-shader-extensions-for-direct3d-and-vulkan/
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}

		// Force shader model 6.0+
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}
};
IMPLEMENT_GLOBAL_SHADER(FHWRasterizeMS, "/Engine/Private/Nanite/Rasterizer.usf", "HWRasterizeMS", SF_Mesh);

class FHWRasterizePS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FHWRasterizePS );
	SHADER_USE_PARAMETER_STRUCT( FHWRasterizePS, FNaniteShader );

	class FInterpOptDim : SHADER_PERMUTATION_BOOL("NANITE_MESH_SHADER_INTERP");
	class FRasterTechniqueDim : SHADER_PERMUTATION_INT("RASTER_TECHNIQUE", (int32)Nanite::ERasterTechnique::NumTechniques);
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FMeshShaderDim : SHADER_PERMUTATION_BOOL("NANITE_MESH_SHADER");
	class FPrimShaderDim : SHADER_PERMUTATION_BOOL("NANITE_PRIM_SHADER");
	class FVisualizeDim : SHADER_PERMUTATION_BOOL("VISUALIZE");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");

	using FPermutationDomain = TShaderPermutationDomain<FInterpOptDim, FRasterTechniqueDim, FMultiViewDim, FMeshShaderDim, FPrimShaderDim, FVisualizeDim, FVirtualTextureTargetDim, FClusterPerPageDim, FNearClipDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizePassParameters, Common)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!DoesPlatformSupportNanite(Parameters.Platform))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::PlatformAtomics) &&
			!FDataDrivenShaderPlatformInfo::GetSupportsUInt64ImageAtomics(Parameters.Platform))
		{
			// Only some platforms support native 64-bit atomics.
			return false;
		}

		if ((PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::NVAtomics) ||
			 PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D11) ||
			 PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
			 && !FDataDrivenShaderPlatformInfo::GetRequiresVendorExtensionsForAtomics(Parameters.Platform))
		{
			// Only supporting vendor extensions on PC D3D SM5+
			return false;
		}

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::DepthOnly) &&
			PermutationVector.Get<FVisualizeDim>())
		{
			// Visualization not supported with depth only
			return false;
		}

		if (PermutationVector.Get<FMeshShaderDim>() &&
			!FDataDrivenShaderPlatformInfo::GetSupportsMeshShadersTier1(Parameters.Platform))
		{
			// Only some platforms support mesh shaders with tier1 support.
			return false;
		}

		if (PermutationVector.Get<FPrimShaderDim>() &&
			!FDataDrivenShaderPlatformInfo::GetSupportsPrimitiveShaders(Parameters.Platform))
		{
			// Only some platforms support primitive shaders.
			return false;
		}

		if (PermutationVector.Get<FMeshShaderDim>() && PermutationVector.Get<FPrimShaderDim>())
		{
			// Mutually exclusive.
			return false;
		}

		if (PermutationVector.Get<FVirtualTextureTargetDim>() && !PermutationVector.Get<FMultiViewDim>())
		{
			return false;
		}

		if (PermutationVector.Get<FClusterPerPageDim>() && !PermutationVector.Get<FVirtualTextureTargetDim>())
		{
			return false;
		}

		if (!PermutationVector.Get<FMeshShaderDim>() && PermutationVector.Get<FInterpOptDim>())
		{
			return false;
		}

		return FNaniteShader::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, EPixelFormat::PF_R32_UINT);
		OutEnvironment.SetDefine(TEXT("SOFTWARE_RASTER"), 0);

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);

		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::NVAtomics) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D11) ||
			PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
		{
			// Need to force optimization for driver injection to work correctly.
			// https://developer.nvidia.com/unlocking-gpu-intrinsics-hlsl
			// https://gpuopen.com/gcn-shader-extensions-for-direct3d-and-vulkan/
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::AMDAtomicsD3D12))
		{
			// Force shader model 6.0+
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		}
	}
};
IMPLEMENT_GLOBAL_SHADER(FHWRasterizePS, "/Engine/Private/Nanite/Rasterizer.usf", "HWRasterizePS", SF_Pixel);

namespace Nanite
{

static void AddPassInitNodesAndClusterBatchesUAV( FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferUAVRef UAVRef )
{
	LLM_SCOPE_BYTAG(Nanite);

	{
		FInitCandidateNodes_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitCandidateNodes_CS::FParameters >();
		PassParameters->OutMainAndPostNodesAndClusterBatches= UAVRef;
		PassParameters->MaxCandidateClusters				= Nanite::FGlobalResources::GetMaxCandidateClusters();
		PassParameters->MaxNodes							= Nanite::FGlobalResources::GetMaxNodes();

		auto ComputeShader = ShaderMap->GetShader< FInitCandidateNodes_CS >();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "Nanite::InitNodes" ),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCountWrapped(Nanite::FGlobalResources::GetMaxNodes(), 64)
		);
	}

	{
		FInitClusterBatches_CS::FParameters* PassParameters	= GraphBuilder.AllocParameters< FInitClusterBatches_CS::FParameters >();
		PassParameters->OutMainAndPostNodesAndClusterBatches= UAVRef;
		PassParameters->MaxCandidateClusters				= Nanite::FGlobalResources::GetMaxCandidateClusters();
		PassParameters->MaxNodes							= Nanite::FGlobalResources::GetMaxNodes();

		auto ComputeShader = ShaderMap->GetShader< FInitClusterBatches_CS >();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "Nanite::InitCullingBatches" ),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCountWrapped(Nanite::FGlobalResources::GetMaxClusterBatches(), 64)
		);
	}
}

FCullingContext InitCullingContext(
	FRDGBuilder& GraphBuilder,
	const FSharedContext& SharedContext,
	const FScene& Scene,
	const TRefCountPtr<IPooledRenderTarget> &PrevHZB,
	const FIntRect &HZBBuildViewRect,
	bool bTwoPassOcclusion,
	bool bUpdateStreaming,
	bool bSupportsMultiplePasses,
	bool bForceHWRaster,
	bool bPrimaryContext,
	bool bDrawOnlyVSMInvalidatingGeometry,
	bool bIgnoreVisibleInRaster
	)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::InitContext");

	INC_DWORD_STAT(STAT_NaniteCullingContexts);

	FCullingContext CullingContext = {};

	CullingContext.PrevHZB					= PrevHZB;
	CullingContext.HZBBuildViewRect			= HZBBuildViewRect;
	CullingContext.bTwoPassOcclusion		= CullingContext.PrevHZB != nullptr && bTwoPassOcclusion;
	CullingContext.bSupportsMultiplePasses	= bSupportsMultiplePasses;
	CullingContext.DrawPassIndex			= 0;
	CullingContext.RenderFlags				= 0;
	CullingContext.DebugFlags				= 0;

	if (bForceHWRaster)
	{
		CullingContext.RenderFlags |= RENDER_FLAG_FORCE_HW_RASTER;
	}

	if (UseMeshShader(SharedContext.Pipeline))
	{
		CullingContext.RenderFlags |= RENDER_FLAG_MESH_SHADER;
	}
	else if (UsePrimitiveShader())
	{
		CullingContext.RenderFlags |= RENDER_FLAG_PRIMITIVE_SHADER;
	}

	if (bIgnoreVisibleInRaster)
	{
		CullingContext.RenderFlags |= RENDER_FLAG_IGNORE_VISIBLE_IN_RASTER;
	}

	// TODO: Exclude from shipping builds
	{
		if (GNaniteSphereCullingFrustum == 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_DISABLE_CULL_FRUSTUM_SPHERE;
		}

		if (GNaniteSphereCullingHZB == 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_DISABLE_CULL_HZB_SPHERE;
		}

		if (GNaniteBoxCullingFrustum == 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_DISABLE_CULL_FRUSTUM_BOX;
		}

		if (GNaniteBoxCullingHZB == 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_DISABLE_CULL_HZB_BOX;
		}

		if (GNaniteShowStats != 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_WRITE_STATS;
		}

		if (bDrawOnlyVSMInvalidatingGeometry)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_DRAW_ONLY_VSM_INVALIDATING;
		}
	}

	// TODO: Might this not break if the view has overridden the InstanceSceneData?
	const uint32 NumSceneInstancesPo2						= FMath::RoundUpToPowerOfTwo(Scene.GPUScene.InstanceSceneDataAllocator.GetMaxSize());
	CullingContext.PageConstants.X							= Scene.GPUScene.InstanceSceneDataSOAStride;
	CullingContext.PageConstants.Y							= Nanite::GStreamingManager.GetMaxStreamingPages();
	
	check(NumSceneInstancesPo2 <= MAX_INSTANCES);		// There are too many instances in the scene.

	CullingContext.QueueState					= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateStructuredDesc( 44, 1 ), TEXT("Nanite.QueueState") );

	FRDGBufferDesc VisibleClustersDesc			= FRDGBufferDesc::CreateStructuredDesc(4, 3 * Nanite::FGlobalResources::GetMaxVisibleClusters());	// Max visible clusters * sizeof(uint3)
	VisibleClustersDesc.Usage					= EBufferUsageFlags(VisibleClustersDesc.Usage | BUF_ByteAddressBuffer);

	CullingContext.VisibleClustersSWHW			= GraphBuilder.CreateBuffer(VisibleClustersDesc, TEXT("Nanite.VisibleClustersSWHW"));

	CullingContext.MainRasterizeArgsSWHW		= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(8), TEXT("Nanite.MainRasterizeArgsSWHW"));
	CullingContext.SafeMainRasterizeArgsSWHW	= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(8), TEXT("Nanite.SafeMainRasterizeArgsSWHW"));
	
	if (CullingContext.bTwoPassOcclusion)
	{
		CullingContext.OccludedInstances		= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FInstanceDraw), NumSceneInstancesPo2), TEXT("Nanite.OccludedInstances"));
		CullingContext.OccludedInstancesArgs	= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("Nanite.OccludedInstancesArgs"));
		CullingContext.PostRasterizeArgsSWHW	= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(8), TEXT("Nanite.PostRasterizeArgsSWHW"));
		CullingContext.SafePostRasterizeArgsSWHW= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(8), TEXT("Nanite.SafePostRasterizeArgsSWHW"));
	}

	CullingContext.StreamingRequests = GraphBuilder.RegisterExternalBuffer(Nanite::GStreamingManager.GetStreamingRequestsBuffer()); 
	if (bUpdateStreaming)
	{
		CullingContext.RenderFlags |= RENDER_FLAG_OUTPUT_STREAMING_REQUESTS;
	}

	if (bSupportsMultiplePasses)
	{
		CullingContext.TotalPrevDrawClustersBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(8, 1), TEXT("Nanite.TotalPrevDrawClustersBuffer"));
	}

	return CullingContext;
}

void AddPass_InstanceHierarchyAndClusterCull(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FCullingParameters& CullingParameters,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	const uint32 NumPrimaryViews,
	const FSharedContext& SharedContext,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	const FGPUSceneParameters &GPUSceneParameters,
	FRDGBufferRef MainAndPostNodesAndClusterBatchesBuffer,
	FRDGBufferRef MainAndPostCandididateClustersBuffer,
	uint32 CullingPass,
	FVirtualShadowMapArray *VirtualShadowMapArray,
	FVirtualTargetParameters &VirtualTargetParameters
	)
{
	LLM_SCOPE_BYTAG(Nanite);

	checkf(GRHIPersistentThreadGroupCount > 0, TEXT("GRHIPersistentThreadGroupCount must be configured correctly in the RHI."));

	// Currently only occlusion free multi-view routing.
	ensure(!VirtualShadowMapArray || CullingPass == CULLING_PASS_NO_OCCLUSION);

	const bool bMultiView = Views.Num() > 1 || VirtualShadowMapArray != nullptr;

	if (VirtualShadowMapArray)
	{
		RDG_GPU_STAT_SCOPE( GraphBuilder, NaniteInstanceCullVSM );

		FInstanceCullVSM_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInstanceCullVSM_CS::FParameters >();

		PassParameters->NumInstances						= CullingContext.NumInstancesPreCull;
		PassParameters->MaxNodes							= Nanite::FGlobalResources::GetMaxNodes();
		
		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->CullingParameters = CullingParameters;

		PassParameters->VirtualShadowMap = VirtualTargetParameters;		
		
		PassParameters->OutQueueState						= GraphBuilder.CreateUAV( CullingContext.QueueState );

		if (CullingContext.StatsBuffer)
		{
			PassParameters->OutStatsBuffer					= GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
		}

		check( CullingPass == CULLING_PASS_NO_OCCLUSION );
		check( CullingContext.InstanceDrawsBuffer == nullptr );
		PassParameters->OutMainAndPostNodesAndClusterBatches = GraphBuilder.CreateUAV( MainAndPostNodesAndClusterBatchesBuffer );
		
		check(CullingContext.ViewsBuffer);

		FInstanceCullVSM_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInstanceCullVSM_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FInstanceCullVSM_CS::FDebugFlagsDim>(CullingContext.DebugFlags != 0);
		PermutationVector.Set<FInstanceCullVSM_CS::FUseCompactedViewsDim>(CVarCompactVSMViews.GetValueOnRenderThread() != 0);

		auto ComputeShader = SharedContext.ShaderMap->GetShader<FInstanceCullVSM_CS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "Main Pass: InstanceCullVSM - No occlusion" ),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCountWrapped(CullingContext.NumInstancesPreCull, 64)
		);
	}
	else if (CullingContext.NumInstancesPreCull > 0 || CullingPass == CULLING_PASS_OCCLUSION_POST)
	{
		RDG_GPU_STAT_SCOPE( GraphBuilder, NaniteInstanceCull );
		FInstanceCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInstanceCull_CS::FParameters >();

		PassParameters->NumInstances						= CullingContext.NumInstancesPreCull;
		PassParameters->MaxNodes							= Nanite::FGlobalResources::GetMaxNodes();
		PassParameters->ImposterMaxPixels					= GNaniteImposterMaxPixels;

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->RasterParameters = RasterContext.Parameters;
		PassParameters->CullingParameters = CullingParameters;

		const ERasterTechnique Technique = RasterContext.RasterTechnique;
		PassParameters->OnlyCastShadowsPrimitives = Technique == ERasterTechnique::DepthOnly ? 1 : 0;

		PassParameters->ImposterAtlas = Nanite::GStreamingManager.GetImposterDataSRV();

		PassParameters->OutQueueState						= GraphBuilder.CreateUAV( CullingContext.QueueState );
		
		if (CullingContext.StatsBuffer)
		{
			PassParameters->OutStatsBuffer					= GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
		}

		PassParameters->OutMainAndPostNodesAndClusterBatches = GraphBuilder.CreateUAV( MainAndPostNodesAndClusterBatchesBuffer );
		if( CullingPass == CULLING_PASS_NO_OCCLUSION )
		{
			if( CullingContext.InstanceDrawsBuffer )
			{
				PassParameters->InInstanceDraws			= GraphBuilder.CreateSRV( CullingContext.InstanceDrawsBuffer );
			}
		}
		else if( CullingPass == CULLING_PASS_OCCLUSION_MAIN )
		{
			PassParameters->OutOccludedInstances		= GraphBuilder.CreateUAV( CullingContext.OccludedInstances );
			PassParameters->OutOccludedInstancesArgs	= GraphBuilder.CreateUAV( CullingContext.OccludedInstancesArgs );
		}
		else
		{
			PassParameters->InInstanceDraws				= GraphBuilder.CreateSRV( CullingContext.OccludedInstances );
			PassParameters->InOccludedInstancesArgs		= GraphBuilder.CreateSRV( CullingContext.OccludedInstancesArgs );
		}
		
		check(CullingContext.ViewsBuffer);

		const uint32 InstanceCullingPass = CullingContext.InstanceDrawsBuffer != nullptr ? CULLING_PASS_EXPLICIT_LIST : CullingPass;
		FInstanceCull_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInstanceCull_CS::FCullingPassDim>(InstanceCullingPass);
		PermutationVector.Set<FInstanceCull_CS::FMultiViewDim>(bMultiView);
		PermutationVector.Set<FInstanceCull_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FInstanceCull_CS::FDebugFlagsDim>(CullingContext.DebugFlags != 0);
		PermutationVector.Set<FInstanceCull_CS::FRasterTechniqueDim>(int32(RasterContext.RasterTechnique));

		auto ComputeShader = SharedContext.ShaderMap->GetShader<FInstanceCull_CS>(PermutationVector);
		if( InstanceCullingPass == CULLING_PASS_OCCLUSION_POST )
		{
			PassParameters->IndirectArgs = CullingContext.OccludedInstancesArgs;
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME( "Post Pass: InstanceCull" ),
				ComputeShader,
				PassParameters,
				PassParameters->IndirectArgs,
				0
			);
		}
		else
		{
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				InstanceCullingPass == CULLING_PASS_OCCLUSION_MAIN ?	RDG_EVENT_NAME( "Main Pass: InstanceCull" ) : 
				InstanceCullingPass == CULLING_PASS_NO_OCCLUSION ?		RDG_EVENT_NAME( "Main Pass: InstanceCull - No occlusion" ) :
																		RDG_EVENT_NAME( "Main Pass: InstanceCull - Explicit list" ),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCountWrapped(CullingContext.NumInstancesPreCull, 64)
			);
		}
	}

	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteClusterCull);
		FPersistentClusterCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FPersistentClusterCull_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->CullingParameters		= CullingParameters;
		PassParameters->MaxNodes				= Nanite::FGlobalResources::GetMaxNodes();
		
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->HierarchyBuffer			= Nanite::GStreamingManager.GetHierarchySRV();
		
		check(CullingContext.DrawPassIndex == 0 || CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA); // sanity check
		if (CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA)
		{
			PassParameters->InTotalPrevDrawClusters = GraphBuilder.CreateSRV(CullingContext.TotalPrevDrawClustersBuffer);
		}
		else
		{
			FRDGBufferRef Dummy = GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetStructureBufferStride8(), TEXT("Nanite.StructuredBufferStride8"));
			PassParameters->InTotalPrevDrawClusters = GraphBuilder.CreateSRV(Dummy);
		}

		PassParameters->QueueState							= GraphBuilder.CreateUAV(CullingContext.QueueState);
		PassParameters->MainAndPostNodesAndClusterBatches	= GraphBuilder.CreateUAV(MainAndPostNodesAndClusterBatchesBuffer);
		PassParameters->MainAndPostCandididateClusters		= GraphBuilder.CreateUAV(MainAndPostCandididateClustersBuffer);

		if( CullingPass == CULLING_PASS_NO_OCCLUSION || CullingPass == CULLING_PASS_OCCLUSION_MAIN )
		{
			PassParameters->VisibleClustersArgsSWHW	= GraphBuilder.CreateUAV( CullingContext.MainRasterizeArgsSWHW );
		}
		else
		{
			PassParameters->OffsetClustersArgsSWHW	= GraphBuilder.CreateSRV( CullingContext.MainRasterizeArgsSWHW );
			PassParameters->VisibleClustersArgsSWHW	= GraphBuilder.CreateUAV( CullingContext.PostRasterizeArgsSWHW );
		}

		PassParameters->OutVisibleClustersSWHW			= GraphBuilder.CreateUAV( CullingContext.VisibleClustersSWHW );
		PassParameters->OutStreamingRequests			= GraphBuilder.CreateUAV( CullingContext.StreamingRequests );

		if (VirtualShadowMapArray)
		{
			PassParameters->VirtualShadowMap = VirtualTargetParameters;
		}

		if (CullingContext.StatsBuffer)
		{
			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
		}

		PassParameters->LargePageRectThreshold = CVarLargePageRectThreshold.GetValueOnRenderThread();
		PassParameters->StreamingRequestsBufferVersion = GStreamingManager.GetStreamingRequestsBufferVersion();

		check(CullingContext.ViewsBuffer);

		FPersistentClusterCull_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FPersistentClusterCull_CS::FCullingPassDim>(CullingPass);
		PermutationVector.Set<FPersistentClusterCull_CS::FMultiViewDim>(bMultiView);
		PermutationVector.Set<FPersistentClusterCull_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FPersistentClusterCull_CS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVector.Set<FPersistentClusterCull_CS::FClusterPerPageDim>(GNaniteClusterPerPage && VirtualShadowMapArray != nullptr);
		PermutationVector.Set<FPersistentClusterCull_CS::FDebugFlagsDim>(CullingContext.DebugFlags != 0);

		auto ComputeShader = SharedContext.ShaderMap->GetShader<FPersistentClusterCull_CS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			CullingPass == CULLING_PASS_NO_OCCLUSION	? RDG_EVENT_NAME( "Main Pass: PersistentCull - No occlusion" ) :
			CullingPass == CULLING_PASS_OCCLUSION_MAIN	? RDG_EVENT_NAME( "Main Pass: PersistentCull" ) :
			RDG_EVENT_NAME( "Post Pass: PersistentCull" ),
			ComputeShader,
			PassParameters,
			FIntVector(GRHIPersistentThreadGroupCount, 1, 1)
		);
	}

	{
		FCalculateSafeRasterizerArgs_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCalculateSafeRasterizerArgs_CS::FParameters >();

		const bool bPrevDrawData	= (CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA) != 0;
		const bool bPostPass		= (CullingPass == CULLING_PASS_OCCLUSION_POST) != 0;

		if (bPrevDrawData)
		{
			PassParameters->InTotalPrevDrawClusters		= GraphBuilder.CreateSRV(CullingContext.TotalPrevDrawClustersBuffer);
		}
		
		if (bPostPass)
		{
			PassParameters->OffsetClustersArgsSWHW		= GraphBuilder.CreateSRV(CullingContext.MainRasterizeArgsSWHW);
			PassParameters->InRasterizerArgsSWHW		= GraphBuilder.CreateSRV(CullingContext.PostRasterizeArgsSWHW);
			PassParameters->OutSafeRasterizerArgsSWHW	= GraphBuilder.CreateUAV(CullingContext.SafePostRasterizeArgsSWHW);
		}
		else
		{
			PassParameters->InRasterizerArgsSWHW		= GraphBuilder.CreateSRV(CullingContext.MainRasterizeArgsSWHW);
			PassParameters->OutSafeRasterizerArgsSWHW	= GraphBuilder.CreateUAV(CullingContext.SafeMainRasterizeArgsSWHW);
		}
		
		PassParameters->MaxVisibleClusters				= Nanite::FGlobalResources::GetMaxVisibleClusters();
		PassParameters->RenderFlags						= CullingContext.RenderFlags;
		
		FCalculateSafeRasterizerArgs_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FCalculateSafeRasterizerArgs_CS::FHasPrevDrawData>(bPrevDrawData);
		PermutationVector.Set<FCalculateSafeRasterizerArgs_CS::FIsPostPass>(bPostPass);

		auto ComputeShader = SharedContext.ShaderMap->GetShader< FCalculateSafeRasterizerArgs_CS >(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			bPostPass ? RDG_EVENT_NAME("Post Pass: CalculateSafeRasterizerArgs") : RDG_EVENT_NAME("Main Pass: CalculateSafeRasterizerArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}
}

void AddPass_Rasterize(
	FRDGBuilder& GraphBuilder,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	const FSharedContext& SharedContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	FIntVector4 PageConstants,
	uint32 RenderFlags,
	FRDGBufferRef ViewsBuffer,
	FRDGBufferRef VisibleClustersSWHW,
	FRDGBufferRef ClusterOffsetSWHW,
	FRDGBufferRef IndirectArgs,
	FRDGBufferRef TotalPrevDrawClustersBuffer,
	const FGPUSceneParameters& GPUSceneParameters,
	bool bMainPass,
	FVirtualShadowMapArray* VirtualShadowMapArray,
	FVirtualTargetParameters& VirtualTargetParameters
)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	check(RasterState.CullMode == CM_CW || RasterState.CullMode == CM_CCW);		// CM_None not implemented

	auto* RasterPassParameters = GraphBuilder.AllocParameters<FHWRasterizePS::FParameters>();
	auto* CommonPassParameters = &RasterPassParameters->Common;

	CommonPassParameters->ClusterPageData = GStreamingManager.GetClusterPageDataSRV();

	if (ViewsBuffer)
	{
		CommonPassParameters->InViews = GraphBuilder.CreateSRV(ViewsBuffer);
	}

	CommonPassParameters->GPUSceneParameters = GPUSceneParameters;
	CommonPassParameters->RasterParameters = RasterContext.Parameters;
	CommonPassParameters->VisualizeModeBitMask = RasterContext.VisualizeModeBitMask;
	CommonPassParameters->PageConstants = PageConstants;
	CommonPassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
	CommonPassParameters->RenderFlags = RenderFlags;
	if (RasterState.CullMode == CM_CCW)
	{
		CommonPassParameters->RenderFlags |= RENDER_FLAG_REVERSE_CULLING;
	}
	CommonPassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
	
	if (VirtualShadowMapArray)
	{
		CommonPassParameters->VirtualShadowMap = VirtualTargetParameters;
	}

	if (!bMainPass)
	{
		CommonPassParameters->InClusterOffsetSWHW = GraphBuilder.CreateSRV(ClusterOffsetSWHW);
	}
	CommonPassParameters->IndirectArgs = IndirectArgs;

	const bool bHavePrevDrawData = (RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA);
	if (bHavePrevDrawData)
	{
		CommonPassParameters->InTotalPrevDrawClusters = GraphBuilder.CreateSRV(TotalPrevDrawClustersBuffer);
	}

	const ERasterTechnique Technique = RasterContext.RasterTechnique;
	const ERasterScheduling Scheduling = RasterContext.RasterScheduling;
	const bool bNearClip = RasterState.bNearClip;
	const bool bMultiView = Views.Num() > 1 || VirtualShadowMapArray != nullptr;

	ERDGPassFlags ComputePassFlags = ERDGPassFlags::Compute;

	if (Scheduling == ERasterScheduling::HardwareAndSoftwareOverlap)
	{
		const auto CreateSkipBarrierUAV = [&](auto& InOutUAV)
		{
			if (InOutUAV)
			{
				InOutUAV = GraphBuilder.CreateUAV(InOutUAV->Desc, ERDGUnorderedAccessViewFlags::SkipBarrier);
			}
		};

		// Create a new set of UAVs with the SkipBarrier flag enabled to allow software / hardware overlap.
		CreateSkipBarrierUAV(CommonPassParameters->RasterParameters.OutDepthBuffer);
		CreateSkipBarrierUAV(CommonPassParameters->RasterParameters.OutVisBuffer64);
		CreateSkipBarrierUAV(CommonPassParameters->RasterParameters.OutDbgBuffer64);
		CreateSkipBarrierUAV(CommonPassParameters->RasterParameters.OutDbgBuffer32);
		CreateSkipBarrierUAV(CommonPassParameters->RasterParameters.LockBuffer);

		ComputePassFlags = ERDGPassFlags::AsyncCompute;
	}

	FIntRect ViewRect(Views[0].ViewRect.X, Views[0].ViewRect.Y, Views[0].ViewRect.Z, Views[0].ViewRect.W);
	if (bMultiView)
	{
		ViewRect.Min = FIntPoint::ZeroValue;
		ViewRect.Max = RasterContext.TextureSize;
	}

	if (VirtualShadowMapArray)
	{
		ViewRect.Min = FIntPoint::ZeroValue;
		if( GNaniteClusterPerPage )
		{
			ViewRect.Max = FIntPoint( FVirtualShadowMap::PageSize, FVirtualShadowMap::PageSize ) * FVirtualShadowMap::RasterWindowPages;
		}
		else
		{
			ViewRect.Max = FIntPoint( FVirtualShadowMap::VirtualMaxResolutionXY, FVirtualShadowMap::VirtualMaxResolutionXY );
		}
	}

	FRHIRenderPassInfo RPInfo;
	RPInfo.ResolveParameters.DestRect.X1 = ViewRect.Min.X;
	RPInfo.ResolveParameters.DestRect.Y1 = ViewRect.Min.Y;
	RPInfo.ResolveParameters.DestRect.X2 = ViewRect.Max.X;
	RPInfo.ResolveParameters.DestRect.Y2 = ViewRect.Max.Y;

	const bool bUseMeshShader = UseMeshShader(SharedContext.Pipeline);

	const bool bUsePrimitiveShader = UsePrimitiveShader() && !bUseMeshShader;

	const bool bUseAutoCullingShader =
		GRHISupportsPrimitiveShaders &&
		!bUsePrimitiveShader &&
		GNaniteAutoShaderCulling != 0;

	FHWRasterizePS::FPermutationDomain PermutationVectorPS;
	PermutationVectorPS.Set<FHWRasterizePS::FInterpOptDim>(GNaniteMSInterp != 0 && bUseMeshShader && !bMultiView);
	PermutationVectorPS.Set<FHWRasterizePS::FRasterTechniqueDim>(int32(Technique));
	PermutationVectorPS.Set<FHWRasterizePS::FMultiViewDim>(bMultiView);
	PermutationVectorPS.Set<FHWRasterizePS::FMeshShaderDim>(bUseMeshShader);
	PermutationVectorPS.Set<FHWRasterizePS::FPrimShaderDim>(bUsePrimitiveShader);
	PermutationVectorPS.Set<FHWRasterizePS::FVisualizeDim>(RasterContext.VisualizeActive && Technique != ERasterTechnique::DepthOnly);
	PermutationVectorPS.Set<FHWRasterizePS::FNearClipDim>(bNearClip);
	PermutationVectorPS.Set<FHWRasterizePS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
	PermutationVectorPS.Set<FHWRasterizePS::FClusterPerPageDim>(GNaniteClusterPerPage && VirtualShadowMapArray != nullptr);
	auto PixelShader = SharedContext.ShaderMap->GetShader<FHWRasterizePS>(PermutationVectorPS);

	if (bUseMeshShader)
	{
		FHWRasterizeMS::FPermutationDomain PermutationVectorMS;
		PermutationVectorMS.Set<FHWRasterizeMS::FInterpOptDim>(GNaniteMSInterp != 0);
		PermutationVectorMS.Set<FHWRasterizeMS::FRasterTechniqueDim>(int32(Technique));
		PermutationVectorMS.Set<FHWRasterizeMS::FAddClusterOffset>(bMainPass ? 0 : 1);
		PermutationVectorMS.Set<FHWRasterizeMS::FMultiViewDim>(bMultiView);
		PermutationVectorMS.Set<FHWRasterizeMS::FHasPrevDrawData>(bHavePrevDrawData);
		PermutationVectorMS.Set<FHWRasterizeMS::FVisualizeDim>(RasterContext.VisualizeActive && Technique != ERasterTechnique::DepthOnly);
		PermutationVectorMS.Set<FHWRasterizeMS::FNearClipDim>(bNearClip);
		PermutationVectorMS.Set<FHWRasterizeMS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVectorMS.Set<FHWRasterizeMS::FClusterPerPageDim>(GNaniteClusterPerPage && VirtualShadowMapArray != nullptr);
		auto MeshShader = SharedContext.ShaderMap->GetShader<FHWRasterizeMS>(PermutationVectorMS);

		GraphBuilder.AddPass(
			bMainPass ? RDG_EVENT_NAME("Main Pass: HW Rasterize") : RDG_EVENT_NAME("Post Pass: HW Rasterize"),
			RasterPassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
			[MeshShader, PixelShader, RasterPassParameters, ViewRect, RPInfo, bMainPass](FRHICommandList& RHICmdList)
		{
			
			RHICmdList.BeginRenderPass(RPInfo, bMainPass ? TEXT("Main Pass: HW Rasterize") : TEXT("Post Pass: HW Rasterize"));
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, FMath::Min(ViewRect.Max.X, 32767), FMath::Min(ViewRect.Max.Y, 32767), 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			// NOTE: We do *not* use RasterState.CullMode here because HWRasterizeVS already
			// changes the index order in cases where the culling should be flipped.
			//GraphicsPSOInit.RasterizerState = GetStaticRasterizerState<false>(FM_Solid, bUsePrimitiveShaderCulling ? CM_None : CM_CW);
			GraphicsPSOInit.RasterizerState = GetStaticRasterizerState<false>(FM_Solid, CM_CW);
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_PointList;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = nullptr;// GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.SetMeshShader(MeshShader.GetMeshShader());
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
			
			SetShaderParameters(RHICmdList, MeshShader, MeshShader.GetMeshShader(), RasterPassParameters->Common);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *RasterPassParameters);

			RHICmdList.SetStreamSource(0, nullptr, 0);
			
			// TODO: Different arg format vs. regular draw?
			RHICmdList.DispatchIndirectMeshShader(RasterPassParameters->Common.IndirectArgs->GetIndirectRHICallBuffer(), 16);

			RHICmdList.EndRenderPass();
		});
	}
	else
	{
		FHWRasterizeVS::FPermutationDomain PermutationVectorVS;
		PermutationVectorVS.Set<FHWRasterizeVS::FRasterTechniqueDim>(int32(Technique));
		PermutationVectorVS.Set<FHWRasterizeVS::FAddClusterOffset>(bMainPass ? 0 : 1);
		PermutationVectorVS.Set<FHWRasterizeVS::FMultiViewDim>(bMultiView);
		PermutationVectorVS.Set<FHWRasterizeVS::FPrimShaderDim>(bUsePrimitiveShader);
		PermutationVectorVS.Set<FHWRasterizeVS::FAutoShaderCullDim>(bUseAutoCullingShader);
		PermutationVectorVS.Set<FHWRasterizeVS::FHasPrevDrawData>(bHavePrevDrawData);
		PermutationVectorVS.Set<FHWRasterizeVS::FVisualizeDim>(RasterContext.VisualizeActive && Technique != ERasterTechnique::DepthOnly);
		PermutationVectorVS.Set<FHWRasterizeVS::FNearClipDim>(bNearClip);
		PermutationVectorVS.Set<FHWRasterizeVS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVectorVS.Set<FHWRasterizeVS::FClusterPerPageDim>(GNaniteClusterPerPage && VirtualShadowMapArray != nullptr );
		auto VertexShader = SharedContext.ShaderMap->GetShader<FHWRasterizeVS>(PermutationVectorVS);

		GraphBuilder.AddPass(
			bMainPass ? RDG_EVENT_NAME("Main Pass: HW Rasterize") : RDG_EVENT_NAME("Post Pass: HW Rasterize"),
			RasterPassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
			[VertexShader, PixelShader, RasterPassParameters, ViewRect, bUsePrimitiveShader, RPInfo, bMainPass](FRHICommandList& RHICmdList)
		{
			RHICmdList.BeginRenderPass(RPInfo, bMainPass ? TEXT("Main Pass: HW Rasterize") : TEXT("Post Pass: HW Rasterize"));
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, FMath::Min(ViewRect.Max.X, 32767), FMath::Min(ViewRect.Max.Y, 32767), 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			// NOTE: We do *not* use RasterState.CullMode here because HWRasterizeVS already
			// changes the index order in cases where the culling should be flipped.
			//GraphicsPSOInit.RasterizerState = GetStaticRasterizerState<false>(FM_Solid, bUsePrimitiveShaderCulling ? CM_None : CM_CW);
			GraphicsPSOInit.RasterizerState = GetStaticRasterizerState<false>(FM_Solid, CM_CW);
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.PrimitiveType = bUsePrimitiveShader ? PT_PointList : PT_TriangleList;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RasterPassParameters->Common);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *RasterPassParameters);

			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitiveIndirect(RasterPassParameters->Common.IndirectArgs->GetIndirectRHICallBuffer(), 16);
			RHICmdList.EndRenderPass();
		});
	}

	if (Scheduling != ERasterScheduling::HardwareOnly)
	{
		// SW Rasterize
		FMicropolyRasterizeCS::FPermutationDomain PermutationVectorCS;
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FAddClusterOffset>(bMainPass ? 0 : 1);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FMultiViewDim>(bMultiView);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FHasPrevDrawData>(bHavePrevDrawData);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FRasterTechniqueDim>(int32(Technique));
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FVisualizeDim>(RasterContext.VisualizeActive && Technique != ERasterTechnique::DepthOnly);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FNearClipDim>(bNearClip);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FClusterPerPageDim>(GNaniteClusterPerPage&& VirtualShadowMapArray != nullptr);

		auto ComputeShader = SharedContext.ShaderMap->GetShader<FMicropolyRasterizeCS>(PermutationVectorCS);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			bMainPass ? RDG_EVENT_NAME("Main Pass: SW Rasterize") : RDG_EVENT_NAME("Post Pass: SW Rasterize"),
			ComputePassFlags,
			ComputeShader,
			CommonPassParameters,
			CommonPassParameters->IndirectArgs,
			0
		);
	}
}

FRasterContext InitRasterContext(
	FRDGBuilder& GraphBuilder,
	const FSharedContext& SharedContext,
	FIntPoint TextureSize,
	bool bVisualize,
	EOutputBufferMode RasterMode,
	bool bClearTarget,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects,
	FRDGTextureRef ExternalDepthBuffer
)
{
	// If an external depth buffer is provided, it must match the context size
	check( ExternalDepthBuffer == nullptr || ExternalDepthBuffer->Desc.Extent == TextureSize );
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::InitContext");

	const FNaniteVisualizationData& VisualizationData = GetNaniteVisualizationData();

	FRasterContext RasterContext{};

	RasterContext.VisualizeActive = VisualizationData.IsActive() && bVisualize;
	if (RasterContext.VisualizeActive)
	{
		if (VisualizationData.GetActiveModeID() == 0) // Overview
		{
			RasterContext.VisualizeModeBitMask = VisualizationData.GetOverviewModeBitMask();
		}
		else
		{
			RasterContext.VisualizeModeBitMask |= VisualizationData.GetActiveModeID();
		}
	}

	RasterContext.TextureSize = TextureSize;

	// Set rasterizer scheduling based on config and platform capabilities.
	if (GNaniteComputeRasterization != 0)
	{
		const bool bUseAsyncCompute = GSupportsEfficientAsyncCompute && (GNaniteAsyncRasterization != 0) && EnumHasAnyFlags(GRHIMultiPipelineMergeableAccessMask, ERHIAccess::UAVMask);
		RasterContext.RasterScheduling = bUseAsyncCompute ? ERasterScheduling::HardwareAndSoftwareOverlap : ERasterScheduling::HardwareThenSoftware;
	}
	else
	{
		// Force hardware-only rasterization.
		RasterContext.RasterScheduling = ERasterScheduling::HardwareOnly;
	}

	if (RasterMode == EOutputBufferMode::DepthOnly)
	{
		RasterContext.RasterTechnique = ERasterTechnique::DepthOnly;
	}
	else if (!GRHISupportsAtomicUInt64 || GNaniteAtomicRasterization == 0)
	{
		// No 64-bit atomic support, or it is disabled.
		RasterContext.RasterTechnique = ERasterTechnique::LockBufferFallback;
	}
	else
	{
		// Determine what is providing support for atomics.
#if PLATFORM_WINDOWS
		if (!FDataDrivenShaderPlatformInfo::GetRequiresVendorExtensionsForAtomics(GShaderPlatformForFeatureLevel[SharedContext.FeatureLevel]))
		{
			RasterContext.RasterTechnique = ERasterTechnique::PlatformAtomics;
		}
		else if (UseMeshShader(SharedContext.Pipeline) && GNaniteAtomicRasterization != 0)
		{
			// TODO: Currently, atomic64 vendor extensions and mesh shaders don't interop.
			// Mesh shaders require PSO stream support, and vendor extensions require legacy PSO create.
			RasterContext.RasterTechnique = ERasterTechnique::LockBufferFallback;
		}
		else if (IsRHIDeviceNVIDIA())
		{
			// Support is provided through NVAPI.
			RasterContext.RasterTechnique = ERasterTechnique::NVAtomics;
		}
		else if (IsRHIDeviceAMD())
		{
			// TODO: This... should be cleaned up. No way to query the RHI in another capacity.
			static const bool bIsDx12 = FCString::Stristr(GDynamicRHI->GetName(), TEXT("D3D12")) != nullptr; // Also covers -rhivalidation => D3D12_Validation

			// Support is provided through AGS.
			RasterContext.RasterTechnique = bIsDx12 ? ERasterTechnique::AMDAtomicsD3D12 : ERasterTechnique::AMDAtomicsD3D11;

			// TODO: Currently the atomics only work properly in the hardware path on DX11. Disable any compute support with this technique.
			if (!bIsDx12)
			{
				RasterContext.RasterScheduling = ERasterScheduling::HardwareOnly;
			}
		}
#else
		RasterContext.RasterTechnique = ERasterTechnique::PlatformAtomics;
#endif
	}

	const EPixelFormat PixelFormat64 = GPixelFormats[PF_R64_UINT].Supported ? PF_R64_UINT : PF_R32G32_UINT;

	RasterContext.DepthBuffer	= ExternalDepthBuffer ? ExternalDepthBuffer :
								  GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Nanite.DepthBuffer32") );
	RasterContext.VisBuffer64	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PixelFormat64, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Nanite.VisBuffer64") );
	RasterContext.DbgBuffer64	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PixelFormat64, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Nanite.DbgBuffer64") );
	RasterContext.DbgBuffer32	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Nanite.DbgBuffer32") );
	RasterContext.LockBuffer	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV), TEXT("Nanite.LockBuffer") );
	
	const uint32 ClearValue[4] = { 0, 0, 0, 0 };

	if (RasterMode == EOutputBufferMode::DepthOnly)
	{
		RasterContext.Parameters.OutDepthBuffer = GraphBuilder.CreateUAV( RasterContext.DepthBuffer );
		if (bClearTarget)
		{
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.OutDepthBuffer, ClearValue, RectMinMaxBufferSRV, NumRects );
		}
	}
	else
	{
		RasterContext.Parameters.OutVisBuffer64 = GraphBuilder.CreateUAV( RasterContext.VisBuffer64 );
		if (bClearTarget)
		{
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.OutVisBuffer64, ClearValue, RectMinMaxBufferSRV, NumRects );
		}
		
		if (RasterContext.VisualizeActive)
		{
			RasterContext.Parameters.OutDbgBuffer64 = GraphBuilder.CreateUAV( RasterContext.DbgBuffer64 );
			RasterContext.Parameters.OutDbgBuffer32 = GraphBuilder.CreateUAV( RasterContext.DbgBuffer32 );
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.OutDbgBuffer64, ClearValue, RectMinMaxBufferSRV, NumRects );
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.OutDbgBuffer32, ClearValue, RectMinMaxBufferSRV, NumRects );
		}

		if (RasterContext.RasterTechnique == ERasterTechnique::LockBufferFallback)
		{
			RasterContext.Parameters.LockBuffer = GraphBuilder.CreateUAV(RasterContext.LockBuffer);
			AddClearUAVPass(GraphBuilder, RasterContext.Parameters.LockBuffer, ClearValue, RectMinMaxBufferSRV, NumRects);
		}
	}

	return RasterContext;
}

static void AllocateNodesAndBatchesBuffers(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferRef* MainAndPostNodesAndClusterBatchesBufferRef)
{
	const uint32 MaxNodes				=	Nanite::FGlobalResources::GetMaxNodes();
	const uint32 MaxCandidateClusters	=	Nanite::FGlobalResources::GetMaxCandidateClusters();
	const uint32 MaxCullingBatches		=	Nanite::FGlobalResources::GetMaxClusterBatches();
	check(MainAndPostNodesAndClusterBatchesBufferRef);

	// Initialize node and cluster batch arrays.
	// They only have to be initialized once as the culling code reverts nodes/batches to their cleared state after they have been consumed.
	{
		TRefCountPtr<FRDGPooledBuffer>& MainAndPostNodesAndClusterBatchesBuffer = Nanite::GGlobalResources.GetMainAndPostNodesAndClusterBatchesBuffer();
		if (MainAndPostNodesAndClusterBatchesBuffer.IsValid())
		{
			*MainAndPostNodesAndClusterBatchesBufferRef = GraphBuilder.RegisterExternalBuffer(MainAndPostNodesAndClusterBatchesBuffer, TEXT("Nanite.MainAndPostNodesAndClusterBatchesBuffer"));
		}
		else
		{
			RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

			FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(4, MaxCullingBatches * 2 + MaxNodes * (2 + 3));
			Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_ByteAddressBuffer);
			*MainAndPostNodesAndClusterBatchesBufferRef = GraphBuilder.CreateBuffer(Desc, TEXT("Nanite.MainAndPostNodesAndClusterBatchesBuffer"));
			AddPassInitNodesAndClusterBatchesUAV(GraphBuilder, ShaderMap, GraphBuilder.CreateUAV(*MainAndPostNodesAndClusterBatchesBufferRef));
			MainAndPostNodesAndClusterBatchesBuffer = GraphBuilder.ConvertToExternalBuffer(*MainAndPostNodesAndClusterBatchesBufferRef);
		}
	}
}

// Render a large number of views by splitting them into multiple passes. This is only supported for depth-only rendering.
// Visibility buffer rendering requires that view references are uniquely decodable.
static void CullRasterizeMultiPass(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	uint32 NumPrimaryViews,
	const FSharedContext& SharedContext,
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	const TArray<FInstanceDraw, SceneRenderingAllocator>* OptionalInstanceDraws,
	FVirtualShadowMapArray* VirtualShadowMapArray,
	bool bExtractStats
)
{
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::CullRasterizeSplitViewRanges");

	check(RasterContext.RasterTechnique == ERasterTechnique::DepthOnly);

	uint32 NextPrimaryViewIndex = 0;
	while (NextPrimaryViewIndex < NumPrimaryViews)
	{
		// Fit as many views as possible into the next range
		int32 RangeStartPrimaryView = NextPrimaryViewIndex;
		int32 RangeNumViews = 0;
		int32 RangeMaxMip = 0;
		while (NextPrimaryViewIndex < NumPrimaryViews)
		{
			const Nanite::FPackedView& PrimaryView = Views[NextPrimaryViewIndex];
			const int32 NumMips = PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z;

			// Can we include the next primary view and its mips?
			int32 NextRangeNumViews = FMath::Max(RangeMaxMip, NumMips) * (NextPrimaryViewIndex - RangeStartPrimaryView + 1);
			if (NextRangeNumViews > MAX_VIEWS_PER_CULL_RASTERIZE_PASS)
				break;

			RangeNumViews = NextRangeNumViews;
			NextPrimaryViewIndex++;
			RangeMaxMip = FMath::Max(RangeMaxMip, NumMips);
		}

		// Construct new view range
		int32 RangeNumPrimaryViews = NextPrimaryViewIndex - RangeStartPrimaryView;
		TArray<FPackedView, SceneRenderingAllocator> RangeViews;
		RangeViews.SetNum(RangeNumViews);

		for (int32 ViewIndex = 0; ViewIndex < RangeNumPrimaryViews; ++ViewIndex)
		{
			const Nanite::FPackedView& PrimaryView = Views[RangeStartPrimaryView + ViewIndex];
			const int32 NumMips = PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z;

			for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
			{
				RangeViews[MipIndex * RangeNumPrimaryViews + ViewIndex] = Views[MipIndex * NumPrimaryViews + (RangeStartPrimaryView + ViewIndex)];
			}
		}

		CullRasterize(
			GraphBuilder,
			Scene,
			RangeViews,
			RangeNumPrimaryViews,
			SharedContext,
			CullingContext,
			RasterContext,
			RasterState,
			OptionalInstanceDraws,
			VirtualShadowMapArray,
			bExtractStats
		);
	}
}

void CullRasterize(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	uint32 NumPrimaryViews,	// Number of non-mip views
	const FSharedContext& SharedContext,
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	const TArray<FInstanceDraw, SceneRenderingAllocator>* OptionalInstanceDraws,
	// VirtualShadowMapArray is the supplier of virtual to physical translation, probably could abstract this a bit better,
	FVirtualShadowMapArray* VirtualShadowMapArray,
	bool bExtractStats
)
{
	LLM_SCOPE_BYTAG(Nanite);
	
	// Split rasterization into multiple passes if there are too many views. Only possible for depth-only rendering.
	if (Views.Num() > MAX_VIEWS_PER_CULL_RASTERIZE_PASS)
	{
		check(RasterContext.RasterTechnique == ERasterTechnique::DepthOnly);
		CullRasterizeMultiPass(GraphBuilder, Scene, Views, NumPrimaryViews, SharedContext, CullingContext, RasterContext, RasterState, OptionalInstanceDraws, VirtualShadowMapArray, bExtractStats);
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::CullRasterize");

	AddPassIfDebug(GraphBuilder, RDG_EVENT_NAME("CheckIsAsyncUpdateInProgress"), [](FRHICommandListImmediate&)
	{
		check(!Nanite::GStreamingManager.IsAsyncUpdateInProgress());
	});

	// Calling CullRasterize more than once on a CullingContext is illegal unless bSupportsMultiplePasses is enabled.
	check(CullingContext.DrawPassIndex == 0 || CullingContext.bSupportsMultiplePasses);

	//check(Views.Num() == 1 || !CullingContext.PrevHZB);	// HZB not supported with multi-view, yet
	ensure(Views.Num() > 0 && Views.Num() <= MAX_VIEWS_PER_CULL_RASTERIZE_PASS);

	{
		const uint32 ViewsBufferElements = FMath::RoundUpToPowerOfTwo(Views.Num());
		CullingContext.ViewsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Nanite.Views"), Views.GetTypeSize(), ViewsBufferElements, Views.GetData(), Views.Num() * Views.GetTypeSize());
	}

	if (OptionalInstanceDraws)
	{
		const uint32 InstanceDrawsBufferElements = FMath::RoundUpToPowerOfTwo(OptionalInstanceDraws->Num());
		CullingContext.InstanceDrawsBuffer = CreateStructuredBuffer
		(
			GraphBuilder,
			TEXT("Nanite.InstanceDraws"),
			OptionalInstanceDraws->GetTypeSize(),
			InstanceDrawsBufferElements,
			OptionalInstanceDraws->GetData(),
			OptionalInstanceDraws->Num() * OptionalInstanceDraws->GetTypeSize()
		);
		CullingContext.NumInstancesPreCull = OptionalInstanceDraws->Num();
	}
	else
	{
		CullingContext.InstanceDrawsBuffer = nullptr;
		CullingContext.NumInstancesPreCull = Scene.GPUScene.InstanceSceneDataAllocator.GetMaxSize();
	}

	if (CullingContext.DebugFlags != 0)
	{
		FNaniteStats Stats;
		Stats.NumTris  = 0;
		Stats.NumVerts = 0;
		Stats.NumViews = 0;
		Stats.NumMainInstancesPreCull	= CullingContext.NumInstancesPreCull;
		Stats.NumMainInstancesPostCull	= 0;
		Stats.NumMainVisitedNodes		= 0;
		Stats.NumMainCandidateClusters	= 0;
		Stats.NumPostInstancesPreCull	= 0;
		Stats.NumPostInstancesPostCull	= 0;
		Stats.NumPostVisitedNodes		= 0;
		Stats.NumPostCandidateClusters	= 0;
		Stats.NumLargePageRectClusters	= 0;
		Stats.NumPrimaryViews			= 0;
		Stats.NumTotalViews				= 0;

		CullingContext.StatsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Nanite.StatsBuffer"), sizeof(FNaniteStats), 1, &Stats, sizeof(FNaniteStats));
	}
	else
	{
		CullingContext.StatsBuffer = nullptr;
	}

	FCullingParameters CullingParameters;
	{
		CullingParameters.InViews		= GraphBuilder.CreateSRV(CullingContext.ViewsBuffer);
		CullingParameters.NumViews		= Views.Num();
		CullingParameters.NumPrimaryViews = NumPrimaryViews;
		CullingParameters.DisocclusionLodScaleFactor = GNaniteDisocclusionHack ? 0.01f : 1.0f;	// TODO: Get rid of this hack
		CullingParameters.HZBTexture	= RegisterExternalTextureWithFallback(GraphBuilder, CullingContext.PrevHZB, GSystemTextures.BlackDummy);
		CullingParameters.HZBSize		= CullingContext.PrevHZB ? CullingContext.PrevHZB->GetDesc().Extent : FVector2f(0.0f);
		CullingParameters.HZBSampler	= TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();
		CullingParameters.PageConstants = CullingContext.PageConstants;
		CullingParameters.MaxCandidateClusters	= Nanite::FGlobalResources::GetMaxCandidateClusters();
		CullingParameters.MaxVisibleClusters	= Nanite::FGlobalResources::GetMaxVisibleClusters();
		CullingParameters.RenderFlags	= CullingContext.RenderFlags;
		CullingParameters.DebugFlags	= CullingContext.DebugFlags;
		CullingParameters.CompactedViewInfo = nullptr;
		CullingParameters.CompactedViewsAllocation = nullptr;
	}

	FVirtualTargetParameters VirtualTargetParameters;
	if (VirtualShadowMapArray)
	{
		VirtualTargetParameters.VirtualShadowMap = VirtualShadowMapArray->GetUniformBuffer(GraphBuilder);
		VirtualTargetParameters.PageFlags = GraphBuilder.CreateSRV(VirtualShadowMapArray->PageFlagsRDG, PF_R32_UINT);
		VirtualTargetParameters.HPageFlags = GraphBuilder.CreateSRV(VirtualShadowMapArray->HPageFlagsRDG, PF_R32_UINT);
		VirtualTargetParameters.PageRectBounds = GraphBuilder.CreateSRV(VirtualShadowMapArray->PageRectBoundsRDG);

		// HZB (if provided) comes from the previous frame, so we need last frame's page table
		FRDGBufferRef HZBPageTableRDG = VirtualShadowMapArray->PageTableRDG;	// Dummy data, but matches the expected format
		if (CullingContext.PrevHZB)
		{
			check( VirtualShadowMapArray->CacheManager );
			TRefCountPtr<FRDGPooledBuffer> HZBPageTable = VirtualShadowMapArray->CacheManager->PrevBuffers.PageTable;
			check( HZBPageTable );
			HZBPageTableRDG = GraphBuilder.RegisterExternalBuffer( HZBPageTable, TEXT( "Shadow.Virtual.HZBPageTable" ) );
		}
		VirtualTargetParameters.ShadowHZBPageTable = GraphBuilder.CreateSRV( HZBPageTableRDG, PF_R32_UINT );
	}
	FGPUSceneParameters GPUSceneParameters;
	GPUSceneParameters.GPUSceneInstanceSceneData = Scene.GPUScene.InstanceSceneDataBuffer.SRV;
	GPUSceneParameters.GPUSceneInstancePayloadData = Scene.GPUScene.InstancePayloadDataBuffer.SRV;
	GPUSceneParameters.GPUScenePrimitiveSceneData = Scene.GPUScene.PrimitiveBuffer.SRV;
	GPUSceneParameters.GPUSceneFrameNumber = Scene.GPUScene.GetSceneFrameNumber();
	
	if (VirtualShadowMapArray && CVarCompactVSMViews.GetValueOnRenderThread() != 0)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteInstanceCullVSM);

		// Compact the views to remove needless (empty) mip views - need to do on GPU as that is where we know what mips have pages.
		const uint32 ViewsBufferElements = FMath::RoundUpToPowerOfTwo(Views.Num());
		FRDGBufferRef CompactedViews = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FPackedView), ViewsBufferElements), TEXT("Shadow.Virtual.CompactedViews"));
		FRDGBufferRef CompactedViewInfo = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FCompactedViewInfo), Views.Num()), TEXT("Shadow.Virtual.CompactedViewInfo"));
		
		// Just a pair of atomic counters, zeroed by a clear UAV pass.
		FRDGBufferRef CompactedViewsAllocation = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 2), TEXT("Shadow.Virtual.CompactedViewsAllocation"));
		FRDGBufferUAVRef CompactedViewsAllocationUAV = GraphBuilder.CreateUAV(CompactedViewsAllocation);
		AddClearUAVPass(GraphBuilder, CompactedViewsAllocationUAV, 0);

		{
			FCompactViewsVSM_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCompactViewsVSM_CS::FParameters >();

			PassParameters->GPUSceneParameters = GPUSceneParameters;
			PassParameters->CullingParameters = CullingParameters;
			PassParameters->VirtualShadowMap = VirtualTargetParameters;


			PassParameters->CompactedViewsOut = GraphBuilder.CreateUAV(CompactedViews);
			PassParameters->CompactedViewInfoOut = GraphBuilder.CreateUAV(CompactedViewInfo);
			PassParameters->CompactedViewsAllocationOut = CompactedViewsAllocationUAV;

			check(CullingContext.ViewsBuffer);
			auto ComputeShader = SharedContext.ShaderMap->GetShader<FCompactViewsVSM_CS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CompactViewsVSM"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(NumPrimaryViews, 64)
			);
		}

		// Override the view info with the compacted info.
		CullingParameters.InViews = GraphBuilder.CreateSRV(CompactedViews);
		CullingContext.ViewsBuffer = CompactedViews;
		CullingParameters.CompactedViewInfo = GraphBuilder.CreateSRV(CompactedViewInfo);
		CullingParameters.CompactedViewsAllocation = GraphBuilder.CreateSRV(CompactedViewsAllocation);
	}

	{
		FInitArgs_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitArgs_CS::FParameters >();

		PassParameters->RenderFlags = CullingParameters.RenderFlags;

		PassParameters->OutQueueState						= GraphBuilder.CreateUAV( CullingContext.QueueState );
		PassParameters->InOutMainPassRasterizeArgsSWHW		= GraphBuilder.CreateUAV( CullingContext.MainRasterizeArgsSWHW );

		uint32 ClampedDrawPassIndex = FMath::Min(CullingContext.DrawPassIndex, 2u);

		if (CullingContext.bTwoPassOcclusion)
		{
			PassParameters->OutOccludedInstancesArgs = GraphBuilder.CreateUAV( CullingContext.OccludedInstancesArgs );
			PassParameters->InOutPostPassRasterizeArgsSWHW = GraphBuilder.CreateUAV( CullingContext.PostRasterizeArgsSWHW );
		}
		
		check(CullingContext.DrawPassIndex == 0 || CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA); // sanity check
		if (CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA)
		{
			PassParameters->InOutTotalPrevDrawClusters = GraphBuilder.CreateUAV(CullingContext.TotalPrevDrawClustersBuffer);
		}
		else
		{
			// Use any UAV just to keep render graph happy that something is bound, but the shader doesn't actually touch this.
			PassParameters->InOutTotalPrevDrawClusters = PassParameters->OutQueueState;
		}

		FInitArgs_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInitArgs_CS::FOcclusionCullingDim>( CullingContext.bTwoPassOcclusion );
		PermutationVector.Set<FInitArgs_CS::FDrawPassIndexDim>( ClampedDrawPassIndex );
		
		auto ComputeShader = SharedContext.ShaderMap->GetShader< FInitArgs_CS >( PermutationVector );

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "InitArgs" ),
			ComputeShader,
			PassParameters,
			FIntVector( 1, 1, 1 )
		);
	}

	// Allocate buffer for nodes and cluster batches
	FRDGBufferRef MainAndPostNodesAndClusterBatchesBuffer = nullptr;
	AllocateNodesAndBatchesBuffers(GraphBuilder, SharedContext.ShaderMap, &MainAndPostNodesAndClusterBatchesBuffer);

	// Allocate candidate cluster buffer. Lifetime only duration of CullRasterize
	FRDGBufferRef MainAndPostCandididateClustersBuffer = nullptr;
	{
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(4, Nanite::FGlobalResources::GetMaxCandidateClusters() * 2);
		Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_ByteAddressBuffer);
		MainAndPostCandididateClustersBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("Nanite.MainAndPostCandididateClustersBuffer"));
	}
	

	// No Occlusion Pass / Occlusion Main Pass
	AddPass_InstanceHierarchyAndClusterCull(
		GraphBuilder,
		Scene,
		CullingParameters,
		Views,
		NumPrimaryViews,
		SharedContext,
		CullingContext,
		RasterContext,
		RasterState,
		GPUSceneParameters,
		MainAndPostNodesAndClusterBatchesBuffer,
		MainAndPostCandididateClustersBuffer,
		CullingContext.bTwoPassOcclusion ? CULLING_PASS_OCCLUSION_MAIN : CULLING_PASS_NO_OCCLUSION,
		VirtualShadowMapArray,
		VirtualTargetParameters
	);

	AddPass_Rasterize(
		GraphBuilder,
		Views,
		SharedContext,
		RasterContext,
		RasterState,
		CullingContext.PageConstants,
		CullingContext.RenderFlags,
		CullingContext.ViewsBuffer,
		CullingContext.VisibleClustersSWHW,
		nullptr,
		CullingContext.SafeMainRasterizeArgsSWHW,
		CullingContext.TotalPrevDrawClustersBuffer,
		GPUSceneParameters,
		true,
		VirtualShadowMapArray,
		VirtualTargetParameters
	);
	
	// Occlusion post pass. Retest instances and clusters that were not visible last frame. If they are visible now, render them.
	if (CullingContext.bTwoPassOcclusion)
	{
		// Build a closest HZB with previous frame occluders to test remainder occluders against.
		{
			RDG_EVENT_SCOPE(GraphBuilder, "BuildPreviousOccluderHZB");
			
			FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

			FRDGTextureRef SceneDepth = SceneTextures.SceneDepthTexture;
			FRDGTextureRef RasterizedDepth = RasterContext.VisBuffer64;

			if( RasterContext.RasterTechnique == ERasterTechnique::DepthOnly )
			{
				SceneDepth = GraphBuilder.RegisterExternalTexture( GSystemTextures.BlackDummy );
				RasterizedDepth = RasterContext.DepthBuffer;
			}

			FRDGTextureRef OutFurthestHZBTexture;

			FIntRect ViewRect(0, 0, RasterContext.TextureSize.X, RasterContext.TextureSize.Y);
			if (Views.Num() == 1)
			{
				//TODO: This is a hack. Using full texture can lead to 'far' borders on left/bottom. How else can we ensure good culling perf for main view.
				ViewRect = FIntRect(Views[0].ViewRect.X, Views[0].ViewRect.Y, Views[0].ViewRect.Z, Views[0].ViewRect.W);
			}
			
			BuildHZBFurthest(
				GraphBuilder,
				SceneDepth,
				RasterizedDepth,
				CullingContext.HZBBuildViewRect,
				Scene.GetFeatureLevel(),
				Scene.GetShaderPlatform(),
				TEXT("Nanite.PreviousOccluderHZB"),
				/* OutFurthestHZBTexture = */ &OutFurthestHZBTexture);

			CullingParameters.HZBTexture = OutFurthestHZBTexture;
			CullingParameters.HZBSize = CullingParameters.HZBTexture->Desc.Extent;
		}

		// Post Pass
		AddPass_InstanceHierarchyAndClusterCull(
			GraphBuilder,
			Scene,
			CullingParameters,
			Views,
			NumPrimaryViews,
			SharedContext,
			CullingContext,
			RasterContext,
			RasterState,
			GPUSceneParameters,
			MainAndPostNodesAndClusterBatchesBuffer,
			MainAndPostCandididateClustersBuffer,
			CULLING_PASS_OCCLUSION_POST,
			VirtualShadowMapArray,
			VirtualTargetParameters
		);

		// Render post pass
		AddPass_Rasterize(
			GraphBuilder,
			Views,
			SharedContext,
			RasterContext,
			RasterState,
			CullingContext.PageConstants,
			CullingContext.RenderFlags,
			CullingContext.ViewsBuffer,
			CullingContext.VisibleClustersSWHW,
			CullingContext.MainRasterizeArgsSWHW,
			CullingContext.SafePostRasterizeArgsSWHW,
			CullingContext.TotalPrevDrawClustersBuffer,
			GPUSceneParameters,
			false,
			VirtualShadowMapArray,
			VirtualTargetParameters
		);
	}

	if (RasterContext.RasterTechnique != ERasterTechnique::DepthOnly)
	{
		// Pass index and number of clusters rendered in previous passes are irrelevant for depth-only rendering.
		CullingContext.DrawPassIndex++;
		CullingContext.RenderFlags |= RENDER_FLAG_HAVE_PREV_DRAW_DATA;
	}

	if (bExtractStats)
	{
		const bool bVirtualTextureTarget = VirtualShadowMapArray != nullptr;
		ExtractStats(GraphBuilder, SharedContext, CullingContext, bVirtualTextureTarget);
	}
}

void CullRasterize(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	const FSharedContext& SharedContext,
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	const TArray<FInstanceDraw, SceneRenderingAllocator>* OptionalInstanceDraws,
	bool bExtractStats
)
{
	CullRasterize(
		GraphBuilder,
		Scene,
		Views,
		Views.Num(),
		SharedContext,
		CullingContext,
		RasterContext,
		RasterState,
		OptionalInstanceDraws,
		nullptr,
		bExtractStats
	);
}

} // namespace Nanite
