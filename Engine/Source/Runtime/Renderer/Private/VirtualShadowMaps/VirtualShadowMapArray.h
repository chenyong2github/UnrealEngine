// Copyright Epic Games, Inc. All Rights Reserved.
/*=============================================================================
	VirtualShadowMapArray.h:
=============================================================================*/
#pragma once

#include "../Nanite/NaniteRender.h"
#include "../MeshDrawCommands.h"
#include "SceneTypes.h"

struct FMinimalSceneTextures;
struct FSortedLightSetSceneInfo;
class FViewInfo;
class FProjectedShadowInfo;
class FVisibleLightInfo;
class FVirtualShadowMapCacheEntry;
class FVirtualShadowMapArrayCacheManager;
struct FSortedLightSetSceneInfo;

// TODO: does this exist?
constexpr uint32 ILog2Const(uint32 n)
{
	return (n > 1) ? 1 + ILog2Const(n / 2) : 0;
}

// See CalcLevelOffsets in PageAccessCommon.ush for some details on this logic
constexpr uint32 CalcVirtualShadowMapLevelOffsets(uint32 Level, uint32 Log2Level0DimPagesXY)
{
	uint32 NumBits = Level << 1;
	uint32 StartBit = (2U * Log2Level0DimPagesXY + 2U) - NumBits;
	uint32 Mask = ((1U << NumBits) - 1U) << StartBit;
	return 0x55555555U & Mask;
}

class FVirtualShadowMap
{
public:
	// PageSize * Level0DimPagesXY defines the virtual address space, e.g., 128x128 = 16k

	// 32x512 = 16k
	//static constexpr uint32 PageSize = 32U;
	//static constexpr uint32 Level0DimPagesXY = 512U;

	// 128x128 = 16k
	static constexpr uint32 PageSize = 128U;
	static constexpr uint32 Level0DimPagesXY = 128U;

	// 512x32 = 16k
	//static constexpr uint32 PageSize = 512U;
	//static constexpr uint32 Level0DimPagesXY = 32U;

	static constexpr uint32 PageSizeMask = PageSize - 1U;
	static constexpr uint32 Log2PageSize = ILog2Const(PageSize);
	static constexpr uint32 Log2Level0DimPagesXY = ILog2Const(Level0DimPagesXY);
	static constexpr uint32 MaxMipLevels = Log2Level0DimPagesXY + 1U;

	static constexpr uint32 PageTableSize = CalcVirtualShadowMapLevelOffsets(MaxMipLevels, Log2Level0DimPagesXY);

	static constexpr uint32 VirtualMaxResolutionXY = Level0DimPagesXY * PageSize;
	
	static constexpr uint32 PhysicalPageAddressBits = 16U;
	static constexpr uint32 MaxPhysicalTextureDimPages = 1U << PhysicalPageAddressBits;
	static constexpr uint32 MaxPhysicalTextureDimTexels = MaxPhysicalTextureDimPages * PageSize;

	static constexpr uint32 RasterWindowPages = 4u;
	
	FVirtualShadowMap(uint32 InID) : ID(InID)
	{
	}

	int32 ID = INDEX_NONE;
	TSharedPtr<FVirtualShadowMapCacheEntry> VirtualShadowMapCacheEntry;
};

// Useful data for both the page mapping shader and the projection shader
// as well as cached shadow maps
struct FVirtualShadowMapProjectionShaderData
{
	/**
	 * Transform from shadow-pre-translated world space to shadow view space, example use: (WorldSpacePos + ShadowPreViewTranslation) * TranslatedWorldToShadowViewMatrix
	 * TODO: Why don't we call it a rotation and store in a 3x3? Does it ever have translation in?
	 */
	FMatrix TranslatedWorldToShadowViewMatrix;
	FMatrix ShadowViewToClipMatrix;
	FMatrix TranslatedWorldToShadowUVMatrix;
	FMatrix TranslatedWorldToShadowUVNormalMatrix;

	FVector ShadowPreViewTranslation;
	uint32 LightType = ELightComponentType::LightType_Directional;
	
	FVector ClipmapWorldOrigin;
	int32 VirtualShadowMapId = INDEX_NONE;
	
	FIntPoint ClipmapCornerOffset;
	int32 ClipmapIndex = 0;					// 0 .. ClipmapLevelCount-1
	int32 ClipmapLevel = 0;					// "Absolute" level, can be negative
	int32 ClipmapLevelCount = 0;
	float ClipmapResolutionLodBias = 0.0f;

	// Seems the FMatrix forces 16-byte alignment
	float Padding[2];
};
static_assert((sizeof(FVirtualShadowMapProjectionShaderData) % 16) == 0, "FVirtualShadowMapProjectionShaderData size should be a multiple of 16-bytes for alignment.");

struct FVirtualShadowMapHZBMetadata
{
	FViewMatrices ViewMatrices;
	FIntRect	  ViewRect;
	uint32		  TargetLayerIndex = INDEX_NONE;
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVirtualShadowMapUniformParameters, )
	SHADER_PARAMETER_ARRAY(uint32, HPageFlagLevelOffsets, [FVirtualShadowMap::MaxMipLevels])
	SHADER_PARAMETER(uint32, HPageTableSize)
	SHADER_PARAMETER(uint32, NumShadowMaps)
	SHADER_PARAMETER(uint32, NumDirectionalLights)
	SHADER_PARAMETER(uint32, MaxPhysicalPages)
	// use to map linear index to x,y page coord
	SHADER_PARAMETER(uint32, PhysicalPageRowMask)
	SHADER_PARAMETER(uint32, PhysicalPageRowShift)
	SHADER_PARAMETER(FVector4, RecPhysicalPoolSize)
	SHADER_PARAMETER(FIntPoint, PhysicalPoolSize)
	SHADER_PARAMETER(FIntPoint, PhysicalPoolSizePages)

	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ProjectionData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PageTable)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PhysicalPagePool)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PhysicalPagePoolHw)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FVirtualShadowMapSamplingParameters, )
	// NOTE: These parameters must only be uniform buffers/references! Loose parameters do not get bound
	// in some of the forward passes that use this structure.
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
END_SHADER_PARAMETER_STRUCT()

/**
 * Use after page allocation but before rendering phase to access page table & related data structures, but not the physical backing.
 */
BEGIN_SHADER_PARAMETER_STRUCT(FVirtualShadowMapPageTableParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PageTable)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PageFlags)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, HPageFlags)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint4 >, PageRectBounds)
END_SHADER_PARAMETER_STRUCT()

FMatrix CalcTranslatedWorldToShadowUVMatrix(const FMatrix& TranslatedWorldToShadowView, const FMatrix& ViewToClip);
FMatrix CalcTranslatedWorldToShadowUVNormalMatrix(const FMatrix& TranslatedWorldToShadowView, const FMatrix& ViewToClip);

class FVirtualShadowMapArray
{
public:	
	FVirtualShadowMapArray();
	~FVirtualShadowMapArray();

	void Initialize(FRDGBuilder& GraphBuilder, FVirtualShadowMapArrayCacheManager* InCacheManager, bool bInEnabled);

	// Returns true if virtual shadow maps are enabled
	bool IsEnabled() const
	{
		return bEnabled;
	}

	FVirtualShadowMap *Allocate()
	{
		check(IsEnabled());
		FVirtualShadowMap *SM = new(FMemStack::Get(), 1, 16) FVirtualShadowMap(ShadowMaps.Num());
		ShadowMaps.Add(SM);
		return SM;
	}

	FIntPoint GetPhysicalPoolSize() const;

	static void SetShaderDefines(FShaderCompilerEnvironment& OutEnvironment);

	// Call after creating physical page pools and rendering
	void SetupProjectionParameters(FRDGBuilder& GraphBuilder);

	void ClearPhysicalMemory(FRDGBuilder& GraphBuilder, FRDGTextureRef& PhysicalTexture);
	void MarkPhysicalPagesRendered(FRDGBuilder& GraphBuilder, const TArray<uint32, SceneRenderingAllocator> &VirtualShadowMapFlags);

	//
	void BuildPageAllocations(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const TArray<FViewInfo> &Views, 
		const FSortedLightSetSceneInfo& SortedLights, 
		const TArray<FVisibleLightInfo, SceneRenderingAllocator> &VisibleLightInfos, 
		const TArray<Nanite::FRasterResults, TInlineAllocator<2>> &NaniteRasterResults, 
		bool bPostBasePass);

	bool IsAllocated() const
	{
		return PhysicalPagePoolRDG != nullptr && PageTableRDG != nullptr;
	}

	void CreateMipViews( TArray<Nanite::FPackedView, SceneRenderingAllocator>& Views ) const;

	/**
	 * Draw old-school hardware based shadow map tiles into virtual SM.
	 */
	void RenderVirtualShadowMapsHw(FRDGBuilder& GraphBuilder, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& VirtualSmMeshCommandPasses, FScene& Scene);

	void AddInitializePhysicalPagesHwPass(FRDGBuilder& GraphBuilder);

	// Draw debug info into render target 'VSMDebug' of screen-size, the mode is controlled by 'r.Shadow.Virtual.DebugVisualize'.
	void RenderDebugInfo(FRDGBuilder& GraphBuilder);
	// 
	void PrintStats(FRDGBuilder& GraphBuilder, const FViewInfo& View);

	TRDGUniformBufferRef<FVirtualShadowMapUniformParameters> GetUniformBuffer(FRDGBuilder& GraphBuilder) const;

	// Get shader parameters necessary to sample virtual shadow maps
	// It is safe to bind this buffer even if VSMs are disabled, but the sampling should be branched around in the shader.
	// This data becomes valid after the shadow depths pass if VSMs are enabled
	FVirtualShadowMapSamplingParameters GetSamplingParameters(FRDGBuilder& GraphBuilder) const;

	bool HasAnyShadowData() const { return PhysicalPagePoolRDG != nullptr || PhysicalPagePoolHw != nullptr;  }

	void GetPageTableParameters(FRDGBuilder& GraphBuilder, FVirtualShadowMapPageTableParameters& OutParameters);

	bool bInitialized = false;
	// Are virtual shadow maps enabled? We store this at the start of the frame to centralize the logic.
	bool bEnabled = false;
	// We keep a reference to the cache manager that was used to initialize this frame as it owns some of the buffers
	FVirtualShadowMapArrayCacheManager* CacheManager = nullptr;

	TArray<FVirtualShadowMap*, SceneRenderingAllocator> ShadowMaps;

	FVirtualShadowMapUniformParameters UniformParameters;

	// Buffer that serves as the page table for all virtual shadow maps
	FRDGBufferRef PageTableRDG = nullptr;
	// Large physical texture of depth format, say 4096^2 or whatever we think is enough texels to go around
	FRDGTextureRef PhysicalPagePoolRDG = nullptr;
	// page pool for HW rasterized shadow data. 
	// Mirrors the regular one, but uses a shadow depth target format
	FRDGTextureRef PhysicalPagePoolHw = nullptr;
		
	// Buffer that stores flags (uints) marking each page that needs to be rendered and cache status, for all virtual shadow maps.
	// Flag values defined in PageAccessCommon.ush: VSM_ALLOCATED_FLAG | VSM_INVALID_FLAG
	FRDGBufferRef PageFlagsRDG = nullptr;
	// HPageFlags is a hierarchy over the PageFlags for quick query
	FRDGBufferRef HPageFlagsRDG = nullptr;

	FRDGBufferRef AllocatedPagesOffsetRDG = nullptr;

	static constexpr uint32 NumStats = 5;
	// 0 - allocated pages
	// 1 - re-usable pages
	// 2 - Touched by dynamic
	// 3 - NumSms
	// 4 - RandRobin invalidated

	FRDGBufferRef StatsBufferRDG = nullptr;

	// Allocation info for each page.
	FRDGBufferRef CachedPageInfosRDG = nullptr;
	FRDGBufferRef PhysicalPageMetaDataRDG = nullptr;

	// Buffer that stores flags marking each page that received dynamic geo.
	FRDGBufferRef DynamicCasterPageFlagsRDG = nullptr;
	
	// uint4 buffer with one rect for each mip level in all SMs, calculated to bound committed pages
	// Used to clip the rect size of clusters during culling.
	FRDGBufferRef PageRectBoundsRDG = nullptr;
	FRDGBufferRef AllocatedPageRectBoundsRDG = nullptr;
	FRDGBufferRef ShadowMapProjectionDataRDG = nullptr;
	TArray< FRDGBufferRef, SceneRenderingAllocator > VirtualShadowMapIdRemapRDG;

	// HZB generated for the *current* frame's physical page pool
	// We use the *previous* frame's HZB (from VirtualShadowMapCacheManager) for culling the current frame
	FRDGTextureRef HZBPhysical = nullptr;
	TMap<int32, FVirtualShadowMapHZBMetadata> HZBMetadata;

	// Convert also?
	TRefCountPtr<IPooledRenderTarget>	DebugVisualizationOutput;
	TRefCountPtr<IPooledRenderTarget>	DebugVisualizationProjectionOutput;
};
