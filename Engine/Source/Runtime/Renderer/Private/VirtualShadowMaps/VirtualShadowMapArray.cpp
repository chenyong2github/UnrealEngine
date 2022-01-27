// Copyright Epic Games, Inc. All Rights Reserved.
/*=============================================================================
	VirtualShadowMap.h:
=============================================================================*/
#include "VirtualShadowMapArray.h"
#include "VirtualShadowMapVisualizationData.h"
#include "../BasePassRendering.h"
#include "../ScreenPass.h"
#include "Components/LightComponent.h"
#include "RendererModule.h"
#include "Rendering/NaniteResources.h"
#include "ShaderPrint.h"
#include "ShaderPrintParameters.h"
#include "VirtualShadowMapCacheManager.h"
#include "VirtualShadowMapClipmap.h"
#include "ComponentRecreateRenderStateContext.h"
#include "HairStrands/HairStrandsData.h"
#include "SceneTextureReductions.h"
#include "ShaderDebug.h"
#include "GPUMessaging.h"
#include "InstanceCulling/InstanceCullingMergedContext.h"

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(VirtualShadowMapUbSlot);

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FVirtualShadowMapUniformParameters, "VirtualShadowMap", VirtualShadowMapUbSlot);

struct FShadowMapCacheData
{
	int32 PrevVirtualShadowMapId = INDEX_NONE;
};


struct FPhysicalPageMetaData
{	
	uint32 Flags;
	uint32 Age;
};

int32 GEnableVirtualShadowMaps = 0;
FAutoConsoleVariableRef CVarEnableVirtualShadowMaps(
	TEXT("r.Shadow.Virtual.Enable"),
	GEnableVirtualShadowMaps,
	TEXT("Enable Virtual Shadow Maps."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		// Needed because the depth state changes with method (so cached draw commands must be re-created) see SetStateForShadowDepth
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMaxPhysicalPages(
	TEXT("r.Shadow.Virtual.MaxPhysicalPages"),
	2048,
	TEXT("Maximum number of physical pages in the pool."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarCacheStaticSeparate(
	TEXT("r.Shadow.Virtual.Cache.StaticSeparate"),
	0,
	TEXT("When enabled, caches static objects in separate pages from dynamic objects.\n")
	TEXT("This can improve performance in largely static scenes, but doubles the memory cost of the physical page pool."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarDebugVisualizeVirtualSms(
	TEXT("r.Shadow.Virtual.DebugVisualize"),
	0,
	TEXT("Set Debug Visualization method for virtual shadow maps, default is off (0).\n")
	TEXT("  To display the result also use the command 'vis VirtSmDebug'"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarShowStats(
	TEXT("r.Shadow.Virtual.ShowStats"),
	0,
	TEXT("ShowStats, also toggle shaderprint one!"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarResolutionLodBiasLocal(
	TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal"),
	0.0f,
	TEXT("Bias applied to LOD calculations for local lights. -1.0 doubles resolution, 1.0 halves it and so on."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarPageDilationBorderSizeDirectional(
	TEXT("r.Shadow.Virtual.PageDilationBorderSizeDirectional"),
	0.05f,
	TEXT("If a screen pixel falls within this fraction of a page border for directional lights, the adacent page will also be mapped.")
	TEXT("Higher values can reduce page misses at screen edges or disocclusions, but increase total page counts."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarPageDilationBorderSizeLocal(
	TEXT("r.Shadow.Virtual.PageDilationBorderSizeLocal"),
	0.05f,
	TEXT("If a screen pixel falls within this fraction of a page border for local lights, the adacent page will also be mapped.")
	TEXT("Higher values can reduce page misses at screen edges or disocclusions, but increase total page counts."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMarkPixelPages(
	TEXT("r.Shadow.Virtual.MarkPixelPages"),
	1,
	TEXT("Marks pages in virtual shadow maps based on depth buffer pixels. Ability to disable is primarily for profiling and debugging."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMarkCoarsePagesDirectional(
	TEXT("r.Shadow.Virtual.MarkCoarsePagesDirectional"),
	1,
	TEXT("Marks coarse pages in directional light virtual shadow maps so that low resolution data is available everywhere.")
	TEXT("Ability to disable is primarily for profiling and debugging."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMarkCoarsePagesLocal(
	TEXT("r.Shadow.Virtual.MarkCoarsePagesLocal"),
	1,
	TEXT("Marks coarse pages in local light virtual shadow maps so that low resolution data is available everywhere.")
	TEXT("Ability to disable is primarily for profiling and debugging."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarCoarsePagesIncludeNonNanite(
	TEXT("r.Shadow.Virtual.CoarsePagesIncludeNonNanite"),
	1,
	TEXT("Include non-nanite geometry in coarse pages.")
	TEXT("Rendering non-nanite geometry into large coarse pages can be expensive; disabling this can be a significant performance win."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarShowClipmapStats(
	TEXT("r.Shadow.Virtual.ShowClipmapStats"),
	-1,
	TEXT("Set to the number of clipmap you want to show stats for (-1 == off)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarCullBackfacingPixels(
	TEXT("r.Shadow.Virtual.CullBackfacingPixels"),
	1,
	TEXT("When enabled does not generate shadow data for pixels that are backfacing to the light."),
	ECVF_RenderThreadSafe
);

int32 GEnableNonNaniteVSM = 1;
FAutoConsoleVariableRef CVarEnableNonNaniteVSM(
	TEXT("r.Shadow.Virtual.NonNaniteVSM"),
	GEnableNonNaniteVSM,
	TEXT("Enable support for non-nanite Virtual Shadow Maps.")
	TEXT("Read-only and to be set in a config file (requires restart)."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static TAutoConsoleVariable<int32> CVarNonNaniteVsmUseHzb(
	TEXT("r.Shadow.Virtual.NonNanite.UseHZB"),
	2,
	TEXT("Cull Non-Nanite instances using HZB. If set to 2, attempt to use Nanite-HZB from the current frame."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarInitializePhysicalUsingIndirect(
	TEXT("r.Shadow.Virtual.InitPhysicalUsingIndirect"),
	1,
	TEXT("."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMergePhysicalUsingIndirect(
	TEXT("r.Shadow.Virtual.MergePhysicalUsingIndirect"),
	1,
	TEXT("."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarVirtualShadowOnePassProjectionMaxLights(
	TEXT("r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel"),
	16,
	TEXT("Maximum lights per pixel that get full filtering when using one pass projection and clustered shading.")
	TEXT("Generally set to 8 (32bpp), 16 (64bpp) or 32 (128bpp). Lower values require less transient VRAM during the lighting pass."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarDoNonNaniteBatching(
	TEXT("r.Shadow.Virtual.NonNanite.Batch"),
	1,
	TEXT("."),
	ECVF_RenderThreadSafe
);

#if !UE_BUILD_SHIPPING
bool GDumpVSMLightNames = false;
void DumpVSMLightNames()
{
	ENQUEUE_RENDER_COMMAND(DumpVSMLightNames)(
		[](FRHICommandList& RHICmdList)
		{
			GDumpVSMLightNames = true;
		});
}

FAutoConsoleCommand CmdDumpVSMLightNames(
	TEXT("r.Shadow.Virtual.Visualize.DumpLightNames"),
	TEXT("Dump light names with virtual shadow maps (for developer use in non-shiping builds)"),
	FConsoleCommandDelegate::CreateStatic(DumpVSMLightNames)
);

FString GVirtualShadowMapVisualizeLightName;
FAutoConsoleVariableRef CVarVisualizeLightName(
	TEXT("r.Shadow.Virtual.Visualize.LightName"),
	GVirtualShadowMapVisualizeLightName,
	TEXT("Sets the name of a specific light to visualize (for developer use in non-shiping builds)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVisualizeLayout(
	TEXT("r.Shadow.Virtual.Visualize.Layout"),
	0,
	TEXT("Overlay layout when virtual shadow map visualization is enabled:\n")
	TEXT("  0: Full screen\n")
	TEXT("  1: Thumbnail\n")
	TEXT("  2: Split screen"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarDebugSkipMergePhysical(
	TEXT("r.Shadow.Virtual.DebugSkipMergePhysical"),
	0,
	TEXT(""),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarDebugSkipDynamicPageInvalidation(
	TEXT("r.Shadow.Virtual.DebugSkipDynamicPageInvalidation"),
	0,
	TEXT("Invalidate cached pages when geometry moves.\n")
	TEXT("This should be left enabled except for targeted profiling, as disabling it will produce artifacts with moving geometry."),
	ECVF_RenderThreadSafe
);
#endif // !UE_BUILD_SHIPPING

TAutoConsoleVariable<int32> CVarVSMBuildHZBPerPage(
	TEXT("r.Shadow.Virtual.BuildHZBPerPage"),
	1,
	TEXT("."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarVSMHZB32Bit(
	TEXT("r.Shadow.Virtual.HZB32Bit"),
	1,
	TEXT("Use a full 32-bit HZB buffer. This uses more memory but can offer more precise culling in cases with lots of overlapping detailed geometry."),
	ECVF_RenderThreadSafe
);


static TAutoConsoleVariable<float> CVarMaxMaterialPositionInvalidationRange(
	TEXT("r.Shadow.Virtual.MaxMaterialPositionInvalidationRange"),
	-1.0f,
	TEXT("Beyond this distance in world units, material position effects (e.g., WPO or PDO) cease to cause VSM invalidations.\n")
	TEXT(" This can be used to tune performance by reducing re-draw overhead, but causes some artifacts.\n")
	TEXT(" < 0 <=> infinite (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

FMatrix CalcTranslatedWorldToShadowUVMatrix(
	const FMatrix& TranslatedWorldToShadowView,
	const FMatrix& ViewToClip)
{
	FMatrix TranslatedWorldToShadowClip = TranslatedWorldToShadowView * ViewToClip;
	FMatrix ScaleAndBiasToSmUV = FScaleMatrix(FVector(0.5f, -0.5f, 1.0f)) * FTranslationMatrix(FVector(0.5f, 0.5f, 0.0f));
	FMatrix TranslatedWorldToShadowUv = TranslatedWorldToShadowClip * ScaleAndBiasToSmUV;
	return TranslatedWorldToShadowUv;
}

FMatrix CalcTranslatedWorldToShadowUVNormalMatrix(
	const FMatrix& TranslatedWorldToShadowView,
	const FMatrix& ViewToClip)
{
	return CalcTranslatedWorldToShadowUVMatrix(TranslatedWorldToShadowView, ViewToClip).GetTransposed().Inverse();
}

FVirtualShadowMapArray::FVirtualShadowMapArray()
{
}

BEGIN_SHADER_PARAMETER_STRUCT(FCacheDataParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FShadowMapCacheData >,	ShadowMapCacheData )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,					PrevPageFlags )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,					PrevPageTable )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FPhysicalPageMetaData >,	PrevPhysicalPageMetaData)
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,					PrevDynamicCasterPageFlags)
END_SHADER_PARAMETER_STRUCT()

static void SetCacheDataShaderParameters(FRDGBuilder& GraphBuilder, const TArray<FVirtualShadowMap*, SceneRenderingAllocator> &ShadowMaps, FVirtualShadowMapArrayCacheManager* CacheManager, FCacheDataParameters &CacheDataParameters)
{
	TArray<FShadowMapCacheData, SceneRenderingAllocator> ShadowMapCacheData;
	ShadowMapCacheData.AddDefaulted(ShadowMaps.Num());
	for (int32 SmIndex = 0; SmIndex < ShadowMaps.Num(); ++SmIndex)
	{
		TSharedPtr<FVirtualShadowMapCacheEntry> VirtualShadowMapCacheEntry = ShadowMaps[SmIndex]->VirtualShadowMapCacheEntry;
		if (VirtualShadowMapCacheEntry != nullptr && VirtualShadowMapCacheEntry->IsValid())
		{
			ShadowMapCacheData[SmIndex].PrevVirtualShadowMapId = VirtualShadowMapCacheEntry->PrevVirtualShadowMapId;
		}
		else
		{
			ShadowMapCacheData[SmIndex].PrevVirtualShadowMapId = INDEX_NONE;
		}
	}
	CacheDataParameters.ShadowMapCacheData = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.ShadowMapCacheData"), ShadowMapCacheData));
	CacheDataParameters.PrevPageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PageFlags, TEXT("Shadow.Virtual.PrevPageFlags")));
	CacheDataParameters.PrevPageTable = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PageTable, TEXT("Shadow.Virtual.PrevPageTable")));
	CacheDataParameters.PrevPhysicalPageMetaData = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PhysicalPageMetaData, TEXT("Shadow.Virtual.PrevPhysicalPageMetaData")));
	CacheDataParameters.PrevDynamicCasterPageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.DynamicCasterPageFlags, TEXT("Shadow.Virtual.PrevDynamicCasterPageFlags")));
}

static FRDGBufferRef CreateProjectionDataBuffer(
	FRDGBuilder& GraphBuilder,
	const TCHAR* Name,
	const TArray<FVirtualShadowMapProjectionShaderData, SceneRenderingAllocator>& InitialData)
{
	uint64 DataSize = InitialData.Num() * InitialData.GetTypeSize();

	FRDGBufferDesc Desc;
	Desc.UnderlyingType = FRDGBufferDesc::EUnderlyingType::StructuredBuffer;
	Desc.Usage = (EBufferUsageFlags)(BUF_UnorderedAccess | BUF_ShaderResource | BUF_ByteAddressBuffer);
	Desc.BytesPerElement = 4;
	Desc.NumElements = DataSize / 4;

	FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(Desc, Name);
	GraphBuilder.QueueBufferUpload(Buffer, InitialData.GetData(), DataSize);
	return Buffer;
}

void FVirtualShadowMapArray::Initialize(FRDGBuilder& GraphBuilder, FVirtualShadowMapArrayCacheManager* InCacheManager, bool bInEnabled)
{
	bInitialized = true;
	bEnabled = bInEnabled;
	CacheManager = InCacheManager;
	check(!bEnabled || CacheManager);

	bCullBackfacingPixels = CVarCullBackfacingPixels.GetValueOnRenderThread() != 0;

	UniformParameters.NumShadowMaps = 0;
	UniformParameters.NumDirectionalLights = 0;

	// Fixed physical page pool width, we adjust the height to accomodate the requested maximum
	// NOTE: Row size in pages has to be POT since we use mask & shift in place of integer ops
	const uint32 PhysicalPagesX = FMath::DivideAndRoundDown(GetMax2DTextureDimension(), FVirtualShadowMap::PageSize);
	check(FMath::IsPowerOfTwo(PhysicalPagesX));
	uint32 PhysicalPagesY = FMath::DivideAndRoundUp((uint32)FMath::Max(1, CVarMaxPhysicalPages.GetValueOnRenderThread()), PhysicalPagesX);	

	UniformParameters.MaxPhysicalPages = PhysicalPagesX * PhysicalPagesY;

	if (CacheManager->IsValid() && CVarCacheStaticSeparate.GetValueOnRenderThread() != 0)
	{
		// Store the static pages below the dynamic/merged pages
		UniformParameters.StaticCachedPixelOffsetY = PhysicalPagesY * FVirtualShadowMap::PageSize;
		// Offset to static pages in linear page index
		UniformParameters.StaticPageIndexOffset = PhysicalPagesY * PhysicalPagesX;
		PhysicalPagesY *= 2;
	}
	else
	{
		UniformParameters.StaticCachedPixelOffsetY = 0;
		UniformParameters.StaticPageIndexOffset = 0;
	}

	uint32 PhysicalX = PhysicalPagesX * FVirtualShadowMap::PageSize;
	uint32 PhysicalY = PhysicalPagesY * FVirtualShadowMap::PageSize;

	// TODO: Some sort of better fallback with warning?
	// All supported platforms support at least 16384 texture dimensions which translates to 16384 max pages with default 128x128 page size
	check(PhysicalX <= GetMax2DTextureDimension());
	check(PhysicalY <= GetMax2DTextureDimension());

	UniformParameters.PhysicalPageRowMask = (PhysicalPagesX - 1);
	UniformParameters.PhysicalPageRowShift = FMath::FloorLog2( PhysicalPagesX );
	UniformParameters.RecPhysicalPoolSize = FVector4f( 1.0f / PhysicalX, 1.0f / PhysicalY, 1.0f, 1.0f );
	UniformParameters.PhysicalPoolSize = FIntPoint( PhysicalX, PhysicalY );
	UniformParameters.PhysicalPoolSizePages = FIntPoint( PhysicalPagesX, PhysicalPagesY );

	// TODO: Parameterize this in a useful way; potentially modify it automatically
	// when there are fewer lights in the scene and/or clustered shading settings differ.
	UniformParameters.PackedShadowMaskMaxLightCount = FMath::Min(CVarVirtualShadowOnePassProjectionMaxLights.GetValueOnRenderThread(), 32);

	// Reference dummy data in the UB initially
	const uint32 DummyPageTableElement = 0xFFFFFFFF;
	UniformParameters.PageTable = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(DummyPageTableElement), DummyPageTableElement));
	UniformParameters.ProjectionData = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FVirtualShadowMapProjectionShaderData)));
	UniformParameters.PageFlags = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32)));
	UniformParameters.PageRectBounds = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FIntVector4)));

	if (bEnabled)
	{
		// If enabled, ensure we have a properly-sized physical page pool
		// We can do this here since the pool is independent of the number of shadow maps
		TRefCountPtr<IPooledRenderTarget> PhysicalPagePool = CacheManager->SetPhysicalPoolSize(GraphBuilder, GetPhysicalPoolSize());
		PhysicalPagePoolRDG = GraphBuilder.RegisterExternalTexture(PhysicalPagePool);
		UniformParameters.PhysicalPagePool = PhysicalPagePoolRDG;
	}
	else
	{
		CacheManager->FreePhysicalPool();
		UniformParameters.PhysicalPagePool = GSystemTextures.GetZeroUIntDummy(GraphBuilder);
	}	
}

FVirtualShadowMapArray::~FVirtualShadowMapArray()
{
	for (FVirtualShadowMap *SM : ShadowMaps)
	{
		SM->~FVirtualShadowMap();
	}
}

EPixelFormat FVirtualShadowMapArray::GetPackedShadowMaskFormat() const
{
	// TODO: Check if we're after any point that determines the format later too (light setup)
	check(bInitialized);
	// NOTE: Currently 4bpp/light
	if (UniformParameters.PackedShadowMaskMaxLightCount <= 8)
	{
		return PF_R32_UINT;
	}
	else if (UniformParameters.PackedShadowMaskMaxLightCount <= 16)
	{
		return PF_R32G32_UINT;
	}
	else
	{
		check(UniformParameters.PackedShadowMaskMaxLightCount <= 32);
		return PF_R32G32B32A32_UINT;
	}
}

FIntPoint FVirtualShadowMapArray::GetPhysicalPoolSize() const
{
	check(bInitialized);
	return FIntPoint(UniformParameters.PhysicalPoolSize.X, UniformParameters.PhysicalPoolSize.Y);
}

FIntPoint FVirtualShadowMapArray::GetDynamicPhysicalPoolSize() const
{
	check(bInitialized);
	return FIntPoint(
		UniformParameters.PhysicalPoolSize.X,
		ShouldCacheStaticSeparately() ? UniformParameters.StaticCachedPixelOffsetY : UniformParameters.PhysicalPoolSize.Y);
}

uint32 FVirtualShadowMapArray::GetTotalAllocatedPhysicalPages() const
{
	check(bInitialized);
	return ShouldCacheStaticSeparately() ? (2U * UniformParameters.MaxPhysicalPages) : UniformParameters.MaxPhysicalPages;
}


TRDGUniformBufferRef<FVirtualShadowMapUniformParameters> FVirtualShadowMapArray::GetUniformBuffer(FRDGBuilder& GraphBuilder) const
{
	// NOTE: Need to allocate new parameter space since the UB changes over the frame as dummy references are replaced
	// TODO: Should we be caching this once all the relevant updates to parameters have been made in a frame?
	FVirtualShadowMapUniformParameters* VersionedParameters = GraphBuilder.AllocParameters<FVirtualShadowMapUniformParameters>();
	*VersionedParameters = UniformParameters;
	return GraphBuilder.CreateUniformBuffer(VersionedParameters);
}

void FVirtualShadowMapArray::SetShaderDefines(FShaderCompilerEnvironment& OutEnvironment)
{
	static_assert(FVirtualShadowMap::Log2Level0DimPagesXY * 2U + MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS <= 32U, "Page indirection plus view index must fit into 32-bits for page-routing storage!");
	OutEnvironment.SetDefine(TEXT("ENABLE_NON_NANITE_VSM"), GEnableNonNaniteVSM);
	OutEnvironment.SetDefine(TEXT("VSM_PAGE_SIZE"), FVirtualShadowMap::PageSize);
	OutEnvironment.SetDefine(TEXT("VSM_PAGE_SIZE_MASK"), FVirtualShadowMap::PageSizeMask);
	OutEnvironment.SetDefine(TEXT("VSM_LOG2_PAGE_SIZE"), FVirtualShadowMap::Log2PageSize);
	OutEnvironment.SetDefine(TEXT("VSM_LEVEL0_DIM_PAGES_XY"), FVirtualShadowMap::Level0DimPagesXY);
	OutEnvironment.SetDefine(TEXT("VSM_LOG2_LEVEL0_DIM_PAGES_XY"), FVirtualShadowMap::Log2Level0DimPagesXY);
	OutEnvironment.SetDefine(TEXT("VSM_MAX_MIP_LEVELS"), FVirtualShadowMap::MaxMipLevels);
	OutEnvironment.SetDefine(TEXT("VSM_VIRTUAL_MAX_RESOLUTION_XY"), FVirtualShadowMap::VirtualMaxResolutionXY);
	OutEnvironment.SetDefine(TEXT("VSM_RASTER_WINDOW_PAGES"), FVirtualShadowMap::RasterWindowPages);
	OutEnvironment.SetDefine(TEXT("VSM_PAGE_TABLE_SIZE"), FVirtualShadowMap::PageTableSize);
	OutEnvironment.SetDefine(TEXT("VSM_NUM_STATS"), NumStats);
	OutEnvironment.SetDefine(TEXT("INDEX_NONE"), INDEX_NONE);
}

FVirtualShadowMapSamplingParameters FVirtualShadowMapArray::GetSamplingParameters(FRDGBuilder& GraphBuilder) const
{
	// Sanity check: either VSMs are disabled and it's expected to be relying on dummy data, or we should have valid data
	// If this fires, it is likely because the caller is trying to sample VSMs before they have been rendered by the ShadowDepths pass
	// This should not crash, but it is not an intended production path as it will not return valid shadow data.
	// TODO: Disabled warning until SkyAtmosphereLUT is moved after ShadowDepths
	//ensureMsgf(!IsEnabled() || IsAllocated(),
	//	TEXT("Attempt to use Virtual Shadow Maps before they have been rendered by ShadowDepths."));

	FVirtualShadowMapSamplingParameters Parameters;
	Parameters.VirtualShadowMap = GetUniformBuffer(GraphBuilder);
	return Parameters;
}

class FVirtualPageManagementShader : public FGlobalShader
{
public:
	// Kernel launch group sizes
	static constexpr uint32 DefaultCSGroupXY = 8;
	static constexpr uint32 DefaultCSGroupX = 256;
	static constexpr uint32 GeneratePageFlagsGroupXYZ = 4;
	static constexpr uint32 BuildExplicitBoundsGroupXY = 16;

	FVirtualPageManagementShader()
	{
	}

	FVirtualPageManagementShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportNanite(Parameters.Platform);
	}

	/**
	* Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	*/
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("VSM_DEFAULT_CS_GROUP_X"), DefaultCSGroupX);
		OutEnvironment.SetDefine(TEXT("VSM_DEFAULT_CS_GROUP_XY"), DefaultCSGroupXY);
		OutEnvironment.SetDefine(TEXT("VSM_GENERATE_PAGE_FLAGS_CS_GROUP_XYZ"), GeneratePageFlagsGroupXYZ);
		OutEnvironment.SetDefine(TEXT("VSM_BUILD_EXPLICIT_BOUNDS_CS_XY"), BuildExplicitBoundsGroupXY);

		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	}
};


class FGeneratePageFlagsFromPixelsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FGeneratePageFlagsFromPixelsCS);
	SHADER_USE_PARAMETER_STRUCT(FGeneratePageFlagsFromPixelsCS, FVirtualPageManagementShader)

	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 3); 
	using FPermutationDomain = TShaderPermutationDomain<FInputType>;
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, VisBuffer64)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPageRequestFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, DirectionalLightIds)
		RDG_BUFFER_ACCESS(IndirectBufferArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER(uint32, InputType)
		SHADER_PARAMETER(uint32, NumDirectionalLightSmInds)
		SHADER_PARAMETER(uint32, bPostBasePass)
		SHADER_PARAMETER(float, ResolutionLodBiasLocal)
		SHADER_PARAMETER(float, PageDilationBorderSizeDirectional)
		SHADER_PARAMETER(float, PageDilationBorderSizeLocal)
		SHADER_PARAMETER(uint32, bCullBackfacingPixels)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGeneratePageFlagsFromPixelsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "GeneratePageFlagsFromPixels", SF_Compute);

class FMarkCoarsePagesCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FMarkCoarsePagesCS);
	SHADER_USE_PARAMETER_STRUCT(FMarkCoarsePagesCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPageRequestFlags)
		SHADER_PARAMETER(uint32, bMarkCoarsePagesLocal)
		SHADER_PARAMETER(uint32, bIncludeNonNaniteGeometry)
		SHADER_PARAMETER(uint32, ClipmapIndexMask)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMarkCoarsePagesCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "MarkCoarsePages", SF_Compute);


class FGenerateHierarchicalPageFlagsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FGenerateHierarchicalPageFlagsCS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateHierarchicalPageFlagsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPageFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, OutPageRectBounds)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGenerateHierarchicalPageFlagsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "GenerateHierarchicalPageFlags", SF_Compute);


class FInitPhysicalPageMetaData : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitPhysicalPageMetaData);
	SHADER_USE_PARAMETER_STRUCT(FInitPhysicalPageMetaData, FVirtualPageManagementShader )

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPhysicalPageMetaData >,	OutPhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutFreePhysicalPages )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitPhysicalPageMetaData, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitPhysicalPageMetaData", SF_Compute );

class FCreateCachedPageMappingsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FCreateCachedPageMappingsCS);
	SHADER_USE_PARAMETER_STRUCT(FCreateCachedPageMappingsCS, FVirtualPageManagementShader)

	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS"); 
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim, FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER( FVirtualShadowMapUniformParameters,		VirtualShadowMap )
		SHADER_PARAMETER_STRUCT_INCLUDE( FCacheDataParameters,							CacheDataParameters )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,						PageRequestFlags )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageTable )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPhysicalPageMetaData >,	OutPhysicalPageMetaData )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutStatsBuffer )
		SHADER_PARAMETER( int32,														bDynamicPageInvalidation )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCreateCachedPageMappingsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "CreateCachedPageMappings", SF_Compute);

class FPackFreePagesCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FPackFreePagesCS);
	SHADER_USE_PARAMETER_STRUCT(FPackFreePagesCS, FVirtualPageManagementShader )

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters,			VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FPhysicalPageMetaData >,		PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutFreePhysicalPages )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPackFreePagesCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "PackFreePages", SF_Compute );

class FAllocateNewPageMappingsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FAllocateNewPageMappingsCS);
	SHADER_USE_PARAMETER_STRUCT(FAllocateNewPageMappingsCS, FVirtualPageManagementShader)

	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS"); 
	using FPermutationDomain = TShaderPermutationDomain<FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER( FVirtualShadowMapUniformParameters,		VirtualShadowMap )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,						PageRequestFlags )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutFreePhysicalPages )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageTable )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPhysicalPageMetaData >,	OutPhysicalPageMetaData )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutStatsBuffer )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FAllocateNewPageMappingsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "AllocateNewPageMappings", SF_Compute);

class FPropagateMappedMipsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FPropagateMappedMipsCS);
	SHADER_USE_PARAMETER_STRUCT(FPropagateMappedMipsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER( FVirtualShadowMapUniformParameters,	VirtualShadowMap )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				OutPageTable )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPropagateMappedMipsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "PropagateMappedMips", SF_Compute);

class FInitializePhysicalPagesCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitializePhysicalPagesCS);
	SHADER_USE_PARAMETER_STRUCT(FInitializePhysicalPagesCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,	PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutPhysicalPagePool)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitializePhysicalPagesCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitializePhysicalPages", SF_Compute);

class FSelectPagesToInitializeCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FSelectPagesToInitializeCS);
	SHADER_USE_PARAMETER_STRUCT(FSelectPagesToInitializeCS, FVirtualPageManagementShader)

	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	using FPermutationDomain = TShaderPermutationDomain<FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhysicalPageMetaData>, PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutInitializePagesIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPhysicalPagesToInitialize)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectPagesToInitializeCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "SelectPagesToInitializeCS", SF_Compute);

class FInitializePhysicalPagesIndirectCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitializePhysicalPagesIndirectCS);
	SHADER_USE_PARAMETER_STRUCT(FInitializePhysicalPagesIndirectCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PhysicalPagesToInitialize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutPhysicalPagePool)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitializePhysicalPagesIndirectCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitializePhysicalPagesIndirectCS", SF_Compute);

class FClearIndirectDispatchArgs1DCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FClearIndirectDispatchArgs1DCS);
	SHADER_USE_PARAMETER_STRUCT(FClearIndirectDispatchArgs1DCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumIndirectArgs)
		SHADER_PARAMETER(uint32, IndirectArgStride)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutIndirectArgsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FClearIndirectDispatchArgs1DCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "ClearIndirectDispatchArgs1DCS", SF_Compute);

static void AddClearIndirectDispatchArgs1DPass(FRDGBuilder& GraphBuilder, FRDGBufferRef IndirectArgsRDG, uint32 NumIndirectArgs = 1U, uint32 IndirectArgStride = 4U)
{
	FClearIndirectDispatchArgs1DCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearIndirectDispatchArgs1DCS::FParameters>();
	PassParameters->NumIndirectArgs = NumIndirectArgs;
	PassParameters->IndirectArgStride = IndirectArgStride;
	PassParameters->OutIndirectArgsBuffer = GraphBuilder.CreateUAV(IndirectArgsRDG);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FClearIndirectDispatchArgs1DCS>();

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ClearIndirectDispatchArgs"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(NumIndirectArgs, 64)
	);
}

class FMergeStaticPhysicalPagesCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FMergeStaticPhysicalPagesCS);
	SHADER_USE_PARAMETER_STRUCT(FMergeStaticPhysicalPagesCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,	PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutPhysicalPagePool)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMergeStaticPhysicalPagesCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "MergeStaticPhysicalPages", SF_Compute);

class FSelectPagesToMergeCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FSelectPagesToMergeCS);
	SHADER_USE_PARAMETER_STRUCT(FSelectPagesToMergeCS, FVirtualPageManagementShader)

	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	using FPermutationDomain = TShaderPermutationDomain<FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhysicalPageMetaData>, PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutMergePagesIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPhysicalPagesToMerge)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectPagesToMergeCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "SelectPagesToMergeCS", SF_Compute);

class FMergeStaticPhysicalPagesIndirectCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FMergeStaticPhysicalPagesIndirectCS);
	SHADER_USE_PARAMETER_STRUCT(FMergeStaticPhysicalPagesIndirectCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PhysicalPagesToMerge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutPhysicalPagePool)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMergeStaticPhysicalPagesIndirectCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "MergeStaticPhysicalPagesIndirectCS", SF_Compute);



void FVirtualShadowMapArray::MergeStaticPhysicalPages(FRDGBuilder& GraphBuilder)
{
	check(IsEnabled());
	if (ShadowMaps.Num() == 0 || !ShouldCacheStaticSeparately())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (CVarDebugSkipMergePhysical.GetValueOnRenderThread() != 0)
	{
		return;
	}
#endif

	RDG_EVENT_SCOPE(GraphBuilder, "FVirtualShadowMapArray::MergeStaticPhysicalPages");
	if (CVarMergePhysicalUsingIndirect.GetValueOnRenderThread() != 0)
	{
		FRDGBufferRef MergePagesIndirectArgsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(3), TEXT("Shadow.Virtual.MergePagesIndirectArgs"));
		// Note: We use GetTotalAllocatedPhysicalPages() to size the buffer as the selection shader emits both static/dynamic pages separately when enabled.
		FRDGBufferRef PhysicalPagesToMergeRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), GetTotalAllocatedPhysicalPages() + 1), TEXT("Shadow.Virtual.PhysicalPagesToMerge"));

		// 1. Initialize the indirect args buffer
		AddClearIndirectDispatchArgs1DPass(GraphBuilder, MergePagesIndirectArgsRDG);
		// 2. Filter the relevant physical pages and set up the indirect args
		{
			FSelectPagesToMergeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectPagesToMergeCS::FParameters>();
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
			PassParameters->OutMergePagesIndirectArgsBuffer = GraphBuilder.CreateUAV(MergePagesIndirectArgsRDG);
			PassParameters->OutPhysicalPagesToMerge = GraphBuilder.CreateUAV(PhysicalPagesToMergeRDG);
			bool bGenerateStats = StatsBufferRDG != nullptr;
			if (bGenerateStats)
			{
				PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(StatsBufferRDG);
			}
			FSelectPagesToMergeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSelectPagesToMergeCS::FGenerateStatsDim>(bGenerateStats);

			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FSelectPagesToMergeCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SelectPagesToMerge"),
				ComputeShader,
				PassParameters,
				FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FSelectPagesToMergeCS::DefaultCSGroupX), 1, 1)
			);

		}
		// 3. Indirect dispatch to clear the selected pages
		{
			FMergeStaticPhysicalPagesIndirectCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMergeStaticPhysicalPagesIndirectCS::FParameters>();
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->OutPhysicalPagePool = GraphBuilder.CreateUAV(PhysicalPagePoolRDG);
			PassParameters->IndirectArgs = MergePagesIndirectArgsRDG;
			PassParameters->PhysicalPagesToMerge = GraphBuilder.CreateSRV(PhysicalPagesToMergeRDG);
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FMergeStaticPhysicalPagesIndirectCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("MergeStaticPhysicalPagesIndirect"),
				ComputeShader,
				PassParameters,
				PassParameters->IndirectArgs,
				0
			);
		}
	}
	else
	{
		FMergeStaticPhysicalPagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMergeStaticPhysicalPagesCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
		PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
		PassParameters->OutPhysicalPagePool = GraphBuilder.CreateUAV(PhysicalPagePoolRDG);		

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FMergeStaticPhysicalPagesCS>();

		// Shader contains logic to deal with static cached pages if enabled
		// We only need to launch one per page, even if there are multiple cached pages per page
		FIntPoint DynamicPoolSize = GetDynamicPhysicalPoolSize();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MergeStaticPhysicalPages"),
			ComputeShader,
			PassParameters,
			FIntVector(
				FMath::DivideAndRoundUp(DynamicPoolSize.X, 16),
				FMath::DivideAndRoundUp(DynamicPoolSize.Y, 16),
				1)
		);
	}
}

/**
 * Helper to get hold of / check for associated virtual shadow map
 */
FORCEINLINE FProjectedShadowInfo* GetVirtualShadowMapInfo(const FVisibleLightInfo &LightInfo)
{
	for (FProjectedShadowInfo *ProjectedShadowInfo : LightInfo.AllProjectedShadows)
	{
		if (ProjectedShadowInfo->HasVirtualShadowMap())
		{
			return ProjectedShadowInfo;
		}
	}

	return nullptr;
}


class FInitPageRectBoundsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitPageRectBoundsCS);
	SHADER_USE_PARAMETER_STRUCT(FInitPageRectBoundsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, OutPageRectBounds)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitPageRectBoundsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitPageRectBounds", SF_Compute);



class FVirtualSmFeedbackStatusCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmFeedbackStatusCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmFeedbackStatusCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, FreePhysicalPages)
		SHADER_PARAMETER_STRUCT_INCLUDE(GPUMessage::FParameters, GPUMessageParams)
		SHADER_PARAMETER(uint32, StatusMessageId)
	END_SHADER_PARAMETER_STRUCT()


	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVirtualPageManagementShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmFeedbackStatusCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "FeedbackStatusCS", SF_Compute);

void FVirtualShadowMapVisualizeLightSearch::CheckLight(const FLightSceneProxy* CheckProxy, int CheckVirtualShadowMapId)
{
#if !UE_BUILD_SHIPPING
	FString CheckLightName = CheckProxy->GetOwnerNameOrLabel();
	if (GDumpVSMLightNames)
	{
		UE_LOG(LogRenderer, Display, TEXT("%s"), *CheckLightName);
	}

	const ULightComponent* Component = CheckProxy->GetLightComponent();
	check(Component);

	// Fill out new sort key and compare to our best found so far
	SortKey CheckKey;
	CheckKey.Packed = 0;
	CheckKey.Fields.bExactNameMatch = (CheckLightName == GVirtualShadowMapVisualizeLightName);
	CheckKey.Fields.bPartialNameMatch = CheckKey.Fields.bExactNameMatch || CheckLightName.Contains(GVirtualShadowMapVisualizeLightName);
	CheckKey.Fields.bSelected = Component->IsSelected();
	CheckKey.Fields.bOwnerSelected = Component->IsOwnerSelected();
	CheckKey.Fields.bDirectionalLight = CheckProxy->GetLightType() == LightType_Directional;
	CheckKey.Fields.bExists = 1;

	if (CheckKey.Packed > FoundKey.Packed)
	{
		FoundKey = CheckKey;
		FoundProxy = CheckProxy;
		FoundVirtualShadowMapId = CheckVirtualShadowMapId;
	}
#endif
}

static FRDGTextureRef CreateDebugOutputTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent)
{
	const FLinearColor ClearColor(1.0f, 0.0f, 1.0f, 0.0f);

	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		Extent,
		PF_R8G8B8A8,
		FClearValueBinding(ClearColor),
		TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef Texture = GraphBuilder.CreateTexture(Desc, TEXT("Shadow.Virtual.DebugProjection"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Texture), ClearColor);
	return Texture;
}

void FVirtualShadowMapArray::BuildPageAllocations(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const TArray<FViewInfo>& Views,
	const FEngineShowFlags& EngineShowFlags,
	const FSortedLightSetSceneInfo& SortedLightsInfo,
	const TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	const TArray<Nanite::FRasterResults, TInlineAllocator<2>>& NaniteRasterResults,
	FScene& Scene)
{
	check(IsEnabled());

	if (ShadowMaps.Num() == 0 || Views.Num() == 0)
	{
		// Nothing to do
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "FVirtualShadowMapArray::BuildPageAllocation");

	bool bDebugOutputEnabled = false;
	VisualizeLight.Reset();

#if !UE_BUILD_SHIPPING
	if (GDumpVSMLightNames)
	{
		bDebugOutputEnabled = true;
		UE_LOG(LogRenderer, Display, TEXT("Lights with Virtual Shadow Maps:"));
	}

	// Setup debug visualization/output if enabled
	{
		FVirtualShadowMapVisualizationData& VisualizationData = GetVirtualShadowMapVisualizationData();
	
		// TODO: Support more than one view/debug output
		const FName& VisualizationMode = Views[0].CurrentVirtualShadowMapVisualizationMode;
		if (VisualizationData.Update(VisualizationMode))
		{
			// TODO - automatically enable the show flag when set from command line?
			//EngineShowFlags.SetVisualizeVirtualShadowMap(true);
		}

		if (VisualizationData.IsActive() && EngineShowFlags.VisualizeVirtualShadowMap)
		{
			bDebugOutputEnabled = true;
			DebugVisualizationOutput = CreateDebugOutputTexture(GraphBuilder, SceneTextures.Config.Extent);
		}
	}
#endif //!UE_BUILD_SHIPPING
		
	// Store shadow map projection data for each virtual shadow map
	TArray<FVirtualShadowMapProjectionShaderData, SceneRenderingAllocator> ShadowMapProjectionData;
	ShadowMapProjectionData.AddDefaulted(ShadowMaps.Num());

	// Gather directional light virtual shadow maps
	TArray<int32, SceneRenderingAllocator> DirectionalLightIds;
	for (const FVisibleLightInfo& VisibleLightInfo : VisibleLightInfos)
	{
		for (const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap : VisibleLightInfo.VirtualShadowMapClipmaps)
		{
			// NOTE: Shader assumes all levels from a given clipmap are contiguous
			int32 ClipmapID = Clipmap->GetVirtualShadowMap()->ID;
			DirectionalLightIds.Add(ClipmapID);
			for (int32 ClipmapLevel = 0; ClipmapLevel < Clipmap->GetLevelCount(); ++ClipmapLevel)
			{
				ShadowMapProjectionData[ClipmapID + ClipmapLevel] = Clipmap->GetProjectionShaderData(ClipmapLevel);
			}

			if (bDebugOutputEnabled)
			{
				VisualizeLight.CheckLight(Clipmap->GetLightSceneInfo().Proxy, ClipmapID);
			}
		}

		for (FProjectedShadowInfo* ProjectedShadowInfo : VisibleLightInfo.AllProjectedShadows)
		{
			if (ProjectedShadowInfo->HasVirtualShadowMap())
			{
				check(ProjectedShadowInfo->CascadeSettings.ShadowSplitIndex == INDEX_NONE);		// We use clipmaps for virtual shadow maps, not cascades

				// NOTE: Virtual shadow maps are never atlased, but verify our assumptions
				{
					const FVector4f ClipToShadowUV = ProjectedShadowInfo->GetClipToShadowBufferUvScaleBias();
					check(ProjectedShadowInfo->BorderSize == 0);
					check(ProjectedShadowInfo->X == 0);
					check(ProjectedShadowInfo->Y == 0);
					const FIntRect ShadowViewRect = ProjectedShadowInfo->GetInnerViewRect();
					check(ShadowViewRect.Min.X == 0);
					check(ShadowViewRect.Min.Y == 0);
					check(ShadowViewRect.Max.X == FVirtualShadowMap::VirtualMaxResolutionXY);
					check(ShadowViewRect.Max.Y == FVirtualShadowMap::VirtualMaxResolutionXY);
				}

				int32 NumMaps = ProjectedShadowInfo->bOnePassPointLightShadow ? 6 : 1;
				for( int32 i = 0; i < NumMaps; i++ )
				{
					int32 ID = ProjectedShadowInfo->VirtualShadowMaps[i]->ID;

					FViewMatrices ViewMatrices = ProjectedShadowInfo->GetShadowDepthRenderingViewMatrices( i, true );

					FVirtualShadowMapProjectionShaderData& Data = ShadowMapProjectionData[ ID ];
					Data.TranslatedWorldToShadowViewMatrix		= FMatrix44f(ViewMatrices.GetTranslatedViewMatrix());	// LWC_TODO: Precision loss?
					Data.ShadowViewToClipMatrix					= FMatrix44f(ViewMatrices.GetProjectionMatrix());
					Data.TranslatedWorldToShadowUVMatrix		= FMatrix44f(CalcTranslatedWorldToShadowUVMatrix( ViewMatrices.GetTranslatedViewMatrix(), ViewMatrices.GetProjectionMatrix() ));
					Data.TranslatedWorldToShadowUVNormalMatrix	= FMatrix44f(CalcTranslatedWorldToShadowUVNormalMatrix( ViewMatrices.GetTranslatedViewMatrix(), ViewMatrices.GetProjectionMatrix() ));
					Data.ShadowPreViewTranslation				= FVector(ProjectedShadowInfo->PreShadowTranslation);
					Data.LightType								= ProjectedShadowInfo->GetLightSceneInfo().Proxy->GetLightType();
					Data.LightSourceRadius						= ProjectedShadowInfo->GetLightSceneInfo().Proxy->GetSourceRadius();
				}

				if (bDebugOutputEnabled)
				{
					VisualizeLight.CheckLight(ProjectedShadowInfo->GetLightSceneInfo().Proxy, ProjectedShadowInfo->VirtualShadowMaps[0]->ID);
				}
			}
		}
	}

	UniformParameters.NumShadowMaps = ShadowMaps.Num();
	UniformParameters.NumDirectionalLights = DirectionalLightIds.Num();

	ShadowMapProjectionDataRDG = CreateProjectionDataBuffer(GraphBuilder, TEXT("Shadow.Virtual.ProjectionData"), ShadowMapProjectionData);

	UniformParameters.ProjectionData = GraphBuilder.CreateSRV(ShadowMapProjectionDataRDG);

	if (CVarShowStats.GetValueOnRenderThread() || CacheManager->IsAccumulatingStats())
	{
		StatsBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumStats), TEXT("Shadow.Virtual.StatsBuffer"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StatsBufferRDG), 0);
	}
		
	// Create and clear the requested page flags
	const uint32 NumPageFlags = ShadowMaps.Num() * FVirtualShadowMap::PageTableSize;
	FRDGBufferRef PageRequestFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("Shadow.Virtual.PageRequestFlags"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageRequestFlagsRDG), 0);
		
	// TODO: Remove/move to next frame and make temporary OR replace with direct page table manipulation?
	DynamicCasterPageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("Shadow.Virtual.DynamicCasterPageFlags"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DynamicCasterPageFlagsRDG), 0);
		
	// Record the number of instances the buffer has capactiy for, should anything change (it shouldn't!)
	NumInvalidatingInstanceSlots = Scene.GPUScene.GetNumInstances();
	// Allocate space for counter, worst case ID storage, and flags.
	int32 InstanceInvalidationBufferSize = 1 + NumInvalidatingInstanceSlots + FMath::DivideAndRoundUp(NumInvalidatingInstanceSlots, 32);
	InvalidatingInstancesRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), InstanceInvalidationBufferSize), TEXT("Shadow.Virtual.InvalidatingInstances"));
	// Clear to zero, technically only need to clear first Scene.GPUScene.GetNumInstances()  + 1 uints
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InvalidatingInstancesRDG), 0);

	const uint32 NumPageRects = UniformParameters.NumShadowMaps * FVirtualShadowMap::MaxMipLevels;
	PageRectBoundsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector4), NumPageRects), TEXT("Shadow.Virtual.PageRectBounds"));
	{
		FInitPageRectBoundsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitPageRectBoundsCS::FParameters >();
		PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
		PassParameters->OutPageRectBounds = GraphBuilder.CreateUAV(PageRectBoundsRDG);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FInitPageRectBoundsCS>();
		ClearUnusedGraphResources(ComputeShader, PassParameters);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitPageRectBounds"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(NumPageRects, FInitPageRectBoundsCS::DefaultCSGroupX), 1, 1)
		);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo &View = Views[ViewIndex];

		// This view contained no local lights (that were stored in the light grid), and no directional lights, so nothing to do.
		if (View.ForwardLightingResources.LocalLightVisibleLightInfosIndex.Num() + DirectionalLightIds.Num() == 0)
		{
			continue;
		}

		FRDGBufferRef DirectionalLightIdsRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.DirectionalLightIds"), DirectionalLightIds);

		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

		FRDGBufferRef ScreenSpaceGridBoundsRDG = nullptr;
			
		{
			// It's safe to overlap these passes that all write to page request flags
			FRDGBufferUAVRef PageRequestFlagsUAV = GraphBuilder.CreateUAV(PageRequestFlagsRDG, ERDGUnorderedAccessViewFlags::SkipBarrier);

			// Mark pages based on projected depth buffer pixels
			if (CVarMarkPixelPages.GetValueOnRenderThread() != 0)
			{
				auto GeneratePageFlags = [&](bool bHairPass)
				{
					const uint32 InputType = bHairPass ? 1U : 0U; // HairStrands or GBuffer

					FGeneratePageFlagsFromPixelsCS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FGeneratePageFlagsFromPixelsCS::FInputType>(InputType);
					FGeneratePageFlagsFromPixelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FGeneratePageFlagsFromPixelsCS::FParameters >();
					PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);

					PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;

					PassParameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
					PassParameters->View = View.ViewUniformBuffer;
					PassParameters->OutPageRequestFlags = PageRequestFlagsUAV;
					PassParameters->ForwardLightData = View.ForwardLightingResources.ForwardLightUniformBuffer;
					PassParameters->DirectionalLightIds = GraphBuilder.CreateSRV(DirectionalLightIdsRDG);
					PassParameters->ResolutionLodBiasLocal = CVarResolutionLodBiasLocal.GetValueOnRenderThread();
					PassParameters->PageDilationBorderSizeLocal = CVarPageDilationBorderSizeLocal.GetValueOnRenderThread();
					PassParameters->PageDilationBorderSizeDirectional = CVarPageDilationBorderSizeDirectional.GetValueOnRenderThread();
					PassParameters->bCullBackfacingPixels = ShouldCullBackfacingPixels() ? 1 : 0;

					auto ComputeShader = View.ShaderMap->GetShader<FGeneratePageFlagsFromPixelsCS>(PermutationVector);

					static_assert((FVirtualPageManagementShader::DefaultCSGroupXY % 2) == 0, "GeneratePageFlagsFromPixels requires even-sized CS groups for quad swizzling.");
					const FIntPoint GridSize = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), FVirtualPageManagementShader::DefaultCSGroupXY);

					if (bHairPass && View.HairStrandsViewData.VisibilityData.TileData.IsValid())
					{
						PassParameters->IndirectBufferArgs = View.HairStrandsViewData.VisibilityData.TileData.TileIndirectDispatchBuffer;
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("GeneratePageFlagsFromPixels(HairStrands,Tile)"),
							ComputeShader,
							PassParameters,
							View.HairStrandsViewData.VisibilityData.TileData.TileIndirectDispatchBuffer,
							View.HairStrandsViewData.VisibilityData.TileData.GetIndirectDispatchArgOffset(FHairStrandsTiles::ETileType::HairAll));
					}
					else
					{
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("GeneratePageFlagsFromPixels(%s)", bHairPass ? TEXT("HairStrands") : TEXT("GBuffer")),
							ComputeShader,
							PassParameters,
							FIntVector(GridSize.X, GridSize.Y, 1));
					}
				};

				GeneratePageFlags(false);
				if (HairStrands::HasViewHairStrandsData(View))
				{
					GeneratePageFlags(true);
				}
			}
			// Mark coarse pages
			bool bMarkCoarsePagesDirectional = CVarMarkCoarsePagesDirectional.GetValueOnRenderThread() != 0;
			bool bMarkCoarsePagesLocal = CVarMarkCoarsePagesLocal.GetValueOnRenderThread() != 0;
			if (bMarkCoarsePagesDirectional || bMarkCoarsePagesLocal)
			{
				FMarkCoarsePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FMarkCoarsePagesCS::FParameters >();
				PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
				PassParameters->OutPageRequestFlags = PageRequestFlagsUAV;
				PassParameters->bMarkCoarsePagesLocal = bMarkCoarsePagesLocal ? 1 : 0;
				PassParameters->ClipmapIndexMask = bMarkCoarsePagesDirectional ? FVirtualShadowMapClipmap::GetCoarsePageClipmapIndexMask() : 0;
				PassParameters->bIncludeNonNaniteGeometry = CVarCoarsePagesIncludeNonNanite.GetValueOnRenderThread();

				auto ComputeShader = View.ShaderMap->GetShader<FMarkCoarsePagesCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("MarkCoarsePages"),
					ComputeShader,
					PassParameters,
					FIntVector(FMath::DivideAndRoundUp(uint32(ShadowMaps.Num()), FMarkCoarsePagesCS::DefaultCSGroupX), 1, 1)
				);
			}
		}
	}

	PageTableRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("Shadow.Virtual.PageTable"));		
	// Note: these are passed to the rendering and are not identical to the PageRequest flags coming in from GeneratePageFlagsFromPixels 
	PageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("Shadow.Virtual.PageFlags"));

	// One additional element as the last element is used as an atomic counter
	FRDGBufferRef FreePhysicalPagesRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), GetMaxPhysicalPages() + 1), TEXT("Shadow.Virtual.FreePhysicalPages"));
		
	// Enough space for all physical pages that might be allocated
	PhysicalPageMetaDataRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FPhysicalPageMetaData), GetMaxPhysicalPages()), TEXT("Shadow.Virtual.PhysicalPageMetaData"));

	AllocatedPageRectBoundsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector4), NumPageRects), TEXT("Shadow.Virtual.AllocatedPageRectBounds"));

	{
		FInitPhysicalPageMetaData::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitPhysicalPageMetaData::FParameters>();
		PassParameters->VirtualShadowMap		= GetUniformBuffer(GraphBuilder);
		PassParameters->OutPhysicalPageMetaData = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
		PassParameters->OutFreePhysicalPages	= GraphBuilder.CreateUAV(FreePhysicalPagesRDG);

		auto ComputeShader = Views[0].ShaderMap->GetShader<FInitPhysicalPageMetaData>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitPhysicalPageMetaData"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FInitPhysicalPageMetaData::DefaultCSGroupX), 1, 1)
		);
	}
		
	// Start by marking any physical pages that we are going to keep due to caching
	// NOTE: We run this pass even with no caching since we still need to initialize the metadata
	{
		FCreateCachedPageMappingsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCreateCachedPageMappingsCS::FParameters >();
		PassParameters->VirtualShadowMap		 = GetUniformBuffer(GraphBuilder);
		PassParameters->PageRequestFlags		 = GraphBuilder.CreateSRV(PageRequestFlagsRDG);
		PassParameters->OutPageTable			 = GraphBuilder.CreateUAV(PageTableRDG);
		PassParameters->OutPhysicalPageMetaData  = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
		PassParameters->OutPageFlags			 = GraphBuilder.CreateUAV(PageFlagsRDG);
		PassParameters->bDynamicPageInvalidation = 1;
#if !UE_BUILD_SHIPPING
		PassParameters->bDynamicPageInvalidation = CVarDebugSkipDynamicPageInvalidation.GetValueOnRenderThread() == 0 ? 1 : 0;
#endif

		bool bCacheEnabled = CacheManager->IsValid();
		if (bCacheEnabled)
		{
			SetCacheDataShaderParameters(GraphBuilder, ShadowMaps, CacheManager, PassParameters->CacheDataParameters);
		}
		bool bGenerateStats = StatsBufferRDG != nullptr;
		if (bGenerateStats)
		{
			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(StatsBufferRDG);
		}

		FCreateCachedPageMappingsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FCreateCachedPageMappingsCS::FHasCacheDataDim>(bCacheEnabled);
		PermutationVector.Set<FCreateCachedPageMappingsCS::FGenerateStatsDim>(bGenerateStats);
		auto ComputeShader = Views[0].ShaderMap->GetShader<FCreateCachedPageMappingsCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CreateCachedPageMappings"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(FVirtualShadowMap::PageTableSize, FCreateCachedPageMappingsCS::DefaultCSGroupX), ShadowMaps.Num(), 1)
		);
	}

	// After we've marked any cached pages, collect all the remaining free pages into a list
	// NOTE: We could optimize this more in the case where there's no caching of course; TBD priority
	{
		FPackFreePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPackFreePagesCS::FParameters>();
		PassParameters->VirtualShadowMap		= GetUniformBuffer(GraphBuilder);
		PassParameters->PhysicalPageMetaData	= GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
		PassParameters->OutFreePhysicalPages	= GraphBuilder.CreateUAV(FreePhysicalPagesRDG);
			
		auto ComputeShader = Views[0].ShaderMap->GetShader<FPackFreePagesCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PackFreePages"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FPackFreePagesCS::DefaultCSGroupX), 1, 1)
		);
	}

	// Allocate any new physical pages that were not cached from the free list
	{
		FAllocateNewPageMappingsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocateNewPageMappingsCS::FParameters>();
		PassParameters->VirtualShadowMap		= GetUniformBuffer(GraphBuilder);
		PassParameters->PageRequestFlags		= GraphBuilder.CreateSRV(PageRequestFlagsRDG);
		PassParameters->OutPageTable			= GraphBuilder.CreateUAV(PageTableRDG);
		PassParameters->OutPageFlags			= GraphBuilder.CreateUAV(PageFlagsRDG);
		PassParameters->OutFreePhysicalPages	= GraphBuilder.CreateUAV(FreePhysicalPagesRDG);
		PassParameters->OutPhysicalPageMetaData = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
			
		bool bGenerateStats = StatsBufferRDG != nullptr;
		if (bGenerateStats)
		{
			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(StatsBufferRDG);
		}

		FAllocateNewPageMappingsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FAllocateNewPageMappingsCS::FGenerateStatsDim>(bGenerateStats);
		auto ComputeShader = Views[0].ShaderMap->GetShader<FAllocateNewPageMappingsCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("AllocateNewPageMappings"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(FVirtualShadowMap::PageTableSize, FAllocateNewPageMappingsCS::DefaultCSGroupX), ShadowMaps.Num(), 1)
		);
	}

	{
		// Run pass building hierarchical page flags to make culling acceptable performance.
		FGenerateHierarchicalPageFlagsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateHierarchicalPageFlagsCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
		PassParameters->OutPageFlags = GraphBuilder.CreateUAV(PageFlagsRDG);
		PassParameters->OutPageRectBounds = GraphBuilder.CreateUAV(PageRectBoundsRDG);

		auto ComputeShader = Views[0].ShaderMap->GetShader<FGenerateHierarchicalPageFlagsCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GenerateHierarchicalPageFlags"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(FVirtualShadowMap::PageTableSize, FGenerateHierarchicalPageFlagsCS::DefaultCSGroupX), ShadowMaps.Num(), 1)
		);
	}

	// NOTE: We could skip this (in shader) for shadow maps that only have 1 mip (ex. clipmaps)
	{
		// Propagate mapped mips down the hierarchy to allow O(1) lookup of coarser mapped pages
		FPropagateMappedMipsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPropagateMappedMipsCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
		PassParameters->OutPageTable	= GraphBuilder.CreateUAV(PageTableRDG);

		auto ComputeShader = Views[0].ShaderMap->GetShader<FPropagateMappedMipsCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PropagateMappedMips"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(FMath::Square(FVirtualShadowMap::Level0DimPagesXY), FPropagateMappedMipsCS::DefaultCSGroupX), ShadowMaps.Num(), 1)
		);
	}

	// Initialize the physical page pool
	check(PhysicalPagePoolRDG != nullptr);
	{
		RDG_EVENT_SCOPE( GraphBuilder, "InitializePhysicalPages" );
		if (CVarInitializePhysicalUsingIndirect.GetValueOnRenderThread() != 0)
		{
			FRDGBufferRef InitializePagesIndirectArgsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(3), TEXT("Shadow.Virtual.InitializePagesIndirectArgs"));
			// Note: We use GetTotalAllocatedPhysicalPages() to size the buffer as the selection shader emits both static/dynamic pages separately when enabled.
			FRDGBufferRef PhysicalPagesToInitializeRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), GetTotalAllocatedPhysicalPages() + 1), TEXT("Shadow.Virtual.PhysicalPagesToInitialize"));

			// 1. Initialize the indirect args buffer
			AddClearIndirectDispatchArgs1DPass(GraphBuilder, InitializePagesIndirectArgsRDG);
			// 2. Filter the relevant physical pages and set up the indirect args
			{
				FSelectPagesToInitializeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectPagesToInitializeCS::FParameters>();
				PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
				PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
				PassParameters->OutInitializePagesIndirectArgsBuffer = GraphBuilder.CreateUAV(InitializePagesIndirectArgsRDG);
				PassParameters->OutPhysicalPagesToInitialize = GraphBuilder.CreateUAV(PhysicalPagesToInitializeRDG);
				bool bGenerateStats = StatsBufferRDG != nullptr;
				if (bGenerateStats)
				{
					PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(StatsBufferRDG);
				}
				FSelectPagesToInitializeCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FSelectPagesToInitializeCS::FGenerateStatsDim>(bGenerateStats);

				auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FSelectPagesToInitializeCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("SelectPagesToInitialize"),
					ComputeShader,
					PassParameters,
					FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FSelectPagesToInitializeCS::DefaultCSGroupX), 1, 1)
				);

			}
			// 3. Indirect dispatch to clear the selected pages
			{
				FInitializePhysicalPagesIndirectCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializePhysicalPagesIndirectCS::FParameters>();
				PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
				PassParameters->OutPhysicalPagePool = GraphBuilder.CreateUAV(PhysicalPagePoolRDG);
				PassParameters->IndirectArgs = InitializePagesIndirectArgsRDG;
				PassParameters->PhysicalPagesToInitialize = GraphBuilder.CreateSRV(PhysicalPagesToInitializeRDG);
				auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FInitializePhysicalPagesIndirectCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("InitializePhysicalMemoryIndirect"),
					ComputeShader,
					PassParameters,
					PassParameters->IndirectArgs,
					0
				);
			}
		}
		else
		{
			FInitializePhysicalPagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializePhysicalPagesCS::FParameters>();
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
			PassParameters->OutPhysicalPagePool = GraphBuilder.CreateUAV(PhysicalPagePoolRDG);

			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FInitializePhysicalPagesCS>();

			// Shader contains logic to deal with static cached pages if enabled
			// We only need to launch one per page, even if there are multiple cached pages per page
			FIntPoint DynamicPoolSize = GetDynamicPhysicalPoolSize();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("InitializePhysicalPages"),
				ComputeShader,
				PassParameters,
				FIntVector(
					FMath::DivideAndRoundUp(DynamicPoolSize.X, 16),
					FMath::DivideAndRoundUp(DynamicPoolSize.Y, 16),
					1)
			);
		}
	}

	UniformParameters.PageTable = GraphBuilder.CreateSRV(PageTableRDG);
	UniformParameters.PageFlags = GraphBuilder.CreateSRV(PageFlagsRDG);
	UniformParameters.PageRectBounds = GraphBuilder.CreateSRV(PageRectBoundsRDG);

	// Add pass to pipe back important stats
	{

		FVirtualSmFeedbackStatusCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmFeedbackStatusCS::FParameters>();
		PassParameters->FreePhysicalPages = GraphBuilder.CreateSRV(FreePhysicalPagesRDG);
		PassParameters->GPUMessageParams = GPUMessage::GetShaderParameters(GraphBuilder);
		PassParameters->StatusMessageId = CacheManager->StatusFeedbackSocket.GetMessageId().GetIndex();
		PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVirtualSmFeedbackStatusCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Feedback Status"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}

#if !UE_BUILD_SHIPPING
	// Only dump one frame of light data
	GDumpVSMLightNames = false;
#endif
}

class FDebugVisualizeVirtualSmCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FDebugVisualizeVirtualSmCS);
	SHADER_USE_PARAMETER_STRUCT(FDebugVisualizeVirtualSmCS, FVirtualPageManagementShader)

	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, ProjectionParameters)
		SHADER_PARAMETER(uint32, DebugTargetWidth)
		SHADER_PARAMETER(uint32, DebugTargetHeight)
		SHADER_PARAMETER(uint32, BorderWidth)
		SHADER_PARAMETER(uint32, ZoomScaleFactor)
		SHADER_PARAMETER(uint32, DebugMethod)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PageFlags)
		
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D< float >,			HZBPhysical )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >,	HZBPageTable )

		SHADER_PARAMETER_STRUCT_INCLUDE(FCacheDataParameters, CacheDataParameters)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDebugVisualizeVirtualSmCS, "/Engine/Private/VirtualShadowMaps/Debug.usf", "DebugVisualizeVirtualSmCS", SF_Compute);


void FVirtualShadowMapArray::RenderDebugInfo(FRDGBuilder& GraphBuilder)
{
	check(IsEnabled());
	int32 DebugMethod = CVarDebugVisualizeVirtualSms.GetValueOnRenderThread();
	if (ShadowMaps.Num() && DebugMethod > 0)
	{
		int32 ZoomScaleFactor = 1;
		int32 BorderWidth = 2;
		// Make debug target wide enough to show a mip-chain
		int32 DebugTargetWidth = ZoomScaleFactor * (FVirtualShadowMap::Level0DimPagesXY * 2 + BorderWidth * (FVirtualShadowMap::MaxMipLevels));
		// Enough rows for all the shadow maps to show
		int32 DebugTargetHeight = ZoomScaleFactor * (FVirtualShadowMap::Level0DimPagesXY + BorderWidth * 2) * ShadowMaps.Num();

		if( DebugMethod > 5 )
		{
			DebugTargetWidth = 2048;
			DebugTargetHeight = 2048;
		}

		FRDGTextureDesc DebugOutputDesc = FRDGTextureDesc::Create2D(
			FIntPoint(DebugTargetWidth, DebugTargetHeight),
			PF_A32B32G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef DebugOutput = GraphBuilder.CreateTexture(
			DebugOutputDesc,
			TEXT("Shadow.Virtual.Debug"));

		FDebugVisualizeVirtualSmCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDebugVisualizeVirtualSmCS::FParameters>();
		PassParameters->ProjectionParameters = GetSamplingParameters(GraphBuilder);
		PassParameters->PageFlags = GraphBuilder.CreateSRV( PageFlagsRDG );

		// TODO: unclear if it's preferable to debug the HZB we generated "this frame" here, or the previous frame (that was used for culling)?
		// We'll stick with the previous frame logic that was there, but it's cleaner to just reference the current frame one
		TRefCountPtr<IPooledRenderTarget> PrevHZBPhysical = CacheManager->PrevBuffers.HZBPhysical;
		TRefCountPtr<FRDGPooledBuffer>    PrevPageTable = CacheManager->PrevBuffers.PageTable;
		PassParameters->HZBPhysical	 = RegisterExternalTextureWithFallback( GraphBuilder, PrevHZBPhysical, GSystemTextures.BlackDummy );
		PassParameters->HZBPageTable = GraphBuilder.CreateSRV( PrevPageTable ? GraphBuilder.RegisterExternalBuffer( PrevPageTable ) : PageTableRDG );

		PassParameters->DebugTargetWidth = DebugTargetWidth;
		PassParameters->DebugTargetHeight = DebugTargetHeight;
		PassParameters->BorderWidth = BorderWidth;
		PassParameters->ZoomScaleFactor = ZoomScaleFactor;
		PassParameters->DebugMethod = DebugMethod;

		bool bCacheDataAvailable = CacheManager->IsValid();
		if (bCacheDataAvailable)
		{
			SetCacheDataShaderParameters(GraphBuilder, ShadowMaps, CacheManager, PassParameters->CacheDataParameters);
		}
		PassParameters->DebugOutput = GraphBuilder.CreateUAV(DebugOutput);

		FDebugVisualizeVirtualSmCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FDebugVisualizeVirtualSmCS::FHasCacheDataDim>(bCacheDataAvailable);
		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FDebugVisualizeVirtualSmCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DebugVisualizeVirtualSmCS"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(DebugTargetWidth, DebugTargetHeight), FVirtualPageManagementShader::DefaultCSGroupXY)
		);

		// TODO!
		// DebugVisualizationOutput = GraphBuilder.ConvertToExternalTexture(DebugOutput);		
	}
}


class FVirtualSmPrintStatsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmPrintStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmPrintStatsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_STRUCT_INCLUDE( ShaderPrint::FShaderParameters, ShaderPrintStruct )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, InStatsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIntVector4>, AllocatedPageRectBounds)
		SHADER_PARAMETER(int, ShowStatsValue)
	END_SHADER_PARAMETER_STRUCT()


	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVirtualPageManagementShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// Disable optimizations as shader print causes long compile times
		OutEnvironment.CompilerFlags.Add(CFLAG_SkipOptimizations);
	}

		
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmPrintStatsCS, "/Engine/Private/VirtualShadowMaps/PrintStats.usf", "PrintStats", SF_Compute);

void FVirtualShadowMapArray::PrintStats(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	check(IsEnabled());
	LLM_SCOPE_BYTAG(Nanite);

	// Print stats
	int ShowStatsValue = CVarShowStats.GetValueOnRenderThread();
	if (ShowStatsValue != 0 && StatsBufferRDG)
	{
		{
			FVirtualSmPrintStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmPrintStatsCS::FParameters>();

			ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintStruct);
			PassParameters->InStatsBuffer = GraphBuilder.CreateSRV(StatsBufferRDG);
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->ShowStatsValue = ShowStatsValue;

			auto ComputeShader = View.ShaderMap->GetShader<FVirtualSmPrintStatsCS>();

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

extern int32 GNaniteClusterPerPage;
void FVirtualShadowMapArray::CreateMipViews( TArray<Nanite::FPackedView, SceneRenderingAllocator>& Views ) const
{
	// strategy: 
	// 1. Use the cull pass to generate copies of every node for every view needed.
	// [2. Fabricate a HZB array?]
	ensure(Views.Num() <= ShadowMaps.Num());
	
	const int32 NumPrimaryViews = Views.Num();

	// 1. create derivative views for each of the Mip levels, 
	Views.AddDefaulted( NumPrimaryViews * ( FVirtualShadowMap::MaxMipLevels - 1) );

	int32 MaxMips = 0;
	for (int32 ViewIndex = 0; ViewIndex < NumPrimaryViews; ++ViewIndex)
	{
		const Nanite::FPackedView& PrimaryView = Views[ViewIndex];
		
		ensure( PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X >= 0 && PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X < ShadowMaps.Num() );
		ensure( PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y == 0 );
		ensure( PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z > 0 && PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z <= FVirtualShadowMap::MaxMipLevels );
		
		const int32 NumMips = PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z;
		MaxMips = FMath::Max(MaxMips, NumMips);
		for (int32 MipLevel = 0; MipLevel < NumMips; ++MipLevel)
		{
			Nanite::FPackedView& MipView = Views[ MipLevel * NumPrimaryViews + ViewIndex ];	// Primary (Non-Mip views) first followed by derived mip views.

			if( MipLevel > 0 )
			{
				MipView = PrimaryView;

				// Slightly messy, but extract any scale factor that was applied to the LOD scale for re-application below
				MipView.UpdateLODScales();
				float LODScaleFactor = PrimaryView.LODScales.X / MipView.LODScales.X;

				MipView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y = MipLevel;
				MipView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z = NumMips;	//FVirtualShadowMap::MaxMipLevels;

				// Size of view, for the virtual SMs these are assumed to not be offset.
				FIntPoint ViewSize = FIntPoint::DivideAndRoundUp( FIntPoint( PrimaryView.ViewSizeAndInvSize.X + 0.5f, PrimaryView.ViewSizeAndInvSize.Y + 0.5f ), 1U <<  MipLevel );
				FIntPoint ViewMin = FIntPoint(MipView.ViewRect.X, MipView.ViewRect.Y) / (1U <<  MipLevel);

				MipView.ViewSizeAndInvSize = FVector4f(ViewSize.X, ViewSize.Y, 1.0f / float(ViewSize.X), 1.0f / float(ViewSize.Y));
				MipView.ViewRect = FIntVector4(ViewMin.X, ViewMin.Y, ViewMin.X + ViewSize.X, ViewMin.Y + ViewSize.Y);

				MipView.UpdateLODScales();
				MipView.LODScales.X *= LODScaleFactor;
			}

			MipView.HZBTestViewRect = MipView.ViewRect;	// Assumed to always be the same for VSM

			float RcpExtXY = 1.0f / FVirtualShadowMap::VirtualMaxResolutionXY;
			if( GNaniteClusterPerPage )
				RcpExtXY = 1.0f / ( FVirtualShadowMap::PageSize * FVirtualShadowMap::RasterWindowPages );

			// Transform clip from virtual address space to viewport.
			MipView.ClipSpaceScaleOffset = FVector4f(
				MipView.ViewSizeAndInvSize.X * RcpExtXY,
				MipView.ViewSizeAndInvSize.Y * RcpExtXY,
				(MipView.ViewSizeAndInvSize.X + 2.0f * MipView.ViewRect.X) * RcpExtXY - 1.0f,
				-(MipView.ViewSizeAndInvSize.Y + 2.0f * MipView.ViewRect.Y) * RcpExtXY + 1.0f);

			uint32 StreamingPriorityCategory = 0;
			uint32 ViewFlags = VIEW_FLAG_HZBTEST;
			MipView.StreamingPriorityCategory_AndFlags = (ViewFlags << NUM_STREAMING_PRIORITY_CATEGORY_BITS) | StreamingPriorityCategory;
		}
	}

	// Remove unused mip views
	check(Views.IsEmpty() || MaxMips > 0);
	Views.SetNum(MaxMips * NumPrimaryViews, false);
}


class FVirtualSmPrintClipmapStatsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmPrintClipmapStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmPrintClipmapStatsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		//SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintStruct)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FIntVector4 >, PageRectBounds)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FIntVector4 >, AllocatedPageRectBounds)
		SHADER_PARAMETER(uint32, ShadowMapIdRangeStart)
		SHADER_PARAMETER(uint32, ShadowMapIdRangeEnd)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmPrintClipmapStatsCS, "/Engine/Private/VirtualShadowMaps/PrintStats.usf", "PrintClipmapStats", SF_Compute);


BEGIN_SHADER_PARAMETER_STRUCT(FVirtualShadowDepthPassParameters,)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FShadowDepthPassUniformParameters, ShadowDepthPass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


struct FVSMCullingBatchInfo
{
	FVector3f CullingViewOriginOffset;
	uint32 FirstPrimaryView;
	FVector3f CullingViewOriginTile;
	uint32 NumPrimaryViews;
};


struct FVisibleInstanceCmd
{
	uint32 PackedPageInfo;
	uint32 InstanceId;
	uint32 DrawCommandId;
};

class FCullPerPageDrawCommandsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullPerPageDrawCommandsCs);
	SHADER_USE_PARAMETER_STRUCT(FCullPerPageDrawCommandsCs, FGlobalShader)

	class FNearClipDim		: SHADER_PERMUTATION_BOOL( "NEAR_CLIP" );
	class FUseHzbDim : SHADER_PERMUTATION_BOOL("USE_HZB_OCCLUSION");
	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	class FBatchedDim : SHADER_PERMUTATION_BOOL("ENABLE_BATCH_MODE");
	using FPermutationDomain = TShaderPermutationDomain< FUseHzbDim, FNearClipDim, FBatchedDim, FGenerateStatsDim >;

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportNanite(Parameters.Platform);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FInstanceProcessingGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FHZBShaderParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, ShadowHZBPageTable)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
		SHADER_PARAMETER(FVector2f, HZBSize)
		SHADER_PARAMETER(uint32, HZBMode)
	END_SHADER_PARAMETER_STRUCT()



	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)

		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstancePayloadData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER(uint32, InstanceSceneDataSOAStride)
		SHADER_PARAMETER(uint32, GPUSceneFrameNumber)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PrimitiveRevealedMask)
		SHADER_PARAMETER(uint32, PrimitiveRevealedNum)

		SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceProcessingGPULoadBalancer::FShaderParameters, LoadBalancerParameters)

		SHADER_PARAMETER(int32, FirstPrimaryView)
		SHADER_PARAMETER(int32, NumPrimaryViews)
		SHADER_PARAMETER(uint32, TotalPrimaryViews)
		SHADER_PARAMETER(uint32, VisibleInstancesBufferNum)
		SHADER_PARAMETER(int32, DynamicInstanceIdOffset)
		SHADER_PARAMETER(int32, DynamicInstanceIdMax)
		SHADER_PARAMETER(float, MaxMaterialPositionInvalidationRange)
		SHADER_PARAMETER(FVector3f, CullingViewOriginOffset)
		SHADER_PARAMETER(FVector3f, CullingViewOriginTile)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FDrawCommandDesc >, DrawCommandDescs)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FContextBatchInfo >, BatchInfos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FVSMCullingBatchInfo >, VSMCullingBatchInfos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, BatchInds)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVisibleInstanceCmd>, VisibleInstancesOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawIndirectArgsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, VisibleInstanceCountBufferOut)

		SHADER_PARAMETER_STRUCT_INCLUDE(FHZBShaderParameters, HZBShaderParameters)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutInvalidatingInstances)
		SHADER_PARAMETER(uint32, NumInvalidatingInstanceSlots)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCullPerPageDrawCommandsCs, "/Engine/Private/VirtualShadowMaps/BuildPerPageDrawCommands.usf", "CullPerPageDrawCommandsCs", SF_Compute);



class FAllocateCommandInstanceOutputSpaceCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAllocateCommandInstanceOutputSpaceCs);
	SHADER_USE_PARAMETER_STRUCT(FAllocateCommandInstanceOutputSpaceCs, FGlobalShader)
public:
	static constexpr int32 NumThreadsPerGroup = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportNanite(Parameters.Platform);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FInstanceProcessingGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DrawIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, InstanceIdOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, TmpInstanceIdOffsetBufferOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FAllocateCommandInstanceOutputSpaceCs, "/Engine/Private/VirtualShadowMaps/BuildPerPageDrawCommands.usf", "AllocateCommandInstanceOutputSpaceCs", SF_Compute);


class FOutputCommandInstanceListsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOutputCommandInstanceListsCs);
	SHADER_USE_PARAMETER_STRUCT(FOutputCommandInstanceListsCs, FGlobalShader)
public:
	static constexpr int32 NumThreadsPerGroup = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportNanite(Parameters.Platform);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FInstanceProcessingGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FVisibleInstanceCmd >, VisibleInstances)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, InstanceIdsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, PageInfoBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, TmpInstanceIdOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleInstanceCountBuffer)

		// Needed reference for make RDG happy somehow
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FOutputCommandInstanceListsCs, "/Engine/Private/VirtualShadowMaps/BuildPerPageDrawCommands.usf", "OutputCommandInstanceListsCs", SF_Compute);


struct FCullingResult
{
	FRDGBufferRef DrawIndirectArgsRDG;
	FRDGBufferRef InstanceIdOffsetBufferRDG;
	FRDGBufferRef InstanceIdsBuffer;
	FRDGBufferRef PageInfoBuffer;
	uint32 MaxNumInstancesPerPass;
};

template <typename InstanceCullingLoadBalancerType>
static FCullingResult AddCullingPasses(FRDGBuilder& GraphBuilder,
	const TConstArrayView<FRHIDrawIndexedIndirectParameters> &IndirectArgs,
	const TConstArrayView<FInstanceCullingContext::FDrawCommandDesc>& DrawCommandDescs,
	const TConstArrayView<uint32>& InstanceIdOffsets,
	InstanceCullingLoadBalancerType *LoadBalancer,
	const TConstArrayView<FInstanceCullingMergedContext::FContextBatchInfo> BatchInfos,
	const TConstArrayView<FVSMCullingBatchInfo> VSMCullingBatchInfos,
	const TConstArrayView<uint32> BatchInds,
	bool bUseNearClip,
	uint32 TotalInstances,
	uint32 TotalPrimaryViews,
	FRDGBufferRef VirtualShadowViewsRDG,
	const FCullPerPageDrawCommandsCs::FHZBShaderParameters &HZBShaderParameters,
	FVirtualShadowMapArray *VirtualShadowMapArray,
	FGPUScene& GPUScene,
	FRDGBufferRef PrimitiveRevealedMaskRdg,
	int32 PrimitiveRevealedNum)
{
	int32 NumIndirectArgs = IndirectArgs.Num();

	FRDGBufferRef TmpInstanceIdOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.TmpInstanceIdOffsetBuffer"), sizeof(uint32), NumIndirectArgs, nullptr, 0);

	// TODO: This is both not right, and also over conservative when running with the atomic path
	FCullingResult CullingResult;
	CullingResult.MaxNumInstancesPerPass = TotalInstances * 64u;
	FRDGBufferRef VisibleInstancesRdg = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.VisibleInstances"), sizeof(FVisibleInstanceCmd), CullingResult.MaxNumInstancesPerPass, nullptr, 0);

	FRDGBufferRef VisibleInstanceWriteOffsetRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.VisibleInstanceWriteOffset"), sizeof(uint32), 1, nullptr, 0);
	FRDGBufferRef OutputOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.OutputOffsetBuffer"), sizeof(uint32), 1, nullptr, 0);

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VisibleInstanceWriteOffsetRDG), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputOffsetBufferRDG), 0);

	// Create buffer for indirect args and upload draw arg data, also clears the instance to zero
	FRDGBufferDesc IndirectArgsDesc = FRDGBufferDesc::CreateIndirectDesc(FInstanceCullingContext::IndirectArgsNumWords * IndirectArgs.Num());
	IndirectArgsDesc.Usage |= BUF_MultiGPUGraphIgnore;

	CullingResult.DrawIndirectArgsRDG = GraphBuilder.CreateBuffer(IndirectArgsDesc, TEXT("Shadow.Virtual.DrawIndirectArgsBuffer"));
	GraphBuilder.QueueBufferUpload(CullingResult.DrawIndirectArgsRDG, IndirectArgs.GetData(), IndirectArgs.GetTypeSize() * IndirectArgs.Num());

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	// Note: we redundantly clear the instance counts here as there is some issue with replays on certain consoles.
	FInstanceCullingContext::AddClearIndirectArgInstanceCountPass(GraphBuilder, ShaderMap, CullingResult.DrawIndirectArgsRDG);

	// not using structured buffer as we have to get at it as a vertex buffer 
	CullingResult.InstanceIdOffsetBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InstanceIdOffsets.Num()), TEXT("Shadow.Virtual.InstanceIdOffsetBuffer"));

	{
		FCullPerPageDrawCommandsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullPerPageDrawCommandsCs::FParameters>();

		PassParameters->VirtualShadowMap = VirtualShadowMapArray->GetUniformBuffer(GraphBuilder);

		PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceSceneDataBuffer.SRV;
		PassParameters->GPUSceneInstancePayloadData = GPUScene.InstancePayloadDataBuffer.SRV;
		PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
		PassParameters->GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();
		PassParameters->InstanceSceneDataSOAStride = GPUScene.InstanceSceneDataSOAStride;

		// Make sure there is enough space in the buffer for all the primitive IDs that might be used to index.
		check(PrimitiveRevealedMaskRdg->Desc.NumElements * 32u >= uint32(PrimitiveRevealedNum));
		PassParameters->PrimitiveRevealedMask = GraphBuilder.CreateSRV(PrimitiveRevealedMaskRdg);
		PassParameters->PrimitiveRevealedNum = uint32(PrimitiveRevealedNum);

		PassParameters->DynamicInstanceIdOffset = BatchInfos[0].DynamicInstanceIdOffset;
		PassParameters->DynamicInstanceIdMax = BatchInfos[0].DynamicInstanceIdMax;
		
		PassParameters->MaxMaterialPositionInvalidationRange = CVarMaxMaterialPositionInvalidationRange.GetValueOnRenderThread();

		auto GPUData = LoadBalancer->Upload(GraphBuilder);
		GPUData.GetShaderParameters(GraphBuilder, PassParameters->LoadBalancerParameters);

		PassParameters->FirstPrimaryView = VSMCullingBatchInfos[0].FirstPrimaryView;
		PassParameters->NumPrimaryViews = VSMCullingBatchInfos[0].NumPrimaryViews;
		PassParameters->CullingViewOriginOffset = VSMCullingBatchInfos[0].CullingViewOriginOffset;
		PassParameters->CullingViewOriginTile = VSMCullingBatchInfos[0].CullingViewOriginTile;

		PassParameters->TotalPrimaryViews = TotalPrimaryViews;
		PassParameters->VisibleInstancesBufferNum = CullingResult.MaxNumInstancesPerPass;
		PassParameters->InViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);
		PassParameters->DrawCommandDescs = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.DrawCommandDescs"), DrawCommandDescs));

		const bool bUseBatchMode = !BatchInds.IsEmpty();
		if (bUseBatchMode)
		{
			PassParameters->BatchInfos = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.BatchInfos"), BatchInfos));
			PassParameters->VSMCullingBatchInfos = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.VSMCullingBatchInfos"), VSMCullingBatchInfos));
			PassParameters->BatchInds = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.BatchInds"), BatchInds));
		}

		PassParameters->DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(CullingResult.DrawIndirectArgsRDG, PF_R32_UINT);

		PassParameters->VisibleInstancesOut = GraphBuilder.CreateUAV(VisibleInstancesRdg);
		PassParameters->VisibleInstanceCountBufferOut = GraphBuilder.CreateUAV(VisibleInstanceWriteOffsetRDG);
		PassParameters->OutInvalidatingInstances = GraphBuilder.CreateUAV(VirtualShadowMapArray->InvalidatingInstancesRDG);
		PassParameters->NumInvalidatingInstanceSlots = VirtualShadowMapArray->NumInvalidatingInstanceSlots;
		PassParameters->HZBShaderParameters = HZBShaderParameters;

		bool bGenerateStats = VirtualShadowMapArray->StatsBufferRDG != nullptr;
		if (bGenerateStats)
		{
			PassParameters->OutStatsBuffer = GraphBuilder.CreateUAV(VirtualShadowMapArray->StatsBufferRDG);
		}

		FCullPerPageDrawCommandsCs::FPermutationDomain PermutationVector;
		PermutationVector.Set< FCullPerPageDrawCommandsCs::FNearClipDim >(bUseNearClip);
		PermutationVector.Set< FCullPerPageDrawCommandsCs::FBatchedDim >(bUseBatchMode);
		PermutationVector.Set< FCullPerPageDrawCommandsCs::FUseHzbDim >(HZBShaderParameters.HZBTexture != nullptr);
		PermutationVector.Set< FCullPerPageDrawCommandsCs::FGenerateStatsDim >(bGenerateStats);

		auto ComputeShader = ShaderMap->GetShader<FCullPerPageDrawCommandsCs>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CullPerPageDrawCommands"),
			ComputeShader,
			PassParameters,
			LoadBalancer->GetWrappedCsGroupCount()
		);
	}
	// 2.2.Allocate space for the final instance ID output and so on.
	if (true)
	{
		FAllocateCommandInstanceOutputSpaceCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocateCommandInstanceOutputSpaceCs::FParameters>();

		FRDGBufferRef InstanceIdOutOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.OutputOffsetBufferOut"), sizeof(uint32), 1, nullptr, 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG), 0);

		PassParameters->NumIndirectArgs = NumIndirectArgs;
		PassParameters->InstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(CullingResult.InstanceIdOffsetBufferRDG, PF_R32_UINT);;
		PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG);
		PassParameters->TmpInstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(TmpInstanceIdOffsetBufferRDG);
		PassParameters->DrawIndirectArgsBuffer = GraphBuilder.CreateSRV(CullingResult.DrawIndirectArgsRDG, PF_R32_UINT);

		auto ComputeShader = ShaderMap->GetShader<FAllocateCommandInstanceOutputSpaceCs>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("AllocateCommandInstanceOutputSpaceCs"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(NumIndirectArgs, FAllocateCommandInstanceOutputSpaceCs::NumThreadsPerGroup)
		);
	}
	// 2.3. Perform final pass to re-shuffle the instance ID's to their final resting places
	CullingResult.InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), CullingResult.MaxNumInstancesPerPass), TEXT("Shadow.Virtual.InstanceIdsBuffer"));
	CullingResult.PageInfoBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), CullingResult.MaxNumInstancesPerPass), TEXT("Shadow.Virtual.PageInfoBuffer"));

	FRDGBufferRef OutputPassIndirectArgs = FComputeShaderUtils::AddIndirectArgsSetupCsPass1D(GraphBuilder, VisibleInstanceWriteOffsetRDG, TEXT("Shadow.Virtual.IndirectArgs"), FOutputCommandInstanceListsCs::NumThreadsPerGroup);
	if (true)
	{

		FOutputCommandInstanceListsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FOutputCommandInstanceListsCs::FParameters>();

		PassParameters->VisibleInstances = GraphBuilder.CreateSRV(VisibleInstancesRdg);
		PassParameters->PageInfoBufferOut = GraphBuilder.CreateUAV(CullingResult.PageInfoBuffer);
		PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(CullingResult.InstanceIdsBuffer);
		PassParameters->TmpInstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(TmpInstanceIdOffsetBufferRDG);
		PassParameters->VisibleInstanceCountBuffer = GraphBuilder.CreateSRV(VisibleInstanceWriteOffsetRDG);
		PassParameters->IndirectArgs = OutputPassIndirectArgs;

		auto ComputeShader = ShaderMap->GetShader<FOutputCommandInstanceListsCs>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("OutputCommandInstanceListsCs"),
			ComputeShader,
			PassParameters,
			OutputPassIndirectArgs,
			0
		);
	}

	return CullingResult;
}


static void AddRasterPass(
	FRDGBuilder& GraphBuilder, 
	FRDGEventName&& PassName,
	const FViewInfo * ShadowDepthView, 
	const TRDGUniformBufferRef<FShadowDepthPassUniformParameters> &ShadowDepthPassUniformBuffer,
	FVirtualShadowMapArray* VirtualShadowMapArray,
	FRDGBufferRef VirtualShadowViewsRDG,
	const FCullingResult &CullingResult, 
	FParallelMeshDrawCommandPass& MeshCommandPass,
	FVirtualShadowDepthPassParameters* PassParameters,
	TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> InstanceCullingUniformBuffer)
{
	PassParameters->View = ShadowDepthView->ViewUniformBuffer;
	PassParameters->ShadowDepthPass = ShadowDepthPassUniformBuffer;

	PassParameters->VirtualShadowMap = VirtualShadowMapArray->GetUniformBuffer(GraphBuilder);
	PassParameters->InViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);
	PassParameters->InstanceCullingDrawParams.DrawIndirectArgsBuffer = CullingResult.DrawIndirectArgsRDG;
	PassParameters->InstanceCullingDrawParams.InstanceIdOffsetBuffer = CullingResult.InstanceIdOffsetBufferRDG;
	PassParameters->InstanceCullingDrawParams.InstanceCulling = InstanceCullingUniformBuffer;

	FIntRect ViewRect;
	ViewRect.Max = FVirtualShadowMap::VirtualMaxResolutionXY;

	GraphBuilder.AddPass(
		MoveTemp(PassName),
		PassParameters,
		ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
		[&MeshCommandPass, PassParameters, ViewRect](FRHICommandList& RHICmdList)
		{
			FRHIRenderPassInfo RPInfo;
			RPInfo.ResolveParameters.DestRect.X1 = ViewRect.Min.X;
			RPInfo.ResolveParameters.DestRect.Y1 = ViewRect.Min.Y;
			RPInfo.ResolveParameters.DestRect.X2 = ViewRect.Max.X;
			RPInfo.ResolveParameters.DestRect.Y2 = ViewRect.Max.Y;
			RHICmdList.BeginRenderPass(RPInfo, TEXT("RasterizeVirtualShadowMaps(Non-Nanite)"));

			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, FMath::Min(ViewRect.Max.X, 32767), FMath::Min(ViewRect.Max.Y, 32767), 1.0f);

			MeshCommandPass.DispatchDraw(nullptr, RHICmdList, &PassParameters->InstanceCullingDrawParams);
			RHICmdList.EndRenderPass();
		});
}

void FVirtualShadowMapArray::RenderVirtualShadowMapsNonNanite(FRDGBuilder& GraphBuilder, const TArray<FProjectedShadowInfo *, SceneRenderingAllocator>& VirtualSmMeshCommandPasses, FScene& Scene, TArrayView<FViewInfo> Views)
{
	if (VirtualSmMeshCommandPasses.Num() == 0)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "RenderVirtualShadowMaps(Non-Nanite)");

	FGPUScene& GPUScene = Scene.GPUScene;

	FRDGBufferSRVRef PrevPageTableRDGSRV = CacheManager->IsValid() ? GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PageTable, TEXT("Shadow.Virtual.PrevPageTable"))) : nullptr;

	int32 HZBMode = CVarNonNaniteVsmUseHzb.GetValueOnRenderThread();

	auto InitHZB = [&]()->FRDGTextureRef
	{
		if (HZBMode == 1 && CacheManager->IsValid())
		{
			return GraphBuilder.RegisterExternalTexture(CacheManager->PrevBuffers.HZBPhysical);
		}

		if (HZBMode == 2 && HZBPhysical != nullptr)
		{
			return HZBPhysical;
		}
		return nullptr;
	};
	const FRDGTextureRef HZBTexture = InitHZB();

	TArray<FVSMCullingBatchInfo, SceneRenderingAllocator> UnBatchedVSMCullingBatchInfo;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> BatchedVirtualSmMeshCommandPasses;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> UnBatchedVirtualSmMeshCommandPasses;
	UnBatchedVSMCullingBatchInfo.Reserve(VirtualSmMeshCommandPasses.Num());
	BatchedVirtualSmMeshCommandPasses.Reserve(VirtualSmMeshCommandPasses.Num());
	UnBatchedVirtualSmMeshCommandPasses.Reserve(VirtualSmMeshCommandPasses.Num());
	TArray<Nanite::FPackedView, SceneRenderingAllocator> VirtualShadowViews;

	TArray<FVSMCullingBatchInfo, SceneRenderingAllocator> VSMCullingBatchInfos;
	VSMCullingBatchInfos.Reserve(VirtualSmMeshCommandPasses.Num());

	TArray<FVirtualShadowDepthPassParameters*, SceneRenderingAllocator> BatchedPassParameters;
	BatchedPassParameters.Reserve(VirtualSmMeshCommandPasses.Num());

	/**
	 * Use the 'dependent view' i.e., the view used to set up a view dependent CSM/VSM(clipmap) OR select the view closest to the local light.
	 * This last is important to get some kind of reasonable behaviour for split screen.
	 */
	auto GetCullingViewOrigin = [&Views](const FProjectedShadowInfo* ProjectedShadowInfo) -> FLargeWorldRenderPosition
	{
		if (ProjectedShadowInfo->DependentView != nullptr)
		{
			return FLargeWorldRenderPosition(ProjectedShadowInfo->DependentView->ShadowViewMatrices.GetViewOrigin());
		}

		// VSM supports only whole scene shadows, so those without a "DependentView" are local lights
		// For local lights the origin is the (inverse of) pre-shadow translation. 
		check(ProjectedShadowInfo->bWholeSceneShadow);

		FVector MinOrigin = Views[0].ShadowViewMatrices.GetViewOrigin();
		double MinDistanceSq = (MinOrigin + ProjectedShadowInfo->PreShadowTranslation).SquaredLength();
		for (int Index = 1; Index < Views.Num(); ++Index)
		{
			FVector TestOrigin = Views[Index].ShadowViewMatrices.GetViewOrigin();
			double TestDistanceSq = (TestOrigin + ProjectedShadowInfo->PreShadowTranslation).SquaredLength();
			if (TestDistanceSq < MinDistanceSq)
			{
				MinOrigin = TestOrigin;
				MinDistanceSq = TestDistanceSq;
			}

		}
		return FLargeWorldRenderPosition(MinOrigin);
	};

	FInstanceCullingMergedContext InstanceCullingMergedContext(GMaxRHIFeatureLevel);
	// We don't use the registered culling views (this redundancy should probably be addressed at some point), set the number to disable index range checking
	InstanceCullingMergedContext.NumCullingViews = -1;
	for (int32 Index = 0; Index < VirtualSmMeshCommandPasses.Num(); ++Index)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = VirtualSmMeshCommandPasses[Index];
		ProjectedShadowInfo->BeginRenderView(GraphBuilder, &Scene);

		FVSMCullingBatchInfo VSMCullingBatchInfo;
		VSMCullingBatchInfo.FirstPrimaryView = uint32(VirtualShadowViews.Num());
		VSMCullingBatchInfo.NumPrimaryViews = 0U;

		{
			const FLargeWorldRenderPosition CullingViewOrigin = GetCullingViewOrigin(ProjectedShadowInfo);
			VSMCullingBatchInfo.CullingViewOriginOffset = CullingViewOrigin.GetOffset();
			VSMCullingBatchInfo.CullingViewOriginTile = CullingViewOrigin.GetTile();
		}

		const TSharedPtr<FVirtualShadowMapClipmap> Clipmap = ProjectedShadowInfo->VirtualShadowMapClipmap;
		if (Clipmap)
		{
			VSMCullingBatchInfo.NumPrimaryViews = AddRenderViews(Clipmap, 1.0f, HZBTexture != nullptr, false, VirtualShadowViews);
			UnBatchedVSMCullingBatchInfo.Add(VSMCullingBatchInfo);
			UnBatchedVirtualSmMeshCommandPasses.Add(ProjectedShadowInfo);
		}
		else if (ProjectedShadowInfo->HasVirtualShadowMap())
		{
			FParallelMeshDrawCommandPass& MeshCommandPass = ProjectedShadowInfo->GetShadowDepthPass();
			MeshCommandPass.WaitForSetupTask();

			FInstanceCullingContext* InstanceCullingContext = MeshCommandPass.GetInstanceCullingContext();

			if (InstanceCullingContext->HasCullingCommands())
			{
				VSMCullingBatchInfo.NumPrimaryViews = AddRenderViews(ProjectedShadowInfo, 1.0f, HZBTexture != nullptr, false, VirtualShadowViews);

				if (CVarDoNonNaniteBatching.GetValueOnRenderThread())
				{
					FViewInfo* ShadowDepthView = ProjectedShadowInfo->ShadowDepthView;
					uint32 DynamicInstanceIdOffset = ShadowDepthView->DynamicPrimitiveCollector.GetInstanceSceneDataOffset();
					uint32 DynamicInstanceIdMax = DynamicInstanceIdOffset + ShadowDepthView->DynamicPrimitiveCollector.NumInstances();

					VSMCullingBatchInfos.Add(VSMCullingBatchInfo);

					// Note: we have to allocate these up front as the context merging machinery writes the offsets directly to the &PassParameters->InstanceCullingDrawParams, 
					// this is a side-effect from sharing the code with the deferred culling. Should probably be refactored.
					FVirtualShadowDepthPassParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualShadowDepthPassParameters>();
					InstanceCullingMergedContext.AddBatch(GraphBuilder, InstanceCullingContext, DynamicInstanceIdOffset, ShadowDepthView->DynamicPrimitiveCollector.NumInstances(), &PassParameters->InstanceCullingDrawParams);
					BatchedVirtualSmMeshCommandPasses.Add(ProjectedShadowInfo);
					BatchedPassParameters.Add(PassParameters);
				}
				else
				{
					UnBatchedVSMCullingBatchInfo.Add(VSMCullingBatchInfo);
					UnBatchedVirtualSmMeshCommandPasses.Add(ProjectedShadowInfo);
				}
			}
		}
	}
	uint32 TotalPrimaryViews = uint32(VirtualShadowViews.Num());
	CreateMipViews(VirtualShadowViews);
	FRDGBufferRef VirtualShadowViewsRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.VirtualShadowViews"), VirtualShadowViews);

	// Helper function to create raster pass UB - only really need two of these ever
	auto CreateShadowDepthPassUniformBuffer = [this, &VirtualShadowViewsRDG, &GraphBuilder](bool bClampToNearPlane)
	{
		FShadowDepthPassUniformParameters* ShadowDepthPassParameters = GraphBuilder.AllocParameters<FShadowDepthPassUniformParameters>();
		check(PhysicalPagePoolRDG != nullptr);
		// TODO: These are not used for this case anyway
		ShadowDepthPassParameters->ProjectionMatrix = FMatrix44f::Identity;
		ShadowDepthPassParameters->ViewMatrix = FMatrix44f::Identity;
		ShadowDepthPassParameters->ShadowParams = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
		ShadowDepthPassParameters->bRenderToVirtualShadowMap = true;

		ShadowDepthPassParameters->VirtualSmPageTable = GraphBuilder.CreateSRV(PageTableRDG);
		ShadowDepthPassParameters->PackedNaniteViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);
		ShadowDepthPassParameters->PageRectBounds = GraphBuilder.CreateSRV(PageRectBoundsRDG);
		ShadowDepthPassParameters->OutDepthBuffer = GraphBuilder.CreateUAV(PhysicalPagePoolRDG, ERDGUnorderedAccessViewFlags::SkipBarrier);
		SetupSceneTextureUniformParameters(GraphBuilder, GMaxRHIFeatureLevel, ESceneTextureSetupMode::None, ShadowDepthPassParameters->SceneTextures);
		ShadowDepthPassParameters->bClampToNearPlane = bClampToNearPlane;

		return GraphBuilder.CreateUniformBuffer(ShadowDepthPassParameters);
	};

	FCullPerPageDrawCommandsCs::FHZBShaderParameters HZBShaderParameters;
	if (HZBTexture)
	{
		// Mode 2 uses the current frame HZB  & page table.
		HZBShaderParameters.ShadowHZBPageTable = HZBMode == 2 ? GraphBuilder.CreateSRV(PageTableRDG) : PrevPageTableRDGSRV;
		HZBShaderParameters.HZBTexture = HZBTexture;
		HZBShaderParameters.HZBSize = HZBTexture->Desc.Extent;
		HZBShaderParameters.HZBSampler = TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();
		HZBShaderParameters.HZBMode = HZBMode;
	}

	// Process batched passes
	if (!InstanceCullingMergedContext.Batches.IsEmpty())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Batched");

		InstanceCullingMergedContext.MergeBatches();

		GraphBuilder.BeginEventScope(RDG_EVENT_NAME("CullingPasses"));
		FCullingResult CullingResult = AddCullingPasses(
			GraphBuilder,
			InstanceCullingMergedContext.IndirectArgs,
			InstanceCullingMergedContext.DrawCommandDescs,
			InstanceCullingMergedContext.InstanceIdOffsets,
			&InstanceCullingMergedContext.LoadBalancers[uint32(EBatchProcessingMode::Generic)],
			InstanceCullingMergedContext.BatchInfos,
			VSMCullingBatchInfos,
			InstanceCullingMergedContext.BatchInds[uint32(EBatchProcessingMode::Generic)],
			true,
			InstanceCullingMergedContext.TotalInstances,
			TotalPrimaryViews,
			VirtualShadowViewsRDG,
			HZBShaderParameters,
			this,
			GPUScene,
			GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, 4),
			0
		);
		GraphBuilder.EndEventScope();

		TRDGUniformBufferRef<FShadowDepthPassUniformParameters> ShadowDepthPassUniformBuffer = CreateShadowDepthPassUniformBuffer(false);

		FInstanceCullingGlobalUniforms* InstanceCullingGlobalUniforms = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
		InstanceCullingGlobalUniforms->InstanceIdsBuffer = GraphBuilder.CreateSRV(CullingResult.InstanceIdsBuffer);
		InstanceCullingGlobalUniforms->PageInfoBuffer = GraphBuilder.CreateSRV(CullingResult.PageInfoBuffer);
		InstanceCullingGlobalUniforms->BufferCapacity = CullingResult.MaxNumInstancesPerPass;
		TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> InstanceCullingUniformBuffer = GraphBuilder.CreateUniformBuffer(InstanceCullingGlobalUniforms);

		{
			RDG_EVENT_SCOPE(GraphBuilder, "RasterPasses");

			for (int Index = 0; Index < BatchedVirtualSmMeshCommandPasses.Num(); ++Index)
			{
				FProjectedShadowInfo* ProjectedShadowInfo = BatchedVirtualSmMeshCommandPasses[Index];
				FParallelMeshDrawCommandPass& MeshCommandPass = ProjectedShadowInfo->GetShadowDepthPass();
				FViewInfo* ShadowDepthView = ProjectedShadowInfo->ShadowDepthView;

				// Local lights are assumed to not use the clamp to near-plane (this is used for some per-object SMs but these should never be used fotr VSM).
				check(!ProjectedShadowInfo->ShouldClampToNearPlane());
			
				FString LightNameWithLevel;
				FSceneRenderer::GetLightNameForDrawEvent(ProjectedShadowInfo->GetLightSceneInfo().Proxy, LightNameWithLevel);
				AddRasterPass(GraphBuilder, RDG_EVENT_NAME("Rasterize[%s]", *LightNameWithLevel), ShadowDepthView, ShadowDepthPassUniformBuffer, this, VirtualShadowViewsRDG, CullingResult, MeshCommandPass, BatchedPassParameters[Index], InstanceCullingUniformBuffer);
			}
		}
	}

	// Loop over the un batched mesh command passes needed, these are all the clipmaps (but we may change the criteria)
	for (int Index = 0; Index < UnBatchedVirtualSmMeshCommandPasses.Num(); ++Index)
	{
		const auto VSMCullingBatchInfo = UnBatchedVSMCullingBatchInfo[Index];
		FProjectedShadowInfo* ProjectedShadowInfo = UnBatchedVirtualSmMeshCommandPasses[Index];
		FInstanceCullingMergedContext::FContextBatchInfo CullingBatchInfo = FInstanceCullingMergedContext::FContextBatchInfo{ 0 };

		FParallelMeshDrawCommandPass& MeshCommandPass = ProjectedShadowInfo->GetShadowDepthPass();
		const TSharedPtr<FVirtualShadowMapClipmap> Clipmap = ProjectedShadowInfo->VirtualShadowMapClipmap;
		FViewInfo* ShadowDepthView = ProjectedShadowInfo->ShadowDepthView;

		MeshCommandPass.WaitForSetupTask();

		FInstanceCullingContext* InstanceCullingContext = MeshCommandPass.GetInstanceCullingContext();

		if (InstanceCullingContext->HasCullingCommands())
		{
			FString LightNameWithLevel;
			FSceneRenderer::GetLightNameForDrawEvent(ProjectedShadowInfo->GetLightSceneInfo().Proxy, LightNameWithLevel);
			RDG_EVENT_SCOPE(GraphBuilder, "%s", *LightNameWithLevel);

			CullingBatchInfo.DynamicInstanceIdOffset = ShadowDepthView->DynamicPrimitiveCollector.GetInstanceSceneDataOffset();
			CullingBatchInfo.DynamicInstanceIdMax = CullingBatchInfo.DynamicInstanceIdOffset + ShadowDepthView->DynamicPrimitiveCollector.NumInstances();

			FRDGBufferRef PrimitiveRevealedMaskRdg = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, 4);
			int32 PrimitiveRevealedNum = 0;
				

			if (!Clipmap->GetRevealedPrimitivesMask().IsEmpty())
			{
				PrimitiveRevealedMaskRdg = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.RevealedPrimitivesMask"), Clipmap->GetRevealedPrimitivesMask());
				PrimitiveRevealedNum = Clipmap->GetNumRevealedPrimitives();

			}

			FCullingResult CullingResult = AddCullingPasses(
				GraphBuilder,
				InstanceCullingContext->IndirectArgs, 
				InstanceCullingContext->DrawCommandDescs,
				InstanceCullingContext->InstanceIdOffsets,
				InstanceCullingContext->LoadBalancers[uint32(EBatchProcessingMode::Generic)],
				MakeArrayView(&CullingBatchInfo, 1),
				MakeArrayView(&VSMCullingBatchInfo, 1),
				MakeArrayView<const uint32>(nullptr, 0),
				!Clipmap.IsValid(),
				InstanceCullingContext->TotalInstances,
				TotalPrimaryViews,
				VirtualShadowViewsRDG,
				HZBShaderParameters,
				this,
				GPUScene,
				PrimitiveRevealedMaskRdg,
				PrimitiveRevealedNum
			);

			TRDGUniformBufferRef<FShadowDepthPassUniformParameters> ShadowDepthPassUniformBuffer = CreateShadowDepthPassUniformBuffer(ProjectedShadowInfo->ShouldClampToNearPlane());

			FInstanceCullingGlobalUniforms* InstanceCullingGlobalUniforms = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
			InstanceCullingGlobalUniforms->InstanceIdsBuffer = GraphBuilder.CreateSRV(CullingResult.InstanceIdsBuffer);
			InstanceCullingGlobalUniforms->PageInfoBuffer = GraphBuilder.CreateSRV(CullingResult.PageInfoBuffer);
			InstanceCullingGlobalUniforms->BufferCapacity = CullingResult.MaxNumInstancesPerPass;
			TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> InstanceCullingUniformBuffer = GraphBuilder.CreateUniformBuffer(InstanceCullingGlobalUniforms);

			FVirtualShadowDepthPassParameters* DepthPassParams = GraphBuilder.AllocParameters<FVirtualShadowDepthPassParameters>();
			DepthPassParams->InstanceCullingDrawParams.IndirectArgsByteOffset = 0;
			DepthPassParams->InstanceCullingDrawParams.InstanceDataByteOffset = 0;
			AddRasterPass(GraphBuilder, RDG_EVENT_NAME("Rasterize"), ShadowDepthView, ShadowDepthPassUniformBuffer, this, VirtualShadowViewsRDG, CullingResult, MeshCommandPass, DepthPassParams, InstanceCullingUniformBuffer);
		}


		//
		if (Index == CVarShowClipmapStats.GetValueOnRenderThread())
		{
			// The 'main' view the shadow was created with respect to
			const FViewInfo* ViewUsedToCreateShadow = ProjectedShadowInfo->DependentView;
			const FViewInfo& View = *ViewUsedToCreateShadow;

			FVirtualSmPrintClipmapStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmPrintClipmapStatsCS::FParameters>();

			ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintStruct);
			//PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->ShadowMapIdRangeStart = Clipmap->GetVirtualShadowMap(0)->ID;
			// Note: assumes range!
			PassParameters->ShadowMapIdRangeEnd = Clipmap->GetVirtualShadowMap(0)->ID + Clipmap->GetLevelCount();
			PassParameters->PageRectBounds = GraphBuilder.CreateSRV(PageRectBoundsRDG);
			PassParameters->AllocatedPageRectBounds = GraphBuilder.CreateSRV(AllocatedPageRectBoundsRDG);

			auto ComputeShader = View.ShaderMap->GetShader<FVirtualSmPrintClipmapStatsCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PrintClipmapStats"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1)
			);
		}
	}
}

class FSelectPagesForHZBCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FSelectPagesForHZBCS);
	SHADER_USE_PARAMETER_STRUCT(FSelectPagesForHZBCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhysicalPageMetaData>, PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutPagesForHZBIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPhysicalPagesForHZB)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectPagesForHZBCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "SelectPagesForHZBCS", SF_Compute);


class FVirtualSmBuildHZBPerPageCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmBuildHZBPerPageCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmBuildHZBPerPageCS, FVirtualPageManagementShader)

	static constexpr uint32 TotalHZBLevels = FVirtualShadowMap::Log2PageSize;
	static constexpr uint32 HZBLevelsBase = TotalHZBLevels - 2U;

	static_assert(HZBLevelsBase == 5U, "The shader is expecting 5 levels, if the page size is changed, this needs to be massaged");

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PhysicalPagesForHZB)
		SHADER_PARAMETER_SAMPLER(SamplerState, PhysicalPagePoolSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PhysicalPagePool)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<float>, FurthestHZBOutput, [HZBLevelsBase])
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmBuildHZBPerPageCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "BuildHZBPerPageCS", SF_Compute);


class FVirtualSmBBuildHZBPerPageTopCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmBBuildHZBPerPageTopCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmBBuildHZBPerPageTopCS, FVirtualPageManagementShader)

	// We need one level less as HZB starts at half-size (not really sure if we really need 1x1 and 2x2 sized levels).
	static constexpr uint32 HZBLevelsTop = 2;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PhysicalPagesForHZB)
		SHADER_PARAMETER_SAMPLER(SamplerState, ParentTextureMipSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, ParentTextureMip)
		SHADER_PARAMETER(FVector2f, InvHzbInputSize)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<float>, FurthestHZBOutput, [HZBLevelsTop])
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmBBuildHZBPerPageTopCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "BuildHZBPerPageTopCS", SF_Compute);

FRDGTextureRef FVirtualShadowMapArray::BuildHZBFurthest(FRDGBuilder& GraphBuilder)
{
	const FIntRect ViewRect(0, 0, GetPhysicalPoolSize().X, GetPhysicalPoolSize().Y);
	const EPixelFormat Format = CVarVSMHZB32Bit.GetValueOnRenderThread() ? PF_R32_FLOAT : PF_R16F;

	FRDGTextureRef OutFurthestHZBTexture = nullptr;
	if (CVarVSMBuildHZBPerPage.GetValueOnRenderThread())
	{
		// 1. Gather up all physical pages that are allocated
		FRDGBufferRef PagesForHZBIndirectArgsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(2U * 4U), TEXT("Shadow.Virtual.PagesForHZBIndirectArgs"));
		// NOTE: Total allocated pages since the shader outputs separate entries for static/dynamic pages
		FRDGBufferRef PhysicalPagesForHZBRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), GetTotalAllocatedPhysicalPages() + 1), TEXT("Shadow.Virtual.PhysicalPagesForHZB"));

		// 1. Clear the indirect args buffer (note 2x args)
		AddClearIndirectDispatchArgs1DPass(GraphBuilder, PagesForHZBIndirectArgsRDG, 2U);

		// 2. Filter the relevant physical pages and set up the indirect args
		{
			FSelectPagesForHZBCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectPagesForHZBCS::FParameters>();
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
			PassParameters->OutPagesForHZBIndirectArgsBuffer = GraphBuilder.CreateUAV(PagesForHZBIndirectArgsRDG);
			PassParameters->OutPhysicalPagesForHZB = GraphBuilder.CreateUAV(PhysicalPagesForHZBRDG);

			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FSelectPagesForHZBCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SelectPagesForHZB"),
				ComputeShader,
				PassParameters,
				FIntVector(FMath::DivideAndRoundUp(UniformParameters.MaxPhysicalPages, FSelectPagesForHZBCS::DefaultCSGroupX), 1, 1)
			);

		}


		//FIntPoint HZBSize(GetPhysicalPoolSize().X / 2, GetPhysicalPoolSize().Y / 2);
		FIntPoint PhysicalPoolSize = GetPhysicalPoolSize();

		FIntPoint HZBSize;
		//HZBSize = FIntPoint(PhysicalPoolSize.X / 2, PhysicalPoolSize.Y / 2);
		// NOTE: IsVisibleHZB currently assumes HZB is pow2 size, so force that. See HZBCull.ush.
		HZBSize.X = FMath::Max( FPlatformMath::RoundUpToPowerOfTwo( PhysicalPoolSize.X ) >> 1, 1u );
		HZBSize.Y = FMath::Max( FPlatformMath::RoundUpToPowerOfTwo( PhysicalPoolSize.Y ) >> 1, 1u );

		FRDGTextureDesc HZBDesc = FRDGTextureDesc::Create2D(HZBSize, Format, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV, FVirtualSmBuildHZBPerPageCS::TotalHZBLevels);
		HZBDesc.Flags |= GFastVRamConfig.HZB;

		/** Closest and furthest HZB are intentionally in separate render target, because majority of the case you only one or the other.
		 * Keeping them separate avoid doubling the size in cache for this cases, to avoid performance regression.
		 */
		OutFurthestHZBTexture = GraphBuilder.CreateTexture(HZBDesc, TEXT("Shadow.Virtual.PreviousOccluderHZB"));
		
		{
			FVirtualSmBuildHZBPerPageCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmBuildHZBPerPageCS::FParameters>();

			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			for (int32 DestMip = 0; DestMip < FVirtualSmBuildHZBPerPageCS::HZBLevelsBase; DestMip++)
			{
				PassParameters->FurthestHZBOutput[DestMip] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutFurthestHZBTexture, DestMip));
			}
			PassParameters->PhysicalPagePool = PhysicalPagePoolRDG;
			PassParameters->PhysicalPagePoolSampler = TStaticSamplerState<SF_Point>::GetRHI();

			PassParameters->IndirectArgs = PagesForHZBIndirectArgsRDG;
			PassParameters->PhysicalPagesForHZB = GraphBuilder.CreateSRV(PhysicalPagesForHZBRDG);
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVirtualSmBuildHZBPerPageCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("BuildHZBPerPage"),
				ComputeShader,
				PassParameters,
				PassParameters->IndirectArgs,
				0
			);
		}
		{
			FVirtualSmBBuildHZBPerPageTopCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmBBuildHZBPerPageTopCS::FParameters>();

			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);

			uint32 StartDestMip = FVirtualSmBuildHZBPerPageCS::HZBLevelsBase;
			for (int32 DestMip = 0; DestMip < FVirtualSmBBuildHZBPerPageTopCS::HZBLevelsTop; DestMip++)
			{
				PassParameters->FurthestHZBOutput[DestMip] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutFurthestHZBTexture, StartDestMip + DestMip));
			}
			FIntPoint SrcSize = FIntPoint::DivideAndRoundUp(HZBSize, 1 << int32(StartDestMip - 1));
			PassParameters->InvHzbInputSize = FVector2f(1.0f / SrcSize.X, 1.0f / SrcSize.Y);;
			PassParameters->ParentTextureMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(OutFurthestHZBTexture, StartDestMip - 1));
			PassParameters->ParentTextureMipSampler = TStaticSamplerState<SF_Point>::GetRHI();

			PassParameters->IndirectArgs = PagesForHZBIndirectArgsRDG;
			PassParameters->PhysicalPagesForHZB = GraphBuilder.CreateSRV(PhysicalPagesForHZBRDG);
			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVirtualSmBBuildHZBPerPageTopCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("BuildHZBPerPageTop"),
				ComputeShader,
				PassParameters,
				PassParameters->IndirectArgs,
				// NOTE: offset 4 to get second set of args in the buffer.
				4U
			);
		}
	}
	else
	{
		::BuildHZBFurthest(
			GraphBuilder,
			FRDGSystemTextures::Get(GraphBuilder).Black,
			PhysicalPagePoolRDG,
			ViewRect,
			GMaxRHIFeatureLevel,
			GMaxRHIShaderPlatform,
			TEXT("Shadow.Virtual.PreviousOccluderHZB"),
			/* OutFurthestHZBTexture = */ &OutFurthestHZBTexture,
			Format);
	}
	return OutFurthestHZBTexture;
}


uint32 FVirtualShadowMapArray::AddRenderViews(const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap, float LODScaleFactor, bool bSetHzbParams, bool bUpdateHZBMetaData, TArray<Nanite::FPackedView, SceneRenderingAllocator> &OutVirtualShadowViews)
{
	// TODO: Decide if this sort of logic belongs here or in Nanite (as with the mip level view expansion logic)
	// We're eventually going to want to snap/quantize these rectangles/positions somewhat so probably don't want it
	// entirely within Nanite, but likely makes sense to have some sort of "multi-viewport" notion in Nanite that can
	// handle both this and mips.
	// NOTE: There's still the additional VSM view logic that runs on top of this in Nanite too (see CullRasterize variant)
	Nanite::FPackedViewParams BaseParams;
	BaseParams.ViewRect = FIntRect(0, 0, FVirtualShadowMap::VirtualMaxResolutionXY, FVirtualShadowMap::VirtualMaxResolutionXY);
	BaseParams.HZBTestViewRect = BaseParams.ViewRect;
	BaseParams.RasterContextSize = GetPhysicalPoolSize();
	BaseParams.LODScaleFactor = LODScaleFactor;
	BaseParams.PrevTargetLayerIndex = INDEX_NONE;
	BaseParams.TargetMipLevel = 0;
	BaseParams.TargetMipCount = 1;	// No mips for clipmaps

	for (int32 ClipmapLevelIndex = 0; ClipmapLevelIndex < Clipmap->GetLevelCount(); ++ClipmapLevelIndex)
	{
		FVirtualShadowMap* VirtualShadowMap = Clipmap->GetVirtualShadowMap(ClipmapLevelIndex);

		Nanite::FPackedViewParams Params = BaseParams;
		Params.TargetLayerIndex = VirtualShadowMap->ID;
		Params.ViewMatrices = Clipmap->GetViewMatrices(ClipmapLevelIndex);
		Params.PrevTargetLayerIndex = INDEX_NONE;
		Params.PrevViewMatrices = Params.ViewMatrices;
		Params.Flags = 0;

		// TODO: Clean this up - could be stored in a single structure for the whole clipmap
		int32 HZBKey = Clipmap->GetHZBKey(ClipmapLevelIndex);

		if (bSetHzbParams)
		{
			CacheManager->SetHZBViewParams(HZBKey, Params);
		}

		// If we're going to generate a new HZB this frame, save the associated metadata
		if (bUpdateHZBMetaData)
		{
			FVirtualShadowMapHZBMetadata& HZBMeta = HZBMetadata.FindOrAdd(HZBKey);
			HZBMeta.TargetLayerIndex = Params.TargetLayerIndex;
			HZBMeta.ViewMatrices = Params.ViewMatrices;
			HZBMeta.ViewRect = Params.ViewRect;
		}

		Nanite::FPackedView View = Nanite::CreatePackedView(Params);
		OutVirtualShadowViews.Add(View);

		// Mark that we rendered to this VSM for caching purposes
		if (VirtualShadowMap->VirtualShadowMapCacheEntry)
		{
			VirtualShadowMap->VirtualShadowMapCacheEntry->MarkRendered();
		}
	}

	return uint32(Clipmap->GetLevelCount());
}

uint32 FVirtualShadowMapArray::AddRenderViews(const FProjectedShadowInfo* ProjectedShadowInfo, float LODScaleFactor, bool bSetHzbParams, bool bUpdateHZBMetaData, TArray<Nanite::FPackedView, SceneRenderingAllocator>& OutVirtualShadowViews)
{
	Nanite::FPackedViewParams BaseParams;
	BaseParams.ViewRect = ProjectedShadowInfo->GetOuterViewRect();
	BaseParams.HZBTestViewRect = BaseParams.ViewRect;
	BaseParams.RasterContextSize = GetPhysicalPoolSize();
	BaseParams.LODScaleFactor = LODScaleFactor;
	BaseParams.PrevTargetLayerIndex = INDEX_NONE;
	BaseParams.TargetMipLevel = 0;
	BaseParams.TargetMipCount = FVirtualShadowMap::MaxMipLevels;

	int32 NumMaps = ProjectedShadowInfo->bOnePassPointLightShadow ? 6 : 1;
	for (int32 Index = 0; Index < NumMaps; ++Index)
	{
		FVirtualShadowMap* VirtualShadowMap = ProjectedShadowInfo->VirtualShadowMaps[Index];

		Nanite::FPackedViewParams Params = BaseParams;
		Params.TargetLayerIndex = VirtualShadowMap->ID;
		Params.ViewMatrices = ProjectedShadowInfo->GetShadowDepthRenderingViewMatrices(Index, true);

		int32 HZBKey = ProjectedShadowInfo->GetLightSceneInfo().Id + (Index << 24);

		if (bSetHzbParams)
		{
			CacheManager->SetHZBViewParams(HZBKey, Params);
		}

		// If we're going to generate a new HZB this frame, save the associated metadata
		if (bUpdateHZBMetaData)
		{
			FVirtualShadowMapHZBMetadata& HZBMeta = HZBMetadata.FindOrAdd(HZBKey);
			HZBMeta.TargetLayerIndex = Params.TargetLayerIndex;
			HZBMeta.ViewMatrices = Params.ViewMatrices;
			HZBMeta.ViewRect = Params.ViewRect;
		}

		OutVirtualShadowViews.Add(Nanite::CreatePackedView(Params));

		if (VirtualShadowMap->VirtualShadowMapCacheEntry)
		{
			VirtualShadowMap->VirtualShadowMapCacheEntry->MarkRendered();
		}
	}

	return uint32(NumMaps);
}

void FVirtualShadowMapArray::AddVisualizePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture Output)
{
#if !UE_BUILD_SHIPPING
	if (!IsAllocated() || DebugVisualizationOutput == nullptr)
	{
		return;
	}

	const FVirtualShadowMapVisualizationData& VisualizationData = GetVirtualShadowMapVisualizationData();
	if (VisualizationData.IsActive() && VisualizeLight.IsValid())
	{	
		FCopyRectPS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
		Parameters->InputTexture = DebugVisualizationOutput;
		Parameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FCopyRectPS> PixelShader(View.ShaderMap);

		FScreenPassTextureViewport InputViewport(DebugVisualizationOutput->Desc.Extent);
		FScreenPassTextureViewport OutputViewport(Output);

		// See CVarVisualizeLayout documentation
		const int32 VisualizeLayout = CVarVisualizeLayout.GetValueOnRenderThread();
		if (VisualizeLayout == 1)		// Thumbnail
		{
			const int32 TileWidth  = View.UnscaledViewRect.Width() / 3;
			const int32 TileHeight = View.UnscaledViewRect.Height() / 3;

			OutputViewport.Rect.Min = FIntPoint(0, 0);
			OutputViewport.Rect.Max = OutputViewport.Rect.Min + FIntPoint(TileWidth, TileHeight);
		}
		else if (VisualizeLayout == 2)	// Split screen
		{
			InputViewport.Rect.Max = InputViewport.Rect.Min + (InputViewport.Rect.Width() / 2);
			OutputViewport.Rect.Max = OutputViewport.Rect.Min + (OutputViewport.Rect.Width() / 2);
		}

		// Use separate input and output viewports w/ bilinear sampling to properly support dynamic resolution scaling
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DrawTexture"), View, OutputViewport, InputViewport, PixelShader, Parameters, EScreenPassDrawFlags::None);
		
		// Visualization light name
		{
			FScreenPassRenderTarget OutputTarget(Output.Texture, View.UnscaledViewRect, ERenderTargetLoadAction::ELoad);

			AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("Labels"), View, OutputTarget,
				[&VisualizeLight=VisualizeLight, &OutputViewport=OutputViewport](FCanvas& Canvas)
			{
				const FLinearColor LabelColor(1, 1, 0);
				Canvas.DrawShadowedString(
					OutputViewport.Rect.Min.X + 8,
					OutputViewport.Rect.Max.Y - 19,
					*VisualizeLight.GetLightName(),
					GetStatsFont(),
					LabelColor);
			});
		}
	}
#endif
}
