// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteRender.h"
#include "CoreMinimal.h"
#include "RHI.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "UnifiedBuffer.h"
#include "GPUScene.h"
#include "ClearQuad.h"
#include "RendererModule.h"
#include "PipelineStateCache.h"
#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PipelineStateCache.h"
#include "PixelShaderUtils.h"
#include "ShaderPrint.h"
#include "ShaderPrintParameters.h"
#include "LightMapRendering.h"
#include "MeshPassProcessor.inl"
#include "SceneTextureReductions.h"
#include "Lumen/LumenSceneRendering.h"

#define CULLING_PASS_NO_OCCLUSION		0
#define CULLING_PASS_OCCLUSION_MAIN		1
#define CULLING_PASS_OCCLUSION_POST		2

#define RENDER_FLAG_CACHE_INSTANCE_DYNAMIC_DATA		0x1
#define RENDER_FLAG_HAVE_PREV_DRAW_DATA				0x2
#define RENDER_FLAG_FORCE_HW_RASTER					0x4
#define RENDER_FLAG_PRIMITIVE_SHADER				0x8
#define RENDER_FLAG_OUTPUT_STREAMING_REQUESTS		0x10

// Only available with the DEBUG_FLAGS permutation active.
#define DEBUG_FLAG_WRITE_STATS						0x1
#define DEBUG_FLAG_CULL_HZB_BOX						0x2
#define DEBUG_FLAG_CULL_HZB_SPHERE					0x4
#define DEBUG_FLAG_CULL_FRUSTUM_BOX					0x8
#define DEBUG_FLAG_CULL_FRUSTUM_SPHERE				0x10

#define NUM_PERSISTENT_THREADS	1440				// TODO: Find a better way to estimate the number of threads we will need

DECLARE_GPU_STAT_NAMED(NaniteInstanceCull,		TEXT("Nanite Instance Cull"));
DECLARE_GPU_STAT_NAMED(NaniteInstanceCullVSM,	TEXT("Nanite Instance Cull VSM"));

DEFINE_GPU_STAT(NaniteDebug);
DEFINE_GPU_STAT(NaniteEditor);
DEFINE_GPU_STAT(NaniteRaster);
DEFINE_GPU_STAT(NaniteMaterials);

DECLARE_DWORD_COUNTER_STAT(TEXT("CullingContexts"), STAT_NaniteCullingContexts, STATGROUP_Nanite);

DEFINE_LOG_CATEGORY(LogNanite);

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

int32 GNanitePrimShaderRasterization = 1;
FAutoConsoleVariableRef CVarNanitePrimShaderRasterization(
	TEXT("r.Nanite.PrimShaderRasterization"),
	GNanitePrimShaderRasterization,
	TEXT("")
);

// 0 = no views
// 1 = primary view
// 2 = shadow views
// 3 = all views
int32 GNanitePrimShaderCulling = 0;
FAutoConsoleVariableRef CVarNanitePrimShaderCulling(
	TEXT("r.Nanite.PrimShaderCulling"),
	GNanitePrimShaderCulling,
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

float GNaniteMinPixelsPerEdgeHW = 18.0f;
FAutoConsoleVariableRef CVarNaniteMinPixelsPerEdgeHW(
	TEXT("r.Nanite.MinPixelsPerEdgeHW"),
	GNaniteMinPixelsPerEdgeHW,
	TEXT("")
);

int32 GNaniteDebugOverdrawScale = 15; // % of contribution per pixel evaluation (up to 100%)
FAutoConsoleVariableRef CVarNaniteDebugOverdrawScale(
	TEXT("r.Nanite.DebugOverdrawScale"),
	GNaniteDebugOverdrawScale,
	TEXT("")
);

int32 GNaniteResummarizeHTile = 1;
static FAutoConsoleVariableRef CVarNaniteResummarizeHTile(
	TEXT("r.Nanite.ResummarizeHTile"),
	GNaniteResummarizeHTile,
	TEXT("")
	);

// Optimized compute dual depth export pass on supported platforms.
int32 GNaniteExportDepth = 1;
static FAutoConsoleVariableRef CVarNaniteExportDepth(
	TEXT("r.Nanite.ExportDepth"),
	GNaniteExportDepth,
	TEXT("")
);

int32 GNaniteMaterialSortMode = 2;
static FAutoConsoleVariableRef CVarNaniteMaterialSortMode(
	TEXT("r.Nanite.MaterialSortMode"),
	GNaniteMaterialSortMode,
	TEXT("Method of sorting Nanite material draws. 0=disabled, 1=shader, 2=sortkey"),
	ECVF_RenderThreadSafe
);

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
int32 GNaniteCacheInstanceDynamicData = 0;
static FAutoConsoleVariableRef CVarNaniteCacheInstanceDynamicData(
	TEXT( "r.Nanite.CacheInstanceDynamicData" ),
	GNaniteCacheInstanceDynamicData,
	TEXT( "" )
);
#endif

int32 GNaniteClusterPerPage = 1;
static FAutoConsoleVariableRef CVarNaniteClusterPerPage(
	TEXT("r.Nanite.ClusterPerPage"),
	GNaniteClusterPerPage,
	TEXT("")
);

int32 GNaniteMaterialCulling = 2;
static FAutoConsoleVariableRef CVarNaniteMaterialCulling(
	TEXT("r.Nanite.MaterialCulling"),
	GNaniteMaterialCulling,
	TEXT("0: Disable culling\n")
	TEXT("1: Cull full screen passes for occluded materials\n")
	TEXT("2: Cull individual screen space tiles on 8x4 grid\n")
	TEXT("3: Cull individual screen space tiles on 64x64 grid - method 1\n")
	TEXT("4: Cull individual screen space tiles on 64x64 grid - method 2")
);

// Nanite Debug Flags

// Enables support for using debug flags.
int32 GNaniteDebugFlags = 0;
static FAutoConsoleVariableRef CVarNaniteDebugFlags(
	TEXT("r.Nanite.Debug"),
	GNaniteDebugFlags,
	TEXT("")
);

int32 GNaniteShowStats = 1;
FAutoConsoleVariableRef CVarNaniteShowStats(
	TEXT("r.Nanite.ShowStats"),
	GNaniteShowStats,
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

FString GNaniteStatsFilter;
FAutoConsoleVariableRef CVarNaniteStatsFilter(
	TEXT("r.Nanite.StatsFilter"),
	GNaniteStatsFilter,
	TEXT("Sets the name of a specific Nanite raster pass to capture stats from - enumerate available filters with `NaniteStats List` cmd."),
	ECVF_RenderThreadSafe
);

extern TAutoConsoleVariable<int32> CVarNaniteShadows;

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

extern int32 GLumenFastCameraMode;

namespace ShaderPrint
{
	extern TAutoConsoleVariable<int32> CVarEnable;
}

bool bNaniteListStatFilters = false;

void NaniteStatsFilterExec(const TCHAR* Cmd, FOutputDevice& Ar)
{
	check(IsInGameThread());

	FlushRenderingCommands();

	uint32 ParameterCount = 0;

	// Convenience, force on Nanite debug/stats and also shader printing.
	GNaniteDebugFlags = 1;
	GNaniteShowStats = 1;
	ShaderPrint::CVarEnable->Set(true);

	// parse parameters
	for (;;)
	{
		FString Parameter = FParse::Token(Cmd, 0);

		if (Parameter.IsEmpty())
		{
			break;
		}

		if (Parameter == TEXT("list"))
		{
			// We don't have access to all the scene data here, so we'll set a flag
			// to print out every filter comparison for the next frame.
			bNaniteListStatFilters = true;
		}
		else if (Parameter == TEXT("primary"))
		{
			// Empty filter name denotes the primary raster view.
			ParameterCount = 0;
			break;
		}
		else
		{
			GNaniteStatsFilter = Parameter;
		}

		++ParameterCount;
	}

	if (!ParameterCount)
	{
		// Default to showing stats for the primary view
		GNaniteStatsFilter.Empty();
	}
}

static bool UseComputeDepthExport()
{
	return (GRHISupportsDepthUAV && GRHISupportsExplicitHTile && GNaniteExportDepth != 0);
}

static bool UsePrimitiveShader()
{
	return GNanitePrimShaderRasterization != 0 && GRHISupportsPrimitiveShaders;
}

// Debug Visualization Modes (must match NaniteDataDecode.ush)
// https://docs.google.com/document/d/1PeRxK_w49jgACYTiQUlCJYSzbT_BKZbhly_7i80j-BU/edit?usp=sharing
#define VISUALIZE_TRIANGLES							1
#define VISUALIZE_CLUSTERS							2
#define VISUALIZE_GROUPS							3
#define VISUALIZE_PAGES								4
#define VISUALIZE_PRIMITIVES						5
#define VISUALIZE_HW_VS_SW							6
#define VISUALIZE_OVERDRAW							7
#define VISUALIZE_HIERARCHY_OFFSET					8
#define VISUALIZE_SCN_HTILE_MINZ					9
#define VISUALIZE_SCN_HTILE_MAXZ					10
#define VISUALIZE_SCN_HTILE_DELTAZ					11
#define VISUALIZE_SCN_HTILE_ZMASK					12
#define VISUALIZE_MAT_HTILE_MINZ					13
#define VISUALIZE_MAT_HTILE_MAXZ					14
#define VISUALIZE_MAT_HTILE_DELTAZ					15
#define VISUALIZE_MAT_HTILE_ZMASK					16
#define VISUALIZE_MATERIAL_FAST_VS_SLOW				17
#define VISUALIZE_MATERIAL_INDEX					18
#define VISUALIZE_MATERIAL_ID						19
#define VISUALIZE_HIT_PROXY_ID						20

int32 GNaniteDebugVisualize = 0;
FAutoConsoleVariableRef CVarNaniteDebugVisualize(
	TEXT("r.Nanite.DebugVisualize"),
	GNaniteDebugVisualize,
	TEXT("")
);

static bool IsVisualizingHTile()
{
	switch (GNaniteDebugVisualize)
	{
	case VISUALIZE_SCN_HTILE_MINZ:
	case VISUALIZE_SCN_HTILE_MAXZ:
	case VISUALIZE_SCN_HTILE_DELTAZ:
	case VISUALIZE_SCN_HTILE_ZMASK:
	case VISUALIZE_MAT_HTILE_MINZ:
	case VISUALIZE_MAT_HTILE_MAXZ:
	case VISUALIZE_MAT_HTILE_DELTAZ:
	case VISUALIZE_MAT_HTILE_ZMASK:
		return true;
	default:
		return false;
	}
}

static bool ShouldExportDebugBuffers()
{
	// HTILE has a separate pass for visualization
	return (GNaniteDebugVisualize > 0) && !IsVisualizingHTile();
}

static FIntVector4 GetVisualizeConfig()
{
	return FIntVector4(GNaniteDebugVisualize, GNaniteDebugOverdrawScale, 0, 0);
}

// Must match FStats in NaniteDataDecode.ush
struct FNaniteStats
{
	uint32 NumTris;
	uint32 NumVerts;
	uint32 NumViews;
	uint32 NumMainInstancesPreCull;
	uint32 NumMainInstancesPostCull;
	uint32 NumPostInstancesPreCull;
	uint32 NumPostInstancesPostCull;
	uint32 NumLargePageRectClusters;
};

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteUniformParameters, "Nanite");

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitGBufferParameters, )
	SHADER_PARAMETER(FIntVector4,	VisualizeConfig)
	SHADER_PARAMETER(FIntVector4,	SOAStrides)
	SHADER_PARAMETER(uint32,		MaxClusters)
	SHADER_PARAMETER(uint32,		MaxNodes)
	SHADER_PARAMETER(uint32,		RenderFlags)
	SHADER_PARAMETER(FIntPoint,		GridSize)
	
	SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageData )
	SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageHeaders )

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,	InstanceDynamicData)
#endif
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,	VisibleClustersSWHW)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, MaterialRange)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleMaterials)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, DbgBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,  DbgBuffer32)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)	// To access VTFeedbackBuffer

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT( FCullingParameters, )
	SHADER_PARAMETER( FIntVector4,	SOAStrides )
	SHADER_PARAMETER( uint32,		MaxClusters )
	SHADER_PARAMETER( uint32,		RenderFlags )
	SHADER_PARAMETER( uint32,		DebugFlags )
	SHADER_PARAMETER( uint32,		NumViews )
	SHADER_PARAMETER( uint32,		NumPrimaryViews )
	SHADER_PARAMETER( float,		DisocclusionLodScaleFactor )

	SHADER_PARAMETER( FVector2D,	HZBViewSize )
	SHADER_PARAMETER( FVector2D,	HZBSize )

	SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	HZBTexture )
	SHADER_PARAMETER_SAMPLER( SamplerState,		HZBSampler )
	
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT( FGPUSceneParameters, )
	SHADER_PARAMETER_SRV( StructuredBuffer<float4>,	GPUSceneInstanceSceneData)
	SHADER_PARAMETER_SRV( StructuredBuffer<float4>,	GPUScenePrimitiveSceneData)
	SHADER_PARAMETER( uint32,						GPUSceneFrameNumber)
END_SHADER_PARAMETER_STRUCT()

// TODO: is it better to declare the buffers in 'FVirtualShadowMapCommonParameters' and not always have them set? I.e., before they are built.
BEGIN_SHADER_PARAMETER_STRUCT( FVirtualTargetParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualShadowMapCommonParameters, VirtualShadowMapCommon )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint2 >,	PageTable )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >, PageFlags )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >, HPageFlags )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint4 >, PageRectBounds )
END_SHADER_PARAMETER_STRUCT()

class FInstanceCull_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInstanceCull_CS );
	SHADER_USE_PARAMETER_STRUCT( FInstanceCull_CS, FNaniteShader );

	class FCullingPassDim : SHADER_PERMUTATION_SPARSE_INT("CULLING_PASS", CULLING_PASS_NO_OCCLUSION, CULLING_PASS_OCCLUSION_MAIN, CULLING_PASS_OCCLUSION_POST);
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FInstanceDrawListDim : SHADER_PERMUTATION_BOOL("INSTANCE_DRAW_LIST");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FDebugFlagsDim : SHADER_PERMUTATION_BOOL("DEBUG_FLAGS");
	using FPermutationDomain = TShaderPermutationDomain<FCullingPassDim, FMultiViewDim, FInstanceDrawListDim, FNearClipDim, FDebugFlagsDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FVirtualShadowMapArray::SetShaderDefines( OutEnvironment );	// Still needed for shader to compile

		// Get data from GPUSceneParameters rather than View.
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( uint32, NumInstances )
		
		SHADER_PARAMETER_STRUCT_INCLUDE( FCullingParameters, CullingParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUSceneParameters, GPUSceneParameters)
		
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FInstanceDraw >, InInstanceDraws )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer, OutNodes )

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< float4 >, OutInstanceDynamicData )
#endif
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >, OutOccludedInstances )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >, OutMainAndPostPassPersistentStates )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OutOccludedInstancesArgs )

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FNaniteStats>, OutStatsBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >, InOccludedInstances )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, InOccludedInstancesArgs )

		SHADER_PARAMETER_RDG_BUFFER( Buffer< uint >, IndirectArgs )

	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInstanceCull_CS, "/Engine/Private/Nanite/InstanceCulling.usf", "InstanceCull", SF_Compute);

class FInstanceCullVSM_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInstanceCullVSM_CS );
	SHADER_USE_PARAMETER_STRUCT( FInstanceCullVSM_CS, FNaniteShader );

	class FNearClipDim : SHADER_PERMUTATION_BOOL( "NEAR_CLIP" );
	class FDebugFlagsDim : SHADER_PERMUTATION_BOOL( "DEBUG_FLAGS" );
	using FPermutationDomain = TShaderPermutationDomain<FNearClipDim, FDebugFlagsDim>;

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
		
		SHADER_PARAMETER_STRUCT_INCLUDE( FCullingParameters, CullingParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer, OutNodes )
	
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >, OutMainAndPostPassPersistentStates )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer<FNaniteStats>, OutStatsBuffer )

		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >, InOccludedInstances )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, InOccludedInstancesArgs )

		SHADER_PARAMETER_RDG_BUFFER( Buffer< uint >, IndirectArgs )

		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualTargetParameters, VirtualShadowMap )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint2 >,	HZBPageTable )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER( FInstanceCullVSM_CS, "/Engine/Private/Nanite/InstanceCulling.usf", "InstanceCullVSM", SF_Compute );


class FPersistentHierarchicalCull_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FPersistentHierarchicalCull_CS );
	SHADER_USE_PARAMETER_STRUCT( FPersistentHierarchicalCull_CS, FNaniteShader );

	class FCullingPassDim : SHADER_PERMUTATION_SPARSE_INT("CULLING_PASS", CULLING_PASS_NO_OCCLUSION, CULLING_PASS_OCCLUSION_MAIN, CULLING_PASS_OCCLUSION_POST);
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FDebugFlagsDim : SHADER_PERMUTATION_BOOL("DEBUG_FLAGS");

	using FPermutationDomain = TShaderPermutationDomain<FCullingPassDim, FMultiViewDim, FNearClipDim, FVirtualTextureTargetDim, FDebugFlagsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( uint32, MaxNodes )
		SHADER_PARAMETER_STRUCT_INCLUDE( FCullingParameters, CullingParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters)

		SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageData )
		SHADER_PARAMETER_SRV( ByteAddressBuffer,				HierarchyBuffer )

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< float4 >,			InstanceDynamicData )
#endif
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >,MainAndPostPassPersistentStates )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					InOutCandidateNodes )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					OutCandidateClusters )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					OutOccludedClusters )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					OutOccludedNodes )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >,						OutStreamingRequests )
		
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >,	OutCandidateClustersArgs )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >,	OutOccludedClustersArgs )

		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualTargetParameters, VirtualShadowMap )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint2 >,	HZBPageTable )
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
IMPLEMENT_GLOBAL_SHADER(FPersistentHierarchicalCull_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "PersistentHierarchicalCull", SF_Compute);

class FCandidateCull_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FCandidateCull_CS );
	SHADER_USE_PARAMETER_STRUCT( FCandidateCull_CS, FNaniteShader );

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
		SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageHeaders )

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< float4 >,		InstanceDynamicData )
#endif
		SHADER_PARAMETER_RDG_BUFFER_SRV( ByteAddressBuffer,					InCandidateClusters )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,				OutVisibleClustersSWHW )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,				OutOccludedClusters )
		
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, InCandidateClustersArgs )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, OffsetClustersArgsSWHW )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FUintVector2 >, InTotalPrevDrawClusters )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, VisibleClustersArgsSWHW )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OccludedClustersArgs )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OutOccludedClustersArgs )

		SHADER_PARAMETER_RDG_BUFFER( Buffer< uint >, IndirectArgs )

		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualTargetParameters, VirtualShadowMap )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >, OutDynamicCasterFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint2 >,	HZBPageTable )

		SHADER_PARAMETER(uint32, LargePageRectThreshold)
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

		// 
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FCandidateCull_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "CandidateCull", SF_Compute );

class FInitNodes_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInitNodes_CS );
	SHADER_USE_PARAMETER_STRUCT( FInitNodes_CS, FNaniteShader );

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,	OutNodes )
		SHADER_PARAMETER( uint32,								InitNodesIsPostPass )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitNodes_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "InitNodes", SF_Compute);

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

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >,OutMainAndPostPassPersistentStates )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FUintVector2 >,	InOutTotalPrevDrawClusters )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >,						OutMainPassCandidateClustersArgs )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >,						InOutMainPassRasterizeArgsSWHW )
		
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OutOccludedInstancesArgs )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OutPostPassCandidateClustersArgs )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, InOutPostPassRasterizeArgsSWHW )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitArgs_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "InitArgs", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT( FRasterizePassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters)

	SHADER_PARAMETER( FIntVector4,	VisualizeConfig )
	SHADER_PARAMETER( FIntVector4,	SOAStrides )
	SHADER_PARAMETER( uint32,		MaxClusters )
	SHADER_PARAMETER( uint32,		RenderFlags )
	SHADER_PARAMETER( uint32,       RasterStateReverseCull )
	
	SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageData )
	SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageHeaders )

	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FPackedView >,	InViews )
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< float4 >,		InstanceDynamicData )
#endif
	SHADER_PARAMETER_RDG_BUFFER_SRV( ByteAddressBuffer,					VisibleClustersSWHW )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FUintVector2 >,	InTotalPrevDrawClusters )

	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< uint >,		OutDepthBuffer )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< UlongType >,	OutVisBuffer64 )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< UlongType >,	OutDbgBuffer64 )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< uint >,		OutDbgBuffer32 )
	SHADER_PARAMETER_RDG_TEXTURE_UAV( RWTexture2D< uint >,		LockBuffer )

	SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, InClusterOffsetSWHW )

	SHADER_PARAMETER_RDG_BUFFER( Buffer< uint >, IndirectArgs )

	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualTargetParameters, VirtualShadowMap)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FMicropolyRasterizeCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FMicropolyRasterizeCS );
	SHADER_USE_PARAMETER_STRUCT( FMicropolyRasterizeCS, FNaniteShader );

	class FAddClusterOffset : SHADER_PERMUTATION_BOOL("ADD_CLUSTER_OFFSET");
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FHasPrevDrawData : SHADER_PERMUTATION_BOOL( "HAS_PREV_DRAW_DATA");
	class FRasterTechniqueDim : SHADER_PERMUTATION_INT("RASTER_TECHNIQUE", (int32)Nanite::ERasterTechnique::NumTechniques);
	class FDebugVisualizeDim : SHADER_PERMUTATION_BOOL("DEBUG_VISUALIZE");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	using FPermutationDomain = TShaderPermutationDomain<FAddClusterOffset, FMultiViewDim, FHasPrevDrawData, FRasterTechniqueDim, FDebugVisualizeDim, FNearClipDim, FVirtualTextureTargetDim, FClusterPerPageDim>;

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
			 && Parameters.Platform != EShaderPlatform::SP_PCD3D_SM5)
		{
			// Only supporting vendor extensions on PC D3D SM5+
			return false;
		}

		if (PermutationVector.Get<FRasterTechniqueDim>() == (int32)Nanite::ERasterTechnique::DepthOnly &&
			PermutationVector.Get<FDebugVisualizeDim>() )
		{
			// Debug not supported with depth only
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
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SOFTWARE_RASTER"), 1);

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
IMPLEMENT_GLOBAL_SHADER(FMicropolyRasterizeCS, "/Engine/Private/Nanite/Rasterizer.usf", "MicropolyRasterize", SF_Compute);

class FHWRasterizeVS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FHWRasterizeVS );
	SHADER_USE_PARAMETER_STRUCT( FHWRasterizeVS, FNaniteShader );

	class FRasterTechniqueDim : SHADER_PERMUTATION_INT("RASTER_TECHNIQUE", (int32)Nanite::ERasterTechnique::NumTechniques);
	class FAddClusterOffset : SHADER_PERMUTATION_BOOL("ADD_CLUSTER_OFFSET");
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FPrimShaderDim : SHADER_PERMUTATION_BOOL("NANITE_PRIM_SHADER");
	class FPrimShaderCullDim : SHADER_PERMUTATION_BOOL("NANITE_PRIM_SHADER_CULL");
	class FAutoShaderCullDim : SHADER_PERMUTATION_BOOL("NANITE_AUTO_SHADER_CULL");
	class FHasPrevDrawData : SHADER_PERMUTATION_BOOL("HAS_PREV_DRAW_DATA");
	class FDebugVisualizeDim : SHADER_PERMUTATION_BOOL("DEBUG_VISUALIZE");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	using FPermutationDomain = TShaderPermutationDomain<FRasterTechniqueDim, FAddClusterOffset, FMultiViewDim, FPrimShaderDim, FPrimShaderCullDim, FAutoShaderCullDim, FHasPrevDrawData, FDebugVisualizeDim, FNearClipDim, FVirtualTextureTargetDim, FClusterPerPageDim>;

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
			&& Parameters.Platform != EShaderPlatform::SP_PCD3D_SM5)
		{
			// Only supporting vendor extensions on PC D3D SM5+
			return false;
		}

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::DepthOnly) &&
			PermutationVector.Get<FDebugVisualizeDim>())
		{
			// Debug not supported with depth only
			return false;
		}

		if ((PermutationVector.Get<FPrimShaderDim>() || PermutationVector.Get<FAutoShaderCullDim>()) &&
			!FDataDrivenShaderPlatformInfo::GetSupportsPrimitiveShaders(Parameters.Platform))
		{
			// Only some platforms support primitive shaders.
			return false;
		}

		if (PermutationVector.Get<FPrimShaderCullDim>() && !PermutationVector.Get<FPrimShaderDim>())
		{
			// Culling in the primitive shader unsurprisingly needs a primitive shader.
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
		
		if (PermutationVector.Get<FPrimShaderDim>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_VertexToPrimitiveShader);
		}
		else if (PermutationVector.Get<FAutoShaderCullDim>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_VertexUseAutoCulling);
		}

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

class FHWRasterizePS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FHWRasterizePS );
	SHADER_USE_PARAMETER_STRUCT( FHWRasterizePS, FNaniteShader );

	class FRasterTechniqueDim : SHADER_PERMUTATION_INT("RASTER_TECHNIQUE", (int32)Nanite::ERasterTechnique::NumTechniques);
	class FMultiViewDim : SHADER_PERMUTATION_BOOL("NANITE_MULTI_VIEW");
	class FPrimShaderDim : SHADER_PERMUTATION_BOOL("NANITE_PRIM_SHADER");
	class FPrimShaderCullDim : SHADER_PERMUTATION_BOOL("NANITE_PRIM_SHADER_CULL");
	class FDebugVisualizeDim : SHADER_PERMUTATION_BOOL("DEBUG_VISUALIZE");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");

	using FPermutationDomain = TShaderPermutationDomain<FRasterTechniqueDim, FMultiViewDim, FPrimShaderDim, FPrimShaderCullDim, FDebugVisualizeDim, FVirtualTextureTargetDim, FClusterPerPageDim, FNearClipDim>;

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
			 && Parameters.Platform != EShaderPlatform::SP_PCD3D_SM5)
		{
			// Only supporting vendor extensions on PC D3D SM5+
			return false;
		}

		if (PermutationVector.Get<FRasterTechniqueDim>() == int32(Nanite::ERasterTechnique::DepthOnly) &&
			PermutationVector.Get<FDebugVisualizeDim>())
		{
			// Debug not supported with depth only
			return false;
		}

		if ((PermutationVector.Get<FPrimShaderDim>() || PermutationVector.Get<FPrimShaderCullDim>()) &&
			!FDataDrivenShaderPlatformInfo::GetSupportsPrimitiveShaders(Parameters.Platform))
		{
			// Only some platforms support primitive shaders.
			return false;
		}

		if (PermutationVector.Get<FPrimShaderCullDim>() && !PermutationVector.Get<FPrimShaderDim>())
		{
			// Culling in the primitive shader unsurprisingly needs a primitive shader.
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

class FNaniteMarkStencilPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FNaniteMarkStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FNaniteMarkStencilPS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FNaniteMarkStencilPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "MarkStencilPS", SF_Pixel);

class FNaniteEmitMaterialIdPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FNaniteEmitMaterialIdPS);
	SHADER_USE_PARAMETER_STRUCT(FNaniteEmitMaterialIdPS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, DummyZero)

		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)

		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FNaniteEmitMaterialIdPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitMaterialIdPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FNaniteVS, "/Engine/Private/Nanite/ExportGBuffer.usf", "FullScreenVS", SF_Vertex);

class FEmitDepthPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitDepthPS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE( Texture2D<UlongType>,	VisBuffer64 )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitDepthPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitDepthPS", SF_Pixel);

class FEmitStencilPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitStencilPS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitStencilPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitStencilPS", SF_Pixel);

class FEmitShadowMapPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitShadowMapPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitShadowMapPS, FNaniteShader);

	class FDepthInputTypeDim : SHADER_PERMUTATION_INT("DEPTH_INPUT_TYPE", 2);
	class FDepthOutputTypeDim : SHADER_PERMUTATION_INT("DEPTH_OUTPUT_TYPE", 3);
	using FPermutationDomain = TShaderPermutationDomain< FDepthInputTypeDim, FDepthOutputTypeDim >;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FNaniteShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
		FVirtualShadowMapArray::SetShaderDefines( OutEnvironment );
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapCommonParameters, CommonVSMParameters)
		SHADER_PARAMETER( FIntPoint, SourceOffset )
		SHADER_PARAMETER( float, ViewToClip22 )
		SHADER_PARAMETER( float, DepthBias )
		SHADER_PARAMETER( uint32, ShadowMapID )

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, PageTable)
		SHADER_PARAMETER_RDG_TEXTURE( Texture2D<uint>,	DepthBuffer )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitShadowMapPS, "/Engine/Private/Nanite/EmitShadow.usf", "EmitShadowMapPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FEmitCubemapShadowParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DepthBuffer)
	SHADER_PARAMETER( uint32, CubemapFaceIndex )
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FEmitCubemapShadowVS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitCubemapShadowVS);
	SHADER_USE_PARAMETER_STRUCT(FEmitCubemapShadowVS, FNaniteShader);

	class FUseGeometryShader : SHADER_PERMUTATION_BOOL("USE_GEOMETRY_SHADER");
	using FPermutationDomain = TShaderPermutationDomain<FUseGeometryShader>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		
		if (PermutationVector.Get<FUseGeometryShader>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_VertexToGeometryShader);
		}
	}

	using FParameters = FEmitCubemapShadowParameters;
};
IMPLEMENT_GLOBAL_SHADER(FEmitCubemapShadowVS, "/Engine/Private/Nanite/EmitShadow.usf", "EmitCubemapShadowVS", SF_Vertex);

class FEmitCubemapShadowGS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitCubemapShadowGS);
	SHADER_USE_PARAMETER_STRUCT(FEmitCubemapShadowGS, FNaniteShader);

	using FParameters = FEmitCubemapShadowParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsGeometryShaders(Parameters.Platform) && DoesPlatformSupportNanite(Parameters.Platform);
	}
	
	static void ModifyCompilationEnvironment( const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_GEOMETRY_SHADER"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FEmitCubemapShadowGS, "/Engine/Private/Nanite/EmitShadow.usf", "EmitCubemapShadowGS", SF_Geometry);

class FEmitCubemapShadowPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitCubemapShadowPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitCubemapShadowPS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
	}
	
	using FParameters = FEmitCubemapShadowParameters;
};
IMPLEMENT_GLOBAL_SHADER(FEmitCubemapShadowPS, "/Engine/Private/Nanite/EmitShadow.usf", "EmitCubemapShadowPS", SF_Pixel);

class FEmitHitProxyIdPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitHitProxyIdPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitHitProxyIdPS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageData )
		SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageHeaders )

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)

		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialHitProxyTable)
		
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitHitProxyIdPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitHitProxyIdPS", SF_Pixel);

class FEmitEditorSelectionDepthPS : public FNaniteShader
{
public:
	DECLARE_GLOBAL_SHADER(FEmitEditorSelectionDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitEditorSelectionDepthPS, FNaniteShader);

	using FParameters = FNaniteSelectionOutlineParameters;

	class FSearchBufferCountDim : SHADER_PERMUTATION_INT("EDITOR_SELECTED_BUFFER_COUNT_LOG_2", 25);
	using FPermutationDomain = TShaderPermutationDomain<FSearchBufferCountDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		uint32 SelectedBufferCount = 1u << (uint32)PermutationVector.Get<FSearchBufferCountDim>();
		OutEnvironment.SetDefine(TEXT("EDITOR_SELECTED_BUFFER_COUNT"), SelectedBufferCount);
	}
};
IMPLEMENT_GLOBAL_SHADER(FEmitEditorSelectionDepthPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitEditorSelectionDepthPS", SF_Pixel);

class FDebugVisualizeCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FDebugVisualizeCS);
	SHADER_USE_PARAMETER_STRUCT(FDebugVisualizeCS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_VIEW_UNIFORM_BUFFER"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
		SHADER_PARAMETER(FIntVector4, VisualizeConfig)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER(uint32, MaxClusters)
		SHADER_PARAMETER(uint32, RenderFlags)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, InstanceDynamicData)
#endif
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, DbgBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DbgBuffer32)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialHitProxyTable)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDebugVisualizeCS, "/Engine/Private/Nanite/DebugVisualize.usf", "DebugVisualize", SF_Compute);

class FDepthExportCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FDepthExportCS);
	SHADER_USE_PARAMETER_STRUCT(FDepthExportCS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER(FIntVector4, DepthExportConfig)
		SHADER_PARAMETER(FIntVector4, ViewRect)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, SceneHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, SceneStencil)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, MaterialHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, MaterialDepth)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, VisibleMaterials)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDepthExportCS, "/Engine/Private/Nanite/DepthExport.usf", "DepthExport", SF_Compute);

class FReduceMaterialRangeCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FReduceMaterialRangeCS);
	SHADER_USE_PARAMETER_STRUCT(FReduceMaterialRangeCS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER(FIntPoint, FetchClamp)
		SHADER_PARAMETER(uint32, CullingMode)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, MaterialRange)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
		END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FReduceMaterialRangeCS, "/Engine/Private/Nanite/MaterialCulling.usf", "ReduceMaterialRange", SF_Compute);

// TODO: Move to common location outside of Nanite
class FHTileVisualizeCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FHTileVisualizeCS);
	SHADER_USE_PARAMETER_STRUCT(FHTileVisualizeCS, FNaniteShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, HTileBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HTileDisplay)
		SHADER_PARAMETER(FIntVector4, HTileConfig)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FHTileVisualizeCS, "/Engine/Private/HTileVisualize.usf", "VisualizeHTile", SF_Compute);

class FCalculateStatsIndirectArgsCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FCalculateStatsIndirectArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FCalculateStatsIndirectArgsCS, FNaniteShader);

	class FTwoPassCullingDim : SHADER_PERMUTATION_BOOL( "TWO_PASS_CULLING" );
	using FPermutationDomain = TShaderPermutationDomain<FTwoPassCullingDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER(uint32, RenderFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FNaniteStats>, OutStatsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutStatsArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, MainPassRasterizeArgsSWHW)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PostPassRasterizeArgsSWHW)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCalculateStatsIndirectArgsCS, "/Engine/Private/Nanite/PrintStats.usf", "CalculateStatsArgs", SF_Compute);

class FCalculateStatsCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FCalculateStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FCalculateStatsCS, FNaniteShader );

	class FTwoPassCullingDim : SHADER_PERMUTATION_BOOL("TWO_PASS_CULLING");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	using FPermutationDomain = TShaderPermutationDomain<FTwoPassCullingDim, FVirtualTextureTargetDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( FIntVector4, SOAStrides )
		SHADER_PARAMETER( uint32, MaxClusters )
		SHADER_PARAMETER( uint32, RenderFlags )

		SHADER_PARAMETER_SRV( ByteAddressBuffer,	ClusterPageData )
		SHADER_PARAMETER_SRV( ByteAddressBuffer,	ClusterPageHeaders )

		SHADER_PARAMETER_RDG_BUFFER_SRV( ByteAddressBuffer, VisibleClustersSWHW )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer<FNaniteStats>, OutStatsBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, MainPassRasterizeArgsSWHW )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, PostPassRasterizeArgsSWHW )
		SHADER_PARAMETER_RDG_BUFFER( Buffer< uint >, StatsArgs )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCalculateStatsCS, "/Engine/Private/Nanite/PrintStats.usf", "CalculateStats", SF_Compute);

class FPrintStatsCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FPrintStatsCS );
	SHADER_USE_PARAMETER_STRUCT( FPrintStatsCS, FNaniteShader );

	class FTwoPassCullingDim : SHADER_PERMUTATION_BOOL( "TWO_PASS_CULLING" );
	using FPermutationDomain = TShaderPermutationDomain<FTwoPassCullingDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER(uint32, PackedTriClusterSize)
		SHADER_PARAMETER(uint32, RenderFlags)
		SHADER_PARAMETER(uint32, DebugFlags)

		SHADER_PARAMETER_STRUCT_INCLUDE( ShaderPrint::FShaderParameters, ShaderPrintStruct )

		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer<FNaniteStats>, InStatsBuffer )

		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, MainPassCandidateClustersArgs )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, PostPassCandidateClustersArgs )

		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, MainPassRasterizeArgsSWHW )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, PostPassRasterizeArgsSWHW )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPrintStatsCS, "/Engine/Private/Nanite/PrintStats.usf", "PrintStats", SF_Compute);

FNaniteDrawListContext::FNaniteDrawListContext
(
	FCriticalSection& InNaniteDrawCommandLock,
	FStateBucketMap& InNaniteDrawCommands
) 
: NaniteDrawCommandLock(InNaniteDrawCommandLock)
, NaniteDrawCommands(InNaniteDrawCommands)
{
}

FMeshDrawCommand& FNaniteDrawListContext::AddCommand(FMeshDrawCommand& Initializer, uint32 NumElements)
{
	{
		MeshDrawCommandForStateBucketing.~FMeshDrawCommand();
		new(&MeshDrawCommandForStateBucketing) FMeshDrawCommand();
	}

	MeshDrawCommandForStateBucketing = Initializer;
	return MeshDrawCommandForStateBucketing;
}

void FNaniteDrawListContext::FinalizeCommand(
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
	)
{
	FGraphicsMinimalPipelineStateId PipelineId;
	PipelineId = FGraphicsMinimalPipelineStateId::GetPersistentId(PipelineState);

	MeshDrawCommand.SetDrawParametersAndFinalize(MeshBatch, BatchElementIndex, PipelineId, ShadersForDebugging);

	check(UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel));

	Experimental::FHashElementId SetId;
	auto hash = NaniteDrawCommands.ComputeHash(MeshDrawCommand);
	{
		FScopeLock Lock(&NaniteDrawCommandLock);

#if UE_BUILD_DEBUG
		FMeshDrawCommand MeshDrawCommandDebug = FMeshDrawCommand(MeshDrawCommand);
		check(MeshDrawCommandDebug.ShaderBindings.GetDynamicInstancingHash() == MeshDrawCommand.ShaderBindings.GetDynamicInstancingHash());
		check(MeshDrawCommandDebug.GetDynamicInstancingHash() == MeshDrawCommand.GetDynamicInstancingHash());
#endif
		SetId = NaniteDrawCommands.FindOrAddIdByHash(hash, MeshDrawCommand, FMeshDrawCommandCount());
		NaniteDrawCommands.GetByElementId(SetId).Value.Num++;

#if MESH_DRAW_COMMAND_DEBUG_DATA
		if (NaniteDrawCommands.GetByElementId(SetId).Value.Num == 1)
		{
			MeshDrawCommand.ClearDebugPrimitiveSceneProxy(); //When using State Buckets multiple PrimitiveSceneProxies use the same MeshDrawCommand, so The PrimitiveSceneProxy pointer can't be stored.
		}
#endif
	}

	CommandInfo.SetStateBucketId(SetId.GetIndex());
}

FNaniteMeshProcessor::FNaniteMeshProcessor(
	const FScene* InScene, 
	ERHIFeatureLevel::Type InFeatureLevel,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InDrawRenderState, 
	FMeshPassDrawListContext* InDrawListContext
	) 
	: FMeshPassProcessor(InScene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InDrawRenderState)
{
	check(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));
}

using FNanitePassShaders = TMeshProcessorShaders<FNaniteVS, FBaseHS, FBaseDS, TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>>;

void FNaniteMeshProcessor::AddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch, 
	uint64 BatchElementMask, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, 
	int32 StaticMeshId /*= -1 */
	)
{
	LLM_SCOPE(ELLMTag::Nanite);

	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

	const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();

	check(BlendMode == BLEND_Opaque);
	check(Material.GetMaterialDomain() == MD_Surface);

	const bool bRenderSkylight = Scene && Scene->ShouldRenderSkylightInBasePass(BlendMode) && ShadingModels != MSM_Unlit;
	const bool bRenderAtmosphericFog = IsTranslucentBlendMode(BlendMode) && (Scene && Scene->HasAtmosphericFog() && Scene->ReadOnlyCVARCache.bEnableAtmosphericFog);

	// Check for a cached light-map.
	const bool bIsLitMaterial = ShadingModels.IsLit();
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnRenderThread() != 0);

	const FLightMapInteraction LightMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
		? MeshBatch.LCI->GetLightMapInteraction(FeatureLevel)
		: FLightMapInteraction();

	// force LQ light maps based on system settings
	const bool bPlatformAllowsHighQualityLightMaps = AllowHighQualityLightmaps(FeatureLevel);
	const bool bAllowHighQualityLightMaps = bPlatformAllowsHighQualityLightMaps && LightMapInteraction.AllowsHighQualityLightmaps();

	static const auto CVarSupportLowQualityLightmap = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportLowQualityLightmaps"));
	const bool bAllowLowQualityLightMaps = (!CVarSupportLowQualityLightmap) || (CVarSupportLowQualityLightmap->GetValueOnAnyThread() != 0);

	const bool bAllowIndirectLightingCache = Scene && Scene->PrecomputedLightVolumes.Num() > 0;
	const bool bUseVolumetricLightmap = Scene && Scene->VolumetricLightmapSceneData.HasData();

	// Determine light map policy type
	ELightMapPolicyType SelectedLightMapPolicyType = ELightMapPolicyType::LMP_NO_LIGHTMAP;
	if (LightMapInteraction.GetType() == LMIT_Texture)
	{
// TODO: See shelved CL 9283534
#if 0
		if (bAllowHighQualityLightMaps)
		{
			const FShadowMapInteraction ShadowMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
				? MeshBatch.LCI->GetShadowMapInteraction(FeatureLevel)
				: FShadowMapInteraction();

			if (ShadowMapInteraction.GetType() == SMIT_Texture)
			{
				SelectedLightMapPolicyType = LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP;
			}
			else
			{
				SelectedLightMapPolicyType = LMP_HQ_LIGHTMAP;
			}
		}
		else if (bAllowLowQualityLightMaps)
		{
			SelectedLightMapPolicyType = LMP_LQ_LIGHTMAP;
		}
#endif
	}
	else
	{
		if (bIsLitMaterial
			&& bAllowStaticLighting
			&& Scene
			&& Scene->VolumetricLightmapSceneData.HasData()
			&& PrimitiveSceneProxy
			&& (PrimitiveSceneProxy->IsMovable()
				|| PrimitiveSceneProxy->NeedsUnbuiltPreviewLighting()
				|| PrimitiveSceneProxy->GetLightmapType() == ELightmapType::ForceVolumetric))
		{
			SelectedLightMapPolicyType = LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING;
		}
		else if (bIsLitMaterial
			&& IsIndirectLightingCacheAllowed(FeatureLevel)
			&& Scene
			&& Scene->PrecomputedLightVolumes.Num() > 0
			&& PrimitiveSceneProxy)
		{
			const FIndirectLightingCacheAllocation* IndirectLightingCacheAllocation = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->IndirectLightingCacheAllocation;
			const bool bPrimitiveIsMovable = PrimitiveSceneProxy->IsMovable();
			const bool bPrimitiveUsesILC = PrimitiveSceneProxy->GetIndirectLightingCacheQuality() != ILCQ_Off;

			// Use the indirect lighting cache shaders if the object has a cache allocation
			// This happens for objects with unbuilt lighting
			if (bPrimitiveUsesILC &&
				((IndirectLightingCacheAllocation && IndirectLightingCacheAllocation->IsValid())
				// Use the indirect lighting cache shaders if the object is movable, it may not have a cache allocation yet because that is done in InitViews
				// And movable objects are sometimes rendered in the static draw lists
				|| bPrimitiveIsMovable))
			{
				if (CanIndirectLightingCacheUseVolumeTexture(FeatureLevel)
					&& ((IndirectLightingCacheAllocation && !IndirectLightingCacheAllocation->bPointSample)
						|| (bPrimitiveIsMovable && PrimitiveSceneProxy->GetIndirectLightingCacheQuality() == ILCQ_Volume)))
				{
					// Use a light map policy that supports reading indirect lighting from a volume texture for dynamic objects
					SelectedLightMapPolicyType = LMP_CACHED_VOLUME_INDIRECT_LIGHTING;
				}
				else
				{
					// Use a light map policy that supports reading indirect lighting from a single SH sample
					SelectedLightMapPolicyType = LMP_CACHED_POINT_INDIRECT_LIGHTING;
				}
			}
		}
	}

	TShaderMapRef<FNaniteVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
	TShaderRef<TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>> BasePassPixelShader;

	GetBasePassShaders<FUniformLightMapPolicy>(
		Material,
		MeshBatch.VertexFactory->GetType(),
		SelectedLightMapPolicyType,
		FeatureLevel,
		bRenderAtmosphericFog,
		bRenderSkylight,
		false,
		nullptr,
		nullptr,
		nullptr,
		&BasePassPixelShader
		);

	FNanitePassShaders PassShaders;
	PassShaders.VertexShader = VertexShader;
	PassShaders.PixelShader = BasePassPixelShader;

	TBasePassShaderElementData<FUniformLightMapPolicy> ShaderElementData(nullptr);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, nullptr, MeshBatch, -1, false);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		nullptr,
		MaterialRenderProxy,
		Material,
		PassDrawRenderState,
		PassShaders,
		FM_Solid,
		CM_None,
		FMeshDrawCommandSortKey::Default,
		EMeshPassFeatures::Default,
		ShaderElementData
	);
}

FMeshPassProcessor* CreateNaniteMeshProcessor(
	const FScene* Scene, 
	const FSceneView* InViewIfDynamicMeshCommand, 
	FMeshPassDrawListContext* InDrawListContext
	)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.OpaqueBasePassUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	PassDrawRenderState.SetNaniteUniformBuffer(Scene->UniformBuffers.NaniteUniformBuffer);

	if (UseComputeDepthExport())
	{
		SetupBasePassState(FExclusiveDepthStencil::DepthWrite_StencilNop, false, PassDrawRenderState);
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal>::GetRHI());
		PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilNop);
	}
	else
	{
		SetupBasePassState(FExclusiveDepthStencil::DepthWrite_StencilWrite, false, PassDrawRenderState);
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal, true, CF_Equal>::GetRHI());
		PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead);
		PassDrawRenderState.SetStencilRef(STENCIL_SANDBOX_MASK);
	}

	return new(FMemStack::Get()) FNaniteMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext);
}

FNaniteMaterialTables::FNaniteMaterialTables(uint32 InMaxMaterials)
: MaxMaterials(InMaxMaterials)
{
	check(MaxMaterials > 0);
}

FNaniteMaterialTables::~FNaniteMaterialTables()
{
	Release();
}

void FNaniteMaterialTables::Release()
{
	DepthTableUploadBuffer.Release();
	DepthTableDataBuffer.Release();
	HitProxyTableUploadBuffer.Release();
	HitProxyTableDataBuffer.Release();
}

bool FNaniteMaterialTables::Begin(FRHICommandListImmediate& RHICmdList, uint32 NumPrimitives, uint32 InNumPrimitiveUpdates)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE(ELLMTag::Nanite);

	check(NumPrimitiveUpdates == 0);
	check(NumDepthTableUpdates == 0);
#if WITH_EDITOR
	check(NumHitProxyTableUpdates == 0);
#endif
	NumPrimitiveUpdates = InNumPrimitiveUpdates;

	TArray<FRHIUnorderedAccessView*, TInlineAllocator<2>> UAVs;

	const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumPrimitives * MaxMaterials, 256u));
	bool bResized = false;
	bResized |= ResizeResourceIfNeeded(RHICmdList, DepthTableDataBuffer, SizeReserve * sizeof(uint32), TEXT("DepthTableDataBuffer"));
	if (bResized)
	{
		UAVs.Add(DepthTableDataBuffer.UAV);
	}
#if WITH_EDITOR
	bResized |= ResizeResourceIfNeeded(RHICmdList, HitProxyTableDataBuffer, SizeReserve * sizeof(uint32), TEXT("HitProxyTableDataBuffer"));
	if (bResized)
	{
		UAVs.Add(HitProxyTableDataBuffer.UAV);
	}
#endif // WITH_EDITOR

	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UAVs.GetData(), UAVs.Num());

	if (NumPrimitiveUpdates > 0)
	{
		DepthTableUploadBuffer.Init(NumPrimitiveUpdates * MaxMaterials, sizeof(uint32), false, TEXT("DepthTableUploadBuffer"));
	#if WITH_EDITOR
		HitProxyTableUploadBuffer.Init(NumPrimitiveUpdates * MaxMaterials, sizeof(uint32), false, TEXT("HitProxyTableUploadBuffer"));
	#endif
	}

	return bResized;
}

void* FNaniteMaterialTables::GetDepthTablePtr(uint32 PrimitiveIndex, uint32 EntryCount)
{
	++NumDepthTableUpdates;
	const uint32 BaseIndex = PrimitiveIndex * MaxMaterials;
	return DepthTableUploadBuffer.Add_GetRef(BaseIndex, EntryCount);
}

#if WITH_EDITOR
void* FNaniteMaterialTables::GetHitProxyTablePtr(uint32 PrimitiveIndex, uint32 EntryCount)
{
	++NumHitProxyTableUpdates;
	const uint32 BaseIndex = PrimitiveIndex * MaxMaterials;
	return HitProxyTableUploadBuffer.Add_GetRef(BaseIndex, EntryCount);
}
#endif

void FNaniteMaterialTables::Finish(FRHICommandListImmediate& RHICmdList)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE(ELLMTag::Nanite);

#if WITH_EDITOR
	check(NumHitProxyTableUpdates <= NumPrimitiveUpdates);
#endif
	check(NumDepthTableUpdates <= NumPrimitiveUpdates);
	if (NumPrimitiveUpdates == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENTF(RHICmdList, UpdateMaterialTables, TEXT("UpdateMaterialTables PrimitivesToUpdate = %u"), NumPrimitiveUpdates);

	TArray<FRHIUnorderedAccessView*, TInlineAllocator<2>> UploadUAVs;
	UploadUAVs.Add(DepthTableDataBuffer.UAV);
#if WITH_EDITOR
	UploadUAVs.Add(HitProxyTableDataBuffer.UAV);
#endif

	RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UploadUAVs.GetData(), UploadUAVs.Num());

	DepthTableUploadBuffer.ResourceUploadTo(RHICmdList, DepthTableDataBuffer, false);
#if WITH_EDITOR
	HitProxyTableUploadBuffer.ResourceUploadTo(RHICmdList, HitProxyTableDataBuffer, false);
#endif
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UploadUAVs.GetData(), UploadUAVs.Num());

	NumDepthTableUpdates = 0;
#if WITH_EDITOR
	NumHitProxyTableUpdates = 0;
#endif
	NumPrimitiveUpdates = 0;
}

static_assert( NUM_CULLING_FLAG_BITS + MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS + MAX_INSTANCES_BITS + MAX_GPU_PAGES_BITS + MAX_CLUSTERS_PER_PAGE_BITS <= 64, "FVisibleCluster fields don't fit in 64bits" );
static_assert( NUM_CULLING_FLAG_BITS + MAX_INSTANCES_BITS + MAX_NODES_PER_PRIMITIVE_BITS + MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS <= 64, "FVisibleNode fields don't fit in 64bits" );

namespace Nanite
{

FString GetFilterNameForLight(const FLightSceneProxy* LightProxy)
{
	FString LightFilterName;
	{
		FString FullLevelName = LightProxy->GetLevelName().ToString();
		const int32 LastSlashIndex = FullLevelName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		if (LastSlashIndex != INDEX_NONE)
		{
			FullLevelName.MidInline(LastSlashIndex + 1, FullLevelName.Len() - (LastSlashIndex + 1), false);
		}

		LightFilterName = FullLevelName + TEXT(".") + LightProxy->GetComponentName().ToString();
	}

	return LightFilterName;
}

bool IsStatFilterActive(const FString& FilterName)
{
	if (GNaniteDebugFlags == 0 || GNaniteShowStats == 0)
	{
		// Stats are disabled, do nothing.
		return false;
	}

	return (GNaniteStatsFilter == FilterName);
}

bool IsStatFilterActiveForLight(const FLightSceneProxy* LightProxy)
{
	if (GNaniteDebugFlags == 0 || GNaniteShowStats == 0)
	{
		return false;
	}

	const FString LightFilterName = Nanite::GetFilterNameForLight(LightProxy);
	return IsStatFilterActive(LightFilterName);
}

void ListStatFilters(FSceneRenderer* SceneRenderer)
{
	if (bNaniteListStatFilters && SceneRenderer)
	{
		UE_LOG(LogNanite, Warning, TEXT("** Available Filters **"));

		// Primary view is always available.
		UE_LOG(LogNanite, Warning, TEXT("Primary"));

		const bool bListShadows = CVarNaniteShadows.GetValueOnRenderThread() != 0;

		// Virtual shadow maps
		const auto& VirtualShadowMaps = SceneRenderer->SortedShadowsForShadowDepthPass.VirtualShadowMapShadows;
		if (bListShadows && VirtualShadowMaps.Num())
		{
			bool bHasDirectional = false;
			bool bHasPerspective = false;

			for (int32 VirtualShadowMapIndex = 0; VirtualShadowMapIndex < VirtualShadowMaps.Num(); ++VirtualShadowMapIndex)
			{
				FProjectedShadowInfo *ProjectedShadowInfo = VirtualShadowMaps[VirtualShadowMapIndex];
				if (ProjectedShadowInfo->ShouldClampToNearPlane())
				{
					bHasDirectional = true;
				}
				else
				{
					bHasPerspective = true;
				}

				if (bHasPerspective && bHasDirectional)
				{
					break;
				}
			}

			if (bHasDirectional)
			{
				UE_LOG(LogNanite, Warning, TEXT("VSM_Directional"));
			}

			if (bHasPerspective)
			{
				UE_LOG(LogNanite, Warning, TEXT("VSM_Perspective"));
			}
		}
		
		// Shadow map atlases
		if (bListShadows)
		{
			const auto& ShadowMapAtlases = SceneRenderer->SortedShadowsForShadowDepthPass.ShadowMapAtlases;
			for (int32 AtlasIndex = 0; AtlasIndex < ShadowMapAtlases.Num(); AtlasIndex++)
			{
				const FSortedShadowMapAtlas& ShadowMapAtlas = ShadowMapAtlases[AtlasIndex];
				for (int32 ShadowIndex = 0; ShadowIndex < ShadowMapAtlas.Shadows.Num(); ShadowIndex++)
				{
					FProjectedShadowInfo* ProjectedShadowInfo = ShadowMapAtlas.Shadows[ShadowIndex];
					if (!ProjectedShadowInfo->bNaniteGeometry || ProjectedShadowInfo->CacheMode == SDCM_MovablePrimitivesOnly)
					{
						continue;
					}

					const FLightSceneProxy* LightProxy = ProjectedShadowInfo->GetLightSceneInfo().Proxy;
					const FString LightFilterName = GetFilterNameForLight(ProjectedShadowInfo->GetLightSceneInfo().Proxy);
					UE_LOG(LogNanite, Warning, TEXT("Shadow Map Atlases: %s"), *LightFilterName);
				}
			}
		}

		// Shadow cube maps
		if (bListShadows)
		{
			const auto& ShadowCubeMaps = SceneRenderer->SortedShadowsForShadowDepthPass.ShadowMapCubemaps;
			for (int32 CubemapIndex = 0; CubemapIndex < ShadowCubeMaps.Num(); CubemapIndex++)
			{
				const FSortedShadowMapAtlas& ShadowMap = ShadowCubeMaps[CubemapIndex];
				check(ShadowMap.Shadows.Num() == 1);
				FProjectedShadowInfo* ProjectedShadowInfo = ShadowMap.Shadows[0];

				if (ProjectedShadowInfo->bNaniteGeometry && ProjectedShadowInfo->CacheMode != SDCM_MovablePrimitivesOnly)
				{
					// Get the base light filter name.
					FString CubeFilterName = GetFilterNameForLight(ProjectedShadowInfo->GetLightSceneInfo().Proxy);
					CubeFilterName.Append(TEXT("_Face_"));

					for (int32 CubemapFaceIndex = 0; CubemapFaceIndex < 6; CubemapFaceIndex++)
					{
						FString CubeFaceFilterName = CubeFilterName;
						CubeFaceFilterName.AppendInt(CubemapFaceIndex);
						UE_LOG(LogNanite, Warning, TEXT("Shadow Cube Map: %s"), *CubeFaceFilterName);
					}
				}
			}
		}
	}

	bNaniteListStatFilters = false;
}

static void AddPassInitNodesUAV( FRDGBuilder& GraphBuilder, FRDGBufferUAVRef UAVRef, bool bIsPostPass )
{
	LLM_SCOPE(ELLMTag::Nanite);

	const uint32 ThreadsPerGroup = 64;
	checkf(Nanite::FGlobalResources::GetMaxNodes() % ThreadsPerGroup == 0, TEXT("Max nodes must be divisible by ThreadsPerGroup"));

	FInitNodes_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitNodes_CS::FParameters >();
	PassParameters->OutNodes = UAVRef;
	PassParameters->InitNodesIsPostPass = bIsPostPass ? 1 : 0;

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader< FInitNodes_CS >();
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME( "Nanite::InitNodes" ),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(Nanite::FGlobalResources::GetMaxNodes(), ThreadsPerGroup)
	);
}

FCullingContext InitCullingContext(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TRefCountPtr<IPooledRenderTarget> &PrevHZB,
	const FIntRect &PrevHZBViewRect,
	bool bTwoPassOcclusion,
	bool bUpdateStreaming,
	bool bSupportsMultiplePasses,
	bool bForceHWRaster,
	bool bPrimaryContext
	)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE(ELLMTag::Nanite);

	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::InitCullingContext");

	INC_DWORD_STAT(STAT_NaniteCullingContexts);

	FCullingContext CullingContext = {};

	CullingContext.PrevHZB					= PrevHZB;
	CullingContext.PrevHZBViewRect			= PrevHZBViewRect;
	CullingContext.bTwoPassOcclusion		= PrevHZB != nullptr && bTwoPassOcclusion;
	CullingContext.bSupportsMultiplePasses	= bSupportsMultiplePasses;
	CullingContext.DrawPassIndex			= 0;
	CullingContext.RenderFlags				= 0;
	CullingContext.DebugFlags				= 0;

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
	if (GNaniteCacheInstanceDynamicData && !bSupportsMultiplePasses)
	{
		CullingContext.RenderFlags |= RENDER_FLAG_CACHE_INSTANCE_DYNAMIC_DATA;
	}
#endif

	if (bForceHWRaster)
	{
		CullingContext.RenderFlags |= RENDER_FLAG_FORCE_HW_RASTER;
	}

	if (UsePrimitiveShader())
	{
		CullingContext.RenderFlags |= RENDER_FLAG_PRIMITIVE_SHADER;
	}

	// TODO: Exclude from shipping builds
	if (GNaniteDebugFlags != 0)
	{
		if (GNaniteSphereCullingFrustum != 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_CULL_FRUSTUM_SPHERE;
		}

		if (GNaniteSphereCullingHZB != 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_CULL_HZB_SPHERE;
		}

		if (GNaniteBoxCullingFrustum != 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_CULL_FRUSTUM_BOX;
		}

		if (GNaniteBoxCullingHZB != 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_CULL_HZB_BOX;
		}

		if (GNaniteShowStats != 0)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_WRITE_STATS;
		}
	}

	// TODO: Might this not break if the view has overridden the InstanceSceneData?
	const uint32 NumSceneInstances = FMath::RoundUpToPowerOfTwo(Scene.GPUScene.InstanceDataAllocator.GetMaxSize());
	checkf(
		NumSceneInstances <= Nanite::FGlobalResources::GetMaxInstances(),
		TEXT("r.Nanite.MaxInstanceCount is set to %d, but the scene is trying to render %d instances, which is out of range. Please adjust the max instance count to a higher setting."),
		Nanite::FGlobalResources::GetMaxInstances(),
		NumSceneInstances
	);

	CullingContext.SOAStrides.X							= Scene.GPUScene.InstanceDataSOAStride;
	CullingContext.SOAStrides.Y							= NumSceneInstances;
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
	FRDGBufferDesc InstanceDynamicDataDesc				= FRDGBufferDesc::CreateStructuredDesc( 4, GNaniteCacheInstanceDynamicData ? ( NumSceneInstances * 24 * 4 ) : 1 ); // TODO: Move to scratch buffer (not used by Reverb)
	InstanceDynamicDataDesc.Usage						= EBufferUsageFlags(InstanceDynamicDataDesc.Usage | BUF_ByteAddressBuffer );
	CullingContext.InstanceDynamicData					= GraphBuilder.CreateBuffer(InstanceDynamicDataDesc, TEXT("InstanceDynamicData") );
#endif

	CullingContext.MainAndPostPassPersistentStates		= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateStructuredDesc( 12, 2 ), TEXT("MainAndPostPassPersistentStates") );

#if NANITE_USE_SCRATCH_BUFFERS
	if (bPrimaryContext)
	{
		CullingContext.VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetPrimaryVisibleClustersBufferRef(), TEXT("VisibleClustersSWHW"));
	}
	else
	{
		CullingContext.VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetScratchVisibleClustersBufferRef(), TEXT("VisibleClustersSWHW"));
	}
	CullingContext.MainPass.CandidateClusters			= GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetMainPassBuffers().ScratchCandidateClustersBuffer, TEXT("MainPass.CandidateClusters"));
#else
	FRDGBufferDesc VisibleClustersDesc					= FRDGBufferDesc::CreateStructuredDesc(4, 3 * Nanite::FGlobalResources::GetMaxClusters());	// Max clusters * sizeof(uint3)
	VisibleClustersDesc.Usage							= EBufferUsageFlags(VisibleClustersDesc.Usage | BUF_ByteAddressBuffer);
	CullingContext.VisibleClustersSWHW					= GraphBuilder.CreateBuffer(VisibleClustersDesc, TEXT("VisibleClustersSWHW"));
	CullingContext.MainPass.CandidateClusters			= GraphBuilder.CreateBuffer(VisibleClustersDesc, TEXT("MainPass.CandidateClusters"));
#endif

	CullingContext.MainPass.CandidateClustersArgs		= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateIndirectDesc( 4 ), TEXT("MainPass.CandidateClustersArgs") );	
	CullingContext.MainPass.RasterizeArgsSWHW			= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateIndirectDesc( 8 ), TEXT("MainPass.RasterizeArgsSWHW") );
	
	if( CullingContext.bTwoPassOcclusion )
	{
	#if NANITE_USE_SCRATCH_BUFFERS
		CullingContext.OccludedInstances				= GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetScratchOccludedInstancesBufferRef(), TEXT("OccludedInstances"));
		CullingContext.PostPass.CandidateClusters		= GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetPostPassBuffers().ScratchCandidateClustersBuffer, TEXT("PostPass.CandidateClusters"));
	#else
		CullingContext.OccludedInstances				= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, NumSceneInstances), TEXT("OccludedInstances"));
		CullingContext.PostPass.CandidateClusters		= GraphBuilder.CreateBuffer(VisibleClustersDesc, TEXT("PostPassCandidateClusters"));
	#endif

		CullingContext.OccludedInstancesArgs			= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateIndirectDesc( 4 ), TEXT("OccludedInstancesArgs") );
		CullingContext.PostPass.CandidateClustersArgs	= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateIndirectDesc( 4 ), TEXT( "PostPass.CandidateClustersArgs" ) );
		CullingContext.PostPass.RasterizeArgsSWHW		= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateIndirectDesc( 8 ), TEXT( "PostPass.RasterizeArgsSWHW" ) );
	}

	CullingContext.StreamingRequests = GraphBuilder.RegisterExternalBuffer(Nanite::GStreamingManager.GetStreamingRequestsBuffer(), TEXT("StreamingRequestsBuffer")); 
	if (bUpdateStreaming)
	{
		CullingContext.RenderFlags |= RENDER_FLAG_OUTPUT_STREAMING_REQUESTS;
	}

	if (bSupportsMultiplePasses)
	{
		CullingContext.TotalPrevDrawClustersBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(8, 1), TEXT("TotalPrevDrawClustersBuffer"));
	}

	// PersistentNodes: Starts out cleared to 0xFFFFFFFF. Only has to be cleared once as the hierarchy cull code clears nodes after they have been visited.	
	{
		TRefCountPtr<FPooledRDGBuffer>& MainPassNodesBufferRef = Nanite::GGlobalResources.GetMainPassBuffers().NodesBuffer;
		if (MainPassNodesBufferRef.IsValid())
		{
			CullingContext.MainPass.Nodes = GraphBuilder.RegisterExternalBuffer( MainPassNodesBufferRef, TEXT("MainPass.NodesBuffer") );
		}
		else
		{
			FRDGBufferDesc PersistentNodesDesc = FRDGBufferDesc::CreateStructuredDesc( 4, 2 * Nanite::FGlobalResources::GetMaxNodes());	// Max nodes * sizeof(uint2)
			PersistentNodesDesc.Usage = EBufferUsageFlags( PersistentNodesDesc.Usage | BUF_ByteAddressBuffer );

			CullingContext.MainPass.Nodes = GraphBuilder.CreateBuffer( PersistentNodesDesc, TEXT("MainPass.NodesBuffer") );
			AddPassInitNodesUAV( GraphBuilder, GraphBuilder.CreateUAV( CullingContext.MainPass.Nodes ), false );
		}
	}

	{
		TRefCountPtr<FPooledRDGBuffer>& PostPassNodesBufferRef = Nanite::GGlobalResources.GetPostPassBuffers().NodesBuffer;
		if (PostPassNodesBufferRef.IsValid())
		{
			CullingContext.PostPass.Nodes = GraphBuilder.RegisterExternalBuffer( PostPassNodesBufferRef, TEXT("PostPass.NodesBuffer") );
		}
		else
		{
			FRDGBufferDesc PersistentNodesDesc = FRDGBufferDesc::CreateStructuredDesc( 4, 4 * Nanite::FGlobalResources::GetMaxNodes());	// Max nodes * sizeof(uint4)
			PersistentNodesDesc.Usage = EBufferUsageFlags( PersistentNodesDesc.Usage | BUF_ByteAddressBuffer );

			CullingContext.PostPass.Nodes = GraphBuilder.CreateBuffer( PersistentNodesDesc, TEXT("PostPass.NodesBuffer") );
			AddPassInitNodesUAV( GraphBuilder, GraphBuilder.CreateUAV( CullingContext.PostPass.Nodes ), true );
		}
	}

	return CullingContext;
}

void AddPass_InstanceHierarchyAndClusterCull(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FCullingParameters& CullingParameters,
	const TArray<FPackedView, SceneRenderingAllocator> &Views,
	const uint32 NumPrimaryViews,
	const FCullingContext& CullingContext,
	const FRasterState& RasterState,
	const FGPUSceneParameters &GPUSceneParameters,
	uint32 CullingPass,
	FVirtualShadowMapArray *VirtualShadowMapArray,
	FVirtualTargetParameters &VirtualTargetParameters
	)
{
	LLM_SCOPE(ELLMTag::Nanite);

	// Currently only occlusion free multi-view routing.
	ensure(!VirtualShadowMapArray || CullingPass == CULLING_PASS_NO_OCCLUSION);
	// TODO: if we need this emulation feature by going through the view we can probably pass in the shader map as part of the context and get it out of the view at context-creation time
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	const bool bMultiView = Views.Num() > 1;

	FRDGBufferRef PageFlags = nullptr;
	FRDGBufferRef HPageFlags = nullptr;
	FRDGBufferRef HZBPageTable = nullptr;

	if( VirtualShadowMapArray )
	{
		RDG_GPU_STAT_SCOPE( GraphBuilder, NaniteInstanceCullVSM );

		PageFlags		= GraphBuilder.RegisterExternalBuffer( VirtualShadowMapArray->PageFlags, TEXT("PageFlags") );
		HPageFlags		= GraphBuilder.RegisterExternalBuffer( VirtualShadowMapArray->HPageFlags, TEXT("HPageFlags") );
		if( VirtualShadowMapArray->HZBPageTable )
			HZBPageTable	= GraphBuilder.RegisterExternalBuffer( VirtualShadowMapArray->HZBPageTable, TEXT("HZBPageTable") );
		else
			HZBPageTable	= GraphBuilder.RegisterExternalBuffer( VirtualShadowMapArray->PageTable, TEXT("HZBPageTable") );

		FInstanceCullVSM_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInstanceCullVSM_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->NumInstances = CullingContext.NumInstancesPreCull;
		PassParameters->CullingParameters = CullingParameters;

		PassParameters->VirtualShadowMap = VirtualTargetParameters;
		PassParameters->HZBPageTable	= GraphBuilder.CreateSRV( HZBPageTable, PF_R32G32_UINT );
		
		PassParameters->OutMainAndPostPassPersistentStates	= GraphBuilder.CreateUAV( CullingContext.MainAndPostPassPersistentStates );

		if (CullingContext.StatsBuffer)
		{
			PassParameters->OutStatsBuffer					= GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
		}

		check( CullingPass == CULLING_PASS_NO_OCCLUSION );
		check( CullingContext.InstanceDrawsBuffer == nullptr );
		PassParameters->OutNodes = GraphBuilder.CreateUAV( CullingContext.MainPass.Nodes );
		
		check(CullingContext.ViewsBuffer);

		FInstanceCullVSM_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInstanceCullVSM_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FInstanceCullVSM_CS::FDebugFlagsDim>(GNaniteDebugFlags != 0);

		auto ComputeShader = ShaderMap->GetShader<FInstanceCullVSM_CS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "Main Pass: InstanceCullVSM - No occlusion" ),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(CullingContext.NumInstancesPreCull, 64)
		);
	}
	else
	{
		RDG_GPU_STAT_SCOPE( GraphBuilder, NaniteInstanceCull );
		FInstanceCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInstanceCull_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->NumInstances = CullingContext.NumInstancesPreCull;
		PassParameters->CullingParameters = CullingParameters;
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		PassParameters->OutInstanceDynamicData				= GraphBuilder.CreateUAV( CullingContext.InstanceDynamicData );
#endif
		PassParameters->OutMainAndPostPassPersistentStates	= GraphBuilder.CreateUAV( CullingContext.MainAndPostPassPersistentStates );
		
		if (CullingContext.StatsBuffer)
		{
			PassParameters->OutStatsBuffer					= GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
		}

		if( CullingPass == CULLING_PASS_NO_OCCLUSION )
		{
			if( CullingContext.InstanceDrawsBuffer )
			{
				PassParameters->InInstanceDraws			= GraphBuilder.CreateSRV( CullingContext.InstanceDrawsBuffer );
			}
			PassParameters->OutNodes					= GraphBuilder.CreateUAV( CullingContext.MainPass.Nodes );
		}
		else if( CullingPass == CULLING_PASS_OCCLUSION_MAIN )
		{
			PassParameters->OutOccludedInstances		= GraphBuilder.CreateUAV( CullingContext.OccludedInstances );
			PassParameters->OutOccludedInstancesArgs	= GraphBuilder.CreateUAV( CullingContext.OccludedInstancesArgs );
			PassParameters->OutNodes					= GraphBuilder.CreateUAV( CullingContext.MainPass.Nodes );
		}
		else
		{
			PassParameters->InOccludedInstances			= GraphBuilder.CreateSRV( CullingContext.OccludedInstances );
			PassParameters->InOccludedInstancesArgs		= GraphBuilder.CreateSRV( CullingContext.OccludedInstancesArgs );
			PassParameters->OutNodes					= GraphBuilder.CreateUAV( CullingContext.PostPass.Nodes );
		}
		
		check(CullingContext.ViewsBuffer);

		FInstanceCull_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInstanceCull_CS::FCullingPassDim>(CullingPass);
		PermutationVector.Set<FInstanceCull_CS::FMultiViewDim>(bMultiView);
		PermutationVector.Set<FInstanceCull_CS::FInstanceDrawListDim>(CullingContext.InstanceDrawsBuffer != nullptr);
		PermutationVector.Set<FInstanceCull_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FInstanceCull_CS::FDebugFlagsDim>(GNaniteDebugFlags != 0);

		auto ComputeShader = ShaderMap->GetShader<FInstanceCull_CS>(PermutationVector);

		if( CullingPass == CULLING_PASS_NO_OCCLUSION || CullingPass == CULLING_PASS_OCCLUSION_MAIN )
		{
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				CullingPass == CULLING_PASS_NO_OCCLUSION ? 
					RDG_EVENT_NAME( "Main Pass: InstanceCull - No occlusion" ) :
					RDG_EVENT_NAME( "Main Pass: InstanceCull" ),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(CullingContext.NumInstancesPreCull, 64)
			);
		}
		else
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
	}

	{
		FPersistentHierarchicalCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FPersistentHierarchicalCull_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->CullingParameters		= CullingParameters;
		PassParameters->MaxNodes				= Nanite::FGlobalResources::GetMaxNodes();
		
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->HierarchyBuffer			= Nanite::GStreamingManager.GetHierarchySRV();

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		if( CullingContext.InstanceDynamicData )
		{
			PassParameters->InstanceDynamicData		= GraphBuilder.CreateSRV( CullingContext.InstanceDynamicData );
		}
#endif
		
		PassParameters->MainAndPostPassPersistentStates	= GraphBuilder.CreateUAV( CullingContext.MainAndPostPassPersistentStates );
		
		if( CullingPass == CULLING_PASS_NO_OCCLUSION || CullingPass == CULLING_PASS_OCCLUSION_MAIN )
		{
			PassParameters->OutCandidateClusters		= GraphBuilder.CreateUAV( CullingContext.MainPass.CandidateClusters );
			PassParameters->OutCandidateClustersArgs	= GraphBuilder.CreateUAV( CullingContext.MainPass.CandidateClustersArgs );
			PassParameters->InOutCandidateNodes			= GraphBuilder.CreateUAV( CullingContext.MainPass.Nodes );
			
			if( CullingPass == CULLING_PASS_OCCLUSION_MAIN )
			{
				PassParameters->OutOccludedClusters			= GraphBuilder.CreateUAV( CullingContext.PostPass.CandidateClusters );
				PassParameters->OutOccludedClustersArgs		= GraphBuilder.CreateUAV( CullingContext.PostPass.CandidateClustersArgs );
				PassParameters->OutOccludedNodes			= GraphBuilder.CreateUAV( CullingContext.PostPass.Nodes );
			}
		}
		else
		{
			PassParameters->OutCandidateClusters		= GraphBuilder.CreateUAV( CullingContext.PostPass.CandidateClusters );
			PassParameters->OutCandidateClustersArgs	= GraphBuilder.CreateUAV( CullingContext.PostPass.CandidateClustersArgs );
			PassParameters->InOutCandidateNodes			= GraphBuilder.CreateUAV( CullingContext.PostPass.Nodes );
		}

		PassParameters->OutStreamingRequests			= GraphBuilder.CreateUAV( CullingContext.StreamingRequests, PF_R32_UINT );
		
		if (VirtualShadowMapArray)
		{
			PassParameters->VirtualShadowMap = VirtualTargetParameters;
			PassParameters->HZBPageTable	= GraphBuilder.CreateSRV( HZBPageTable, PF_R32G32_UINT );
		}

		check(CullingContext.ViewsBuffer);

		FPersistentHierarchicalCull_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FPersistentHierarchicalCull_CS::FCullingPassDim>(CullingPass);
		PermutationVector.Set<FPersistentHierarchicalCull_CS::FMultiViewDim>(bMultiView);
		PermutationVector.Set<FPersistentHierarchicalCull_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FPersistentHierarchicalCull_CS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVector.Set<FPersistentHierarchicalCull_CS::FDebugFlagsDim>(GNaniteDebugFlags != 0);

		auto ComputeShader = ShaderMap->GetShader<FPersistentHierarchicalCull_CS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			CullingPass == CULLING_PASS_NO_OCCLUSION	? RDG_EVENT_NAME( "Main Pass: PersistentHierarchicalCull - No occlusion" ) :
			CullingPass == CULLING_PASS_OCCLUSION_MAIN	? RDG_EVENT_NAME( "Main Pass: PersistentHierarchicalCull" ) :
			RDG_EVENT_NAME( "Post Pass: PersistentHierarchicalCull" ),
			ComputeShader,
			PassParameters,
			FIntVector( NUM_PERSISTENT_THREADS, 1, 1 )
			);
	}

	{
		FCandidateCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCandidateCull_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->CullingParameters		= CullingParameters;

		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		PassParameters->InstanceDynamicData		= GraphBuilder.CreateSRV( CullingContext.InstanceDynamicData );
#endif

		PassParameters->OutVisibleClustersSWHW	= GraphBuilder.CreateUAV( CullingContext.VisibleClustersSWHW );

		if( CullingPass == CULLING_PASS_NO_OCCLUSION || CullingPass == CULLING_PASS_OCCLUSION_MAIN )
		{
			PassParameters->InCandidateClusters		= GraphBuilder.CreateSRV( CullingContext.MainPass.CandidateClusters );
			PassParameters->InCandidateClustersArgs	= GraphBuilder.CreateSRV( CullingContext.MainPass.CandidateClustersArgs );
			
			PassParameters->VisibleClustersArgsSWHW	= GraphBuilder.CreateUAV( CullingContext.MainPass.RasterizeArgsSWHW );
			
			if( CullingPass == CULLING_PASS_OCCLUSION_MAIN )
			{
				PassParameters->OutOccludedClusters = GraphBuilder.CreateUAV( CullingContext.PostPass.CandidateClusters );
				PassParameters->OutOccludedClustersArgs = GraphBuilder.CreateUAV( CullingContext.PostPass.CandidateClustersArgs );
			}
			
			PassParameters->IndirectArgs			= CullingContext.MainPass.CandidateClustersArgs;
		}
		else
		{
			PassParameters->InCandidateClusters		= GraphBuilder.CreateSRV( CullingContext.PostPass.CandidateClusters );
			PassParameters->InCandidateClustersArgs	= GraphBuilder.CreateSRV( CullingContext.PostPass.CandidateClustersArgs );

			PassParameters->OffsetClustersArgsSWHW	= GraphBuilder.CreateSRV( CullingContext.MainPass.RasterizeArgsSWHW );
			PassParameters->VisibleClustersArgsSWHW	= GraphBuilder.CreateUAV( CullingContext.PostPass.RasterizeArgsSWHW );
			
			PassParameters->IndirectArgs			= CullingContext.PostPass.CandidateClustersArgs;
		}

		check(CullingContext.DrawPassIndex == 0 || CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA); // sanity check
		if (CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA)
		{
			PassParameters->InTotalPrevDrawClusters = GraphBuilder.CreateSRV(CullingContext.TotalPrevDrawClustersBuffer);
		}
		else
		{
			FRDGBufferRef Dummy = GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetStructureBufferStride8(), TEXT("StructuredBufferStride8"));
			PassParameters->InTotalPrevDrawClusters = GraphBuilder.CreateSRV(Dummy);
		}

		if (VirtualShadowMapArray)
		{
			PassParameters->VirtualShadowMap = VirtualTargetParameters;
			PassParameters->OutDynamicCasterFlags = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArray->DynamicCasterPageFlags, TEXT("DynamicCasterFlags")), PF_R32_UINT);
			PassParameters->HZBPageTable	= GraphBuilder.CreateSRV( HZBPageTable, PF_R32G32_UINT );
		}

		if (CullingContext.StatsBuffer)
		{
			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
		}

		PassParameters->LargePageRectThreshold = CVarLargePageRectThreshold.GetValueOnRenderThread();

		check(CullingContext.ViewsBuffer);

		FCandidateCull_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FCandidateCull_CS::FCullingPassDim>(CullingPass);
		PermutationVector.Set<FCandidateCull_CS::FMultiViewDim>(bMultiView);
		PermutationVector.Set<FCandidateCull_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FCandidateCull_CS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVector.Set<FCandidateCull_CS::FClusterPerPageDim>( GNaniteClusterPerPage && VirtualShadowMapArray != nullptr );
		PermutationVector.Set<FCandidateCull_CS::FDebugFlagsDim>(GNaniteDebugFlags != 0);

		auto ComputeShader = ShaderMap->GetShader<FCandidateCull_CS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			CullingPass == CULLING_PASS_NO_OCCLUSION ? RDG_EVENT_NAME( "Main Pass: CandidateCull - No occlusion" ) :
			CullingPass == CULLING_PASS_OCCLUSION_MAIN ? RDG_EVENT_NAME( "Main Pass: CandidateCull" ) :
			RDG_EVENT_NAME( "Post Pass: CandidateCull" ),
			ComputeShader,
			PassParameters,
			PassParameters->IndirectArgs,
			0
		);
	}
}

void AddPass_Rasterize(
	FRDGBuilder& GraphBuilder,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	FIntVector4 SOAStrides, 
	uint32 RenderFlags,
	FRDGBufferRef ViewsBuffer,
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
	FRDGBufferRef InstanceDynamicData,
#endif
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

	LLM_SCOPE(ELLMTag::Nanite);

	check(RasterState.CullMode == CM_CW || RasterState.CullMode == CM_CCW);		// CM_None not implemented

	// TODO: if we need this emulation feature by going through the view we can probably pass in the shader map as part of the context and get it out of the view at context-creation time
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	FRasterizePassParameters* PassParameters = GraphBuilder.AllocParameters<FRasterizePassParameters>();

	PassParameters->ClusterPageData = GStreamingManager.GetClusterPageDataSRV();
	PassParameters->ClusterPageHeaders = GStreamingManager.GetClusterPageHeadersSRV();

	if (ViewsBuffer)
	{
		PassParameters->InViews = GraphBuilder.CreateSRV(ViewsBuffer);
	}

	PassParameters->GPUSceneParameters = GPUSceneParameters;
	PassParameters->VisualizeConfig = GetVisualizeConfig();
	PassParameters->SOAStrides = SOAStrides;
	PassParameters->MaxClusters = Nanite::FGlobalResources::GetMaxClusters();
	PassParameters->RenderFlags = RenderFlags;
	PassParameters->RasterStateReverseCull = RasterState.CullMode == CM_CCW ? 1 : 0;
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
	PassParameters->InstanceDynamicData = GraphBuilder.CreateSRV(InstanceDynamicData);
#endif
	PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
	if (RasterContext.RasterTechnique == ERasterTechnique::DepthOnly)
	{
		PassParameters->OutDepthBuffer = GraphBuilder.CreateUAV(RasterContext.DepthBuffer);
	}
	else
	{
		PassParameters->OutVisBuffer64 = GraphBuilder.CreateUAV(RasterContext.VisBuffer64);
	}

	if( ShouldExportDebugBuffers() )
	{
		PassParameters->OutDbgBuffer64 = GraphBuilder.CreateUAV(RasterContext.DbgBuffer64);
		PassParameters->OutDbgBuffer32 = GraphBuilder.CreateUAV(RasterContext.DbgBuffer32);
	}

	if( RasterContext.RasterTechnique == ERasterTechnique::LockBufferFallback )
	{
		PassParameters->LockBuffer = GraphBuilder.CreateUAV(RasterContext.LockBuffer);
	}
	
	if (VirtualShadowMapArray)
	{
		PassParameters->VirtualShadowMap = VirtualTargetParameters;
	}

	if (!bMainPass)
	{
		PassParameters->InClusterOffsetSWHW = GraphBuilder.CreateSRV(ClusterOffsetSWHW);
	}
	PassParameters->IndirectArgs = IndirectArgs;

	const bool bHavePrevDrawData = (RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA);
	if (bHavePrevDrawData)
	{
		PassParameters->InTotalPrevDrawClusters = GraphBuilder.CreateSRV(TotalPrevDrawClustersBuffer);
	}

	const ERasterTechnique Technique = RasterContext.RasterTechnique;
	const ERasterScheduling Scheduling = RasterContext.RasterScheduling;
	const bool bNearClip = RasterState.bNearClip;
	const bool bMultiView = Views.Num() > 1;


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
			ViewRect.Max = FIntPoint( FVirtualShadowMap::PageSize, FVirtualShadowMap::PageSize ) * FVirtualShadowMap::RasterWindowPages;
		else
			ViewRect.Max = FIntPoint( FVirtualShadowMap::VirtualMaxResolutionXY, FVirtualShadowMap::VirtualMaxResolutionXY );
	}

	GraphBuilder.AddPass(
		bMainPass ? RDG_EVENT_NAME("Main Pass: Rasterize") : RDG_EVENT_NAME("Post Pass: Rasterize"),
		PassParameters,
		ERDGPassFlags::Raster | ERDGPassFlags::Compute,
		[ PassParameters, ShaderMap, ViewRect, bMultiView, bHavePrevDrawData, Technique, Scheduling, bNearClip, VirtualShadowMapArray, bMainPass ]( FRHICommandListImmediate& RHICmdList )
		{
			FComputeFenceRHIRef AsyncRasterStartFence;
			FComputeFenceRHIRef AsyncRasterEndFence;
			FRHIAsyncComputeCommandListImmediate& RHICmdListComputeImmediate = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();

			// SW Rasterize
			FMicropolyRasterizeCS::FPermutationDomain PermutationVectorCS;
			PermutationVectorCS.Set<FMicropolyRasterizeCS::FAddClusterOffset>(bMainPass ? 0 : 1);
			PermutationVectorCS.Set<FMicropolyRasterizeCS::FMultiViewDim>( bMultiView );
			PermutationVectorCS.Set<FMicropolyRasterizeCS::FHasPrevDrawData>(bHavePrevDrawData);
			PermutationVectorCS.Set<FMicropolyRasterizeCS::FRasterTechniqueDim>(int32(Technique));
			PermutationVectorCS.Set<FMicropolyRasterizeCS::FDebugVisualizeDim>( ShouldExportDebugBuffers() && Technique != ERasterTechnique::DepthOnly );
			PermutationVectorCS.Set<FMicropolyRasterizeCS::FNearClipDim>(bNearClip);
			PermutationVectorCS.Set<FMicropolyRasterizeCS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
			PermutationVectorCS.Set<FMicropolyRasterizeCS::FClusterPerPageDim>( GNaniteClusterPerPage && VirtualShadowMapArray != nullptr );
			
			auto ComputeShader = ShaderMap->GetShader<FMicropolyRasterizeCS>(PermutationVectorCS);

			// Overlap SW and HW rasterizers?
			if (Scheduling == ERasterScheduling::HardwareAndSoftwareOverlap)
			{
				static FName AsyncRasterStartFenceName( TEXT("AsyncRasterStartFence") );
				static FName AsyncRasterEndFenceName( TEXT("AsyncRasterEndFence") );
				AsyncRasterStartFence = RHICmdList.CreateComputeFence( AsyncRasterStartFenceName );
				AsyncRasterEndFence = RHICmdList.CreateComputeFence( AsyncRasterEndFenceName );

				RHICmdList.TransitionResource( EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, nullptr, AsyncRasterStartFence );

				RHICmdListComputeImmediate.WaitComputeFence( AsyncRasterStartFence );
				RHICmdListComputeImmediate.SetComputeShader( ComputeShader.GetComputeShader() );
				SetShaderParameters( RHICmdListComputeImmediate, ComputeShader, ComputeShader.GetComputeShader(), *PassParameters );
				RHICmdListComputeImmediate.DispatchIndirectComputeShader( PassParameters->IndirectArgs->GetIndirectRHICallBuffer(), 0 );
				UnsetShaderUAVs( RHICmdListComputeImmediate, ComputeShader, ComputeShader.GetComputeShader() );
					
				RHICmdListComputeImmediate.TransitionResources( EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, nullptr, 0, AsyncRasterEndFence );
				FRHIAsyncComputeCommandListImmediate::ImmediateDispatch( RHICmdListComputeImmediate );
			}

			// HW rasterizer
			{
				const bool bUsePrimitiveShader = UsePrimitiveShader();
				bool bUsePrimitiveShaderCulling = UsePrimitiveShader() && GNanitePrimShaderCulling != 0;
				if (bUsePrimitiveShaderCulling)
				{
					if (Technique == ERasterTechnique::DepthOnly || VirtualShadowMapArray != nullptr)
					{
						// Shadow views
						bUsePrimitiveShaderCulling = GNanitePrimShaderCulling == 2 || GNanitePrimShaderCulling == 3;
					}
					else
					{
						// Primary view
						bUsePrimitiveShaderCulling = GNanitePrimShaderCulling == 1 || GNanitePrimShaderCulling == 3;
					}
				}

				const bool bUseAutoCullingShader =
					GRHISupportsPrimitiveShaders &&
					!bUsePrimitiveShader &&
					GNaniteAutoShaderCulling != 0;

				RHICmdList.SetViewport( ViewRect.Min.X, ViewRect.Min.Y, 0.0f, FMath::Min(ViewRect.Max.X, 32767), FMath::Min(ViewRect.Max.Y, 32767), 1.0f );
				
				FHWRasterizeVS::FPermutationDomain PermutationVectorVS;
				PermutationVectorVS.Set<FHWRasterizeVS::FRasterTechniqueDim>(int32(Technique));
				PermutationVectorVS.Set<FHWRasterizeVS::FAddClusterOffset>(bMainPass ? 0 : 1);
				PermutationVectorVS.Set<FHWRasterizeVS::FMultiViewDim>(bMultiView);
				PermutationVectorVS.Set<FHWRasterizeVS::FPrimShaderDim>(bUsePrimitiveShader);
				PermutationVectorVS.Set<FHWRasterizeVS::FPrimShaderCullDim>(bUsePrimitiveShaderCulling);
				PermutationVectorVS.Set<FHWRasterizeVS::FAutoShaderCullDim>(bUseAutoCullingShader);
				PermutationVectorVS.Set<FHWRasterizeVS::FHasPrevDrawData>(bHavePrevDrawData);
				PermutationVectorVS.Set<FHWRasterizeVS::FDebugVisualizeDim>( ShouldExportDebugBuffers() && Technique != ERasterTechnique::DepthOnly );
				PermutationVectorVS.Set<FHWRasterizeVS::FNearClipDim>(bNearClip);
				PermutationVectorVS.Set<FHWRasterizeVS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
				PermutationVectorVS.Set<FHWRasterizeVS::FClusterPerPageDim>( GNaniteClusterPerPage && VirtualShadowMapArray != nullptr );

				FHWRasterizePS::FPermutationDomain PermutationVectorPS;
				PermutationVectorPS.Set<FHWRasterizePS::FRasterTechniqueDim>(int32(Technique));
				PermutationVectorPS.Set<FHWRasterizePS::FMultiViewDim>( bMultiView );
				PermutationVectorPS.Set<FHWRasterizePS::FPrimShaderDim>( bUsePrimitiveShader );
				PermutationVectorPS.Set<FHWRasterizePS::FPrimShaderCullDim>(bUsePrimitiveShaderCulling);
				PermutationVectorPS.Set<FHWRasterizePS::FDebugVisualizeDim>( ShouldExportDebugBuffers() && Technique != ERasterTechnique::DepthOnly );
				PermutationVectorPS.Set<FHWRasterizePS::FNearClipDim>(bNearClip);
				PermutationVectorPS.Set<FHWRasterizePS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
				PermutationVectorPS.Set<FHWRasterizePS::FClusterPerPageDim>( GNaniteClusterPerPage && VirtualShadowMapArray != nullptr );

				auto VertexShader = ShaderMap->GetShader<FHWRasterizeVS>(PermutationVectorVS);
				auto PixelShader  = ShaderMap->GetShader<FHWRasterizePS>(PermutationVectorPS);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets( GraphicsPSOInit );

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

				SetGraphicsPipelineState( RHICmdList, GraphicsPSOInit );
			
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *PassParameters);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				RHICmdList.SetStreamSource( 0, nullptr, 0 );
				RHICmdList.DrawPrimitiveIndirect( PassParameters->IndirectArgs->GetIndirectRHICallBuffer(), 16 );
			}

			if (Scheduling == ERasterScheduling::HardwareAndSoftwareOverlap)
			{
				// Wait for SW rasterizer to complete
				RHICmdList.WaitComputeFence(AsyncRasterEndFence);
			}
			else if (Scheduling != ERasterScheduling::HardwareOnly)
			{
				// SW rasterizer
				FComputeShaderUtils::DispatchIndirect(RHICmdList, ComputeShader, *PassParameters, PassParameters->IndirectArgs, 0);
			}
		}
	);
}

FRasterContext InitRasterContext(
	FRDGBuilder& GraphBuilder,
	FIntPoint TextureSize,
	EOutputBufferMode RasterMode,
	bool bClearTarget,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects )
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE(ELLMTag::Nanite);

	FRasterContext RasterContext;

	RasterContext.TextureSize = TextureSize;

	// Set rasterizer scheduling based on config and platform capabilities.
	if (GNaniteComputeRasterization != 0)
	{
		const bool bUseAsyncCompute = GSupportsEfficientAsyncCompute && (GNaniteAsyncRasterization != 0);
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
		if (IsRHIDeviceNVIDIA())
		{
			// Support is provided through NVAPI.
			RasterContext.RasterTechnique = ERasterTechnique::NVAtomics;
		}
		else if (IsRHIDeviceAMD())
		{
			// TODO: This... should be cleaned up. No way to query the RHI in another capacity. Should be cleaned up
			//       after switching over to DXC.
			static const bool bIsDx12 = FCString::Strcmp(GDynamicRHI->GetName(), TEXT("D3D12")) == 0;

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

	RasterContext.DepthBuffer	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2DDesc(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false ), TEXT("DepthBuffer32") );
	RasterContext.VisBuffer64	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2DDesc(RasterContext.TextureSize, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false ), TEXT("VisBuffer64") );
	RasterContext.DbgBuffer64	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2DDesc(RasterContext.TextureSize, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false), TEXT("DbgBuffer64") );
	RasterContext.DbgBuffer32	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2DDesc(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false), TEXT("DbgBuffer32") );
	RasterContext.LockBuffer	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2DDesc(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV, false), TEXT("LockBuffer") );
	
	const uint32 ClearValue[4] = { 0, 0, 0, 0 };

	if(RasterMode == EOutputBufferMode::DepthOnly)
	{
		if( bClearTarget )
		{
			AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( RasterContext.DepthBuffer ), ClearValue, RectMinMaxBufferSRV, NumRects );
		}
	}
	else
	{
		if( bClearTarget )
		{
			AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( RasterContext.VisBuffer64 ), ClearValue, RectMinMaxBufferSRV, NumRects );
		}
		
		if( ShouldExportDebugBuffers() )
		{
			AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( RasterContext.DbgBuffer64 ), ClearValue, RectMinMaxBufferSRV, NumRects );
			AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( RasterContext.DbgBuffer32 ), ClearValue, RectMinMaxBufferSRV, NumRects );
		}

		if( RasterContext.RasterTechnique == ERasterTechnique::LockBufferFallback )
		{
			AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( RasterContext.LockBuffer ), ClearValue, RectMinMaxBufferSRV, NumRects );
		}
	}

	return RasterContext;
}


void FPackedView::UpdateLODScales()
{
	const float ViewToPixels = 0.5f * ViewToClip.M[1][1] * ViewSizeAndInvSize.Y;

	const float LODScale = ViewToPixels / GNaniteMaxPixelsPerEdge;
	const float LODScaleHW = ViewToPixels / GNaniteMinPixelsPerEdgeHW;

	LODScales = FVector2D(LODScale, LODScaleHW);
}


void CullRasterizeInner(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	uint32 NumPrimaryViews,	// Number of non-mip views
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	const TArray<FInstanceDraw, SceneRenderingAllocator>* OptionalInstanceDraws,
	// VirtualShadowMapArray is the supplier of virtual to physical translation, probably could abstract this a bit better,
	FVirtualShadowMapArray* VirtualShadowMapArray,
	bool bExtractStats
)
{
	LLM_SCOPE(ELLMTag::Nanite);
	RDG_EVENT_SCOPE( GraphBuilder, "Nanite::CullRasterize" );

	check(!Nanite::GStreamingManager.IsAsyncUpdateInProgress());

	// TODO: if we need this emulation feature by going through the view we can probably pass in the shader map as part of the context and get it out of the view at context-creation time
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	// Calling CullRasterize more than once on a CullingContext is illegal unless bSupportsMultiplePasses is enabled.
	check(CullingContext.DrawPassIndex == 0 || CullingContext.bSupportsMultiplePasses);

	//check(Views.Num() == 1 || !CullingContext.PrevHZB);	// HZB not supported with multi-view, yet
	check(Views.Num() > 0 && Views.Num() <= MAX_VIEWS_PER_CULL_RASTERIZE_PASS);

	{
		const uint32 ViewsBufferElements = FMath::RoundUpToPowerOfTwo(Views.Num());
		CullingContext.ViewsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Views"), Views.GetTypeSize(), ViewsBufferElements, Views.GetData(), Views.Num() * Views.GetTypeSize());
	}

	if (OptionalInstanceDraws)
	{
		const uint32 InstanceDrawsBufferElements = FMath::RoundUpToPowerOfTwo(OptionalInstanceDraws->Num());
		CullingContext.InstanceDrawsBuffer = CreateStructuredBuffer
		(
			GraphBuilder,
			TEXT("InstanceDraws"),
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
		CullingContext.NumInstancesPreCull = Scene.GPUScene.InstanceDataAllocator.GetMaxSize();
	}

	if (GNaniteDebugFlags != 0)
	{
		FNaniteStats Stats;
		Stats.NumTris  = 0;
		Stats.NumVerts = 0;
		Stats.NumViews = 0;
		Stats.NumMainInstancesPreCull  = CullingContext.NumInstancesPreCull;
		Stats.NumMainInstancesPostCull = 0;
		Stats.NumPostInstancesPreCull  = 0;
		Stats.NumPostInstancesPostCull = 0;
		Stats.NumLargePageRectClusters = 0;

		CullingContext.StatsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("StatsBuffer"), sizeof(FNaniteStats), 1, &Stats, sizeof(FNaniteStats));
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
		CullingParameters.DisocclusionLodScaleFactor = (GNaniteDisocclusionHack && GLumenFastCameraMode) ? 0.01f : 1.0f;	// TODO: Get rid of this hack
		CullingParameters.HZBTexture	= RegisterExternalTextureWithFallback(GraphBuilder, CullingContext.PrevHZB, GSystemTextures.BlackDummy, TEXT("PrevHZB"));
		CullingParameters.HZBSize		= CullingContext.PrevHZB ? CullingContext.PrevHZB->GetDesc().Extent : FVector2D(0.0f);
		CullingParameters.HZBViewSize	= CullingContext.PrevHZB ? FVector2D(CullingContext.PrevHZBViewRect.Size()) : FVector2D(0.0f);
		CullingParameters.HZBSampler	= TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();
		CullingParameters.SOAStrides	= CullingContext.SOAStrides;
		CullingParameters.MaxClusters	= Nanite::FGlobalResources::GetMaxClusters();
		CullingParameters.RenderFlags	= CullingContext.RenderFlags;
		CullingParameters.DebugFlags	= CullingContext.DebugFlags;
	}

	FVirtualTargetParameters VirtualTargetParameters;
	if (VirtualShadowMapArray)
	{
		VirtualTargetParameters.VirtualShadowMapCommon = VirtualShadowMapArray->CommonParameters;
		VirtualTargetParameters.PageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArray->PageFlags, TEXT("PageFlags")), PF_R32_UINT);
		VirtualTargetParameters.HPageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArray->HPageFlags, TEXT("HPageFlags")), PF_R32_UINT);
		VirtualTargetParameters.PageTable = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArray->PageTable, TEXT("PageTable")));
		VirtualTargetParameters.PageRectBounds = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArray->PageRectBounds, TEXT("PageRectBounds")));
	}
	FGPUSceneParameters GPUSceneParameters;
	GPUSceneParameters.GPUSceneInstanceSceneData = Scene.GPUScene.InstanceDataBuffer.SRV;
	GPUSceneParameters.GPUScenePrimitiveSceneData = Scene.GPUScene.PrimitiveBuffer.SRV;
	GPUSceneParameters.GPUSceneFrameNumber = Scene.GPUScene.SceneFrameNumber;
	
	{
		FInitArgs_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitArgs_CS::FParameters >();

		PassParameters->RenderFlags = CullingParameters.RenderFlags;

		PassParameters->OutMainAndPostPassPersistentStates	= GraphBuilder.CreateUAV( CullingContext.MainAndPostPassPersistentStates );
		PassParameters->OutMainPassCandidateClustersArgs	= GraphBuilder.CreateUAV( CullingContext.MainPass.CandidateClustersArgs );
		PassParameters->InOutMainPassRasterizeArgsSWHW		= GraphBuilder.CreateUAV( CullingContext.MainPass.RasterizeArgsSWHW );

		uint32 ClampedDrawPassIndex = FMath::Min( CullingContext.DrawPassIndex, 2u );

		if( CullingContext.bTwoPassOcclusion )
		{
			PassParameters->OutOccludedInstancesArgs = GraphBuilder.CreateUAV( CullingContext.OccludedInstancesArgs );
			PassParameters->OutPostPassCandidateClustersArgs = GraphBuilder.CreateUAV( CullingContext.PostPass.CandidateClustersArgs );
			PassParameters->InOutPostPassRasterizeArgsSWHW = GraphBuilder.CreateUAV( CullingContext.PostPass.RasterizeArgsSWHW );
		}
		
		check(CullingContext.DrawPassIndex == 0 || CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA); // sanity check
		if (CullingContext.RenderFlags & RENDER_FLAG_HAVE_PREV_DRAW_DATA)
		{
			PassParameters->InOutTotalPrevDrawClusters = GraphBuilder.CreateUAV(CullingContext.TotalPrevDrawClustersBuffer);
		}
		else
		{
			// Use any UAV just to keep render graph happy that something is bound, but the shader doesn't actually touch this.
			PassParameters->InOutTotalPrevDrawClusters = PassParameters->OutMainAndPostPassPersistentStates;
		}

		FInitArgs_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInitArgs_CS::FOcclusionCullingDim>( CullingContext.bTwoPassOcclusion );
		PermutationVector.Set<FInitArgs_CS::FDrawPassIndexDim>( ClampedDrawPassIndex );
		
		auto ComputeShader = ShaderMap->GetShader< FInitArgs_CS >( PermutationVector );

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "InitArgs" ),
			ComputeShader,
			PassParameters,
			FIntVector( 1, 1, 1 )
		);
	}

	// No Occlusion Pass / Occlusion Main Pass
	AddPass_InstanceHierarchyAndClusterCull(
		GraphBuilder,
		Scene,
		CullingParameters,
		Views,
		NumPrimaryViews,
		CullingContext,
		RasterState,
		GPUSceneParameters,
		CullingContext.bTwoPassOcclusion ? CULLING_PASS_OCCLUSION_MAIN : CULLING_PASS_NO_OCCLUSION,
		VirtualShadowMapArray,
		VirtualTargetParameters
	);

	AddPass_Rasterize(
		GraphBuilder,
		Views,
		RasterContext,
		RasterState,
		CullingContext.SOAStrides,
		CullingContext.RenderFlags,
		CullingContext.ViewsBuffer,
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		CullingContext.InstanceDynamicData,
#endif
		CullingContext.VisibleClustersSWHW,
		nullptr,
		CullingContext.MainPass.RasterizeArgsSWHW,
		CullingContext.TotalPrevDrawClustersBuffer,
		GPUSceneParameters,
		true,
		VirtualShadowMapArray,
		VirtualTargetParameters
	);
	
	// Occlusion post pass. Retest instances and clusters that were not visible last frame. If they are visible now, render them.
	if( CullingContext.bTwoPassOcclusion )
	{
		ensureMsgf(Views.Num() == 1, TEXT("Multi-view does not support two pass occlusion culling"));

		// Build a closest HZB with previous frame occluders to test remainder occluders against.
		{
			RDG_EVENT_SCOPE(GraphBuilder, "BuildPreviousOccluderHZB");
			
			FSceneTextureParameters SceneTextures;
			SetupSceneTextureParameters( GraphBuilder, &SceneTextures );

			FRDGTextureRef SceneDepth = SceneTextures.SceneDepthBuffer;
			FRDGTextureRef RasterizedDepth = RasterContext.VisBuffer64;

			if( RasterContext.RasterTechnique == ERasterTechnique::DepthOnly )
			{
				SceneDepth = GraphBuilder.RegisterExternalTexture( GSystemTextures.BlackDummy );
				RasterizedDepth = RasterContext.DepthBuffer;
			}

			FRDGTextureRef OutFurthestHZBTexture;

			FIntRect ViewRect(Views[0].ViewRect.X, Views[0].ViewRect.Y, Views[0].ViewRect.Z, Views[0].ViewRect.W);
			BuildHZB(
				GraphBuilder,
				SceneDepth,
				RasterizedDepth,
				ViewRect,
				/* OutClosestHZBTexture = */ nullptr,
				/* OutFurthestHZBTexture = */ &OutFurthestHZBTexture);

			CullingParameters.HZBTexture = OutFurthestHZBTexture;
			CullingParameters.HZBSize = CullingParameters.HZBTexture->Desc.Extent;
			CullingParameters.HZBViewSize = ViewRect.Size();
		}

		// Post Pass
		AddPass_InstanceHierarchyAndClusterCull(
			GraphBuilder,
			Scene,
			CullingParameters,
			Views,
			NumPrimaryViews,
			CullingContext,
			RasterState,
			GPUSceneParameters,
			CULLING_PASS_OCCLUSION_POST,
			VirtualShadowMapArray,
			VirtualTargetParameters
		);

		// Render post pass
		AddPass_Rasterize(
			GraphBuilder,
			Views,
			RasterContext,
			RasterState,
			CullingContext.SOAStrides,
			CullingContext.RenderFlags,
			CullingContext.ViewsBuffer,
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
			CullingContext.InstanceDynamicData,
#endif
			CullingContext.VisibleClustersSWHW,
			CullingContext.MainPass.RasterizeArgsSWHW,
			CullingContext.PostPass.RasterizeArgsSWHW,
			CullingContext.TotalPrevDrawClustersBuffer,
			GPUSceneParameters,
			false,
			VirtualShadowMapArray,
			VirtualTargetParameters
		);

		GraphBuilder.QueueBufferExtraction(CullingContext.PostPass.Nodes, &Nanite::GGlobalResources.GetPostPassBuffers().NodesBuffer);
	}

	GraphBuilder.QueueBufferExtraction(CullingContext.MainPass.Nodes, &Nanite::GGlobalResources.GetMainPassBuffers().NodesBuffer);

	CullingContext.DrawPassIndex++;
	CullingContext.RenderFlags |= RENDER_FLAG_HAVE_PREV_DRAW_DATA;

	if (bExtractStats)
	{
		const bool bVirtualTextureTarget = VirtualShadowMapArray != nullptr;
		ExtractStats(GraphBuilder, CullingContext, bVirtualTextureTarget);
	}
}

void CullRasterize(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TArray<FPackedView, SceneRenderingAllocator>& Views,
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	const TArray<FInstanceDraw, SceneRenderingAllocator>* OptionalInstanceDraws,
	bool bExtractStats
)
{
	CullRasterizeInner( GraphBuilder, Scene, Views, Views.Num(), CullingContext, RasterContext, RasterState, OptionalInstanceDraws, nullptr, bExtractStats );
}

void CullRasterize(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	FVirtualShadowMapArray &VirtualShadowMapArray,
	const TArray<FPackedView, SceneRenderingAllocator> &Views,
	FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	bool bExtractStats
	)
{
	LLM_SCOPE(ELLMTag::Nanite);

	// strategy: 
	// 1. Use the cull pass to generate copies of every node for every view needed.
	// [2. Fabricate a HZB array?]

	// 1. create derivative views for each of the Mip levels, 
	TArray<FPackedView, SceneRenderingAllocator> MipViews;
	MipViews.AddDefaulted(Views.Num() * FVirtualShadowMap::MaxMipLevels);
	ensure(Views.Num() <= VirtualShadowMapArray.ShadowMaps.Num());

	const int32 NumPrimaryViews = Views.Num();
	int32 MaxMips = 0;
	for (int32 ViewIndex = 0; ViewIndex < NumPrimaryViews; ++ViewIndex)
	{
		const FPackedView &View = Views[ViewIndex];
		ensure(View.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X >= 0 && View.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X < VirtualShadowMapArray.ShadowMaps.Num());
		ensure(View.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y == 0);
		ensure(View.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z > 0 && View.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z <= FVirtualShadowMap::MaxMipLevels);
		const int32 NumMips = View.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z;
		MaxMips = FMath::Max(MaxMips, NumMips);
		for (int32 MipLevel = 0; MipLevel < NumMips; ++MipLevel)
		{
			FPackedView MipView = View;

			// Slightly messy, but extract any scale factor that was applied to the LOD scale for re-application below
			MipView.UpdateLODScales();
			float LODScaleFactor = View.LODScales.X / MipView.LODScales.X;

			MipView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y = MipLevel;
			MipView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z = FVirtualShadowMap::MaxMipLevels;
			// Size of view, for the virtual SMs these are assumed to not be offset.
			FIntPoint ViewSize = FIntPoint::DivideAndRoundUp(FIntPoint(View.ViewSizeAndInvSize.X + 0.5f, View.ViewSizeAndInvSize.Y + 0.5f), 1U <<  MipLevel);
			FIntPoint ViewMin = FIntPoint(MipView.ViewRect.X, MipView.ViewRect.Y) / (1U <<  MipLevel);

			MipView.ViewSizeAndInvSize = FVector4(ViewSize.X, ViewSize.Y, 1.0f / float(ViewSize.X), 1.0f / float(ViewSize.Y));
			MipView.ViewRect = FIntVector4(ViewMin.X, ViewMin.Y, ViewMin.X + ViewSize.X, ViewMin.Y + ViewSize.Y);

			float RcpExtXY = 1.0f / FVirtualShadowMap::VirtualMaxResolutionXY;
			if( GNaniteClusterPerPage )
				RcpExtXY = 1.0f / ( FVirtualShadowMap::PageSize * FVirtualShadowMap::RasterWindowPages );

			// Transform clip from virtual address space to viewport.
			MipView.ClipSpaceScaleOffset = FVector4(
				MipView.ViewSizeAndInvSize.X * RcpExtXY,
				MipView.ViewSizeAndInvSize.Y * RcpExtXY,
				(MipView.ViewSizeAndInvSize.X + 2.0f * MipView.ViewRect.X) * RcpExtXY - 1.0f,
				-(MipView.ViewSizeAndInvSize.Y + 2.0f * MipView.ViewRect.Y) * RcpExtXY + 1.0f);

			MipView.StreamingPriorityCategory = 0;

			MipView.UpdateLODScales();
			MipView.LODScales.X *= LODScaleFactor;

			MipViews[MipLevel * NumPrimaryViews + ViewIndex] = MipView; // Primary (Non-Mip views) first followed by derived mip views.
		}
	}

	// Remove unused mip views
	check(MaxMips > 0);
	MipViews.SetNum(MaxMips * NumPrimaryViews, false);

	// 2. Invoke culling & raster pass with a special shader permutation
	CullRasterizeInner(GraphBuilder, Scene, MipViews, NumPrimaryViews, CullingContext, RasterContext, RasterState, nullptr, &VirtualShadowMapArray, bExtractStats);
	//
}

void ExtractStats(
	FRDGBuilder& GraphBuilder,
	const FCullingContext& CullingContext,
	bool bVirtualTextureTarget
)
{
	LLM_SCOPE(ELLMTag::Nanite);

	if (GNaniteDebugFlags != 0 && GNaniteShowStats != 0 && CullingContext.StatsBuffer != nullptr)
	{
		auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		FRDGBufferRef CalculateStatsArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("CalculateStatsArgs"));

		{
			FCalculateStatsIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCalculateStatsIndirectArgsCS::FParameters>();

			PassParameters->RenderFlags = CullingContext.RenderFlags;

			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
			PassParameters->OutStatsArgs = GraphBuilder.CreateUAV(CalculateStatsArgs);

			PassParameters->MainPassRasterizeArgsSWHW = GraphBuilder.CreateSRV(CullingContext.MainPass.RasterizeArgsSWHW);

			if( CullingContext.bTwoPassOcclusion )
			{
				check(CullingContext.PostPass.RasterizeArgsSWHW);
				PassParameters->PostPassRasterizeArgsSWHW = GraphBuilder.CreateSRV(CullingContext.PostPass.RasterizeArgsSWHW);
			}
			
			FCalculateStatsIndirectArgsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FCalculateStatsIndirectArgsCS::FTwoPassCullingDim>( CullingContext.bTwoPassOcclusion );
			auto ComputeShader = ShaderMap->GetShader<FCalculateStatsIndirectArgsCS>( PermutationVector );

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CalculateStatsArgs"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1)
			);
		}

		{
			FCalculateStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCalculateStatsCS::FParameters>();

			PassParameters->SOAStrides = CullingContext.SOAStrides;
			PassParameters->MaxClusters = Nanite::FGlobalResources::GetMaxClusters();
			PassParameters->RenderFlags = CullingContext.RenderFlags;

			PassParameters->ClusterPageData = GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders = GStreamingManager.GetClusterPageHeadersSRV();
			PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(CullingContext.StatsBuffer);

			PassParameters->MainPassRasterizeArgsSWHW = GraphBuilder.CreateSRV(CullingContext.MainPass.RasterizeArgsSWHW);
			if( CullingContext.bTwoPassOcclusion )
			{
				check(CullingContext.PostPass.RasterizeArgsSWHW != nullptr);
				PassParameters->PostPassRasterizeArgsSWHW = GraphBuilder.CreateSRV( CullingContext.PostPass.RasterizeArgsSWHW );
			}
			PassParameters->StatsArgs = CalculateStatsArgs;

			FCalculateStatsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FCalculateStatsCS::FTwoPassCullingDim>( CullingContext.bTwoPassOcclusion );
			PermutationVector.Set<FCalculateStatsCS::FVirtualTextureTargetDim>(bVirtualTextureTarget);
			auto ComputeShader = ShaderMap->GetShader<FCalculateStatsCS>( PermutationVector );

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CalculateStats"),
				ComputeShader,
				PassParameters,
				CalculateStatsArgs,
				0
			);
		}

		// Extract main pass buffers
		{
			auto& MainPassBuffers = Nanite::GGlobalResources.GetMainPassBuffers();
			GraphBuilder.QueueBufferExtraction(CullingContext.MainPass.RasterizeArgsSWHW, &MainPassBuffers.StatsRasterizeArgsSWHWBuffer);
			GraphBuilder.QueueBufferExtraction(CullingContext.MainPass.CandidateClustersArgs, &MainPassBuffers.StatsCandidateClustersArgsBuffer);
		}

		// Extract post pass buffers
		auto& PostPassBuffers = Nanite::GGlobalResources.GetPostPassBuffers();
		PostPassBuffers.StatsRasterizeArgsSWHWBuffer = nullptr;
		PostPassBuffers.StatsCandidateClustersArgsBuffer = nullptr;
		if( CullingContext.bTwoPassOcclusion )
		{
			check( CullingContext.PostPass.RasterizeArgsSWHW != nullptr );
			check( CullingContext.PostPass.CandidateClustersArgs != nullptr );
			GraphBuilder.QueueBufferExtraction(CullingContext.PostPass.RasterizeArgsSWHW, &PostPassBuffers.StatsRasterizeArgsSWHWBuffer);
			GraphBuilder.QueueBufferExtraction(CullingContext.PostPass.CandidateClustersArgs, &PostPassBuffers.StatsCandidateClustersArgsBuffer);
		}

		// Extract calculated stats (so VisibleClustersSWHW isn't needed later)
		{
			GraphBuilder.QueueBufferExtraction(CullingContext.StatsBuffer, &Nanite::GGlobalResources.GetStatsBufferRef());
		}

		// Save out current render and debug flags.
		Nanite::GGlobalResources.StatsRenderFlags = CullingContext.RenderFlags;
		Nanite::GGlobalResources.StatsDebugFlags = CullingContext.DebugFlags;
	}
}

void PrintStats(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View)
{
	LLM_SCOPE(ELLMTag::Nanite);

	// Print stats
	if (GNaniteDebugFlags != 0 && GNaniteShowStats != 0 && Nanite::GGlobalResources.GetStatsBufferRef())
	{
		auto& MainPassBuffers = Nanite::GGlobalResources.GetMainPassBuffers();
		auto& PostPassBuffers = Nanite::GGlobalResources.GetPostPassBuffers();

		{
			FPrintStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrintStatsCS::FParameters>();

			ShaderPrint::SetParameters(View, PassParameters->ShaderPrintStruct);
			PassParameters->PackedTriClusterSize = sizeof(Nanite::FPackedTriCluster);

			PassParameters->RenderFlags = Nanite::GGlobalResources.StatsRenderFlags;
			PassParameters->DebugFlags = GNaniteDebugFlags == 0 ? 0 : Nanite::GGlobalResources.StatsDebugFlags;

			PassParameters->InStatsBuffer = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetStatsBufferRef()));

			PassParameters->MainPassCandidateClustersArgs = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(MainPassBuffers.StatsCandidateClustersArgsBuffer));
			PassParameters->MainPassRasterizeArgsSWHW = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(MainPassBuffers.StatsRasterizeArgsSWHWBuffer));
			
			bool bTwoPass = PostPassBuffers.StatsCandidateClustersArgsBuffer != nullptr;
			if( bTwoPass )
			{
				PassParameters->PostPassCandidateClustersArgs = GraphBuilder.CreateSRV( GraphBuilder.RegisterExternalBuffer( PostPassBuffers.StatsCandidateClustersArgsBuffer ) );
				PassParameters->PostPassRasterizeArgsSWHW = GraphBuilder.CreateSRV( GraphBuilder.RegisterExternalBuffer( PostPassBuffers.StatsRasterizeArgsSWHWBuffer ) );
			}

			FPrintStatsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FPrintStatsCS::FTwoPassCullingDim>( bTwoPass );
			auto ComputeShader = View.ShaderMap->GetShader<FPrintStatsCS>( PermutationVector );

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Print Stats"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1)
				);
		}
	}
}

void ExtractResults(
	FRDGBuilder& GraphBuilder,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	FRasterResults& RasterResults )
{
	LLM_SCOPE(ELLMTag::Nanite);

	RasterResults.SOAStrides 	= CullingContext.SOAStrides;
	RasterResults.MaxClusters	= Nanite::FGlobalResources::GetMaxClusters();
	RasterResults.MaxNodes		= Nanite::FGlobalResources::GetMaxNodes();
	RasterResults.RenderFlags	= CullingContext.RenderFlags;
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
	GraphBuilder.QueueBufferExtraction(CullingContext.InstanceDynamicData,	&RasterResults.InstanceDynamicData);
#endif
	GraphBuilder.QueueBufferExtraction(CullingContext.VisibleClustersSWHW,	&RasterResults.VisibleClustersSWHW);
	GraphBuilder.QueueTextureExtraction(RasterContext.VisBuffer64,			&RasterResults.VisBuffer64);
	
	if (ShouldExportDebugBuffers())
	{
		GraphBuilder.QueueTextureExtraction(RasterContext.DbgBuffer64, &RasterResults.DbgBuffer64);
		GraphBuilder.QueueTextureExtraction(RasterContext.DbgBuffer32, &RasterResults.DbgBuffer32);
	}

	if (CullingContext.RenderFlags & RENDER_FLAG_OUTPUT_STREAMING_REQUESTS)
	{
		GraphBuilder.QueueBufferExtraction(CullingContext.StreamingRequests, &GStreamingManager.GetStreamingRequestsBuffer());
	}
}

void DrawHitProxies(
	FRHICommandListImmediate& RHICmdList,
	const FScene& Scene,
	const FViewInfo& View, 
	const FRasterResults& RasterResults,
	const TRefCountPtr<IPooledRenderTarget>& HitProxyRT,
	const TRefCountPtr<IPooledRenderTarget>& HitProxyDepthRT
	)
{
#if WITH_EDITOR
	LLM_SCOPE(ELLMTag::Nanite);

	SCOPED_DRAW_EVENT(RHICmdList, NaniteHitProxyPass);
	SCOPED_GPU_STAT(RHICmdList, NaniteEditor);

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef HitProxyId  = RegisterExternalTextureWithFallback(GraphBuilder, HitProxyRT,					GSystemTextures.BlackDummy,	TEXT("HitProxyId"));
	FRDGTextureRef SceneDepth  = RegisterExternalTextureWithFallback(GraphBuilder, HitProxyDepthRT,				GSystemTextures.BlackDummy,	TEXT("SceneDepth"));
	FRDGTextureRef VisBuffer64 = RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.VisBuffer64,	GSystemTextures.BlackDummy,	TEXT("VisBuffer64"));

	FRDGBufferRef VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(RasterResults.VisibleClustersSWHW, TEXT("VisibleClustersSWHW"));

	{
		auto* PassParameters = GraphBuilder.AllocParameters<FEmitHitProxyIdPS::FParameters>();

		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->SOAStrides = RasterResults.SOAStrides;
		PassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->MaterialHitProxyTable = Scene.MaterialTables[ENaniteMeshPass::BasePass].GetHitProxyTableSRV();

		PassParameters->RenderTargets[0]			= FRenderTargetBinding(HitProxyId, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

		auto PixelShader = View.ShaderMap->GetShader<FEmitHitProxyIdPS>();

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			GetGlobalShaderMap(View.FeatureLevel),
			RDG_EVENT_NAME("Emit HitProxy Id"),
			PixelShader,
			PassParameters,
			View.ViewRect,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI()
			);
	}

	GraphBuilder.Execute();
#endif
}

void EmitShadowMap(
	FRDGBuilder& GraphBuilder,
	const FRasterContext& RasterContext,
	const FRDGTextureRef DepthBuffer,
	const FIntRect& SourceRect,
	const FIntPoint DestOrigin,
	const FMatrix& ProjectionMatrix,
	float DepthBias,
	bool bOrtho
	)
{
	LLM_SCOPE(ELLMTag::Nanite);

	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	auto* PassParameters = GraphBuilder.AllocParameters< FEmitShadowMapPS::FParameters >();

	PassParameters->SourceOffset = SourceRect.Min - DestOrigin;
	PassParameters->ViewToClip22 = ProjectionMatrix.M[2][2];
	PassParameters->DepthBias = DepthBias;
	
	PassParameters->DepthBuffer = RasterContext.DepthBuffer;
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding( DepthBuffer, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop );

	FEmitShadowMapPS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FEmitShadowMapPS::FDepthInputTypeDim >( 0 );
	PermutationVector.Set< FEmitShadowMapPS::FDepthOutputTypeDim >( bOrtho ? 1 : 2 );

	auto PixelShader = ShaderMap->GetShader< FEmitShadowMapPS >( PermutationVector );

	FIntRect DestRect;
	DestRect.Min = DestOrigin;
	DestRect.Max = DestRect.Min + SourceRect.Max - SourceRect.Min;
	
	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("Emit Shadow Map"),
		PixelShader,
		PassParameters,
		DestRect,
		nullptr,
		nullptr,
		TStaticDepthStencilState<true, CF_LessEqual>::GetRHI()
		);
}

void EmitFallbackShadowMapFromVSM(
	FRDGBuilder& GraphBuilder,
	FVirtualShadowMapArray &VirtualShadowMapArray,
	uint32 ShadowMapID,
	const FRDGTextureRef DepthBuffer,
	const FIntRect& DestRect,
	const FMatrix& ProjectionMatrix,
	float DepthBias,
	bool bOrtho
)
{
	LLM_SCOPE( ELLMTag::Nanite );

	check( DestRect.Width() == FVirtualShadowMap::PageSize );
	check( DestRect.Height() == FVirtualShadowMap::PageSize );

	auto ShaderMap = GetGlobalShaderMap( GMaxRHIFeatureLevel );

	auto* PassParameters = GraphBuilder.AllocParameters< FEmitShadowMapPS::FParameters >();

	PassParameters->CommonVSMParameters = VirtualShadowMapArray.CommonParameters;
	PassParameters->ViewToClip22 = ProjectionMatrix.M[2][2];
	PassParameters->DepthBias = DepthBias;
	PassParameters->ShadowMapID = ShadowMapID;
	PassParameters->SourceOffset = FIntPoint( -DestRect.Min.X, -DestRect.Min.Y );

	PassParameters->PageTable = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArray.PageTable));
	PassParameters->DepthBuffer = GraphBuilder.RegisterExternalTexture(VirtualShadowMapArray.PhysicalPagePool);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding( DepthBuffer, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop );

	FEmitShadowMapPS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FEmitShadowMapPS::FDepthInputTypeDim >( 1 );
	PermutationVector.Set< FEmitShadowMapPS::FDepthOutputTypeDim >( bOrtho ? 1 : 2 );

	auto PixelShader = ShaderMap->GetShader< FEmitShadowMapPS >( PermutationVector );

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME( "Emit Fallback Shadow Map From VSM" ),
		PixelShader,
		PassParameters,
		DestRect,
		nullptr,
		nullptr,
		TStaticDepthStencilState<true, CF_LessEqual>::GetRHI()
	);

	GraphBuilder.QueueTextureExtraction(PassParameters->DepthBuffer, &VirtualShadowMapArray.PhysicalPagePool);
}

void EmitCubemapShadow(
	FRDGBuilder& GraphBuilder,
	const FRasterContext& RasterContext,
	const FRDGTextureRef CubemapDepthBuffer,
	const FIntRect& ViewRect,
	uint32 CubemapFaceIndex,
	bool bUseGeometryShader
	)
{
	LLM_SCOPE(ELLMTag::Nanite);

	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	FEmitCubemapShadowVS::FPermutationDomain VertexPermutationVector;
	VertexPermutationVector.Set<FEmitCubemapShadowVS::FUseGeometryShader>(bUseGeometryShader);
	TShaderMapRef<FEmitCubemapShadowVS> VertexShader(ShaderMap, VertexPermutationVector);
	TShaderRef<FEmitCubemapShadowGS> GeometryShader;
	TShaderMapRef<FEmitCubemapShadowPS> PixelShader(ShaderMap);

	// VS output of RT array index on D3D11 requires a caps bit. Use GS fallback if set.
	if (bUseGeometryShader)
	{
		GeometryShader = TShaderMapRef<FEmitCubemapShadowGS>(ShaderMap);
	}

	FEmitCubemapShadowParameters* PassParameters = GraphBuilder.AllocParameters<FEmitCubemapShadowParameters>();
	PassParameters->CubemapFaceIndex = CubemapFaceIndex;	
	PassParameters->DepthBuffer = RasterContext.DepthBuffer;
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(CubemapDepthBuffer, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Emit Cubemap Shadow"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, VertexShader, GeometryShader, PixelShader, ViewRect, CubemapFaceIndex](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
						
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			// NOTE: Shadow cubemaps are reverse Z
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNear>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			if (GeometryShader.GetGeometryShader())
			{
				GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
			}

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *PassParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
			if (GeometryShader.GetGeometryShader())
			{
				SetShaderParameters(RHICmdList, GeometryShader, GeometryShader.GetGeometryShader(), *PassParameters);
			}

			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitive(0, 1, 1);
		}
	);
}

void DrawPrePass(
	FRHICommandListImmediate& RHICmdList,
	const FScene& Scene,
	const FViewInfo& View,
	FRasterResults& RasterResults
	)
{
	LLM_SCOPE(ELLMTag::Nanite);

	if (UseComputeDepthExport())
	{
		// TODO: Perform depth export here and if done here dont do it in base pass
	}
	else
	{
		SCOPED_DRAW_EVENT(RHICmdList, NanitePrePass);

		FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(RHICmdList);

		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureRef VisBuffer64 = RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.VisBuffer64, GSystemTextures.BlackDummy, TEXT("VisBuffer64"));
		FRDGTextureRef SceneDepth = GraphBuilder.RegisterExternalTexture(SceneTargets.SceneDepthZ, TEXT("SceneDepth"));

		auto* PassParameters = GraphBuilder.AllocParameters<FEmitDepthPS::FParameters>();

		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

		auto PixelShader = View.ShaderMap->GetShader<FEmitDepthPS>();

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			GetGlobalShaderMap(View.FeatureLevel),
			RDG_EVENT_NAME("Emit Depth"),
			PixelShader,
			PassParameters,
			View.ViewRect,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI()
			);

		GraphBuilder.Execute();
	}
}

struct FNaniteMaterialPassCommand
{
	FNaniteMaterialPassCommand(const FMeshDrawCommand& InMeshDrawCommand)
		: MeshDrawCommand(InMeshDrawCommand)
		, MaterialDepth(0.0f)
		, SortKey(MeshDrawCommand.CachedPipelineId.GetId())
	{
	}

	bool operator < (const FNaniteMaterialPassCommand& Other) const
	{
		return SortKey < Other.SortKey;
	}

	FMeshDrawCommand MeshDrawCommand;
	float MaterialDepth = 0.0f;
	uint64 SortKey = 0;
};

static void BuildNaniteMaterialPassCommands(
	FRHICommandListImmediate& RHICmdList,
	const FStateBucketMap& NaniteDrawCommands,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& OutNaniteMaterialPassCommands)
{
	OutNaniteMaterialPassCommands.Reset(NaniteDrawCommands.Num());

	FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;

	// Pull into local here so another thread can't change the sort values mid-iteration.
	const int32 MaterialSortMode = GNaniteMaterialSortMode;

	for (auto& Command : NaniteDrawCommands)
	{
		FNaniteMaterialPassCommand PassCommand(Command.Key);

		Experimental::FHashElementId SetId = NaniteDrawCommands.FindId(Command.Key);

		int32 DrawIdx = SetId.GetIndex();
		PassCommand.MaterialDepth = FNaniteCommandInfo::GetDepthId(DrawIdx);

		if (MaterialSortMode == 2 && GRHISupportsPipelineStateSortKey)
		{
			const FMeshDrawCommand& MeshDrawCommand = Command.Key;
			const FGraphicsMinimalPipelineStateInitializer& MeshPipelineState = MeshDrawCommand.CachedPipelineId.GetPipelineState(GraphicsMinimalPipelineStateSet);
			FGraphicsPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateGraphicsPipelineState(RHICmdList, MeshPipelineState.AsGraphicsPipelineStateInitializer(), EApplyRendertargetOption::DoNothing);
			if (PipelineState)
			{
				const uint64 StateSortKey = PipelineStateCache::RetrieveGraphicsPipelineStateSortKey(PipelineState);
				if (StateSortKey != 0) // 0 on the first occurrence (prior to caching), so these commands will fall back on shader id for sorting.
				{
					PassCommand.SortKey = StateSortKey;
				}
			}
		}

		OutNaniteMaterialPassCommands.Emplace(PassCommand);
	}

	if (MaterialSortMode != 0)
	{
		OutNaniteMaterialPassCommands.Sort();
	}
}

void DrawBasePass(
	FRHICommandListImmediate& RHICmdList,
	const FScene& Scene,
	const FViewInfo& View,
	const FRasterResults& RasterResults
	)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE(ELLMTag::Nanite);
	SCOPED_DRAW_EVENT(RHICmdList, NaniteBasePass);
	SCOPED_GPU_STAT(RHICmdList, NaniteMaterials);

	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(RHICmdList);
	const ENaniteMeshPass::Type MeshPass = ENaniteMeshPass::BasePass;

	const int32 ViewWidth		= View.ViewRect.Max.X - View.ViewRect.Min.X;
	const int32 ViewHeight		= View.ViewRect.Max.Y - View.ViewRect.Min.Y;
	const FIntPoint ViewSize	= FIntPoint(ViewWidth, ViewHeight);

	TRefCountPtr<IPooledRenderTarget> DebugVisualizationOutput;
	TRefCountPtr<IPooledRenderTarget> MaterialDepthOutput; // Only used for visualizing material depth export

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef SceneDepth = GraphBuilder.RegisterExternalTexture(SceneTargets.SceneDepthZ, TEXT("SceneDepth"));

	if (UseComputeDepthExport())
	{
		// TODO: Force decompress depth buffer. This is a workaround for current lack of decompression support in the RHI when binding a compressed resource as UAV.
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, SceneTargets.SceneDepthZ->GetRenderTargetItem().TargetableTexture);
	}
	else if (GNaniteMaterialCulling == 1 || GNaniteMaterialCulling == 2)
	{
		// Mode 1 and 2 (32bit mask) is currently unsupported when compute depth export is disabled.
		// Culling was intended, so fall back to range load method.
		// TODO: Test and optimize further before allowing the new fallback
		//GNaniteMaterialCulling = 3;
		GNaniteMaterialCulling = 0;
	}

	int32 VelocityRTIndex = -1;
	int32 TangentRTIndex = -1;
	FRenderTargetBinding RenderTargets[MaxSimultaneousRenderTargets] = {};
	int32 NumMRTs = SceneTargets.GetGBufferRenderTargets(GraphBuilder, ERenderTargetLoadAction::ELoad, RenderTargets, VelocityRTIndex, TangentRTIndex);

	FRDGTextureRef VisBuffer64		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.VisBuffer64,		GSystemTextures.BlackDummy, TEXT("VisBuffer64"));
	FRDGTextureRef DbgBuffer64		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.DbgBuffer64,		GSystemTextures.BlackDummy, TEXT("DbgBuffer64"));
	FRDGTextureRef DbgBuffer32		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.DbgBuffer32,		GSystemTextures.BlackDummy, TEXT("DbgBuffer32"));
	FRDGTextureRef MaterialDepth	= SceneDepth;
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
	FRDGBufferRef InstanceDynamicData	= GraphBuilder.RegisterExternalBuffer(RasterResults.InstanceDynamicData,	TEXT("InstanceDynamicData"));
#endif
	FRDGBufferRef VisibleClustersSWHW	= GraphBuilder.RegisterExternalBuffer(RasterResults.VisibleClustersSWHW,	TEXT("VisibleClustersSWHW"));

	const bool b32BitMaskCulling = (GNaniteMaterialCulling == 1 || GNaniteMaterialCulling == 2);

	FRDGBufferDesc   VisibleMaterialsDesc = FRDGBufferDesc::CreateStructuredDesc(4, b32BitMaskCulling ? FNaniteCommandInfo::MAX_STATE_BUCKET_ID+1 : 1);
	FRDGBufferRef    VisibleMaterials     = GraphBuilder.CreateBuffer(VisibleMaterialsDesc, TEXT("NaniteVisibleMaterials"));
	FRDGBufferUAVRef VisibleMaterialsUAV  = GraphBuilder.CreateUAV(VisibleMaterials);

	// Visible material buffer is currently only filled by compute depth export pass.
	// If that's not used, then initialize all materials to visible.
	AddClearUAVPass(GraphBuilder, VisibleMaterialsUAV, 0);

	FRDGTextureDesc MaterialRangeDesc = FRDGTextureDesc::Create2DDesc(FMath::DivideAndRoundUp(ViewSize, { 64, 64 }), PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false);
	FRDGTextureRef  MaterialRange = GraphBuilder.CreateTexture(MaterialRangeDesc, TEXT("NaniteMaterialRange"));
	FRDGTextureUAVRef  MaterialRangeUAV = GraphBuilder.CreateUAV(MaterialRange);
	FRDGTextureSRVDesc MaterialRangeSRVDesc = FRDGTextureSRVDesc::Create(MaterialRange);
	FRDGTextureSRVRef  MaterialRangeSRV = GraphBuilder.CreateSRV(MaterialRangeSRVDesc);

	AddClearUAVPass(GraphBuilder, MaterialRangeUAV, { 0u, 1u, 0u, 0u });

	if (UseComputeDepthExport())
	{
		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

#if 1 //PLATFORM_PS4
		// TODO: For some strange reason, using FClearValueBinding::None will cause the PS4 GPU to crash
		//       due to unmapped memory. The creation of the Material HTILE UAV seems to succeed, yet
		//       the binding is failing somewhere along the way. Needs further investigation.
		const FClearValueBinding MaterialDepthClear = SceneTargets.GetDefaultDepthClear(); // FClearValueBinding::None; // Cleared explicitly in compute pass
#else
		const FClearValueBinding MaterialDepthClear = FClearValueBinding::None; // Cleared explicitly in compute pass
#endif
		FRDGTextureDesc MaterialDepthDesc = FRDGTextureDesc::Create2DDesc(
			SceneTargets.GetBufferSizeXY(),
			PF_DepthStencil,
			MaterialDepthClear,
			TexCreate_None,
			TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead | TexCreate_UAV,
			false);

		MaterialDepth = GraphBuilder.CreateTexture(MaterialDepthDesc, TEXT("MaterialDepth"));

		// Emit scene depth and material depth
		{
			FRDGTextureUAVRef SceneDepthUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::CompressedSurface));
			FRDGTextureUAVRef SceneStencilUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::Stencil));
			FRDGTextureUAVRef SceneHTileUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::HTile));
			FRDGTextureUAVRef MaterialDepthUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::CompressedSurface));
			FRDGTextureUAVRef MaterialHTileUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::HTile));

			FDepthExportCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDepthExportCS::FParameters>();

			const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max, 8);	// Only run DepthExport shader on viewport. We have already asserted that ViewRect.Min=0.

			const uint32 PlatformConfig = 0; // TODO: Platform config from depth target, queried from RHI

			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->SOAStrides = RasterResults.SOAStrides;
			PassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();

			PassParameters->DepthExportConfig = FIntVector4(PlatformConfig, SceneTargets.GetBufferSizeXY().X, GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0);
			PassParameters->ViewRect = FIntVector4(View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Max.X, View.ViewRect.Max.Y);

			PassParameters->VisBuffer64 = VisBuffer64;

			PassParameters->SceneHTile = SceneHTileUAV;
			PassParameters->SceneDepth = SceneDepthUAV;
			PassParameters->SceneStencil = SceneStencilUAV;

			PassParameters->MaterialHTile = MaterialHTileUAV;
			PassParameters->MaterialDepth = MaterialDepthUAV;

			PassParameters->MaterialDepthTable = Scene.MaterialTables[MeshPass].GetDepthTableSRV();
			PassParameters->VisibleMaterials = VisibleMaterialsUAV;

			auto ComputeShader = View.ShaderMap->GetShader<FDepthExportCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("DepthExport"),
				ComputeShader,
				PassParameters,
				DispatchDim
			);
		}
	}
	else
	{
		// Classify materials for 64x64 tiles
		if (GNaniteMaterialCulling == 3 || GNaniteMaterialCulling == 4)
		{
			FReduceMaterialRangeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReduceMaterialRangeCS::FParameters>();

			const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max, 64);

			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->SOAStrides = RasterResults.SOAStrides;
			PassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();
			PassParameters->FetchClamp = View.ViewRect.Max - 1;
			PassParameters->CullingMode = GNaniteMaterialCulling;

			PassParameters->VisBuffer64 = VisBuffer64;

			PassParameters->MaterialDepthTable = Scene.MaterialTables[MeshPass].GetDepthTableSRV();
			PassParameters->MaterialRange = MaterialRangeUAV;

			auto ComputeShader = View.ShaderMap->GetShader<FReduceMaterialRangeCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ReduceMaterialRange"),
				ComputeShader,
				PassParameters,
				DispatchDim
			);
		}

		// Mark stencil for all pixels that pass depth test
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FNaniteMarkStencilPS::FParameters>();

			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->VisBuffer64 = VisBuffer64;

			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
				SceneDepth,
				ERenderTargetLoadAction::ELoad,
				FExclusiveDepthStencil::DepthRead_StencilWrite
			);

			auto PixelShader = View.ShaderMap->GetShader<FNaniteMarkStencilPS>();

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GetGlobalShaderMap(View.FeatureLevel),
				RDG_EVENT_NAME("Mark Stencil"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
				STENCIL_SANDBOX_MASK
				);
		}

		// Emit material IDs as depth values
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitMaterialIdPS::FParameters>();

			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->DummyZero = 0u;

			PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->SOAStrides = RasterResults.SOAStrides;
			PassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();

			PassParameters->VisBuffer64 = VisBuffer64;

			PassParameters->MaterialDepthTable = Scene.MaterialTables[MeshPass].GetDepthTableSRV();

			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
				SceneDepth,
				ERenderTargetLoadAction::ELoad,
				ERenderTargetLoadAction::ELoad,
				FExclusiveDepthStencil::DepthWrite_StencilRead
			);

			auto PixelShader = View.ShaderMap->GetShader<FNaniteEmitMaterialIdPS>();

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GetGlobalShaderMap(View.FeatureLevel),
				RDG_EVENT_NAME("Emit Material Id"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				TStaticDepthStencilState<true, CF_Always, true, CF_Equal>::GetRHI(),
				STENCIL_SANDBOX_MASK
				);
		}
	}

	// Emit GBuffer Values
	{
		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->SOAStrides	= RasterResults.SOAStrides;
		PassParameters->MaxClusters	= RasterResults.MaxClusters;
		PassParameters->MaxNodes	= RasterResults.MaxNodes;
		PassParameters->RenderFlags	= RasterResults.RenderFlags;
			
		PassParameters->ClusterPageData		= Nanite::GStreamingManager.GetClusterPageDataSRV(); 
		PassParameters->ClusterPageHeaders	= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		PassParameters->InstanceDynamicData = GraphBuilder.CreateSRV(InstanceDynamicData);
#endif
		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);

		PassParameters->MaterialRange = MaterialRange;
		PassParameters->VisibleMaterials = GraphBuilder.CreateSRV(VisibleMaterials, PF_R32_UINT);

		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->DbgBuffer64 = DbgBuffer64;
		PassParameters->DbgBuffer32 = DbgBuffer32;

		for (int32 MRTIdx = 0; MRTIdx < NumMRTs; ++MRTIdx)
		{
			PassParameters->RenderTargets[MRTIdx] = RenderTargets[MRTIdx];
		}

		PassParameters->View = View.ViewUniformBuffer; // To get VTFeedbackBuffer

		switch (GNaniteMaterialCulling)
		{
		// Rendering 32 tiles in a 8x4 grid
		// 32bits, 1 bit per tile
		case 1:
		case 2:
			PassParameters->GridSize.X = 8;
			PassParameters->GridSize.Y = 4;
			break;

		// Rendering grid of 64x64 pixel tiles
		case 3:
		case 4:
			PassParameters->GridSize = FMath::DivideAndRoundUp(View.ViewRect.Max, { 64,64 });
			break;

		// Rendering a full screen quad
		default:
			PassParameters->GridSize.X = 1;
			PassParameters->GridSize.Y = 1;
			break;
		}

		const FExclusiveDepthStencil MaterialDepthStencil = UseComputeDepthExport()
			? FExclusiveDepthStencil::DepthWrite_StencilNop
			: FExclusiveDepthStencil::DepthWrite_StencilRead;

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			MaterialDepth,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			MaterialDepthStencil
		);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Emit GBuffer"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &Scene, MeshPass, ViewRect = View.ViewRect](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			RHICmdList.BeginUAVOverlap(); // Due to VTFeedbackBuffer

			FNaniteUniformParameters UniformParams;
			UniformParams.SOAStrides = PassParameters->SOAStrides;
			UniformParams.MaxClusters = PassParameters->MaxClusters;
			UniformParams.MaxNodes = PassParameters->MaxNodes;
			UniformParams.RenderFlags = PassParameters->RenderFlags;

			UniformParams.MaterialConfig.X = GNaniteMaterialCulling;
			UniformParams.MaterialConfig.Y = PassParameters->GridSize.X;
			UniformParams.MaterialConfig.Z = PassParameters->GridSize.Y;
			UniformParams.MaterialConfig.W = 0;

			UniformParams.RectScaleOffset = FVector4(1.0f, 1.0f, 0.0f, 0.0f); // Render a rect that covers the entire screen

			if (GNaniteMaterialCulling == 3 || GNaniteMaterialCulling == 4)
			{
				FIntPoint ScaledSize = PassParameters->GridSize * 64;
				UniformParams.RectScaleOffset.X = float(ScaledSize.X) / float(ViewRect.Max.X);
				UniformParams.RectScaleOffset.Y = float(ScaledSize.Y) / float(ViewRect.Max.Y);
			}

			UniformParams.ClusterPageData = PassParameters->ClusterPageData;
			UniformParams.ClusterPageHeaders = PassParameters->ClusterPageHeaders;
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
			UniformParams.InstanceDynamicData = PassParameters->InstanceDynamicData->GetRHI();
#endif
			UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

			UniformParams.MaterialRange = PassParameters->MaterialRange->GetRHI();
			UniformParams.VisibleMaterials = PassParameters->VisibleMaterials->GetRHI();

			UniformParams.VisBuffer64 = PassParameters->VisBuffer64->GetRHI();
			UniformParams.DbgBuffer64 = PassParameters->DbgBuffer64->GetRHI();
			UniformParams.DbgBuffer32 = PassParameters->DbgBuffer32->GetRHI();

			FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;

			TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator> NaniteMaterialPassCommands;
			BuildNaniteMaterialPassCommands(RHICmdList, Scene.NaniteDrawCommands[MeshPass], NaniteMaterialPassCommands);

			FMeshDrawCommandStateCache StateCache;

			const uint32 TileCount = UniformParams.MaterialConfig.Y * UniformParams.MaterialConfig.Z; // (W * H)
			for (auto CommandsIt = NaniteMaterialPassCommands.CreateConstIterator(); CommandsIt; ++CommandsIt)
			{
				const FNaniteMaterialPassCommand& MaterialPassCommand = *CommandsIt;

				UniformParams.MaterialDepth = MaterialPassCommand.MaterialDepth;
				const_cast<FScene&>(Scene).UniformBuffers.NaniteUniformBuffer.UpdateUniformBufferImmediate(UniformParams);
				StateCache.InvalidateUniformBuffer(Scene.UniformBuffers.NaniteUniformBuffer);

				const FMeshDrawCommand& MeshDrawCommand = MaterialPassCommand.MeshDrawCommand;
				FMeshDrawCommand::SubmitDraw(MeshDrawCommand, GraphicsMinimalPipelineStateSet, nullptr, 0, TileCount, RHICmdList, StateCache);
			}

			RHICmdList.EndUAVOverlap();
		});
	}

	// Emit depth values
	if (!UseComputeDepthExport())
	{
		// While we are emitting depth also decrement stencil (setting it to 0) to disable all Nanite meshes receiving decals.
		// Then do another pass that sets stencil value to all the Nanite meshes (depth tested) that want to receive decals.
		{
			FEmitDepthPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEmitDepthPS::FParameters>();

			PassParameters->VisBuffer64 = VisBuffer64;
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			auto PixelShader = View.ShaderMap->GetShader<FEmitDepthPS>();

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GetGlobalShaderMap(View.FeatureLevel),
				RDG_EVENT_NAME("Emit Depth"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				TStaticDepthStencilState<true, CF_Always, true, CF_Equal, SO_Keep, SO_Keep, SO_Decrement>::GetRHI(),
				STENCIL_SANDBOX_MASK
				);
		}

		{
			auto* PassParameters = GraphBuilder.AllocParameters<FEmitStencilPS::FParameters>();

			PassParameters->View = View.ViewUniformBuffer;

			PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->SOAStrides = RasterResults.SOAStrides;
			PassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();

			PassParameters->VisBuffer64 = VisBuffer64;

			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			auto PixelShader = View.ShaderMap->GetShader<FEmitStencilPS>();

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				GetGlobalShaderMap(View.FeatureLevel),
				RDG_EVENT_NAME("Emit Stencil"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
				GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1)
				);
		}
	}

	// Visualize Debug Views
	if (ShouldExportDebugBuffers())
	{
		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		// TODO: Hook up to RDG pass
		SCOPED_GPU_STAT(RHICmdList, NaniteDebug);

		FRDGTextureDesc DebugOutputDesc = FRDGTextureDesc::Create2DDesc(
			View.ViewRect.Max,
			PF_A32B32G32R32F,
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_UAV,
			false);
		DebugOutputDesc.DebugName = TEXT("NaniteDebug");

		FRDGTextureRef DebugOutput = GraphBuilder.CreateTexture(
			DebugOutputDesc,
			TEXT("NaniteDebug"));

		FDebugVisualizeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDebugVisualizeCS::FParameters>();

		PassParameters->View					= View.ViewUniformBuffer;
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->VisualizeConfig			= GetVisualizeConfig();
		PassParameters->SOAStrides				= RasterResults.SOAStrides;
		PassParameters->MaxClusters				= RasterResults.MaxClusters;
		PassParameters->RenderFlags				= RasterResults.RenderFlags;
#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		PassParameters->InstanceDynamicData		= GraphBuilder.CreateSRV(InstanceDynamicData);
#endif
		PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->VisBuffer64				= VisBuffer64;
		PassParameters->DbgBuffer64				= DbgBuffer64;
		PassParameters->DbgBuffer32				= DbgBuffer32;
		PassParameters->MaterialDepthTable		= Scene.MaterialTables[MeshPass].GetDepthTableSRV();
	#if WITH_EDITOR
		PassParameters->MaterialHitProxyTable	= Scene.MaterialTables[MeshPass].GetHitProxyTableSRV();
	#else
		// TODO: Permutation with hit proxy support to keep this clean?
		// For now, bind a valid SRV
		PassParameters->MaterialHitProxyTable	= Scene.MaterialTables[MeshPass].GetDepthTableSRV();
	#endif
		PassParameters->DebugOutput				= GraphBuilder.CreateUAV(DebugOutput);

		auto ComputeShader = View.ShaderMap->GetShader<FDebugVisualizeCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DebugVisualize"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ViewSize, 8)
		);

		GraphBuilder.QueueTextureExtraction(DebugOutput, &DebugVisualizationOutput);
	}

	// Extract the textures to ensure RDG transitions them to readable as they exit the graph.
	if (UseComputeDepthExport() && IsVisualizingHTile())
	{
		if (GNaniteDebugVisualize == VISUALIZE_MAT_HTILE_MINZ ||
			GNaniteDebugVisualize == VISUALIZE_MAT_HTILE_MAXZ ||
			GNaniteDebugVisualize == VISUALIZE_MAT_HTILE_DELTAZ ||
			GNaniteDebugVisualize == VISUALIZE_MAT_HTILE_ZMASK)
		{
			GraphBuilder.QueueTextureExtraction(MaterialDepth, &MaterialDepthOutput);
		}
	}

	GraphBuilder.Execute();

	// TODO hack to enable triangle view in test mode
	if (DebugVisualizationOutput)
	{
		GVisualizeTexture.SetCheckPoint(RHICmdList, DebugVisualizationOutput);
	}

	// Scene depth buffer will be rendered to next, so we need to explicitly put it into depth write state.
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToGfx, SceneTargets.SceneDepthZ->GetRenderTargetItem().UAV);

	if (GRHISupportsResummarizeHTile && GNaniteResummarizeHTile != 0 && !UseComputeDepthExport())
	{
		// Resummarize HTile meta data if the RHI supports it and the compute depth export path isn't active.
		RHICmdList.ResummarizeHTile(SceneTargets.GetSceneDepthSurface());
	}

	// Start a new graph builder (needed after explicitly inlining the resummarize depth command above)
	if (IsVisualizingHTile())
	{
		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		FShaderResourceViewRHIRef HTileBufferRef;
		if (MaterialDepthOutput)
		{
			FSceneRenderTargetItem& MaterialDepthRef = MaterialDepthOutput->GetRenderTargetItem();
			if (!MaterialDepthRef.HTileSRV)
			{
				MaterialDepthRef.HTileSRV = RHICreateShaderResourceViewHTile((FTexture2DRHIRef&)MaterialDepthRef.TargetableTexture);
			}
			HTileBufferRef = MaterialDepthRef.HTileSRV;
		}
		else
		{
			FRHITexture2D* DepthSurface = SceneTargets.GetSceneDepthSurface();
			HTileBufferRef = RHICreateShaderResourceViewHTile(DepthSurface);
		}

		if (HTileBufferRef != nullptr)
		{
			//TODO  - link errors.
			//SCOPED_GPU_STAT(RHICmdList, NaniteDebug);

			// TODO: Make this inline inside GraphBuilder instead of a new RDG instance.
			FRDGBuilder GraphBuilder2(RHICmdList);

			FRDGTextureRef DebugOutput = GraphBuilder2.CreateTexture(
				FRDGTextureDesc::Create2DDesc(
					SceneTargets.GetBufferSizeXY(),
					PF_A32B32G32R32F,
					FClearValueBinding::None,
					TexCreate_None,
					TexCreate_ShaderResource | TexCreate_UAV,
					false),
				TEXT("NaniteDebug"));

			FHTileVisualizeCS::FParameters* PassParameters = GraphBuilder2.AllocParameters<FHTileVisualizeCS::FParameters>();

			const uint32 PlatformConfig = 0; // TODO: Platform config from depth target, queried from RHI
			const uint32 PixelsWide = uint32(ViewSize.X);

			PassParameters->HTileBuffer  = HTileBufferRef;
			PassParameters->HTileDisplay = GraphBuilder2.CreateUAV(DebugOutput);
			PassParameters->HTileConfig  = FIntVector4(PlatformConfig, PixelsWide, GNaniteDebugVisualize, 0);

			auto ComputeShader = View.ShaderMap->GetShader<FHTileVisualizeCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder2,
				RDG_EVENT_NAME("HTileVisualize"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(ViewSize, 8)
			);

			GraphBuilder2.QueueTextureExtraction(DebugOutput, &DebugVisualizationOutput);
			GraphBuilder2.Execute();
		}
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteMarkStencilRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FNaniteMarkStencilPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitMaterialIdRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FNaniteEmitMaterialIdPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitDepthRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FEmitDepthPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

void DrawLumenMeshCapturePass(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	FViewInfo* SharedView,
	const TArray<FCardRenderData, SceneRenderingAllocator>& CardsToRender,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects,
	FIntPoint ViewportSize,
	const TRefCountPtr<IPooledRenderTarget>& Color0RT,
	const TRefCountPtr<IPooledRenderTarget>& Color1RT,
	const TRefCountPtr<IPooledRenderTarget>& DepthRT
	)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));
	checkSlow(DoesPlatformSupportLumenGI(GMaxRHIShaderPlatform));

	LLM_SCOPE(ELLMTag::Nanite);
	RDG_EVENT_SCOPE( GraphBuilder, "Nanite::DrawLumenMeshCapturePass" );

	FRDGTextureRef Color0			= RegisterExternalTextureWithFallback(GraphBuilder, Color0RT, GSystemTextures.BlackDummy, TEXT("CardColor0"));
	FRDGTextureRef Color1			= RegisterExternalTextureWithFallback(GraphBuilder, Color1RT, GSystemTextures.BlackDummy, TEXT("CardColor1"));
	FRDGTextureRef CardDepth		= RegisterExternalTextureWithFallback(GraphBuilder, DepthRT, GSystemTextures.BlackDummy, TEXT("CardDepth"));
	FRDGTextureRef MaterialDepth	= CardDepth;
	FRDGTextureRef Black			= GraphBuilder.RegisterExternalTexture( GSystemTextures.BlackDummy, TEXT("Black") );

	// Visible material mask buffer (currently not used by Lumen, but still must be bound)
	FRDGBufferDesc   VisibleMaterialsDesc = FRDGBufferDesc::CreateStructuredDesc(4, 1);
	FRDGBufferRef    VisibleMaterials     = GraphBuilder.CreateBuffer(VisibleMaterialsDesc, TEXT("NaniteVisibleMaterials"));
	FRDGBufferUAVRef VisibleMaterialsUAV  = GraphBuilder.CreateUAV(VisibleMaterials);
	AddClearUAVPass(GraphBuilder, VisibleMaterialsUAV, 0);

	// Mark stencil for all pixels that pass depth test
	{
		FNaniteMarkStencilRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteMarkStencilRectsParameters>();

		PassParameters->PS.View = SharedView->ViewUniformBuffer;
		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;

		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
			CardDepth,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthRead_StencilWrite
		);
		
		auto PixelShader = SharedView->ShaderMap->GetShader<FNaniteMarkStencilPS>();

		FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
			SharedView->ShaderMap,
			RDG_EVENT_NAME("Mark Stencil"),
			PixelShader,
			PassParameters,
			ViewportSize,
			RectMinMaxBufferSRV,
			NumRects,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
			STENCIL_SANDBOX_MASK
			);
	}

	// Emit material IDs as depth values
	{
		FNaniteEmitMaterialIdRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitMaterialIdRectsParameters>();

		PassParameters->PS.View = SharedView->ViewUniformBuffer;
		PassParameters->PS.DummyZero = 0u;

		PassParameters->PS.VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
		PassParameters->PS.SOAStrides = CullingContext.SOAStrides;
		PassParameters->PS.ClusterPageData = GStreamingManager.GetClusterPageDataSRV();
		PassParameters->PS.ClusterPageHeaders = GStreamingManager.GetClusterPageHeadersSRV();

		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;

		PassParameters->PS.MaterialDepthTable = Scene.MaterialTables[ENaniteMeshPass::LumenCardCapture].GetDepthTableSRV();

		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
			CardDepth,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		auto PixelShader = SharedView->ShaderMap->GetShader<FNaniteEmitMaterialIdPS>();

		FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
			SharedView->ShaderMap,
			RDG_EVENT_NAME("Emit Material Id"),
			PixelShader,
			PassParameters,
			ViewportSize,
			RectMinMaxBufferSRV,
			NumRects,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_Always, true, CF_Equal>::GetRHI(),
			STENCIL_SANDBOX_MASK
		);
	}

	// Emit GBuffer Values
	{
		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->SOAStrides	= CullingContext.SOAStrides;
		PassParameters->MaxClusters	= Nanite::FGlobalResources::GetMaxClusters();
		PassParameters->MaxNodes	= Nanite::FGlobalResources::GetMaxNodes();
		PassParameters->RenderFlags	= CullingContext.RenderFlags;
			
		PassParameters->ClusterPageData		= GStreamingManager.GetClusterPageDataSRV(); 
		PassParameters->ClusterPageHeaders	= GStreamingManager.GetClusterPageHeadersSRV();

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
		PassParameters->InstanceDynamicData = GraphBuilder.CreateSRV(CullingContext.InstanceDynamicData);
#endif
		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);

		PassParameters->MaterialRange = Black;
		PassParameters->GridSize = { 1u, 1u };

		PassParameters->VisibleMaterials = GraphBuilder.CreateSRV(VisibleMaterials, PF_R32_UINT);

		PassParameters->VisBuffer64 = RasterContext.VisBuffer64;
		PassParameters->DbgBuffer64 = Black;
		PassParameters->DbgBuffer32 = Black;

		PassParameters->RenderTargets[0] = FRenderTargetBinding(Color0, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(Color1, ERenderTargetLoadAction::ELoad);

		PassParameters->View = SharedView->ViewUniformBuffer; // To get VTFeedbackBuffer

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			MaterialDepth,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Lumen Emit GBuffer"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &Scene, SharedView, &CardsToRender, ViewportSize](FRHICommandListImmediate& RHICmdList)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LumenEmitGBuffer);

			FMeshDrawCommandStateCache StateCache;

			const FVector2D ViewportSizeF = FVector2D(float(ViewportSize.X), float(ViewportSize.Y));

			for (const FCardRenderData& CardRenderData : CardsToRender)
			{
				CardRenderData.PatchView(RHICmdList, &Scene, SharedView);

				const FVector2D CardViewportSize = FVector2D(float(SharedView->ViewRect.Width()), float(SharedView->ViewRect.Height()));
				const FVector2D RectOffset = FVector2D(float(SharedView->ViewRect.Min.X), float(SharedView->ViewRect.Min.Y)) / ViewportSizeF;
				const FVector2D RectScale = CardViewportSize / ViewportSizeF;

				const_cast<FScene&>(Scene).UniformBuffers.LumenCardCaptureViewUniformBuffer.UpdateUniformBufferImmediate(*SharedView->CachedViewUniformShaderParameters);
				StateCache.InvalidateUniformBuffer(Scene.UniformBuffers.LumenCardCaptureViewUniformBuffer);

				FNaniteUniformParameters UniformParams;
				UniformParams.SOAStrides = PassParameters->SOAStrides;
				UniformParams.MaxClusters = PassParameters->MaxClusters;
				UniformParams.MaxNodes = PassParameters->MaxNodes;
				UniformParams.RenderFlags = PassParameters->RenderFlags;
				UniformParams.MaterialConfig = FIntVector4(0, 1, 1, 0); // Tile based material culling is not required for Lumen, as each card is rendered as a small rect
				UniformParams.RectScaleOffset = FVector4(RectScale, RectOffset); // Render a rect that covers the card viewport

				UniformParams.ClusterPageData = PassParameters->ClusterPageData;
				UniformParams.ClusterPageHeaders = PassParameters->ClusterPageHeaders;

#if SUPPORT_CACHE_INSTANCE_DYNAMIC_DATA
				UniformParams.InstanceDynamicData = PassParameters->InstanceDynamicData->GetRHI();
#endif
				UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

				UniformParams.MaterialRange = PassParameters->MaterialRange->GetRHI();
				UniformParams.VisibleMaterials = PassParameters->VisibleMaterials->GetRHI();

				UniformParams.VisBuffer64 = PassParameters->VisBuffer64->GetRHI();
				UniformParams.DbgBuffer64 = PassParameters->DbgBuffer64->GetRHI();
				UniformParams.DbgBuffer32 = PassParameters->DbgBuffer32->GetRHI();

				FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;

				if (CardRenderData.CardData.bDistantScene)
				{
					TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator> NaniteMaterialPassCommands;
					BuildNaniteMaterialPassCommands(RHICmdList, Scene.NaniteDrawCommands[ENaniteMeshPass::LumenCardCapture], NaniteMaterialPassCommands);

					for (auto CommandsIt = NaniteMaterialPassCommands.CreateConstIterator(); CommandsIt; ++CommandsIt)
					{
						UniformParams.MaterialDepth = CommandsIt->MaterialDepth;
						const_cast<FScene&>(Scene).UniformBuffers.NaniteUniformBuffer.UpdateUniformBufferImmediate(UniformParams);
						StateCache.InvalidateUniformBuffer(Scene.UniformBuffers.NaniteUniformBuffer);

						const uint32 InstanceFactor = 1; // Rendering a single rect per Lumen card, unlike main GBuffer export path that may render 32 if tiled material culling is used.
						FMeshDrawCommand::SubmitDraw(CommandsIt->MeshDrawCommand, GraphicsMinimalPipelineStateSet, nullptr, 0, InstanceFactor, RHICmdList, StateCache);
					}
				}
				else
				{
					for (const FNaniteCommandInfo& CommandInfo : CardRenderData.NaniteCommandInfos)
					{
						Experimental::FHashElementId SetId(CommandInfo.GetStateBucketId());
						const FMeshDrawCommand& MeshDrawCommand = Scene.NaniteDrawCommands[ENaniteMeshPass::LumenCardCapture].GetByElementId(SetId).Key;

						int32 DrawIdx = CommandInfo.GetStateBucketId();

						UniformParams.MaterialDepth = FNaniteCommandInfo::GetDepthId(DrawIdx);
						const_cast<FScene&>(Scene).UniformBuffers.NaniteUniformBuffer.UpdateUniformBufferImmediate(UniformParams);
						StateCache.InvalidateUniformBuffer(Scene.UniformBuffers.NaniteUniformBuffer);

						const uint32 InstanceFactor = 1; // Rendering a single rect per Lumen card, unlike main GBuffer export path that may render 32 if tiled material culling is used.
						FMeshDrawCommand::SubmitDraw(MeshDrawCommand, GraphicsMinimalPipelineStateSet, nullptr, 0, InstanceFactor, RHICmdList, StateCache);
					}
				}
			}
		});
	}

	// Emit depth values
	{
		FNaniteEmitDepthRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitDepthRectsParameters>();

		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;
		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(CardDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilRead);

		auto PixelShader = SharedView->ShaderMap->GetShader<FEmitDepthPS>();

		FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
			SharedView->ShaderMap,
			RDG_EVENT_NAME("Emit Depth"),
			PixelShader,
			PassParameters,
			ViewportSize,
			RectMinMaxBufferSRV,
			NumRects,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_Always, true, CF_Equal>::GetRHI(),
			STENCIL_SANDBOX_MASK
		);
	}
}

FPackedView CreatePackedView( const FPackedViewParams& Params )
{
	// NOTE: There is some overlap with the logic - and this should stay consistent with - FSceneView::SetupViewRectUniformBufferParameters
	// Longer term it would be great to refactor a common place for both of this logic, but currently FSceneView has a lot of heavy-weight
	// stuff in it beyond the relevant parameters to SetupViewRectUniformBufferParameters (and Nanite has a few of its own parameters too).

	FPackedView PackedView;

	PackedView.TranslatedWorldToView		= Params.ViewMatrices.GetOverriddenTranslatedViewMatrix();
	PackedView.TranslatedWorldToClip		= Params.ViewMatrices.GetTranslatedViewProjectionMatrix();
	PackedView.ViewToClip					= Params.ViewMatrices.GetProjectionMatrix();
	PackedView.ClipToWorld					= Params.ViewMatrices.GetInvViewProjectionMatrix();
	PackedView.PreViewTranslation			= Params.ViewMatrices.GetPreViewTranslation();
	PackedView.WorldCameraOrigin			= FVector4(Params.ViewMatrices.GetViewOrigin(), 0.0f);
	PackedView.ViewForwardAndNearPlane		= FVector4(Params.ViewMatrices.GetOverriddenTranslatedViewMatrix().GetColumn(2), Params.ViewMatrices.ComputeNearPlane());

	PackedView.PrevTranslatedWorldToView	= Params.PrevViewMatrices.GetOverriddenTranslatedViewMatrix();
	PackedView.PrevTranslatedWorldToClip	= Params.PrevViewMatrices.GetTranslatedViewProjectionMatrix();
	PackedView.PrevViewToClip				= Params.PrevViewMatrices.GetProjectionMatrix();
	PackedView.PrevClipToWorld				= Params.PrevViewMatrices.GetInvViewProjectionMatrix();
	PackedView.PrevPreViewTranslation		= Params.PrevViewMatrices.GetPreViewTranslation();

	const FIntRect &ViewRect = Params.ViewRect;
	const FVector4 ViewSizeAndInvSize(ViewRect.Width(), ViewRect.Height(), 1.0f / float(ViewRect.Width()), 1.0f / float(ViewRect.Height()));

	PackedView.ViewRect = FIntVector4(ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
	PackedView.ViewSizeAndInvSize = ViewSizeAndInvSize;

	// Transform clip from full screen to viewport.
	FVector2D RcpRasterContextSize = FVector2D(1.0f / Params.RasterContextSize.X, 1.0f / Params.RasterContextSize.Y);
	PackedView.ClipSpaceScaleOffset = FVector4(	ViewSizeAndInvSize.X * RcpRasterContextSize.X,
												ViewSizeAndInvSize.Y * RcpRasterContextSize.Y,
												 ( ViewSizeAndInvSize.X + 2.0f * ViewRect.Min.X) * RcpRasterContextSize.X - 1.0f,
												-( ViewSizeAndInvSize.Y + 2.0f * ViewRect.Min.Y) * RcpRasterContextSize.Y + 1.0f );

	check(Params.StreamingPriorityCategory <= 3);
	PackedView.StreamingPriorityCategory = Params.StreamingPriorityCategory;
	PackedView.MinBoundsRadiusSq = Params.MinBoundsRadius * Params.MinBoundsRadius;
	PackedView.UpdateLODScales();

	PackedView.LODScales.X *= Params.LODScaleFactor;

	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X = Params.TargetLayerIndex;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y = Params.TargetMipLevel;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z = Params.TargetMipCount;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.W = Params.PrevTargetLayerIndex;

	return PackedView;
}

FPackedView CreatePackedViewFromViewInfo(
	const FViewInfo& View,
	FIntPoint RasterContextSize,
	uint32 StreamingPriorityCategory,
	float MinBoundsRadius,
	float LODScaleFactor
	)
{
	FPackedViewParams Params;
	Params.ViewMatrices = View.ViewMatrices;
	Params.PrevViewMatrices = View.PrevViewInfo.ViewMatrices;
	Params.ViewRect = View.ViewRect;
	Params.RasterContextSize = RasterContextSize;
	Params.StreamingPriorityCategory = StreamingPriorityCategory;
	Params.MinBoundsRadius = MinBoundsRadius;
	Params.LODScaleFactor = LODScaleFactor;
	return CreatePackedView(Params);
}

#if WITH_EDITOR

void GetEditorSelectionPassParameters(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FRasterResults* NaniteRasterResults,
	FNaniteSelectionOutlineParameters* OutPassParameters
	)
{
	if (!NaniteRasterResults)
	{
		return;
	}

	LLM_SCOPE(ELLMTag::Nanite);

	FRDGTextureRef VisBuffer64 = RegisterExternalTextureWithFallback(GraphBuilder, NaniteRasterResults->VisBuffer64, GSystemTextures.BlackDummy, TEXT("VisBuffer64"));
	FRDGBufferRef VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(NaniteRasterResults->VisibleClustersSWHW, TEXT("VisibleClustersSWHW"));

	OutPassParameters->View						= View.ViewUniformBuffer;
	OutPassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
	OutPassParameters->MaxClusters				= Nanite::FGlobalResources::GetMaxClusters();
	OutPassParameters->SOAStrides				= NaniteRasterResults->SOAStrides;
	OutPassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
	OutPassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
	OutPassParameters->VisBuffer64				= VisBuffer64;
	OutPassParameters->MaterialHitProxyTable	= Scene.MaterialTables[ENaniteMeshPass::BasePass].GetHitProxyTableSRV();
	OutPassParameters->OutputToInputScale		= FVector2D(View.ViewRect.Size()) / FVector2D(ViewportRect.Size());
}

void DrawEditorSelection(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FNaniteSelectionOutlineParameters& PassParameters
	)
{
	LLM_SCOPE(ELLMTag::Nanite);

	if (View.EditorSelectedHitProxyIds.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NaniteEditorSelection);
	SCOPED_GPU_STAT(RHICmdList, NaniteEditor);

	uint32 SelectionCount = FMath::RoundUpToPowerOfTwo(View.EditorSelectedHitProxyIds.Num());
	uint32 SearchBufferCountDim = FMath::Min((uint32)FEmitEditorSelectionDepthPS::FSearchBufferCountDim::MaxValue, FMath::FloorLog2(SelectionCount));

	FEmitEditorSelectionDepthPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FEmitEditorSelectionDepthPS::FSearchBufferCountDim>(SearchBufferCountDim);

	auto PixelShader = View.ShaderMap->GetShader<FEmitEditorSelectionDepthPS>(PermutationVector.ToDimensionValueId());

	FPixelShaderUtils::DrawFullscreenPixelShader(
		RHICmdList,
		GetGlobalShaderMap(View.FeatureLevel),
		PixelShader,
		PassParameters,
		ViewportRect,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
		3
		);
}

#endif // WITH_EDITOR

} // namespace Nanite
