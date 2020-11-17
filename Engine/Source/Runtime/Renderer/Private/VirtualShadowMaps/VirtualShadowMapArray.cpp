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


static TAutoConsoleVariable<int32> CVarEnableVirtualShadowMaps(
	TEXT("r.Shadow.v.Enable"),
	0,
	TEXT("Enable Virtual Shadow Maps."),
	ECVF_RenderThreadSafe
);


static TAutoConsoleVariable<int32> CVarDebugVisualizeVirtualSms(
	TEXT("r.Shadow.v.DebugVisualize"),
	0,
	TEXT("Set Debug Visualization method for virtual shadow maps, default is off (0).\n")
	TEXT("  To display the result also use the command 'vis VirtSmDebug'"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarShowStats(
	TEXT("r.Shadow.v.ShowStats"),
	0,
	TEXT("ShowStats, also toggle shaderprint one!"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarResolutionLodScale(
	TEXT("r.Shadow.v.ResolutionLodScale"),
	1.0f,
	TEXT("Scale factor applied to LOD calculations (0.5 effectively halves resolution requested)."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarResolutionPixelCountPercent(
	TEXT("r.Shadow.v.ResolutionPixelCountPercent"),
	0.0f,
	TEXT("If more than this percent of the screen pixels fall into a single page, virtual resolution will be increased. 0 disables. 1-2% typical."),
	ECVF_RenderThreadSafe
);

FMatrix CalcTranslatedWorldToShadowUvNormalMatrix(
	const FMatrix& TranslatedWorldToShadowView,
	const FMatrix& ViewToClip)
{
	FMatrix TranslatedWorldToShadowClip = TranslatedWorldToShadowView * ViewToClip;
	FMatrix ScaleAndBiasToSmUv = FScaleMatrix(FVector(0.5f, -0.5f, 1.0f)) * FTranslationMatrix(FVector(0.5f, 0.5f, 0.0f));
	FMatrix TranslatedWorldToShadowUv = TranslatedWorldToShadowClip * ScaleAndBiasToSmUv;
	return TranslatedWorldToShadowUv.GetTransposed().Inverse();
}

FVirtualShadowMapProjectionShaderData GetVirtualShadowMapProjectionShaderData(const FProjectedShadowInfo* ShadowInfo)
{
	check(ShadowInfo->HasVirtualShadowMap());
	check(ShadowInfo->CascadeSettings.ShadowSplitIndex == INDEX_NONE);		// We use clipmaps for virtual shadow maps, not cascades

	// NOTE: Virtual shadow maps are never atlased, but verify our assumptions
	{
		const FVector4 ClipToShadowUV = ShadowInfo->GetClipToShadowBufferUvScaleBias();
		check(ShadowInfo->BorderSize == 0);
		check(ShadowInfo->X == 0);
		check(ShadowInfo->Y == 0);
		const FIntRect ShadowViewRect = ShadowInfo->GetInnerViewRect();
		check(ShadowViewRect.Min.X == 0);
		check(ShadowViewRect.Min.Y == 0);
		check(ShadowViewRect.Max.X == FVirtualShadowMap::VirtualMaxResolutionXY);
		check(ShadowViewRect.Max.Y == FVirtualShadowMap::VirtualMaxResolutionXY);
	}

	FMatrix ViewToClip = ShadowInfo->ViewToClipInner;
		
	FVirtualShadowMapProjectionShaderData Data;
	Data.TranslatedWorldToShadowViewMatrix = ShadowInfo->TranslatedWorldToView;
	Data.ShadowViewToClipMatrix = ViewToClip;
	Data.TranslatedWorldToShadowUvNormalMatrix = CalcTranslatedWorldToShadowUvNormalMatrix(ShadowInfo->TranslatedWorldToView, ViewToClip);
	Data.ShadowPreViewTranslation = FVector(ShadowInfo->PreShadowTranslation);
	Data.VirtualShadowMapId = ShadowInfo->VirtualShadowMap->ID;
	Data.LightType = ShadowInfo->GetLightSceneInfo().Proxy->GetLightType();

	return Data;
}

FVirtualShadowMapArray::FVirtualShadowMapArray()
{
	CommonParameters.PageTableSize = FVirtualShadowMap::PageTableSize;		// TODO: Define

	uint32 HPageFlagOffset = 0;
	for (uint32 Level = 0; Level < FVirtualShadowMap::MaxMipLevels - 1; ++Level)
	{
		CommonParameters.HPageFlagLevelOffsets[Level] = HPageFlagOffset;
		HPageFlagOffset += CommonParameters.PageTableSize - CalcVirtualShadowMapLevelOffsets(Level + 1, FVirtualShadowMap::Log2Level0DimPagesXY);
	}
	// The last mip level is 1x1 and thus does not have any H levels possible.
	CommonParameters.HPageFlagLevelOffsets[FVirtualShadowMap::MaxMipLevels - 1] = 0;
	CommonParameters.HPageTableSize = HPageFlagOffset;

	FIntPoint PhysSize = GetPhysicalPoolSize();
	// Can't be too sure...
	check((PhysSize.X %  FVirtualShadowMap::PageSize) == 0);
	check((PhysSize.Y %  FVirtualShadowMap::PageSize) == 0);
	
	// Row size in pages has to be POT since we use mask & shift in place of integer ops.
	FIntPoint PhysSizePages = PhysSize / FVirtualShadowMap::PageSize;
	check(FMath::IsPowerOfTwo(PhysSizePages.X));

	CommonParameters.MaxPhysicalPages = PhysSizePages.X * PhysSizePages.Y;
	CommonParameters.PhysicalPageRowMask = (PhysSizePages.X - 1);
	CommonParameters.PhysicalPageRowShift = FMath::FloorLog2( PhysSizePages.X );
}

BEGIN_SHADER_PARAMETER_STRUCT(FCacheDataParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FShadowMapCacheData >,	ShadowMapCacheData )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,					PrevPageFlags )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint2 >,					PrevPageTable )
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FPhysicalPageMetaData >,	PrevPhysicalPageMetaData)
	SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,					PrevDynamicCasterPageFlags)
END_SHADER_PARAMETER_STRUCT()

static void SetCacheDataShaderParameters(FRDGBuilder& GraphBuilder, const TArray<FVirtualShadowMap*, SceneRenderingAllocator> &ShadowMaps, FVirtualShadowMapArrayCacheManager *VirtualShadowMapArrayCacheManager, FCacheDataParameters &CacheDataParameters)
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
	CacheDataParameters.ShadowMapCacheData = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("ShadowMapCacheData"), ShadowMapCacheData));
	CacheDataParameters.PrevPageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArrayCacheManager->PrevPageFlags, TEXT("PrevPageFlags")));
	CacheDataParameters.PrevPageTable = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArrayCacheManager->PrevPageTable, TEXT("PrevPageTable")));
	CacheDataParameters.PrevPhysicalPageMetaData = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArrayCacheManager->PrevPhysicalPageMetaData, TEXT("PrevPhysicalPageMetaData")));
	CacheDataParameters.PrevDynamicCasterPageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArrayCacheManager->PrevDynamicCasterPageFlags, TEXT("PrevDynamicCasterPageFlags")));
}

FVirtualShadowMapArray::~FVirtualShadowMapArray()
{
	for (FVirtualShadowMap *SM : ShadowMaps)
	{
		SM->~FVirtualShadowMap();
	}
}

void FVirtualShadowMapArray::SetShaderDefines(FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("VSM_PAGE_SIZE"), FVirtualShadowMap::PageSize);
	OutEnvironment.SetDefine(TEXT("VSM_PAGE_SIZE_MASK"), FVirtualShadowMap::PageSizeMask);
	OutEnvironment.SetDefine(TEXT("VSM_LOG2_PAGE_SIZE"), FVirtualShadowMap::Log2PageSize);
	OutEnvironment.SetDefine(TEXT("VSM_LEVEL0_DIM_PAGES_XY"), FVirtualShadowMap::Level0DimPagesXY);
	OutEnvironment.SetDefine(TEXT("VSM_LOG2_LEVEL0_DIM_PAGES_XY"), FVirtualShadowMap::Log2Level0DimPagesXY);
	OutEnvironment.SetDefine(TEXT("VSM_MAX_MIP_LEVELS"), FVirtualShadowMap::MaxMipLevels);
	OutEnvironment.SetDefine(TEXT("VSM_VIRTUAL_MAX_RESOLUTION_XY"), FVirtualShadowMap::VirtualMaxResolutionXY);
	OutEnvironment.SetDefine(TEXT("VSM_INVALID_PHYSICAL_PAGE_ADDRESS"), FVirtualShadowMap::InvalidPhysicalPageAddress);
	OutEnvironment.SetDefine(TEXT("VSM_RASTER_WINDOW_PAGES"), FVirtualShadowMap::RasterWindowPages);
	OutEnvironment.SetDefine(TEXT("VSM_CACHE_ALIGNMENT_LEVEL"), FVirtualShadowMapArrayCacheManager::AlignmentLevel);
	OutEnvironment.SetDefine(TEXT("INDEX_NONE"), INDEX_NONE);
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

	class FNaniteDepthBufferDim : SHADER_PERMUTATION_BOOL("LOAD_DEPTH_FROM_NANITE_BUFFER");
	using FPermutationDomain = TShaderPermutationDomain<FNaniteDepthBufferDim>;
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapCommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, VisBuffer64)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPageRequestFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FVirtualShadowMapProjectionShaderData >, ShadowMapProjectionData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, VirtualShadowMapIdRemap)
		SHADER_PARAMETER(uint32, NumDirectionalLightSmInds)
		SHADER_PARAMETER(uint32, bPostBasePass)
		SHADER_PARAMETER(float, LodFootprintScale)
		SHADER_PARAMETER(uint32, LodPixelCountThreshold)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGeneratePageFlagsFromPixelsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "GeneratePageFlagsFromPixels", SF_Compute);


class FGenerateHierarchicalPageFlagsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FGenerateHierarchicalPageFlagsCS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateHierarchicalPageFlagsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapCommonParameters, CommonParameters)
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
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapCommonParameters, CommonParameters)
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
		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualShadowMapCommonParameters,			CommonParameters )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,					PageRequestFlags )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				CoverageSummaryInOut )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				AllocatedPagesOffset )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint2 >,				OutPageTable )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				OutStatsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FCachedPageInfo >,		OutCachedPageInfos )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,				OutPageFlags)

		SHADER_PARAMETER_STRUCT_INCLUDE(FCacheDataParameters, CacheDataParameters)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCreatePageMappingsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "CreatePageMappings", SF_Compute);

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
		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualShadowMapCommonParameters, CommonParameters )

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PhysicalPagesTexture)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgs)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D< uint >, CachedPhysicalPagesTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FCachedPageInfo >, CachedPageInfos)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FPhysicalPageMetaData >, PhysicalPageMetaDataOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPhysicalPageMetaData >, PrevPhysicalPageMetaData)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FClearPhysicalPagesCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "ClearPhysicalPages", SF_Compute);


void FVirtualShadowMapArray::ClearPhysicalMemory(FRDGBuilder& GraphBuilder, FRDGTextureRef& PhysicalTexture, FVirtualShadowMapArrayCacheManager *VirtualShadowMapArrayCacheManager)
{
	if (ShadowMaps.Num() == 0)
		return;

	RDG_EVENT_SCOPE( GraphBuilder, "FVirtualShadowMapArray::ClearPhysicalMemory" );

	{
		FRDGBufferRef IndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("IndirectArgs"));
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
			PassParameters->CommonParameters = CommonParameters;
			PassParameters->PhysicalPagesTexture = GraphBuilder.CreateUAV(PhysicalTexture);
			PassParameters->IndirectArgs = IndirectArgsBuffer;
			bool bCacheDataAvailable = VirtualShadowMapArrayCacheManager && VirtualShadowMapArrayCacheManager->PrevPhysicalPageMetaData;
			if (bCacheDataAvailable)
			{
				PassParameters->CachedPhysicalPagesTexture = RegisterExternalTextureWithFallback(GraphBuilder, VirtualShadowMapArrayCacheManager->PrevPhysicalPagePool, GSystemTextures.BlackDummy);
				PassParameters->CachedPageInfos = GraphBuilder.CreateSRV(CachedPageInfosRDG);
				PassParameters->PrevPhysicalPageMetaData = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VirtualShadowMapArrayCacheManager->PrevPhysicalPageMetaData));
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
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapCommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, VirtualShadowMapFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint2 >, PageTable)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FPhysicalPageMetaData >, InOutPhysicalPageMetaData)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMarkRenderedPhysicalPagesCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "MarkRenderedPhysicalPages", SF_Compute);

void FVirtualShadowMapArray::MarkPhysicalPagesRendered(FRDGBuilder& GraphBuilder, const TArray<uint32, SceneRenderingAllocator> &VirtualShadowMapFlags)
{
	if (VirtualShadowMapFlags.Num() == 0)
	{
		return;
	}
	ensure(VirtualShadowMapFlags.Num() == ShadowMaps.Num());

	RDG_EVENT_SCOPE(GraphBuilder, "FVirtualShadowMapArray::MarkPhysicalPagesRendered");

	CommonParameters.NumShadowMaps = ShadowMaps.Num();

	{
		// One launch per All SMs, since they share page table data structure.
		FRDGBufferRef VirtualShadowMapFlagsRDG = CreateStructuredBuffer(GraphBuilder, TEXT("VirtualShadowMapFlags"), VirtualShadowMapFlags);


		FMarkRenderedPhysicalPagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FMarkRenderedPhysicalPagesCS::FParameters >();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->VirtualShadowMapFlags = GraphBuilder.CreateSRV(VirtualShadowMapFlagsRDG);
		PassParameters->PageTable = GraphBuilder.CreateSRV(PageTableRDG);
		PassParameters->InOutPhysicalPageMetaData = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);

		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FMarkRenderedPhysicalPagesCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MarkRenderedPhysicalPages"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(ShadowMaps.Num() * CommonParameters.PageTableSize, FMarkRenderedPhysicalPagesCS::DefaultCSGroupX), 1, 1)
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
		if (ProjectedShadowInfo->VirtualShadowMap)
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
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapCommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, PageRectBoundsOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitPageRectBoundsCS, "/Engine/Private/VirtualShadowMaps/PageManagement.usf", "InitPageRectBounds", SF_Compute);


static void AddInitPageRectsPass(FRDGBuilder& GraphBuilder, const FVirtualShadowMapCommonParameters &CommonParameters, FRDGBufferRef &PageRectBoundsRDG)
{
	FInitPageRectBoundsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitPageRectBoundsCS::FParameters >();
	PassParameters->CommonParameters = CommonParameters;
	PassParameters->PageRectBoundsOut = GraphBuilder.CreateUAV(PageRectBoundsRDG);

	const uint32 NumPageRects = CommonParameters.NumShadowMaps * FVirtualShadowMap::MaxMipLevels;
	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FInitPageRectBoundsCS>();

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("InitPageRectBounds"),
		ComputeShader,
		PassParameters,
		FIntVector(FMath::DivideAndRoundUp(NumPageRects, FInitPageRectBoundsCS::DefaultCSGroupX), 1, 1)
	);
}


void FVirtualShadowMapArray::BuildPageAllocations(FRDGBuilder& GraphBuilder, const TArray<FViewInfo> &Views, const FSortedLightSetSceneInfo& SortedLightsInfo, const TArray<FVisibleLightInfo, SceneRenderingAllocator> &VisibleLightInfos, const TArray<Nanite::FRasterResults, TInlineAllocator<2>> &NaniteRasterResults, bool bPostBasePass, FVirtualShadowMapArrayCacheManager *VirtualShadowMapArrayCacheManager)
{
	RDG_EVENT_SCOPE(GraphBuilder, "FVirtualShadowMapArray::GeneratePageFlagsFromLightGrid");

	ensure(NaniteRasterResults.Num() == Views.Num());

	// Scale the projected footprint by the inverse scale factor such that 2x -> double the res.
	const float LodFootprintScale = 1.0f / CVarResolutionLodScale.GetValueOnRenderThread();

	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SortedLights = SortedLightsInfo.SortedLights;
	if (ShadowMaps.Num())
	{
		if (CVarShowStats.GetValueOnRenderThread() || (VirtualShadowMapArrayCacheManager && VirtualShadowMapArrayCacheManager->IsAccumulatingStats()))
		{
			StatsBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumStats), TEXT("StatsBuffer"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StatsBufferRDG), 0);
		}

		CommonParameters.NumShadowMaps = ShadowMaps.Num();
		// Create and clear the requested page flags
		const uint32 NumPageFlags = ShadowMaps.Num() * CommonParameters.PageTableSize;
		FRDGBufferRef PageRequestFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("PageRequestFlags"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageRequestFlagsRDG), 0);
		DynamicCasterPageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("DynamicCasterPageFlags"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DynamicCasterPageFlagsRDG), 0);

		// Total storage for Hierarchical page tables for all virtual shadow maps
		const uint32 NumHPageFlags = ShadowMaps.Num() * CommonParameters.HPageTableSize;
		HPageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumHPageFlags), TEXT("HPageFlags"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(HPageFlagsRDG), 0);

		const uint32 NumPageRects = CommonParameters.NumShadowMaps * FVirtualShadowMap::MaxMipLevels;
		PageRectBoundsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector4), NumPageRects), TEXT("PageRectBounds"));
		AddInitPageRectsPass(GraphBuilder, CommonParameters, PageRectBoundsRDG);

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
				for (int32 ClipmapLevel = 0; ClipmapLevel < Clipmap->GetLevelCount(); ++ClipmapLevel)
				{
					int32 ID = Clipmap->GetVirtualShadowMap(ClipmapLevel)->ID;
					ShadowMapProjectionData[ID] = Clipmap->GetProjectionShaderData(ClipmapLevel);
					DirectionalLightSmInds.Add(ID);
				}
			}

			for (FProjectedShadowInfo* ProjectedShadowInfo : VisibleLightInfo.AllProjectedShadows)
			{
				if (ProjectedShadowInfo->HasVirtualShadowMap())
				{
					int32 ID = ProjectedShadowInfo->VirtualShadowMap->ID;
					ShadowMapProjectionData[ID] = GetVirtualShadowMapProjectionShaderData(ProjectedShadowInfo);

					if (ProjectedShadowInfo->bDirectionalLight)
					{
						DirectionalLightSmInds.Add(ID);
					}
				}
			}
		}
		ShadowMapProjectionDataRDG = CreateStructuredBuffer(GraphBuilder, TEXT("ShadowMapProjectionData"), ShadowMapProjectionData);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewInfo &View = Views[ViewIndex];
			const Nanite::FRasterResults &NaniteRasterResult = NaniteRasterResults[ViewIndex];

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
						ensure(ShadowInfo->VirtualShadowMap);
						ensure(ShadowInfo->VirtualShadowMap->ID != INDEX_NONE);
						VirtualShadowMapIdRemap[DirectionalLightSmInds.Num() + L] = ShadowInfo->VirtualShadowMap->ID;
					}
				}
			}

			FRDGBufferRef VirtualShadowMapIdRemapRDG = CreateStructuredBuffer(GraphBuilder, TEXT("VirtualShadowMapIdRemap"), VirtualShadowMapIdRemap);
			FRDGTextureRef VisBuffer64 = RegisterExternalTextureWithFallback(GraphBuilder, NaniteRasterResult.VisBuffer64, GSystemTextures.BlackDummy);

			FRDGBufferRef ScreenSpaceGridBoundsRDG = nullptr;
			
			// Project Pixels onto SMs
			{
				FGeneratePageFlagsFromPixelsCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FGeneratePageFlagsFromPixelsCS::FNaniteDepthBufferDim>(NaniteRasterResult.VisBuffer64 != nullptr && !bPostBasePass);			

				FGeneratePageFlagsFromPixelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FGeneratePageFlagsFromPixelsCS::FParameters >();
				PassParameters->CommonParameters = CommonParameters;

				if (bPostBasePass)
				{
					PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel, ESceneTextureSetupMode::GBuffers | ESceneTextureSetupMode::SceneDepth);
				}
				else
				{
					PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel, ESceneTextureSetupMode::SceneDepth);
				}
				PassParameters->bPostBasePass = bPostBasePass;
				
				// Number of pixels in a single page before we forcably bump the LOD
				const float ResolutionPixelCountFactor = CVarResolutionPixelCountPercent.GetValueOnRenderThread() / 100.0f;
				const uint32 LodPixelCountThreshold = ResolutionPixelCountFactor <= 0.0f ? 0 :
					static_cast<uint32>(ResolutionPixelCountFactor * static_cast<float>(View.ViewRect.Area()));
				
				PassParameters->VisBuffer64 = VisBuffer64;
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->OutPageRequestFlags = GraphBuilder.CreateUAV(PageRequestFlagsRDG);
				PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
				PassParameters->VirtualShadowMapIdRemap = GraphBuilder.CreateSRV(VirtualShadowMapIdRemapRDG);
				PassParameters->ShadowMapProjectionData = GraphBuilder.CreateSRV(ShadowMapProjectionDataRDG);
				PassParameters->NumDirectionalLightSmInds = DirectionalLightSmInds.Num();
				PassParameters->LodFootprintScale = LodFootprintScale;
				PassParameters->LodPixelCountThreshold = LodPixelCountThreshold;
				
				auto ComputeShader = View.ShaderMap->GetShader<FGeneratePageFlagsFromPixelsCS>(PermutationVector);

				static_assert((FVirtualPageManagementShader::DefaultCSGroupXY % 2) == 0, "GeneratePageFlagsFromPixels requires even-sized CS groups for quad swizzling.");
				const FIntPoint GridSize = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), FVirtualPageManagementShader::DefaultCSGroupXY);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("GeneratePageFlagsFromPixels"),
					ComputeShader,
					PassParameters,
					FIntVector(GridSize.X, GridSize.Y, 1)
				);
			}
		}

		FRDGBufferRef HZBPageTableRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(8, NumPageFlags), TEXT("HZBPageTable"));

		PageTableRDG			= GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateStructuredDesc( 8, NumPageFlags ), TEXT("PageTable") );
		// Note: these are passed to the rendering and are not identical to the PageRequest flags coming in from GeneratePageFlagsFromPixels 
		PageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlags), TEXT("PageFlags"));

		FRDGBufferRef HInvalidPageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumHPageFlags), TEXT("HInvalidPageFlags"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(HInvalidPageFlagsRDG), 0);

		// Create and clear the counter / page offset, it gets atomically incremented to allocate the physical pages
		AllocatedPagesOffsetRDG = GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),  1), TEXT("AllocatedPagesOffset") );
		AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV(AllocatedPagesOffsetRDG), 0 );

		// Enough space for all physical pages that might be allocated
		CachedPageInfosRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FCachedPageInfo), CommonParameters.MaxPhysicalPages), TEXT("CachedPageInfos"));
		PhysicalPageMetaDataRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FPhysicalPageMetaData), CommonParameters.MaxPhysicalPages), TEXT("PhysicalPageMetaData"));
		
		{
			FInitPhysicalPageMetaData::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitPhysicalPageMetaData::FParameters >();
			PassParameters->PhysicalPageMetaDataOut = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
			PassParameters->CommonParameters = CommonParameters;

			auto ComputeShader = Views[0].ShaderMap->GetShader< FInitPhysicalPageMetaData >();
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("InitPhysicalPageMetaData"),
				ComputeShader,
				PassParameters,
				FIntVector(FMath::DivideAndRoundUp(CommonParameters.MaxPhysicalPages, FInitPhysicalPageMetaData::DefaultCSGroupX), 1, 1)
			);
		}
		

		{
			// Note: does not actually need mip0 so can be trimmed down a bit
			FRDGBufferRef CoverageSummary = GraphBuilder.CreateBuffer( FRDGBufferDesc::CreateStructuredDesc( 4, NumPageFlags ), TEXT("CoverageSummary") );
			
			AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( CoverageSummary ), 0 );
			
			// Run a pass to create page mappings
			FCreatePageMappingsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FCreatePageMappingsCS::FParameters >();
			PassParameters->CommonParameters = CommonParameters;

			PassParameters->PageRequestFlags		= GraphBuilder.CreateSRV(PageRequestFlagsRDG );
			PassParameters->CoverageSummaryInOut	= GraphBuilder.CreateUAV( CoverageSummary );
			PassParameters->AllocatedPagesOffset	= GraphBuilder.CreateUAV( AllocatedPagesOffsetRDG);
			PassParameters->OutPageTable			= GraphBuilder.CreateUAV( PageTableRDG );
			PassParameters->OutCachedPageInfos		= GraphBuilder.CreateUAV( CachedPageInfosRDG );
			PassParameters->OutPageFlags			= GraphBuilder.CreateUAV( PageFlagsRDG );

			bool bCacheDataAvailable = VirtualShadowMapArrayCacheManager && VirtualShadowMapArrayCacheManager->IsValid();
			if (bCacheDataAvailable)
			{
				SetCacheDataShaderParameters(GraphBuilder, ShadowMaps, VirtualShadowMapArrayCacheManager, PassParameters->CacheDataParameters);
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
			PassParameters->CommonParameters = CommonParameters;
			PassParameters->OutHPageFlags = GraphBuilder.CreateUAV(HPageFlagsRDG);
			PassParameters->PageFlags = GraphBuilder.CreateSRV(PageFlagsRDG);
			PassParameters->PageRectBoundsOut = GraphBuilder.CreateUAV(PageRectBoundsRDG);

			auto ComputeShader = Views[0].ShaderMap->GetShader<FGenerateHierarchicalPageFlagsCS>();
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GenerateHierarchicalPageFlags"),
				ComputeShader,
				PassParameters,
				FIntVector(FMath::DivideAndRoundUp(CommonParameters.PageTableSize, FGenerateHierarchicalPageFlagsCS::DefaultCSGroupX), ShadowMaps.Num(), 1)
			);
		}
	}
}

void FVirtualShadowMapArray::SetProjectionParameters(FRDGBuilder& GraphBuilder, FVirtualShadowMapSamplingParameters& OutParameters)
{
	OutParameters.CommonParameters = CommonParameters;
	OutParameters.PageTable = GraphBuilder.CreateSRV(PageTableRDG);
	OutParameters.PhysicalPagePool = PhysicalPagePoolRDG;
	OutParameters.VirtualShadowMapProjectionData = GraphBuilder.CreateSRV(ShadowMapProjectionDataRDG);
}

class FDebugVisualizeVirtualSmCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FDebugVisualizeVirtualSmCS);
	SHADER_USE_PARAMETER_STRUCT(FDebugVisualizeVirtualSmCS, FVirtualPageManagementShader)

	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapCommonParameters, CommonParameters)
		SHADER_PARAMETER(uint32, DebugTargetWidth)
		SHADER_PARAMETER(uint32, DebugTargetHeight)
		SHADER_PARAMETER(uint32, BorderWidth)
		SHADER_PARAMETER(uint32, ZoomScaleFactor)
		SHADER_PARAMETER(uint32, DebugMethod)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PageFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, HPageFlags)

		SHADER_PARAMETER_RDG_TEXTURE(	Texture2D< uint >,			PhysicalPagePool )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint2 >,	PageTable )
		
		SHADER_PARAMETER_RDG_TEXTURE(	Texture2D< float >,			HZBPhysical )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint2 >,	HZBPageTable )

		SHADER_PARAMETER_STRUCT_INCLUDE(FCacheDataParameters, CacheDataParameters)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDebugVisualizeVirtualSmCS, "/Engine/Private/VirtualShadowMaps/Debug.usf", "DebugVisualizeVirtualSmCS", SF_Compute);


void FVirtualShadowMapArray::RenderDebugInfo(FRDGBuilder& GraphBuilder, FVirtualShadowMapArrayCacheManager *VirtualShadowMapArrayCacheManager)
{
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
			TEXT("VirtSmDebug"));

		FDebugVisualizeVirtualSmCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDebugVisualizeVirtualSmCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;

		PassParameters->PageFlags			= GraphBuilder.CreateSRV( PageFlagsRDG );
		PassParameters->HPageFlags			= GraphBuilder.CreateSRV( HPageFlagsRDG );

		PassParameters->PhysicalPagePool	= PhysicalPagePoolRDG ? PhysicalPagePoolRDG : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		PassParameters->PageTable			= GraphBuilder.CreateSRV( PageTableRDG );

		PassParameters->HZBPhysical			= RegisterExternalTextureWithFallback( GraphBuilder, HZBPhysical, GSystemTextures.BlackDummy);
		PassParameters->HZBPageTable		= GraphBuilder.CreateSRV( HZBPageTable ? GraphBuilder.RegisterExternalBuffer(HZBPageTable) : PageTableRDG);

		PassParameters->DebugTargetWidth = DebugTargetWidth;
		PassParameters->DebugTargetHeight = DebugTargetHeight;
		PassParameters->BorderWidth = BorderWidth;
		PassParameters->ZoomScaleFactor = ZoomScaleFactor;
		PassParameters->DebugMethod = DebugMethod;

		bool bCacheDataAvailable = VirtualShadowMapArrayCacheManager && VirtualShadowMapArrayCacheManager->IsValid();
		if (bCacheDataAvailable)
		{
			SetCacheDataShaderParameters(GraphBuilder, ShadowMaps, VirtualShadowMapArrayCacheManager, PassParameters->CacheDataParameters);
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

		ConvertToExternalTexture(GraphBuilder, DebugOutput, DebugVisualizationOutput);
	}
}


class FVirtualSmPrintStatsCS : public FVirtualPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmPrintStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmPrintStatsCS, FVirtualPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapCommonParameters, CommonParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE( ShaderPrint::FShaderParameters, ShaderPrintStruct )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, InStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmPrintStatsCS, "/Engine/Private/VirtualShadowMaps/PrintStats.usf", "PrintStats", SF_Compute);

void FVirtualShadowMapArray::PrintStats(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	LLM_SCOPE_BYTAG(Nanite);

	// Print stats
	if (CVarShowStats.GetValueOnRenderThread() != 0 && StatsBufferRDG)
	{
		{
			FVirtualSmPrintStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmPrintStatsCS::FParameters>();

			ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintStruct);
			PassParameters->InStatsBuffer = GraphBuilder.CreateSRV(StatsBufferRDG);
			PassParameters->CommonParameters = CommonParameters;

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
