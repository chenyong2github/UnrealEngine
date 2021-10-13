// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRendering.h: Scene rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "Containers/ArrayView.h"
#include "Stats/Stats.h"
#include "RHI.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "GlobalDistanceFieldParameters.h"
#include "SceneView.h"
#include "RendererInterface.h"
#include "BatchedElements.h"
#include "MeshBatch.h"
#include "SceneManagement.h"
#include "ScenePrivateBase.h"
#include "PrimitiveSceneInfo.h"
#include "GlobalShader.h"
#include "PrimitiveViewRelevance.h"
#include "DistortionRendering.h"
#include "HeightfieldLighting.h"
#include "LightShaftRendering.h"
#include "SkyAtmosphereRendering.h"
#include "Templates/UniquePtr.h"
#include "RenderGraphUtils.h"
#include "MeshDrawCommands.h"
#include "GpuDebugRendering.h"
#include "PostProcess/PostProcessAmbientOcclusionMobile.h"

// Forward declarations.
class FScene;
class FSceneViewState;
class FViewInfo;
struct FILCUpdatePrimTaskData;
class FPostprocessContext;
struct FILCUpdatePrimTaskData;
class FRaytracingLightDataPacked;
class FRayTracingLocalShaderBindingWriter;
struct FCloudRenderContext;
struct FSceneWithoutWaterTextures;
struct FHairStrandsVisibilityViews;
struct FSortedLightSetSceneInfo;
struct FHairStrandsRenderingData;
enum class EVelocityPass : uint32;
class FTransientLightFunctionTextureAtlas;

DECLARE_STATS_GROUP(TEXT("Command List Markers"), STATGROUP_CommandListMarkers, STATCAT_Advanced);

DECLARE_GPU_DRAWCALL_STAT_EXTERN(VirtualTextureUpdate);

/** Hair strands persitent information per view. Used for GPU->CPU feedback */
struct FHairStrandsViewData
{
	FRHIGPUBufferReadback* GetBuffer() const { return VoxelPageAllocationCountReadback; }
	bool IsReady() const				{ return VoxelPageAllocationCountReadback->IsReady(); }
	bool IsInit() const					{ return VoxelPageAllocationCountReadback != nullptr; }
	void Init();
	void Release();

	float VoxelWorldSize		= 0; // Voxel size used during the last frame allocation
	uint32 AllocatedPageCount	= 0; // Number of voxels allocated last frame

	// Buffer used for reading back the number of voxels allocated on the GPU
	FRHIGPUBufferReadback* VoxelPageAllocationCountReadback = nullptr;
};

/** Mobile only. Information used to determine whether static meshes will be rendered with CSM shaders or not. */
class FMobileCSMVisibilityInfo
{
public:
	/** true if there are any primitives affected by CSM subjects */
	uint32 bMobileDynamicCSMInUse : 1;

	// true if all draws should be forced to use CSM shaders.
	uint32 bAlwaysUseCSM : 1;

	/** Visibility lists for static meshes that will use expensive CSM shaders. */
	FSceneBitArray MobilePrimitiveCSMReceiverVisibilityMap;
	FSceneBitArray MobileCSMStaticMeshVisibilityMap;

	/** Visibility lists for static meshes that will use the non CSM shaders. */
	FSceneBitArray MobileNonCSMStaticMeshVisibilityMap;

	/** Initialization constructor. */
	FMobileCSMVisibilityInfo() : bMobileDynamicCSMInUse(false), bAlwaysUseCSM(false)
	{}
};

/** Stores a list of CSM shadow casters. Used by mobile renderer for culling primitives receiving static + CSM shadows. */
class FMobileCSMSubjectPrimitives
{
public:
	/** Adds a subject primitive */
	void AddSubjectPrimitive(const FPrimitiveSceneInfo* PrimitiveSceneInfo, int32 PrimitiveId)
	{
		checkSlow(PrimitiveSceneInfo->GetIndex() == PrimitiveId);
		const int32 PrimitiveIndex = PrimitiveSceneInfo->GetIndex();
		if (!ShadowSubjectPrimitivesEncountered[PrimitiveId])
		{
			ShadowSubjectPrimitives.Add(PrimitiveSceneInfo);
			ShadowSubjectPrimitivesEncountered[PrimitiveId] = true;
		}
	}

	/** Returns the list of subject primitives */
	const TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator>& GetShadowSubjectPrimitives() const
	{
		return ShadowSubjectPrimitives;
	}

	/** Used to initialize the ShadowSubjectPrimitivesEncountered bit array
	  * to prevent shadow primitives being added more than once. */
	void InitShadowSubjectPrimitives(int32 PrimitiveCount)
	{
		ShadowSubjectPrimitivesEncountered.Init(false, PrimitiveCount);
	}

protected:
	/** List of this light's shadow subject primitives. */
	FSceneBitArray ShadowSubjectPrimitivesEncountered;
	TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> ShadowSubjectPrimitives;
};

class FMobileMovableSpotLightsShadowInfo
{
public:
	FVector4 ShadowBufferSize = FVector4(0.0f);
	FRHITexture* ShadowDepthTexture = nullptr;
};

/** Information about a visible light which is specific to the view it's visible in. */
class FVisibleLightViewInfo
{
public:

	/** The dynamic primitives which are both visible and affected by this light. */
	TArray<FPrimitiveSceneInfo*,SceneRenderingAllocator> VisibleDynamicLitPrimitives;
	
	/** Whether each shadow in the corresponding FVisibleLightInfo::AllProjectedShadows array is visible. */
	FSceneBitArray ProjectedShadowVisibilityMap;

	/** The view relevance of each shadow in the corresponding FVisibleLightInfo::AllProjectedShadows array. */
	TArray<FPrimitiveViewRelevance,SceneRenderingAllocator> ProjectedShadowViewRelevanceMap;

	/** true if this light in the view frustum (dir/sky lights always are). */
	uint32 bInViewFrustum : 1;

	/** List of CSM shadow casters. Used by mobile renderer for culling primitives receiving static + CSM shadows */
	FMobileCSMSubjectPrimitives MobileCSMSubjectPrimitives;

	/** Initialization constructor. */
	FVisibleLightViewInfo()
	:	bInViewFrustum(false)
	{}
};

/** Information about a visible light which isn't view-specific. */
class FVisibleLightInfo
{
public:

	/** Projected shadows allocated on the scene rendering mem stack. */
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> MemStackProjectedShadows;

	/** All visible projected shadows, output of shadow setup.  Not all of these will be rendered. */
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> AllProjectedShadows;

	/** Shadows to project for each feature that needs special handling. */
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> ShadowsToProject;
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> CapsuleShadowsToProject;
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> RSMsToProject;

	/** All visible projected preshdows.  These are not allocated on the mem stack so they are refcounted. */
	TArray<TRefCountPtr<FProjectedShadowInfo>,SceneRenderingAllocator> ProjectedPreShadows;

	/** A list of per-object shadows that were occluded. We need to track these so we can issue occlusion queries for them. */
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator> OccludedPerObjectShadows;
};

// Stores the primitive count of each translucency pass (redundant, could be computed after sorting but this way we touch less memory)
struct FTranslucenyPrimCount
{
private:
	uint32 Count[ETranslucencyPass::TPT_MAX];
	bool UseSceneColorCopyPerPass[ETranslucencyPass::TPT_MAX];

public:
	// constructor
	FTranslucenyPrimCount()
	{
		for(uint32 i = 0; i < ETranslucencyPass::TPT_MAX; ++i)
		{
			Count[i] = 0;
			UseSceneColorCopyPerPass[i] = false;
		}
	}

	// interface similar to TArray but here we only store the count of Prims per pass
	void Append(const FTranslucenyPrimCount& InSrc)
	{
		for(uint32 i = 0; i < ETranslucencyPass::TPT_MAX; ++i)
		{
			Count[i] += InSrc.Count[i];
			UseSceneColorCopyPerPass[i] |= InSrc.UseSceneColorCopyPerPass[i];
		}
	}

	// interface similar to TArray but here we only store the count of Prims per pass
	void Add(ETranslucencyPass::Type InPass, bool bUseSceneColorCopy)
	{
		++Count[InPass];
		UseSceneColorCopyPerPass[InPass] |= bUseSceneColorCopy;
	}

	int32 Num(ETranslucencyPass::Type InPass) const
	{
		return Count[InPass];
	}

	int32 NumPrims() const
	{
		int32 NumTotal = 0;
		for (uint32 PassIndex = 0; PassIndex < ETranslucencyPass::TPT_MAX; ++PassIndex)
		{
			NumTotal += Count[PassIndex];
		}
		return NumTotal;
	}

	bool UseSceneColorCopy(ETranslucencyPass::Type InPass) const
	{
		return UseSceneColorCopyPerPass[InPass];
	}
};

/** A batched occlusion primitive. */
struct FOcclusionPrimitive
{
	FVector Center;
	FVector Extent;
};

// An occlusion query pool with frame based lifetime management
class FFrameBasedOcclusionQueryPool
{
public:
	FFrameBasedOcclusionQueryPool()
		: OcclusionFrameCounter(-1)
		, NumBufferedFrames(0)
	{}

	FRHIRenderQuery* AllocateQuery();

	// Recycle queries that are (OcclusionFrameCounter - NumBufferedFrames) old or older
	void AdvanceFrame(uint32 InOcclusionFrameCounter, uint32 InNumBufferedFrames, bool bStereoRoundRobin);

private:
	struct FFrameOcclusionQueries
	{
		TArray<FRenderQueryRHIRef> Queries;
		int32 FirstFreeIndex;
		uint32 OcclusionFrameCounter;

		FFrameOcclusionQueries()
			: FirstFreeIndex(0)
			, OcclusionFrameCounter(0)
		{}
	};

	FFrameOcclusionQueries FrameQueries[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames * 2];
	uint32 CurrentFrameIndex;
	uint32 OcclusionFrameCounter;
	uint32 NumBufferedFrames;
};

class FRefCountedRHIPooledRenderQuery
{
public:
	FRefCountedRHIPooledRenderQuery() : FRefCountedRHIPooledRenderQuery(FRHIPooledRenderQuery())
	{
	}

	explicit FRefCountedRHIPooledRenderQuery(FRHIPooledRenderQuery&& InQuery)
	{
		char* data = new char[sizeof(FRHIPooledRenderQuery) + sizeof(int)];
		Query = new (data) FRHIPooledRenderQuery();
		RefCount = new (data + sizeof(FRHIPooledRenderQuery)) int(1);
		*Query = MoveTemp(InQuery);
	}

	~FRefCountedRHIPooledRenderQuery()
	{
		Deref();
	}

	bool IsValid() const
	{
		return Query && Query->IsValid();
	}

	FRHIRenderQuery* GetQuery() const
	{
		return Query ? Query->GetQuery() : nullptr;
	}

	void ReleaseQuery()
	{
		Deref();
	}

	FRefCountedRHIPooledRenderQuery(const FRefCountedRHIPooledRenderQuery& Other)
	{
		Other.Addref();
		RefCount = Other.RefCount;
		Query = Other.Query;
	}

	FRefCountedRHIPooledRenderQuery& operator= (const FRefCountedRHIPooledRenderQuery& Other)
	{
		Other.Addref();
		Deref();
		RefCount = Other.RefCount;
		Query = Other.Query;

		return *this;
	}

	FRefCountedRHIPooledRenderQuery(FRefCountedRHIPooledRenderQuery&& Other)
	{
		RefCount = Other.RefCount;
		Query = Other.Query;
		Other.RefCount = nullptr;
		Other.Query = nullptr;
	}

	FRefCountedRHIPooledRenderQuery& operator=(FRefCountedRHIPooledRenderQuery&& Other)
	{
		Deref();
		RefCount = Other.RefCount;
		Query = Other.Query;
		Other.RefCount = nullptr;
		Other.Query = nullptr;

		return *this;
	}

private:
	void Addref() const
	{
		check(RefCount != nullptr);
		(*RefCount)++;
	}

	void Deref()
	{
		if (RefCount && --(*RefCount) == 0)
		{
			Query->~FRHIPooledRenderQuery();
			char* data = reinterpret_cast<char*>(Query);
			delete[] data;
		}

		Query = nullptr;
		RefCount = nullptr;
	}

	mutable int* RefCount = nullptr;
	FRHIPooledRenderQuery* Query = nullptr;
};

/**
 * Combines consecutive primitives which use the same occlusion query into a single DrawIndexedPrimitive call.
 */
class FOcclusionQueryBatcher
{
public:

	/** The maximum number of consecutive previously occluded primitives which will be combined into a single occlusion query. */
	enum { OccludedPrimitiveQueryBatchSize = 16 };

	/** Initialization constructor. */
	FOcclusionQueryBatcher(class FSceneViewState* ViewState, uint32 InMaxBatchedPrimitives);

	/** Destructor. */
	~FOcclusionQueryBatcher();

	/** @returns True if the batcher has any outstanding batches, otherwise false. */
	bool HasBatches(void) const { return (NumBatchedPrimitives > 0); }

	/** Renders the current batch and resets the batch state. */
	void Flush(FRHICommandList& RHICmdList);

	/**
	 * Batches a primitive's occlusion query for rendering.
	 * @param Bounds - The primitive's bounds.
	 */
	FRHIRenderQuery* BatchPrimitive(const FVector& BoundsOrigin, const FVector& BoundsBoxExtent, FGlobalDynamicVertexBuffer& DynamicVertexBuffer);
	inline int32 GetNumBatchOcclusionQueries() const
	{
		return BatchOcclusionQueries.Num();
	}

private:

	struct FOcclusionBatch
	{
		FRHIRenderQuery* Query;
		FGlobalDynamicVertexBuffer::FAllocation VertexAllocation;
	};

	/** The pending batches. */
	TArray<FOcclusionBatch,SceneRenderingAllocator> BatchOcclusionQueries;

	/** The batch new primitives are being added to. */
	FOcclusionBatch* CurrentBatchOcclusionQuery;

	/** The maximum number of primitives in a batch. */
	const uint32 MaxBatchedPrimitives;

	/** The number of primitives in the current batch. */
	uint32 NumBatchedPrimitives;

	/** The pool to allocate occlusion queries from. */
	FFrameBasedOcclusionQueryPool* OcclusionQueryPool;
};

class FHZBOcclusionTester : public FRenderResource
{
public:
	FHZBOcclusionTester();
	~FHZBOcclusionTester() {}

	// FRenderResource interface
	virtual void InitDynamicRHI() override;
	virtual void ReleaseDynamicRHI() override;
	
	uint32 GetNum() const { return Primitives.Num(); }

	uint32 AddBounds( const FVector& BoundsOrigin, const FVector& BoundsExtent );
	void Submit(FRDGBuilder& GraphBuilder, const FViewInfo& View);

	void MapResults(FRHICommandListImmediate& RHICmdList);
	void UnmapResults(FRHICommandListImmediate& RHICmdList);
	bool IsVisible( uint32 Index ) const;

	bool IsValidFrame(uint32 FrameNumber) const;

	void SetValidFrameNumber(uint32 FrameNumber);

private:
	enum { SizeX = 256 };
	enum { SizeY = 256 };
	enum { FrameNumberMask = 0x7fffffff };
	enum { InvalidFrameNumber = 0xffffffff };

	TArray< FOcclusionPrimitive, SceneRenderingAllocator >	Primitives;

	TRefCountPtr<IPooledRenderTarget>	ResultsTextureCPU;
	const uint8*						ResultsBuffer;


	bool IsInvalidFrame() const;

	// set ValidFrameNumber to a number that cannot be set by SetValidFrameNumber so IsValidFrame will return false for any frame number
	void SetInvalidFrameNumber();

	uint32 ValidFrameNumber;
	FGPUFenceRHIRef Fence;
};

DECLARE_STATS_GROUP(TEXT("Parallel Command List Markers"), STATGROUP_ParallelCommandListMarkers, STATCAT_Advanced);

/** Helper class to marshal data from your RDG pass into the parallel command list set. */
class FParallelCommandListBindings
{
public:
	template <typename ParameterStructType>
	FParallelCommandListBindings(ParameterStructType* ParameterStruct)
		: RenderPassInfo(GetRenderPassInfo(ParameterStruct))
		, GlobalUniformBuffers(GetGlobalUniformBuffers(ParameterStruct))
	{}

	inline void SetOnCommandList(FRHICommandList& RHICmdList) const
	{
		RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("Parallel"));
		RHICmdList.SetGlobalUniformBuffers(GlobalUniformBuffers);
	}

	FRHIRenderPassInfo RenderPassInfo;
	FUniformBufferStaticBindings GlobalUniformBuffers;
};

class FParallelCommandListSet
{
public:
	const FViewInfo& View;
	FRHICommandListImmediate& ParentCmdList;
	FSceneRenderTargets* Snapshot;
	TStatId	ExecuteStat;
	int32 Width;
	int32 NumAlloc;
	int32 MinDrawsPerCommandList;
	// see r.RHICmdBalanceParallelLists
	bool bBalanceCommands;
	// see r.RHICmdSpewParallelListBalance
	bool bSpewBalance;
public:
	TArray<FRHICommandList*,SceneRenderingAllocator> CommandLists;
	TArray<FGraphEventRef,SceneRenderingAllocator> Events;
	// number of draws in this commandlist if known, -1 if not known. Overestimates are better than nothing.
	TArray<int32,SceneRenderingAllocator> NumDrawsIfKnown;
protected:
	//this must be called by deriving classes virtual destructor because it calls the virtual SetStateOnCommandList.
	//C++ will not do dynamic dispatch of virtual calls from destructors so we can't call it in the base class.
	void Dispatch(bool bHighPriority = false);
	FRHICommandList* AllocCommandList();
	bool bCreateSceneContext;
public:
	FParallelCommandListSet(TStatId InExecuteStat, const FViewInfo& InView, FRHICommandListImmediate& InParentCmdList, bool bInCreateSceneContext);
	virtual ~FParallelCommandListSet();

	int32 NumParallelCommandLists() const
	{
		return CommandLists.Num();
	}

	FRHICommandList* NewParallelCommandList();

	FORCEINLINE FGraphEventArray* GetPrereqs()
	{
		return nullptr;
	}

	void AddParallelCommandList(FRHICommandList* CmdList, FGraphEventRef& CompletionEvent, int32 InNumDrawsIfKnown = -1);	

	virtual void SetStateOnCommandList(FRHICommandList& CmdList) {}

	static void WaitForTasks();
private:
	void WaitForTasksInternal();
};

class FRDGParallelCommandListSet final : public FParallelCommandListSet
{
public:
	FRDGParallelCommandListSet(
		FRHICommandListImmediate& InParentCmdList,
		TStatId InStatId,
		const FSceneRenderer& InSceneRenderer,
		const FViewInfo& InView,
		const FParallelCommandListBindings& InBindings,
		float InViewportScale = 1.0f)
		: FParallelCommandListSet(InStatId, InView, InParentCmdList, false)
		, SceneRenderer(InSceneRenderer)
		, Bindings(InBindings)
		, ViewportScale(InViewportScale)
	{}

	~FRDGParallelCommandListSet() override
	{
		Dispatch();
	}

	void SetStateOnCommandList(FRHICommandList& RHICmdList) override;

private:
	const FSceneRenderer& SceneRenderer;
	FParallelCommandListBindings Bindings;
	float ViewportScale;
};

enum EVolumeUpdateType
{
	VUT_MeshDistanceFields = 1,
	VUT_Heightfields = 2,
	VUT_All = VUT_MeshDistanceFields | VUT_Heightfields
};

class FVolumeUpdateRegion
{
public:

	FVolumeUpdateRegion() :
		UpdateType(VUT_All)
	{}

	/** World space bounds. */
	FBox Bounds;

	/** Number of texels in each dimension to update. */
	FIntVector CellsSize;

	EVolumeUpdateType UpdateType;
};

class FGlobalDistanceFieldClipmap
{
public:
	/** World space bounds. */
	FBox Bounds;

	/** Offset applied to UVs so that only new or dirty areas of the volume texture have to be updated. */
	FVector ScrollOffset;

	/** Regions in the volume texture to update. */
	TArray<FVolumeUpdateRegion, TInlineAllocator<3> > UpdateRegions;

	/** Volume texture for this clipmap. */
	TRefCountPtr<IPooledRenderTarget> RenderTarget;
};

class FGlobalDistanceFieldInfo
{
public:

	bool bInitialized;
	TArray<FGlobalDistanceFieldClipmap> MostlyStaticClipmaps;
	TArray<FGlobalDistanceFieldClipmap> Clipmaps;
	FGlobalDistanceFieldParameterData ParameterData;

	void UpdateParameterData(float MaxOcclusionDistance);

	FGlobalDistanceFieldInfo() :
		bInitialized(false)
	{}
};

const int32 GMaxForwardShadowCascades = 4;

#define FORWARD_GLOBAL_LIGHT_DATA_UNIFORM_BUFFER_MEMBER_TABLE \
	SHADER_PARAMETER(uint32,NumLocalLights) \
	SHADER_PARAMETER(uint32, NumReflectionCaptures) \
	SHADER_PARAMETER(uint32, HasDirectionalLight) \
	SHADER_PARAMETER(uint32, NumGridCells) \
	SHADER_PARAMETER(FIntVector, CulledGridSize) \
	SHADER_PARAMETER(uint32, MaxCulledLightsPerCell) \
	SHADER_PARAMETER(uint32, LightGridPixelSizeShift) \
	SHADER_PARAMETER(FVector, LightGridZParams) \
	SHADER_PARAMETER(FVector, DirectionalLightDirection) \
	SHADER_PARAMETER(FVector, DirectionalLightColor) \
	SHADER_PARAMETER(float, DirectionalLightVolumetricScatteringIntensity) \
	SHADER_PARAMETER(uint32, DirectionalLightShadowMapChannelMask) \
	SHADER_PARAMETER(FVector2D, DirectionalLightDistanceFadeMAD) \
	SHADER_PARAMETER(uint32, NumDirectionalLightCascades) \
	SHADER_PARAMETER(FVector4, CascadeEndDepths) \
	SHADER_PARAMETER_ARRAY(FMatrix, DirectionalLightWorldToShadowMatrix, [GMaxForwardShadowCascades]) \
	SHADER_PARAMETER_ARRAY(FVector4, DirectionalLightShadowmapMinMax, [GMaxForwardShadowCascades]) \
	SHADER_PARAMETER(FVector4, DirectionalLightShadowmapAtlasBufferSize) \
	SHADER_PARAMETER(float, DirectionalLightDepthBias) \
	SHADER_PARAMETER(uint32, DirectionalLightUseStaticShadowing) \
	SHADER_PARAMETER(uint32, SimpleLightsEndIndex) \
	SHADER_PARAMETER(uint32, ClusteredDeferredSupportedEndIndex) \
	SHADER_PARAMETER(FVector4, DirectionalLightStaticShadowBufferSize) \
	SHADER_PARAMETER(FMatrix, DirectionalLightWorldToStaticShadow) \
	SHADER_PARAMETER_TEXTURE(Texture2D, DirectionalLightShadowmapAtlas) \
	SHADER_PARAMETER_SAMPLER(SamplerState, ShadowmapSampler) \
	SHADER_PARAMETER_TEXTURE(Texture2D, DirectionalLightStaticShadowmap) \
	SHADER_PARAMETER_SAMPLER(SamplerState, StaticShadowmapSampler) \
	SHADER_PARAMETER_SRV(StrongTypedBuffer<float4>, ForwardLocalLightBuffer) \
	SHADER_PARAMETER_SRV(StrongTypedBuffer<uint>, NumCulledLightsGrid) \
	SHADER_PARAMETER_SRV(StrongTypedBuffer<uint>, CulledLightDataGrid) \
	SHADER_PARAMETER_TEXTURE(Texture2D, DummyRectLightSourceTexture)

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FForwardLightData,)
	FORWARD_GLOBAL_LIGHT_DATA_UNIFORM_BUFFER_MEMBER_TABLE
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FForwardLightingViewResources
{
public:
	FForwardLightData ForwardLightData;
	const FLightSceneProxy* SelectedForwardDirectionalLightProxy = nullptr;

	TUniformBufferRef<FForwardLightData> ForwardLightDataUniformBuffer;
	FDynamicReadBuffer ForwardLocalLightBuffer;
	FRWBuffer NumCulledLightsGrid;
	FRWBuffer CulledLightDataGrid;

	void Release()
	{
		ForwardLightDataUniformBuffer.SafeRelease();
		ForwardLocalLightBuffer.Release();
		NumCulledLightsGrid.Release();
		CulledLightDataGrid.Release();
	}
};

#define ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA 1

class FForwardLightingCullingResources
{
public:

#if ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
	FDynamicReadBuffer ViewSpacePosAndRadiusData;
	FDynamicReadBuffer ViewSpaceDirAndPreprocAngleData;
#endif // ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
	void Release()
	{
#if ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
		ViewSpacePosAndRadiusData.Release();
		ViewSpaceDirAndPreprocAngleData.Release();
#endif // ENABLE_LIGHT_CULLING_VIEW_SPACE_BUILD_DATA
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FVolumetricFogGlobalData,) 
	SHADER_PARAMETER(FIntVector, GridSizeInt)
	SHADER_PARAMETER(FVector, GridSize)
	SHADER_PARAMETER(FVector, GridZParams)
	SHADER_PARAMETER(FVector2D, SVPosToVolumeUV)
	SHADER_PARAMETER(FIntPoint, FogGridToPixelXY)
	SHADER_PARAMETER(float, MaxDistance)
	SHADER_PARAMETER(FVector, HeightFogInscatteringColor)
	SHADER_PARAMETER(FVector, HeightFogDirectionalLightInscatteringColor)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern void SetupVolumetricFogGlobalData(const FViewInfo& View, FVolumetricFogGlobalData& Parameters);

struct FTransientLightFunctionTextureAtlasTile
{
	bool bIsDefault;		// If true, then the atlas item generation can be skipped
	FRDGTextureRef Texture;
	FIntRect RectBound;
	FVector4 MinMaxUvBound;
};

struct FVolumetricFogLocalLightFunctionInfo
{
	FTransientLightFunctionTextureAtlasTile AtlasTile;
	FMatrix LightFunctionMatrix;
};

class FVolumetricFogViewResources
{
public:
	TUniformBufferRef<FVolumetricFogGlobalData> VolumetricFogGlobalData;

	FRDGTextureRef IntegratedLightScatteringTexture = nullptr;

	// TODO: right now the lightfunction atlas is dedicated to the volumetric fog.
	// Later we could put the allocated atlas tiles on FLightSceneInfo and uploaded as light data on GPU
	// so that the lightfunction atlas can be used for forward rendering or tiled lighting.
	// For this to work we would also need to add the default white light functoin as an atlas item.
	// Note: this is not a smart pointer since it is allocated using the GraphBuilder frame transient memory.
	FTransientLightFunctionTextureAtlas* TransientLightFunctionTextureAtlas = nullptr;

	TMap<FLightSceneInfo*, FVolumetricFogLocalLightFunctionInfo> LocalLightFunctionData;

	FVolumetricFogViewResources()
	{}

	void Release()
	{
		IntegratedLightScatteringTexture = nullptr;
		TransientLightFunctionTextureAtlas = nullptr;
	}
};

struct FVolumetricMeshBatch
{
	const FMeshBatch* Mesh;
	const FPrimitiveSceneProxy* Proxy;
};

struct FSkyMeshBatch
{
	const FMeshBatch* Mesh;
	const FPrimitiveSceneProxy* Proxy;
	bool bVisibleInMainPass : 1;
	bool bVisibleInRealTimeSkyCapture : 1;
};

struct FMeshDecalBatch
{
	const FMeshBatch* Mesh;
	const FPrimitiveSceneProxy* Proxy;
	int16 SortKey;

	FORCEINLINE bool operator<(const FMeshDecalBatch& rhs) const
	{
		return SortKey < rhs.SortKey;
	}
};

// DX11 maximum 2d texture array size is D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION = 2048, and 2048/6 = 341.33.
static const int32 GMaxNumReflectionCaptures = 341;

/** Per-reflection capture data needed by the shader. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FReflectionCaptureShaderData,)
	SHADER_PARAMETER_ARRAY(FVector4,PositionAndRadius,[GMaxNumReflectionCaptures])
	// R is brightness, G is array index, B is shape
	SHADER_PARAMETER_ARRAY(FVector4,CaptureProperties,[GMaxNumReflectionCaptures])
	SHADER_PARAMETER_ARRAY(FVector4,CaptureOffsetAndAverageBrightness,[GMaxNumReflectionCaptures])
	// Stores the box transform for a box shape, other data is packed for other shapes
	SHADER_PARAMETER_ARRAY(FMatrix,BoxTransform,[GMaxNumReflectionCaptures])
	SHADER_PARAMETER_ARRAY(FVector4,BoxScales,[GMaxNumReflectionCaptures])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// Structure in charge of storing all information about TAA's history.
struct FTemporalAAHistory
{
	// Number of render target in the history.
	static constexpr uint32 kRenderTargetCount = 4;

	// Render targets holding's pixel history.
	//  scene color's RGBA are in RT[0].
	TStaticArray<TRefCountPtr<IPooledRenderTarget>, kRenderTargetCount> RT;

	// Reference size of RT. Might be different than RT's actual size to handle down res.
	FIntPoint ReferenceBufferSize;

	// Viewport coordinate of the history in RT according to ReferenceBufferSize.
	FIntRect ViewportRect;


	void SafeRelease()
	{
		for (uint32 i = 0; i < kRenderTargetCount; i++)
		{
			RT[i].SafeRelease();
		}
	}

	bool IsValid() const
	{
		return RT[0].IsValid();
	}
};

/** Temporal history for a denoiser. */
struct FScreenSpaceDenoiserHistory
{
	// Number of history render target to store.
	static constexpr int32 RTCount = 3;

	// Scissor of valid data in the render target;
	FIntRect Scissor;

	// Render target specific to the history.
	TStaticArray<TRefCountPtr<IPooledRenderTarget>, RTCount> RT;

	// The texture for tile classification.
	TRefCountPtr<IPooledRenderTarget> TileClassification;


	void SafeRelease()
	{
		for (int32 i = 0; i < RTCount; i++)
			RT[i].SafeRelease();
		TileClassification.SafeRelease();
	}

	bool IsValid() const
	{
		return RT[0].IsValid();
	}
};



// Structure for storing a frame of GTAO history.
struct FGTAOTAAHistory
{
	// Render targets holding a frame's pixel history.
	//  scene color's RGBA are in RT[0].
	TRefCountPtr<IPooledRenderTarget> RT;

	// Reference size of RT. Might be different than RT's actual size to handle down res.
	FIntPoint ReferenceBufferSize;

	// Viewport coordinate of the history in RT according to ReferenceBufferSize.
	FIntRect ViewportRect;

	void SafeRelease()
	{
		RT.SafeRelease();
	}

	bool IsValid() const
	{
		return RT.IsValid();
	}
};


// Plugins can derive from this and use it for their own purposes
class RENDERER_API ICustomTemporalAAHistory : public IRefCountedObject
{
public:
	virtual ~ICustomTemporalAAHistory() {}
};

// Structure that hold all information related to previous frame.
struct FPreviousViewInfo
{
	// View rect
	FIntRect ViewRect;

	// View matrices.
	FViewMatrices ViewMatrices;

	// Scene color's PreExposure.
	float SceneColorPreExposure = 1.0f;

	// Depth buffer and Normals of the previous frame generating this history entry for bilateral kernel rejection.
	TRefCountPtr<IPooledRenderTarget> DepthBuffer;
	TRefCountPtr<IPooledRenderTarget> GBufferA;
	TRefCountPtr<IPooledRenderTarget> GBufferB;
	TRefCountPtr<IPooledRenderTarget> GBufferC;
	TRefCountPtr<IPooledRenderTarget> ImaginaryReflectionDepthBuffer;
	TRefCountPtr<IPooledRenderTarget> ImaginaryReflectionGBufferA;

	// Compressed scene textures for bandwidth efficient bilateral kernel rejection.
	// DeviceZ as float16, and normal in view space.
	TRefCountPtr<IPooledRenderTarget> CompressedDepthViewNormal;
	TRefCountPtr<IPooledRenderTarget> ImaginaryReflectionCompressedDepthViewNormal;

	// Bleed free scene color to use for screen space ray tracing.
	TRefCountPtr<IPooledRenderTarget> ScreenSpaceRayTracingInput;

	// Temporal AA result of last frame
	FTemporalAAHistory TemporalAAHistory;

	// Custom Temporal AA result of last frame, used by plugins
	TRefCountPtr<ICustomTemporalAAHistory> CustomTemporalAAHistory;

	// Half resolution version temporal AA result of last frame
	TRefCountPtr<IPooledRenderTarget> HalfResTemporalAAHistory;

	// Temporal AA history for diaphragm DOF.
	FTemporalAAHistory DOFSetupHistory;
	
	// Temporal AA history for SSR
	FTemporalAAHistory SSRHistory;
	FTemporalAAHistory WaterSSRHistory;

	// Scene color input for SSR, that can be different from TemporalAAHistory.RT[0] if there is a SSR
	// input post process material.
	TRefCountPtr<IPooledRenderTarget> CustomSSRInput;

	// History for the reflections
	FScreenSpaceDenoiserHistory ReflectionsHistory;
	FScreenSpaceDenoiserHistory WaterReflectionsHistory;
	
	// History for the ambient occlusion
	FScreenSpaceDenoiserHistory AmbientOcclusionHistory;

	// History for GTAO
	FGTAOTAAHistory				 GTAOHistory;

	// History for global illumination
	FScreenSpaceDenoiserHistory DiffuseIndirectHistory;

	// History for sky light
	FScreenSpaceDenoiserHistory SkyLightHistory;

	// History for reflected sky light
	FScreenSpaceDenoiserHistory ReflectedSkyLightHistory;

	// History for shadow denoising.
	TMap<const ULightComponent*, TSharedPtr<FScreenSpaceDenoiserHistory>> ShadowHistories;

	// History for denoising all lights penumbra at once.
	FScreenSpaceDenoiserHistory PolychromaticPenumbraHarmonicsHistory;

	// Mobile bloom setup eye adaptation surface.
	TRefCountPtr<IPooledRenderTarget> MobileBloomSetup_EyeAdaptation;
	// Mobile temporal AA surface.
	TRefCountPtr<IPooledRenderTarget> MobileAaBloomSunVignette;
	TRefCountPtr<IPooledRenderTarget> MobileAaColor;
};

class FViewCommands
{
public:
	FViewCommands()
	{
		for (int32 PassIndex = 0; PassIndex < EMeshPass::Num; ++PassIndex)
		{
			NumDynamicMeshCommandBuildRequestElements[PassIndex] = 0;
		}
	}

	TStaticArray<FMeshCommandOneFrameArray, EMeshPass::Num> MeshCommands;
	TStaticArray<int32, EMeshPass::Num> NumDynamicMeshCommandBuildRequestElements;
	TStaticArray<TArray<const FStaticMeshBatch*, SceneRenderingAllocator>, EMeshPass::Num> DynamicMeshCommandBuildRequests;
};

typedef TArray<FViewCommands, TInlineAllocator<4>> FViewVisibleCommandsPerView;

#if RHI_RAYTRACING
struct FRayTracingMeshBatchWorkItem
{
	FRayTracingMeshBatchWorkItem(TArray<FMeshBatch>& InBatches, FPrimitiveSceneProxy* InSceneProxy, uint32 InInstanceIndex) :
		SceneProxy(InSceneProxy),
		InstanceIndex(InInstanceIndex)
	{
		Swap(MeshBatches, InBatches);
	}

	TArray<FMeshBatch> MeshBatches;
	FPrimitiveSceneProxy* SceneProxy;
	uint32 InstanceIndex;
};

/** Convenience struct for all lighting data used by ray tracing effects using RayTracingLightingCommon.ush */
struct FRayTracingLightData
{
	/** Uniform buffer with all lighting data */
	TUniformBufferRef<FRaytracingLightDataPacked>	UniformBuffer;

	/** Structured buffer containing all light data */
	FStructuredBufferRHIRef							LightBuffer;
	FShaderResourceViewRHIRef						LightBufferSRV;

	/** Buffer of light indices reference by the culling volume */
	FRWBuffer										LightIndices;

	/** Camera-centered volume used to cull lights to cells */
	FStructuredBufferRHIRef							LightCullVolume;
	FShaderResourceViewRHIRef						LightCullVolumeSRV;
};
#endif

/** A FSceneView with additional state used by the scene renderer. */
class FViewInfo : public FSceneView
{
public:

	/* Final position of the view in the final render target (in pixels), potentially scaled by ScreenPercentage */
	FIntRect ViewRect;

	/** 
	 * The view's state, or NULL if no state exists.
	 * This should be used internally to the renderer module to avoid having to cast View.State to an FSceneViewState*
	 */
	FSceneViewState* ViewState;

	/** Cached view uniform shader parameters, to allow recreating the view uniform buffer without having to fill out the entire struct. */
	TUniquePtr<FViewUniformShaderParameters> CachedViewUniformShaderParameters;

	/** A map from primitive ID to a boolean visibility value. */
	FSceneBitArray PrimitiveVisibilityMap;

	/** Bit set when a primitive is known to be unoccluded. */
	FSceneBitArray PrimitiveDefinitelyUnoccludedMap;

	/** A map from primitive ID to a boolean is fading value. */
	FSceneBitArray PotentiallyFadingPrimitiveMap;

	/** A map from primitive ID to a boolean is distance culled */
	FSceneBitArray DistanceCullingPrimitiveMap;

	/** Primitive fade uniform buffers, indexed by packed primitive index. */
	TArray<FRHIUniformBuffer*,SceneRenderingAllocator> PrimitiveFadeUniformBuffers;

	/**  Bit set when a primitive has a valid fade uniform buffer. */
	FSceneBitArray PrimitiveFadeUniformBufferMap;

	/** One frame dither fade in uniform buffer. */
	FUniformBufferRHIRef DitherFadeInUniformBuffer;

	/** One frame dither fade out uniform buffer. */
	FUniformBufferRHIRef DitherFadeOutUniformBuffer;

	/** A map from primitive ID to the primitive's view relevance. */
	TArray<FPrimitiveViewRelevance,SceneRenderingAllocator> PrimitiveViewRelevanceMap;

	/** A map from static mesh ID to a boolean visibility value. */
	FSceneBitArray StaticMeshVisibilityMap;

	/** A map from static mesh ID to a boolean dithered LOD fade out value. */
	FSceneBitArray StaticMeshFadeOutDitheredLODMap;

	/** A map from static mesh ID to a boolean dithered LOD fade in value. */
	FSceneBitArray StaticMeshFadeInDitheredLODMap;

	/** Will only contain relevant primitives for view and/or shadow */
	TArray<FLODMask, SceneRenderingAllocator> PrimitivesLODMask;

	/** The dynamic primitives with simple lights visible in this view. */
	TArray<FPrimitiveSceneInfo*, SceneRenderingAllocator> VisibleDynamicPrimitivesWithSimpleLights;

	/** Number of dynamic primitives visible in this view. */
	int32 NumVisibleDynamicPrimitives;

	/** Number of dynamic editor primitives visible in this view. */
	int32 NumVisibleDynamicEditorPrimitives;

	/** Number of dynamic mesh elements per mesh pass (inside FViewInfo::DynamicMeshElements). */
	int32 NumVisibleDynamicMeshElements[EMeshPass::Num];

	/** List of visible primitives with dirty indirect lighting cache buffers */
	TArray<FPrimitiveSceneInfo*,SceneRenderingAllocator> DirtyIndirectLightingCacheBufferPrimitives;

	/** Maps a single primitive to it's per view translucent self shadow uniform buffer. */
	FTranslucentSelfShadowUniformBufferMap TranslucentSelfShadowUniformBufferMap;

	/** View dependent global distance field clipmap info. */
	FGlobalDistanceFieldInfo GlobalDistanceFieldInfo;

	/** Count of translucent prims for this view. */
	FTranslucenyPrimCount TranslucentPrimCount;
	
	bool bHasDistortionPrimitives;
	bool bHasCustomDepthPrimitives;

	/** Mesh batches with for mesh decal rendering. */
	TArray<FMeshDecalBatch, SceneRenderingAllocator> MeshDecalBatches;

	/** Mesh batches with a volumetric material. */
	TArray<FVolumetricMeshBatch, SceneRenderingAllocator> VolumetricMeshBatches;

	/** Mesh batches with a sky material. */
	TArray<FSkyMeshBatch, SceneRenderingAllocator> SkyMeshBatches;

	/** A map from light ID to a boolean visibility value. */
	TArray<FVisibleLightViewInfo,SceneRenderingAllocator> VisibleLightInfos;

	/** The view's batched elements. */
	FBatchedElements BatchedViewElements;

	/** The view's batched elements, above all other elements, for gizmos that should never be occluded. */
	FBatchedElements TopBatchedViewElements;

	/** The view's mesh elements. */
	TIndirectArray<FMeshBatch> ViewMeshElements;

	/** The view's mesh elements for the foreground (editor gizmos and primitives )*/
	TIndirectArray<FMeshBatch> TopViewMeshElements;

	/** The dynamic resources used by the view elements. */
	TArray<FDynamicPrimitiveResource*> DynamicResources;

	/** Gathered in initviews from all the primitives with dynamic view relevance, used in each mesh pass. */
	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator> DynamicMeshElements;

	/* [PrimitiveIndex] = end index index in DynamicMeshElements[], to support GetDynamicMeshElementRange(). Contains valid values only for visible primitives with bDynamicRelevance. */
	TArray<uint32, SceneRenderingAllocator> DynamicMeshEndIndices;

	/** Hair strands dynamic mesh element. */
	TArray<FMeshBatchAndRelevance, SceneRenderingAllocator> HairStrandsMeshElements;

	/* Mesh pass relevance for gathered dynamic mesh elements. */
	TArray<FMeshPassMask, SceneRenderingAllocator> DynamicMeshElementsPassRelevance;

	/** Gathered in UpdateRayTracingWorld from all the primitives with dynamic view relevance, used in each mesh pass. */
	TArray<FMeshBatchAndRelevance, SceneRenderingAllocator> RayTracedDynamicMeshElements;

	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator> DynamicEditorMeshElements;

	FSimpleElementCollector SimpleElementCollector;

	FSimpleElementCollector EditorSimpleElementCollector;

	/** Tracks dynamic primitive data for upload to GPU Scene, when enabled. */
	TArray<FPrimitiveUniformShaderParameters> DynamicPrimitiveShaderData;

	/** Only one of the resources(TextureBuffer or Texture2D) will be used depending on the Mobile.UseGPUSceneTexture cvar */
	FRWBufferStructured OneFramePrimitiveShaderDataBuffer;
	FTextureRWBuffer2D OneFramePrimitiveShaderDataTexture;

	TStaticArray<FParallelMeshDrawCommandPass, EMeshPass::Num> ParallelMeshDrawCommandPasses;
	
#if RHI_RAYTRACING
	TUniquePtr<FRayTracingMeshResourceCollector> RayTracingMeshResourceCollector;
	FRayTracingMeshCommandOneFrameArray VisibleRayTracingMeshCommands;
	FDynamicRayTracingMeshCommandStorage DynamicRayTracingMeshCommandStorage;

	FGraphEventArray AddRayTracingMeshBatchTaskList;
	TArray<FRayTracingMeshBatchWorkItem> AddRayTracingMeshBatchData;

	TArray<FRayTracingMeshCommandOneFrameArray> VisibleRayTracingMeshCommandsParallel;
	TArray<FDynamicRayTracingMeshCommandStorage> DynamicRayTracingMeshCommandStorageParallel;
#endif

	// Used by mobile renderer to determine whether static meshes will be rendered with CSM shaders or not.
	FMobileCSMVisibilityInfo MobileCSMVisibilityInfo;

	//Spotlight shadow info for mobile.
	FMobileMovableSpotLightsShadowInfo MobileMovableSpotLightsShadowInfo;

	/** Parameters for exponential height fog. */
	FVector4 ExponentialFogParameters;
	FVector4 ExponentialFogParameters2;
	FVector ExponentialFogColor;
	float FogMaxOpacity;
	FVector4 ExponentialFogParameters3;
	FVector2D SinCosInscatteringColorCubemapRotation;

	UTexture* FogInscatteringColorCubemap;
	FVector FogInscatteringTextureParameters;

	/** Parameters for directional inscattering of exponential height fog. */
	bool bUseDirectionalInscattering;
	float DirectionalInscatteringExponent;
	float DirectionalInscatteringStartDistance;
	FVector InscatteringLightDirection;
	FLinearColor DirectionalInscatteringColor;

	/** Translucency lighting volume properties. */
	FVector TranslucencyLightingVolumeMin[TVC_MAX];
	float TranslucencyVolumeVoxelSize[TVC_MAX];
	FVector TranslucencyLightingVolumeSize[TVC_MAX];

	/** Number of samples in the temporal AA sequqnce */
	int32 TemporalJitterSequenceLength;

	/** Index of the temporal AA jitter in the sequence. */
	int32 TemporalJitterIndex;

	/** Temporal AA jitter at the pixel scale. */
	FVector2D TemporalJitterPixels;

	/** Whether FSceneViewState::PrevFrameViewInfo can be updated with this view. */
	uint32 bStatePrevViewInfoIsReadOnly : 1;

	/** true if all PrimitiveVisibilityMap's bits are set to false. */
	uint32 bHasNoVisiblePrimitive : 1;

	/** true if the view has at least one mesh with a translucent material. */
	uint32 bHasTranslucentViewMeshElements : 1;
	/** Indicates whether previous frame transforms were reset this frame for any reason. */
	uint32 bPrevTransformsReset : 1;
	/** Whether we should ignore queries from last frame (useful to ignoring occlusions on the first frame after a large camera movement). */
	uint32 bIgnoreExistingQueries : 1;
	/** Whether we should submit new queries this frame. (used to disable occlusion queries completely. */
	uint32 bDisableQuerySubmissions : 1;
	/** Whether we should disable distance-based fade transitions for this frame (usually after a large camera movement.) */
	uint32 bDisableDistanceBasedFadeTransitions : 1;
	/** Whether the view has any materials that use the global distance field. */
	uint32 bUsesGlobalDistanceField : 1;
	uint32 bUsesLightingChannels : 1;
	uint32 bTranslucentSurfaceLighting : 1;
	/** Whether the view has any materials that read from scene depth. */
	uint32 bUsesSceneDepth : 1;
	uint32 bCustomDepthStencilValid : 1;
	uint32 bUsesCustomDepthStencilInTranslucentMaterials : 1;
	uint32 bShouldRenderDepthToTranslucency : 1;

	/** Whether fog should only be computed on rendered opaque pixels or not. */
	uint32 bFogOnlyOnRenderedOpaque : 1;

	/** 
	 * true if the scene has at least one decal. Used to disable stencil operations in the mobile base pass when the scene has no decals.
	 * TODO: Right now decal visibility is computed right before rendering them. Ideally it should be done in InitViews and this flag should be replaced with list of visible decals  
	 */
	uint32 bSceneHasDecals : 1;
	/**
	 * true if the scene has at least one mesh with a material tagged as sky. 
	 * This is used to skip the sky rendering part during the SkyAtmosphere pass on non mobile platforms.
	 */
	uint32 bSceneHasSkyMaterial : 1;
	/**
	 * true if the scene has at least one mesh with a material tagged as water visible in a view.
	 */
	uint32 bHasSingleLayerWaterMaterial : 1;
	/**
	 * true if the scene has at least one mesh with a material that needs dual blending AND is applied post DOF. If true,
	 * that means we need to run the separate modulation render pass.
	 */
	uint32 bHasTranslucencySeparateModulation : 1;

	/** Bitmask of all shading models used by primitives in this view */
	uint16 ShadingModelMaskInView;

	/** Informations from the previous frame to use for this view. */
	FPreviousViewInfo PrevViewInfo;

	/** An intermediate number of visible static meshes.  Doesn't account for occlusion until after FinishOcclusionQueries is called. */
	int32 NumVisibleStaticMeshElements;

	/** Frame's exposure. Always > 0. */
	float PreExposure;

	/** Precomputed visibility data, the bits are indexed by VisibilityId of a primitive component. */
	const uint8* PrecomputedVisibilityData;

	FOcclusionQueryBatcher IndividualOcclusionQueries;
	FOcclusionQueryBatcher GroupedOcclusionQueries;

	// Furthest and closest Hierarchical Z Buffer
	TRefCountPtr<IPooledRenderTarget> HZB;
	TRefCountPtr<IPooledRenderTarget> ClosestHZB;

	int32 NumBoxReflectionCaptures;
	int32 NumSphereReflectionCaptures;
	float FurthestReflectionCaptureDistance;
	TUniformBufferRef<FReflectionCaptureShaderData> ReflectionCaptureUniformBuffer;

	TRefCountPtr<IPooledRenderTarget> HalfResDepthSurfaceCheckerboardMinMax;

	// Sky / Atmosphere textures (transient owned by this view info) and pointer to constants owned by SkyAtmosphere proxy.
	TRefCountPtr<IPooledRenderTarget> SkyAtmosphereCameraAerialPerspectiveVolume;
	TRefCountPtr<IPooledRenderTarget> SkyAtmosphereViewLutTexture;
	const FAtmosphereUniformShaderParameters* SkyAtmosphereUniformShaderParameters;

	TRefCountPtr<IPooledRenderTarget> VolumetricCloudSkyAO;
	TUniformBufferRef<FViewUniformShaderParameters> VolumetricRenderTargetViewUniformBuffer;
	// The effective cloud shadow target this frame independently of the fact that a view can have a state (primary view) or not (sky light reflection capture)
	TRefCountPtr<IPooledRenderTarget> VolumetricCloudShadowRenderTarget[NUM_ATMOSPHERE_LIGHTS];

	/** Used when there is no view state, buffers reallocate every frame. */
	TUniquePtr<FForwardLightingViewResources> ForwardLightingResourcesStorage;

	FVolumetricFogViewResources VolumetricFogResources;

	// Size of the HZB's mipmap 0
	// NOTE: the mipmap 0 is downsampled version of the depth buffer
	FIntPoint HZBMipmap0Size;

	/** Used by occlusion for percent unoccluded calculations. */
	float OneOverNumPossiblePixels;

	TOptional<FMobileLightShaftInfo> MobileLightShaft;

	FHeightfieldLightingViewInfo HeightfieldLightingViewInfo;

	FGlobalShaderMap* ShaderMap;

	bool bIsSnapshot;

	// Whether this view should use an HMD hidden area mask where appropriate.
	bool bHMDHiddenAreaMaskActive = false;

	// Whether this view should use compute passes where appropriate.
	bool bUseComputePasses = false;

	// Optional stencil dithering optimization during prepasses
	bool bAllowStencilDither;

	/** Custom visibility query for view */
	ICustomVisibilityQuery* CustomVisibilityQuery;

	TArray<FPrimitiveSceneInfo*, SceneRenderingAllocator> IndirectShadowPrimitives;

	/** Only one of the resources(TextureBuffer or Texture2D) will be used depending on the Mobile.UseGPUSceneTexture cvar */
	FShaderResourceViewRHIRef PrimitiveSceneDataOverrideSRV;
	FTexture2DRHIRef PrimitiveSceneDataTextureOverrideRHI;
	FShaderResourceViewRHIRef LightmapSceneDataOverrideSRV;

	FRWBufferStructured ShaderPrintValueBuffer;

	FShaderDrawDebugData ShaderDrawData;

#if RHI_RAYTRACING
	TArray<FRayTracingGeometryInstance, SceneRenderingAllocator> RayTracingGeometryInstances;

	// Geometries which still have a pending build request but are used this frame and require a force build
	TSet<const FRayTracingGeometry*> ForceBuildRayTracingGeometries;

#ifdef DO_CHECK
	// Keep track of all used RT Geometries which are used to validate the vertex buffer data (see FRayTracingGeometry::DynamicGeometrySharedBufferGenerationID)
	TSet<const FRayTracingGeometry*> RayTracingGeometries;
#endif

	// Ray tracing scene specific to this view
	FRayTracingScene RayTracingScene;

	// Primary pipeline state object to be used with the ray tracing scene for this view.
	// Material shaders are only available when using this pipeline.
	FRayTracingPipelineState* RayTracingMaterialPipeline = nullptr;

	// Pipeline state object to be used with deferred material processing.
	FRayTracingPipelineState* RayTracingMaterialGatherPipeline = nullptr;

	TArray<FRayTracingLocalShaderBindingWriter*>	RayTracingMaterialBindings; // One per binding task
	FGraphEventRef									RayTracingMaterialBindingsTask;

	// Common resources used for lighting in ray tracing effects
	TRefCountPtr<IPooledRenderTarget>				RayTracingSubSurfaceProfileTexture;
	FShaderResourceViewRHIRef						RayTracingSubSurfaceProfileSRV;
	FRayTracingLightData							RayTracingLightData;

#endif // RHI_RAYTRACING

	uint32 InstancedStereoWidth = 0;

	/** 
	 * Initialization constructor. Passes all parameters to FSceneView constructor
	 */
	RENDERER_API FViewInfo(const FSceneViewInitOptions& InitOptions);

	/** 
	* Initialization constructor. 
	* @param InView - copy to init with
	*/
	explicit FViewInfo(const FSceneView* InView);

	/** 
	* Destructor. 
	*/
	RENDERER_API ~FViewInfo();

#if DO_CHECK
	/** Verifies all the assertions made on members. */
	bool VerifyMembersChecks() const;
#endif

	/** Returns the size of view rect after primary upscale ( == only with secondary screen percentage). */
	RENDERER_API FIntPoint GetSecondaryViewRectSize() const;
	
	/** Returns whether the view requires a secondary upscale. */
	bool RequiresSecondaryUpscale() const
	{
		return UnscaledViewRect.Size() != GetSecondaryViewRectSize();
	}

	/** Creates ViewUniformShaderParameters given a set of view transforms. */
	RENDERER_API void SetupUniformBufferParameters(
		FSceneRenderTargets& SceneContext,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrices,
		FBox* OutTranslucentCascadeBoundsArray, 
		int32 NumTranslucentCascades,
		FViewUniformShaderParameters& ViewUniformShaderParameters) const;

	/** Recreates ViewUniformShaderParameters, taking the view transform from the View Matrices */
	inline void SetupUniformBufferParameters(
		FSceneRenderTargets& SceneContext,
		FBox* OutTranslucentCascadeBoundsArray,
		int32 NumTranslucentCascades,
		FViewUniformShaderParameters& ViewUniformShaderParameters) const
	{
		SetupUniformBufferParameters(SceneContext,
			ViewMatrices,
			PrevViewInfo.ViewMatrices,
			OutTranslucentCascadeBoundsArray,
			NumTranslucentCascades,
			ViewUniformShaderParameters);
	}

	void UpdateLateLatchData();

	void SetupDefaultGlobalDistanceFieldUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const;
	void SetupGlobalDistanceFieldUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const;
	void SetupVolumetricFogUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters) const;

	/** Initializes the RHI resources used by this view. */
	void InitRHIResources();

	/** Determines distance culling and fades if the state changes */
	bool IsDistanceCulled(float DistanceSquared, float MinDrawDistance, float InMaxDrawDistance, const FPrimitiveSceneInfo* PrimitiveSceneInfo);

	bool IsDistanceCulled_AnyThread(float DistanceSquared, float MinDrawDistance, float InMaxDrawDistance, const FPrimitiveSceneInfo* PrimitiveSceneInfo, bool& bOutMayBeFading, bool& bOutFadingIn) const;

	/** @return - whether this primitive has completely faded out */
	bool UpdatePrimitiveFadingState(const FPrimitiveSceneInfo* PrimitiveSceneInfo, bool bFadingIn);

	/** Allocates and returns the current eye adaptation texture. */
	using FSceneView::GetEyeAdaptationTexture;
	IPooledRenderTarget* GetEyeAdaptationTexture(FRHICommandList& RHICmdList) const;
	IPooledRenderTarget* GetLastEyeAdaptationTexture(FRHICommandList& RHICmdList) const;

	/** Allocates and returns the current eye adaptation buffer. */
	using FSceneView::GetEyeAdaptationBuffer;
	const FExposureBufferData* GetEyeAdaptationBuffer(FRHICommandListImmediate& RHICmdList) const;
	const FExposureBufferData* GetLastEyeAdaptationBuffer(FRHICommandListImmediate& RHICmdList) const;

#if WITH_MGPU
	void BroadcastEyeAdaptationTemporalEffect(FRHICommandList& RHICmdList);
	void WaitForEyeAdaptationTemporalEffect(FRHICommandList& RHICmdList);
#endif

	/** Get the last valid exposure value for eye adapation. */
	float GetLastEyeAdaptationExposure() const;

	/** Get the last valid average scene luminange for eye adapation (exposure compensation curve). */
	float GetLastAverageSceneLuminance() const;

	/**Swap the order of the two eye adaptation targets in the double buffer system */
	void SwapEyeAdaptationTextures(FRDGBuilder& GraphBuilder) const;
	void SwapEyeAdaptationBuffers() const;
	
	/** Returns the load action to use when overwriting all pixels of a target that you intend to read from. Takes into account the HMD hidden area mesh. */
	ERenderTargetLoadAction GetOverwriteLoadAction() const;

	/** Informs sceneinfo that tonemapping LUT has queued commands to compute it at least once */
	void SetValidTonemappingLUT() const;

	/** Gets the tonemapping LUT texture, previously computed by the CombineLUTS post process,
	* for stereo rendering, this will force the post-processing to use the same texture for both eyes*/
	IPooledRenderTarget* GetTonemappingLUT() const;

	/** Gets the rendertarget that will be populated by CombineLUTS post process 
	* for stereo rendering, this will force the post-processing to use the same render target for both eyes*/
	IPooledRenderTarget* GetTonemappingLUT(FRHICommandList& RHICmdList, const int32 LUTSize, const bool bUseVolumeLUT, const bool bNeedUAV, const bool bNeedFloatOutput) const;

	bool IsFirstInFamily() const
	{
		return Family->Views[0] == this;
	}

	bool IsLastInFamily() const
	{
		return Family->Views.Last() == this;
	}

	ERenderTargetLoadAction DecayLoadAction(ERenderTargetLoadAction RequestedLoadAction) const
	{
		return IsFirstInFamily() || Family->bMultiGPUForkAndJoin ? RequestedLoadAction : ERenderTargetLoadAction::ELoad;
	}

	/** Instanced stereo and multi-view only need to render the left eye. */
	bool ShouldRenderView() const 
	{
		if (bHasNoVisiblePrimitive)
		{
			return false;
		}
		else if (!bIsInstancedStereoEnabled && !bIsMobileMultiViewEnabled)
		{
			return true;
		}
		else if ((bIsInstancedStereoEnabled || bIsMobileMultiViewEnabled) && !IStereoRendering::IsASecondaryPass(StereoPass))
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	inline FVector GetPrevViewDirection() const { return PrevViewInfo.ViewMatrices.GetViewMatrix().GetColumn(2); }

	/** Create a snapshot of this view info on the scene allocator. */
	FViewInfo* CreateSnapshot() const;

	/** Destroy all snapshots before we wipe the scene allocator. */
	static void DestroyAllSnapshots();

	// Get the range in DynamicMeshElements[] for a given PrimitiveIndex
	// @return range (start is inclusive, end is exclusive)
	FInt32Range GetDynamicMeshElementRange(uint32 PrimitiveIndex) const;

private:
	// Cache of TEXTUREGROUP_World to create view's samplers on render thread.
	// may not have a valid value if FViewInfo is created on the render thread.
	ESamplerFilter WorldTextureGroupSamplerFilter;
	bool bIsValidWorldTextureGroupSamplerFilter;

	FSceneViewState* GetEyeAdaptationViewState() const;

	/** Initialization that is common to the constructors. */
	void Init();

	/** Calculates bounding boxes for the translucency lighting volume cascades. */
	void CalcTranslucencyLightingVolumeBounds(FBox* InOutCascadeBoundsArray, int32 NumCascades) const;
};


/**
 * Masks indicating for which views a primitive needs to have a certain operation on.
 * One entry per primitive in the scene.
 */
typedef TArray<uint8, SceneRenderingAllocator> FPrimitiveViewMasks;

class FShadowMapRenderTargetsRefCounted
{
public:
	// This structure gets included in FCachedShadowMapData, so avoid SceneRenderingAllocator use!
	TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<4>> ColorTargets;
	TRefCountPtr<IPooledRenderTarget> DepthTarget;

	bool IsValid() const
	{
		if (DepthTarget)
		{
			return true;
		}
		else 
		{
			return ColorTargets.Num() > 0;
		}
	}

	FIntPoint GetSize() const
	{
		const FPooledRenderTargetDesc* Desc = NULL;

		if (DepthTarget)
		{
			Desc = &DepthTarget->GetDesc();
		}
		else 
		{
			check(ColorTargets.Num() > 0);
			Desc = &ColorTargets[0]->GetDesc();
		}

		return Desc->Extent;
	}

	int64 ComputeMemorySize() const
	{
		int64 MemorySize = 0;

		for (int32 i = 0; i < ColorTargets.Num(); i++)
		{
			MemorySize += ColorTargets[i]->ComputeMemorySize();
		}

		if (DepthTarget)
		{
			MemorySize += DepthTarget->ComputeMemorySize();
		}

		return MemorySize;
	}

	void Release()
	{
		for (int32 i = 0; i < ColorTargets.Num(); i++)
		{
			ColorTargets[i] = NULL;
		}

		ColorTargets.Empty();

		DepthTarget = NULL;
	}
};

struct FSortedShadowMapAtlas
{
	FShadowMapRenderTargetsRefCounted RenderTargets;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> Shadows;
};

struct FSortedShadowMaps
{
	/** Visible shadows sorted by their shadow depth map render target. */
	TArray<FSortedShadowMapAtlas,SceneRenderingAllocator> ShadowMapAtlases;

	TArray<FSortedShadowMapAtlas,SceneRenderingAllocator> RSMAtlases;

	TArray<FSortedShadowMapAtlas,SceneRenderingAllocator> ShadowMapCubemaps;

	FSortedShadowMapAtlas PreshadowCache;

	TArray<FSortedShadowMapAtlas,SceneRenderingAllocator> TranslucencyShadowMapAtlases;

	void Release();

	int64 ComputeMemorySize() const
	{
		int64 MemorySize = 0;

		for (int i = 0; i < ShadowMapAtlases.Num(); i++)
		{
			MemorySize += ShadowMapAtlases[i].RenderTargets.ComputeMemorySize();
		}

		for (int i = 0; i < RSMAtlases.Num(); i++)
		{
			MemorySize += RSMAtlases[i].RenderTargets.ComputeMemorySize();
		}

		for (int i = 0; i < ShadowMapCubemaps.Num(); i++)
		{
			MemorySize += ShadowMapCubemaps[i].RenderTargets.ComputeMemorySize();
		}

		MemorySize += PreshadowCache.RenderTargets.ComputeMemorySize();

		for (int i = 0; i < TranslucencyShadowMapAtlases.Num(); i++)
		{
			MemorySize += TranslucencyShadowMapAtlases[i].RenderTargets.ComputeMemorySize();
		}

		return MemorySize;
	}
};

/**
 * Used as the scope for scene rendering functions.
 * It is initialized in the game thread by FSceneViewFamily::BeginRender, and then passed to the rendering thread.
 * The rendering thread calls Render(), and deletes the scene renderer when it returns.
 */
class FSceneRenderer
{
public:

	/** The scene being rendered. */
	FScene* Scene;

	/** The view family being rendered.  This references the Views array. */
	FSceneViewFamily ViewFamily;

	/** The views being rendered. */
	TArray<FViewInfo> Views;

	FMeshElementCollector MeshCollector;

	FMeshElementCollector RayTracingCollector;

	/** Information about the visible lights. */
	TArray<FVisibleLightInfo,SceneRenderingAllocator> VisibleLightInfos;

	/** Array of dispatched parallel shadow depth passes. */
	TArray<FParallelMeshDrawCommandPass*, SceneRenderingAllocator> DispatchedShadowDepthPasses;

	FSortedShadowMaps SortedShadowsForShadowDepthPass;

	/** If a freeze request has been made */
	bool bHasRequestedToggleFreeze;

	/** True if precomputed visibility was used when rendering the scene. */
	bool bUsedPrecomputedVisibility;

	/** Lights added if wholescenepointlight shadow would have been rendered (ignoring r.SupportPointLightWholeSceneShadows). Used for warning about unsupported features. */	
	TArray<FName, SceneRenderingAllocator> UsedWholeScenePointLightNames;

	/** Feature level being rendered */
	ERHIFeatureLevel::Type FeatureLevel;
	EShaderPlatform ShaderPlatform;
	
	/** 
	 * The width in pixels of the stereo view family being rendered. This may be different than FamilySizeX if
	 * we're using adaptive resolution stereo rendering. In that case, FamilySizeX represents the maximum size of 
	 * the family to ensure the backing render targets don't change between frames as the view size varies.
	 */
	uint32 InstancedStereoWidth;

	/** Only used if we are going to delay the deletion of the scene renderer until later. */
	FMemMark* RootMark;

public:

	FSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer);
	virtual ~FSceneRenderer();

	// FSceneRenderer interface

	virtual void Render(FRHICommandListImmediate& RHICmdList) = 0;
	virtual void RenderHitProxies(FRHICommandListImmediate& RHICmdList) {}
	virtual bool ShouldRenderVelocities() const { return false; }
	virtual bool SupportsMSAA() const { return true; }

	/** Creates a scene renderer based on the current feature level. */
	static FSceneRenderer* CreateSceneRenderer(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer);

	/** Setups FViewInfo::ViewRect according to ViewFamilly's ScreenPercentageInterface. */
	void PrepareViewRectsForRendering(FRHICommandListImmediate& RHICmdList);

#if WITH_MGPU
	/** Setups each FViewInfo::GPUMask. */
	void ComputeViewGPUMasks(FRHIGPUMask RenderTargetGPUMask);
#endif

	/** Update the rendertarget with each view results.*/
	void DoCrossGPUTransfers(FRDGBuilder& GraphBuilder, FRHIGPUMask RenderTargetGPUMask, FRDGTextureRef ViewFamilyTexture);

	bool DoOcclusionQueries(ERHIFeatureLevel::Type InFeatureLevel) const;

	void FenceOcclusionTests(FRDGBuilder& GraphBuilder);
	void WaitOcclusionTests(FRHICommandListImmediate& GraphBuilder);

	// fences to make sure the rhi thread has digested the occlusion query renders before we attempt to read them back async
	static FGraphEventRef OcclusionSubmittedFence[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames];

	bool ShouldDumpMeshDrawCommandInstancingStats() const { return bDumpMeshDrawCommandInstancingStats; }

	/** bound shader state for occlusion test prims */
	static FGlobalBoundShaderState OcclusionTestBoundShaderState;
	
	/**
	* Whether or not to composite editor objects onto the scene as a post processing step
	*
	* @param View The view to test against
	*
	* @return true if compositing is needed
	*/
	static bool ShouldCompositeEditorPrimitives(const FViewInfo& View);

	/** the last thing we do with a scene renderer, lots of cleanup related to the threading **/
	static void WaitForTasksClearSnapshotsAndDeleteSceneRenderer(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, bool bWaitForTasks = true);
	static void DelayWaitForTasksClearSnapshotsAndDeleteSceneRenderer(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer);
	
	/** Apply the ResolutionFraction on ViewSize, taking into account renderer's requirements. */
	static FIntPoint ApplyResolutionFraction(
		const FSceneViewFamily& ViewFamily, const FIntPoint& UnscaledViewSize, float ResolutionFraction);

	/** Quantize the ViewRect.Min according to various renderer's downscale requirements. */
	static FIntPoint QuantizeViewRectMin(const FIntPoint& ViewRectMin);

	/** Get the desired buffer size from the view family's ResolutionFraction upperbound.
	 * Can be called on game thread or render thread. 
	 */
	static FIntPoint GetDesiredInternalBufferSize(const FSceneViewFamily& ViewFamily);

	/** Exposes renderer's privilege to fork view family's screen percentage interface. */
	static ISceneViewFamilyScreenPercentage* ForkScreenPercentageInterface(
		const ISceneViewFamilyScreenPercentage* ScreenPercentageInterface, FSceneViewFamily& ForkedViewFamily)
	{
		return ScreenPercentageInterface->Fork_GameThread(ForkedViewFamily);
	}

	static int32 GetRefractionQuality(const FSceneViewFamily& ViewFamily);

	/** Create/Update the scene view irradiance buffer from CPU data or empty if generated fully on GPU. */
	void UpdateSkyIrradianceGpuBuffer(FRHICommandListImmediate& RHICmdList);

	/** Common function to render a sky using shared LUT resources from any view point (if not using the SkyView and AerialPerspective textures). */
	void RenderSkyAtmosphereInternal(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureShaderParameters& SceneTextures,
		FSkyAtmosphereRenderContext& SkyRenderContext);

	/** Common function to render a cloud layer using shared LUT resources. */
	void  RenderVolumetricCloudsInternal(FRDGBuilder& GraphBuilder, FCloudRenderContext& CloudRC);

	/** Sets the stereo-compatible RHI viewport. If the view doesn't requires stereo rendering, the standard viewport is set. */
	void SetStereoViewport(FRHICommandList& RHICmdList, const FViewInfo& View, float ViewportScale = 1.0f) const;

	/** Cache the FXSystem value from the Scene. Must be ran on the renderthread to ensure it is valid throughout rendering. */
	void InitFXSystem();

	/** Whether distance field global data structures should be prepared for features that use it. */
	bool ShouldPrepareForDistanceFieldShadows() const;
	bool ShouldPrepareForDistanceFieldAO() const;
	bool ShouldPrepareForDFInsetIndirectShadow() const;

	bool ShouldPrepareDistanceFieldScene() const;
	bool ShouldPrepareGlobalDistanceField() const;
	bool ShouldPrepareHeightFieldScene() const;

	void UpdateGlobalDistanceFieldObjectBuffers(FRHICommandListImmediate& RHICmdList);
	void UpdateGlobalHeightFieldObjectBuffers(FRHICommandListImmediate& RHICmdList);
	void AddOrRemoveSceneHeightFieldPrimitives(bool bSkipAdd = false);
	void PrepareDistanceFieldScene(FRHICommandListImmediate& RHICmdList, bool bSplitDispatch);


protected:

	/** Size of the family. */
	FIntPoint FamilySize;

#if WITH_MGPU
	FRHIGPUMask AllViewsGPUMask;
	FRHIGPUMask GetGPUMaskForShadow(FProjectedShadowInfo* ProjectedShadowInfo) const;
#endif

	/** The cached FXSystem which could be released while we are rendering. */
	class FFXSystemInterface* FXSystem = nullptr;

	bool bDumpMeshDrawCommandInstancingStats;

	// Shared functionality between all scene renderers

	void InitDynamicShadows(FRHICommandListImmediate& RHICmdList, FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer);

	void SetupMeshPass(FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewCommands& ViewCommands);

	void RenderShadowProjections(
		FRHICommandListImmediate& RHICmdList,
		const FLightSceneProxy* LightSceneProxy,
		const FHairStrandsRenderingData* HairDatas,
		TArrayView<const FProjectedShadowInfo* const> Shadows,
		bool bProjectingForForwardShading,
		bool bMobileModulatedProjections);

	/** Finds a matching cached preshadow, if one exists. */
	TRefCountPtr<FProjectedShadowInfo> GetCachedPreshadow(
		const FLightPrimitiveInteraction* InParentInteraction,
		const FProjectedShadowInitializer& Initializer,
		const FBoxSphereBounds& Bounds,
		uint32 InResolutionX);

	/** Creates a per object projected shadow for the given interaction. */
	void CreatePerObjectProjectedShadow(
		FRHICommandListImmediate& RHICmdList,
		FLightPrimitiveInteraction* Interaction,
		bool bCreateTranslucentObjectShadow,
		bool bCreateInsetObjectShadow,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& OutPreShadows);

	/** Creates shadows for the given interaction. */
	void SetupInteractionShadows(
		FRHICommandListImmediate& RHICmdList,
		FLightPrimitiveInteraction* Interaction,
		FVisibleLightInfo& VisibleLightInfo,
		bool bStaticSceneOnly,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& PreShadows);

	/** Generates FProjectedShadowInfos for all wholesceneshadows on the given light.*/
	void AddViewDependentWholeSceneShadowsForView(
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ShadowInfos, 
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ShadowInfosThatNeedCulling, 
		FVisibleLightInfo& VisibleLightInfo, 
		FLightSceneInfo& LightSceneInfo);

	void AllocateShadowDepthTargets(FRHICommandListImmediate& RHICmdList);
	
	void AllocatePerObjectShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& Shadows);

	void AllocateCachedSpotlightShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& CachedShadows);

	void AllocateCSMDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeSceneDirectionalShadows);

	void AllocateRSMDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& RSMShadows);

	void AllocateOnePassPointLightDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeScenePointShadows);

	void AllocateTranslucentShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& TranslucentShadows);

	void AllocateMobileCSMAndSpotLightShadowDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& MobileCSMAndSpotLightShadows);
	/**
	* Used by RenderLights to figure out if projected shadows need to be rendered to the attenuation buffer.
	* Or to render a given shadowdepth map for forward rendering.
	*
	* @param LightSceneInfo Represents the current light
	* @return true if anything needs to be rendered
	*/
	bool CheckForProjectedShadows(const FLightSceneInfo* LightSceneInfo) const;

	/** Gathers the list of primitives used to draw various shadow types */
	void GatherShadowPrimitives(
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& PreShadows,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		bool bReflectionCaptureScene);

	void RenderShadowDepthMaps(FRHICommandListImmediate& RHICmdList);
	void RenderShadowDepthMapAtlases(FRHICommandListImmediate& RHICmdList);

	/**
	* Creates a projected shadow for all primitives affected by a light.
	* @param LightSceneInfo - The light to create a shadow for.
	*/
	void CreateWholeSceneProjectedShadow(FLightSceneInfo* LightSceneInfo, uint32& NumPointShadowCachesUpdatedThisFrame, uint32& NumSpotShadowCachesUpdatedThisFrame);

	/** Updates the preshadow cache, allocating new preshadows that can fit and evicting old ones. */
	void UpdatePreshadowCache(FSceneRenderTargets& SceneContext);

	/** Gets a readable light name for use with a draw event. */
	static void GetLightNameForDrawEvent(const FLightSceneProxy* LightProxy, FString& LightNameWithLevel);

	/** Gathers simple lights from visible primtives in the passed in views. */
	static void GatherSimpleLights(const FSceneViewFamily& ViewFamily, const TArray<FViewInfo>& Views, FSimpleLightArray& SimpleLights);

	/** Calculates projected shadow visibility. */
	void InitProjectedShadowVisibility(FRHICommandListImmediate& RHICmdList);	

	/** Gathers dynamic mesh elements for all shadows. */
	void GatherShadowDynamicMeshElements(FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer);

	/** Performs once per frame setup prior to visibility determination. */
	void PreVisibilityFrameSetup(FRHICommandListImmediate& RHICmdList);

	/** Computes which primitives are visible and relevant for each view. */
	void ComputeViewVisibility(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewVisibleCommandsPerView& ViewCommandsPerView, 
		FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer);

	/** Performs once per frame setup after to visibility determination. */
	void PostVisibilityFrameSetup(FILCUpdatePrimTaskData& OutILCTaskData);

	void GatherDynamicMeshElements(
		TArray<FViewInfo>& InViews, 
		const FScene* InScene, 
		const FSceneViewFamily& InViewFamily, 
		FGlobalDynamicIndexBuffer& DynamicIndexBuffer,
		FGlobalDynamicVertexBuffer& DynamicVertexBuffer,
		FGlobalDynamicReadBuffer& DynamicReadBuffer,
		const FPrimitiveViewMasks& HasDynamicMeshElementsMasks,
		const FPrimitiveViewMasks& HasDynamicEditorMeshElementsMasks,
		FMeshElementCollector& Collector);

	/** Initialized the fog constants for each view. */
	void InitFogConstants();

	/** Returns whether there are translucent primitives to be rendered. */
	bool ShouldRenderTranslucency() const;
	bool ShouldRenderTranslucency(ETranslucencyPass::Type TranslucencyPass) const;

	/** TODO: REMOVE if no longer needed: Copies scene color to the viewport's render target after applying gamma correction. */
	void GammaCorrectToViewportRenderTarget(FRHICommandList& RHICmdList, const FViewInfo* View, float OverrideGamma);

	/** Updates state for the end of the frame. */
	void RenderFinish(FRDGBuilder& GraphBuilder, FRDGTextureRef ViewFamilyTexture);

	void RenderCustomDepthPassAtLocation(FRDGBuilder& GraphBuilder, int32 Location, const FSceneTextureShaderParameters& SceneTextures);
	void RenderCustomDepthPass(FRDGBuilder& GraphBuilder, const FSceneTextureShaderParameters& SceneTextures);

	void OnStartRender(FRHICommandListImmediate& RHICmdList);

	void UpdatePrimitiveIndirectLightingCacheBuffers();

	void RenderPlanarReflection(class FPlanarReflectionSceneProxy* ReflectionSceneProxy);

	/** Initialise sky atmosphere resources.*/
	void InitSkyAtmosphereForViews(FRHICommandListImmediate& RHICmdList);
	
	/** Render the sky atmosphere look up table needed for this frame.*/
	void RenderSkyAtmosphereLookUpTables(FRDGBuilder& GraphBuilder);

	/** Render the sky atmosphere over the scene.*/
	void RenderSkyAtmosphere(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture);

	/** Initialise volumetric cloud resources.*/
	void InitVolumetricCloudsForViews(FRDGBuilder& GraphBuilder, bool bShouldRenderVolumetricCloud);

	/** Render volumetric cloud. */
	bool RenderVolumetricCloud(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureShaderParameters& SceneTextures,
		bool bSkipVolumetricRenderTarget,
		bool bSkipPerPixelTracing,
		FRDGTextureMSAA SceneColorTexture,
		FRDGTextureMSAA SceneDepthTexture,
		bool bAsyncCompute);

	/** Render notification to artist when a sky material is used but it might comtains the camera (and then the sky/background would look black).*/
	void RenderSkyAtmosphereEditorNotifications(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture);

	/** We should render on screen notification only if any of the scene contains a mesh using a sky material.*/
	bool ShouldRenderSkyAtmosphereEditorNotifications();

	/** Initialise volumetric render target.*/
	void InitVolumetricRenderTargetForViews(FRDGBuilder& GraphBuilder);
	/** Process the volumetric render target, generating the high resolution version.*/
	void ReconstructVolumetricRenderTarget(FRDGBuilder& GraphBuilder, bool bWaitFinishFence);
	/** Compose the volumetric render target over the scene.*/
	void ComposeVolumetricRenderTargetOverScene(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, bool bShouldRenderSingleLayerWater, const FSceneWithoutWaterTextures& WaterPassData, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesWithDepth);
	/** Compose the volumetric render target over the scene from a view under water, in the water render target.*/
	void ComposeVolumetricRenderTargetOverSceneUnderWater(FRDGBuilder& GraphBuilder, const FSceneWithoutWaterTextures& WaterPassData, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesWithDepth);
	/** Simply overwrite scene color for debug visualization. */
	void ComposeVolumetricRenderTargetOverSceneForVisualization(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesWithDepth);

	void ResolveSceneColor(FRHICommandListImmediate& RHICmdList);
	void ResolveSceneDepth(FRHICommandListImmediate& RHICmdList);

	/**
	 * Rounds up lights and sorts them according to what type of renderer supports them. The result is stored in OutSortedLights 
	 * NOTE: Also extracts the SimpleLights AND adds them to the sorted range (first sub-range). 
	 */
	void GatherAndSortLights(FSortedLightSetSceneInfo& OutSortedLights);
	
	/** 
	 * Culls local lights and reflection probes to a grid in frustum space, builds one light list and grid per view in the current Views.  
	 * Needed for forward shading or translucency using the Surface lighting mode, and clustered deferred shading. 
	 */
	void ComputeLightGrid(FRDGBuilder& GraphBuilder, bool bCullLightsToGrid, FSortedLightSetSceneInfo &SortedLightSet);

	/**
	* Used by RenderLights to figure out if light functions need to be rendered to the attenuation buffer.
	*
	* @param LightSceneInfo Represents the current light
	* @return true if anything got rendered
	*/
	bool CheckForLightFunction(const FLightSceneInfo* LightSceneInfo) const;

	void SetupSceneReflectionCaptureBuffer(FRHICommandListImmediate& RHICmdList);

	void RenderVelocities(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef& VelocityTexture,
		const FSceneTextureShaderParameters& SceneTextures,
		EVelocityPass VelocityPass,
		bool bForceVelocity);

private:
	void ComputeFamilySize();

#if !UE_BUILD_SHIPPING
	/** Dump all UPrimitiveComponents in the Scene to a CSV file */
	void DumpPrimitives(const FViewCommands& ViewCommands);
#endif
};

struct FForwardScreenSpaceShadowMaskTextureMobileOutputs
{
	TRefCountPtr<IPooledRenderTarget> ScreenSpaceShadowMaskTextureMobile;

	bool IsValid()
	{
		return ScreenSpaceShadowMaskTextureMobile.IsValid();
	}

	void Release()
	{
		ScreenSpaceShadowMaskTextureMobile.SafeRelease();
	}
};

extern FForwardScreenSpaceShadowMaskTextureMobileOutputs GScreenSpaceShadowMaskTextureMobileOutputs;

/**
 * Renderer that implements simple forward shading and associated features.
 */
class FMobileSceneRenderer : public FSceneRenderer
{
public:

	FMobileSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer);

	// FSceneRenderer interface

	virtual void Render(FRHICommandListImmediate& RHICmdList) override;

	virtual void RenderHitProxies(FRHICommandListImmediate& RHICmdList) override;

	virtual bool ShouldRenderVelocities() const override;

	virtual bool SupportsMSAA() const override;

	bool RenderInverseOpacity(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

protected:
	/** Finds the visible dynamic shadows for each view. */
	void InitDynamicShadows(FRHICommandListImmediate& RHICmdList);

	void PrepareViewVisibilityLists();

	/** Build visibility lists on CSM receivers and non-csm receivers. */
	void BuildCSMVisibilityState(FLightSceneInfo* LightSceneInfo);

	void InitViews(FRHICommandListImmediate& RHICmdList);

	void RenderPrePass(FRHICommandListImmediate& RHICmdList);

	/** Renders the opaque base pass for mobile. */
	void RenderMobileBasePass(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews);

	void RenderMobileEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, const FMeshPassProcessorRenderState& DrawRenderState);

	/** Renders the debug view pass for mobile. */
	void RenderMobileDebugView(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews);

	/** Render modulated shadow projections in to the scene, loops over any unrendered shadows until all are processed.*/
	void RenderModulatedShadowProjections(FRHICommandListImmediate& RHICmdList);

	/** Resolves scene depth in case hardware does not support reading depth in the shader */
	void ConditionalResolveSceneDepth(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Issues occlusion queries */
	void RenderOcclusion(FRHICommandListImmediate& RHICmdList);

	bool ShouldRenderHZB();

	/** Generate HZB */
	void RenderHZB(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneDepthZ);
	void RenderHZB(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture);
	
	/** Computes how many queries will be issued this frame */
	int32 ComputeNumOcclusionQueriesToBatch() const;

	/** Whether platform requires multiple render-passes for SceneColor rendering */
	bool RequiresMultiPass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View) const;

	/** Renders decals. */
	void RenderDecals(FRHICommandListImmediate& RHICmdList);

	/** Renders the base pass for translucency. */
	void RenderTranslucency(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews);

	/** Creates uniform buffers with the mobile directional light parameters, for each lighting channel. Called by InitViews */
	void CreateDirectionalLightUniformBuffers(FViewInfo& View);

	/** On chip pre-tonemap before scene color MSAA resolve (iOS only) */
	void PreTonemapMSAA(FRHICommandListImmediate& RHICmdList);

	void SortMobileBasePassAfterShadowInit(FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewVisibleCommandsPerView& ViewCommandsPerView);
	void SetupMobileBasePassAfterShadowInit(FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewVisibleCommandsPerView& ViewCommandsPerView);

	void UpdateOpaqueBasePassUniformBuffer(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
	void UpdateTranslucentBasePassUniformBuffer(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
	void UpdateDirectionalLightUniformBuffers(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
	void UpdateSkyReflectionUniformBuffer();

	void BeginLateLatching(FRHICommandListImmediate& RHICmdList);
	void EndLateLatching(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	FRHITexture* RenderForward(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> ViewList);
	FRHITexture* RenderDeferred(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> ViewList, const FSortedLightSetSceneInfo& SortedLightSet);
	
	void InitAmbientOcclusionOutputs(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneDepthZ);
	void RenderAmbientOcclusion(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneDepthZ);
	void RenderAmbientOcclusion(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture, FRDGTextureRef AmbientOcclusionTexture);
	void ReleaseAmbientOcclusionOutputs();

	void InitSDFShadowingOutputs(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneDepthZ);
	void RenderSDFShadowing(FRHICommandListImmediate& RHICmdList);
	void ReleaseSDFShadowingOutputs();

	void InitPixelProjectedReflectionOutputs(FRHICommandListImmediate& RHICmdList, const FIntPoint& BufferSize);
	void RenderPixelProjectedReflection(FRHICommandListImmediate& RHICmdList, const FSceneRenderTargets& SceneContext, const FPlanarReflectionSceneProxy* PlanarReflectionSceneProxy);
	void RenderPixelProjectedReflection(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthTexture, FRDGTextureRef PixelProjectedReflectionTexture, const FPlanarReflectionSceneProxy* PlanarReflectionSceneProxy);
	void ReleasePixelProjectedReflectionOutputs();

	/** Before SetupMobileBasePassAfterShadowInit, we need to update the uniform buffer and shadow info for all movable point lights.*/
	void UpdateMovablePointLightUniformBufferAndShadowInfo();
private:
	const bool bGammaSpace;
	const bool bDeferredShading;
	const bool bUseVirtualTexturing;
	int32 NumMSAASamples;
	bool bRenderToSceneColor;
	bool bRequiresMultiPass;
	bool bKeepDepthContent;
	bool bSubmitOffscreenRendering;
	bool bModulatedShadowsInUse;
	bool bShouldRenderCustomDepth;
	bool bRequiresPixelProjectedPlanarRelfectionPass;
	bool bRequiresAmbientOcclusionPass;
	bool bRequiresDistanceField;
	bool bRequiresDistanceFieldShadowingPass;
	bool bIsFullPrepassEnabled;
	bool bShouldRenderVelocities;
	bool bShouldRenderHZB;
	bool bShouldRenderDepthToTranslucency;
	static FGlobalDynamicIndexBuffer DynamicIndexBuffer;
	static FGlobalDynamicVertexBuffer DynamicVertexBuffer;
	static TGlobalResource<FGlobalDynamicReadBuffer> DynamicReadBuffer;
};

// The noise textures need to be set in Slate too.
RENDERER_API void UpdateNoiseTextureParameters(FViewUniformShaderParameters& ViewUniformShaderParameters);

inline FRHITexture* OrWhite2DIfNull(FRHITexture* Tex)
{
	FRHITexture* Result = Tex ? Tex : GWhiteTexture->TextureRHI.GetReference();
	check(Result);
	return Result;
}

inline FRHITexture* OrBlack2DIfNull(FRHITexture* Tex)
{
	FRHITexture* Result = Tex ? Tex : GBlackTexture->TextureRHI.GetReference();
	check(Result);
	return Result;
}

inline FRHITexture* OrBlack3DIfNull(FRHITexture* Tex)
{
	// we fall back to 2D which are unbound mobile parameters
	return OrBlack2DIfNull(Tex ? Tex : GBlackVolumeTexture->TextureRHI.GetReference());
}

inline FRHITexture* OrBlack3DAlpha1IfNull(FRHITexture* Tex)
{
	// we fall back to 2D which are unbound mobile parameters
	return OrBlack2DIfNull(Tex ? Tex : GBlackAlpha1VolumeTexture->TextureRHI.GetReference());
}

inline FRHITexture* OrBlack3DUintIfNull(FRHITexture* Tex)
{
	// we fall back to 2D which are unbound mobile parameters
	return OrBlack2DIfNull(Tex ? Tex : GBlackUintVolumeTexture->TextureRHI.GetReference());
}

inline void SetBlack2DIfNull(FRHITexture*& Tex)
{
	if (!Tex)
	{
		Tex = GBlackTexture->TextureRHI.GetReference();
		check(Tex);
	}
}

inline void SetBlack3DIfNull(FRHITexture*& Tex)
{
	if (!Tex)
	{
		Tex = GBlackVolumeTexture->TextureRHI.GetReference();
		// we fall back to 2D which are unbound mobile parameters
		SetBlack2DIfNull(Tex);
	}
}

inline void SetBlackAlpha13DIfNull(FRHITexture*& Tex)
{
	if (!Tex)
	{
		Tex = GBlackAlpha1VolumeTexture->TextureRHI.GetReference();
		// we fall back to 2D which are unbound mobile parameters
		SetBlack2DIfNull(Tex); // This is actually a rgb=0, a=1 texture
	}
}

extern TAutoConsoleVariable<int32> CVarTransientResourceAliasing_Buffers;

FORCEINLINE bool IsTransientResourceBufferAliasingEnabled()
{
	return (GSupportsTransientResourceAliasing && CVarTransientResourceAliasing_Buffers.GetValueOnRenderThread() != 0);
}

struct FFastVramConfig
{
	FFastVramConfig();
	void Update();
	void OnCVarUpdated();
	void OnSceneRenderTargetsAllocated();

	ETextureCreateFlags GBufferA;
	ETextureCreateFlags GBufferB;
	ETextureCreateFlags GBufferC;
	ETextureCreateFlags GBufferD;
	ETextureCreateFlags GBufferE;
	ETextureCreateFlags GBufferF;
	ETextureCreateFlags GBufferVelocity;
	ETextureCreateFlags HZB;
	ETextureCreateFlags SceneDepth;
	ETextureCreateFlags SceneColor;
	ETextureCreateFlags LPV;
	ETextureCreateFlags BokehDOF;
	ETextureCreateFlags CircleDOF;
	ETextureCreateFlags CombineLUTs;
	ETextureCreateFlags Downsample;
	ETextureCreateFlags EyeAdaptation;
	ETextureCreateFlags Histogram;
	ETextureCreateFlags HistogramReduce;
	ETextureCreateFlags VelocityFlat;
	ETextureCreateFlags VelocityMax;
	ETextureCreateFlags MotionBlur;
	ETextureCreateFlags Tonemap;
	ETextureCreateFlags Upscale;
	ETextureCreateFlags DistanceFieldNormal;
	ETextureCreateFlags DistanceFieldAOHistory;
	ETextureCreateFlags DistanceFieldAOBentNormal;
	ETextureCreateFlags DistanceFieldAODownsampledBentNormal;
	ETextureCreateFlags DistanceFieldShadows;
	ETextureCreateFlags DistanceFieldIrradiance;
	ETextureCreateFlags DistanceFieldAOConfidence;
	ETextureCreateFlags Distortion;
	ETextureCreateFlags ScreenSpaceShadowMask;
	ETextureCreateFlags VolumetricFog;
	ETextureCreateFlags SeparateTranslucency;
	ETextureCreateFlags SeparateTranslucencyModulate;
	ETextureCreateFlags LightAccumulation;
	ETextureCreateFlags LightAttenuation;
	ETextureCreateFlags ScreenSpaceAO;
	ETextureCreateFlags SSR;
	ETextureCreateFlags DBufferA;
	ETextureCreateFlags DBufferB;
	ETextureCreateFlags DBufferC;
	ETextureCreateFlags DBufferMask;
	ETextureCreateFlags DOFSetup;
	ETextureCreateFlags DOFReduce;
	ETextureCreateFlags DOFPostfilter;
	ETextureCreateFlags PostProcessMaterial;

	ETextureCreateFlags CustomDepth;
	ETextureCreateFlags ShadowPointLight;
	ETextureCreateFlags ShadowPerObject;
	ETextureCreateFlags ShadowCSM;

	// Buffers
	uint32 DistanceFieldCulledObjectBuffers;
	uint32 DistanceFieldTileIntersectionResources;
	uint32 DistanceFieldAOScreenGridResources;
	uint32 ForwardLightingCullingResources;
	uint32 GlobalDistanceFieldCullGridBuffers;
	bool bDirty;

private:
	bool UpdateTextureFlagFromCVar(TAutoConsoleVariable<int32>& CVar, ETextureCreateFlags& InOutValue);
	bool UpdateBufferFlagFromCVar(TAutoConsoleVariable<int32>& CVar, uint32& InOutValue);
};

extern FFastVramConfig GFastVRamConfig;

extern bool UseCachedMeshDrawCommands();
extern bool UseCachedMeshDrawCommands_AnyThread();
extern bool IsDynamicInstancingEnabled(ERHIFeatureLevel::Type FeatureLevel);

enum class EGPUSkinCacheTransition
{
	FrameSetup,
	Renderer,
};

/* Run GPU skin cache resource transitions */
void RunGPUSkinCacheTransition(class FRHICommandList& RHICmdList, class FScene* Scene, EGPUSkinCacheTransition Type);
/** Resolves the view rect of scene color or depth using either a custom resolve or hardware resolve. */
void AddResolveSceneColorPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureMSAA SceneColor);
void AddResolveSceneDepthPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureMSAA SceneDepth);

/** Resolves all views for scene color / depth. */
void AddResolveSceneColorPass(FRDGBuilder& GraphBuilder, TArrayView<const FViewInfo> Views, FRDGTextureMSAA SceneColor);
void AddResolveSceneDepthPass(FRDGBuilder& GraphBuilder, TArrayView<const FViewInfo> Views, FRDGTextureMSAA SceneDepth);