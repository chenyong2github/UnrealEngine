// Copyright Epic Games, Inc. All Rights Reserved.
/*=============================================================================
	VirtualShadowMap.h:
=============================================================================*/
#include "VirtualShadowMapArray.h"
#include "../BasePassRendering.h"
#include "RendererModule.h"
#include "Rendering/NaniteResources.h"
#include "ShaderPrint.h"
#include "ShaderPrintParameters.h"
#include "VirtualShadowMapCacheManager.h"
#include "VirtualShadowMapClipmap.h"
#include "ComponentRecreateRenderStateContext.h"
#include "HairStrands/HairStrandsData.h"

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(VirtualShadowMapUbSlot);

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FVirtualShadowMapUniformParameters, "VirtualShadowMap", VirtualShadowMapUbSlot);

struct FShadowMapCacheData
{
	// XY offset in pages to the location of the previous frame's page table.
	FIntPoint SmPageOffset;
	// ID of the corresponding virtual SM in the chached data 
	int32 VirtualShadowMapId;
	// Depth offset to add to SM texels when copying
	float DepthOffset;
};


struct FPhysicalPageMetaData
{
	uint32 State;
	uint32 Age;
};


struct FCachedPageInfo
{
	FIntPoint PhysPageAddress;
	float DepthOffset;
	float Padding;
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
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarPageDilationBorderSize(
	TEXT("r.Shadow.Virtual.PageDilationBorderSize"),
	0.05f,
	TEXT("If a screen pixel falls within this fraction of a page border, the adacent page will also be mapped.")
	TEXT("Higher values can reduce page misses at screen edges or disocclusions, but increase total page counts."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarAllocatePagesUsingRects(
	TEXT("r.Shadow.Virtual.AllocatePagesUsingRects"),
	0,
	TEXT("If set to 1 then pages are allocated in a contigious block to support the bounding rectangle of allocated virtual pages leading to a trivial mapping but higher memory overhead."),
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
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMarkCoarsePagesLocal(
	TEXT("r.Shadow.Virtual.MarkCoarsePagesLocal"),
	1,
	TEXT("Marks coarse pages in local light virtual shadow maps so that low resolution data is available everywhere.")
	TEXT("Ability to disable is primarily for profiling and debugging."),
	ECVF_RenderThreadSafe
);

int32 GVirtualShadowMapAtomicWrites = 1;
FAutoConsoleVariableRef CVarAtomicWrites(
	TEXT("r.Shadow.Virtual.AtomicWrites"),
	GVirtualShadowMapAtomicWrites,
	TEXT("Use per pixel page table routing and atomic writes instead of an instance per page."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		// Needed because the depth state changes with method (so cached draw commands must be re-created) see SetStateForShadowDepth
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarShowClipmapStats(
	TEXT("r.Shadow.Virtual.ShowClipmapStats"),
	-1,
	TEXT("Set to the number of clipmap you want to show stats for (-1 == off)\n"),
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
			ShadowMapCacheData[SmIndex].SmPageOffset = VirtualShadowMapCacheEntry->GetPageSpaceOffset();
			ShadowMapCacheData[SmIndex].VirtualShadowMapId = VirtualShadowMapCacheEntry->PrevVirtualShadowMapId;
			ShadowMapCacheData[SmIndex].DepthOffset = VirtualShadowMapCacheEntry->GetDepthOffset();
		}
		else
		{
			ShadowMapCacheData[SmIndex].SmPageOffset = FIntPoint(0, 0);
			ShadowMapCacheData[SmIndex].VirtualShadowMapId = INDEX_NONE;
			ShadowMapCacheData[SmIndex].DepthOffset = 0.0f;
		}
	}
	FRDGBufferUploader BufferUploader;
	CacheDataParameters.ShadowMapCacheData = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.ShadowMapCacheData"), ShadowMapCacheData));
	CacheDataParameters.PrevPageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PageFlags, TEXT("Shadow.Virtual.PrevPageFlags")));
	CacheDataParameters.PrevPageTable = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PageTable, TEXT("Shadow.Virtual.PrevPageTable")));
	CacheDataParameters.PrevPhysicalPageMetaData = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PhysicalPageMetaData, TEXT("Shadow.Virtual.PrevPhysicalPageMetaData")));
	CacheDataParameters.PrevDynamicCasterPageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.DynamicCasterPageFlags, TEXT("Shadow.Virtual.PrevDynamicCasterPageFlags")));
	BufferUploader.Submit(GraphBuilder);
}

static FRDGBufferRef CreateProjectionDataBuffer(
	FRDGBuilder& GraphBuilder,
	FRDGBufferUploader& BufferUploader,
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
	BufferUploader.Upload(GraphBuilder, Buffer, InitialData.GetData(), DataSize);
	return Buffer;
}

void FVirtualShadowMapArray::Initialize(FRDGBuilder& GraphBuilder, FVirtualShadowMapArrayCacheManager* InCacheManager, bool bInEnabled)
{
	bInitialized = true;
	bEnabled = bInEnabled;
	CacheManager = InCacheManager;
	check(!bEnabled || CacheManager);

	UniformParameters.NumShadowMaps = 0;
	UniformParameters.NumDirectionalLights = 0;

	uint32 HPageFlagOffset = 0;
	for (uint32 Level = 0; Level < FVirtualShadowMap::MaxMipLevels - 1; ++Level)
	{
		UniformParameters.HPageFlagLevelOffsets[Level] = HPageFlagOffset;
		HPageFlagOffset += FVirtualShadowMap::PageTableSize - CalcVirtualShadowMapLevelOffsets(Level + 1, FVirtualShadowMap::Log2Level0DimPagesXY);
	}
	// The last mip level is 1x1 and thus does not have any H levels possible.
	UniformParameters.HPageFlagLevelOffsets[FVirtualShadowMap::MaxMipLevels - 1] = 0;
	UniformParameters.HPageTableSize = HPageFlagOffset;

	// Fixed physical page pool width, we adjust the height to accomodate the requested maximum
	// NOTE: Row size in pages has to be POT since we use mask & shift in place of integer ops
	const uint32 PhysicalPagesX = FMath::DivideAndRoundUp(8192U, FVirtualShadowMap::PageSize);
	check(FMath::IsPowerOfTwo(PhysicalPagesX));
	const uint32 PhysicalPagesY = FMath::DivideAndRoundUp((uint32)FMath::Max(1, CVarMaxPhysicalPages.GetValueOnRenderThread()), PhysicalPagesX);	
	const uint32 PhysicalX = PhysicalPagesX * FVirtualShadowMap::PageSize;
	const uint32 PhysicalY = PhysicalPagesY * FVirtualShadowMap::PageSize;

	UniformParameters.MaxPhysicalPages = PhysicalPagesX * PhysicalPagesY;
	UniformParameters.PhysicalPageRowMask = (PhysicalPagesX - 1);
	UniformParameters.PhysicalPageRowShift = FMath::FloorLog2( PhysicalPagesX );
	UniformParameters.RecPhysicalPoolSize = FVector4( 1.0f / PhysicalX, 1.0f / PhysicalY, 1.0f, 1.0f );
	UniformParameters.PhysicalPoolSize = FIntPoint( PhysicalX, PhysicalY );
	UniformParameters.PhysicalPoolSizePages = FIntPoint( PhysicalPagesX, PhysicalPagesY );

	// Reference dummy data in the UB initially
	const uint32 DummyPageElement = 0xFFFFFFFF;
	UniformParameters.PageTable = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(DummyPageElement), DummyPageElement));
	UniformParameters.ProjectionData = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FVirtualShadowMapProjectionShaderData)));

	UniformParameters.PhysicalPagePool = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	UniformParameters.PhysicalPagePoolHw = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
}

FVirtualShadowMapArray::~FVirtualShadowMapArray()
{
	for (FVirtualShadowMap *SM : ShadowMaps)
	{
		SM->~FVirtualShadowMap();
	}
}

FIntPoint FVirtualShadowMapArray::GetPhysicalPoolSize() const
{
	check(bInitialized);
	return FIntPoint(UniformParameters.PhysicalPoolSize.X, UniformParameters.PhysicalPoolSize.Y);
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
	OutEnvironment.SetDefine(TEXT("VSM_CACHE_ALIGNMENT_LEVEL"), FVirtualShadowMapArrayCacheManager::AlignmentLevel);
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
		return DoesPlatformSupportNanite(Parameters.Platform);
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
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, VisBuffer64)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPageRequestFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, VirtualShadowMapIdRemap)
		RDG_BUFFER_ACCESS(IndirectBufferArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER(uint32, InputType)
		SHADER_PARAMETER(uint32, NumDirectionalLightSmInds)
		SHADER_PARAMETER(uint32, bPostBasePass)
		SHADER_PARAMETER(float, ResolutionLodBiasLocal)
		SHADER_PARAMETER(float, PageDilationBorderSize)
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutHPageFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PageFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, PageRectBoundsOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGenerateHierarchicalPageFlagsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "GenerateHierarchicalPageFlags", SF_Compute);


class FInitPhysicalPageMetaData : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitPhysicalPageMetaData);
	SHADER_USE_PARAMETER_STRUCT(FInitPhysicalPageMetaData, FVirtualPageManagementShader )

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPhysicalPageMetaData >, PhysicalPageMetaDataOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitPhysicalPageMetaData, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitPhysicalPageMetaData", SF_Compute );

class FCreatePageMappingsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FCreatePageMappingsCS);
	SHADER_USE_PARAMETER_STRUCT(FCreatePageMappingsCS, FVirtualPageManagementShader)

	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim, FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,					PageRequestFlags )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				CoverageSummaryInOut )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				AllocatedPagesOffset )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				OutPageTable )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				OutStatsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FCachedPageInfo >,		OutCachedPageInfos )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				OutPageFlags)

		SHADER_PARAMETER_STRUCT_INCLUDE(FCacheDataParameters, CacheDataParameters)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCreatePageMappingsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "CreatePageMappings", SF_Compute);

class FPropagateMappedMipsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FPropagateMappedMipsCS);
	SHADER_USE_PARAMETER_STRUCT(FPropagateMappedMipsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER( FVirtualShadowMapUniformParameters,	VirtualShadowMap )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,			OutPageTable )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPropagateMappedMipsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "PropagateMappedMips", SF_Compute);

class FInitClearPhysicalPagesArgsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitClearPhysicalPagesArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FInitClearPhysicalPagesArgsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, NumAllocatedPhysicalPages)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer< uint >, ClearPhysicalPagesArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitClearPhysicalPagesArgsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitClearPhysicalPagesArgs", SF_Compute);

class FClearPhysicalPagesCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FClearPhysicalPagesCS);
	SHADER_USE_PARAMETER_STRUCT(FClearPhysicalPagesCS, FVirtualPageManagementShader)

	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PhysicalPagesTexture)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D< uint >, CachedPhysicalPagesTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FCachedPageInfo >, CachedPageInfos)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FPhysicalPageMetaData >, PhysicalPageMetaDataOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPhysicalPageMetaData >, PrevPhysicalPageMetaData)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FClearPhysicalPagesCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "ClearPhysicalPages", SF_Compute);


class FInitIndirectArgs1DCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitIndirectArgs1DCS);
	SHADER_USE_PARAMETER_STRUCT(FInitIndirectArgs1DCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, InputCountBuffer)
		SHADER_PARAMETER(uint32, Multiplier)
		SHADER_PARAMETER(uint32, Divisor)
		SHADER_PARAMETER(uint32, InputCountOffset)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer< uint >, IndirectDispatchArgsOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitIndirectArgs1DCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitIndirectArgs1D", SF_Compute);


FRDGBufferRef AddIndirectArgsSetupCsPass1D(FRDGBuilder& GraphBuilder, FRDGBufferRef &InputCountBuffer, uint32 Multiplier, uint32 Divisor = 1U, uint32 InputCountOffset = 0U)
{
	// 1. Add setup pass
	FRDGBufferRef IndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("Shadow.Virtual.IndirectArgs"));
	{
		FInitIndirectArgs1DCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitIndirectArgs1DCS::FParameters>();
		PassParameters->InputCountBuffer = GraphBuilder.CreateSRV(InputCountBuffer);
		PassParameters->Multiplier = Multiplier;
		PassParameters->Divisor = Divisor;
		PassParameters->InputCountOffset = InputCountOffset;
		PassParameters->IndirectDispatchArgsOut = GraphBuilder.CreateUAV(IndirectArgsBuffer, PF_R32_UINT);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FInitIndirectArgs1DCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitIndirectArgs1D"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}

	return IndirectArgsBuffer;
}


void FVirtualShadowMapArray::ClearPhysicalMemory(FRDGBuilder& GraphBuilder, FRDGTextureRef& PhysicalTexture)
{
	check(IsEnabled());
	if (ShadowMaps.Num() == 0)
	{
		return;
	}

	RDG_EVENT_SCOPE( GraphBuilder, "FVirtualShadowMapArray::ClearPhysicalMemory" );

	{
		FRDGBufferRef IndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("Shadow.Virtual.IndirectArgs"));
		{
			FInitClearPhysicalPagesArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitClearPhysicalPagesArgsCS::FParameters>();
			PassParameters->NumAllocatedPhysicalPages = GraphBuilder.CreateSRV(AllocatedPagesOffsetRDG);
			PassParameters->ClearPhysicalPagesArgs = GraphBuilder.CreateUAV(IndirectArgsBuffer, PF_R32_UINT);

			auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FInitClearPhysicalPagesArgsCS>();
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("InitClearPhysicalPagesArgs"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1)
			);
		}

		{
			FClearPhysicalPagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearPhysicalPagesCS::FParameters>();
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->PhysicalPagesTexture = GraphBuilder.CreateUAV(PhysicalTexture);
			PassParameters->IndirectArgs = IndirectArgsBuffer;
			bool bCacheDataAvailable = CacheManager && CacheManager->PrevBuffers.PhysicalPageMetaData;
			if (bCacheDataAvailable)
			{
				PassParameters->CachedPhysicalPagesTexture = RegisterExternalTextureWithFallback(GraphBuilder, CacheManager->PrevBuffers.PhysicalPagePool, GSystemTextures.BlackDummy);
				PassParameters->CachedPageInfos = GraphBuilder.CreateSRV(CachedPageInfosRDG);
				PassParameters->PrevPhysicalPageMetaData = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PhysicalPageMetaData));
			}
			PassParameters->PhysicalPageMetaDataOut = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);

			FClearPhysicalPagesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FClearPhysicalPagesCS::FHasCacheDataDim>(bCacheDataAvailable);
			auto ComputeShader = GetGlobalShaderMap( GMaxRHIFeatureLevel )->GetShader<FClearPhysicalPagesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearPhysicalMemory"),
				ComputeShader,
				PassParameters,
				PassParameters->IndirectArgs,
				0
			);
		}
	}
}

class FMarkRenderedPhysicalPagesCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FMarkRenderedPhysicalPagesCS);
	SHADER_USE_PARAMETER_STRUCT(FMarkRenderedPhysicalPagesCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, VirtualShadowMapFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FPhysicalPageMetaData >, InOutPhysicalPageMetaData)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMarkRenderedPhysicalPagesCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "MarkRenderedPhysicalPages", SF_Compute);

void FVirtualShadowMapArray::MarkPhysicalPagesRendered(FRDGBuilder& GraphBuilder, const TArray<uint32, SceneRenderingAllocator> &VirtualShadowMapFlags)
{
	check(IsEnabled());
	if (VirtualShadowMapFlags.Num() == 0)
	{
		return;
	}
	ensure(VirtualShadowMapFlags.Num() == ShadowMaps.Num());

	RDG_EVENT_SCOPE(GraphBuilder, "FVirtualShadowMapArray::MarkPhysicalPagesRendered");

	UniformParameters.NumShadowMaps = ShadowMaps.Num();

	{
		// One launch per All SMs, since they share page table data structure.
		FRDGBufferUploader BufferUploader;
		FRDGBufferRef VirtualShadowMapFlagsRDG = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.Flags"), VirtualShadowMapFlags);
		BufferUploader.Submit(GraphBuilder);

		FMarkRenderedPhysicalPagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FMarkRenderedPhysicalPagesCS::FParameters >();
		PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
		PassParameters->VirtualShadowMapFlags = GraphBuilder.CreateSRV(VirtualShadowMapFlagsRDG);
		PassParameters->InOutPhysicalPageMetaData = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FMarkRenderedPhysicalPagesCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MarkRenderedPhysicalPages"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(FVirtualShadowMap::PageTableSize, FMarkRenderedPhysicalPagesCS::DefaultCSGroupX), ShadowMaps.Num(), 1)
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, PageRectBoundsOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitPageRectBoundsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitPageRectBounds", SF_Compute);





class FCalculatePageRectsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FCalculatePageRectsCS);
	SHADER_USE_PARAMETER_STRUCT(FCalculatePageRectsCS, FVirtualPageManagementShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PageFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, PageRectBoundsOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPageTable)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCalculatePageRectsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "CalculatePageRects", SF_Compute);


class FAllocatePagesUsingRectsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FAllocatePagesUsingRectsCS);
	SHADER_USE_PARAMETER_STRUCT(FAllocatePagesUsingRectsCS, FVirtualPageManagementShader)

	//class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	//class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");

	//using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim, FGenerateStatsDim>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVirtualPageManagementShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Ensure these are off for now.
		OutEnvironment.SetDefine(TEXT("HAS_CACHE_DATA"), 0);
		OutEnvironment.SetDefine(TEXT("VSM_GENERATE_STATS"), 0);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, AllocatedPagesOffset)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPageTable)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutStatsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FCachedPageInfo >, OutCachedPageInfos)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPageFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FIntVector4 >, PageRectBounds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, AllocatedPageRectBoundsOut)

		//SHADER_PARAMETER_STRUCT_INCLUDE(FCacheDataParameters, CacheDataParameters)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FAllocatePagesUsingRectsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "AllocatePagesUsingRects", SF_Compute);

void FVirtualShadowMapArray::BuildPageAllocations(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const TArray<FViewInfo>& Views,
	const FSortedLightSetSceneInfo& SortedLightsInfo,
	const TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	const TArray<Nanite::FRasterResults, TInlineAllocator<2>>& NaniteRasterResults,
	bool bPostBasePass)
{
	check(IsEnabled());
	RDG_EVENT_SCOPE(GraphBuilder, "FVirtualShadowMapArray::BuildPageAllocation");

	const float ResolutionLodBiasLocal = CVarResolutionLodBiasLocal.GetValueOnRenderThread();
	const float PageDilationBorderSize = CVarPageDilationBorderSize.GetValueOnRenderThread();
	
	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SortedLights = SortedLightsInfo.SortedLights;
	if (ShadowMaps.Num())
	{
		// Store shadow map projection data for each virtual shadow map
		TArray<FVirtualShadowMapProjectionShaderData, SceneRenderingAllocator> ShadowMapProjectionData;
		ShadowMapProjectionData.AddDefaulted(ShadowMaps.Num());

		// Gather directional light virtual shadow maps
		TArray<int32, SceneRenderingAllocator> DirectionalLightSmInds;
		for (const FVisibleLightInfo& VisibleLightInfo : VisibleLightInfos)
		{
			for (const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap : VisibleLightInfo.VirtualShadowMapClipmaps)
			{
				// NOTE: Shader assumes all levels from a given clipmap are contiguous in both the remap and projection arrays
				int32 ClipmapID = Clipmap->GetVirtualShadowMap()->ID;
				DirectionalLightSmInds.Add(ClipmapID);
				for (int32 ClipmapLevel = 0; ClipmapLevel < Clipmap->GetLevelCount(); ++ClipmapLevel)
				{
					ShadowMapProjectionData[ClipmapID + ClipmapLevel] = Clipmap->GetProjectionShaderData(ClipmapLevel);
				}
			}

			for (FProjectedShadowInfo* ProjectedShadowInfo : VisibleLightInfo.AllProjectedShadows)
			{
				if (ProjectedShadowInfo->HasVirtualShadowMap())
				{
					check(ProjectedShadowInfo->CascadeSettings.ShadowSplitIndex == INDEX_NONE);		// We use clipmaps for virtual shadow maps, not cascades

					// NOTE: Virtual shadow maps are never atlased, but verify our assumptions
					{
						const FVector4 ClipToShadowUV = ProjectedShadowInfo->GetClipToShadowBufferUvScaleBias();
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
						Data.TranslatedWorldToShadowViewMatrix		= ViewMatrices.GetTranslatedViewMatrix();
						Data.ShadowViewToClipMatrix					= ViewMatrices.GetProjectionMatrix();
						Data.TranslatedWorldToShadowUVMatrix		= CalcTranslatedWorldToShadowUVMatrix( ViewMatrices.GetTranslatedViewMatrix(), ViewMatrices.GetProjectionMatrix() );
						Data.TranslatedWorldToShadowUVNormalMatrix	= CalcTranslatedWorldToShadowUVNormalMatrix( ViewMatrices.GetTranslatedViewMatrix(), ViewMatrices.GetProjectionMatrix() );
						Data.ShadowPreViewTranslation				= FVector(ProjectedShadowInfo->PreShadowTranslation);
						Data.VirtualShadowMapId						= ID;
						Data.LightType								= ProjectedShadowInfo->GetLightSceneInfo().Proxy->GetLightType();
					}
				}
			}
		}

		UniformParameters.NumShadowMaps = ShadowMaps.Num();
		UniformParameters.NumDirectionalLights = DirectionalLightSmInds.Num();

		FRDGBufferUploader BufferUploader;
		ShadowMapProjectionDataRDG = CreateProjectionDataBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.ProjectionData"), ShadowMapProjectionData);
		BufferUploader.Submit(GraphBuilder);

		UniformParameters.ProjectionData = GraphBuilder.CreateSRV(ShadowMapProjectionDataRDG);

		if (CVarShowStats.GetValueOnRenderThread() || (CacheManager && CacheManager->IsAccumulatingStats()))
		{
			StatsBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumStats), TEXT("Shadow.Virtual.StatsBuffer"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StatsBufferRDG), 0);
		}
		
		// Create and clear the requested page flags
		const uint32 NumPageFlags = ShadowMaps.Num() * FVirtualShadowMap::PageTableSize;
		FRDGBufferRef PageRequestFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("Shadow.Virtual.PageRequestFlags"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageRequestFlagsRDG), 0);
		DynamicCasterPageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("Shadow.Virtual.DynamicCasterPageFlags"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DynamicCasterPageFlagsRDG), 0);

		// Total storage for Hierarchical page tables for all virtual shadow maps
		const uint32 NumHPageFlags = ShadowMaps.Num() * UniformParameters.HPageTableSize;
		HPageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumHPageFlags), TEXT("Shadow.Virtual.HPageFlags"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(HPageFlagsRDG), 0);

		const uint32 NumPageRects = UniformParameters.NumShadowMaps * FVirtualShadowMap::MaxMipLevels;
		PageRectBoundsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector4), NumPageRects), TEXT("Shadow.Virtual.PageRectBounds"));
		{
			FInitPageRectBoundsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitPageRectBoundsCS::FParameters >();
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->PageRectBoundsOut = GraphBuilder.CreateUAV(PageRectBoundsRDG);

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
			FRDGTextureRef NaniteVisBuffer64 = ViewIndex < NaniteRasterResults.Num() ? NaniteRasterResults[ViewIndex].VisBuffer64 : nullptr;

			// This view contained no local lights (that were stored in the light grid), and no directional lights, so nothing to do.
			if (View.ForwardLightingResources->LocalLightVisibleLightInfosIndex.Num() + DirectionalLightSmInds.Num() == 0)
			{
				continue;
			}

			// Build light-index-in-light-grid => virtual-shadow-map-index remap, must be built for each view since they have different sub-sets of lights
			// TODO: change this engine behaviour and instead upload lights once and for all, such that all indexes can refer to the same light set.
			TArray<int32, SceneRenderingAllocator> VirtualShadowMapIdRemap = DirectionalLightSmInds;
			// Note: the remap for the local lights is stored after the directional lights, such that this array is always non-empty...
			VirtualShadowMapIdRemap.AddDefaulted(View.ForwardLightingResources->LocalLightVisibleLightInfosIndex.Num());
			for (int32 L = 0; L < View.ForwardLightingResources->LocalLightVisibleLightInfosIndex.Num(); ++L)
			{
				// Default value
				VirtualShadowMapIdRemap[DirectionalLightSmInds.Num() + L] = INDEX_NONE;

				int32 VisibleLightInfosIndex = View.ForwardLightingResources->LocalLightVisibleLightInfosIndex[L];
				// This can be invalid for example for so-called 'Simple lights' which are injected into the light grid, but not present elsewhere.
				if (VisibleLightInfosIndex != INDEX_NONE)
				{
					const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[VisibleLightInfosIndex];

					// Get hold of info about this light to figure out if there is a virtual SM
					if (FProjectedShadowInfo *ShadowInfo = GetVirtualShadowMapInfo(VisibleLightInfo))
					{
						ensure(ShadowInfo->VirtualShadowMaps.Num());
						ensure(ShadowInfo->VirtualShadowMaps[0]->ID != INDEX_NONE);
						VirtualShadowMapIdRemap[DirectionalLightSmInds.Num() + L] = ShadowInfo->VirtualShadowMaps[0]->ID;
					}
				}
			}

			VirtualShadowMapIdRemapRDG.Add( CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.IdRemap"), VirtualShadowMapIdRemap) );
			BufferUploader.Submit(GraphBuilder);

			const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
			FRDGTextureRef VisBuffer64 = NaniteVisBuffer64 != nullptr ? NaniteVisBuffer64 : SystemTextures.Black;

			FRDGBufferRef ScreenSpaceGridBoundsRDG = nullptr;
			
			{
				// It's safe to overlap these passes that all write to page request flags
				FRDGBufferUAVRef PageRequestFlagsUAV = GraphBuilder.CreateUAV(PageRequestFlagsRDG, ERDGUnorderedAccessViewFlags::SkipBarrier);

				// Mark pages based on projected depth buffer pixels
				if (CVarMarkPixelPages.GetValueOnRenderThread() != 0)
				{
					auto GeneratePageFlags = [&](bool bHairPass)
					{
						const bool bUseNaniteDepth = NaniteVisBuffer64 != nullptr && !bPostBasePass;
						const uint32 InputType = bHairPass ? 2u : (bUseNaniteDepth ? 1u : 0u); // HairStrands, Nanite, or GBuffer

						FGeneratePageFlagsFromPixelsCS::FPermutationDomain PermutationVector;
						PermutationVector.Set<FGeneratePageFlagsFromPixelsCS::FInputType>(InputType);
						FGeneratePageFlagsFromPixelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FGeneratePageFlagsFromPixelsCS::FParameters >();
						PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);

						PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
						PassParameters->bPostBasePass = bPostBasePass;

						PassParameters->VisBuffer64 = VisBuffer64;
						PassParameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
						PassParameters->View = View.ViewUniformBuffer;
						PassParameters->OutPageRequestFlags = PageRequestFlagsUAV;
						PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
						PassParameters->VirtualShadowMapIdRemap = GraphBuilder.CreateSRV(VirtualShadowMapIdRemapRDG[ViewIndex]);
						PassParameters->NumDirectionalLightSmInds = DirectionalLightSmInds.Num();
						PassParameters->ResolutionLodBiasLocal = ResolutionLodBiasLocal;
						PassParameters->PageDilationBorderSize = PageDilationBorderSize;

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
								0);
						}
						else
						{
							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("GeneratePageFlagsFromPixels(%s)", bHairPass ? TEXT("HairStrands") : (bUseNaniteDepth ? TEXT("Nanite") : TEXT("GBuffer"))),
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

		FRDGBufferRef HInvalidPageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumHPageFlags), TEXT("Shadow.Virtual.HInvalidPageFlags"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(HInvalidPageFlagsRDG), 0);

		// Create and clear the counter / page offset, it gets atomically incremented to allocate the physical pages
		AllocatedPagesOffsetRDG = GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),  1), TEXT("Shadow.Virtual.AllocatedPagesOffset") );
		AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV(AllocatedPagesOffsetRDG), 0 );

		// Enough space for all physical pages that might be allocated
		CachedPageInfosRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FCachedPageInfo), UniformParameters.MaxPhysicalPages), TEXT("Shadow.Virtual.CachedPageInfos"));
		PhysicalPageMetaDataRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FPhysicalPageMetaData), UniformParameters.MaxPhysicalPages), TEXT("Shadow.Virtual.PhysicalPageMetaData"));

		AllocatedPageRectBoundsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector4), NumPageRects), TEXT("Shadow.Virtual.AllocatedPageRectBounds"));

		{
			FInitPhysicalPageMetaData::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitPhysicalPageMetaData::FParameters >();
			PassParameters->PhysicalPageMetaDataOut = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);

			auto ComputeShader = Views[0].ShaderMap->GetShader< FInitPhysicalPageMetaData >();
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("InitPhysicalPageMetaData"),
				ComputeShader,
				PassParameters,
				FIntVector(FMath::DivideAndRoundUp(UniformParameters.MaxPhysicalPages, FInitPhysicalPageMetaData::DefaultCSGroupX), 1, 1)
			);
		}
		
		const bool bAllocatePageRectAtlas = CVarAllocatePagesUsingRects.GetValueOnRenderThread() != 0;
		if (bAllocatePageRectAtlas)
		{
			// 1. Build page rects from requested pages
			{
				FCalculatePageRectsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCalculatePageRectsCS::FParameters >();
				PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
				PassParameters->PageFlags = GraphBuilder.CreateSRV(PageRequestFlagsRDG);
				PassParameters->PageRectBoundsOut = GraphBuilder.CreateUAV(PageRectBoundsRDG);
				PassParameters->OutPageTable = GraphBuilder.CreateUAV(PageTableRDG);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageFlagsRDG), 0);

				auto ComputeShader = Views[0].ShaderMap->GetShader< FCalculatePageRectsCS >();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CalculatePageRequestRects"),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(FIntVector(FVirtualShadowMap::Level0DimPagesXY, FVirtualShadowMap::Level0DimPagesXY, ShadowMaps.Num()), FIntVector(FCalculatePageRectsCS::DefaultCSGroupXY, FCalculatePageRectsCS::DefaultCSGroupXY, 1U))
				);
			}


			// 2. Allocate and all that
			{
				FAllocatePagesUsingRectsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FAllocatePagesUsingRectsCS::FParameters >();
				PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
				PassParameters->PageRectBounds = GraphBuilder.CreateSRV(PageRectBoundsRDG);

				PassParameters->OutPageTable = GraphBuilder.CreateUAV(PageTableRDG);
				PassParameters->OutCachedPageInfos = GraphBuilder.CreateUAV(CachedPageInfosRDG);
				PassParameters->OutPageFlags = GraphBuilder.CreateUAV(PageFlagsRDG);
				PassParameters->AllocatedPageRectBoundsOut = GraphBuilder.CreateUAV(AllocatedPageRectBoundsRDG);

				auto ComputeShader = Views[0].ShaderMap->GetShader< FAllocatePagesUsingRectsCS >();
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("AllocatePagesUsingRects"),
					ComputeShader,
					PassParameters,
					FIntVector(1U, 1U, 1U)
				);
			}
		}
		else
		{
			// Note: does not actually need mip0 so can be trimmed down a bit
			FRDGBufferRef CoverageSummary = GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateStructuredDesc( 4, NumPageFlags ), TEXT("Shadow.Virtual.CoverageSummary") );
			
			AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( CoverageSummary ), 0 );
			
			// Run a pass to create page mappings
			FCreatePageMappingsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCreatePageMappingsCS::FParameters >();
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);

			PassParameters->PageRequestFlags		= GraphBuilder.CreateSRV( PageRequestFlagsRDG );
			PassParameters->CoverageSummaryInOut	= GraphBuilder.CreateUAV( CoverageSummary );
			PassParameters->AllocatedPagesOffset	= GraphBuilder.CreateUAV( AllocatedPagesOffsetRDG);
			PassParameters->OutPageTable			= GraphBuilder.CreateUAV( PageTableRDG );
			PassParameters->OutCachedPageInfos		= GraphBuilder.CreateUAV( CachedPageInfosRDG );
			PassParameters->OutPageFlags			= GraphBuilder.CreateUAV( PageFlagsRDG );

			bool bCacheDataAvailable = CacheManager && CacheManager->IsValid();
			if (bCacheDataAvailable)
			{
				SetCacheDataShaderParameters(GraphBuilder, ShadowMaps, CacheManager, PassParameters->CacheDataParameters);
			}
			PassParameters->OutStatsBuffer = StatsBufferRDG ? GraphBuilder.CreateUAV(StatsBufferRDG) : nullptr;

			// Invoked one CS thread per 2x2 mip0 texels (i.e. one per mip1 texel)
			const uint32 DispatchWidthThreads = FVirtualShadowMap::Level0DimPagesXY >> 1;

			FCreatePageMappingsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FCreatePageMappingsCS::FHasCacheDataDim>(bCacheDataAvailable);
			PermutationVector.Set<FCreatePageMappingsCS::FGenerateStatsDim>(StatsBufferRDG != nullptr);
			auto ComputeShader = Views[0].ShaderMap->GetShader< FCreatePageMappingsCS >(PermutationVector);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CreatePageMappingsCS"),
				ComputeShader,
				PassParameters,
				FIntVector(FMath::DivideAndRoundUp(FMath::Square(DispatchWidthThreads), FCreatePageMappingsCS::DefaultCSGroupX), ShadowMaps.Num(), 1)
			);
		}

		{
			// Run pass building hierarchical page flags to make culling acceptable performance.
			FGenerateHierarchicalPageFlagsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FGenerateHierarchicalPageFlagsCS::FParameters >();
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->OutHPageFlags = GraphBuilder.CreateUAV(HPageFlagsRDG);
			PassParameters->PageFlags = GraphBuilder.CreateSRV(PageFlagsRDG);
			PassParameters->PageRectBoundsOut = GraphBuilder.CreateUAV(PageRectBoundsRDG);

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

			const uint32 DispatchWidthThreads = FVirtualShadowMap::Level0DimPagesXY;

			auto ComputeShader = Views[0].ShaderMap->GetShader<FPropagateMappedMipsCS>();
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PropagateMappedMips"),
				ComputeShader,
				PassParameters,
				FIntVector(FMath::DivideAndRoundUp(FMath::Square(DispatchWidthThreads), FPropagateMappedMipsCS::DefaultCSGroupX), ShadowMaps.Num(), 1)
			);
		}

		UniformParameters.PageTable = GraphBuilder.CreateSRV(PageTableRDG);
	}
}

void FVirtualShadowMapArray::SetupProjectionParameters(FRDGBuilder& GraphBuilder)
{
	UniformParameters.PhysicalPagePool = PhysicalPagePoolRDG ? PhysicalPagePoolRDG : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	UniformParameters.PhysicalPagePoolHw = PhysicalPagePoolHw ? PhysicalPagePoolHw : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, HPageFlags)
		
		SHADER_PARAMETER_RDG_TEXTURE(	Texture2D< float >,			HZBPhysical )
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

		PassParameters->PageFlags			= GraphBuilder.CreateSRV( PageFlagsRDG );
		PassParameters->HPageFlags			= GraphBuilder.CreateSRV( HPageFlagsRDG );

		// TODO: unclear if it's preferable to debug the HZB we generated "this frame" here, or the previous frame (that was used for culling)?
		// We'll stick with the previous frame logic that was there, but it's cleaner to just reference the current frame one
		TRefCountPtr<IPooledRenderTarget> PrevHZBPhysical = CacheManager ? CacheManager->PrevBuffers.HZBPhysical : nullptr;
		TRefCountPtr<FRDGPooledBuffer>    PrevPageTable = CacheManager ? CacheManager->PrevBuffers.PageTable : nullptr;
		PassParameters->HZBPhysical			= RegisterExternalTextureWithFallback( GraphBuilder, PrevHZBPhysical, GSystemTextures.BlackDummy );
		PassParameters->HZBPageTable		= GraphBuilder.CreateSRV( PrevPageTable ? GraphBuilder.RegisterExternalBuffer( PrevPageTable ) : PageTableRDG );

		PassParameters->DebugTargetWidth = DebugTargetWidth;
		PassParameters->DebugTargetHeight = DebugTargetHeight;
		PassParameters->BorderWidth = BorderWidth;
		PassParameters->ZoomScaleFactor = ZoomScaleFactor;
		PassParameters->DebugMethod = DebugMethod;

		bool bCacheDataAvailable = CacheManager && CacheManager->IsValid();
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

		DebugVisualizationOutput = GraphBuilder.ConvertToExternalTexture(DebugOutput);
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
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmPrintStatsCS, "/Engine/Private/VirtualShadowMaps/PrintStats.usf", "PrintStats", SF_Compute);

void FVirtualShadowMapArray::PrintStats(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	check(IsEnabled());
	LLM_SCOPE_BYTAG(Nanite);

	// Print stats
	if (CVarShowStats.GetValueOnRenderThread() != 0 && StatsBufferRDG)
	{
		{
			FVirtualSmPrintStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmPrintStatsCS::FParameters>();

			ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintStruct);
			PassParameters->InStatsBuffer = GraphBuilder.CreateSRV(StatsBufferRDG);
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);

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

				MipView.ViewSizeAndInvSize = FVector4(ViewSize.X, ViewSize.Y, 1.0f / float(ViewSize.X), 1.0f / float(ViewSize.Y));
				MipView.ViewRect = FIntVector4(ViewMin.X, ViewMin.Y, ViewMin.X + ViewSize.X, ViewMin.Y + ViewSize.Y);

				MipView.UpdateLODScales();
				MipView.LODScales.X *= LODScaleFactor;
			}

			MipView.HZBTestViewRect = MipView.ViewRect;	// Assumed to always be the same for VSM

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
		}
	}

	// Remove unused mip views
	check(MaxMips > 0);
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


void FVirtualShadowMapArray::GetPageTableParameters(FRDGBuilder& GraphBuilder, FVirtualShadowMapPageTableParameters& OutParameters)
{
	OutParameters.VirtualShadowMap = GetUniformBuffer(GraphBuilder);
	OutParameters.PageFlags = GraphBuilder.CreateSRV(PageFlagsRDG);
	OutParameters.HPageFlags = GraphBuilder.CreateSRV(HPageFlagsRDG);
	OutParameters.PageRectBounds = GraphBuilder.CreateSRV(PageRectBoundsRDG);
	OutParameters.PageTable = GraphBuilder.CreateSRV(PageTableRDG);
}

BEGIN_SHADER_PARAMETER_STRUCT(FVirtualShadowDepthPassParameters,)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FShadowDepthPassUniformParameters, ShadowDepthPass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


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
	class FLoopOverViewsDim	: SHADER_PERMUTATION_BOOL( "LOOP_OVER_VIEWS" );
	using FPermutationDomain = TShaderPermutationDomain< FNearClipDim, FLoopOverViewsDim >;

public:
	static constexpr int32 NumThreadsPerGroup = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapPageTableParameters, PageTableParams)

		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
		SHADER_PARAMETER(uint32, InstanceDataSOAStride)
		SHADER_PARAMETER(uint32, GPUSceneFrameNumber)


		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer< uint>, InstanceIdsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer< uint>, DrawCommandIdsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, NumInstanceIdsBuffer)
		SHADER_PARAMETER(int32, FirstPrimaryView)
		SHADER_PARAMETER(int32, NumPrimaryViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FInstanceCullingContext::FPrimCullingCommand >, PrimitiveCullingCommands)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVisibleInstanceCmd>, VisibleInstancesOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, DrawCommandInstanceCountBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, VisibleInstanceCountBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutDynamicCasterFlags)
		SHADER_PARAMETER(int32, bInstancePerPage)

		// Needed reference for make RDG happy somehow
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
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
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumDrawCommands)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawIndirectArgsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, InstanceIdOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, TmpInstanceIdOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DrawCommandInstanceCountBuffer)
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
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FVisibleInstanceCmd >, VisibleInstances)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, InstanceIdsBufferLegacyOut)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, PageInfoBufferLegacyOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, TmpInstanceIdOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleInstanceCountBuffer)

		// Needed reference for make RDG happy somehow
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FOutputCommandInstanceListsCs, "/Engine/Private/VirtualShadowMaps/BuildPerPageDrawCommands.usf", "OutputCommandInstanceListsCs", SF_Compute);

void FVirtualShadowMapArray::RenderVirtualShadowMapsHw(FRDGBuilder& GraphBuilder, const TArray<FProjectedShadowInfo *, SceneRenderingAllocator>& VirtualSmMeshCommandPasses, FScene& Scene)
{
	if (VirtualSmMeshCommandPasses.Num() == 0)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "RenderVirtualShadowMapsHw");

	const bool bAllocatePageRectAtlas = CVarAllocatePagesUsingRects.GetValueOnRenderThread() != 0;
	const bool bAtomicWrites = GVirtualShadowMapAtomicWrites != 0;

	FGPUScene& GPUScene = Scene.GPUScene;
	// Create that pass struct and whatever I guess.

	// track whether the page pool was cleared
	bool bWasCleared = bAtomicWrites;

	if( !bAtomicWrites )
	{
		// Create physical page pool
		PhysicalPagePoolHw = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(GetPhysicalPoolSize(), PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource), TEXT("Shadow.Virtual.PhysicalPagePoolHw"));

		if( !bAllocatePageRectAtlas )
		{
			AddInitializePhysicalPagesHwPass(GraphBuilder);
			bWasCleared = true;
		}
	}

	TArray<uint32, SceneRenderingAllocator> VirtualShadowMapFlags;
	VirtualShadowMapFlags.AddZeroed(ShadowMaps.Num());

	// TODO: create a VirtualClipMapMeshRenderInfo - or something - that contains the required info:
	//       The View used to set up (to get all the usual crap), The MeshCommandPass (to perform setup & culling), Culling volume?, Ref to Clipmap itself.

	// Loop over the mesh command passes needed (one for each SM, we need to do this on a per-SM basis mainly because different views may have different view infos)
	// In practice there is no fundamental reason why we could not merge all the draws into one giant submission - should probably consider this at some point. 
	// One question is whether the shaders refer to the view, and actually can get sensible data out (I'm not sure this is the case at present anyway, as it contains a bastardization of main view and shadow view info)
	// The other one to beware of is the fact that we have these dynamic primitives, which until this is fixed will cause the creation of an entire copy of the GPU scene data for each shadow view that contains one...
	//   Starting to become urgent!
	for (int Index = 0; Index < VirtualSmMeshCommandPasses.Num(); ++Index)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = VirtualSmMeshCommandPasses[Index];

		FParallelMeshDrawCommandPass& MeshCommandPass = ProjectedShadowInfo->GetShadowDepthPass();
		// TODO: Right now, this is for clipmaps only! Get hold of them somehow!
		const TSharedPtr<FVirtualShadowMapClipmap> Clipmap = ProjectedShadowInfo->VirtualShadowMapClipmap;
		// TODO: Also get the associated view such that all the parameters can be set.
		const FViewInfo* ViewUsedToCreateShadow = ProjectedShadowInfo->DependentView;
		FViewInfo* ShadowDepthView = ProjectedShadowInfo->ShadowDepthView;

		const FViewInfo& View = *ViewUsedToCreateShadow;
		TArray<Nanite::FPackedView, SceneRenderingAllocator> VirtualShadowViews;

		ProjectedShadowInfo->BeginRenderView(GraphBuilder, &Scene);

		if( Clipmap )
		{
			Nanite::FPackedViewParams BaseParams;
			BaseParams.ViewRect = FIntRect(0, 0, FVirtualShadowMap::VirtualMaxResolutionXY, FVirtualShadowMap::VirtualMaxResolutionXY);
			BaseParams.RasterContextSize = GetPhysicalPoolSize();
			BaseParams.PrevTargetLayerIndex = INDEX_NONE;
			BaseParams.TargetMipLevel = 0;
			BaseParams.TargetMipCount = 1;	// No mips for clipmaps

			for (int32 ClipmapLevelIndex = 0; ClipmapLevelIndex < Clipmap->GetLevelCount(); ++ClipmapLevelIndex)
			{
				Nanite::FPackedViewParams Params = BaseParams;
				Params.TargetLayerIndex = Clipmap->GetVirtualShadowMap(ClipmapLevelIndex)->ID;
				Params.ViewMatrices = Clipmap->GetViewMatrices(ClipmapLevelIndex);
				Params.PrevTargetLayerIndex = INDEX_NONE;
				Params.PrevViewMatrices = Params.ViewMatrices;

				VirtualShadowViews.Add(Nanite::CreatePackedView(Params));

				// This is used to mark all the referenced physical pages as being updated. 
				// If rendering was disabled or some such we can't assume the content of a mapped page is useful.
				VirtualShadowMapFlags[Params.TargetLayerIndex] = 1;
			}
		}
		else if (ProjectedShadowInfo->HasVirtualShadowMap())
		{

			Nanite::FPackedViewParams BaseParams;
			BaseParams.ViewRect = ProjectedShadowInfo->GetOuterViewRect();
			BaseParams.HZBTestViewRect = BaseParams.ViewRect;
			BaseParams.RasterContextSize = GetPhysicalPoolSize();
			//BaseParams.LODScaleFactor = ComputeNaniteShadowsLODScaleFactor();
			BaseParams.PrevTargetLayerIndex = INDEX_NONE;
			BaseParams.TargetMipLevel = 0;
			BaseParams.TargetMipCount = FVirtualShadowMap::MaxMipLevels;

			int32 NumMaps = ProjectedShadowInfo->bOnePassPointLightShadow ? 6 : 1;
			for( int32 i = 0; i < NumMaps; i++ )
			{
				Nanite::FPackedViewParams Params = BaseParams;
				Params.TargetLayerIndex = ProjectedShadowInfo->VirtualShadowMaps[i]->ID;
				Params.ViewMatrices = ProjectedShadowInfo->GetShadowDepthRenderingViewMatrices(i, true);

				VirtualShadowViews.Add(Nanite::CreatePackedView(Params));
				VirtualShadowMapFlags[ProjectedShadowInfo->VirtualShadowMaps[i]->ID] = 1;
			}
		}

		int32 NumPrimaryViews = VirtualShadowViews.Num();
		CreateMipViews( VirtualShadowViews );

		// Created by BuildInstanceList
		// 1. Run post-cull unpack command (?) - maybe skip straight to the second pass, or output to intermediate instance index buffer?
		//MeshCommandPass.BuildRenderingCommands(GraphBuilder, GPUScene, InstanceListBuffer, InstanceCountBuffer);
		// 1.1. Pass to unpack into flat drawcommand + instance ID pairs
		FInstanceCullingRdgParams Params;
		MeshCommandPass.BuildInstanceList(GraphBuilder, GPUScene, Params);

		const int32 NumDrawCommands = MeshCommandPass.GetInstanceCullingContext()->CullingCommands.Num();
		if (NumDrawCommands > 0)
		{
			FString LightNameWithLevel;
			FSceneRenderer::GetLightNameForDrawEvent(ProjectedShadowInfo->GetLightSceneInfo().Proxy, LightNameWithLevel);
			RDG_EVENT_SCOPE(GraphBuilder, "%s", *LightNameWithLevel);

			FRDGBufferUploader BufferUploader;

			TArray<int32, SceneRenderingAllocator> DrawCommandInstanceCountTmp;
			DrawCommandInstanceCountTmp.AddZeroed(NumDrawCommands);
			FRDGBufferRef DrawCommandInstanceCountRDG = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.DrawCommandInstanceCount"), DrawCommandInstanceCountTmp);
			FRDGBufferRef TmpInstanceIdOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.TmpInstanceIdOffsetBuffer"), DrawCommandInstanceCountTmp);
			//AddBuildInstanceListPass(GraphBuilder, GPUScene, InstanceIdBuffer, InstanceCountBuffer);
			// 2. Run page culling & command building pass(es)
			// This takes the per high-level view instance list and performs page overlap tests and amplification/compaction for all target mip levels OR clip levels.
			//if (IsClipMap())
			// AddPageRenderCommandPass(GraphBuilder, GPUScene, InstanceIdBuffer, InstanceCountBuffer, ShadowMapViews);
			// 2.1. Pass to perform the culling & replication to each page, store 
			const uint32 MaxNumInstancesPerPass = NumDrawCommands * FInstanceCullingManager::MaxAverageInstanceFactor * 64u;
			FRDGBufferRef VisibleInstancesRdg = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.VisibleInstances"), sizeof(FVisibleInstanceCmd), MaxNumInstancesPerPass, nullptr, 0);

			TArray<uint32> NullArray;
			NullArray.AddZeroed(1);
			FRDGBufferRef VisibleInstanceWriteOffsetRDG = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.VisibleInstanceWriteOffset"), NullArray);
			FRDGBufferRef OutputOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.OutputOffsetBuffer"), NullArray);
			FRDGBufferRef VirtualShadowViewsRDG = CreateStructuredBuffer(GraphBuilder, BufferUploader, TEXT("Shadow.Virtual.VirtualShadowViews"), VirtualShadowViews);

			BufferUploader.Submit(GraphBuilder);

			// TODO: Remove this when everything is properly RDG'd
			AddPass(GraphBuilder, [](FRHICommandList& RHICmdList)
			{
				FRHITransitionInfo 	Transitions[2] =
				{
					FRHITransitionInfo(GInstanceCullingManagerResources.GetInstancesIdBufferUav(), ERHIAccess::Unknown, ERHIAccess::UAVCompute),
					FRHITransitionInfo(GInstanceCullingManagerResources.GetPageInfoBufferUav(), ERHIAccess::Unknown, ERHIAccess::UAVCompute)
				};

				RHICmdList.Transition(Transitions);
			});

			{
				FRDGBufferRef IndirectArgs = AddIndirectArgsSetupCsPass1D(GraphBuilder, Params.InstanceIdWriteOffsetBuffer, 1, FCullPerPageDrawCommandsCs::NumThreadsPerGroup);

				FCullPerPageDrawCommandsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullPerPageDrawCommandsCs::FParameters>();

				GetPageTableParameters(GraphBuilder, PassParameters->PageTableParams);

				PassParameters->GPUSceneInstanceSceneData = GPUScene.InstanceDataBuffer.SRV;
				PassParameters->GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;
				PassParameters->InstanceDataSOAStride = GPUScene.InstanceDataSOAStride;
				PassParameters->GPUSceneFrameNumber = GPUScene.GetSceneFrameNumber();
				PassParameters->InstanceIdsBuffer = GraphBuilder.CreateSRV(Params.InstanceIdsBuffer, PF_R32_UINT);
				PassParameters->DrawCommandIdsBuffer = GraphBuilder.CreateSRV(Params.DrawCommandIdsBuffer, PF_R32_UINT);
				PassParameters->NumInstanceIdsBuffer = GraphBuilder.CreateSRV(Params.InstanceIdWriteOffsetBuffer);
				PassParameters->FirstPrimaryView = 0;
				PassParameters->NumPrimaryViews = NumPrimaryViews;
				PassParameters->InViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);
				PassParameters->PrimitiveCullingCommands = GraphBuilder.CreateSRV(Params.PrimitiveCullingCommands);
				PassParameters->VisibleInstancesOut = GraphBuilder.CreateUAV(VisibleInstancesRdg);
				PassParameters->DrawCommandInstanceCountBufferOut = GraphBuilder.CreateUAV(DrawCommandInstanceCountRDG);
				PassParameters->VisibleInstanceCountBufferOut = GraphBuilder.CreateUAV(VisibleInstanceWriteOffsetRDG);
				PassParameters->OutDynamicCasterFlags = GraphBuilder.CreateUAV(DynamicCasterPageFlagsRDG);
				PassParameters->IndirectArgs = IndirectArgs;
				PassParameters->bInstancePerPage = !bAllocatePageRectAtlas && !bAtomicWrites;

				FCullPerPageDrawCommandsCs::FPermutationDomain PermutationVector;
				PermutationVector.Set< FCullPerPageDrawCommandsCs::FNearClipDim >( !Clipmap.IsValid() );
				PermutationVector.Set< FCullPerPageDrawCommandsCs::FLoopOverViewsDim >( Clipmap.IsValid() );

				auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FCullPerPageDrawCommandsCs>( PermutationVector );

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CullPerPageDrawCommandsCs"),
					ComputeShader,
					PassParameters,
					IndirectArgs,
					0
				);
			}
			// 2.2.Allocate space for the final instance ID output and so on.
			{
				FAllocateCommandInstanceOutputSpaceCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocateCommandInstanceOutputSpaceCs::FParameters>();

				// get this in a neater way
				FRDGBufferRef InstanceIdOutOffsetBufferRDG = MeshCommandPass.GetInstanceCullingContext()->InstanceCullingManager->CullingIntermediate.InstanceIdOutOffsetBuffer;

				PassParameters->NumDrawCommands = NumDrawCommands;
				PassParameters->DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(Params.DrawIndirectArgs, PF_R32_UINT);
				PassParameters->InstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(Params.InstanceIdStartOffsetBuffer, PF_R32_UINT);;
				PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG);
				PassParameters->TmpInstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(TmpInstanceIdOffsetBufferRDG);
				PassParameters->DrawCommandInstanceCountBuffer = GraphBuilder.CreateSRV(DrawCommandInstanceCountRDG);

				auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FAllocateCommandInstanceOutputSpaceCs>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("AllocateCommandInstanceOutputSpaceCs"),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(NumDrawCommands, FAllocateCommandInstanceOutputSpaceCs::NumThreadsPerGroup)
				);

			}
			// 2.3. Perform final pass to re-shuffle the instance ID's to their final resting places
			{
				FRDGBufferRef IndirectArgs = AddIndirectArgsSetupCsPass1D(GraphBuilder, VisibleInstanceWriteOffsetRDG, 1, FOutputCommandInstanceListsCs::NumThreadsPerGroup);

				FOutputCommandInstanceListsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FOutputCommandInstanceListsCs::FParameters>();

				PassParameters->VisibleInstances = GraphBuilder.CreateSRV(VisibleInstancesRdg);
				PassParameters->PageInfoBufferLegacyOut = GInstanceCullingManagerResources.GetPageInfoBufferUav();
				PassParameters->InstanceIdsBufferLegacyOut = GInstanceCullingManagerResources.GetInstancesIdBufferUav();
				PassParameters->TmpInstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(TmpInstanceIdOffsetBufferRDG);
				PassParameters->VisibleInstanceCountBuffer = GraphBuilder.CreateSRV(VisibleInstanceWriteOffsetRDG);
				PassParameters->IndirectArgs = IndirectArgs;

				auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FOutputCommandInstanceListsCs>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("OutputCommandInstanceListsCs"),
					ComputeShader,
					PassParameters,
					IndirectArgs,
					0
				);
			}

			if( PhysicalPagePoolRDG == nullptr )
			{
				PhysicalPagePoolRDG = GraphBuilder.CreateTexture( FRDGTextureDesc::Create2D( GetPhysicalPoolSize(), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV ), TEXT("Shadow.Virtual.PhysicalPagePool") );

				ClearPhysicalMemory(GraphBuilder, PhysicalPagePoolRDG);
			}

			FVirtualShadowDepthPassParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualShadowDepthPassParameters>();
			PassParameters->View = ShadowDepthView->ViewUniformBuffer;

			if( !bAtomicWrites )
			{
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(PhysicalPagePoolHw, bWasCleared ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilNop);
				bWasCleared = true;
			}

			FShadowDepthPassUniformParameters* ShadowDepthPassParameters = GraphBuilder.AllocParameters<FShadowDepthPassUniformParameters>();

			SetupSceneTextureUniformParameters(GraphBuilder, GMaxRHIFeatureLevel, ESceneTextureSetupMode::None, ShadowDepthPassParameters->SceneTextures);

			ShadowDepthPassParameters->bClampToNearPlane = ProjectedShadowInfo->ShouldClampToNearPlane();

			// TODO: These are not used for this case anyway
			ShadowDepthPassParameters->ProjectionMatrix = FMatrix::Identity;
			ShadowDepthPassParameters->ViewMatrix = FMatrix::Identity;
			ShadowDepthPassParameters->ShadowParams = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
			ShadowDepthPassParameters->bRenderToVirtualShadowMap = true;
			ShadowDepthPassParameters->bInstancePerPage = !bAllocatePageRectAtlas && !bAtomicWrites;
			ShadowDepthPassParameters->bAtomicWrites = bAtomicWrites;
			
			ShadowDepthPassParameters->VirtualSmPageTable	= GraphBuilder.CreateSRV( PageTableRDG );
			ShadowDepthPassParameters->PackedNaniteViews	= GraphBuilder.CreateSRV( VirtualShadowViewsRDG );
			ShadowDepthPassParameters->PageRectBounds		= GraphBuilder.CreateSRV( PageRectBoundsRDG );
			ShadowDepthPassParameters->OutDepthBuffer		= GraphBuilder.CreateUAV( PhysicalPagePoolRDG, ERDGUnorderedAccessViewFlags::SkipBarrier );
			
			PassParameters->ShadowDepthPass = GraphBuilder.CreateUniformBuffer(ShadowDepthPassParameters);
			PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
			PassParameters->InViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);

			FInstanceCullingResult InstanceCullingResult;
			InstanceCullingResult.DrawIndirectArgsBuffer = Params.DrawIndirectArgs;
			InstanceCullingResult.InstanceIdOffsetBuffer = Params.InstanceIdStartOffsetBuffer;
			InstanceCullingResult.GetDrawParameters(PassParameters->InstanceCullingDrawParams);

			FIntRect ViewRect;
			ViewRect.Max = bAtomicWrites ? FVirtualShadowMap::VirtualMaxResolutionXY : GetPhysicalPoolSize();

			// TODO: Remove this when everything is properly RDG'd
			AddPass(GraphBuilder, [](FRHICommandList& RHICmdList)
			{
				FRHITransitionInfo 	Transitions[2] =
				{
					FRHITransitionInfo(GInstanceCullingManagerResources.GetInstancesIdBufferUav(), ERHIAccess::UAVCompute, ERHIAccess::SRVGraphics),
					FRHITransitionInfo(GInstanceCullingManagerResources.GetPageInfoBufferUav(), ERHIAccess::UAVCompute, ERHIAccess::SRVGraphics)
				};

				RHICmdList.Transition(Transitions);
			});

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("RenderVirtualShadowMapsHw"),
				PassParameters,
				ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
				[this, ProjectedShadowInfo , &MeshCommandPass, PassParameters, ShadowDepthPassParameters, ViewRect](FRHICommandList& RHICmdList)
				{
					FRHIRenderPassInfo RPInfo;
					RPInfo.ResolveParameters.DestRect.X1 = ViewRect.Min.X;
					RPInfo.ResolveParameters.DestRect.Y1 = ViewRect.Min.Y;
					RPInfo.ResolveParameters.DestRect.X2 = ViewRect.Max.X;
					RPInfo.ResolveParameters.DestRect.Y2 = ViewRect.Max.Y;
					RHICmdList.BeginRenderPass(RPInfo, TEXT("RenderVirtualShadowMapsHw"));

					RHICmdList.SetViewport( ViewRect.Min.X, ViewRect.Min.Y, 0.0f, FMath::Min( ViewRect.Max.X, 32767 ), FMath::Min( ViewRect.Max.Y, 32767 ), 1.0f );

					MeshCommandPass.DispatchDraw(nullptr, RHICmdList, &PassParameters->InstanceCullingDrawParams);
					RHICmdList.EndRenderPass();
				});
		}


		//
		if (Index == CVarShowClipmapStats.GetValueOnRenderThread())
		{
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

	MarkPhysicalPagesRendered(GraphBuilder, VirtualShadowMapFlags);

	if (!bWasCleared)
	{
		AddClearDepthStencilPass(GraphBuilder, PhysicalPagePoolHw);
	}
}

class FUpdatePhysicalPageMetaDataClearStateCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FUpdatePhysicalPageMetaDataClearStateCS);
	SHADER_USE_PARAMETER_STRUCT(FUpdatePhysicalPageMetaDataClearStateCS, FVirtualPageManagementShader)

	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FCachedPageInfo >, CachedPageInfos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPhysicalPageMetaData >, PrevPhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, NumAllocatedPhysicalPages)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FPhysicalPageMetaData >, PhysicalPageMetaDataOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FUpdatePhysicalPageMetaDataClearStateCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "UpdatePhysicalPageMetaDataClearState", SF_Compute);


class FSetupClearHwPhysicalPagesDrawArgsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FSetupClearHwPhysicalPagesDrawArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FSetupClearHwPhysicalPagesDrawArgsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, NumAllocatedPhysicalPages)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer< uint >, ClearPhysicalPageDrawArgs)
		SHADER_PARAMETER(int32, bRectPrimitive)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSetupClearHwPhysicalPagesDrawArgsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "SetupClearHwPhysicalPagesDrawArgs", SF_Compute);


class FInitializePhysicalPagesVS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitializePhysicalPagesVS);
	SHADER_USE_PARAMETER_STRUCT(FInitializePhysicalPagesVS, FVirtualPageManagementShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
	END_SHADER_PARAMETER_STRUCT()
};

class FInitializePhysicalPagesPS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitializePhysicalPagesPS);
	SHADER_USE_PARAMETER_STRUCT(FInitializePhysicalPagesPS, FVirtualPageManagementShader);
	
	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D< float >, PrevPhysicalPagePoolHw)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FCachedPageInfo >, CachedPageInfos)
		RDG_BUFFER_ACCESS(IndirectArgsBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER(int32, bCacheDataAvailable)
		SHADER_PARAMETER(int32, bRectPrimitive)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FInitializePhysicalPagesVS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitializePhysicalPagesVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FInitializePhysicalPagesPS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitializePhysicalPagesPS", SF_Pixel);

void FVirtualShadowMapArray::AddInitializePhysicalPagesHwPass(FRDGBuilder& GraphBuilder)
{
	FRDGBufferRef IndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), TEXT("Shadow.Virtual.InitializePhysicalPagesArgs"));
	{
		FSetupClearHwPhysicalPagesDrawArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupClearHwPhysicalPagesDrawArgsCS::FParameters>();
		PassParameters->NumAllocatedPhysicalPages = GraphBuilder.CreateSRV(AllocatedPagesOffsetRDG);
		PassParameters->ClearPhysicalPageDrawArgs = GraphBuilder.CreateUAV(IndirectArgsBuffer, PF_R32_UINT);
		PassParameters->bRectPrimitive = GRHISupportsRectTopology ? 1 : 0;

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FSetupClearHwPhysicalPagesDrawArgsCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitClearPhysicalPagesArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}

	{
		FInitializePhysicalPagesPS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FInitializePhysicalPagesPS::FParameters>();
		ParametersPS->VirtualShadowMap = GetUniformBuffer(GraphBuilder);
		ParametersPS->IndirectArgsBuffer = IndirectArgsBuffer;
		ParametersPS->bRectPrimitive = GRHISupportsRectTopology ? 1 : 0;

		bool bCacheDataAvailable = CacheManager && CacheManager->PrevBuffers.PhysicalPagePoolHw;
		if (bCacheDataAvailable)
		{
			ParametersPS->PrevPhysicalPagePoolHw = GraphBuilder.RegisterExternalTexture(CacheManager->PrevBuffers.PhysicalPagePoolHw);
			ParametersPS->CachedPageInfos = GraphBuilder.CreateSRV(CachedPageInfosRDG);
		}


		FInitializePhysicalPagesPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FInitializePhysicalPagesPS::FHasCacheDataDim>(bCacheDataAvailable);

		TShaderMapRef<FInitializePhysicalPagesVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FInitializePhysicalPagesPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

		ParametersPS->RenderTargets.DepthStencil = FDepthStencilBinding(PhysicalPagePoolHw, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("InitializePhysicalPages"),
			ParametersPS,
			ERDGPassFlags::Raster,
			[ParametersPS, VertexShader, PixelShader, PhysicalPagePoolSize = GetPhysicalPoolSize()](FRHICommandList& RHICmdList)
		{
			FInitializePhysicalPagesVS::FParameters ParametersVS;
			ParametersVS.VirtualShadowMap = ParametersPS->VirtualShadowMap;

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_NONE>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = ParametersPS->bRectPrimitive > 0 ? PT_RectList : PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);

			RHICmdList.SetViewport(
				0.0f,
				0.0f,
				0.0f,
				PhysicalPagePoolSize.X,
				PhysicalPagePoolSize.Y,
				1.0f
			);
			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitiveIndirect(ParametersPS->IndirectArgsBuffer->GetRHI(), 0);
		});
	}
	{
		FUpdatePhysicalPageMetaDataClearStateCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpdatePhysicalPageMetaDataClearStateCS::FParameters>();
		PassParameters->NumAllocatedPhysicalPages = GraphBuilder.CreateSRV(AllocatedPagesOffsetRDG);
		PassParameters->PhysicalPageMetaDataOut = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
		PassParameters->CachedPageInfos = GraphBuilder.CreateSRV(CachedPageInfosRDG);
		PassParameters->VirtualShadowMap = GetUniformBuffer(GraphBuilder);

		bool bCacheDataAvailable = CacheManager && CacheManager->PrevBuffers.PhysicalPageMetaData;
		if (bCacheDataAvailable)
		{
			PassParameters->PrevPhysicalPageMetaData = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->PrevBuffers.PhysicalPageMetaData));
		}

		FUpdatePhysicalPageMetaDataClearStateCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FUpdatePhysicalPageMetaDataClearStateCS::FHasCacheDataDim>(bCacheDataAvailable);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FUpdatePhysicalPageMetaDataClearStateCS>(PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("UpdatePhysicalPageMetaDataClearState"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(UniformParameters.MaxPhysicalPages, 64U)
		);
	}
}
