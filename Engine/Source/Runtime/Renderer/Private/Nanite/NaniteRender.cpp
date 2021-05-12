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
#include "NaniteVisualizationData.h"
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
#include "BasePassRendering.h"
#include "Lumen/LumenSceneRendering.h"
#include "ScreenPass.h"
#include "VirtualShadowMaps/VirtualShadowMapCacheManager.h"

#define CULLING_PASS_NO_OCCLUSION		0
#define CULLING_PASS_OCCLUSION_MAIN		1
#define CULLING_PASS_OCCLUSION_POST		2
#define CULLING_PASS_EXPLICIT_LIST		3

#define RENDER_FLAG_HAVE_PREV_DRAW_DATA				0x1
#define RENDER_FLAG_FORCE_HW_RASTER					0x2
#define RENDER_FLAG_PRIMITIVE_SHADER				0x4
#define RENDER_FLAG_OUTPUT_STREAMING_REQUESTS		0x8
#define RENDER_FLAG_REVERSE_CULLING					0x10

// Only available with the DEBUG_FLAGS permutation active.
#define DEBUG_FLAG_WRITE_STATS						0x1
#define DEBUG_FLAG_CULL_HZB_BOX						0x2
#define DEBUG_FLAG_CULL_HZB_SPHERE					0x4
#define DEBUG_FLAG_CULL_FRUSTUM_BOX					0x8
#define DEBUG_FLAG_CULL_FRUSTUM_SPHERE				0x10
#define DEBUG_FLAG_DRAW_ONLY_VSM_INVALIDATING		0x20

#define NUM_PRINT_STATS_PASSES						3

DECLARE_GPU_STAT_NAMED(NaniteInstanceCull,		TEXT("Nanite Instance Cull"));
DECLARE_GPU_STAT_NAMED(NaniteInstanceCullVSM,	TEXT("Nanite Instance Cull VSM"));

DECLARE_GPU_STAT_NAMED(NaniteClusterCull,		TEXT("Nanite Cluster Cull"));

DEFINE_GPU_STAT(NaniteDebug);
DEFINE_GPU_STAT(NaniteDepth);
DEFINE_GPU_STAT(NaniteEditor);
DEFINE_GPU_STAT(NaniteRaster);
DEFINE_GPU_STAT(NaniteMaterials);

DECLARE_DWORD_COUNTER_STAT(TEXT("CullingContexts"), STAT_NaniteCullingContexts, STATGROUP_Nanite);

DEFINE_LOG_CATEGORY(LogNanite);

#define NANITE_MATERIAL_STENCIL 1

int32 GNaniteAsyncRasterization = 0; // @todo: Disabled because GPUScene/Nanite resource transitions are not correct for async compute rasterization
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

int32 GNaniteVisualizeOverdrawScale = 15; // % of contribution per pixel evaluation (up to 100%)
FAutoConsoleVariableRef CVarNaniteVisualizeOverdrawScale(
	TEXT("r.Nanite.Visualize.OverdrawScale"),
	GNaniteVisualizeOverdrawScale,
	TEXT("")
);

int32 GNaniteVisualizeComplexityScale = 80; // % of contribution per material evaluation (up to 100%)
FAutoConsoleVariableRef CVarNaniteVisualizeComplexityScale(
	TEXT("r.Nanite.Visualize.ComplexityScale"),
	GNaniteVisualizeComplexityScale,
	TEXT("")
);

// Specifies if visualization only shows Nanite information that passes full scene depth test
// -1: Use default composition specified the each mode
//  0: Force composition with scene depth off
//  1: Force composition with scene depth on
int32 GNaniteVisualizeComposite = -1;
FAutoConsoleVariableRef CVarNaniteVisualizeComposite(
	TEXT("r.Nanite.Visualize.Composite"),
	GNaniteVisualizeComposite,
	TEXT("")
);

int32 GNaniteVisualizeEdgeDetect = 1;
static FAutoConsoleVariableRef CVarNaniteVisualizeEdgeDetect(
	TEXT("r.Nanite.Visualize.EdgeDetect"),
	GNaniteVisualizeEdgeDetect,
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

int32 GNaniteClusterPerPage = 1;
static FAutoConsoleVariableRef CVarNaniteClusterPerPage(
	TEXT("r.Nanite.ClusterPerPage"),
	GNaniteClusterPerPage,
	TEXT("")
);

int32 GNaniteMaterialCulling = 4;
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

int32 GNaniteShowStats = 0;
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

static TAutoConsoleVariable<int32> CVarCompactVSMViews(
	TEXT("r.Nanite.CompactVSMViews"),
	1,
	TEXT(""),
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

static FIntVector4 GetVisualizeConfig(int32 ModeID, bool bCompositeScene, bool bEdgeDetect)
{
	if (ModeID != INDEX_NONE)
	{
		return FIntVector4(ModeID, 0 /* Unused */, bCompositeScene ? 1 : 0, bEdgeDetect ? 1 : 0);
	}
	
	return FIntVector4(INDEX_NONE, 0, 0, 0);
}

static FIntVector4 GetVisualizeScales(int32 ModeID)
{
	if (ModeID != INDEX_NONE)
	{
		return FIntVector4(GNaniteVisualizeOverdrawScale, GNaniteVisualizeComplexityScale, 0 /* Unused */, 0 /* Unused */);
	}

	return FIntVector4(INDEX_NONE, 0, 0, 0);
}

// Must match FStats in NaniteDataDecode.ush
struct FNaniteStats
{
	uint32 NumTris;
	uint32 NumVerts;
	uint32 NumViews;
	uint32 NumMainInstancesPreCull;
	uint32 NumMainInstancesPostCull;
	uint32 NumMainVisitedNodes;
	uint32 NumMainCandidateClusters;
	uint32 NumPostInstancesPreCull;
	uint32 NumPostInstancesPostCull;
	uint32 NumPostVisitedNodes;
	uint32 NumPostCandidateClusters;
	uint32 NumLargePageRectClusters;
	uint32 NumPrimaryViews;
	uint32 NumTotalViews;
};

struct FCompactedViewInfo
{
	uint32 StartOffset;
	uint32 NumValidViews;
};


IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteUniformParameters, "Nanite");

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitGBufferParameters, )
	SHADER_PARAMETER(FIntVector4,	VisualizeConfig)
	SHADER_PARAMETER(FIntVector4,	SOAStrides)
	SHADER_PARAMETER(uint32,		MaxVisibleClusters)
	SHADER_PARAMETER(uint32,		MaxNodes)
	SHADER_PARAMETER(uint32,		RenderFlags)
	SHADER_PARAMETER(FIntPoint,		GridSize)
	
	SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageData )
	SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageHeaders )

	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,	VisibleClustersSWHW)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, MaterialRange)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleMaterials)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, DbgBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,  DbgBuffer32)

	// Multi view
	SHADER_PARAMETER(uint32, MultiViewEnabled)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MultiViewIndices)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, MultiViewRectScaleOffsets)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)	// To access VTFeedbackBuffer
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardPassUniformParameters, CardPass)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT( FCullingParameters, )
	SHADER_PARAMETER( FIntVector4,	SOAStrides )
	SHADER_PARAMETER( uint32,		MaxCandidateClusters )
	SHADER_PARAMETER( uint32,		MaxVisibleClusters )
	SHADER_PARAMETER( uint32,		RenderFlags )
	SHADER_PARAMETER( uint32,		DebugFlags )
	SHADER_PARAMETER( uint32,		NumViews )
	SHADER_PARAMETER( uint32,		NumPrimaryViews )
	SHADER_PARAMETER( float,		DisocclusionLodScaleFactor )

	SHADER_PARAMETER( FVector2D,	HZBSize )

	SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	HZBTexture )
	SHADER_PARAMETER_SAMPLER( SamplerState,		HZBSampler )
	
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FCompactedViewInfo >, CompactedViewInfo)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, CompactedViewsAllocation)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT( FGPUSceneParameters, )
	SHADER_PARAMETER_SRV( StructuredBuffer<float4>,	GPUSceneInstanceSceneData)
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

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer, OutCandidateNodesAndClusters )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FInstanceDraw >, OutOccludedInstances )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >, OutMainAndPostPassPersistentStates )
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

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer, OutCandidateNodesAndClusters )
	
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >, OutMainAndPostPassPersistentStates )
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

		SHADER_PARAMETER_SRV( ByteAddressBuffer,								ClusterPageHeaders )
		SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageData )
		SHADER_PARAMETER_SRV( ByteAddressBuffer,				HierarchyBuffer )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FUintVector2 >,		InTotalPrevDrawClusters )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >,						OffsetClustersArgsSWHW )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >,MainAndPostPassPersistentStates )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					InOutCandidateNodesAndClusters )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					OutVisibleClustersSWHW )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,					OutOccludedNodesAndClusters )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer<FStreamingRequest>,	OutStreamingRequests )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, VisibleClustersArgsSWHW )

		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualTargetParameters, VirtualShadowMap )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >, OutDynamicCasterFlags)

		SHADER_PARAMETER(uint32,												MaxNodes)
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

		// The routing requires access to page table data structures, only for 'VIRTUAL_TEXTURE_TARGET' really...
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FPersistentClusterCull_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "PersistentClusterCull", SF_Compute);

class FInitCandidateClusters_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInitCandidateClusters_CS );
	SHADER_USE_PARAMETER_STRUCT( FInitCandidateClusters_CS, FNaniteShader );

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,	OutCandidateNodesAndClusters )
		SHADER_PARAMETER( uint32,								MaxCandidateClusters )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitCandidateClusters_CS, "/Engine/Private/Nanite/ClusterCulling.usf", "InitCandidateClusters", SF_Compute);

class FInitCandidateNodes_CS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FInitCandidateNodes_CS );
	SHADER_USE_PARAMETER_STRUCT( FInitCandidateNodes_CS, FNaniteShader );

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer,	OutCandidateNodesAndClusters)
		SHADER_PARAMETER( uint32,								InitIsPostPass )
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

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >,OutMainAndPostPassPersistentStates )
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

	SHADER_PARAMETER( FIntVector4,	SOAStrides )
	SHADER_PARAMETER( uint32,		MaxVisibleClusters )
	SHADER_PARAMETER( uint32,		RenderFlags )
	SHADER_PARAMETER( uint32,		VisualizeModeBitMask )
	
	SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageData )
	SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageHeaders )

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
	class FVisualizeDim : SHADER_PERMUTATION_BOOL("VISUALIZE");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	class FClusterPerPageDim : SHADER_PERMUTATION_BOOL("CLUSTER_PER_PAGE");
	class FNearClipDim : SHADER_PERMUTATION_BOOL("NEAR_CLIP");

	using FPermutationDomain = TShaderPermutationDomain<FRasterTechniqueDim, FMultiViewDim, FPrimShaderDim, FVisualizeDim, FVirtualTextureTargetDim, FClusterPerPageDim, FNearClipDim>;

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

		if (PermutationVector.Get<FPrimShaderDim>() &&
			!FDataDrivenShaderPlatformInfo::GetSupportsPrimitiveShaders(Parameters.Platform))
		{
			// Only some platforms support primitive shaders.
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

class FEmitMaterialDepthPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitMaterialDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitMaterialDepthPS, FNaniteShader);

	class FNaniteMaskDim : SHADER_PERMUTATION_BOOL("NANITE_MASK");
	using FPermutationDomain = TShaderPermutationDomain<FNaniteMaskDim>;

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

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NaniteMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)

		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitMaterialDepthPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitMaterialDepthPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FNaniteMaterialVS, "/Engine/Private/Nanite/ExportGBuffer.usf", "FullScreenVS", SF_Vertex);

class FEmitSceneDepthPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneDepthPS, FNaniteShader);

	class FVelocityExportDim : SHADER_PERMUTATION_BOOL("VELOCITY_EXPORT");
	using FPermutationDomain = TShaderPermutationDomain<FVelocityExportDim>;

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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_RDG_TEXTURE( Texture2D<UlongType>,	VisBuffer64 )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneDepthPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitSceneDepthPS", SF_Pixel);

class FEmitSceneStencilPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneStencilPS, FNaniteShader);

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

		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NaniteMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneStencilPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitSceneStencilPS", SF_Pixel);

class FEmitSceneDepthStencilPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneDepthStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneDepthStencilPS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER(uint32, StencilClear)
		SHADER_PARAMETER(uint32, StencilDecal)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneDepthStencilPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitSceneDepthStencilPS", SF_Pixel);

class FEmitShadowMapPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitShadowMapPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitShadowMapPS, FNaniteShader);

	class FDepthOutputTypeDim : SHADER_PERMUTATION_INT("DEPTH_OUTPUT_TYPE", 3);
	using FPermutationDomain = TShaderPermutationDomain< FDepthOutputTypeDim >;

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
		SHADER_PARAMETER( FIntPoint, SourceOffset )
		SHADER_PARAMETER( float, ViewToClip22 )
		SHADER_PARAMETER( float, DepthBias )
		SHADER_PARAMETER( uint32, ShadowMapID )
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

class FEmitEditingLevelInstanceDepthPS : public FNaniteShader
{
public:
	DECLARE_GLOBAL_SHADER(FEmitEditingLevelInstanceDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitEditingLevelInstanceDepthPS, FNaniteShader);

	using FParameters = FNaniteVisualizeLevelInstanceParameters;

	class FSearchBufferCountDim : SHADER_PERMUTATION_INT("EDITOR_LEVELINSTANCE_BUFFER_COUNT_LOG_2", 25);
	using FPermutationDomain = TShaderPermutationDomain<FSearchBufferCountDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		uint32 LevelInstanceBufferCount = 1u << (uint32)PermutationVector.Get<FSearchBufferCountDim>();
		OutEnvironment.SetDefine(TEXT("EDITOR_LEVELINSTANCE_BUFFER_COUNT"), LevelInstanceBufferCount);
	}
};
IMPLEMENT_GLOBAL_SHADER(FEmitEditingLevelInstanceDepthPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitEditorLevelInstanceDepthPS", SF_Pixel);

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

class FNaniteVisualizeCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FNaniteVisualizeCS);
	SHADER_USE_PARAMETER_STRUCT(FNaniteVisualizeCS, FNaniteShader);

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
		SHADER_PARAMETER(FIntVector4, VisualizeScales)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER(uint32, MaxVisibleClusters)
		SHADER_PARAMETER(uint32, RenderFlags)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, DbgBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DbgBuffer32)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NaniteMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, MaterialComplexity)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialHitProxyTable)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FNaniteVisualizeCS, "/Engine/Private/Nanite/Visualize.usf", "VisualizeCS", SF_Compute);

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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Velocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, NaniteMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, SceneHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, SceneStencil)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, MaterialHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, MaterialDepth)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDepthExportCS, "/Engine/Private/Nanite/DepthExport.usf", "DepthExport", SF_Compute);

class FClassifyMaterialsCS : public FNaniteShader
{
public:
	class FCullingMethodDim : SHADER_PERMUTATION_INT("CULLING_METHOD", 5);
	using FPermutationDomain = TShaderPermutationDomain<FCullingMethodDim>;

	DECLARE_GLOBAL_SHADER(FClassifyMaterialsCS);
	SHADER_USE_PARAMETER_STRUCT(FClassifyMaterialsCS, FNaniteShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER(FIntVector4, ViewRect)
		SHADER_PARAMETER(FIntPoint, FetchClamp)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, MaterialRange)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, VisibleMaterials)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const uint32 CullingMethod = uint32(PermutationVector.Get<FCullingMethodDim>());
		if (CullingMethod == 0)
		{
			// No culling - don't bother compiling this permutation. Keeps it 1:1 with cvar
			return false;
		}

		if ((CullingMethod == 1 || CullingMethod == 2) && !FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform))
		{
			// Platform doesn't support necessary wave intrinsics for the 8x4 grid method
			return false;
		}

		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const uint32 CullingMethod = uint32(PermutationVector.Get<FCullingMethodDim>());
		OutEnvironment.SetDefine(TEXT("MATERIAL_CULLING_METHOD"), CullingMethod);
	}
};
IMPLEMENT_GLOBAL_SHADER(FClassifyMaterialsCS, "/Engine/Private/Nanite/MaterialCulling.usf", "ClassifyMaterials", SF_Compute);

class FMaterialComplexityCS : public FNaniteShader
{
public:
	DECLARE_GLOBAL_SHADER(FMaterialComplexityCS);
	SHADER_USE_PARAMETER_STRUCT(FMaterialComplexityCS, FNaniteShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER(FIntVector4, ViewRect)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, MaterialComplexity)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMaterialComplexityCS, "/Engine/Private/Nanite/MaterialComplexity.usf", "CalculateMaterialComplexity", SF_Compute);

// TODO: Move to common location outside of Nanite
class FHTileVisualizeCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FHTileVisualizeCS);
	SHADER_USE_PARAMETER_STRUCT(FHTileVisualizeCS, FNaniteShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(TextureMetadata, HTileBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HTileDisplay)
		SHADER_PARAMETER(FIntVector4, HTileConfig)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FHTileVisualizeCS, "/Engine/Private/HTileVisualize.usf", "VisualizeHTile", SF_Compute);

// Gather culling stats and build dispatch indirect buffer for per-cluster stats
class FCalculateStatsCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FCalculateStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FCalculateStatsCS, FNaniteShader);

	class FTwoPassCullingDim : SHADER_PERMUTATION_BOOL( "TWO_PASS_CULLING" );
	using FPermutationDomain = TShaderPermutationDomain<FTwoPassCullingDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER(uint32, RenderFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FNaniteStats>, OutStatsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutClusterStatsArgs)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPersistentState >, MainAndPostPassPersistentStates)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, MainPassRasterizeArgsSWHW)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PostPassRasterizeArgsSWHW)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCalculateStatsCS, "/Engine/Private/Nanite/PrintStats.usf", "CalculateStats", SF_Compute);

// Calculates and accumulates per-cluster stats
class FCalculateClusterStatsCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FCalculateClusterStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FCalculateClusterStatsCS, FNaniteShader );

	class FTwoPassCullingDim : SHADER_PERMUTATION_BOOL("TWO_PASS_CULLING");
	class FVirtualTextureTargetDim : SHADER_PERMUTATION_BOOL("VIRTUAL_TEXTURE_TARGET");
	using FPermutationDomain = TShaderPermutationDomain<FTwoPassCullingDim, FVirtualTextureTargetDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( FIntVector4, SOAStrides )
		SHADER_PARAMETER( uint32, MaxVisibleClusters )
		SHADER_PARAMETER( uint32, RenderFlags )

		SHADER_PARAMETER_SRV( ByteAddressBuffer,	ClusterPageData )
		SHADER_PARAMETER_SRV( ByteAddressBuffer,	ClusterPageHeaders )

		SHADER_PARAMETER_RDG_BUFFER_SRV( ByteAddressBuffer, VisibleClustersSWHW )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer<FNaniteStats>, OutStatsBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, MainPassRasterizeArgsSWHW )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, PostPassRasterizeArgsSWHW )
		RDG_BUFFER_ACCESS(StatsArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCalculateClusterStatsCS, "/Engine/Private/Nanite/PrintStats.usf", "CalculateClusterStats", SF_Compute);

class FPrintStatsCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER( FPrintStatsCS );
	SHADER_USE_PARAMETER_STRUCT( FPrintStatsCS, FNaniteShader );

	class FTwoPassCullingDim : SHADER_PERMUTATION_BOOL( "TWO_PASS_CULLING" );
	class FPassDim : SHADER_PERMUTATION_INT("PRINT_PASS", NUM_PRINT_STATS_PASSES);
	using FPermutationDomain = TShaderPermutationDomain<FTwoPassCullingDim, FPassDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER(uint32, PackedClusterSize)
		SHADER_PARAMETER(uint32, RenderFlags)
		SHADER_PARAMETER(uint32, DebugFlags)

		SHADER_PARAMETER_STRUCT_INCLUDE( ShaderPrint::FShaderParameters, ShaderPrintStruct )

		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer<FNaniteStats>, InStatsBuffer )

		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, MainPassRasterizeArgsSWHW )
		SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, PostPassRasterizeArgsSWHW )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPrintStatsCS, "/Engine/Private/Nanite/PrintStats.usf", "PrintStats", SF_Compute);

FNaniteDrawListContext::FNaniteDrawListContext
(
	FRWLock& InNaniteDrawCommandLock,
	FStateBucketMap& InNaniteDrawCommands
) 
: NaniteDrawCommandLock(&InNaniteDrawCommandLock)
, NaniteDrawCommands(&InNaniteDrawCommands)
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
	EFVisibleMeshDrawCommandFlags Flags,
	const FGraphicsMinimalPipelineStateInitializer& PipelineState,
	const FMeshProcessorShaders* ShadersForDebugging,
	FMeshDrawCommand& MeshDrawCommand
	)
{
	// ensureMsgf(!EnumHasAnyFlags(Flags, EFVisibleMeshDrawCommandFlags::HasPrimitiveIdStreamIndex), TEXT("Nanite does not support WPO materials!"));
	FGraphicsMinimalPipelineStateId PipelineId;
	PipelineId = FGraphicsMinimalPipelineStateId::GetPersistentId(PipelineState);

	MeshDrawCommand.SetDrawParametersAndFinalize(MeshBatch, BatchElementIndex, PipelineId, ShadersForDebugging);

	check(UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel));

	Experimental::FHashElementId SetId;
	auto hash = NaniteDrawCommands->ComputeHash(MeshDrawCommand);
	{
		FRWScopeLock Lock(*NaniteDrawCommandLock, SLT_ReadOnly);

#if UE_BUILD_DEBUG
		FMeshDrawCommand MeshDrawCommandDebug = FMeshDrawCommand(MeshDrawCommand);
		check(MeshDrawCommandDebug.ShaderBindings.GetDynamicInstancingHash() == MeshDrawCommand.ShaderBindings.GetDynamicInstancingHash());
		check(MeshDrawCommandDebug.GetDynamicInstancingHash() == MeshDrawCommand.GetDynamicInstancingHash());
#endif
		
		SetId = NaniteDrawCommands->FindIdByHash(hash, MeshDrawCommand);
		
		if (!SetId.IsValid())
		{
			Lock.ReleaseReadOnlyLockAndAcquireWriteLock_USE_WITH_CAUTION();
			SetId = NaniteDrawCommands->FindOrAddIdByHash(hash, MeshDrawCommand, FMeshDrawCommandCount());

#if MESH_DRAW_COMMAND_DEBUG_DATA
			FMeshDrawCommandCount& DrawCount = NaniteDrawCommands->GetByElementId(SetId).Value;
			if (DrawCount.Num == 0)
			{
				MeshDrawCommand.ClearDebugPrimitiveSceneProxy(); //When using State Buckets multiple PrimitiveSceneProxies use the same MeshDrawCommand, so The PrimitiveSceneProxy pointer can't be stored.
			}
#endif
		}
		
		FMeshDrawCommandCount& DrawCount = NaniteDrawCommands->GetByElementId(SetId).Value;
		DrawCount.Num++;
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

void FNaniteMeshProcessor::AddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId /*= -1 */
)
{
	LLM_SCOPE_BYTAG(Nanite);

	if (!MeshBatch.bUseForMaterial)
	{
		return;
	}

	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = MeshBatch.MaterialRenderProxy;
	while (FallbackMaterialRenderProxyPtr)
	{
		const FMaterial* Material = FallbackMaterialRenderProxyPtr->GetMaterialNoFallback(FeatureLevel);
		if (Material && TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, *FallbackMaterialRenderProxyPtr, *Material))
		{
			break;
		}
		FallbackMaterialRenderProxyPtr = FallbackMaterialRenderProxyPtr->GetFallback(FeatureLevel);
	}
}

bool FNaniteMeshProcessor::TryAddMeshBatch(
	const FMeshBatch & RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy * RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material
)
{
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

	const bool bAllowIndirectLightingCache = Scene && Scene->PrecomputedLightVolumes.Num() > 0;
	const bool bUseVolumetricLightmap = Scene && Scene->VolumetricLightmapSceneData.HasData(); 
	
	static const auto CVarSupportLowQualityLightmap = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportLowQualityLightmaps"));
	const bool bAllowLowQualityLightMaps = (!CVarSupportLowQualityLightmap) || (CVarSupportLowQualityLightmap->GetValueOnAnyThread() != 0);

	// Determine light map policy type
	FUniformLightMapPolicy LightMapPolicy = FUniformLightMapPolicy(LMP_NO_LIGHTMAP);
	if (LightMapInteraction.GetType() == LMIT_Texture)
	{
		if (bAllowHighQualityLightMaps)
		{
			const FShadowMapInteraction ShadowMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
				? MeshBatch.LCI->GetShadowMapInteraction(FeatureLevel)
				: FShadowMapInteraction();

			if (ShadowMapInteraction.GetType() == SMIT_Texture)
			{
				LightMapPolicy = FUniformLightMapPolicy(LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP);
			}
			else
			{
				LightMapPolicy = FUniformLightMapPolicy(LMP_HQ_LIGHTMAP);
			}
		}
		else if (bAllowLowQualityLightMaps)
		{
			LightMapPolicy = FUniformLightMapPolicy(LMP_LQ_LIGHTMAP);
		}
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
			LightMapPolicy = FUniformLightMapPolicy(LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING);
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
					LightMapPolicy = FUniformLightMapPolicy(LMP_CACHED_VOLUME_INDIRECT_LIGHTING);
				}
				else
				{
					// Use a light map policy that supports reading indirect lighting from a single SH sample
					LightMapPolicy = FUniformLightMapPolicy(LMP_CACHED_POINT_INDIRECT_LIGHTING);
				}
			}
		}
	}

	TShaderMapRef<FNaniteMaterialVS> NaniteVertexShader(GetGlobalShaderMap(FeatureLevel));
	TShaderRef<TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>> BasePassPixelShader;

	bool bShadersValid = GetBasePassShaders<FUniformLightMapPolicy>(
		Material,
		MeshBatch.VertexFactory->GetType(),
		LightMapPolicy,
		FeatureLevel,
		bRenderAtmosphericFog,
		bRenderSkylight,
		false,
		nullptr,
		&BasePassPixelShader
		);

	if (!bShadersValid)
	{
		return false;
	}

	TMeshProcessorShaders
	<
		FNaniteMaterialVS,
		TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>
	>
	PassShaders;

	PassShaders.VertexShader = NaniteVertexShader;
	PassShaders.PixelShader = BasePassPixelShader;

	TBasePassShaderElementData<FUniformLightMapPolicy> ShaderElementData(MeshBatch.LCI);
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

	return true;
}

FMeshPassProcessor* CreateNaniteMeshProcessor(
	const FScene* Scene, 
	const FSceneView* InViewIfDynamicMeshCommand, 
	FMeshPassDrawListContext* InDrawListContext
	)
{
	FMeshPassProcessorRenderState PassDrawRenderState;
	PassDrawRenderState.SetNaniteUniformBuffer(Scene->UniformBuffers.NaniteUniformBuffer);

	const bool bStencilExport = (NANITE_MATERIAL_STENCIL != 0) && !UseComputeDepthExport();
	if (bStencilExport)
	{
		SetupBasePassState(FExclusiveDepthStencil::DepthWrite_StencilWrite, false, PassDrawRenderState);
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal, true, CF_Equal>::GetRHI());
		PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
		PassDrawRenderState.SetStencilRef(STENCIL_SANDBOX_MASK);
	}
	else
	{
		SetupBasePassState(FExclusiveDepthStencil::DepthWrite_StencilNop, false, PassDrawRenderState);
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal>::GetRHI());
		PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilNop);
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

void FNaniteMaterialTables::UpdateBufferState(FRDGBuilder& GraphBuilder, uint32 NumPrimitives)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	check(NumPrimitiveUpdates == 0);
	check(NumDepthTableUpdates == 0);
#if WITH_EDITOR
	check(NumHitProxyTableUpdates == 0);
#endif

	TArray<FRHITransitionInfo, TInlineAllocator<2>> UAVs;

	const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumPrimitives * MaxMaterials, 256u));

	if (ResizeResourceIfNeeded(GraphBuilder, DepthTableDataBuffer, SizeReserve * sizeof(uint32), TEXT("Nanite.DepthTableDataBuffer")))
	{
		UAVs.Add(FRHITransitionInfo(DepthTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
	}
#if WITH_EDITOR
	if (ResizeResourceIfNeeded(GraphBuilder, HitProxyTableDataBuffer, SizeReserve * sizeof(uint32), TEXT("Nanite.HitProxyTableDataBuffer")))
	{
		UAVs.Add(FRHITransitionInfo(HitProxyTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
	}
#endif // WITH_EDITOR

	GraphBuilder.AddPass(RDG_EVENT_NAME("NaniteMaterialTables.UpdateBufferState-Transition"), ERDGPassFlags::None,
		[LocalUAVs = MoveTemp(UAVs)](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.Transition(LocalUAVs);
	});
}

void FNaniteMaterialTables::Begin(FRHICommandListImmediate& RHICmdList, uint32 NumPrimitives, uint32 InNumPrimitiveUpdates)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	check(NumPrimitiveUpdates == 0);
	check(NumDepthTableUpdates == 0);
	const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumPrimitives * MaxMaterials, 256u));
#if WITH_EDITOR
	check(NumHitProxyTableUpdates == 0);

	check(HitProxyTableDataBuffer.NumBytes == SizeReserve * sizeof(uint32));
#endif
	check(DepthTableDataBuffer.NumBytes == SizeReserve * sizeof(uint32));

	NumPrimitiveUpdates = InNumPrimitiveUpdates;
	if (NumPrimitiveUpdates > 0)
	{
		DepthTableUploadBuffer.Init(NumPrimitiveUpdates * MaxMaterials, sizeof(uint32), false, TEXT("Nanite.DepthTableUploadBuffer"));
	#if WITH_EDITOR
		HitProxyTableUploadBuffer.Init(NumPrimitiveUpdates * MaxMaterials, sizeof(uint32), false, TEXT("Nanite.HitProxyTableUploadBuffer"));
	#endif
	}
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

	LLM_SCOPE_BYTAG(Nanite);

#if WITH_EDITOR
	check(NumHitProxyTableUpdates <= NumPrimitiveUpdates);
#endif
	check(NumDepthTableUpdates <= NumPrimitiveUpdates);
	if (NumPrimitiveUpdates == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENTF(RHICmdList, UpdateMaterialTables, TEXT("UpdateMaterialTables PrimitivesToUpdate = %u"), NumPrimitiveUpdates);

	TArray<FRHITransitionInfo, TInlineAllocator<2>> UploadUAVs;
	UploadUAVs.Add(FRHITransitionInfo(DepthTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
#if WITH_EDITOR
	UploadUAVs.Add(FRHITransitionInfo(HitProxyTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
#endif

	RHICmdList.Transition(UploadUAVs);

	DepthTableUploadBuffer.ResourceUploadTo(RHICmdList, DepthTableDataBuffer, false);
#if WITH_EDITOR
	HitProxyTableUploadBuffer.ResourceUploadTo(RHICmdList, HitProxyTableDataBuffer, false);
#endif

	for (FRHITransitionInfo& Info : UploadUAVs)
	{
		Info.AccessBefore = Info.AccessAfter;
		Info.AccessAfter = ERHIAccess::SRVMask;
	}

	RHICmdList.Transition(UploadUAVs);

	NumDepthTableUpdates = 0;
#if WITH_EDITOR
	NumHitProxyTableUpdates = 0;
#endif
	NumPrimitiveUpdates = 0;
}

static_assert( 1 + NUM_CULLING_FLAG_BITS + MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS + MAX_INSTANCES_BITS + MAX_GPU_PAGES_BITS + MAX_CLUSTERS_PER_PAGE_BITS <= 64, "FVisibleCluster fields don't fit in 64bits" );

static_assert( 1 + NUM_CULLING_FLAG_BITS + MAX_INSTANCES_BITS <= 32,							"FCandidateNode.x fields don't fit in 32bits" );
static_assert( 1 + MAX_NODES_PER_PRIMITIVE_BITS + MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS <= 32, "FCandidateNode.y fields don't fit in 32bits" );
static_assert( 1 + MAX_BVH_NODES_PER_GROUP <= 32,												"FCandidateNode.z fields don't fit in 32bits" );


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
	if (GNaniteShowStats == 0)
	{
		// Stats are disabled, do nothing.
		return false;
	}

	return (GNaniteStatsFilter == FilterName);
}

bool IsStatFilterActiveForLight(const FLightSceneProxy* LightProxy)
{
	if (GNaniteShowStats == 0)
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
		if (bListShadows)
		{
			const auto& VirtualShadowMaps = SceneRenderer->SortedShadowsForShadowDepthPass.VirtualShadowMapShadows;
			const auto& VirtualShadowClipmaps = SceneRenderer->SortedShadowsForShadowDepthPass.VirtualShadowMapClipmaps;

			if (VirtualShadowClipmaps.Num() > 0)
			{
				UE_LOG(LogNanite, Warning, TEXT("VSM_Directional"));
			}

			if (VirtualShadowMaps.Num() > 0)
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
				UE_LOG(LogNanite, Warning, TEXT("ShadowAtlas%d"), AtlasIndex);
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

static void AddPassInitCandidateNodesAndClustersUAV( FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferUAVRef UAVRef, bool bIsPostPass )
{
	LLM_SCOPE_BYTAG(Nanite);

	{
		const uint32 ThreadsPerGroup = 64;
		checkf(Nanite::FGlobalResources::GetMaxNodes() % ThreadsPerGroup == 0, TEXT("Max nodes must be divisible by ThreadsPerGroup"));

		FInitCandidateNodes_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitCandidateNodes_CS::FParameters >();
		PassParameters->OutCandidateNodesAndClusters		= UAVRef;
		PassParameters->InitIsPostPass						= bIsPostPass ? 1 : 0;
		PassParameters->MaxCandidateClusters				= Nanite::FGlobalResources::GetMaxCandidateClusters();

		const uint32 NumGroups = FMath::DivideAndRoundUp(Nanite::FGlobalResources::GetMaxNodes(), ThreadsPerGroup);

		auto ComputeShader = ShaderMap->GetShader< FInitCandidateNodes_CS >();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "Nanite::InitNodes" ),
			ComputeShader,
			PassParameters,
			FIntVector(NumGroups, 1, 1)
		);
	}

	{
		const uint32 ThreadsPerYGroup = 64 * 256;
		FInitCandidateClusters_CS::FParameters* PassParameters	= GraphBuilder.AllocParameters< FInitCandidateClusters_CS::FParameters >();
		PassParameters->OutCandidateNodesAndClusters			= UAVRef;
		PassParameters->MaxCandidateClusters					= Nanite::FGlobalResources::GetMaxCandidateClusters();

		const uint32 NumYGroups = FMath::DivideAndRoundUp(Nanite::FGlobalResources::GetMaxCandidateClusters(), ThreadsPerYGroup);

		auto ComputeShader = ShaderMap->GetShader< FInitCandidateClusters_CS >();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Nanite::InitCandidates"),
			ComputeShader,
			PassParameters,
			FIntVector(256, NumYGroups, 1)
		);
	}
}

FCullingContext InitCullingContext(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const TRefCountPtr<IPooledRenderTarget> &PrevHZB,
	const FIntRect &HZBBuildViewRect,
	bool bTwoPassOcclusion,
	bool bUpdateStreaming,
	bool bSupportsMultiplePasses,
	bool bForceHWRaster,
	bool bPrimaryContext,
	bool bDrawOnlyVSMInvalidatingGeometry
	)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::InitContext");

	INC_DWORD_STAT(STAT_NaniteCullingContexts);

	FCullingContext CullingContext = {};

	CullingContext.ShaderMap = GetGlobalShaderMap(Scene.GetFeatureLevel());

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

	if (UsePrimitiveShader())
	{
		CullingContext.RenderFlags |= RENDER_FLAG_PRIMITIVE_SHADER;
	}

	// TODO: Exclude from shipping builds
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

		if (bDrawOnlyVSMInvalidatingGeometry)
		{
			CullingContext.DebugFlags |= DEBUG_FLAG_DRAW_ONLY_VSM_INVALIDATING;
		}
	}

	// TODO: Might this not break if the view has overridden the InstanceSceneData?
	const uint32 NumSceneInstancesPo2					= FMath::RoundUpToPowerOfTwo(Scene.GPUScene.InstanceDataAllocator.GetMaxSize());
	CullingContext.SOAStrides.X							= Scene.GPUScene.InstanceDataSOAStride;
	CullingContext.SOAStrides.Y							= NumSceneInstancesPo2;
	
	check(NumSceneInstancesPo2 <= MAX_INSTANCES);		// There are too many instances in the scene.

	CullingContext.MainAndPostPassPersistentStates		= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateStructuredDesc( 20, 2 ), TEXT("Nanite.MainAndPostPassPersistentStates") );

#if NANITE_USE_SCRATCH_BUFFERS
	if (bPrimaryContext)
	{
		CullingContext.VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetPrimaryVisibleClustersBufferRef(), TEXT("Nanite.VisibleClustersSWHW"));
	}
	else
	{
		CullingContext.VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetScratchVisibleClustersBufferRef(), TEXT("Nanite.VisibleClustersSWHW"));
	}
#else
	FRDGBufferDesc VisibleClustersDesc			= FRDGBufferDesc::CreateStructuredDesc(4, 3 * Nanite::FGlobalResources::GetMaxVisibleClusters());	// Max visible clusters * sizeof(uint3)
	VisibleClustersDesc.Usage					= EBufferUsageFlags(VisibleClustersDesc.Usage | BUF_ByteAddressBuffer);

	CullingContext.VisibleClustersSWHW			= GraphBuilder.CreateBuffer(VisibleClustersDesc, TEXT("Nanite.VisibleClustersSWHW"));
#endif

	CullingContext.MainRasterizeArgsSWHW		= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(8), TEXT("Nanite.MainRasterizeArgsSWHW"));
	CullingContext.SafeMainRasterizeArgsSWHW	= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(8), TEXT("Nanite.SafeMainRasterizeArgsSWHW"));
	
	if( CullingContext.bTwoPassOcclusion )
	{
	#if NANITE_USE_SCRATCH_BUFFERS
		if (!Nanite::GGlobalResources.GetScratchOccludedInstancesBufferRef().IsValid() || Nanite::GGlobalResources.GetScratchOccludedInstancesBufferRef()->Desc.NumElements < NumSceneInstancesPo2)
		{
			GetPooledFreeBuffer(GraphBuilder.RHICmdList, FRDGBufferDesc::CreateStructuredDesc(sizeof(FInstanceDraw), NumSceneInstancesPo2), Nanite::GGlobalResources.GetScratchOccludedInstancesBufferRef(), TEXT("Nanite.OccludedInstances"));
		}
		CullingContext.OccludedInstances		= GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetScratchOccludedInstancesBufferRef(), TEXT("Nanite.OccludedInstances"));
	#else
		CullingContext.OccludedInstances		= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FInstanceDraw), NumSceneInstancesPo2), TEXT("Nanite.OccludedInstances"));
	#endif

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
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	const FGPUSceneParameters &GPUSceneParameters,
	FRDGBufferRef MainCandidateNodesAndClusters,
	FRDGBufferRef PostCandidateNodesAndClusters,
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
		
		PassParameters->OutMainAndPostPassPersistentStates	= GraphBuilder.CreateUAV( CullingContext.MainAndPostPassPersistentStates );

		if (CullingContext.StatsBuffer)
		{
			PassParameters->OutStatsBuffer					= GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
		}

		check( CullingPass == CULLING_PASS_NO_OCCLUSION );
		check( CullingContext.InstanceDrawsBuffer == nullptr );
		PassParameters->OutCandidateNodesAndClusters = GraphBuilder.CreateUAV( MainCandidateNodesAndClusters );
		
		check(CullingContext.ViewsBuffer);

		FInstanceCullVSM_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInstanceCullVSM_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FInstanceCullVSM_CS::FDebugFlagsDim>(CullingContext.DebugFlags != 0);
		PermutationVector.Set<FInstanceCullVSM_CS::FUseCompactedViewsDim>(CVarCompactVSMViews.GetValueOnRenderThread() != 0);

		auto ComputeShader = CullingContext.ShaderMap->GetShader<FInstanceCullVSM_CS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "Main Pass: InstanceCullVSM - No occlusion" ),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(CullingContext.NumInstancesPreCull, 64)
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

		PassParameters->ImposterAtlas = Nanite::GStreamingManager.GetRootPagesSRV();

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
			PassParameters->OutCandidateNodesAndClusters	= GraphBuilder.CreateUAV( MainCandidateNodesAndClusters);
		}
		else if( CullingPass == CULLING_PASS_OCCLUSION_MAIN )
		{
			PassParameters->OutOccludedInstances		= GraphBuilder.CreateUAV( CullingContext.OccludedInstances );
			PassParameters->OutOccludedInstancesArgs	= GraphBuilder.CreateUAV( CullingContext.OccludedInstancesArgs );
			PassParameters->OutCandidateNodesAndClusters	= GraphBuilder.CreateUAV( MainCandidateNodesAndClusters );
		}
		else
		{
			PassParameters->InInstanceDraws				= GraphBuilder.CreateSRV( CullingContext.OccludedInstances );
			PassParameters->InOccludedInstancesArgs		= GraphBuilder.CreateSRV( CullingContext.OccludedInstancesArgs );
			PassParameters->OutCandidateNodesAndClusters	= GraphBuilder.CreateUAV( PostCandidateNodesAndClusters);
		}
		
		check(CullingContext.ViewsBuffer);

		const uint32 InstanceCullingPass = CullingContext.InstanceDrawsBuffer != nullptr ? CULLING_PASS_EXPLICIT_LIST : CullingPass;
		FInstanceCull_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInstanceCull_CS::FCullingPassDim>(InstanceCullingPass);
		PermutationVector.Set<FInstanceCull_CS::FMultiViewDim>(bMultiView);
		PermutationVector.Set<FInstanceCull_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FInstanceCull_CS::FDebugFlagsDim>(CullingContext.DebugFlags != 0);
		PermutationVector.Set<FInstanceCull_CS::FRasterTechniqueDim>(int32(RasterContext.RasterTechnique));

		auto ComputeShader = CullingContext.ShaderMap->GetShader<FInstanceCull_CS>(PermutationVector);
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
				FComputeShaderUtils::GetGroupCount(CullingContext.NumInstancesPreCull, 64)
			);
		}
	}

	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteClusterCull);
		FPersistentClusterCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FPersistentClusterCull_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->CullingParameters		= CullingParameters;
		PassParameters->MaxNodes				= Nanite::FGlobalResources::GetMaxNodes();
		
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
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

		PassParameters->MainAndPostPassPersistentStates	= GraphBuilder.CreateUAV( CullingContext.MainAndPostPassPersistentStates );
		
		if( CullingPass == CULLING_PASS_NO_OCCLUSION || CullingPass == CULLING_PASS_OCCLUSION_MAIN )
		{
			PassParameters->InOutCandidateNodesAndClusters	= GraphBuilder.CreateUAV( MainCandidateNodesAndClusters );
			PassParameters->VisibleClustersArgsSWHW	= GraphBuilder.CreateUAV( CullingContext.MainRasterizeArgsSWHW );
			
			if( CullingPass == CULLING_PASS_OCCLUSION_MAIN )
			{
				PassParameters->OutOccludedNodesAndClusters	= GraphBuilder.CreateUAV( PostCandidateNodesAndClusters );
			}
		}
		else
		{
			PassParameters->InOutCandidateNodesAndClusters	= GraphBuilder.CreateUAV( PostCandidateNodesAndClusters );
			PassParameters->OffsetClustersArgsSWHW	= GraphBuilder.CreateSRV( CullingContext.MainRasterizeArgsSWHW );
			PassParameters->VisibleClustersArgsSWHW	= GraphBuilder.CreateUAV( CullingContext.PostRasterizeArgsSWHW );
		}

		PassParameters->OutVisibleClustersSWHW			= GraphBuilder.CreateUAV( CullingContext.VisibleClustersSWHW );
		PassParameters->OutStreamingRequests			= GraphBuilder.CreateUAV( CullingContext.StreamingRequests );

		if (VirtualShadowMapArray)
		{
			PassParameters->VirtualShadowMap = VirtualTargetParameters;
			PassParameters->OutDynamicCasterFlags = GraphBuilder.CreateUAV(VirtualShadowMapArray->DynamicCasterPageFlagsRDG, PF_R32_UINT);
		}

		if (CullingContext.StatsBuffer)
		{
			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
		}

		PassParameters->LargePageRectThreshold = CVarLargePageRectThreshold.GetValueOnRenderThread();

		check(CullingContext.ViewsBuffer);

		FPersistentClusterCull_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FPersistentClusterCull_CS::FCullingPassDim>(CullingPass);
		PermutationVector.Set<FPersistentClusterCull_CS::FMultiViewDim>(bMultiView);
		PermutationVector.Set<FPersistentClusterCull_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FPersistentClusterCull_CS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVector.Set<FPersistentClusterCull_CS::FClusterPerPageDim>(GNaniteClusterPerPage && VirtualShadowMapArray != nullptr);
		PermutationVector.Set<FPersistentClusterCull_CS::FDebugFlagsDim>(CullingContext.DebugFlags != 0);

		auto ComputeShader = CullingContext.ShaderMap->GetShader<FPersistentClusterCull_CS>(PermutationVector);

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

		auto ComputeShader = CullingContext.ShaderMap->GetShader< FCalculateSafeRasterizerArgs_CS >(PermutationVector);

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
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	FIntVector4 SOAStrides, 
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
	CommonPassParameters->ClusterPageHeaders = GStreamingManager.GetClusterPageHeadersSRV();

	if (ViewsBuffer)
	{
		CommonPassParameters->InViews = GraphBuilder.CreateSRV(ViewsBuffer);
	}

	CommonPassParameters->GPUSceneParameters = GPUSceneParameters;
	CommonPassParameters->RasterParameters = RasterContext.Parameters;
	CommonPassParameters->VisualizeModeBitMask = RasterContext.VisualizeModeBitMask;
	CommonPassParameters->SOAStrides = SOAStrides;
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

	{
		const bool bUsePrimitiveShader = UsePrimitiveShader();

		const bool bUseAutoCullingShader =
			GRHISupportsPrimitiveShaders &&
			!bUsePrimitiveShader &&
			GNaniteAutoShaderCulling != 0;

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

		FHWRasterizePS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FHWRasterizePS::FRasterTechniqueDim>(int32(Technique));
		PermutationVectorPS.Set<FHWRasterizePS::FMultiViewDim>(bMultiView);
		PermutationVectorPS.Set<FHWRasterizePS::FPrimShaderDim>(bUsePrimitiveShader);
		PermutationVectorPS.Set<FHWRasterizePS::FVisualizeDim>(RasterContext.VisualizeActive && Technique != ERasterTechnique::DepthOnly);
		PermutationVectorPS.Set<FHWRasterizePS::FNearClipDim>(bNearClip);
		PermutationVectorPS.Set<FHWRasterizePS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVectorPS.Set<FHWRasterizePS::FClusterPerPageDim>( GNaniteClusterPerPage && VirtualShadowMapArray != nullptr );

		auto VertexShader = RasterContext.ShaderMap->GetShader<FHWRasterizeVS>(PermutationVectorVS);
		auto PixelShader  = RasterContext.ShaderMap->GetShader<FHWRasterizePS>(PermutationVectorPS);

		GraphBuilder.AddPass(
			bMainPass ? RDG_EVENT_NAME("Main Pass: Rasterize") : RDG_EVENT_NAME("Post Pass: Rasterize"),
			RasterPassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
			[VertexShader, PixelShader, RasterPassParameters, ViewRect, bUsePrimitiveShader, bMainPass](FRHICommandListImmediate& RHICmdList)
		{
			FRHIRenderPassInfo RPInfo;
			RPInfo.ResolveParameters.DestRect.X1 = ViewRect.Min.X;
			RPInfo.ResolveParameters.DestRect.Y1 = ViewRect.Min.Y;
			RPInfo.ResolveParameters.DestRect.X2 = ViewRect.Max.X;
			RPInfo.ResolveParameters.DestRect.Y2 = ViewRect.Max.Y;
			RHICmdList.BeginRenderPass(RPInfo, bMainPass ? TEXT("Main Pass: Rasterize") : TEXT("Post Pass: Rasterize"));
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

			SetGraphicsPipelineState( RHICmdList, GraphicsPSOInit );
			
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RasterPassParameters->Common);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *RasterPassParameters);

			RHICmdList.SetStreamSource( 0, nullptr, 0 );
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

		auto ComputeShader = RasterContext.ShaderMap->GetShader<FMicropolyRasterizeCS>(PermutationVectorCS);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			bMainPass ? RDG_EVENT_NAME("Main Pass: Rasterize") : RDG_EVENT_NAME("Post Pass: Rasterize"),
			ComputePassFlags,
			ComputeShader,
			CommonPassParameters,
			CommonPassParameters->IndirectArgs,
			0);
	}
}

FRasterContext InitRasterContext(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	FIntPoint TextureSize,
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

	RasterContext.VisualizeActive = VisualizationData.IsActive();
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

	RasterContext.ShaderMap = GetGlobalShaderMap(FeatureLevel);
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

	RasterContext.DepthBuffer	= ExternalDepthBuffer ? ExternalDepthBuffer :
								  GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Nanite.DepthBuffer32") );
	RasterContext.VisBuffer64	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Nanite.VisBuffer64") );
	RasterContext.DbgBuffer64	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Nanite.DbgBuffer64") );
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


void FPackedView::UpdateLODScales()
{
	const float ViewToPixels = 0.5f * ViewToClip.M[1][1] * ViewSizeAndInvSize.Y;

	const float LODScale = ViewToPixels / GNaniteMaxPixelsPerEdge;
	const float LODScaleHW = ViewToPixels / GNaniteMinPixelsPerEdgeHW;

	LODScales = FVector2D(LODScale, LODScaleHW);
}


static void AllocateCandidateBuffers(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferRef* MainCandidateNodesAndClustersBufferRef, FRDGBufferRef* PostCandidateNodesAndClustersBufferRef)
{
	const uint32 MaxNodes = Nanite::FGlobalResources::GetMaxNodes();
	const uint32 MaxCandidateClusters = Nanite::FGlobalResources::GetMaxCandidateClusters();
	const uint32 MaxCullingBatches = MaxCandidateClusters / PERSISTENT_CLUSTER_CULLING_GROUP_SIZE;
	check(MaxCandidateClusters % PERSISTENT_CLUSTER_CULLING_GROUP_SIZE == 0);
	check(MainCandidateNodesAndClustersBufferRef);

	// Persistent nodes and clusters: Initialize node and cluster array.
	// They only have to be initialized once as the culling code reverts nodes/clusters to their cleared state after they have been consumed.
	{
		TRefCountPtr<FRDGPooledBuffer>& CandidateNodesAndClustersBuffer = Nanite::GGlobalResources.GetMainPassBuffers().CandidateNodesAndClustersBuffer;
		if (CandidateNodesAndClustersBuffer.IsValid())
		{
			*MainCandidateNodesAndClustersBufferRef = GraphBuilder.RegisterExternalBuffer(CandidateNodesAndClustersBuffer, TEXT("Nanite.MainPass.CandidateNodesAndClustersBuffer"));
		}
		else
		{
			FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(4, MaxCullingBatches + MaxCandidateClusters * 2 + MaxNodes * 2);
			Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_ByteAddressBuffer);
			*MainCandidateNodesAndClustersBufferRef = GraphBuilder.CreateBuffer(Desc, TEXT("Nanite.MainPass.CandidateNodesAndClustersBuffer"));
			AddPassInitCandidateNodesAndClustersUAV(GraphBuilder, ShaderMap, GraphBuilder.CreateUAV(*MainCandidateNodesAndClustersBufferRef), false);
			CandidateNodesAndClustersBuffer = GraphBuilder.ConvertToExternalBuffer(*MainCandidateNodesAndClustersBufferRef);
		}
	}

	if (PostCandidateNodesAndClustersBufferRef)
	{
		TRefCountPtr<FRDGPooledBuffer>& CandidateNodesAndClustersBuffer = Nanite::GGlobalResources.GetPostPassBuffers().CandidateNodesAndClustersBuffer;
		if (CandidateNodesAndClustersBuffer.IsValid())
		{
			*PostCandidateNodesAndClustersBufferRef = GraphBuilder.RegisterExternalBuffer(CandidateNodesAndClustersBuffer, TEXT("Nanite.PostPass.CandidateNodesAndClustersBuffer"));
		}
		else
		{
			FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(4, MaxCullingBatches + MaxCandidateClusters * 2 + MaxNodes * 3);
			Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_ByteAddressBuffer);
			*PostCandidateNodesAndClustersBufferRef = GraphBuilder.CreateBuffer(Desc, TEXT("Nanite.PostPass.CandidateNodesAndClustersBuffer"));
			AddPassInitCandidateNodesAndClustersUAV(GraphBuilder, ShaderMap, GraphBuilder.CreateUAV(*PostCandidateNodesAndClustersBufferRef), true);
			CandidateNodesAndClustersBuffer = GraphBuilder.ConvertToExternalBuffer(*PostCandidateNodesAndClustersBufferRef);
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

		for (int32 i = 0; i < RangeNumPrimaryViews; i++)
		{
			const Nanite::FPackedView& PrimaryView = Views[RangeStartPrimaryView + i];
			const int32 NumMips = PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z;

			for (int32 j = 0; j < NumMips; j++)
			{
				RangeViews[j * RangeNumPrimaryViews + i] = Views[j * NumPrimaryViews + (RangeStartPrimaryView + i)];
			}
		}

		CullRasterize(GraphBuilder, Scene, RangeViews, RangeNumPrimaryViews, CullingContext, RasterContext, RasterState, OptionalInstanceDraws, VirtualShadowMapArray, bExtractStats);
	}
}

void CullRasterize(
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
	LLM_SCOPE_BYTAG(Nanite);
	
	// Split rasterization into multiple passes if there are too many views. Only possible for depth-only rendering.
	if (Views.Num() > MAX_VIEWS_PER_CULL_RASTERIZE_PASS)
	{
		check(RasterContext.RasterTechnique == ERasterTechnique::DepthOnly);
		CullRasterizeMultiPass(GraphBuilder, Scene, Views, NumPrimaryViews, CullingContext, RasterContext, RasterState, OptionalInstanceDraws, VirtualShadowMapArray, bExtractStats);
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::CullRasterize");

	AddPassIfDebug(GraphBuilder, [](FRHICommandList&)
	{
		check(!Nanite::GStreamingManager.IsAsyncUpdateInProgress());
	});

	// Calling CullRasterize more than once on a CullingContext is illegal unless bSupportsMultiplePasses is enabled.
	check(CullingContext.DrawPassIndex == 0 || CullingContext.bSupportsMultiplePasses);

	//check(Views.Num() == 1 || !CullingContext.PrevHZB);	// HZB not supported with multi-view, yet
	ensure(Views.Num() > 0 && Views.Num() <= MAX_VIEWS_PER_CULL_RASTERIZE_PASS);

	FRDGBufferUploader BufferUploader;

	{
		const uint32 ViewsBufferElements = FMath::RoundUpToPowerOfTwo(Views.Num());
		CullingContext.ViewsBuffer = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Nanite.Views"), Views.GetTypeSize(), ViewsBufferElements, Views.GetData(), Views.Num() * Views.GetTypeSize());
	}

	if (OptionalInstanceDraws)
	{
		const uint32 InstanceDrawsBufferElements = FMath::RoundUpToPowerOfTwo(OptionalInstanceDraws->Num());
		CullingContext.InstanceDrawsBuffer = CreateStructuredBuffer
		(
			GraphBuilder,
			BufferUploader,
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
		CullingContext.NumInstancesPreCull = Scene.GPUScene.InstanceDataAllocator.GetMaxSize();
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

		CullingContext.StatsBuffer = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Nanite.StatsBuffer"), sizeof(FNaniteStats), 1, &Stats, sizeof(FNaniteStats));
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
		CullingParameters.HZBSize		= CullingContext.PrevHZB ? CullingContext.PrevHZB->GetDesc().Extent : FVector2D(0.0f);
		CullingParameters.HZBSampler	= TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();
		CullingParameters.SOAStrides	= CullingContext.SOAStrides;
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
	GPUSceneParameters.GPUSceneInstanceSceneData = Scene.GPUScene.InstanceDataBuffer.SRV;
	GPUSceneParameters.GPUScenePrimitiveSceneData = Scene.GPUScene.PrimitiveBuffer.SRV;
	GPUSceneParameters.GPUSceneFrameNumber = Scene.GPUScene.GetSceneFrameNumber();
	
	if (VirtualShadowMapArray && CVarCompactVSMViews.GetValueOnRenderThread() != 0)
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteInstanceCullVSM);

		// Compact the views to remove needless (empty) mip views - need to do on GPU as that is where we know what mips have pages.
		const uint32 ViewsBufferElements = FMath::RoundUpToPowerOfTwo(Views.Num());
		FRDGBufferRef CompactedViews = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FPackedView), ViewsBufferElements), TEXT("Shadow.Virtual.CompactedViews"));
		FRDGBufferRef CompactedViewInfo = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FCompactedViewInfo), Views.Num()), TEXT("Shadow.Virtual.CompactedViewInfo"));
		// just an atomic counter that needs to be zero
		// NOTE: must be static since we're passing a reference to RDG
		const static uint32 TheZeros[2] = { 0U, 0U };
		FRDGBufferRef CompactedViewsAllocation = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.CompactedViewsAllocation"), sizeof(uint32), 2, TheZeros, sizeof(TheZeros), ERDGInitialDataFlags::NoCopy);
		BufferUploader.Submit(GraphBuilder);

		{
			FCompactViewsVSM_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCompactViewsVSM_CS::FParameters >();

			PassParameters->GPUSceneParameters = GPUSceneParameters;
			PassParameters->CullingParameters = CullingParameters;
			PassParameters->VirtualShadowMap = VirtualTargetParameters;


			PassParameters->CompactedViewsOut = GraphBuilder.CreateUAV(CompactedViews);
			PassParameters->CompactedViewInfoOut = GraphBuilder.CreateUAV(CompactedViewInfo);
			PassParameters->CompactedViewsAllocationOut = GraphBuilder.CreateUAV(CompactedViewsAllocation);

			check(CullingContext.ViewsBuffer);
			auto ComputeShader = CullingContext.ShaderMap->GetShader<FCompactViewsVSM_CS>();

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

	BufferUploader.Submit(GraphBuilder);

	{
		FInitArgs_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitArgs_CS::FParameters >();

		PassParameters->RenderFlags = CullingParameters.RenderFlags;

		PassParameters->OutMainAndPostPassPersistentStates	= GraphBuilder.CreateUAV( CullingContext.MainAndPostPassPersistentStates );
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
			PassParameters->InOutTotalPrevDrawClusters = PassParameters->OutMainAndPostPassPersistentStates;
		}

		FInitArgs_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInitArgs_CS::FOcclusionCullingDim>( CullingContext.bTwoPassOcclusion );
		PermutationVector.Set<FInitArgs_CS::FDrawPassIndexDim>( ClampedDrawPassIndex );
		
		auto ComputeShader = CullingContext.ShaderMap->GetShader< FInitArgs_CS >( PermutationVector );

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "InitArgs" ),
			ComputeShader,
			PassParameters,
			FIntVector( 1, 1, 1 )
		);
	}

	// Allocate candidate buffers. Lifetime only duration of CullRasterize.
	FRDGBufferRef MainCandidateNodesAndClustersBuffer = nullptr;
	FRDGBufferRef PostCandidateNodesAndClustersBuffer = nullptr;
	AllocateCandidateBuffers(GraphBuilder, CullingContext.ShaderMap, &MainCandidateNodesAndClustersBuffer, CullingContext.bTwoPassOcclusion ? &PostCandidateNodesAndClustersBuffer : nullptr);

	// No Occlusion Pass / Occlusion Main Pass
	AddPass_InstanceHierarchyAndClusterCull(
		GraphBuilder,
		Scene,
		CullingParameters,
		Views,
		NumPrimaryViews,
		CullingContext,
		RasterContext,
		RasterState,
		GPUSceneParameters,
		MainCandidateNodesAndClustersBuffer,
		PostCandidateNodesAndClustersBuffer,
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
			CullingContext,
			RasterContext,
			RasterState,
			GPUSceneParameters,
			MainCandidateNodesAndClustersBuffer,
			PostCandidateNodesAndClustersBuffer,
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
	CullRasterize(
		GraphBuilder,
		Scene,
		Views,
		Views.Num(),
		CullingContext,
		RasterContext,
		RasterState,
		OptionalInstanceDraws,
		nullptr,
		bExtractStats
	);
}

void ExtractStats(
	FRDGBuilder& GraphBuilder,
	const FCullingContext& CullingContext,
	bool bVirtualTextureTarget
)
{
	LLM_SCOPE_BYTAG(Nanite);

	if (CullingContext.DebugFlags != 0 && GNaniteShowStats != 0 && CullingContext.StatsBuffer != nullptr)
	{
		FRDGBufferRef ClusterStatsArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("Nanite.ClusterStatsArgs"));

		{
			FCalculateStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCalculateStatsCS::FParameters>();

			PassParameters->RenderFlags = CullingContext.RenderFlags;

			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(CullingContext.StatsBuffer);
			PassParameters->OutClusterStatsArgs = GraphBuilder.CreateUAV(ClusterStatsArgs);

			PassParameters->MainAndPostPassPersistentStates = GraphBuilder.CreateSRV(CullingContext.MainAndPostPassPersistentStates);
			PassParameters->MainPassRasterizeArgsSWHW = GraphBuilder.CreateSRV(CullingContext.MainRasterizeArgsSWHW);

			if( CullingContext.bTwoPassOcclusion )
			{
				check(CullingContext.PostRasterizeArgsSWHW);
				PassParameters->PostPassRasterizeArgsSWHW = GraphBuilder.CreateSRV(CullingContext.PostRasterizeArgsSWHW);
			}
			
			FCalculateStatsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FCalculateStatsCS::FTwoPassCullingDim>( CullingContext.bTwoPassOcclusion );
			auto ComputeShader = CullingContext.ShaderMap->GetShader<FCalculateStatsCS>( PermutationVector );

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CalculateStatsArgs"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1)
			);
		}

		{
			FCalculateClusterStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCalculateClusterStatsCS::FParameters>();

			PassParameters->SOAStrides = CullingContext.SOAStrides;
			PassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
			PassParameters->RenderFlags = CullingContext.RenderFlags;

			PassParameters->ClusterPageData = GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders = GStreamingManager.GetClusterPageHeadersSRV();
			PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(CullingContext.StatsBuffer);

			PassParameters->MainPassRasterizeArgsSWHW = GraphBuilder.CreateSRV(CullingContext.MainRasterizeArgsSWHW);
			if( CullingContext.bTwoPassOcclusion )
			{
				check(CullingContext.PostRasterizeArgsSWHW != nullptr);
				PassParameters->PostPassRasterizeArgsSWHW = GraphBuilder.CreateSRV( CullingContext.PostRasterizeArgsSWHW );
			}
			PassParameters->StatsArgs = ClusterStatsArgs;

			FCalculateClusterStatsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FCalculateClusterStatsCS::FTwoPassCullingDim>( CullingContext.bTwoPassOcclusion );
			PermutationVector.Set<FCalculateClusterStatsCS::FVirtualTextureTargetDim>(bVirtualTextureTarget);
			auto ComputeShader = CullingContext.ShaderMap->GetShader<FCalculateClusterStatsCS>( PermutationVector );

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CalculateStats"),
				ComputeShader,
				PassParameters,
				ClusterStatsArgs,
				0
			);
		}

		// Extract main pass buffers
		{
			auto& MainPassBuffers = Nanite::GGlobalResources.GetMainPassBuffers();
			MainPassBuffers.StatsRasterizeArgsSWHWBuffer = GraphBuilder.ConvertToExternalBuffer(CullingContext.MainRasterizeArgsSWHW);
		}

		// Extract post pass buffers
		auto& PostPassBuffers = Nanite::GGlobalResources.GetPostPassBuffers();
		PostPassBuffers.StatsRasterizeArgsSWHWBuffer = nullptr;
		if( CullingContext.bTwoPassOcclusion )
		{
			check( CullingContext.PostRasterizeArgsSWHW != nullptr );
			PostPassBuffers.StatsRasterizeArgsSWHWBuffer = GraphBuilder.ConvertToExternalBuffer(CullingContext.PostRasterizeArgsSWHW);
		}

		// Extract calculated stats (so VisibleClustersSWHW isn't needed later)
		{
			Nanite::GGlobalResources.GetStatsBufferRef() = GraphBuilder.ConvertToExternalBuffer(CullingContext.StatsBuffer);
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
	LLM_SCOPE_BYTAG(Nanite);

	// Print stats
	if (GNaniteShowStats != 0 && Nanite::GGlobalResources.GetStatsBufferRef())
	{
		auto& MainPassBuffers = Nanite::GGlobalResources.GetMainPassBuffers();
		auto& PostPassBuffers = Nanite::GGlobalResources.GetPostPassBuffers();

		// Shader compilers have a hard time handling the size of the full PrintStats shader, so we split it into multiple passes.
		// This reduces the FXC compilation time from 2-3 minutes to just a few seconds.
		for (uint32 Pass = 0; Pass < NUM_PRINT_STATS_PASSES; Pass++)
		{
			FPrintStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrintStatsCS::FParameters>();

			ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintStruct);
			PassParameters->PackedClusterSize = sizeof(Nanite::FPackedCluster);

			PassParameters->RenderFlags = Nanite::GGlobalResources.StatsRenderFlags;
			PassParameters->DebugFlags = Nanite::GGlobalResources.StatsDebugFlags;

			PassParameters->InStatsBuffer = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetStatsBufferRef()));

			PassParameters->MainPassRasterizeArgsSWHW = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(MainPassBuffers.StatsRasterizeArgsSWHWBuffer));
			
			const bool bTwoPass = (PostPassBuffers.StatsRasterizeArgsSWHWBuffer != nullptr);
			if( bTwoPass )
			{
				PassParameters->PostPassRasterizeArgsSWHW = GraphBuilder.CreateSRV( GraphBuilder.RegisterExternalBuffer( PostPassBuffers.StatsRasterizeArgsSWHWBuffer ) );
			}

			FPrintStatsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FPrintStatsCS::FTwoPassCullingDim>(bTwoPass);
			PermutationVector.Set<FPrintStatsCS::FPassDim>(Pass);
			auto ComputeShader = View.ShaderMap->GetShader<FPrintStatsCS>(PermutationVector);

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
	FRasterResults& RasterResults
)
{
	LLM_SCOPE_BYTAG(Nanite);

	RasterResults.SOAStrides 			= CullingContext.SOAStrides;
	RasterResults.MaxVisibleClusters	= Nanite::FGlobalResources::GetMaxVisibleClusters();
	RasterResults.MaxNodes				= Nanite::FGlobalResources::GetMaxNodes();
	RasterResults.RenderFlags			= CullingContext.RenderFlags;

	RasterResults.ViewsBuffer			= CullingContext.ViewsBuffer;
	RasterResults.VisibleClustersSWHW	= CullingContext.VisibleClustersSWHW;
	RasterResults.VisBuffer64			= RasterContext.VisBuffer64;
	
	if (RasterContext.VisualizeActive)
	{
		RasterResults.DbgBuffer64 = RasterContext.DbgBuffer64;
		RasterResults.DbgBuffer32 = RasterContext.DbgBuffer32;
	}
}

void DrawHitProxies(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View, 
	const FRasterResults& RasterResults,
	FRDGTextureRef HitProxyTexture,
	FRDGTextureRef HitProxyDepthTexture
	)
{
#if WITH_EDITOR
	LLM_SCOPE_BYTAG(Nanite);

	RDG_EVENT_SCOPE(GraphBuilder, "NaniteHitProxyPass");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteEditor);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	FRDGTextureRef VisBuffer64 = RasterResults.VisBuffer64 ? RasterResults.VisBuffer64 : SystemTextures.Black;

	FRDGBufferRef VisibleClustersSWHW = RasterResults.VisibleClustersSWHW;

	{
		auto* PassParameters = GraphBuilder.AllocParameters<FEmitHitProxyIdPS::FParameters>();

		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->SOAStrides = RasterResults.SOAStrides;
		PassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->MaterialHitProxyTable = Scene.MaterialTables[ENaniteMeshPass::BasePass].GetHitProxyTableSRV();

		PassParameters->RenderTargets[0]			= FRenderTargetBinding(HitProxyTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(HitProxyDepthTexture, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

		auto PixelShader = View.ShaderMap->GetShader<FEmitHitProxyIdPS>();

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			View.ShaderMap,
			RDG_EVENT_NAME("Emit HitProxy Id"),
			PixelShader,
			PassParameters,
			View.ViewRect,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI()
			);
	}
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
	LLM_SCOPE_BYTAG(Nanite);

	auto* PassParameters = GraphBuilder.AllocParameters< FEmitShadowMapPS::FParameters >();

	PassParameters->SourceOffset = SourceRect.Min - DestOrigin;
	PassParameters->ViewToClip22 = ProjectionMatrix.M[2][2];
	PassParameters->DepthBias = DepthBias;
	
	PassParameters->DepthBuffer = RasterContext.DepthBuffer;
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding( DepthBuffer, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop );

	FEmitShadowMapPS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FEmitShadowMapPS::FDepthOutputTypeDim >( bOrtho ? 1 : 2 );

	auto PixelShader = RasterContext.ShaderMap->GetShader< FEmitShadowMapPS >( PermutationVector );

	FIntRect DestRect;
	DestRect.Min = DestOrigin;
	DestRect.Max = DestRect.Min + SourceRect.Max - SourceRect.Min;
	
	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		RasterContext.ShaderMap,
		RDG_EVENT_NAME("EmitShadowMap"),
		PixelShader,
		PassParameters,
		DestRect,
		nullptr,
		nullptr,
		TStaticDepthStencilState<true, CF_LessEqual>::GetRHI()
		);
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
	LLM_SCOPE_BYTAG(Nanite);

	FEmitCubemapShadowVS::FPermutationDomain VertexPermutationVector;
	VertexPermutationVector.Set<FEmitCubemapShadowVS::FUseGeometryShader>(bUseGeometryShader);
	TShaderMapRef<FEmitCubemapShadowVS> VertexShader(RasterContext.ShaderMap, VertexPermutationVector);
	TShaderRef<FEmitCubemapShadowGS> GeometryShader;
	TShaderMapRef<FEmitCubemapShadowPS> PixelShader(RasterContext.ShaderMap);

	// VS output of RT array index on D3D11 requires a caps bit. Use GS fallback if set.
	if (bUseGeometryShader)
	{
		GeometryShader = TShaderMapRef<FEmitCubemapShadowGS>(RasterContext.ShaderMap);
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

BEGIN_SHADER_PARAMETER_STRUCT(FDummyDepthDecompressParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
END_SHADER_PARAMETER_STRUCT()

void EmitDepthTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntVector4& SOAStrides,
	FRDGBufferRef VisibleClustersSWHW,
	FRDGBufferRef ViewsBuffer,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBuffer64,
	FRDGTextureRef& OutMaterialDepth,
	FRDGTextureRef& OutNaniteMask,
	FRDGTextureRef& OutVelocityBuffer,
	bool bPrePass
)
{
	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::EmitDepthTargets");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteDepth);

	const EShaderPlatform ShaderPlatform = View.GetShaderPlatform();
	const FIntPoint SceneTexturesExtent = GetSceneTextureExtent();	
	const FClearValueBinding DefaultDepthStencil = GetSceneDepthClearValue();

	float DefaultDepth = 0.0f;
	uint32 DefaultStencil = 0;
	DefaultDepthStencil.GetDepthStencil(DefaultDepth, DefaultStencil);

	const uint32 StencilDecalMask = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);

	// Nanite mask (TODO: unpacked right now, 7bits wasted per pixel).
	FRDGTextureDesc NaniteMaskDesc = FRDGTextureDesc::Create2D(
		SceneTexturesExtent,
		PF_R8_UINT,
		FClearValueBinding::Transparent,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureDesc VelocityBufferDesc = FVelocityRendering::GetRenderTargetDesc(ShaderPlatform, SceneTexturesExtent);

	// TODO: Can be 16bit UNORM (PF_ShadowDepth) (32bit float w/ 8bit stencil is a waste of bandwidth and memory)
	FRDGTextureDesc MaterialDepthDesc = FRDGTextureDesc::Create2D(
		SceneTexturesExtent,
		PF_DepthStencil,
		DefaultDepthStencil,
		TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead | (UseComputeDepthExport() ? TexCreate_UAV : TexCreate_None));

	FRDGTextureRef NaniteMask		= GraphBuilder.CreateTexture(NaniteMaskDesc, TEXT("Nanite.Mask"));
	FRDGTextureRef VelocityBuffer	= GraphBuilder.CreateTexture(VelocityBufferDesc, TEXT("Nanite.Velocity"));
	FRDGTextureRef MaterialDepth	= GraphBuilder.CreateTexture(MaterialDepthDesc, TEXT("Nanite.MaterialDepth"));

	if (UseComputeDepthExport())
	{
		// Emit depth, stencil, and velocity mask

		{
			// HACK: Dummy pass to force depth decompression. Depth export shader needs to be refactored to handle already-compressed surfaces.
			FDummyDepthDecompressParameters* DummyParams = GraphBuilder.AllocParameters<FDummyDepthDecompressParameters>();
			DummyParams->SceneDepth = SceneDepth;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("DummyDepthDecompress"),
				DummyParams,
				ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
				[](FRHICommandList&) {}
			);
		}

		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max, 8); // Only run DepthExport shader on viewport. We have already asserted that ViewRect.Min=0.
		const uint32 PlatformConfig = RHIGetHTilePlatformConfig(SceneTexturesExtent.X, SceneTexturesExtent.Y);

		FRDGTextureUAVRef SceneDepthUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef SceneStencilUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::Stencil));
		FRDGTextureUAVRef SceneHTileUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef MaterialDepthUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef MaterialHTileUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef VelocityUAV		= GraphBuilder.CreateUAV(VelocityBuffer);
		FRDGTextureUAVRef NaniteMaskUAV		= GraphBuilder.CreateUAV(NaniteMask);

		FDepthExportCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDepthExportCS::FParameters>();

		PassParameters->View					= View.ViewUniformBuffer;
		PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->SOAStrides				= SOAStrides;
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->DepthExportConfig		= FIntVector4(PlatformConfig, SceneTexturesExtent.X, StencilDecalMask, 0);
		PassParameters->VisBuffer64				= VisBuffer64;
		PassParameters->Velocity				= VelocityUAV;
		PassParameters->NaniteMask				= NaniteMaskUAV;
		PassParameters->SceneHTile				= SceneHTileUAV;
		PassParameters->SceneDepth				= SceneDepthUAV;
		PassParameters->SceneStencil			= SceneStencilUAV;
		PassParameters->MaterialHTile			= MaterialHTileUAV;
		PassParameters->MaterialDepth			= MaterialDepthUAV;
		PassParameters->MaterialDepthTable		= Scene.MaterialTables[ENaniteMeshPass::BasePass].GetDepthTableSRV();

		auto ComputeShader = View.ShaderMap->GetShader<FDepthExportCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DepthExport"),
			ComputeShader,
			PassParameters,
			DispatchDim
		);
	}
	else
	{
		if (GRHISupportsStencilRefFromPixelShader)
		{
			// Emit scene depth, stencil, and velocity mask

			auto  PixelShader		= View.ShaderMap->GetShader<FEmitSceneDepthStencilPS>();
			auto* PassParameters	= GraphBuilder.AllocParameters<FEmitSceneDepthStencilPS::FParameters>();

			PassParameters->View						= View.ViewUniformBuffer;
			PassParameters->InViews						= GraphBuilder.CreateSRV(ViewsBuffer);
			PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->SOAStrides					= SOAStrides;
			PassParameters->StencilClear				= DefaultStencil;
			PassParameters->StencilDecal				= StencilDecalMask;
			PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders			= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
			PassParameters->VisBuffer64					= VisBuffer64;
			PassParameters->RenderTargets[0]			= FRenderTargetBinding(
				NaniteMask,
				ERenderTargetLoadAction::EClear
			);
			PassParameters->RenderTargets[1]			= FRenderTargetBinding(
				VelocityBuffer,
				ERenderTargetLoadAction::EClear
			);
			PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding
			(
				SceneDepth,
				ERenderTargetLoadAction::ELoad,
				FExclusiveDepthStencil::DepthWrite_StencilWrite
			);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("Emit Scene Depth/Stencil/Velocity"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI()
			);
		}
		else
		{
			// Emit scene depth buffer and velocity mask
			{
				FEmitSceneDepthPS::FPermutationDomain PermutationVectorPS;
				PermutationVectorPS.Set<FEmitSceneDepthPS::FVelocityExportDim>(true);
				auto  PixelShader		= View.ShaderMap->GetShader<FEmitSceneDepthPS>(PermutationVectorPS);
				auto* PassParameters	= GraphBuilder.AllocParameters<FEmitSceneDepthPS::FParameters>();

				PassParameters->View						= View.ViewUniformBuffer;
				PassParameters->InViews						= GraphBuilder.CreateSRV(ViewsBuffer);
				PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
				PassParameters->SOAStrides					= SOAStrides;
				PassParameters->VisBuffer64					= VisBuffer64;
				PassParameters->RenderTargets[0]			= FRenderTargetBinding(
					NaniteMask,
					ERenderTargetLoadAction::EClear
				);
				PassParameters->RenderTargets[1]			= FRenderTargetBinding(
					VelocityBuffer,
					ERenderTargetLoadAction::EClear
				);
				PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding
				(
					SceneDepth,
					ERenderTargetLoadAction::ELoad,
					FExclusiveDepthStencil::DepthWrite_StencilWrite
				);

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("Emit Scene Depth"),
					PixelShader,
					PassParameters,
					View.ViewRect,
					TStaticBlendState<>::GetRHI(),
					TStaticRasterizerState<>::GetRHI(),
					TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI()
				);
			}

			// Emit scene stencil
			{
				auto  PixelShader		= View.ShaderMap->GetShader<FEmitSceneStencilPS>();
				auto* PassParameters	= GraphBuilder.AllocParameters<FEmitSceneStencilPS::FParameters>();

				PassParameters->View						= View.ViewUniformBuffer;
				PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
				PassParameters->SOAStrides					= SOAStrides;
				PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV();
				PassParameters->ClusterPageHeaders			= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
				PassParameters->NaniteMask					= NaniteMask;
				PassParameters->VisBuffer64					= VisBuffer64;
				PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding
				(
					SceneDepth,
					ERenderTargetLoadAction::ELoad,
					FExclusiveDepthStencil::DepthWrite_StencilWrite
				);

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("Emit Scene Stencil/Velocity"),
					PixelShader,
					PassParameters,
					View.ViewRect,
					TStaticBlendState<>::GetRHI(),
					TStaticRasterizerState<>::GetRHI(),
					TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
					StencilDecalMask | GET_STENCIL_BIT_MASK(DISTANCE_FIELD_REPRESENTATION, 1)
				);
			}
		}

		// Emit material depth (and stencil mask) for pixels produced from Nanite rasterization.
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FEmitMaterialDepthPS::FParameters>();

			PassParameters->DummyZero = 0u;
			PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders			= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
			PassParameters->MaterialDepthTable			= Scene.MaterialTables[ENaniteMeshPass::BasePass].GetDepthTableSRV();
			PassParameters->SOAStrides					= SOAStrides;
			PassParameters->View						= View.ViewUniformBuffer;
			PassParameters->NaniteMask					= NaniteMask;
			PassParameters->VisBuffer64					= VisBuffer64;
			PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(
				MaterialDepth,
				ERenderTargetLoadAction::EClear,
				FExclusiveDepthStencil::DepthWrite_StencilWrite
			);

			FEmitMaterialDepthPS::FPermutationDomain PermutationVectorPS;
			PermutationVectorPS.Set<FEmitMaterialDepthPS::FNaniteMaskDim>(true /* using Nanite mask */);
			auto PixelShader = View.ShaderMap->GetShader<FEmitMaterialDepthPS>(PermutationVectorPS);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("Emit Material Depth"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
			#if NANITE_MATERIAL_STENCIL
				TStaticDepthStencilState<true, CF_Always, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
				STENCIL_SANDBOX_MASK
			#else
				TStaticDepthStencilState<true, CF_Always>::GetRHI()
			#endif
			);
		}

		if (GRHISupportsResummarizeHTile && GNaniteResummarizeHTile != 0)
		{
			// Resummarize HTile meta data if the RHI supports it
			AddResummarizeHTilePass(GraphBuilder, SceneDepth);
		}
	}

	OutNaniteMask = NaniteMask;
	OutVelocityBuffer = VelocityBuffer;
	OutMaterialDepth = MaterialDepth;
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

			FGraphicsPipelineStateInitializer GraphicsPSOInit = MeshPipelineState.AsGraphicsPipelineStateInitializer();
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			FGraphicsPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateGraphicsPipelineState(RHICmdList, GraphicsPSOInit, EApplyRendertargetOption::DoNothing);
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

static void SubmitNaniteMaterialPassCommand(
	const FMeshDrawCommand& MeshDrawCommand,
	const float MaterialDepth,
	const TShaderRef<FNaniteMaterialVS>& NaniteVertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FMeshDrawCommandStateCache& StateCache,
	uint32 InstanceBaseOffset = 0)
{
#if WANTS_DRAW_MESH_EVENTS
	FMeshDrawCommand::FMeshDrawEvent MeshEvent(MeshDrawCommand, InstanceFactor, RHICmdList);
#endif
	FMeshDrawCommand::SubmitDrawBegin(MeshDrawCommand, GraphicsMinimalPipelineStateSet, nullptr, 0, InstanceFactor, RHICmdList, StateCache);

	// All Nanite mesh draw commands are using the same vertex shader, which has a material depth parameter we assign at render time.
	{
		FNaniteMaterialVS::FParameters Parameters;
		Parameters.MaterialDepth = MaterialDepth;
		Parameters.InstanceBaseOffset = InstanceBaseOffset;
		SetShaderParameters(RHICmdList, NaniteVertexShader, NaniteVertexShader.GetVertexShader(), Parameters);
	}

	FMeshDrawCommand::SubmitDrawEnd(MeshDrawCommand, InstanceFactor, RHICmdList);
}

static void SubmitNaniteMaterialPassCommand(
	const FNaniteMaterialPassCommand& MaterialPassCommand,
	const TShaderRef<FNaniteMaterialVS>& NaniteVertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FMeshDrawCommandStateCache& StateCache)
{
	SubmitNaniteMaterialPassCommand(
		MaterialPassCommand.MeshDrawCommand,
		MaterialPassCommand.MaterialDepth,
		NaniteVertexShader,
		GraphicsMinimalPipelineStateSet,
		InstanceFactor,
		RHICmdList,
		StateCache);
}

void DrawBasePass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FDBufferTextures& DBufferTextures,
	const FScene& Scene,
	const FViewInfo& View,
	const FRasterResults& RasterResults
)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::BasePass");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteMaterials);

	const int32 ViewWidth		= View.ViewRect.Max.X - View.ViewRect.Min.X;
	const int32 ViewHeight		= View.ViewRect.Max.Y - View.ViewRect.Min.Y;
	const FIntPoint ViewSize	= FIntPoint(ViewWidth, ViewHeight);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	FRenderTargetBindingSlots GBufferRenderTargets;
	SceneTextures.GetGBufferRenderTargets(ERenderTargetLoadAction::ELoad, GBufferRenderTargets);

	FRDGTextureRef MaterialDepth	= RasterResults.MaterialDepth ? RasterResults.MaterialDepth : SystemTextures.Black;
	FRDGTextureRef VisBuffer64		= RasterResults.VisBuffer64   ? RasterResults.VisBuffer64   : SystemTextures.Black;
	FRDGTextureRef DbgBuffer64		= RasterResults.DbgBuffer64   ? RasterResults.DbgBuffer64   : SystemTextures.Black;
	FRDGTextureRef DbgBuffer32		= RasterResults.DbgBuffer32   ? RasterResults.DbgBuffer32   : SystemTextures.Black;

	FRDGBufferRef VisibleClustersSWHW	= RasterResults.VisibleClustersSWHW;

	if (!FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(GMaxRHIShaderPlatform) &&
		(GNaniteMaterialCulling == 1 || GNaniteMaterialCulling == 2))
	{
		// Invalid culling method, platform does not support wave operations
		// Default to 64x64 tile grid method instead.
		UE_LOG(LogNanite, Warning, TEXT("r.Nanite.MaterialCulling set to %d which requires wave-ops (not supported on this platform), switching to mode 4"), GNaniteMaterialCulling);
		GNaniteMaterialCulling = 4;
	}

	// Use local copy so we can override without modifying for all views
	int32 NaniteMaterialCulling = GNaniteMaterialCulling;
	if ((NaniteMaterialCulling == 1 || NaniteMaterialCulling == 2) && (View.ViewRect.Min.X != 0 || View.ViewRect.Min.Y != 0))
	{
		NaniteMaterialCulling = 4;

		static bool bLoggedAlready = false;
		if (!bLoggedAlready)
		{
			bLoggedAlready = true;
			UE_LOG(LogNanite, Warning, TEXT("View has non-zero viewport offset, using material culling mode 4 (overrides r.Nanite.MaterialCulling = %d)."), GNaniteMaterialCulling);
		}
	}

	const bool b32BitMaskCulling = (NaniteMaterialCulling == 1 || NaniteMaterialCulling == 2);
	const bool bTileGridCulling  = (NaniteMaterialCulling == 3 || NaniteMaterialCulling == 4);

	const FIntPoint TileGridDim = bTileGridCulling ? FMath::DivideAndRoundUp(ViewSize, { 64, 64 }) : FIntPoint(1, 1);

	FRDGBufferDesc     VisibleMaterialsDesc	= FRDGBufferDesc::CreateStructuredDesc(4, b32BitMaskCulling ? FNaniteCommandInfo::MAX_STATE_BUCKET_ID+1 : 1);
	FRDGBufferRef      VisibleMaterials		= GraphBuilder.CreateBuffer(VisibleMaterialsDesc, TEXT("Nanite.VisibleMaterials"));
	FRDGBufferUAVRef   VisibleMaterialsUAV	= GraphBuilder.CreateUAV(VisibleMaterials);
	FRDGTextureDesc    MaterialRangeDesc	= FRDGTextureDesc::Create2D(TileGridDim, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef     MaterialRange		= GraphBuilder.CreateTexture(MaterialRangeDesc, TEXT("Nanite.MaterialRange"));
	FRDGTextureUAVRef  MaterialRangeUAV		= GraphBuilder.CreateUAV(MaterialRange);
	FRDGTextureSRVDesc MaterialRangeSRVDesc	= FRDGTextureSRVDesc::Create(MaterialRange);
	FRDGTextureSRVRef  MaterialRangeSRV		= GraphBuilder.CreateSRV(MaterialRangeSRVDesc);

	FRDGBufferRef MultiViewIndices = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Nanite.DummyMultiViewIndices"));
	FRDGBufferRef MultiViewRectScaleOffsets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), 1), TEXT("Nanite.DummyMultiViewRectScaleOffsets"));
	FRDGBufferRef ViewsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), 1), TEXT("Nanite.PackedViews"));

	AddClearUAVPass(GraphBuilder, VisibleMaterialsUAV, 0);
	AddClearUAVPass(GraphBuilder, MaterialRangeUAV, { 0u, 1u, 0u, 0u });
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MultiViewIndices), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MultiViewRectScaleOffsets), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ViewsBuffer), 0);

	// Classify materials for tile culling
	// TODO: Run velocity export in here instead of depth pre-pass?
	if (b32BitMaskCulling || bTileGridCulling)
	{
		FClassifyMaterialsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClassifyMaterialsCS::FParameters>();
		PassParameters->View					= View.ViewUniformBuffer;
		PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->SOAStrides				= RasterResults.SOAStrides;
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->VisBuffer64				= VisBuffer64;
		PassParameters->MaterialDepthTable		= Scene.MaterialTables[ENaniteMeshPass::BasePass].GetDepthTableSRV();

		uint32 DispatchGroupSize = 0;

		PassParameters->ViewRect = FIntVector4(View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Max.X, View.ViewRect.Max.Y);
		if (b32BitMaskCulling)
		{
			// TODO: Don't currently support offset views.
			checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));
			DispatchGroupSize = 8;
			PassParameters->VisibleMaterials = VisibleMaterialsUAV;

		}
		else if (bTileGridCulling)
		{
			DispatchGroupSize = 64;
			PassParameters->FetchClamp = View.ViewRect.Max - 1;
			PassParameters->MaterialRange = MaterialRangeUAV;
		}

		const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max - View.ViewRect.Min, DispatchGroupSize);

		FClassifyMaterialsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FClassifyMaterialsCS::FCullingMethodDim>(NaniteMaterialCulling);
		auto ComputeShader = View.ShaderMap->GetShader<FClassifyMaterialsCS>(PermutationVector.ToDimensionValueId());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Classify Materials"),
			ComputeShader,
			PassParameters,
			DispatchDim
		);
	}

	// Emit GBuffer Values
	{
		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->SOAStrides	= RasterResults.SOAStrides;
		PassParameters->MaxVisibleClusters	= RasterResults.MaxVisibleClusters;
		PassParameters->MaxNodes	= RasterResults.MaxNodes;
		PassParameters->RenderFlags	= RasterResults.RenderFlags;
			
		PassParameters->ClusterPageData		= Nanite::GStreamingManager.GetClusterPageDataSRV(); 
		PassParameters->ClusterPageHeaders	= Nanite::GStreamingManager.GetClusterPageHeadersSRV();

		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);

		PassParameters->MaterialRange = MaterialRange;
		PassParameters->VisibleMaterials = GraphBuilder.CreateSRV(VisibleMaterials, PF_R32_UINT);

		PassParameters->MultiViewEnabled = 0;
		PassParameters->MultiViewIndices = GraphBuilder.CreateSRV(MultiViewIndices);
		PassParameters->MultiViewRectScaleOffsets = GraphBuilder.CreateSRV(MultiViewRectScaleOffsets);
		PassParameters->InViews = GraphBuilder.CreateSRV(ViewsBuffer);

		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->DbgBuffer64 = DbgBuffer64;
		PassParameters->DbgBuffer32 = DbgBuffer32;
		PassParameters->RenderTargets = GBufferRenderTargets;

		PassParameters->View = View.ViewUniformBuffer; // To get VTFeedbackBuffer
		PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, View, 0, {}, DBufferTextures, nullptr);

		switch (NaniteMaterialCulling)
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
			PassParameters->GridSize = FMath::DivideAndRoundUp(View.ViewRect.Max - View.ViewRect.Min, { 64, 64 });
			break;

		// Rendering a full screen quad
		default:
			PassParameters->GridSize.X = 1;
			PassParameters->GridSize.Y = 1;
			break;
		}

		const FExclusiveDepthStencil MaterialDepthStencil = UseComputeDepthExport()
			? FExclusiveDepthStencil::DepthWrite_StencilNop
			: FExclusiveDepthStencil::DepthWrite_StencilWrite;

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			MaterialDepth,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			MaterialDepthStencil
		);

		TShaderMapRef<FNaniteMaterialVS> NaniteVertexShader(View.ShaderMap);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Emit GBuffer"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &Scene, NaniteVertexShader, ViewRect = View.ViewRect, NaniteMaterialCulling](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			FNaniteUniformParameters UniformParams;
			UniformParams.SOAStrides = PassParameters->SOAStrides;
			UniformParams.MaxVisibleClusters= PassParameters->MaxVisibleClusters;
			UniformParams.MaxNodes = PassParameters->MaxNodes;
			UniformParams.RenderFlags = PassParameters->RenderFlags;

			UniformParams.MaterialConfig.X = NaniteMaterialCulling;
			UniformParams.MaterialConfig.Y = PassParameters->GridSize.X;
			UniformParams.MaterialConfig.Z = PassParameters->GridSize.Y;
			UniformParams.MaterialConfig.W = 0;

			UniformParams.RectScaleOffset = FVector4(1.0f, 1.0f, 0.0f, 0.0f); // Render a rect that covers the entire screen

			if (NaniteMaterialCulling == 3 || NaniteMaterialCulling == 4)
			{
				FIntPoint ScaledSize = PassParameters->GridSize * 64;
				UniformParams.RectScaleOffset.X = float(ScaledSize.X) / float(ViewRect.Max.X - ViewRect.Min.X);
				UniformParams.RectScaleOffset.Y = float(ScaledSize.Y) / float(ViewRect.Max.Y - ViewRect.Min.Y);
			}

			UniformParams.ClusterPageData = PassParameters->ClusterPageData;
			UniformParams.ClusterPageHeaders = PassParameters->ClusterPageHeaders;
			UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

			UniformParams.MaterialRange = PassParameters->MaterialRange->GetRHI();
			UniformParams.VisibleMaterials = PassParameters->VisibleMaterials->GetRHI();

			UniformParams.MultiViewEnabled = PassParameters->MultiViewEnabled;
			UniformParams.MultiViewIndices = PassParameters->MultiViewIndices;
			UniformParams.MultiViewRectScaleOffsets = PassParameters->MultiViewRectScaleOffsets;
			UniformParams.InViews = PassParameters->InViews;

			UniformParams.VisBuffer64 = PassParameters->VisBuffer64->GetRHI();
			UniformParams.DbgBuffer64 = PassParameters->DbgBuffer64->GetRHI();
			UniformParams.DbgBuffer32 = PassParameters->DbgBuffer32->GetRHI();
			const_cast<FScene&>(Scene).UniformBuffers.NaniteUniformBuffer.UpdateUniformBufferImmediate(UniformParams);

			FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;

			TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator> NaniteMaterialPassCommands;
			BuildNaniteMaterialPassCommands(RHICmdList, Scene.NaniteDrawCommands[ENaniteMeshPass::BasePass], NaniteMaterialPassCommands);

			FMeshDrawCommandStateCache StateCache;

			const uint32 TileCount = UniformParams.MaterialConfig.Y * UniformParams.MaterialConfig.Z; // (W * H)
			for (auto CommandsIt = NaniteMaterialPassCommands.CreateConstIterator(); CommandsIt; ++CommandsIt)
			{
				SubmitNaniteMaterialPassCommand(*CommandsIt, NaniteVertexShader, GraphicsMinimalPipelineStateSet, TileCount, RHICmdList, StateCache);
			}
		});
	}
}

#if 0 // TODO: NANITE_VIEW_MODES: Reimplement HTILE
void DrawVisualization(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	const FScene& Scene,
	const FViewInfo& View,
	const FRasterResults& RasterResults
)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "NaniteVisualization");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteDebug);

	const int32 ViewWidth    = View.ViewRect.Max.X - View.ViewRect.Min.X;
	const int32 ViewHeight   = View.ViewRect.Max.Y - View.ViewRect.Min.Y;
	const FIntPoint ViewSize = FIntPoint(ViewWidth, ViewHeight);

	// Visualize Debug Views
	if (ShouldExportDebugBuffers())
	{
		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

		FRDGTextureRef VisBuffer64		= RasterResults.VisBuffer64 ? RasterResults.VisBuffer64 : SystemTextures.Black;
		FRDGTextureRef DbgBuffer64		= RasterResults.DbgBuffer64 ? RasterResults.DbgBuffer64 : SystemTextures.Black;
		FRDGTextureRef DbgBuffer32		= RasterResults.DbgBuffer32 ? RasterResults.DbgBuffer32 : SystemTextures.Black;
		FRDGTextureRef NaniteMask		= RasterResults.NaniteMask ? RasterResults.NaniteMask : SystemTextures.Black;
		FRDGTextureRef VelocityBuffer	= RasterResults.VelocityBuffer ? RasterResults.VelocityBuffer : SystemTextures.Black;

		FRDGBufferRef VisibleClustersSWHW = RasterResults.VisibleClustersSWHW;

		FRDGTextureDesc VisualizeOutputDesc = FRDGTextureDesc::Create2D(
			View.ViewRect.Max,
			PF_A32B32G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef DebugOutput = GraphBuilder.CreateTexture(VisualizeOutputDesc, TEXT("Nanite.Visualize"));

		FNaniteVisualizeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteVisualizeCS::FParameters>();

		PassParameters->View					= View.ViewUniformBuffer;
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->VisualizeConfig			= GetVisualizeConfig();
		PassParameters->SOAStrides				= RasterResults.SOAStrides;
		PassParameters->MaxVisibleClusters		= RasterResults.MaxVisibleClusters;
		PassParameters->RenderFlags				= RasterResults.RenderFlags;
		PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->VisBuffer64				= VisBuffer64;
		PassParameters->DbgBuffer64				= DbgBuffer64;
		PassParameters->DbgBuffer32				= DbgBuffer32;
		PassParameters->NaniteMask				= NaniteMask;
		PassParameters->SceneDepth				= SceneDepth;
		PassParameters->MaterialDepthTable		= Scene.MaterialTables[ENaniteMeshPass::BasePass].GetDepthTableSRV();
#if WITH_EDITOR
		PassParameters->MaterialHitProxyTable	= Scene.MaterialTables[ENaniteMeshPass::BasePass].GetHitProxyTableSRV();
#else
		// TODO: Permutation with hit proxy support to keep this clean?
		// For now, bind a valid SRV
		PassParameters->MaterialHitProxyTable	= Scene.MaterialTables[ENaniteMeshPass::BasePass].GetDepthTableSRV();
#endif
		PassParameters->DebugOutput				= GraphBuilder.CreateUAV(DebugOutput);

		auto ComputeShader = View.ShaderMap->GetShader<FNaniteVisualizeCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Visualization"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ViewSize, 8)
		);
	}

	if (IsVisualizingHTile())
	{
		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		FRDGTextureSRVRef HTileSRV = nullptr;

		if (RasterResults.MaterialDepth)
		{
			if (GNaniteDebugVisualize == VISUALIZE_MAT_HTILE_MINZ ||
				GNaniteDebugVisualize == VISUALIZE_MAT_HTILE_MAXZ ||
				GNaniteDebugVisualize == VISUALIZE_MAT_HTILE_DELTAZ ||
				GNaniteDebugVisualize == VISUALIZE_MAT_HTILE_ZMASK)
			{
				HTileSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMetaData(RasterResults.MaterialDepth, ERDGTextureMetaDataAccess::HTile));
			}
		}

		if (!HTileSRV)
		{
			HTileSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::HTile));
		}

		FRDGTextureRef DebugOutput = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
			SceneDepth->Desc.Extent,
			PF_A32B32G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Nanite.Debug"));

		FHTileVisualizeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHTileVisualizeCS::FParameters>();

		const uint32 PixelsWide = uint32(ViewSize.X);
		const uint32 PixelsTall = uint32(ViewSize.Y);
		const uint32 PlatformConfig = RHIGetHTilePlatformConfig(PixelsWide, PixelsTall);

		PassParameters->HTileBuffer = HTileSRV;
		PassParameters->HTileDisplay = GraphBuilder.CreateUAV(DebugOutput);
		PassParameters->HTileConfig  = FIntVector4(PlatformConfig, PixelsWide, GNaniteDebugVisualize, 0);

		auto ComputeShader = View.ShaderMap->GetShader<FHTileVisualizeCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HTileVisualize"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ViewSize, 8)
		);
	}
}
#endif

void AddVisualizationPasses(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextures& SceneTextures,
	const FEngineShowFlags& EngineShowFlags,
	TArrayView<const FViewInfo> Views,
	TArrayView<Nanite::FRasterResults> Results
)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	const FNaniteVisualizationData& VisualizationData = GetNaniteVisualizationData();

	// We only support debug visualization on the first view (at the moment)
	if (Scene && Views.Num() > 0 && VisualizationData.IsActive() && EngineShowFlags.VisualizeNanite)
	{
		// These should always match 1:1
		if (ensure(Views.Num() == Results.Num()))
		{
			Nanite::FRasterResults& Data = Results[0];
			const FViewInfo& View = Views[0];

			// TODO: Don't currently support offset views.
			checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

			const int32 ViewWidth  = View.ViewRect.Max.X - View.ViewRect.Min.X;
			const int32 ViewHeight = View.ViewRect.Max.Y - View.ViewRect.Min.Y;
			const FIntPoint ViewSize = FIntPoint(ViewWidth, ViewHeight);

			LLM_SCOPE_BYTAG(Nanite);
			RDG_EVENT_SCOPE(GraphBuilder, "Nanite::Visualization");
			RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteDebug);

			const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

			FRDGTextureRef VisBuffer64 = Data.VisBuffer64 ? Data.VisBuffer64 : SystemTextures.Black;
			FRDGTextureRef DbgBuffer64 = Data.DbgBuffer64 ? Data.DbgBuffer64 : SystemTextures.Black;
			FRDGTextureRef DbgBuffer32 = Data.DbgBuffer32 ? Data.DbgBuffer32 : SystemTextures.Black;
			FRDGTextureRef NaniteMask = Data.NaniteMask ? Data.NaniteMask : SystemTextures.Black;
			FRDGTextureRef VelocityBuffer = Data.VelocityBuffer ? Data.VelocityBuffer : SystemTextures.Black;

			FRDGBufferRef VisibleClustersSWHW = Data.VisibleClustersSWHW;

			// Generate material complexity
			FRDGTextureRef MaterialComplexity = nullptr;
			{
				const FIntPoint TileGridDim = FMath::DivideAndRoundUp(ViewSize, { 8, 8 });

				FRDGTextureDesc MaterialComplexityDesc = FRDGTextureDesc::Create2D(TileGridDim, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
				MaterialComplexity = GraphBuilder.CreateTexture(MaterialComplexityDesc, TEXT("Nanite.MaterialComplexity"));
				FRDGTextureUAVRef MaterialComplexityUAV = GraphBuilder.CreateUAV(MaterialComplexity);

				FMaterialComplexityCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMaterialComplexityCS::FParameters>();
				PassParameters->View					= View.ViewUniformBuffer;
				PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
				PassParameters->SOAStrides				= Data.SOAStrides;
				PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
				PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
				PassParameters->VisBuffer64				= VisBuffer64;
				PassParameters->MaterialDepthTable		= Scene->MaterialTables[ENaniteMeshPass::BasePass].GetDepthTableSRV();
				PassParameters->MaterialComplexity		= MaterialComplexityUAV;

				auto ComputeShader = View.ShaderMap->GetShader<FMaterialComplexityCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("MaterialComplexity"),
					ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(ViewSize, 8)
				);
			}

			Data.Visualizations.Reset();

			if (VisualizationData.GetActiveModeID() > 0)
			{
				// Single visualization
				FVisualizeResult Visualization = {};
				Visualization.ModeName			= VisualizationData.GetActiveModeName();
				Visualization.ModeID			= VisualizationData.GetActiveModeID();
				Visualization.bCompositeScene	= VisualizationData.GetActiveModeDefaultComposited();
				Visualization.bSkippedTile		= false;
				Data.Visualizations.Emplace(Visualization);
			}
			else if (VisualizationData.GetActiveModeID() == 0)
			{
				// Overview mode
				const auto& OverviewModeNames = VisualizationData.GetOverviewModeNames();
				for (const FName& ModeName : OverviewModeNames)
				{
					FVisualizeResult Visualization = {};
					Visualization.ModeName			= ModeName;
					Visualization.ModeID			= VisualizationData.GetModeID(Visualization.ModeName);
					Visualization.bCompositeScene	= VisualizationData.GetModeDefaultComposited(Visualization.ModeName);
					Visualization.bSkippedTile		= Visualization.ModeName == NAME_None;
					Data.Visualizations.Emplace(Visualization);
				}
			}

			for (FVisualizeResult& Visualization : Data.Visualizations)
			{
				if (Visualization.bSkippedTile)
				{
					continue;
				}

				// Apply force off/on scene composition
				if (GNaniteVisualizeComposite == 0)
				{
					// Force off
					Visualization.bCompositeScene = false;
				}
				else if (GNaniteVisualizeComposite == 1)
				{
					// Force on
					Visualization.bCompositeScene = true;
				}

				FRDGTextureDesc VisualizationOutputDesc = FRDGTextureDesc::Create2D(
					View.ViewRect.Max,
					PF_A32B32G32R32F,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV);

				Visualization.ModeOutput = GraphBuilder.CreateTexture(VisualizationOutputDesc, TEXT("Nanite.Visualization"));

				FNaniteVisualizeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteVisualizeCS::FParameters>();

				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
				PassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();
				PassParameters->VisualizeConfig = GetVisualizeConfig(Visualization.ModeID, Visualization.bCompositeScene, GNaniteVisualizeEdgeDetect != 0);
				PassParameters->VisualizeScales = GetVisualizeScales(Visualization.ModeID);
				PassParameters->SOAStrides = Data.SOAStrides;
				PassParameters->MaxVisibleClusters = Data.MaxVisibleClusters;
				PassParameters->RenderFlags = Data.RenderFlags;
				PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
				PassParameters->VisBuffer64 = VisBuffer64;
				PassParameters->DbgBuffer64 = DbgBuffer64;
				PassParameters->DbgBuffer32 = DbgBuffer32;
				PassParameters->NaniteMask = NaniteMask;
				PassParameters->SceneDepth = SceneTextures.Depth.Target;
				PassParameters->MaterialComplexity = MaterialComplexity ? MaterialComplexity : SystemTextures.Black;
				PassParameters->MaterialDepthTable = Scene->MaterialTables[ENaniteMeshPass::BasePass].GetDepthTableSRV();
			#if WITH_EDITOR
				PassParameters->MaterialHitProxyTable = Scene->MaterialTables[ENaniteMeshPass::BasePass].GetHitProxyTableSRV();
			#else
				// TODO: Permutation with hit proxy support to keep this clean?
				// For now, bind a valid SRV
				PassParameters->MaterialHitProxyTable = Scene->MaterialTables[ENaniteMeshPass::BasePass].GetDepthTableSRV();
			#endif
				PassParameters->DebugOutput = GraphBuilder.CreateUAV(Visualization.ModeOutput);

				auto ComputeShader = View.ShaderMap->GetShader<FNaniteVisualizeCS>();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Nanite::Visualize"),
					ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(ViewSize, 8)
				);
			}
		}
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteMarkStencilRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FNaniteMarkStencilPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitMaterialIdRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FEmitMaterialDepthPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitDepthRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FEmitSceneDepthPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

struct FLumenMeshCaptureMaterialPassIndex
{
	FLumenMeshCaptureMaterialPassIndex(int32 InIndex, int32 InCommandStateBucketId)
		: Index(InIndex)
		, CommandStateBucketId(InCommandStateBucketId)
	{
	}

	inline friend uint32 GetTypeHash(const FLumenMeshCaptureMaterialPassIndex& PassIndex)
	{
		return CityHash32((const char*)&PassIndex.CommandStateBucketId, sizeof(PassIndex.CommandStateBucketId));
	}

	inline bool operator==(const FLumenMeshCaptureMaterialPassIndex& PassIndex) const
	{
		return CommandStateBucketId == PassIndex.CommandStateBucketId;
	}

	int32 Index = -1;
	int32 CommandStateBucketId = -1;
};

struct FLumenMeshCaptureMaterialPass
{
	int32 CommandStateBucketId = -1;
	uint32 ViewIndexBufferOffset = 0;
	TArray<uint16, TInlineAllocator<256>> ViewIndices;
};

void DrawLumenMeshCapturePass(
	FRDGBuilder& GraphBuilder,
	FScene& Scene,
	FViewInfo* SharedView,
	TArrayView<const FCardPageRenderData> CardPagesToRender,
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
)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));
	checkSlow(DoesPlatformSupportLumenGI(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::DrawLumenMeshCapturePass");

	// Material range placeholder (not used by Lumen, but must still be bound)
	FRDGTextureDesc MaterialRangeDesc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource);
	FRDGTextureRef  MaterialRange = GraphBuilder.CreateTexture(MaterialRangeDesc, TEXT("Nanite.MaterialRange"));

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	// Visible material mask buffer (currently not used by Lumen, but still must be bound)
	FRDGBufferDesc   VisibleMaterialsDesc = FRDGBufferDesc::CreateStructuredDesc(4, 1);
	FRDGBufferRef    VisibleMaterials     = GraphBuilder.CreateBuffer(VisibleMaterialsDesc, TEXT("Nanite.VisibleMaterials"));
	FRDGBufferUAVRef VisibleMaterialsUAV  = GraphBuilder.CreateUAV(VisibleMaterials);
	AddClearUAVPass(GraphBuilder, VisibleMaterialsUAV, 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaterialRange), FUintVector4(0, 0, 0, 0));

	// Mark stencil for all pixels that pass depth test
	{
		FNaniteMarkStencilRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteMarkStencilRectsParameters>();

		PassParameters->PS.View = SharedView->ViewUniformBuffer;
		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;

		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
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
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		FEmitMaterialDepthPS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FEmitMaterialDepthPS::FNaniteMaskDim>(false /* not using Nanite mask */);
		auto PixelShader = SharedView->ShaderMap->GetShader<FEmitMaterialDepthPS>(PermutationVectorPS);

		FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
			SharedView->ShaderMap,
			RDG_EVENT_NAME("Emit Material Depth"),
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
		int32 NumMaterialQuads = 0;

		TArray<FLumenMeshCaptureMaterialPass, SceneRenderingAllocator> MaterialPasses;
		MaterialPasses.Reserve(CardPagesToRender.Num());

		// Build list of unique materials
		{
			Experimental::TRobinHoodHashSet<FLumenMeshCaptureMaterialPassIndex> MaterialPassSet;

			for (int32 CardPageIndex = 0; CardPageIndex < CardPagesToRender.Num(); ++CardPageIndex)
			{
				const FCardPageRenderData& CardPageRenderData = CardPagesToRender[CardPageIndex];

				for (const FNaniteCommandInfo& CommandInfo : CardPageRenderData.NaniteCommandInfos)
				{
					const FLumenMeshCaptureMaterialPassIndex& PassIndex = *MaterialPassSet.FindOrAdd(FLumenMeshCaptureMaterialPassIndex(MaterialPasses.Num(), CommandInfo.GetStateBucketId()));

					if (PassIndex.Index >= MaterialPasses.Num())
					{
						FLumenMeshCaptureMaterialPass MaterialPass;
						MaterialPass.CommandStateBucketId = CommandInfo.GetStateBucketId();
						MaterialPass.ViewIndexBufferOffset = 0;
						MaterialPasses.Add(MaterialPass);
					}

					MaterialPasses[PassIndex.Index].ViewIndices.Add(CardPageIndex);
					++NumMaterialQuads;
				}
			}
			ensure(MaterialPasses.Num() > 0);
		}

		TArray<uint32, SceneRenderingAllocator> ViewIndices;
		ViewIndices.Reserve(NumMaterialQuads);

		for (FLumenMeshCaptureMaterialPass& MaterialPass : MaterialPasses)
		{
			MaterialPass.ViewIndexBufferOffset = ViewIndices.Num();

			for (int32 ViewIndex : MaterialPass.ViewIndices)
			{
				ViewIndices.Add(ViewIndex);
			}
		}
		ensure(ViewIndices.Num() > 0);

		FRDGBufferRef ViewIndexBuffer = CreateStructuredBuffer(
			GraphBuilder, 
			TEXT("Nanite.ViewIndices"),
			ViewIndices.GetTypeSize(),
			FMath::RoundUpToPowerOfTwo(ViewIndices.Num()),
			ViewIndices.GetData(),
			ViewIndices.Num() * ViewIndices.GetTypeSize());

		TArray<FVector4, SceneRenderingAllocator> ViewRectScaleOffsets;
		ViewRectScaleOffsets.Reserve(CardPagesToRender.Num());

		TArray<Nanite::FPackedView, SceneRenderingAllocator> PackedViews;
		PackedViews.Reserve(CardPagesToRender.Num());

		const FVector2D ViewportSizeF = FVector2D(float(ViewportSize.X), float(ViewportSize.Y));

		for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
		{
			const FVector2D CardViewportSize = FVector2D(float(CardPageRenderData.CardCaptureAtlasRect.Width()), float(CardPageRenderData.CardCaptureAtlasRect.Height()));
			const FVector2D RectOffset = FVector2D(float(CardPageRenderData.CardCaptureAtlasRect.Min.X), float(CardPageRenderData.CardCaptureAtlasRect.Min.Y)) / ViewportSizeF;
			const FVector2D RectScale = CardViewportSize / ViewportSizeF;

			ViewRectScaleOffsets.Add(FVector4(RectScale, RectOffset));

			Nanite::FPackedViewParams Params;
			Params.ViewMatrices = CardPageRenderData.ViewMatrices;
			Params.PrevViewMatrices = CardPageRenderData.ViewMatrices;
			Params.ViewRect = CardPageRenderData.CardCaptureAtlasRect;
			Params.RasterContextSize = ViewportSize;
			Params.LODScaleFactor = 0.0f;
			PackedViews.Add(Nanite::CreatePackedView(Params));
		}

		FRDGBufferRef ViewRectScaleOffsetBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("Nanite.ViewRectScaleOffset"),
			ViewRectScaleOffsets.GetTypeSize(),
			FMath::RoundUpToPowerOfTwo(ViewRectScaleOffsets.Num()),
			ViewRectScaleOffsets.GetData(),
			ViewRectScaleOffsets.Num() * ViewRectScaleOffsets.GetTypeSize());

		FRDGBufferRef PackedViewBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("Nanite.PackedViews"),
			PackedViews.GetTypeSize(),
			FMath::RoundUpToPowerOfTwo(PackedViews.Num()),
			PackedViews.GetData(),
			PackedViews.Num() * PackedViews.GetTypeSize());


		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->SOAStrides = CullingContext.SOAStrides;
		PassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
		PassParameters->MaxNodes = Nanite::FGlobalResources::GetMaxNodes();
		PassParameters->RenderFlags = CullingContext.RenderFlags;

		PassParameters->ClusterPageData = GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders = GStreamingManager.GetClusterPageHeadersSRV();

		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);

		PassParameters->MaterialRange = MaterialRange;
		PassParameters->GridSize = { 1u, 1u };

		PassParameters->VisibleMaterials = GraphBuilder.CreateSRV(VisibleMaterials, PF_R32_UINT);
			
		PassParameters->MultiViewEnabled = 1;
		PassParameters->MultiViewIndices = GraphBuilder.CreateSRV(ViewIndexBuffer);
		PassParameters->MultiViewRectScaleOffsets = GraphBuilder.CreateSRV(ViewRectScaleOffsetBuffer);
		PassParameters->InViews = GraphBuilder.CreateSRV(PackedViewBuffer);

		PassParameters->VisBuffer64 = RasterContext.VisBuffer64;
		PassParameters->DbgBuffer64 = SystemTextures.Black;
		PassParameters->DbgBuffer32 = SystemTextures.Black;

		PassParameters->RenderTargets[0] = FRenderTargetBinding(AlbedoAtlasTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(NormalAtlasTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[2] = FRenderTargetBinding(EmissiveAtlasTexture, ERenderTargetLoadAction::ELoad);

		PassParameters->View = Scene.UniformBuffers.LumenCardCaptureViewUniformBuffer;
		PassParameters->CardPass = GraphBuilder.CreateUniformBuffer(PassUniformParameters);

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		TShaderMapRef<FNaniteMaterialVS> NaniteVertexShader(SharedView->ShaderMap);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Lumen Emit GBuffer %d materials %d quads", MaterialPasses.Num(), NumMaterialQuads),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, 
				&Scene, 
				&FirstCardPage = CardPagesToRender[0],
				MaterialPassArray = TArrayView<const FLumenMeshCaptureMaterialPass>(MaterialPasses),
				NaniteVertexShader, 
				SharedView,
				ViewportSize](FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LumenEmitGBuffer);

				FirstCardPage.PatchView(RHICmdList, &Scene, SharedView);
				Scene.UniformBuffers.LumenCardCaptureViewUniformBuffer.UpdateUniformBufferImmediate(*SharedView->CachedViewUniformShaderParameters);

				FNaniteUniformParameters UniformParams;
				UniformParams.SOAStrides = PassParameters->SOAStrides;
				UniformParams.MaxVisibleClusters = PassParameters->MaxVisibleClusters;
				UniformParams.MaxNodes = PassParameters->MaxNodes;
				UniformParams.RenderFlags = PassParameters->RenderFlags;
				UniformParams.MaterialConfig = FIntVector4(0, 1, 1, 0); // Tile based material culling is not required for Lumen, as each card is rendered as a small rect
				UniformParams.RectScaleOffset = FVector4(1.0f, 1.0f, 0.0f, 0.0f); // This will be overridden in vertex shader

				UniformParams.ClusterPageData = PassParameters->ClusterPageData;
				UniformParams.ClusterPageHeaders = PassParameters->ClusterPageHeaders;

				UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

				UniformParams.MaterialRange = PassParameters->MaterialRange->GetRHI();
				UniformParams.VisibleMaterials = PassParameters->VisibleMaterials->GetRHI();

				UniformParams.MultiViewEnabled = PassParameters->MultiViewEnabled;
				UniformParams.MultiViewIndices = PassParameters->MultiViewIndices;
				UniformParams.MultiViewRectScaleOffsets = PassParameters->MultiViewRectScaleOffsets;
				UniformParams.InViews = PassParameters->InViews;

				UniformParams.VisBuffer64 = PassParameters->VisBuffer64->GetRHI();
				UniformParams.DbgBuffer64 = PassParameters->DbgBuffer64->GetRHI();
				UniformParams.DbgBuffer32 = PassParameters->DbgBuffer32->GetRHI();

				Scene.UniformBuffers.NaniteUniformBuffer.UpdateUniformBufferImmediate(UniformParams);

				FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
				FMeshDrawCommandStateCache StateCache;

				for (const FLumenMeshCaptureMaterialPass& MaterialPass : MaterialPassArray)
				{
					// One instance per card page
					const uint32 InstanceFactor = MaterialPass.ViewIndices.Num();
					const uint32 InstanceBaseOffset = MaterialPass.ViewIndexBufferOffset;

					Experimental::FHashElementId SetId(MaterialPass.CommandStateBucketId);
					const FMeshDrawCommand& MeshDrawCommand = Scene.NaniteDrawCommands[ENaniteMeshPass::LumenCardCapture].GetByElementId(SetId).Key;

					const int32 DrawIdx = MaterialPass.CommandStateBucketId;
					const float MaterialDepth = FNaniteCommandInfo::GetDepthId(DrawIdx);
					SubmitNaniteMaterialPassCommand(MeshDrawCommand, MaterialDepth, NaniteVertexShader, GraphicsMinimalPipelineStateSet, InstanceFactor, RHICmdList, StateCache, InstanceBaseOffset);
				}
			});
	}

	// Emit depth values
	{
		FNaniteEmitDepthRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitDepthRectsParameters>();

		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;
		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		FEmitSceneDepthPS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FEmitSceneDepthPS::FVelocityExportDim>(false);
		auto PixelShader = SharedView->ShaderMap->GetShader<FEmitSceneDepthPS>(PermutationVectorPS);

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

	const float Mx = 2.0f * ViewSizeAndInvSize.Z;
	const float My = -2.0f * ViewSizeAndInvSize.W;
	const float Ax = -1.0f - 2.0f * ViewRect.Min.X * ViewSizeAndInvSize.Z;
	const float Ay = 1.0f + 2.0f * ViewRect.Min.Y * ViewSizeAndInvSize.W;

	PackedView.SVPositionToTranslatedWorld =
		FMatrix(FPlane(Mx, 0, 0, 0),
			FPlane(0, My, 0, 0),
			FPlane(0, 0, 1, 0),
			FPlane(Ax, Ay, 0, 1)) * Params.ViewMatrices.GetInvTranslatedViewProjectionMatrix();
	PackedView.ViewToTranslatedWorld = Params.ViewMatrices.GetOverriddenInvTranslatedViewMatrix();

	check(Params.StreamingPriorityCategory <= STREAMING_PRIORITY_CATEGORY_MASK);
	PackedView.StreamingPriorityCategory_AndFlags = (Params.Flags << NUM_STREAMING_PRIORITY_CATEGORY_BITS) | Params.StreamingPriorityCategory;
	PackedView.MinBoundsRadiusSq = Params.MinBoundsRadius * Params.MinBoundsRadius;
	PackedView.UpdateLODScales();

	PackedView.LODScales.X *= Params.LODScaleFactor;

	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X = Params.TargetLayerIndex;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y = Params.TargetMipLevel;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z = Params.TargetMipCount;
	PackedView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.W = Params.PrevTargetLayerIndex;

	PackedView.HZBTestViewRect = FIntVector4(Params.HZBTestViewRect.Min.X, Params.HZBTestViewRect.Min.Y, Params.HZBTestViewRect.Max.X, Params.HZBTestViewRect.Max.Y);
	
	return PackedView;
}

FPackedView CreatePackedViewFromViewInfo(
	const FViewInfo& View,
	FIntPoint RasterContextSize,
	uint32 Flags,
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
	Params.Flags = Flags;
	Params.StreamingPriorityCategory = StreamingPriorityCategory;
	Params.MinBoundsRadius = MinBoundsRadius;
	Params.LODScaleFactor = LODScaleFactor;
	Params.HZBTestViewRect = View.PrevViewInfo.ViewRect;
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

	LLM_SCOPE_BYTAG(Nanite);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
	FRDGTextureRef VisBuffer64 = NaniteRasterResults->VisBuffer64 ? NaniteRasterResults->VisBuffer64 : SystemTextures.Black;
	FRDGBufferRef VisibleClustersSWHW = NaniteRasterResults->VisibleClustersSWHW;

	OutPassParameters->View						= View.ViewUniformBuffer;
	OutPassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
	OutPassParameters->MaxVisibleClusters		= Nanite::FGlobalResources::GetMaxVisibleClusters();
	OutPassParameters->SOAStrides				= NaniteRasterResults->SOAStrides;
	OutPassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
	OutPassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
	OutPassParameters->VisBuffer64				= VisBuffer64;
	OutPassParameters->MaterialHitProxyTable	= Scene.MaterialTables[ENaniteMeshPass::BasePass].GetHitProxyTableSRV();
	OutPassParameters->OutputToInputScale		= FScreenTransform::ChangeRectFromTo(ViewportRect, View.ViewRect).Scale;
	OutPassParameters->OutputToInputBias		= FScreenTransform::ChangeRectFromTo(ViewportRect, View.ViewRect).Bias;
}

void DrawEditorSelection(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FNaniteSelectionOutlineParameters& PassParameters
	)
{
	LLM_SCOPE_BYTAG(Nanite);

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
		View.ShaderMap,
		PixelShader,
		PassParameters,
		ViewportRect,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
		3
	);
}

void GetEditorVisualizeLevelInstancePassParameters(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FRasterResults* NaniteRasterResults,
	FNaniteVisualizeLevelInstanceParameters* OutPassParameters
)
{
	if (!NaniteRasterResults)
	{
		return;
	}

	LLM_SCOPE_BYTAG(Nanite);
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	FRDGTextureRef VisBuffer64 = NaniteRasterResults->VisBuffer64 ? NaniteRasterResults->VisBuffer64 : SystemTextures.Black;
	FRDGBufferRef VisibleClustersSWHW = NaniteRasterResults->VisibleClustersSWHW;

	OutPassParameters->View = View.ViewUniformBuffer;
	OutPassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
	OutPassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
	OutPassParameters->SOAStrides = NaniteRasterResults->SOAStrides;
	OutPassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
	OutPassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();
	OutPassParameters->VisBuffer64 = VisBuffer64;
	OutPassParameters->MaterialHitProxyTable = Scene.MaterialTables[ENaniteMeshPass::BasePass].GetHitProxyTableSRV();
	OutPassParameters->OutputToInputScale = FScreenTransform::ChangeRectFromTo(ViewportRect, View.ViewRect).Scale;
	OutPassParameters->OutputToInputBias = FScreenTransform::ChangeRectFromTo(ViewportRect, View.ViewRect).Bias;
}

void DrawEditorVisualizeLevelInstance(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	const FIntRect ViewportRect,
	const FNaniteVisualizeLevelInstanceParameters& PassParameters
)
{
	LLM_SCOPE_BYTAG(Nanite);

	if (View.EditorVisualizeLevelInstanceIds.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NaniteEditorLevelInstance);
	SCOPED_GPU_STAT(RHICmdList, NaniteEditor);

	uint32 LevelInstancePrimCount = FMath::RoundUpToPowerOfTwo(View.EditorVisualizeLevelInstanceIds.Num());
	uint32 SearchBufferCountDim = FMath::Min((uint32)FEmitEditingLevelInstanceDepthPS::FSearchBufferCountDim::MaxValue, FMath::FloorLog2(LevelInstancePrimCount));

	FEmitEditingLevelInstanceDepthPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FEmitEditingLevelInstanceDepthPS::FSearchBufferCountDim>(SearchBufferCountDim);

	auto PixelShader = View.ShaderMap->GetShader<FEmitEditingLevelInstanceDepthPS>(PermutationVector.ToDimensionValueId());

	FPixelShaderUtils::DrawFullscreenPixelShader(
		RHICmdList,
		View.ShaderMap,
		PixelShader,
		PassParameters,
		ViewportRect,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
		1
	);
}

#endif // WITH_EDITOR

} // namespace Nanite
