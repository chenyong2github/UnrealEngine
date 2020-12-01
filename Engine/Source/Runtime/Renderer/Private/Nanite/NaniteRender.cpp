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
#include "BasePassRendering.h"
#include "Lumen/LumenSceneRendering.h"

#define CULLING_PASS_NO_OCCLUSION		0
#define CULLING_PASS_OCCLUSION_MAIN		1
#define CULLING_PASS_OCCLUSION_POST		2
#define CULLING_PASS_EXPLICIT_LIST		3

#define RENDER_FLAG_HAVE_PREV_DRAW_DATA				0x1
#define RENDER_FLAG_FORCE_HW_RASTER					0x2
#define RENDER_FLAG_PRIMITIVE_SHADER				0x4
#define RENDER_FLAG_OUTPUT_STREAMING_REQUESTS		0x8

// Only available with the DEBUG_FLAGS permutation active.
#define DEBUG_FLAG_WRITE_STATS						0x1
#define DEBUG_FLAG_CULL_HZB_BOX						0x2
#define DEBUG_FLAG_CULL_HZB_SPHERE					0x4
#define DEBUG_FLAG_CULL_FRUSTUM_BOX					0x8
#define DEBUG_FLAG_CULL_FRUSTUM_SPHERE				0x10

DECLARE_GPU_STAT_NAMED(NaniteInstanceCull,		TEXT("Nanite Instance Cull"));
DECLARE_GPU_STAT_NAMED(NaniteInstanceCullVSM,	TEXT("Nanite Instance Cull VSM"));

DECLARE_GPU_STAT_NAMED(NaniteHierarchyCull,		TEXT("Nanite Hierarchy Cull"));
DECLARE_GPU_STAT_NAMED(NaniteClusterCull,		TEXT("Nanite Cluster Cull"));

DEFINE_GPU_STAT(NaniteDebug);
DEFINE_GPU_STAT(NaniteDepth);
DEFINE_GPU_STAT(NaniteEditor);
DEFINE_GPU_STAT(NaniteRaster);
DEFINE_GPU_STAT(NaniteMaterials);

DECLARE_DWORD_COUNTER_STAT(TEXT("CullingContexts"), STAT_NaniteCullingContexts, STATGROUP_Nanite);

DEFINE_LOG_CATEGORY(LogNanite);

#define NANITE_MATERIAL_STENCIL 1

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

int32 GNaniteDebugSceneComposite = 1; // If != 0, only visualize Nanite information that passes full scene depth test
FAutoConsoleVariableRef CVarNaniteDebugSceneComposite(
	TEXT("r.Nanite.DebugSceneComposite"),
	GNaniteDebugSceneComposite,
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
#define VISUALIZE_NANITE_MASK						21

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
	return FIntVector4(GNaniteDebugVisualize, GNaniteDebugOverdrawScale, GNaniteDebugSceneComposite, 0);
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
			 && Parameters.Platform != EShaderPlatform::SP_PCD3D_SM5)
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
		
		SHADER_PARAMETER_STRUCT_INCLUDE( FCullingParameters, CullingParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters )
		SHADER_PARAMETER_STRUCT_INCLUDE( FRasterParameters, RasterParameters )

		SHADER_PARAMETER_SRV( ByteAddressBuffer, ImposterAtlas )
		
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FInstanceDraw >, InInstanceDraws )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWByteAddressBuffer, OutNodes )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FInstanceDraw >, OutOccludedInstances )

		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPersistentState >, OutMainAndPostPassPersistentStates )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OutOccludedInstancesArgs )

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FNaniteStats>, OutStatsBuffer)

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

		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FInstanceDraw >, InOccludedInstances )
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
	SHADER_PARAMETER_STRUCT_INCLUDE( FGPUSceneParameters, GPUSceneParameters )
	SHADER_PARAMETER_STRUCT_INCLUDE( FRasterParameters, RasterParameters )

	SHADER_PARAMETER( FIntVector4,	VisualizeConfig )
	SHADER_PARAMETER( FIntVector4,	SOAStrides )
	SHADER_PARAMETER( uint32,		MaxVisibleClusters )
	SHADER_PARAMETER( uint32,		RenderFlags )
	SHADER_PARAMETER( uint32,       RasterStateReverseCull )
	
	SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageData )
	SHADER_PARAMETER_SRV( ByteAddressBuffer,				ClusterPageHeaders )

	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FPackedView >,	InViews )
	SHADER_PARAMETER_RDG_BUFFER_SRV( ByteAddressBuffer,					VisibleClustersSWHW )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FUintVector2 >,	InTotalPrevDrawClusters )

	SHADER_PARAMETER_RDG_BUFFER_SRV( Buffer< uint >, InClusterOffsetSWHW )

	SHADER_PARAMETER_RDG_BUFFER( Buffer< uint >, IndirectArgs )

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
		
		if( !FRasterTechnique::ShouldCompilePermutation( Parameters, PermutationVector.Get<FRasterTechniqueDim>() ) )
			return false;

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

IMPLEMENT_GLOBAL_SHADER(FNaniteVS, "/Engine/Private/Nanite/ExportGBuffer.usf", "FullScreenVS", SF_Vertex);

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
		SHADER_PARAMETER( uint32, MaxVisibleClusters )
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
		SHADER_PARAMETER(uint32, PackedClusterSize)
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
	LLM_SCOPE_BYTAG(Nanite);

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
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
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

bool FNaniteMaterialTables::Begin(FRHICommandListImmediate& RHICmdList, uint32 NumPrimitives, uint32 InNumPrimitiveUpdates)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	check(NumPrimitiveUpdates == 0);
	check(NumDepthTableUpdates == 0);
#if WITH_EDITOR
	check(NumHitProxyTableUpdates == 0);
#endif
	NumPrimitiveUpdates = InNumPrimitiveUpdates;

	TArray<FRHITransitionInfo, TInlineAllocator<2>> UAVs;

	const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumPrimitives * MaxMaterials, 256u));
	bool bResized = false;
	bResized |= ResizeResourceIfNeeded(RHICmdList, DepthTableDataBuffer, SizeReserve * sizeof(uint32), TEXT("DepthTableDataBuffer"));
	if (bResized)
	{
		UAVs.Add(FRHITransitionInfo(DepthTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
	}
#if WITH_EDITOR
	bResized |= ResizeResourceIfNeeded(RHICmdList, HitProxyTableDataBuffer, SizeReserve * sizeof(uint32), TEXT("HitProxyTableDataBuffer"));
	if (bResized)
	{
		UAVs.Add(FRHITransitionInfo(HitProxyTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
	}
#endif // WITH_EDITOR

	RHICmdList.Transition(UAVs);

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

static void AddPassInitNodesUAV( FRDGBuilder& GraphBuilder, FRDGBufferUAVRef UAVRef, bool bIsPostPass )
{
	LLM_SCOPE_BYTAG(Nanite);

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
	const FIntRect &HZBBuildViewRect,
	bool bTwoPassOcclusion,
	bool bUpdateStreaming,
	bool bSupportsMultiplePasses,
	bool bForceHWRaster,
	bool bPrimaryContext
	)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::InitCullingContext");

	INC_DWORD_STAT(STAT_NaniteCullingContexts);

	FCullingContext CullingContext = {};

	CullingContext.PrevHZB					= PrevHZB;
	CullingContext.HZBBuildViewRect			= HZBBuildViewRect;
	CullingContext.bTwoPassOcclusion		= PrevHZB != nullptr && bTwoPassOcclusion;
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
	FRDGBufferDesc CandidateClustersDesc				= FRDGBufferDesc::CreateStructuredDesc(4, 3 * Nanite::FGlobalResources::GetMaxCandidateClusters());	// Max candidate clusters * sizeof(uint3)
	CandidateClustersDesc.Usage							= EBufferUsageFlags(CandidateClustersDesc.Usage | BUF_ByteAddressBuffer);
	FRDGBufferDesc VisibleClustersDesc					= FRDGBufferDesc::CreateStructuredDesc(4, 3 * Nanite::FGlobalResources::GetMaxVisibleClusters());	// Max visible clusters * sizeof(uint3)
	VisibleClustersDesc.Usage							= EBufferUsageFlags(VisibleClustersDesc.Usage | BUF_ByteAddressBuffer);

	CullingContext.VisibleClustersSWHW					= GraphBuilder.CreateBuffer(VisibleClustersDesc, TEXT("VisibleClustersSWHW"));
	CullingContext.MainPass.CandidateClusters			= GraphBuilder.CreateBuffer(CandidateClustersDesc, TEXT("MainPass.CandidateClusters"));
#endif

	CullingContext.MainPass.CandidateClustersArgs		= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateIndirectDesc( 4 ), TEXT("MainPass.CandidateClustersArgs") );	
	CullingContext.MainPass.RasterizeArgsSWHW			= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateIndirectDesc( 8 ), TEXT("MainPass.RasterizeArgsSWHW") );
	
	if( CullingContext.bTwoPassOcclusion )
	{
	#if NANITE_USE_SCRATCH_BUFFERS
		CullingContext.OccludedInstances				= GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetScratchOccludedInstancesBufferRef(), TEXT("OccludedInstances"));
		CullingContext.PostPass.CandidateClusters		= GraphBuilder.RegisterExternalBuffer(Nanite::GGlobalResources.GetPostPassBuffers().ScratchCandidateClustersBuffer, TEXT("PostPass.CandidateClusters"));
	#else
		CullingContext.OccludedInstances				= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FInstanceDraw), NumSceneInstances), TEXT("OccludedInstances"));
		CullingContext.PostPass.CandidateClusters		= GraphBuilder.CreateBuffer(CandidateClustersDesc, TEXT("PostPassCandidateClusters"));
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
		TRefCountPtr<FRDGPooledBuffer>& MainPassNodesBufferRef = Nanite::GGlobalResources.GetMainPassBuffers().NodesBuffer;
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
		TRefCountPtr<FRDGPooledBuffer>& PostPassNodesBufferRef = Nanite::GGlobalResources.GetPostPassBuffers().NodesBuffer;
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
	const FRasterContext& RasterContext,
	const FRasterState& RasterState,
	const FGPUSceneParameters &GPUSceneParameters,
	uint32 CullingPass,
	FVirtualShadowMapArray *VirtualShadowMapArray,
	FVirtualTargetParameters &VirtualTargetParameters
	)
{
	LLM_SCOPE_BYTAG(Nanite);

	checkf(GRHIPersistentThreadGroupCount > 0, TEXT("GRHIPersistentThreadGroupCount must be configured correctly in the RHI."));

	// Currently only occlusion free multi-view routing.
	ensure(!VirtualShadowMapArray || CullingPass == CULLING_PASS_NO_OCCLUSION);
	// TODO: if we need this emulation feature by going through the view we can probably pass in the shader map as part of the context and get it out of the view at context-creation time
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	const bool bMultiView = Views.Num() > 1 || VirtualShadowMapArray != nullptr;

	FRDGBufferRef PageFlags = nullptr;
	FRDGBufferRef HPageFlags = nullptr;
	FRDGBufferRef HZBPageTable = nullptr;

	if( VirtualShadowMapArray )
	{
		RDG_GPU_STAT_SCOPE( GraphBuilder, NaniteInstanceCullVSM );

		PageFlags = VirtualShadowMapArray->PageFlagsRDG;
		HPageFlags = VirtualShadowMapArray->HPageFlagsRDG;
		if( VirtualShadowMapArray->HZBPageTable )
			HZBPageTable	= GraphBuilder.RegisterExternalBuffer( VirtualShadowMapArray->HZBPageTable, TEXT("HZBPageTable") );
		else
			HZBPageTable	= VirtualShadowMapArray->PageTableRDG;

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
	else if (CullingContext.NumInstancesPreCull > 0 || CullingPass == CULLING_PASS_OCCLUSION_POST)
	{
		RDG_GPU_STAT_SCOPE( GraphBuilder, NaniteInstanceCull );
		FInstanceCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInstanceCull_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->RasterParameters = RasterContext.Parameters;
		PassParameters->NumInstances = CullingContext.NumInstancesPreCull;
		PassParameters->CullingParameters = CullingParameters;

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
			PassParameters->InInstanceDraws				= GraphBuilder.CreateSRV( CullingContext.OccludedInstances );
			PassParameters->InOccludedInstancesArgs		= GraphBuilder.CreateSRV( CullingContext.OccludedInstancesArgs );
			PassParameters->OutNodes					= GraphBuilder.CreateUAV( CullingContext.PostPass.Nodes );
		}
		
		check(CullingContext.ViewsBuffer);

		const uint32 InstanceCullingPass = CullingContext.InstanceDrawsBuffer != nullptr ? CULLING_PASS_EXPLICIT_LIST : CullingPass;
		FInstanceCull_CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInstanceCull_CS::FCullingPassDim>(InstanceCullingPass);
		PermutationVector.Set<FInstanceCull_CS::FMultiViewDim>(bMultiView);
		PermutationVector.Set<FInstanceCull_CS::FNearClipDim>(RasterState.bNearClip);
		PermutationVector.Set<FInstanceCull_CS::FDebugFlagsDim>(GNaniteDebugFlags != 0);
		PermutationVector.Set<FInstanceCull_CS::FRasterTechniqueDim>(int32(RasterContext.RasterTechnique));

		auto ComputeShader = ShaderMap->GetShader<FInstanceCull_CS>(PermutationVector);
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
		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteHierarchyCull);
		FPersistentHierarchicalCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FPersistentHierarchicalCull_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->CullingParameters		= CullingParameters;
		PassParameters->MaxNodes				= Nanite::FGlobalResources::GetMaxNodes();
		
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->HierarchyBuffer			= Nanite::GStreamingManager.GetHierarchySRV();
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
			PassParameters->VirtualShadowMap			= VirtualTargetParameters;
			PassParameters->HZBPageTable				= GraphBuilder.CreateSRV( HZBPageTable, PF_R32G32_UINT );
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
			FIntVector(GRHIPersistentThreadGroupCount, 1, 1)
		);
	}

	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteClusterCull);
		FCandidateCull_CS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCandidateCull_CS::FParameters >();

		PassParameters->GPUSceneParameters = GPUSceneParameters;
		PassParameters->CullingParameters		= CullingParameters;

		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();

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
			PassParameters->OutDynamicCasterFlags = GraphBuilder.CreateUAV(VirtualShadowMapArray->DynamicCasterPageFlagsRDG, PF_R32_UINT);
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

	// TODO: if we need this emulation feature by going through the view we can probably pass in the shader map as part of the context and get it out of the view at context-creation time
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

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
	CommonPassParameters->VisualizeConfig = GetVisualizeConfig();
	CommonPassParameters->SOAStrides = SOAStrides;
	CommonPassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
	CommonPassParameters->RenderFlags = RenderFlags;
	CommonPassParameters->RasterStateReverseCull = RasterState.CullMode == CM_CCW ? 1 : 0;
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
		if (GNaniteClusterPerPage)
		{
			ViewRect.Max = FIntPoint(FVirtualShadowMap::PageSize, FVirtualShadowMap::PageSize) * FVirtualShadowMap::RasterWindowPages;
		}
		else
		{
			ViewRect.Max = FIntPoint(FVirtualShadowMap::VirtualMaxResolutionXY, FVirtualShadowMap::VirtualMaxResolutionXY);
		}
	}

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

		FHWRasterizeVS::FPermutationDomain PermutationVectorVS;
		PermutationVectorVS.Set<FHWRasterizeVS::FRasterTechniqueDim>(int32(Technique));
		PermutationVectorVS.Set<FHWRasterizeVS::FAddClusterOffset>(bMainPass ? 0 : 1);
		PermutationVectorVS.Set<FHWRasterizeVS::FMultiViewDim>(bMultiView);
		PermutationVectorVS.Set<FHWRasterizeVS::FPrimShaderDim>(bUsePrimitiveShader);
		PermutationVectorVS.Set<FHWRasterizeVS::FPrimShaderCullDim>(bUsePrimitiveShaderCulling);
		PermutationVectorVS.Set<FHWRasterizeVS::FAutoShaderCullDim>(bUseAutoCullingShader);
		PermutationVectorVS.Set<FHWRasterizeVS::FHasPrevDrawData>(bHavePrevDrawData);
		PermutationVectorVS.Set<FHWRasterizeVS::FDebugVisualizeDim>(ShouldExportDebugBuffers() && Technique != ERasterTechnique::DepthOnly);
		PermutationVectorVS.Set<FHWRasterizeVS::FNearClipDim>(bNearClip);
		PermutationVectorVS.Set<FHWRasterizeVS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVectorVS.Set<FHWRasterizeVS::FClusterPerPageDim>(GNaniteClusterPerPage && VirtualShadowMapArray != nullptr);

		FHWRasterizePS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FHWRasterizePS::FRasterTechniqueDim>(int32(Technique));
		PermutationVectorPS.Set<FHWRasterizePS::FMultiViewDim>(bMultiView);
		PermutationVectorPS.Set<FHWRasterizePS::FPrimShaderDim>(bUsePrimitiveShader);
		PermutationVectorPS.Set<FHWRasterizePS::FPrimShaderCullDim>(bUsePrimitiveShaderCulling);
		PermutationVectorPS.Set<FHWRasterizePS::FDebugVisualizeDim>(ShouldExportDebugBuffers() && Technique != ERasterTechnique::DepthOnly);
		PermutationVectorPS.Set<FHWRasterizePS::FNearClipDim>(bNearClip);
		PermutationVectorPS.Set<FHWRasterizePS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVectorPS.Set<FHWRasterizePS::FClusterPerPageDim>(GNaniteClusterPerPage && VirtualShadowMapArray != nullptr);

		auto VertexShader = ShaderMap->GetShader<FHWRasterizeVS>(PermutationVectorVS);
		auto PixelShader = ShaderMap->GetShader<FHWRasterizePS>(PermutationVectorPS);

		GraphBuilder.AddPass(
			bMainPass ? RDG_EVENT_NAME("Main Pass: Rasterize") : RDG_EVENT_NAME("Post Pass: Rasterize"),
			RasterPassParameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, RasterPassParameters, ViewRect, bUsePrimitiveShader](FRHICommandListImmediate& RHICmdList)
		{
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

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RasterPassParameters->Common);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *RasterPassParameters);

			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitiveIndirect(RasterPassParameters->Common.IndirectArgs->GetIndirectRHICallBuffer(), 16);
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
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FDebugVisualizeDim>(ShouldExportDebugBuffers() && Technique != ERasterTechnique::DepthOnly);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FNearClipDim>(bNearClip);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FVirtualTextureTargetDim>(VirtualShadowMapArray != nullptr);
		PermutationVectorCS.Set<FMicropolyRasterizeCS::FClusterPerPageDim>(GNaniteClusterPerPage&& VirtualShadowMapArray != nullptr);

		auto ComputeShader = ShaderMap->GetShader<FMicropolyRasterizeCS>(PermutationVectorCS);

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
	FIntPoint TextureSize,
	EOutputBufferMode RasterMode,
	bool bClearTarget,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects )
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

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

	RasterContext.DepthBuffer	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("DepthBuffer32") );
	RasterContext.VisBuffer64	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("VisBuffer64") );
	RasterContext.DbgBuffer64	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32G32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("DbgBuffer64") );
	RasterContext.DbgBuffer32	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("DbgBuffer32") );
	RasterContext.LockBuffer	= GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D(RasterContext.TextureSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV), TEXT("LockBuffer") );
	
	const uint32 ClearValue[4] = { 0, 0, 0, 0 };

	if(RasterMode == EOutputBufferMode::DepthOnly)
	{
		RasterContext.Parameters.OutDepthBuffer = GraphBuilder.CreateUAV( RasterContext.DepthBuffer );
		if( bClearTarget )
		{
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.OutDepthBuffer, ClearValue, RectMinMaxBufferSRV, NumRects );
		}
	}
	else
	{
		RasterContext.Parameters.OutVisBuffer64 = GraphBuilder.CreateUAV( RasterContext.VisBuffer64 );
		if( bClearTarget )
		{
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.OutVisBuffer64, ClearValue, RectMinMaxBufferSRV, NumRects );
		}
		
		if( ShouldExportDebugBuffers() )
		{
			RasterContext.Parameters.OutDbgBuffer64 = GraphBuilder.CreateUAV( RasterContext.DbgBuffer64 );
			RasterContext.Parameters.OutDbgBuffer32 = GraphBuilder.CreateUAV( RasterContext.DbgBuffer32 );
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.OutDbgBuffer64, ClearValue, RectMinMaxBufferSRV, NumRects );
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.OutDbgBuffer32, ClearValue, RectMinMaxBufferSRV, NumRects );
		}

		if( RasterContext.RasterTechnique == ERasterTechnique::LockBufferFallback )
		{
			RasterContext.Parameters.LockBuffer = GraphBuilder.CreateUAV( RasterContext.LockBuffer );
			AddClearUAVPass( GraphBuilder, RasterContext.Parameters.LockBuffer, ClearValue, RectMinMaxBufferSRV, NumRects );
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
	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE( GraphBuilder, "Nanite::CullRasterize" );

	AddPassIfDebug(GraphBuilder, [](FRHICommandList&)
	{
		check(!Nanite::GStreamingManager.IsAsyncUpdateInProgress());
	});

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
		CullingParameters.HZBTexture	= RegisterExternalTextureWithFallback(GraphBuilder, CullingContext.PrevHZB, GSystemTextures.BlackDummy);
		CullingParameters.HZBSize		= CullingContext.PrevHZB ? CullingContext.PrevHZB->GetDesc().Extent : FVector2D(0.0f);
		CullingParameters.HZBSampler	= TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();
		CullingParameters.SOAStrides	= CullingContext.SOAStrides;
		CullingParameters.MaxCandidateClusters	= Nanite::FGlobalResources::GetMaxCandidateClusters();
		CullingParameters.MaxVisibleClusters	= Nanite::FGlobalResources::GetMaxVisibleClusters();
		CullingParameters.RenderFlags	= CullingContext.RenderFlags;
		CullingParameters.DebugFlags	= CullingContext.DebugFlags;
	}

	FVirtualTargetParameters VirtualTargetParameters;
	if (VirtualShadowMapArray)
	{
		VirtualTargetParameters.VirtualShadowMapCommon = VirtualShadowMapArray->CommonParameters;
		VirtualTargetParameters.PageFlags = GraphBuilder.CreateSRV(VirtualShadowMapArray->PageFlagsRDG, PF_R32_UINT);
		VirtualTargetParameters.HPageFlags = GraphBuilder.CreateSRV(VirtualShadowMapArray->HPageFlagsRDG, PF_R32_UINT);
		VirtualTargetParameters.PageTable = GraphBuilder.CreateSRV(VirtualShadowMapArray->PageTableRDG);
		VirtualTargetParameters.PageRectBounds = GraphBuilder.CreateSRV(VirtualShadowMapArray->PageRectBoundsRDG);
	}
	FGPUSceneParameters GPUSceneParameters;
	GPUSceneParameters.GPUSceneInstanceSceneData = Scene.GPUScene.InstanceDataBuffer.SRV;
	GPUSceneParameters.GPUScenePrimitiveSceneData = Scene.GPUScene.PrimitiveBuffer.SRV;
	GPUSceneParameters.GPUSceneFrameNumber = Scene.GPUScene.GetSceneFrameNumber();
	
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
		RasterContext,
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
			
			BuildHZB(
				GraphBuilder,
				SceneDepth,
				RasterizedDepth,
				CullingContext.HZBBuildViewRect,
				/* OutClosestHZBTexture = */ nullptr,
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
	LLM_SCOPE_BYTAG(Nanite);

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

			uint32 StreamingPriorityCategory = 0;
			uint32 ViewFlags = VIEW_FLAG_HZBTEST;
			MipView.StreamingPriorityCategory_AndFlags = (ViewFlags << NUM_STREAMING_PRIORITY_CATEGORY_BITS) | StreamingPriorityCategory;

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
	LLM_SCOPE_BYTAG(Nanite);

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
			PassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
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
			ConvertToExternalBuffer(GraphBuilder, CullingContext.MainPass.RasterizeArgsSWHW, MainPassBuffers.StatsRasterizeArgsSWHWBuffer);
			ConvertToExternalBuffer(GraphBuilder, CullingContext.MainPass.CandidateClustersArgs, MainPassBuffers.StatsCandidateClustersArgsBuffer);
		}

		// Extract post pass buffers
		auto& PostPassBuffers = Nanite::GGlobalResources.GetPostPassBuffers();
		PostPassBuffers.StatsRasterizeArgsSWHWBuffer = nullptr;
		PostPassBuffers.StatsCandidateClustersArgsBuffer = nullptr;
		if( CullingContext.bTwoPassOcclusion )
		{
			check( CullingContext.PostPass.RasterizeArgsSWHW != nullptr );
			check( CullingContext.PostPass.CandidateClustersArgs != nullptr );
			ConvertToExternalBuffer(GraphBuilder, CullingContext.PostPass.RasterizeArgsSWHW, PostPassBuffers.StatsRasterizeArgsSWHWBuffer);
			ConvertToExternalBuffer(GraphBuilder, CullingContext.PostPass.CandidateClustersArgs, PostPassBuffers.StatsCandidateClustersArgsBuffer);
		}

		// Extract calculated stats (so VisibleClustersSWHW isn't needed later)
		{
			ConvertToExternalBuffer(GraphBuilder, CullingContext.StatsBuffer, Nanite::GGlobalResources.GetStatsBufferRef());
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
	if (GNaniteDebugFlags != 0 && GNaniteShowStats != 0 && Nanite::GGlobalResources.GetStatsBufferRef())
	{
		auto& MainPassBuffers = Nanite::GGlobalResources.GetMainPassBuffers();
		auto& PostPassBuffers = Nanite::GGlobalResources.GetPostPassBuffers();

		{
			FPrintStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPrintStatsCS::FParameters>();

			ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintStruct);
			PassParameters->PackedClusterSize = sizeof(Nanite::FPackedCluster);

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
	LLM_SCOPE_BYTAG(Nanite);

	RasterResults.SOAStrides 	= CullingContext.SOAStrides;
	RasterResults.MaxVisibleClusters	= Nanite::FGlobalResources::GetMaxVisibleClusters();
	RasterResults.MaxNodes		= Nanite::FGlobalResources::GetMaxNodes();
	RasterResults.RenderFlags	= CullingContext.RenderFlags;

	ConvertToExternalBuffer(GraphBuilder, CullingContext.VisibleClustersSWHW, RasterResults.VisibleClustersSWHW);
	ConvertToExternalTexture(GraphBuilder, RasterContext.VisBuffer64, RasterResults.VisBuffer64);
	
	if (ShouldExportDebugBuffers())
	{
		ConvertToExternalTexture(GraphBuilder, RasterContext.DbgBuffer64, RasterResults.DbgBuffer64);
		ConvertToExternalTexture(GraphBuilder, RasterContext.DbgBuffer32, RasterResults.DbgBuffer32);
	}

	if (CullingContext.RenderFlags & RENDER_FLAG_OUTPUT_STREAMING_REQUESTS)
	{
		ConvertToExternalBuffer(GraphBuilder, CullingContext.StreamingRequests, GStreamingManager.GetStreamingRequestsBuffer());
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

	FRDGTextureRef VisBuffer64 = RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.VisBuffer64,	GSystemTextures.BlackDummy);

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

		PassParameters->RenderTargets[0]			= FRenderTargetBinding(HitProxyTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(HitProxyDepthTexture, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

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
	LLM_SCOPE_BYTAG(Nanite);

	check( DestRect.Width() == FVirtualShadowMap::PageSize );
	check( DestRect.Height() == FVirtualShadowMap::PageSize );

	auto ShaderMap = GetGlobalShaderMap( GMaxRHIFeatureLevel );

	auto* PassParameters = GraphBuilder.AllocParameters< FEmitShadowMapPS::FParameters >();

	PassParameters->CommonVSMParameters = VirtualShadowMapArray.CommonParameters;
	PassParameters->ViewToClip22 = ProjectionMatrix.M[2][2];
	PassParameters->DepthBias = DepthBias;
	PassParameters->ShadowMapID = ShadowMapID;
	PassParameters->SourceOffset = FIntPoint( -DestRect.Min.X, -DestRect.Min.Y );

	PassParameters->PageTable = GraphBuilder.CreateSRV(VirtualShadowMapArray.PageTableRDG);
	PassParameters->DepthBuffer = VirtualShadowMapArray.PhysicalPagePoolRDG;
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

void EmitDepthTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	TRefCountPtr<IPooledRenderTarget>& OutMaterialDepth,
	TRefCountPtr<IPooledRenderTarget>& OutNaniteMask,
	TRefCountPtr<IPooledRenderTarget>& OutVelocityBuffer
)
{
	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::EmitDepthTargets");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteDepth);

	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get();
	const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(SceneTargets.GetCurrentFeatureLevel());

	const FClearValueBinding DefaultDepthStencil = SceneTargets.GetDefaultDepthClear();
	float DefaultDepth = 0.0f;
	uint32 DefaultStencil = 0;
	DefaultDepthStencil.GetDepthStencil(DefaultDepth, DefaultStencil);

	const uint32 StencilDecalMask = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);

	// Nanite mask (TODO: unpacked right now, 7bits wasted per pixel).
	FRDGTextureDesc NaniteMaskDesc = FRDGTextureDesc::Create2D(
		SceneTargets.GetBufferSizeXY(),
		PF_R8_UINT,
		FClearValueBinding::Transparent,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureDesc VelocityBufferDesc = FVelocityRendering::GetRenderTargetDesc(ShaderPlatform, SceneTargets.GetBufferSizeXY());

	// TODO: Can be 16bit UNORM (PF_ShadowDepth) (32bit float w/ 8bit stencil is a waste of bandwidth and memory)
	FRDGTextureDesc MaterialDepthDesc = FRDGTextureDesc::Create2D(
		SceneTargets.GetBufferSizeXY(),
		PF_DepthStencil,
		SceneTargets.GetDefaultDepthClear(),
		TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead | (UseComputeDepthExport() ? TexCreate_UAV : TexCreate_None));

	FRDGTextureRef NaniteMask		= GraphBuilder.CreateTexture(NaniteMaskDesc, TEXT("NaniteMask"));
	FRDGTextureRef VelocityBuffer	= GraphBuilder.CreateTexture(VelocityBufferDesc, TEXT("NaniteVelocity"));
	FRDGTextureRef MaterialDepth	= GraphBuilder.CreateTexture(MaterialDepthDesc, TEXT("MaterialDepth"));
	FRDGTextureRef SceneDepth		= GraphBuilder.RegisterExternalTexture(SceneTargets.SceneDepthZ, TEXT("SceneDepth"));

	if (UseComputeDepthExport())
	{
		// Emit depth, stencil, and velocity mask

		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max, 8); // Only run DepthExport shader on viewport. We have already asserted that ViewRect.Min=0.
		const uint32 PlatformConfig = RHIGetHTilePlatformConfig(SceneTargets.GetBufferSizeXY().X, SceneTargets.GetBufferSizeXY().Y);

		FRDGTextureUAVRef SceneDepthUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef SceneStencilUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::Stencil));
		FRDGTextureUAVRef SceneHTileUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef MaterialDepthUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef MaterialHTileUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef VelocityUAV		= GraphBuilder.CreateUAV(VelocityBuffer);
		FRDGTextureUAVRef NaniteMaskUAV		= GraphBuilder.CreateUAV(NaniteMask);

		FDepthExportCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDepthExportCS::FParameters>();

		PassParameters->View					= View.ViewUniformBuffer;
		PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
		PassParameters->SOAStrides				= CullingContext.SOAStrides;
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->DepthExportConfig		= FIntVector4(PlatformConfig, SceneTargets.GetBufferSizeXY().X, StencilDecalMask, 0);
		PassParameters->VisBuffer64				= RasterContext.VisBuffer64;
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
			PassParameters->InViews						= GraphBuilder.CreateSRV(CullingContext.ViewsBuffer);
			PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
			PassParameters->SOAStrides					= CullingContext.SOAStrides;
			PassParameters->StencilClear				= DefaultStencil;
			PassParameters->StencilDecal				= StencilDecalMask;
			PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders			= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
			PassParameters->VisBuffer64					= RasterContext.VisBuffer64;
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
				GetGlobalShaderMap(View.FeatureLevel),
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
				PassParameters->InViews						= GraphBuilder.CreateSRV(CullingContext.ViewsBuffer);
				PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
				PassParameters->SOAStrides					= CullingContext.SOAStrides;
				PassParameters->VisBuffer64					= RasterContext.VisBuffer64;
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
					GetGlobalShaderMap(View.FeatureLevel),
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
				PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
				PassParameters->SOAStrides					= CullingContext.SOAStrides;
				PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV();
				PassParameters->ClusterPageHeaders			= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
				PassParameters->NaniteMask					= NaniteMask;
				PassParameters->VisBuffer64					= RasterContext.VisBuffer64;
				PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding
				(
					SceneDepth,
					ERenderTargetLoadAction::ELoad,
					FExclusiveDepthStencil::DepthWrite_StencilWrite
				);

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					GetGlobalShaderMap(View.FeatureLevel),
					RDG_EVENT_NAME("Emit Scene Stencil/Velocity"),
					PixelShader,
					PassParameters,
					View.ViewRect,
					TStaticBlendState<>::GetRHI(),
					TStaticRasterizerState<>::GetRHI(),
					TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
					StencilDecalMask
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
			PassParameters->SOAStrides					= CullingContext.SOAStrides;
			PassParameters->View						= View.ViewUniformBuffer;
			PassParameters->NaniteMask					= NaniteMask;
			PassParameters->VisBuffer64					= RasterContext.VisBuffer64;
			PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
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
				GetGlobalShaderMap(View.FeatureLevel),
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

	ConvertToExternalTexture(GraphBuilder, NaniteMask, OutNaniteMask);
	ConvertToExternalTexture(GraphBuilder, VelocityBuffer, OutVelocityBuffer);
	ConvertToExternalTexture(GraphBuilder, MaterialDepth, OutMaterialDepth);
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
	RDG_EVENT_SCOPE(GraphBuilder, "NaniteBasePass");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteMaterials);

	const int32 ViewWidth		= View.ViewRect.Max.X - View.ViewRect.Min.X;
	const int32 ViewHeight		= View.ViewRect.Max.Y - View.ViewRect.Min.Y;
	const FIntPoint ViewSize	= FIntPoint(ViewWidth, ViewHeight);

	FRenderTargetBindingSlots GBufferRenderTargets;
	SceneTextures.GetGBufferRenderTargets(ERenderTargetLoadAction::ELoad, GBufferRenderTargets);

	FRDGTextureRef MaterialDepth	= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.MaterialDepth, GSystemTextures.BlackDummy);
	FRDGTextureRef VisBuffer64		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.VisBuffer64,   GSystemTextures.BlackDummy);
	FRDGTextureRef DbgBuffer64		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.DbgBuffer64,   GSystemTextures.BlackDummy);
	FRDGTextureRef DbgBuffer32		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.DbgBuffer32,   GSystemTextures.BlackDummy);

	FRDGBufferRef VisibleClustersSWHW	= GraphBuilder.RegisterExternalBuffer(RasterResults.VisibleClustersSWHW);

	if (!FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(GMaxRHIShaderPlatform) &&
		(GNaniteMaterialCulling == 1 || GNaniteMaterialCulling == 2))
	{
		// Invalid culling method, platform does not support wave operations
		// Default to 64x64 tile grid method instead.
		GNaniteMaterialCulling = 4;
	}

	const bool b32BitMaskCulling = (GNaniteMaterialCulling == 1 || GNaniteMaterialCulling == 2);
	const bool bTileGridCulling  = (GNaniteMaterialCulling == 3 || GNaniteMaterialCulling == 4);

	const FIntPoint TileGridDim = bTileGridCulling ? FMath::DivideAndRoundUp(ViewSize, { 64, 64 }) : FIntPoint(1, 1);

	FRDGBufferDesc     VisibleMaterialsDesc	= FRDGBufferDesc::CreateStructuredDesc(4, b32BitMaskCulling ? FNaniteCommandInfo::MAX_STATE_BUCKET_ID+1 : 1);
	FRDGBufferRef      VisibleMaterials		= GraphBuilder.CreateBuffer(VisibleMaterialsDesc, TEXT("NaniteVisibleMaterials"));
	FRDGBufferUAVRef   VisibleMaterialsUAV	= GraphBuilder.CreateUAV(VisibleMaterials);
	FRDGTextureDesc    MaterialRangeDesc	= FRDGTextureDesc::Create2D(TileGridDim, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef     MaterialRange		= GraphBuilder.CreateTexture(MaterialRangeDesc, TEXT("NaniteMaterialRange"));
	FRDGTextureUAVRef  MaterialRangeUAV		= GraphBuilder.CreateUAV(MaterialRange);
	FRDGTextureSRVDesc MaterialRangeSRVDesc	= FRDGTextureSRVDesc::Create(MaterialRange);
	FRDGTextureSRVRef  MaterialRangeSRV		= GraphBuilder.CreateSRV(MaterialRangeSRVDesc);

	AddClearUAVPass(GraphBuilder, VisibleMaterialsUAV, 0);
	AddClearUAVPass(GraphBuilder, MaterialRangeUAV, { 0u, 1u, 0u, 0u });

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

		if (b32BitMaskCulling)
		{
			// TODO: Don't currently support offset views.
			checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));
			DispatchGroupSize = 8;
			PassParameters->ViewRect = FIntVector4(View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Max.X, View.ViewRect.Max.Y);
			PassParameters->VisibleMaterials = VisibleMaterialsUAV;

		}
		else if (bTileGridCulling)
		{
			DispatchGroupSize = 64;
			PassParameters->FetchClamp = View.ViewRect.Max - 1;
			PassParameters->MaterialRange = MaterialRangeUAV;
		}

		const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max, DispatchGroupSize);

		FClassifyMaterialsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FClassifyMaterialsCS::FCullingMethodDim>(GNaniteMaterialCulling);
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

		PassParameters->SOAStrides			= RasterResults.SOAStrides;
		PassParameters->MaxVisibleClusters	= RasterResults.MaxVisibleClusters;
		PassParameters->MaxNodes			= RasterResults.MaxNodes;
		PassParameters->RenderFlags			= RasterResults.RenderFlags;
			
		PassParameters->ClusterPageData		= Nanite::GStreamingManager.GetClusterPageDataSRV(); 
		PassParameters->ClusterPageHeaders	= Nanite::GStreamingManager.GetClusterPageHeadersSRV();

		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);

		PassParameters->MaterialRange = MaterialRange;
		PassParameters->VisibleMaterials = GraphBuilder.CreateSRV(VisibleMaterials, PF_R32_UINT);

		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->DbgBuffer64 = DbgBuffer64;
		PassParameters->DbgBuffer32 = DbgBuffer32;
		PassParameters->RenderTargets = GBufferRenderTargets;

		PassParameters->View = View.ViewUniformBuffer; // To get VTFeedbackBuffer
		PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, View, 0, {}, DBufferTextures, nullptr);

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
			PassParameters->GridSize = FMath::DivideAndRoundUp(View.ViewRect.Max, { 64, 64 });
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

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Emit GBuffer"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &Scene, ViewRect = View.ViewRect](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			FNaniteUniformParameters UniformParams;
			UniformParams.SOAStrides		= PassParameters->SOAStrides;
			UniformParams.MaxVisibleClusters= PassParameters->MaxVisibleClusters;
			UniformParams.MaxNodes			= PassParameters->MaxNodes;
			UniformParams.RenderFlags		= PassParameters->RenderFlags;

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
			UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

			UniformParams.MaterialRange = PassParameters->MaterialRange->GetRHI();
			UniformParams.VisibleMaterials = PassParameters->VisibleMaterials->GetRHI();

			UniformParams.VisBuffer64 = PassParameters->VisBuffer64->GetRHI();
			UniformParams.DbgBuffer64 = PassParameters->DbgBuffer64->GetRHI();
			UniformParams.DbgBuffer32 = PassParameters->DbgBuffer32->GetRHI();

			FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;

			TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator> NaniteMaterialPassCommands;
			BuildNaniteMaterialPassCommands(RHICmdList, Scene.NaniteDrawCommands[ENaniteMeshPass::BasePass], NaniteMaterialPassCommands);

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
		});
	}
}

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

	TRefCountPtr<IPooledRenderTarget> DebugVisualizationOutput;

	// Visualize Debug Views
	if (ShouldExportDebugBuffers())
	{
		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteDebug);

		FRDGTextureRef VisBuffer64		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.VisBuffer64,    GSystemTextures.BlackDummy);
		FRDGTextureRef DbgBuffer64		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.DbgBuffer64,    GSystemTextures.BlackDummy);
		FRDGTextureRef DbgBuffer32		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.DbgBuffer32,    GSystemTextures.BlackDummy);
		FRDGTextureRef NaniteMask		= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.NaniteMask,     GSystemTextures.BlackDummy);
		FRDGTextureRef VelocityBuffer	= RegisterExternalTextureWithFallback(GraphBuilder, RasterResults.VelocityBuffer, GSystemTextures.BlackDummy);

		FRDGBufferRef VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(RasterResults.VisibleClustersSWHW);

		FRDGTextureDesc DebugOutputDesc = FRDGTextureDesc::Create2D(
			View.ViewRect.Max,
			PF_A32B32G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef DebugOutput = GraphBuilder.CreateTexture(
			DebugOutputDesc,
			TEXT("NaniteDebug"));

		FDebugVisualizeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDebugVisualizeCS::FParameters>();

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

		auto ComputeShader = View.ShaderMap->GetShader<FDebugVisualizeCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DebugVisualize"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ViewSize, 8)
		);

		ConvertToExternalTexture(GraphBuilder, DebugOutput, DebugVisualizationOutput);
	}

	if (IsVisualizingHTile())
	{
		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get();

		FShaderResourceViewRHIRef HTileBufferRef;
		if (RasterResults.MaterialDepth)
		{
			FSceneRenderTargetItem& MaterialDepthRef = RasterResults.MaterialDepth->GetRenderTargetItem();
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
			FRDGTextureRef DebugOutput = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					SceneTargets.GetBufferSizeXY(),
					PF_A32B32G32R32F,
					FClearValueBinding::None,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("NaniteDebug"));

			FHTileVisualizeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHTileVisualizeCS::FParameters>();

			const uint32 PixelsWide = uint32(ViewSize.X);
			const uint32 PixelsTall = uint32(ViewSize.Y);
			const uint32 PlatformConfig = RHIGetHTilePlatformConfig(PixelsWide, PixelsTall);

			PassParameters->HTileBuffer  = HTileBufferRef;
			PassParameters->HTileDisplay = GraphBuilder.CreateUAV(DebugOutput);
			PassParameters->HTileConfig  = FIntVector4(PlatformConfig, PixelsWide, GNaniteDebugVisualize, 0);

			auto ComputeShader = View.ShaderMap->GetShader<FHTileVisualizeCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("HTileVisualize"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(ViewSize, 8)
			);

			ConvertToExternalTexture(GraphBuilder, DebugOutput, DebugVisualizationOutput);
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
	)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));
	checkSlow(DoesPlatformSupportLumenGI(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::DrawLumenMeshCapturePass");

	// Material range placeholder (not used by Lumen, but must still be bound)
	FRDGTextureDesc MaterialRangeDesc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource);
	FRDGTextureRef  MaterialRange = GraphBuilder.CreateTexture(MaterialRangeDesc, TEXT("NaniteMaterialRange"));
	//FRDGTextureSRVRef  MaterialRangeSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MaterialRange));

	FRDGTextureRef BlackTexture = GraphBuilder.RegisterExternalTexture( GSystemTextures.BlackDummy, TEXT("Black") );

	// Visible material mask buffer (currently not used by Lumen, but still must be bound)
	FRDGBufferDesc   VisibleMaterialsDesc = FRDGBufferDesc::CreateStructuredDesc(4, 1);
	FRDGBufferRef    VisibleMaterials     = GraphBuilder.CreateBuffer(VisibleMaterialsDesc, TEXT("NaniteVisibleMaterials"));
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
		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->SOAStrides			= CullingContext.SOAStrides;
		PassParameters->MaxVisibleClusters	= Nanite::FGlobalResources::GetMaxVisibleClusters();
		PassParameters->MaxNodes			= Nanite::FGlobalResources::GetMaxNodes();
		PassParameters->RenderFlags			= CullingContext.RenderFlags;
			
		PassParameters->ClusterPageData		= GStreamingManager.GetClusterPageDataSRV(); 
		PassParameters->ClusterPageHeaders	= GStreamingManager.GetClusterPageHeadersSRV();

		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);

		PassParameters->MaterialRange = MaterialRange;
		PassParameters->GridSize = { 1u, 1u };

		PassParameters->VisibleMaterials = GraphBuilder.CreateSRV(VisibleMaterials, PF_R32_UINT);

		PassParameters->VisBuffer64 = RasterContext.VisBuffer64;
		PassParameters->DbgBuffer64 = BlackTexture;
		PassParameters->DbgBuffer32 = BlackTexture;

		PassParameters->RenderTargets[0] = FRenderTargetBinding(AlbedoAtlasTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(NormalAtlasTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[2] = FRenderTargetBinding(EmissiveAtlasTexture, ERenderTargetLoadAction::ELoad);

		PassParameters->View = SharedView->ViewUniformBuffer; // To get VTFeedbackBuffer
		PassParameters->CardPass = GraphBuilder.CreateUniformBuffer(PassUniformParameters);

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
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
				UniformParams.MaxVisibleClusters = PassParameters->MaxVisibleClusters;
				UniformParams.MaxNodes = PassParameters->MaxNodes;
				UniformParams.RenderFlags = PassParameters->RenderFlags;
				UniformParams.MaterialConfig = FIntVector4(0, 1, 1, 0); // Tile based material culling is not required for Lumen, as each card is rendered as a small rect
				UniformParams.RectScaleOffset = FVector4(RectScale, RectOffset); // Render a rect that covers the card viewport

				UniformParams.ClusterPageData = PassParameters->ClusterPageData;
				UniformParams.ClusterPageHeaders = PassParameters->ClusterPageHeaders;

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

	FRDGTextureRef VisBuffer64 = RegisterExternalTextureWithFallback(GraphBuilder, NaniteRasterResults->VisBuffer64, GSystemTextures.BlackDummy);
	FRDGBufferRef VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(NaniteRasterResults->VisibleClustersSWHW);

	OutPassParameters->View						= View.ViewUniformBuffer;
	OutPassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
	OutPassParameters->MaxVisibleClusters		= Nanite::FGlobalResources::GetMaxVisibleClusters();
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

	FRDGTextureRef VisBuffer64 = RegisterExternalTextureWithFallback(GraphBuilder, NaniteRasterResults->VisBuffer64, GSystemTextures.BlackDummy);
	FRDGBufferRef VisibleClustersSWHW = GraphBuilder.RegisterExternalBuffer(NaniteRasterResults->VisibleClustersSWHW);

	OutPassParameters->View = View.ViewUniformBuffer;
	OutPassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);
	OutPassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
	OutPassParameters->SOAStrides = NaniteRasterResults->SOAStrides;
	OutPassParameters->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV();
	OutPassParameters->ClusterPageHeaders = Nanite::GStreamingManager.GetClusterPageHeadersSRV();
	OutPassParameters->VisBuffer64 = VisBuffer64;
	OutPassParameters->MaterialHitProxyTable = Scene.MaterialTables[ENaniteMeshPass::BasePass].GetHitProxyTableSRV();
	OutPassParameters->OutputToInputScale = FVector2D(View.ViewRect.Size()) / FVector2D(ViewportRect.Size());
}

void DrawEditorVisualizeLevelInstance(
	FRHICommandListImmediate& RHICmdList,
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
		GetGlobalShaderMap(View.FeatureLevel),
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