// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ScenePrivate.h: Private scene manager definitions.
=============================================================================*/

#pragma once

// Dependencies.

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Math/RandomStream.h"
#include "Engine/EngineTypes.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "SceneTypes.h"
#include "UniformBuffer.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "RendererInterface.h"
#include "SceneUtils.h"
#include "SceneManagement.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureLayout3d.h"
#include "ScenePrivateBase.h"
#include "RenderTargetPool.h"
#include "SceneCore.h"
#include "Containers/AllocatorFixedSizeFreeList.h"
#include "PrimitiveSceneInfo.h"
#include "LightSceneInfo.h"
#include "DepthRendering.h"
#include "SceneHitProxyRendering.h"
#include "ShadowRendering.h"
#include "TextureLayout.h"
#include "SceneRendering.h"
#include "LightMapRendering.h"
#include "VelocityRendering.h"
#include "BasePassRendering.h"
#include "MobileBasePassRendering.h"
#include "VolumeRendering.h"
#include "SceneSoftwareOcclusion.h"
#include "CommonRenderResources.h"
#include "VisualizeTexture.h"
#include "UnifiedBuffer.h"
#include "LightMapDensityRendering.h"
#include "VolumetricFogShared.h"
#include "DebugViewModeRendering.h"
#include "PrecomputedVolumetricLightmap.h"
#include "RayTracing/RaytracingOptions.h"
#if RHI_RAYTRACING
#include "RayTracing/RayTracingIESLightProfiles.h"
#include "Halton.h"
#endif
#include "VolumetricRenderTargetViewStateData.h"

/** Factor by which to grow occlusion tests **/
#define OCCLUSION_SLOP (1.0f)

/** Extern GPU stats (used in multiple modules) **/
DECLARE_GPU_STAT_NAMED_EXTERN(ShadowProjection, TEXT("Shadow Projection"));

class AWorldSettings;
class FAtmosphericFogSceneInfo;
class FLightPropagationVolume;
class FMaterialParameterCollectionInstanceResource;
class FPrecomputedLightVolume;
class FScene;
class UAtmosphericFogComponent;
class UDecalComponent;
class UExponentialHeightFogComponent;
class ULightComponent;
class UPlanarReflectionComponent;
class UPrimitiveComponent;
class UReflectionCaptureComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextureCube;
class UWindDirectionalSourceComponent;
class FRHIGPUBufferReadback;
class FRHIGPUTextureReadback;
class FRuntimeVirtualTextureSceneProxy;

/** Holds information about a single primitive's occlusion. */
class FPrimitiveOcclusionHistory
{
public:
	/** The primitive the occlusion information is about. */
	FPrimitiveComponentId PrimitiveId;

	/** The occlusion query which contains the primitive's pending occlusion results. */
	FRHIRenderQuery* PendingOcclusionQuery[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames];
	uint32 PendingOcclusionQueryFrames[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames]; 

	uint32 LastTestFrameNumber;
	uint32 LastConsideredFrameNumber;
	uint32 HZBTestIndex;

	/** The last time the primitive was visible. */
	float LastProvenVisibleTime;

	/** The last time the primitive was in the view frustum. */
	float LastConsideredTime;

	/** 
	 *	The pixels that were rendered the last time the primitive was drawn.
	 *	It is the ratio of pixels unoccluded to the resolution of the scene.
	 */
	float LastPixelsPercentage;

	/**
	* For things that have subqueries (folaige), this is the non-zero
	*/
	int32 CustomIndex;

	/** When things first become eligible for occlusion, then might be sweeping into the frustum, we are going to leave them at visible for a few frames, then start real queries.  */
	uint8 BecameEligibleForQueryCooldown : 6;

	uint8 WasOccludedLastFrame : 1;
	uint8 OcclusionStateWasDefiniteLastFrame : 1;

	/** whether or not this primitive was grouped the last time it was queried */
	bool bGroupedQuery[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames];

private:
	/**
	 *	Whether or not we need to linearly search the history for a past entry. Scanning may be necessary if for every frame there
	 *	is a hole in PendingOcclusionQueryFrames in the same spot (ex. if for every frame PendingOcclusionQueryFrames[1] is null).
	 *	This could lead to overdraw for the frames that attempt to read these holes by getting back nothing every time.
	 *	This can occur when round robin occlusion queries are turned on while NumBufferedFrames is even.
	 */
	bool bNeedsScanOnRead;

	/**
	 *	Scan for the oldest non-stale (<= LagTolerance frames old) in the occlusion history by examining their corresponding frame numbers.
	 *	Conditions where this is needed to get a query for read-back are described for bNeedsScanOnRead.
	 *	Returns -1 if no such query exists in the occlusion history.
	 */
	inline int32 ScanOldestNonStaleQueryIndex(uint32 FrameNumber, int32 NumBufferedFrames, int32 LagTolerance) const
	{
		uint32 OldestFrame = UINT32_MAX;
		int32 OldestQueryIndex = -1;
		for (int Index = 0; Index < NumBufferedFrames; ++Index)
		{
			const uint32 ThisFrameNumber = PendingOcclusionQueryFrames[Index];
			const int32 LaggedFrames = FrameNumber - ThisFrameNumber;
			// Queries older than LagTolerance are invalid. They may have already been reused and will give incorrect results if read
			if (PendingOcclusionQuery[Index] && LaggedFrames <= LagTolerance && ThisFrameNumber < OldestFrame)
			{
				OldestFrame = ThisFrameNumber;
				OldestQueryIndex = Index;
			}
		}
		return OldestQueryIndex;
	}

public:
	/** Initialization constructor. */
	inline FPrimitiveOcclusionHistory(FPrimitiveComponentId InPrimitiveId, int32 SubQuery)
		: PrimitiveId(InPrimitiveId)
		, LastTestFrameNumber(~0u)
		, LastConsideredFrameNumber(~0u)
		, HZBTestIndex(0)
		, LastProvenVisibleTime(0.0f)
		, LastConsideredTime(0.0f)
		, LastPixelsPercentage(0.0f)
		, CustomIndex(SubQuery)
		, BecameEligibleForQueryCooldown(0)
		, WasOccludedLastFrame(false)
		, OcclusionStateWasDefiniteLastFrame(false)
		, bNeedsScanOnRead(false)
	{
		for (int32 Index = 0; Index < FOcclusionQueryHelpers::MaxBufferedOcclusionFrames; Index++)
		{
			PendingOcclusionQuery[Index] = nullptr;
			PendingOcclusionQueryFrames[Index] = 0;
			bGroupedQuery[Index] = false;
		}
	}

	inline FPrimitiveOcclusionHistory()
		: LastTestFrameNumber(~0u)
		, LastConsideredFrameNumber(~0u)
		, HZBTestIndex(0)
		, LastProvenVisibleTime(0.0f)
		, LastConsideredTime(0.0f)
		, LastPixelsPercentage(0.0f)
		, CustomIndex(0)
		, BecameEligibleForQueryCooldown(0)
		, WasOccludedLastFrame(false)
		, OcclusionStateWasDefiniteLastFrame(false)
		, bNeedsScanOnRead(false)
	{
		for (int32 Index = 0; Index < FOcclusionQueryHelpers::MaxBufferedOcclusionFrames; Index++)
		{
			PendingOcclusionQuery[Index] = nullptr;
			PendingOcclusionQueryFrames[Index] = 0;
			bGroupedQuery[Index] = false;
		}
	}

	inline void ReleaseStaleQueries(uint32 FrameNumber, int32 NumBufferedFrames)
	{
		// No need to release. FFrameBasedOcclusionQueryPool automatically reuses stale queries
	}

	inline void ReleaseQuery(uint32 FrameNumber, int32 NumBufferedFrames)
	{
		// No need to release. FFrameBasedOcclusionQueryPool automatically reuses stale queries
	}

	inline FRHIRenderQuery* GetQueryForEviction(uint32 FrameNumber, int32 NumBufferedFrames) const
	{
		// No need to release. FFrameBasedOcclusionQueryPool automatically reuses stale queries
		return nullptr;
	}


	inline FRHIRenderQuery* GetQueryForReading(uint32 FrameNumber, int32 NumBufferedFrames, int32 LagTolerance, bool& bOutGrouped) const
	{
		const int32 OldestQueryIndex = bNeedsScanOnRead ? ScanOldestNonStaleQueryIndex(FrameNumber, NumBufferedFrames, LagTolerance)
														: FOcclusionQueryHelpers::GetQueryLookupIndex(FrameNumber, NumBufferedFrames);
		const int32 LaggedFrames = FrameNumber - PendingOcclusionQueryFrames[OldestQueryIndex];
		// Nenever read from queries are older than LagTolerance. They may have already been reused and will give incorrect results
		if (OldestQueryIndex == -1 || !PendingOcclusionQuery[OldestQueryIndex] || LaggedFrames > LagTolerance)
		{
			bOutGrouped = false;
			return nullptr;
		}
		bOutGrouped = bGroupedQuery[OldestQueryIndex];
		return PendingOcclusionQuery[OldestQueryIndex];
	}

	inline void SetCurrentQuery(uint32 FrameNumber, FRHIRenderQuery* NewQuery, int32 NumBufferedFrames, bool bGrouped, bool bNeedsScan)
	{
		// Get the current occlusion query
		const uint32 QueryIndex = FOcclusionQueryHelpers::GetQueryIssueIndex(FrameNumber, NumBufferedFrames);
		PendingOcclusionQuery[QueryIndex] = NewQuery;
		PendingOcclusionQueryFrames[QueryIndex] = FrameNumber;
		bGroupedQuery[QueryIndex] = bGrouped;

		bNeedsScanOnRead = bNeedsScan;
	}

	inline uint32 LastQuerySubmitFrame() const
	{
		uint32 Result = 0;

		for (int32 Index = 0; Index < FOcclusionQueryHelpers::MaxBufferedOcclusionFrames; Index++)
		{
			if (!bGroupedQuery[Index])
			{
				Result = FMath::Max(Result, PendingOcclusionQueryFrames[Index]);
			}
		}

		return Result;
	}
};

struct FPrimitiveOcclusionHistoryKey
{
	FPrimitiveComponentId PrimitiveId;
	int32 CustomIndex;

	FPrimitiveOcclusionHistoryKey(const FPrimitiveOcclusionHistory& Element)
		: PrimitiveId(Element.PrimitiveId)
		, CustomIndex(Element.CustomIndex)
	{
	}
	FPrimitiveOcclusionHistoryKey(FPrimitiveComponentId InPrimitiveId, int32 InCustomIndex)
		: PrimitiveId(InPrimitiveId)
		, CustomIndex(InCustomIndex)
	{
	}
};

/** Defines how the hash set indexes the FPrimitiveOcclusionHistory objects. */
struct FPrimitiveOcclusionHistoryKeyFuncs : BaseKeyFuncs<FPrimitiveOcclusionHistory,FPrimitiveOcclusionHistoryKey>
{
	typedef FPrimitiveOcclusionHistoryKey KeyInitType;

	static KeyInitType GetSetKey(const FPrimitiveOcclusionHistory& Element)
	{
		return FPrimitiveOcclusionHistoryKey(Element);
	}

	static bool Matches(KeyInitType A,KeyInitType B)
	{
		return A.PrimitiveId == B.PrimitiveId && A.CustomIndex == B.CustomIndex;
	}

	static uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(Key.PrimitiveId.PrimIDValue) ^ (GetTypeHash(Key.CustomIndex) >> 20);
	}
};


class FIndividualOcclusionHistory
{
	FRHIPooledRenderQuery PendingOcclusionQuery[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames];
	uint32 PendingOcclusionQueryFrames[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames]; // not intialized...this is ok

public:

	inline void ReleaseStaleQueries(uint32 FrameNumber, int32 NumBufferedFrames)
	{
		for (uint32 DeltaFrame = NumBufferedFrames; DeltaFrame > 0; DeltaFrame--)
		{
			if (FrameNumber >= (DeltaFrame - 1))
			{
				uint32 TestFrame = FrameNumber - (DeltaFrame - 1);
				const uint32 QueryIndex = FOcclusionQueryHelpers::GetQueryLookupIndex(TestFrame, NumBufferedFrames);
				if (PendingOcclusionQueryFrames[QueryIndex] != TestFrame)
				{
					PendingOcclusionQuery[QueryIndex].ReleaseQuery();
				}
			}
		}
	}
	inline void ReleaseQuery(uint32 FrameNumber, int32 NumBufferedFrames)
	{
		const uint32 QueryIndex = FOcclusionQueryHelpers::GetQueryLookupIndex(FrameNumber, NumBufferedFrames);
		PendingOcclusionQuery[QueryIndex].ReleaseQuery();
	}

	inline FRHIRenderQuery* GetPastQuery(uint32 FrameNumber, int32 NumBufferedFrames)
	{
		// Get the oldest occlusion query
		const uint32 QueryIndex = FOcclusionQueryHelpers::GetQueryLookupIndex(FrameNumber, NumBufferedFrames);
		if (PendingOcclusionQuery[QueryIndex].GetQuery() && PendingOcclusionQueryFrames[QueryIndex] == FrameNumber - uint32(NumBufferedFrames))
		{
			return PendingOcclusionQuery[QueryIndex].GetQuery();
		}
		return nullptr;
	}

	inline void SetCurrentQuery(uint32 FrameNumber, FRHIPooledRenderQuery&& NewQuery, int32 NumBufferedFrames)
	{
		// Get the current occlusion query
		const uint32 QueryIndex = FOcclusionQueryHelpers::GetQueryIssueIndex(FrameNumber, NumBufferedFrames);
		PendingOcclusionQuery[QueryIndex] = MoveTemp(NewQuery);
		PendingOcclusionQueryFrames[QueryIndex] = FrameNumber;
	}
};

/**
 * Distance cull fading uniform buffer containing fully faded in.
 */
class FGlobalDistanceCullFadeUniformBuffer : public TUniformBuffer< FDistanceCullFadeUniformShaderParameters >
{
public:
	/** Default constructor. */
	FGlobalDistanceCullFadeUniformBuffer()
	{
		FDistanceCullFadeUniformShaderParameters Parameters;
		Parameters.FadeTimeScaleBias.X = 0.0f;
		Parameters.FadeTimeScaleBias.Y = 1.0f;
		SetContents(Parameters);
	}
};

/** Global primitive uniform buffer resource containing faded in */
extern TGlobalResource< FGlobalDistanceCullFadeUniformBuffer > GDistanceCullFadedInUniformBuffer;

/**
 * Dither uniform buffer containing fully faded in.
 */
class FGlobalDitherUniformBuffer : public TUniformBuffer< FDitherUniformShaderParameters >
{
public:
	/** Default constructor. */
	FGlobalDitherUniformBuffer()
	{
		FDitherUniformShaderParameters Parameters;
		Parameters.LODFactor = 0.0f;
		SetContents(Parameters);
	}
};

/** Global primitive uniform buffer resource containing faded in */
extern TGlobalResource< FGlobalDitherUniformBuffer > GDitherFadedInUniformBuffer;

/**
 * Stores fading state for a single primitive in a single view
 */
class FPrimitiveFadingState
{
public:
	FPrimitiveFadingState()
		: FadeTimeScaleBias(ForceInitToZero)
		, FrameNumber(0)
		, EndTime(0.0f)
		, bIsVisible(false)
		, bValid(false)
	{
	}

	/** Scale and bias to use on time to calculate fade opacity */
	FVector2D FadeTimeScaleBias;

	/** The uniform buffer for the fade parameters */
	FDistanceCullFadeUniformBufferRef UniformBuffer;

	/** Frame number when last updated */
	uint32 FrameNumber;

	/** Time when fade will be finished. */
	float EndTime;

	/** Currently visible? */
	bool bIsVisible;

	/** Valid? */
	bool bValid;
};

enum FGlobalDFCacheType
{
	GDF_MostlyStatic,
	GDF_Full,
	GDF_Num
};

class FGlobalDistanceFieldCacheTypeState
{
public:

	FGlobalDistanceFieldCacheTypeState()
	{
	}

	TArray<FVector4> PrimitiveModifiedBounds;
	TRefCountPtr<IPooledRenderTarget> VolumeTexture;
};

class FGlobalDistanceFieldClipmapState
{
public:

	FGlobalDistanceFieldClipmapState()
	{
		FullUpdateOrigin = FIntVector::ZeroValue;
		LastPartialUpdateOrigin = FIntVector::ZeroValue;
		CachedMaxOcclusionDistance = 0;
		CachedGlobalDistanceFieldViewDistance = 0;
		CacheMostlyStaticSeparately = 1;
		LastUsedSceneDataForFullUpdate = nullptr;
	}

	FIntVector FullUpdateOrigin;
	FIntVector LastPartialUpdateOrigin;
	float CachedMaxOcclusionDistance;
	float CachedGlobalDistanceFieldViewDistance;
	uint32 CacheMostlyStaticSeparately;

	FGlobalDistanceFieldCacheTypeState Cache[GDF_Num];

	// Used to perform a full update of the clip map when the scene data changes
	const class FDistanceFieldSceneData* LastUsedSceneDataForFullUpdate;
};

/** Maps a single primitive to it's per-view fading state data */
typedef TMap<FPrimitiveComponentId, FPrimitiveFadingState> FPrimitiveFadingStateMap;

class FOcclusionRandomStream
{
	enum {NumSamples = 3571};
public:

	/** Default constructor - should set seed prior to use. */
	FOcclusionRandomStream()
		: CurrentSample(0)
	{
		FRandomStream RandomStream(0x83246);
		for (int32 Index = 0; Index < NumSamples; Index++)
		{
			Samples[Index] = RandomStream.GetFraction();
		}
		Samples[0] = 0.0f; // we want to make sure we have at least a few zeros
		Samples[NumSamples/3] = 0.0f; // we want to make sure we have at least a few zeros
		Samples[(NumSamples*2)/3] = 0.0f; // we want to make sure we have at least a few zeros
	}

	/** @return A random number between 0 and 1. */
	inline float GetFraction()
	{
		if (CurrentSample >= NumSamples)
		{
			CurrentSample = 0;
		}
		return Samples[CurrentSample++];
	}
private:

	/** Index of the last sample we produced **/
	uint32 CurrentSample;
	/** A list of float random samples **/
	float Samples[NumSamples];
};

/** Random table for occlusion **/
extern FOcclusionRandomStream GOcclusionRandomStream;


/**
Helper class to time sections of the GPU work.
Buffers multiple frames to avoid waiting on the GPU so times are a little lagged.
*/
class FLatentGPUTimer
{
	FRenderQueryPoolRHIRef TimerQueryPool;
public:
	static const int32 NumBufferedFrames = FOcclusionQueryHelpers::MaxBufferedOcclusionFrames + 1;

	FLatentGPUTimer(FRenderQueryPoolRHIRef InTimerQueryPool, int32 InAvgSamples = 30);
	~FLatentGPUTimer()
	{
		Release();
	}

	void Release();

	/** Retrieves the most recently ready query results. */
	bool Tick(FRHICommandListImmediate& RHICmdList);
	/** Kicks off the query for the start of the rendering you're timing. */
	void Begin(FRHICommandListImmediate& RHICmdList);
	/** Kicks off the query for the end of the rendering you're timing. */
	void End(FRHICommandListImmediate& RHICmdList);

	/** Returns the most recent time in ms. */
	float GetTimeMS();
	/** Gets the average time in ms. Average is tracked over AvgSamples. */
	float GetAverageTimeMS();

private:

	int32 GetQueryIndex();

	//Average Tracking;
	int32 AvgSamples;
	TArray<float> TimeSamples;
	float TotalTime;
	int32 SampleIndex;

	int32 QueryIndex;
	bool QueriesInFlight[NumBufferedFrames];
	FRHIPooledRenderQuery StartQueries[NumBufferedFrames];
	FRHIPooledRenderQuery EndQueries[NumBufferedFrames];
	FGraphEventRef QuerySubmittedFences[NumBufferedFrames];
};

/** HLOD tree persistent fading and visibility state */
class FHLODVisibilityState
{
public:
	FHLODVisibilityState()
		: TemporalLODSyncTime(0.0f)
		, FOVDistanceScaleSq(1.0f)
		, UpdateCount(0)
	{}

	bool IsNodeFading(const int32 PrimIndex) const
	{
		checkSlow(IsValidPrimitiveIndex(PrimIndex));
		return PrimitiveFadingLODMap[PrimIndex];
	}

	bool IsNodeFadingOut(const int32 PrimIndex) const
	{
		checkSlow(IsValidPrimitiveIndex(PrimIndex));
		return PrimitiveFadingOutLODMap[PrimIndex];
	}

	bool IsNodeForcedVisible(const int32 PrimIndex) const
	{
		checkSlow(IsValidPrimitiveIndex(PrimIndex));
		return  ForcedVisiblePrimitiveMap[PrimIndex];
	}

	bool IsNodeForcedHidden(const int32 PrimIndex) const
	{
		checkSlow(IsValidPrimitiveIndex(PrimIndex));
		return ForcedHiddenPrimitiveMap[PrimIndex];
	}

	bool IsValidPrimitiveIndex(const int32 PrimIndex) const
	{
		return ForcedHiddenPrimitiveMap.IsValidIndex(PrimIndex);
	}

	TBitArray<>	PrimitiveFadingLODMap;
	TBitArray<>	PrimitiveFadingOutLODMap;
	TBitArray<>	ForcedVisiblePrimitiveMap;
	TBitArray<>	ForcedHiddenPrimitiveMap;
	float		TemporalLODSyncTime;
	float		FOVDistanceScaleSq;
	uint16		UpdateCount;
};

/** HLOD scene node persistent fading and visibility state */
struct FHLODSceneNodeVisibilityState
{
	FHLODSceneNodeVisibilityState()
		: UpdateCount(0)
		, bWasVisible(0)
		, bIsVisible(0)
		, bIsFading(0)
	{}

	/** Last updated FrameCount */
	uint16 UpdateCount;

	/** Persistent visibility states */
	uint16 bWasVisible	: 1;
	uint16 bIsVisible	: 1;
	uint16 bIsFading	: 1;
};

struct FExposureBufferData
{
	FVertexBufferRHIRef Buffer;
	FShaderResourceViewRHIRef SRV;
	FUnorderedAccessViewRHIRef UAV;

	bool IsValid() const
	{
		return Buffer.IsValid();
	}

	void SafeRelease()
	{
		Buffer.SafeRelease();
		SRV.SafeRelease();
		UAV.SafeRelease();
	}
};

/**
 * The scene manager's private implementation of persistent view state.
 * This class is associated with a particular camera across multiple frames by the game thread.
 * The game thread calls FRendererModule::AllocateViewState to create an instance of this private implementation.
 */
class FSceneViewState : public FSceneViewStateInterface, public FRenderResource
{
public:

	class FProjectedShadowKey
	{
	public:

		inline bool operator == (const FProjectedShadowKey &Other) const
		{
			return (PrimitiveId == Other.PrimitiveId && Light == Other.Light && ShadowSplitIndex == Other.ShadowSplitIndex && bTranslucentShadow == Other.bTranslucentShadow);
		}

		FProjectedShadowKey(const FProjectedShadowInfo& ProjectedShadowInfo)
			: PrimitiveId(ProjectedShadowInfo.GetParentSceneInfo() ? ProjectedShadowInfo.GetParentSceneInfo()->PrimitiveComponentId : FPrimitiveComponentId())
			, Light(ProjectedShadowInfo.GetLightSceneInfo().Proxy->GetLightComponent())
			, ShadowSplitIndex(ProjectedShadowInfo.CascadeSettings.ShadowSplitIndex)
			, bTranslucentShadow(ProjectedShadowInfo.bTranslucentShadow)
		{
		}

		FProjectedShadowKey(FPrimitiveComponentId InPrimitiveId, const ULightComponent* InLight, int32 InSplitIndex, bool bInTranslucentShadow)
			: PrimitiveId(InPrimitiveId)
			, Light(InLight)
			, ShadowSplitIndex(InSplitIndex)
			, bTranslucentShadow(bInTranslucentShadow)
		{
		}

		friend inline uint32 GetTypeHash(const FSceneViewState::FProjectedShadowKey& Key)
		{
			return PointerHash(Key.Light,GetTypeHash(Key.PrimitiveId));
		}

	private:
		FPrimitiveComponentId PrimitiveId;
		const ULightComponent* Light;
		int32 ShadowSplitIndex;
		bool bTranslucentShadow;
	};

	uint32 UniqueID;
	typedef TMap<FSceneViewState::FProjectedShadowKey, FRHIPooledRenderQuery> ShadowKeyOcclusionQueryMap;
	TArray<ShadowKeyOcclusionQueryMap, TInlineAllocator<FOcclusionQueryHelpers::MaxBufferedOcclusionFrames> > ShadowOcclusionQueryMaps;

	/** The view's occlusion query pool. */
	FRenderQueryPoolRHIRef OcclusionQueryPool;
	FFrameBasedOcclusionQueryPool PrimitiveOcclusionQueryPool;

	FHZBOcclusionTester HZBOcclusionTests;

	/** Storage to which compressed visibility chunks are uncompressed at runtime. */
	TArray<uint8> DecompressedVisibilityChunk;

	/** Cached visibility data from the last call to GetPrecomputedVisibilityData. */
	const TArray<uint8>* CachedVisibilityChunk;
	int32 CachedVisibilityHandlerId;
	int32 CachedVisibilityBucketIndex;
	int32 CachedVisibilityChunkIndex;

	uint32		PendingPrevFrameNumber;
	uint32		PrevFrameNumber;
	float		LastRenderTime;
	float		LastRenderTimeDelta;
	float		MotionBlurTimeScale;
	float		MotionBlurTargetDeltaTime;
	FMatrix		PrevViewMatrixForOcclusionQuery;
	FVector		PrevViewOriginForOcclusionQuery;

#if RHI_RAYTRACING
	/** Number of consecutive frames the camera is static */
	uint32 NumCameraStaticFrames;
	int32 RayTracingNumIterations;
#endif

	// A counter incremented once each time this view is rendered.
	uint32 OcclusionFrameCounter;

	/** Used by states that have IsViewParent() == true to store primitives for child states. */
	TSet<FPrimitiveComponentId> ParentPrimitives;

	/** For this view, the set of primitives that are currently fading, either in or out. */
	FPrimitiveFadingStateMap PrimitiveFadingStates;

	FIndirectLightingCacheAllocation* TranslucencyLightingCacheAllocations[TVC_MAX];

	TMap<int32, FIndividualOcclusionHistory> PlanarReflectionOcclusionHistories;

	// Array of ClipmapIndex
	TArray<int32> DeferredGlobalDistanceFieldUpdates[GDF_Num];

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** Are we currently in the state of freezing rendering? (1 frame where we gather what was rendered) */
	uint32 bIsFreezing : 1;

	/** Is rendering currently frozen? */
	uint32 bIsFrozen : 1;

	/** True if the CachedViewMatrices is holding frozen view matrices, otherwise false */
	uint32 bIsFrozenViewMatricesCached : 1;

	/** The set of primitives that were rendered the frame that we froze rendering */
	TSet<FPrimitiveComponentId> FrozenPrimitives;

	/** The cache view matrices at the time of freezing or the cached debug fly cam's view matrices. */
	FViewMatrices CachedViewMatrices;
#endif

	/** HLOD persistent fading and visibility state */
	FHLODVisibilityState HLODVisibilityState;
	TMap<FPrimitiveComponentId, FHLODSceneNodeVisibilityState> HLODSceneNodeVisibilityStates;

	// Software occlusion data
	TUniquePtr<FSceneSoftwareOcclusion> SceneSoftwareOcclusion;

	void UpdatePreExposure(FViewInfo& View);

private:
	void ConditionallyAllocateSceneSoftwareOcclusion(ERHIFeatureLevel::Type InFeatureLevel);

	/** The current frame PreExposure */
	float PreExposure;

	/** Whether to get the last exposure from GPU */
	bool bUpdateLastExposure;

	// to implement eye adaptation / auto exposure changes over time
	// SM5 and above should use RenderTarget and ES3_1 for mobile should use RWBuffer for read back.
	class FEyeAdaptationManager
	{
	public:
		// Allows forward declaration of FRHIGPUTextureReadback
		~FEyeAdaptationManager();

		void SafeRelease();

		const TRefCountPtr<IPooledRenderTarget>& GetCurrentTexture() const
		{
			return GetTexture(CurrentBuffer);
		}

		/** Return current Render Target */
		const TRefCountPtr<IPooledRenderTarget>& GetCurrentTexture(FRHICommandList& RHICmdList)
		{
			return GetOrCreateTexture(RHICmdList, CurrentBuffer);
		}

		int32 GetPreviousPreviousIndex() const
		{
			return ((CurrentBuffer-2)+3)%3;
		}

		/** Return old Render Target*/
		const TRefCountPtr<IPooledRenderTarget>& GetLastTexture(FRHICommandList& RHICmdList)
		{
			// "last" frame is actually 2 behind
			return GetOrCreateTexture(RHICmdList, GetPreviousPreviousIndex());
		}

		/** Reverse the current/last order of the targets */
		void SwapTextures(FRDGBuilder& GraphBuilder, bool bUpdateLastExposure);

		/** Get the last frame exposure value (used to compute pre-exposure) */
		float GetLastExposure() const { return LastExposure; }

		/** Get the last frame average scene luminance (used for exposure compensation curve) */
		float GetLastAverageSceneLuminance() const { return LastAverageSceneLuminance; }

		const FExposureBufferData* GetCurrentBuffer() const
		{
			return GetBuffer(CurrentBuffer);
		}

		const FExposureBufferData* GetCurrentBuffer(FRHICommandListImmediate& RHICmdList)
		{
			return GetOrCreateBuffer(RHICmdList, CurrentBuffer);
		}

		const FExposureBufferData* GetLastBuffer(FRHICommandListImmediate& RHICmdList)
		{
			return GetOrCreateBuffer(RHICmdList, 1 - CurrentBuffer);
		}

		void SwapBuffers(bool bUpdateLastExposure);
	private:
		const TRefCountPtr<IPooledRenderTarget>& GetTexture(uint32 TextureIndex) const;
		const TRefCountPtr<IPooledRenderTarget>& GetOrCreateTexture(FRHICommandList& RHICmdList, uint32 TextureIndex);

		const FExposureBufferData* GetBuffer(uint32 BufferIndex) const;
		FExposureBufferData* GetOrCreateBuffer(FRHICommandListImmediate& RHICmdList, uint32 BufferIndex);

		int32 CurrentBuffer = 0;

		float LastExposure = 0;
		float LastAverageSceneLuminance = 0; // 0 means invalid. Used for Exposure Compensation Curve.

		// Data is triple buffered. When getting the "previous" frame, we actually want the
		// data from two frames agao, so the actual previous frame is ((CurrentBuffer-1)+3)%3, but 
		// the frame we actually want to use is ((CurrentBuffer-2)+3)%3;
		TRefCountPtr<IPooledRenderTarget> PooledRenderTarget[3];
		TUniquePtr<FRHIGPUTextureReadback> ExposureTextureReadback[3];

		// ES3.1 feature level. For efficent readback use buffers instead of textures
		FExposureBufferData ExposureBufferData[2];
		TUniquePtr<FRHIGPUBufferReadback> ExposureBufferReadback;

	} EyeAdaptationManager;

	// The LUT used by tonemapping.  In stereo this is only computed and stored by the Left Eye.
	TRefCountPtr<IPooledRenderTarget> CombinedLUTRenderTarget;

	// LUT is only valid after it has been computed, not on allocation of the RT
	bool bValidTonemappingLUT = false;


	// used by the Postprocess Material Blending system to avoid recreation and garbage collection of MIDs
	TArray<UMaterialInstanceDynamic*> MIDPool;
	uint32 MIDUsedCount;

	// counts up by one each frame, warped in 0..3 range, ResetViewState() puts it back to 0
	int32 DistanceFieldTemporalSampleIndex;

	// light propagation volume used in this view
	TRefCountPtr<FLightPropagationVolume> LightPropagationVolume;

	// whether this view is a stereo counterpart to a primary view
	bool bIsStereoView;

	// The whether or not round-robin occlusion querying is enabled for this view
	bool bRoundRobinOcclusionEnabled;

public:
	
	// if TemporalAA is on this cycles through 0..TemporalAASampleCount-1, ResetViewState() puts it back to 0
	int8 TemporalAASampleIndex;

	// if TemporalAA is on this cycles through 0..Onwards, ResetViewState() puts it back to 0
	uint32 TemporalAASampleIndexUnclamped;

	// counts up by one each frame, warped in 0..7 range, ResetViewState() puts it back to 0
	uint32 FrameIndex;
	
	/** Informations of to persist for the next frame's FViewInfo::PrevViewInfo.
	 *
	 * Under normal use case (temporal histories are not frozen), this gets cleared after setting FViewInfo::PrevViewInfo
	 * after being copied to FViewInfo::PrevViewInfo. New temporal histories get directly written to it.
	 *
	 * When temporal histories are frozen (pause command, or r.Test.FreezeTemporalHistories), this keeps it's values, and the currently
	 * rendering FViewInfo should not update it. Refer to FViewInfo::bStatePrevViewInfoIsReadOnly.
	 */
	FPreviousViewInfo PrevFrameViewInfo;

	FHeightfieldLightingAtlas* HeightfieldLightingAtlas;

	// Temporal AA result for light shafts of last frame
	FTemporalAAHistory LightShaftOcclusionHistory;
	// Temporal AA result for light shafts of last frame
	TMap<const ULightComponent*, TUniquePtr<FTemporalAAHistory> > LightShaftBloomHistoryRTs;

	FIntRect DistanceFieldAOHistoryViewRect;
	TRefCountPtr<IPooledRenderTarget> DistanceFieldAOHistoryRT;
	TRefCountPtr<IPooledRenderTarget> DistanceFieldIrradianceHistoryRT;

	// Burley Subsurface scattering variance texture from the last frame.
	TRefCountPtr<IPooledRenderTarget> SubsurfaceScatteringQualityHistoryRT;

	// Pre-computed filter in spectral (i.e. FFT) domain along with data to determine if we need to up date it
	struct {
		/// @cond DOXYGEN_WARNINGS
		void SafeRelease() { Spectral.SafeRelease(); CenterWeight.SafeRelease(); }
		/// @endcond

		// The 2d fourier transform of the physical space texture.
		TRefCountPtr<IPooledRenderTarget> Spectral;
		TRefCountPtr<IPooledRenderTarget> CenterWeight; // a 1-pixel buffer that holds blend weights for half-resolution fft.

														// The physical space source texture
		UTexture2D* Physical = NULL;

		// The Scale * 100 = percentage of the image space that the physical kernel represents.
		// e.g. Scale = 1 indicates that the physical kernel image occupies the same size 
		// as the image to be processed with the FFT convolution.
		float Scale = 0.f;

		// The size of the viewport for which the spectral kernel was calculated. 
		FIntPoint ImageSize;

		FVector2D CenterUV;

		// Mip level of the physical space source texture used when caching the spectral space texture.
		uint32 PhysicalMipLevel;
	} BloomFFTKernel;

	// Cached material texture samplers
	float MaterialTextureCachedMipBias;
	FSamplerStateRHIRef MaterialTextureBilinearWrapedSamplerCache;
	FSamplerStateRHIRef MaterialTextureBilinearClampedSamplerCache;

#if RHI_RAYTRACING
	// Invalidates cached results related to the path tracer so accumulated rendering can start over
	void PathTracingInvalidate();

	// Reference path tracing cached results
	TRefCountPtr<IPooledRenderTarget> PathTracingRadianceRT;
	TRefCountPtr<IPooledRenderTarget> PathTracingAlbedoRT;
	TRefCountPtr<IPooledRenderTarget> PathTracingNormalRT;
	TRefCountPtr<IPooledRenderTarget> PathTracingRadianceDenoisedRT;
	// Keep track of the rectangle of pixels the Radiance texture is valid for so that path tracing can restart if this changes
	FIntRect PathTracingRect;
	// Target sampling count for the path tracer - to allow different views to target different quality levels
	uint32   PathTracingTargetSPP;

	// Current sample index to be rendered by the path tracer - this gets incremented each time the path tracer accumulates a frame of samples
	uint32 PathTracingSampleIndex;

	// Path tracer frame index, not reset on invalidation unlike PathTracingSampleIndex to avoid
	// the "screen door" effect and reduce temporal aliasing
	uint32_t PathTracingFrameIndex;

	// IES light profiles
	FIESLightProfileResource IESLightProfileResources;

	// Ray Traced Reflection Imaginary GBuffer Data containing a pseudo-geometric representation of the reflected surface(s)
	TRefCountPtr<IPooledRenderTarget> ImaginaryReflectionGBufferA;
	TRefCountPtr<IPooledRenderTarget> ImaginaryReflectionDepthZ;
	TRefCountPtr<IPooledRenderTarget> ImaginaryReflectionVelocity;

	// Ray Traced Sky Light Sample Direction Data
	TRefCountPtr<FRDGPooledBuffer> SkyLightVisibilityRaysBuffer;
	FIntVector SkyLightVisibilityRaysDimensions;

	// Ray Traced Global Illumination Gather Point Data
	TRefCountPtr<FRDGPooledBuffer> GatherPointsBuffer;
	FIntVector GatherPointsResolution;
	// todo: shared definition for maximum gather points per-pixel (32)
	TStaticArray<FMatrix, 32> GatherPointsViewHistory;
	uint32 GatherPointsCount;

	// Last valid RTPSO is saved, so it could be used as fallback in future frames if background PSO compilation is enabled.
	// This RTPSO can be used only if the only difference from previous PSO is the material hit shaders.
	FRayTracingPipelineStateSignature LastRayTracingMaterialPipelineSignature;
#endif

	TUniquePtr<FForwardLightingViewResources> ForwardLightingResources;

	FForwardLightingCullingResources ForwardLightingCullingResources;

	TRefCountPtr<IPooledRenderTarget> LightScatteringHistory;

	/** Distance field AO tile intersection GPU resources.  Last frame's state is not used, but they must be sized exactly to the view so stored here. */
	class FTileIntersectionResources* AOTileIntersectionResources;

	class FAOScreenGridResources* AOScreenGridResources;

	bool bInitializedGlobalDistanceFieldOrigins;
	FGlobalDistanceFieldClipmapState GlobalDistanceFieldClipmapState[GMaxGlobalDistanceFieldClipmaps];
	int32 GlobalDistanceFieldUpdateIndex;

	FVertexBufferRHIRef IndirectShadowCapsuleShapesVertexBuffer;
	FShaderResourceViewRHIRef IndirectShadowCapsuleShapesSRV;
	FVertexBufferRHIRef IndirectShadowMeshDistanceFieldCasterIndicesVertexBuffer;
	FShaderResourceViewRHIRef IndirectShadowMeshDistanceFieldCasterIndicesSRV;
	FVertexBufferRHIRef IndirectShadowLightDirectionVertexBuffer;
	FShaderResourceViewRHIRef IndirectShadowLightDirectionSRV;
	FRWBuffer IndirectShadowVolumetricLightmapDerivedLightDirection;
	FRWBuffer CapsuleTileIntersectionCountsBuffer;

	/** Contains both DynamicPrimitiveShaderData (per view) and primitive shader data (per scene).  Stored in ViewState for pooling only (contents are not persistent). */
	/** Only one of the resources(TextureBuffer or Texture2D) will be used depending on the Mobile.UseGPUSceneTexture cvar */
	FRWBufferStructured PrimitiveShaderDataBuffer;
	FTextureRWBuffer2D PrimitiveShaderDataTexture;

	/** Timestamp queries around separate translucency, used for auto-downsampling. */
	FRenderQueryPoolRHIRef TimerQueryPool;
	FLatentGPUTimer TranslucencyTimer;
	FLatentGPUTimer SeparateTranslucencyTimer;
	FLatentGPUTimer SeparateTranslucencyModulateTimer;

	/** This is float since it is derived off of UWorld::RealTimeSeconds, which is relative to BeginPlay time. */
	float LastAutoDownsampleChangeTime;
	float SmoothedHalfResTranslucencyGPUDuration;
	float SmoothedFullResTranslucencyGPUDuration;

	/** Current desired state of auto-downsampled separate translucency for this view. */
	bool bShouldAutoDownsampleTranslucency;

	// Is DOFHistoryRT set from DepthOfField?
	bool bDOFHistory;
	// Is DOFHistoryRT2 set from DepthOfField?
	bool bDOFHistory2;

	// Sequencer state for view management
	ESequencerState SequencerState;

	FTemporalLODState TemporalLODState;

	FVolumetricRenderTargetViewStateData VolumetricCloudRenderTarget;
	FTemporalRenderTargetState VolumetricCloudShadowRenderTarget[NUM_ATMOSPHERE_LIGHTS];
	FMatrix VolumetricCloudShadowmapPreviousWorldToLightClipMatrix[NUM_ATMOSPHERE_LIGHTS];
	FVector VolumetricCloudShadowmapPreviousAtmosphericLightPos[NUM_ATMOSPHERE_LIGHTS];
	FVector VolumetricCloudShadowmapPreviousAnchorPoint[NUM_ATMOSPHERE_LIGHTS];
	FVector VolumetricCloudShadowmapPreviousAtmosphericLightDir[NUM_ATMOSPHERE_LIGHTS];

	// View state
	FHairStrandsViewData HairStrandsViewData;

	// call after OnFrameRenderingSetup()
	virtual uint32 GetCurrentTemporalAASampleIndex() const
	{
		return TemporalAASampleIndex;
	}

	virtual uint32 GetCurrentUnclampedTemporalAASampleIndex() const
	{
		return TemporalAASampleIndexUnclamped;
	}
	// Returns the index of the frame with a desired power of two modulus.
	inline uint32 GetFrameIndex(uint32 Pow2Modulus) const
	{
		check(FMath::IsPowerOfTwo(Pow2Modulus));
		return FrameIndex & (Pow2Modulus - 1);
	}

	// Returns 32bits frame index.
	inline uint32 GetFrameIndex() const
	{
		return FrameIndex;
	}

	// to make rendering more deterministic
	virtual void ResetViewState()
	{
		TemporalAASampleIndex = 0;
		TemporalAASampleIndexUnclamped = 0;
		FrameIndex = 0;
		DistanceFieldTemporalSampleIndex = 0;
		PreExposure = 1.f;

		ReleaseDynamicRHI();
	}

	void SetupDistanceFieldTemporalOffset(const FSceneViewFamily& Family)
	{
		if (!Family.bWorldIsPaused)
		{
			DistanceFieldTemporalSampleIndex++;
		}

		if(DistanceFieldTemporalSampleIndex >= 4)
		{
			DistanceFieldTemporalSampleIndex = 0;
		}
	}

	int32 GetDistanceFieldTemporalSampleIndex() const
	{
		return DistanceFieldTemporalSampleIndex;
	}


	// call only if not yet created
	void SetupLightPropagationVolume(FSceneView& View, FSceneViewFamily& ViewFamily);

	/**
	 * @return can return 0
	 * @param bIncludeStereo - specifies whether the getter should include stereo views in its returned value
	 */
	FLightPropagationVolume* GetLightPropagationVolume(ERHIFeatureLevel::Type InFeatureLevel, bool bIncludeStereo = false) const;

	/** Default constructor. */
	FSceneViewState();

	void DestroyLightPropagationVolume();

	virtual ~FSceneViewState();

	// called every frame after the view state was updated
	void UpdateLastRenderTime(const FSceneViewFamily& Family)
	{
		// The editor can trigger multiple update calls within a frame
		if(Family.CurrentRealTime != LastRenderTime)
		{
			LastRenderTimeDelta = Family.CurrentRealTime - LastRenderTime;
			LastRenderTime = Family.CurrentRealTime;
		}
	}

	void TrimHistoryRenderTargets(const FScene* Scene);

	/**
	 * Calculates and stores the scale factor to apply to motion vectors based on the current game
	 * time and view post process settings.
	 */
	void UpdateMotionBlurTimeScale(const FViewInfo& View);

	/** 
	 * Called every frame after UpdateLastRenderTime, sets up the information for the lagged temporal LOD transition
	 */
	void UpdateTemporalLODTransition(const FViewInfo& View)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (bIsFrozen)
		{
			return;
		}
#endif

		TemporalLODState.UpdateTemporalLODTransition(View, LastRenderTime);
	}

	/** 
	 * Returns an array of visibility data for the given view, or NULL if none exists. 
	 * The data bits are indexed by VisibilityId of each primitive in the scene.
	 * This method decompresses data if necessary and caches it based on the bucket and chunk index in the view state.
	 */
	const uint8* GetPrecomputedVisibilityData(FViewInfo& View, const FScene* Scene);

	/**
	 * Cleans out old entries from the primitive occlusion history, and resets unused pending occlusion queries.
	 * @param MinHistoryTime - The occlusion history for any primitives which have been visible and unoccluded since
	 *							this time will be kept.  The occlusion history for any primitives which haven't been
	 *							visible and unoccluded since this time will be discarded.
	 * @param MinQueryTime - The pending occlusion queries older than this will be discarded.
	 */
	void TrimOcclusionHistory(float CurrentTime, float MinHistoryTime, float MinQueryTime, int32 FrameNumber);

	inline void UpdateRoundRobin(const bool bUseRoundRobin)
	{
		bRoundRobinOcclusionEnabled = bUseRoundRobin;
	}

	inline bool IsRoundRobinEnabled() const
	{
		return bRoundRobinOcclusionEnabled;
	}

	/**
	 * Checks whether a shadow is occluded this frame.
	 * @param Primitive - The shadow subject.
	 * @param Light - The shadow source.
	 */
	bool IsShadowOccluded(FRHICommandListImmediate& RHICmdList, FSceneViewState::FProjectedShadowKey ShadowKey, int32 NumBufferedFrames) const;

	/**
	* Retrieve a single-pixel render targets with intra-frame state for use in eye adaptation post processing.
	*/
	IPooledRenderTarget* GetCurrentEyeAdaptationTexture() const override final
	{
		IPooledRenderTarget* Texture = EyeAdaptationManager.GetCurrentTexture().GetReference();
		check(bValidEyeAdaptationTexture && Texture);
		return Texture;
	}

	IPooledRenderTarget* GetCurrentEyeAdaptationTexture(FRHICommandList& RHICmdList)
	{
		bValidEyeAdaptationTexture = true;
		return EyeAdaptationManager.GetCurrentTexture(RHICmdList).GetReference();
	}

	IPooledRenderTarget* GetLastEyeAdaptationTexture(FRHICommandList& RHICmdList)
	{
		return EyeAdaptationManager.GetLastTexture(RHICmdList).GetReference();
	}

	/** Swaps the double-buffer targets used in eye adaptation */
	void SwapEyeAdaptationTextures(FRDGBuilder& GraphBuilder)
	{
		EyeAdaptationManager.SwapTextures(GraphBuilder, bUpdateLastExposure && bValidEyeAdaptationTexture);
	}

	const FExposureBufferData* GetCurrentEyeAdaptationBuffer() const override final
	{
		const FExposureBufferData* Buffer = EyeAdaptationManager.GetCurrentBuffer();
		check(bValidEyeAdaptationBuffer && Buffer);
		return Buffer;
	}

	const FExposureBufferData* GetCurrentEyeAdaptationBuffer(FRHICommandListImmediate& RHICmdList)
	{
		bValidEyeAdaptationBuffer = true;
		return EyeAdaptationManager.GetCurrentBuffer(RHICmdList);
	}

	const FExposureBufferData* GetLastEyeAdaptationBuffer(FRHICommandListImmediate& RHICmdList)
	{
		return EyeAdaptationManager.GetLastBuffer(RHICmdList);
	}

	void SwapEyeAdaptationBuffers()
	{
		EyeAdaptationManager.SwapBuffers(bUpdateLastExposure && bValidEyeAdaptationBuffer);
	}

#if WITH_MGPU
	void BroadcastEyeAdaptationTemporalEffect(FRHICommandList& RHICmdList);
	void WaitForEyeAdaptationTemporalEffect(FRHICommandList& RHICmdList);
#endif

	float GetLastEyeAdaptationExposure() const
	{
		return EyeAdaptationManager.GetLastExposure();
	}

	float GetLastAverageSceneLuminance() const
	{
		return EyeAdaptationManager.GetLastAverageSceneLuminance();
	}

	bool HasValidTonemappingLUT() const
	{
		return bValidTonemappingLUT;
	}

	void SetValidTonemappingLUT(bool bValid = true)
	{
		bValidTonemappingLUT = bValid;
	}

	static FPooledRenderTargetDesc CreateLUTRenderTarget(const int32 LUTSize, const bool bUseVolumeLUT, const bool bNeedUAV, const bool bNeedFloatOutput)
	{
		// Create the texture needed for the tonemapping LUT in one place
		EPixelFormat LUTPixelFormat = PF_A2B10G10R10;
		if (!GPixelFormats[LUTPixelFormat].Supported)
		{
			LUTPixelFormat = PF_R8G8B8A8;
		}
		if (bNeedFloatOutput)
		{
			LUTPixelFormat = PF_FloatRGBA;
		}

		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(FIntPoint(LUTSize * LUTSize, LUTSize), LUTPixelFormat, FClearValueBinding::Transparent, TexCreate_None, TexCreate_ShaderResource, false);
		Desc.TargetableFlags |= bNeedUAV ? TexCreate_UAV : TexCreate_RenderTargetable;

		if (bUseVolumeLUT)
		{
			Desc.Extent = FIntPoint(LUTSize, LUTSize);
			Desc.Depth = LUTSize;
		}

		Desc.DebugName = TEXT("CombineLUTs");
		Desc.Flags |= GFastVRamConfig.CombineLUTs;

		return Desc;
	}

	// Returns a reference to the render target used for the LUT.  Allocated on the first request.
	IPooledRenderTarget* GetTonemappingLUT(FRHICommandList& RHICmdList, const int32 LUTSize, const bool bUseVolumeLUT, const bool bNeedUAV, const bool bNeedFloatOutput)
	{
		if (CombinedLUTRenderTarget.IsValid() == false || 
			CombinedLUTRenderTarget->GetDesc().Extent.Y != LUTSize ||
			((CombinedLUTRenderTarget->GetDesc().Depth != 0) != bUseVolumeLUT) ||
			!!(CombinedLUTRenderTarget->GetDesc().TargetableFlags & TexCreate_UAV) != bNeedUAV ||
			(CombinedLUTRenderTarget->GetDesc().Format == PF_FloatRGBA) != bNeedFloatOutput)
		{
			// Create the texture needed for the tonemapping LUT
			FPooledRenderTargetDesc Desc = CreateLUTRenderTarget(LUTSize, bUseVolumeLUT, bNeedUAV, bNeedFloatOutput);

			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, CombinedLUTRenderTarget, Desc.DebugName);
		}

		return CombinedLUTRenderTarget.GetReference();
	}

	IPooledRenderTarget* GetTonemappingLUT() const
	{
		return CombinedLUTRenderTarget.GetReference();
	}

	// FRenderResource interface.
	virtual void InitDynamicRHI() override
	{
		HZBOcclusionTests.InitDynamicRHI();
	}

	virtual void ReleaseDynamicRHI() override
	{
		HZBOcclusionTests.ReleaseDynamicRHI();
		EyeAdaptationManager.SafeRelease();
		bValidEyeAdaptationTexture = false;
		bValidEyeAdaptationBuffer = false;
	}

	// FSceneViewStateInterface
	RENDERER_API virtual void Destroy() override;

	virtual FSceneViewState* GetConcreteViewState() override
	{
		return this;
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{

		Collector.AddReferencedObjects(MIDPool);

		if (BloomFFTKernel.Physical)
		{
			Collector.AddReferencedObject(BloomFFTKernel.Physical);
		}
	}

	/** called in InitViews() */
	void OnStartRender(FViewInfo& View, FSceneViewFamily& ViewFamily)
	{
		check(IsInRenderingThread());

		if(!(View.FinalPostProcessSettings.IndirectLightingColor * View.FinalPostProcessSettings.IndirectLightingIntensity).IsAlmostBlack())
		{
			SetupLightPropagationVolume(View, ViewFamily);
		}
		ConditionallyAllocateSceneSoftwareOcclusion(View.GetFeatureLevel());
	}

	// needed for GetReusableMID()
	virtual void OnStartPostProcessing(FSceneView& CurrentView) override
	{
		check(IsInGameThread());

		// Needs to be done once for all viewstates.  If multiple FSceneViews are sharing the same ViewState, this will cause problems.
		// Sharing should be illegal right now though.
		MIDUsedCount = 0;
	}

	/** Returns the current PreExposure value. PreExposure is a custom scale applied to the scene color to prevent buffer overflow. */
	virtual float GetPreExposure() const override
	{
		return PreExposure;
	}

	// Note: OnStartPostProcessing() needs to be called each frame for each view
	virtual UMaterialInstanceDynamic* GetReusableMID(class UMaterialInterface* InSource) override
	{		
		check(IsInGameThread());
		check(InSource);

		// 0 or MID (MaterialInstanceDynamic) pointer
		auto InputAsMID = Cast<UMaterialInstanceDynamic>(InSource);

		// fixup MID parents as this is not allowed, take the next MIC or Material.
		UMaterialInterface* ParentOfTheNewMID = InputAsMID ? InputAsMID->Parent : InSource;

		// this is not allowed and would cause an error later in the code
		check(!ParentOfTheNewMID->IsA(UMaterialInstanceDynamic::StaticClass()));

		UMaterialInstanceDynamic* NewMID = 0;

		if(MIDUsedCount < (uint32)MIDPool.Num())
		{
			NewMID = MIDPool[MIDUsedCount];

			if(NewMID->Parent != ParentOfTheNewMID)
			{
				// create a new one
				// garbage collector will remove the old one
				// this should not happen too often
				NewMID = UMaterialInstanceDynamic::Create(ParentOfTheNewMID, 0);
				MIDPool[MIDUsedCount] = NewMID;
			}

			// reusing an existing object means we need to clear out the Vector and Scalar parameters
			NewMID->ClearParameterValues();
		}
		else
		{
			NewMID = UMaterialInstanceDynamic::Create(ParentOfTheNewMID, 0);
			check(NewMID);

			MIDPool.Add(NewMID);
		}

		if(InputAsMID)
		{
			// parent is an MID so we need to copy the MID Vector and Scalar parameters over
			NewMID->CopyInterpParameters(InputAsMID);
		}

		check(NewMID->GetRenderProxy());
		MIDUsedCount++;
		return NewMID;
	}

	virtual void ClearMIDPool() override
	{
		check(IsInGameThread());
		MIDPool.Empty();
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	virtual const FViewMatrices* GetFrozenViewMatrices() const override
	{
		if (bIsFrozen && bIsFrozenViewMatricesCached)
		{
			return &CachedViewMatrices;
		}
		return nullptr;
	}

	virtual void ActivateFrozenViewMatrices(FSceneView& SceneView) override
	{
		auto* ViewState = static_cast<FSceneViewState*>(SceneView.State);
		if (ViewState->bIsFrozen)
		{
			check(ViewState->bIsFrozenViewMatricesCached);

			Swap(SceneView.ViewMatrices, ViewState->CachedViewMatrices);
			ViewState->bIsFrozenViewMatricesCached = false;
		}
	}

	virtual void RestoreUnfrozenViewMatrices(FSceneView& SceneView) override
	{
		auto* ViewState = static_cast<FSceneViewState*>(SceneView.State);
		if (ViewState->bIsFrozen)
		{
			check(!ViewState->bIsFrozenViewMatricesCached);

			Swap(SceneView.ViewMatrices, ViewState->CachedViewMatrices);
			ViewState->bIsFrozenViewMatricesCached = true;
		}
	}
#endif

	virtual FTemporalLODState& GetTemporalLODState() override
	{
		return TemporalLODState;
	}

	virtual const FTemporalLODState& GetTemporalLODState() const override
	{
		return TemporalLODState;
	}

	float GetTemporalLODTransition() const override
	{
		return TemporalLODState.GetTemporalLODTransition(LastRenderTime);
	}

	uint32 GetViewKey() const override
	{
		return UniqueID;
	}

	uint32 GetOcclusionFrameCounter() const
	{
		return OcclusionFrameCounter;
	}

	virtual SIZE_T GetSizeBytes() const override;

	virtual void SetSequencerState(ESequencerState InSequencerState) override
	{
		SequencerState = InSequencerState;
	}

	virtual ESequencerState GetSequencerState() override
	{
		return SequencerState;
	}

	/** Information about visibility/occlusion states in past frames for individual primitives. */
	TSet<FPrimitiveOcclusionHistory,FPrimitiveOcclusionHistoryKeyFuncs> PrimitiveOcclusionHistorySet;
};

/** Rendering resource class that manages a cubemap array for reflections. */
class FReflectionEnvironmentCubemapArray : public FRenderResource
{
public:

	FReflectionEnvironmentCubemapArray(ERHIFeatureLevel::Type InFeatureLevel)
		: FRenderResource(InFeatureLevel)
		, MaxCubemaps(0)
		, CubemapSize(0)
	{}

	virtual void InitDynamicRHI() override;
	virtual void ReleaseDynamicRHI() override;

	/** 
	 * Updates the maximum number of cubemaps that this array is allocated for.
	 * This reallocates the resource but does not copy over the old contents. 
	 */
	void UpdateMaxCubemaps(uint32 InMaxCubemaps, int32 CubemapSize);

	/**
	* Updates the maximum number of cubemaps that this array is allocated for.
	* This reallocates the resource and copies over the old contents, preserving indices
	*/
	void ResizeCubemapArrayGPU(uint32 InMaxCubemaps, int32 CubemapSize, const TArray<int32>& IndexRemapping);

	int32 GetMaxCubemaps() const { return MaxCubemaps; }
	int32 GetCubemapSize() const { return CubemapSize; }
	bool IsValid() const { return IsValidRef(ReflectionEnvs); }
	FSceneRenderTargetItem& GetRenderTarget() const { return ReflectionEnvs->GetRenderTargetItem(); }

protected:
	uint32 MaxCubemaps;
	int32 CubemapSize;
	TRefCountPtr<IPooledRenderTarget> ReflectionEnvs;

	void ReleaseCubeArray();
};

/** Per-component reflection capture state that needs to persist through a re-register. */
class FCaptureComponentSceneState
{
public:
	/** Index of the cubemap in the array for this capture component. */
	int32 CubemapIndex;

	float AverageBrightness;

	FCaptureComponentSceneState(int32 InCubemapIndex) :
		CubemapIndex(InCubemapIndex),
		AverageBrightness(0.0f)
	{}

	bool operator==(const FCaptureComponentSceneState& Other) const 
	{
		return CubemapIndex == Other.CubemapIndex;
	}
};

struct FReflectionCaptureSortData
{
	uint32 Guid;
	int32 CubemapIndex;
	FVector4 PositionAndRadius;
	FVector4 CaptureProperties;
	FMatrix BoxTransform;
	FVector4 BoxScales;
	FVector4 CaptureOffsetAndAverageBrightness;
	FReflectionCaptureProxy* CaptureProxy;

	bool operator < (const FReflectionCaptureSortData& Other) const
	{
		if (PositionAndRadius.W != Other.PositionAndRadius.W)
		{
			return PositionAndRadius.W < Other.PositionAndRadius.W;
		}
		else
		{
			return Guid < Other.Guid;
		}
	}
};

/** Scene state used to manage the reflection environment feature. */
class FReflectionEnvironmentSceneData
{
public:

	/** 
	 * Set to true for one frame whenever RegisteredReflectionCaptures or the transforms of any registered reflection proxy has changed,
	 * Which allows one frame to update cached proxy associations.
	 */
	bool bRegisteredReflectionCapturesHasChanged;

	/** True if AllocatedReflectionCaptureState has changed. Allows to update cached single capture id. */
	bool AllocatedReflectionCaptureStateHasChanged;

	/** The rendering thread's list of visible reflection captures in the scene. */
	TArray<FReflectionCaptureProxy*> RegisteredReflectionCaptures;
	TArray<FVector> RegisteredReflectionCapturePositions;

	/** 
	 * Cubemap array resource which contains the captured scene for each reflection capture.
	 * This is indexed by the value of AllocatedReflectionCaptureState.CaptureIndex.
	 */
	FReflectionEnvironmentCubemapArray CubemapArray;

	/** Rendering thread map from component to scene state.  This allows storage of RT state that needs to persist through a component re-register. */
	TMap<const UReflectionCaptureComponent*, FCaptureComponentSceneState> AllocatedReflectionCaptureState;

	/** Rendering bitfield to track cubemap slots used. Needs to kept in sync with AllocatedReflectionCaptureState */
	TBitArray<> CubemapArraySlotsUsed;

	/** Sorted scene reflection captures for upload to the GPU. */
	TArray<FReflectionCaptureSortData> SortedCaptures;
	int32 NumBoxCaptures;
	int32 NumSphereCaptures;

	/** 
	 * Game thread list of reflection components that have been allocated in the cubemap array. 
	 * These are not necessarily all visible or being rendered, but their scene state is stored in the cubemap array.
	 */
	TSparseArray<UReflectionCaptureComponent*> AllocatedReflectionCapturesGameThread;

	/** Game thread tracking of what size this scene has allocated for the cubemap array. */
	int32 MaxAllocatedReflectionCubemapsGameThread;

	FReflectionEnvironmentSceneData(ERHIFeatureLevel::Type InFeatureLevel) :
		bRegisteredReflectionCapturesHasChanged(true),
		AllocatedReflectionCaptureStateHasChanged(false),
		CubemapArray(InFeatureLevel),
		MaxAllocatedReflectionCubemapsGameThread(0)
	{}

	void ResizeCubemapArrayGPU(uint32 InMaxCubemaps, int32 InCubemapSize);
};

class FVolumetricLightmapInterpolation
{
public:
	FVector4 IndirectLightingSHCoefficients0[3];
	FVector4 IndirectLightingSHCoefficients1[3];
	FVector4 IndirectLightingSHCoefficients2;
	FVector4 IndirectLightingSHSingleCoefficient;
	FVector4 PointSkyBentNormal;
	float DirectionalLightShadowing;
	uint32 LastUsedSceneFrameNumber;
};

class FVolumetricLightmapSceneData
{
public:

	FVolumetricLightmapSceneData(FScene* InScene)
		: Scene(InScene)
	{
		GlobalVolumetricLightmap.Data = &GlobalVolumetricLightmapData;
	}

	bool HasData() const;
	void AddLevelVolume(const class FPrecomputedVolumetricLightmap* InVolume, EShadingPath ShadingPath, bool bIsPersistentLevel);
	void RemoveLevelVolume(const class FPrecomputedVolumetricLightmap* InVolume);
	const FPrecomputedVolumetricLightmap* GetLevelVolumetricLightmap() const;

	TMap<FVector, FVolumetricLightmapInterpolation> CPUInterpolationCache;

	FPrecomputedVolumetricLightmapData GlobalVolumetricLightmapData;
private:
	FScene* Scene;
	FPrecomputedVolumetricLightmap GlobalVolumetricLightmap;
	const FPrecomputedVolumetricLightmap* PersistentLevelVolumetricLightmap = nullptr;
	TArray<const FPrecomputedVolumetricLightmap*> LevelVolumetricLightmaps;
};

class FPrimitiveAndInstance
{
public:

	FPrimitiveAndInstance(const FVector4& InBoundingSphere, FPrimitiveSceneInfo* InPrimitive, int32 InInstanceIndex) :
		BoundingSphere(InBoundingSphere),
		Primitive(InPrimitive),
		InstanceIndex(InInstanceIndex)
	{}

	FVector4 BoundingSphere;
	FPrimitiveSceneInfo* Primitive;
	int32 InstanceIndex;
};

class FLinearAllocation
{
public:

	FLinearAllocation(int32 InStartOffset, int32 InNum) :
		StartOffset(InStartOffset),
		Num(InNum)
	{}

	int32 StartOffset;
	int32 Num;

	bool Contains(FLinearAllocation Other)
	{
		return StartOffset <= Other.StartOffset && (StartOffset + Num) >= (Other.StartOffset + Other.Num);
	}
};

class FGrowOnlySpanAllocator
{
public:

	FGrowOnlySpanAllocator() :
		MaxSize(0)
	{}

	// Allocate a range.  Returns allocated StartOffset.
	int32 Allocate(int32 Num);

	// Free an already allocated range.  
	void Free(int32 BaseOffset, int32 Num);

	int32 GetMaxSize() const
	{
		return MaxSize;
	}

private:

	// Size of the linear range used by the allocator
	int32 MaxSize;

	// Unordered free list
	TArray<FLinearAllocation, TInlineAllocator<10>> FreeSpans;

	int32 SearchFreeList(int32 Num);
};

class FGPUScene
{
public:
	FGPUScene()
		: bUpdateAllPrimitives(false)
	{
	}

	bool bUpdateAllPrimitives;

	/** Indices of primitives that need to be updated in GPU Scene */
	TArray<int32> PrimitivesToUpdate;

	/** Bit array of all scene primitives. Set bit means that current primitive is in PrimitivesToUpdate array. */
	TBitArray<> PrimitivesMarkedToUpdate;

	/** GPU mirror of Primitives */
	/** Only one of the resources(TextureBuffer or Texture2D) will be used depending on the Mobile.UseGPUSceneTexture cvar */
	FRWBufferStructured PrimitiveBuffer;
	FTextureRWBuffer2D PrimitiveTexture;
	FScatterUploadBuffer PrimitiveUploadBuffer;
	FScatterUploadBuffer PrimitiveUploadViewBuffer;

	FGrowOnlySpanAllocator	LightmapDataAllocator;
	FRWBufferStructured		LightmapDataBuffer;
	FScatterUploadBuffer	LightmapUploadBuffer;
};

class FPrimitiveRemoveInfo
{
public:
	FPrimitiveRemoveInfo(const FPrimitiveSceneInfo* InPrimitive) :
		Primitive(InPrimitive),
		bOftenMoving(InPrimitive->Proxy->IsOftenMoving()),
		DistanceFieldInstanceIndices(Primitive->DistanceFieldInstanceIndices)
	{}

	/** 
	 * Must not be dereferenced after creation, the primitive was removed from the scene and deleted
	 * Value of the pointer is still useful for map lookups
	 */
	const FPrimitiveSceneInfo* Primitive;

	bool bOftenMoving;

	TArray<int32, TInlineAllocator<1>> DistanceFieldInstanceIndices;
};

class FHeightFieldPrimitiveRemoveInfo : public FPrimitiveRemoveInfo
{
public:
	FHeightFieldPrimitiveRemoveInfo(const FPrimitiveSceneInfo* InPrimitive)
		: FPrimitiveRemoveInfo(InPrimitive)
	{
		const FBoxSphereBounds Bounds = InPrimitive->Proxy->GetBounds();
		SphereBound = FVector4(Bounds.Origin, Bounds.SphereRadius);
	}

	FVector4 SphereBound;
};

/** Scene data used to manage distance field object buffers on the GPU. */
class FDistanceFieldSceneData
{
public:

	FDistanceFieldSceneData(EShaderPlatform ShaderPlatform);
	~FDistanceFieldSceneData();

	void AddPrimitive(FPrimitiveSceneInfo* InPrimitive);
	void UpdatePrimitive(FPrimitiveSceneInfo* InPrimitive);
	void RemovePrimitive(FPrimitiveSceneInfo* InPrimitive);
	void Release();
	void VerifyIntegrity();

	bool HasPendingOperations() const
	{
		return PendingAddOperations.Num() > 0 || PendingUpdateOperations.Num() > 0 || PendingRemoveOperations.Num() > 0;
	}

	bool HasPendingHeightFieldOperations() const
	{
		return PendingHeightFieldAddOps.Num() > 0 || PendingHeightFieldUpdateOps.Num() > 0 || PendingHeightFieldRemoveOps.Num() > 0;
	}

	bool HasPendingRemovePrimitive(const FPrimitiveSceneInfo* Primitive) const
	{
		for (int32 RemoveIndex = 0; RemoveIndex < PendingRemoveOperations.Num(); ++RemoveIndex)
		{
			if (PendingRemoveOperations[RemoveIndex].Primitive == Primitive)
			{
				return true;
			}
		}

		return false;
	}

	bool HasPendingRemoveHeightFieldPrimitive(const FPrimitiveSceneInfo* Primitive) const
	{
		for (int32 RemoveIndex = 0; RemoveIndex < PendingHeightFieldRemoveOps.Num(); ++RemoveIndex)
		{
			if (PendingHeightFieldRemoveOps[RemoveIndex].Primitive == Primitive)
			{
				return true;
			}
		}

		return false;
	}

	inline bool CanUse16BitObjectIndices() const
	{
		return bCanUse16BitObjectIndices && (NumObjectsInBuffer < (1 << 16));
	}

	bool CanUse16BitHeightFieldObjectIndices() const
	{
		return bCanUse16BitObjectIndices && (NumHeightFieldObjectsInBuffer < 65536);
	}

	const class FDistanceFieldObjectBuffers* GetCurrentObjectBuffers() const
	{
		return ObjectBuffers[ObjectBufferIndex];
	}

	const class FHeightFieldObjectBuffers* GetHeightFieldObjectBuffers() const
	{
		return HeightFieldObjectBuffers;
	}

	int32 NumObjectsInBuffer;
	int32 NumHeightFieldObjectsInBuffer;
	class FDistanceFieldObjectBuffers* ObjectBuffers[2];
	class FHeightFieldObjectBuffers* HeightFieldObjectBuffers;
	int ObjectBufferIndex;

	/** Stores the primitive and instance index of every entry in the object buffer. */
	TArray<FPrimitiveAndInstance> PrimitiveInstanceMapping;
	TArray<FPrimitiveSceneInfo*> HeightfieldPrimitives;

	/** Pending operations on the object buffers to be processed next frame. */
	TArray<FPrimitiveSceneInfo*> PendingAddOperations;
	TArray<FPrimitiveSceneInfo*> PendingThrottledOperations;
	TSet<FPrimitiveSceneInfo*> PendingUpdateOperations;
	TArray<FPrimitiveRemoveInfo> PendingRemoveOperations;
	TArray<FVector4> PrimitiveModifiedBounds[GDF_Num];

	TArray<FPrimitiveSceneInfo*> PendingHeightFieldAddOps;
	TArray<FPrimitiveSceneInfo*> PendingHeightFieldUpdateOps;
	TArray<FHeightFieldPrimitiveRemoveInfo> PendingHeightFieldRemoveOps;

	/** Used to detect atlas reallocations, since objects store UVs into the atlas and need to be updated when it changes. */
	int32 AtlasGeneration;
	int32 HeightFieldAtlasGeneration;
	int32 HFVisibilityAtlasGenerattion;

	bool bTrackAllPrimitives;
	bool bCanUse16BitObjectIndices;
};

/** Stores data for an allocation in the FIndirectLightingCache. */
class FIndirectLightingCacheBlock
{
public:

	FIndirectLightingCacheBlock() :
		MinTexel(FIntVector(0, 0, 0)),
		TexelSize(0),
		Min(FVector(0, 0, 0)),
		Size(FVector(0, 0, 0)),
		bHasEverBeenUpdated(false)
	{}

	FIntVector MinTexel;
	int32 TexelSize;
	FVector Min;
	FVector Size;
	bool bHasEverBeenUpdated;
};

/** Stores information about an indirect lighting cache block to be updated. */
class FBlockUpdateInfo
{
public:

	FBlockUpdateInfo(const FIndirectLightingCacheBlock& InBlock, FIndirectLightingCacheAllocation* InAllocation) :
		Block(InBlock),
		Allocation(InAllocation)
	{}

	FIndirectLightingCacheBlock Block;
	FIndirectLightingCacheAllocation* Allocation;
};

/** Information about the primitives that are attached together. */
class FAttachmentGroupSceneInfo
{
public:

	/** The parent primitive, which is the root of the attachment tree. */
	FPrimitiveSceneInfo* ParentSceneInfo;

	/** The primitives in the attachment group. */
	TArray<FPrimitiveSceneInfo*> Primitives;

	FAttachmentGroupSceneInfo() :
		ParentSceneInfo(nullptr)
	{}
};

struct FILCUpdatePrimTaskData
{
	FGraphEventRef TaskRef;
	TMap<FIntVector, FBlockUpdateInfo> OutBlocksToUpdate;
	TArray<FIndirectLightingCacheAllocation*> OutTransitionsOverTimeToUpdate;
	TArray<FPrimitiveSceneInfo*> OutPrimitivesToUpdateStaticMeshes;
};

/** 
 * Implements a volume texture atlas for caching indirect lighting on a per-object basis.
 * The indirect lighting is interpolated from Lightmass SH volume lighting samples.
 */
class FIndirectLightingCache : public FRenderResource
{
public:	

	/** true for the editor case where we want a better preview for object that have no valid lightmaps */
	FIndirectLightingCache(ERHIFeatureLevel::Type InFeatureLevel);

	// FRenderResource interface
	virtual void InitDynamicRHI();
	virtual void ReleaseDynamicRHI();

	/** Allocates a block in the volume texture atlas for a primitive. */
	FIndirectLightingCacheAllocation* AllocatePrimitive(const FPrimitiveSceneInfo* PrimitiveSceneInfo, bool bUnbuiltPreview);

	/** Releases the indirect lighting allocation for the given primitive. */
	void ReleasePrimitive(FPrimitiveComponentId PrimitiveId);

	FIndirectLightingCacheAllocation* FindPrimitiveAllocation(FPrimitiveComponentId PrimitiveId) const;	

	/** Updates indirect lighting in the cache based on visibility synchronously. */
	void UpdateCache(FScene* Scene, FSceneRenderer& Renderer, bool bAllowUnbuiltPreview);

	/** Starts a task to update the cache primitives.  Results and task ref returned in the FILCUpdatePrimTaskData structure */
	void StartUpdateCachePrimitivesTask(FScene* Scene, FSceneRenderer& Renderer, bool bAllowUnbuiltPreview, FILCUpdatePrimTaskData& OutTaskData);

	/** Wait on a previously started task and complete any block updates and debug draw */
	void FinalizeCacheUpdates(FScene* Scene, FSceneRenderer& Renderer, FILCUpdatePrimTaskData& TaskData);

	/** Force all primitive allocations to be re-interpolated. */
	void SetLightingCacheDirty(FScene* Scene, const FPrecomputedLightVolume* Volume);

	// Accessors
	FSceneRenderTargetItem& GetTexture0() { return Texture0->GetRenderTargetItem(); }
	FSceneRenderTargetItem& GetTexture1() { return Texture1->GetRenderTargetItem(); }
	FSceneRenderTargetItem& GetTexture2() { return Texture2->GetRenderTargetItem(); }

private:
	/** Internal helper to determine if indirect lighting is enabled at all */
	bool IndirectLightingAllowed(FScene* Scene, FSceneRenderer& Renderer) const;

	void ProcessPrimitiveUpdate(FScene* Scene, FViewInfo& View, int32 PrimitiveIndex, bool bAllowUnbuiltPreview, bool bAllowVolumeSample, TMap<FIntVector, FBlockUpdateInfo>& OutBlocksToUpdate, TArray<FIndirectLightingCacheAllocation*>& OutTransitionsOverTimeToUpdate, TArray<FPrimitiveSceneInfo*>& OutPrimitivesToUpdateStaticMeshes);

	/** Internal helper to perform the work of updating the cache primitives.  Can be done on any thread as a task */
	void UpdateCachePrimitivesInternal(FScene* Scene, FSceneRenderer& Renderer, bool bAllowUnbuiltPreview, TMap<FIntVector, FBlockUpdateInfo>& OutBlocksToUpdate, TArray<FIndirectLightingCacheAllocation*>& OutTransitionsOverTimeToUpdate, TArray<FPrimitiveSceneInfo*>& OutPrimitivesToUpdateStaticMeshes);

	/** Internal helper to perform blockupdates and transition updates on the results of UpdateCachePrimitivesInternal.  Must be on render thread. */
	void FinalizeUpdateInternal_RenderThread(FScene* Scene, FSceneRenderer& Renderer, TMap<FIntVector, FBlockUpdateInfo>& BlocksToUpdate, const TArray<FIndirectLightingCacheAllocation*>& TransitionsOverTimeToUpdate, TArray<FPrimitiveSceneInfo*>& PrimitivesToUpdateStaticMeshes);

	/** Internal helper which adds an entry to the update lists for this allocation, if needed (due to movement, etc). Returns true if the allocation was updated or will be udpated */
	bool UpdateCacheAllocation(
		const FBoxSphereBounds& Bounds, 
		int32 BlockSize,
		bool bPointSample,
		bool bUnbuiltPreview,
		FIndirectLightingCacheAllocation*& Allocation, 
		TMap<FIntVector, FBlockUpdateInfo>& BlocksToUpdate,
		TArray<FIndirectLightingCacheAllocation*>& TransitionsOverTimeToUpdate);	

	/** 
	 * Creates a new allocation if needed, caches the result in PrimitiveSceneInfo->IndirectLightingCacheAllocation, 
	 * And adds an entry to the update lists when an update is needed. 
	 */
	void UpdateCachePrimitive(
		const TMap<FPrimitiveComponentId, FAttachmentGroupSceneInfo>& AttachmentGroups,
		FPrimitiveSceneInfo* PrimitiveSceneInfo,
		bool bAllowUnbuiltPreview, 
		bool bAllowVolumeSample, 
		TMap<FIntVector, FBlockUpdateInfo>& BlocksToUpdate, 
		TArray<FIndirectLightingCacheAllocation*>& TransitionsOverTimeToUpdate,
		TArray<FPrimitiveSceneInfo*>& PrimitivesToUpdateStaticMeshes);

	/** Updates the contents of the volume texture blocks in BlocksToUpdate. */
	void UpdateBlocks(FScene* Scene, FViewInfo* DebugDrawingView, TMap<FIntVector, FBlockUpdateInfo>& BlocksToUpdate);

	/** Updates any outstanding transitions with a new delta time. */
	void UpdateTransitionsOverTime(const TArray<FIndirectLightingCacheAllocation*>& TransitionsOverTimeToUpdate, float DeltaWorldTime) const;

	/** Creates an allocation to be used outside the indirect lighting cache and a block to be used internally. */
	FIndirectLightingCacheAllocation* CreateAllocation(int32 BlockSize, const FBoxSphereBounds& Bounds, bool bPointSample, bool bUnbuiltPreview);	

	/** Block accessors. */
	FIndirectLightingCacheBlock& FindBlock(FIntVector TexelMin);
	const FIndirectLightingCacheBlock& FindBlock(FIntVector TexelMin) const;

	/** Block operations. */
	void DeallocateBlock(FIntVector Min, int32 Size);
	bool AllocateBlock(int32 Size, FIntVector& OutMin);

	/**
	 * Updates an allocation block in the cache, by re-interpolating values and uploading to the cache volume texture.
	 * @param DebugDrawingView can be 0
	 */
	void UpdateBlock(FScene* Scene, FViewInfo* DebugDrawingView, FBlockUpdateInfo& Block);

	/** Interpolates a single SH sample from all levels. */
	void InterpolatePoint(
		FScene* Scene, 
		const FIndirectLightingCacheBlock& Block,
		float& OutDirectionalShadowing, 
		FSHVectorRGB3& OutIncidentRadiance,
		FVector& OutSkyBentNormal);

	/** Interpolates SH samples for a block from all levels. */
	void InterpolateBlock(
		FScene* Scene, 
		const FIndirectLightingCacheBlock& Block, 
		TArray<float>& AccumulatedWeight, 
		TArray<FSHVectorRGB2>& AccumulatedIncidentRadiance);

	/** 
	 * Normalizes, adjusts for SH ringing, and encodes SH samples into a texture format.
	 * @param DebugDrawingView can be 0
	 */
	void EncodeBlock(
		FViewInfo* DebugDrawingView,
		const FIndirectLightingCacheBlock& Block, 
		const TArray<float>& AccumulatedWeight, 
		const TArray<FSHVectorRGB2>& AccumulatedIncidentRadiance,
		TArray<FFloat16Color>& Texture0Data,
		TArray<FFloat16Color>& Texture1Data,
		TArray<FFloat16Color>& Texture2Data		
	);

	/** Helper that calculates an effective world position min and size given a bounds. */
	void CalculateBlockPositionAndSize(const FBoxSphereBounds& Bounds, int32 TexelSize, FVector& OutMin, FVector& OutSize) const;

	/** Helper that calculates a scale and add to convert world space position into volume texture UVs for a given block. */
	void CalculateBlockScaleAndAdd(FIntVector InTexelMin, int32 AllocationTexelSize, FVector InMin, FVector InSize, FVector& OutScale, FVector& OutAdd, FVector& OutMinUV, FVector& OutMaxUV) const;

	/** true: next rendering we update all entries no matter if they are visible to avoid further hitches */
	bool bUpdateAllCacheEntries;

	/** Size of the volume texture cache. */
	int32 CacheSize;

	/** Volume textures that store SH indirect lighting, interpolated from Lightmass volume samples. */
	TRefCountPtr<IPooledRenderTarget> Texture0;
	TRefCountPtr<IPooledRenderTarget> Texture1;
	TRefCountPtr<IPooledRenderTarget> Texture2;

	/** Tracks the allocation state of the atlas. */
	TMap<FIntVector, FIndirectLightingCacheBlock> VolumeBlocks;

	/** Tracks used sections of the volume texture atlas. */
	FTextureLayout3d BlockAllocator;

	int32 NextPointId;

	/** Tracks primitive allocations by component, so that they persist across re-registers. */
	TMap<FPrimitiveComponentId, FIndirectLightingCacheAllocation*> PrimitiveAllocations;

	friend class FUpdateCachePrimitivesTask;
};

/**
 * Bounding information used to cull primitives in the scene.
 */
struct FPrimitiveBounds
{
	FBoxSphereBounds BoxSphereBounds;
	/** Square of the minimum draw distance for the primitive. */
	float MinDrawDistanceSq;
	/** Maximum draw distance for the primitive. */
	float MaxDrawDistance;
	/** Maximum cull distance for the primitive. This is only different from the MaxDrawDistance for HLOD.*/
	float MaxCullDistance;
};

/**
 * Precomputed primitive visibility ID.
 */
struct FPrimitiveVisibilityId
{
	/** Index in to the byte where precomputed occlusion data is stored. */
	int32 ByteIndex;
	/** Mast of the bit where precomputed occlusion data is stored. */
	uint8 BitMask;
};

/**
 * Flags that affect how primitives are occlusion culled.
 */
namespace EOcclusionFlags
{
	enum Type
	{
		/** No flags. */
		None = 0x0,
		/** Indicates the primitive can be occluded. */
		CanBeOccluded = 0x1,
		/** Allow the primitive to be batched with others to determine occlusion. */
		AllowApproximateOcclusion = 0x4,
		/** Indicates the primitive has a valid ID for precomputed visibility. */
		HasPrecomputedVisibility = 0x8,
		/** Indicates the primitive has a valid ID for precomputed visibility. */
		HasSubprimitiveQueries = 0x10,
	};
};

/** Velocity state for a single component. */
class FComponentVelocityData
{
public:

	FPrimitiveSceneInfo* PrimitiveSceneInfo;
	FMatrix LocalToWorld;
	FMatrix PreviousLocalToWorld;
	mutable uint64 LastFrameUsed;
	uint64 LastFrameUpdated;
	bool bPreviousLocalToWorldValid = false;
};

/**
 * Tracks primitive transforms so they will be persistent across rendering state recreates.
 */
class FSceneVelocityData
{
public:

	/**
	 * Must be called once per frame, even when there are multiple BeginDrawingViewports.
	 */
	void StartFrame(FScene* Scene);

	/** 
	 * Looks up the PreviousLocalToWorld state for the given component.  Returns false if none is found (the primitive has never been moved). 
	 */
	inline bool GetComponentPreviousLocalToWorld(FPrimitiveComponentId PrimitiveComponentId, FMatrix& OutPreviousLocalToWorld) const
	{
		const FComponentVelocityData* VelocityData = ComponentData.Find(PrimitiveComponentId);

		if (VelocityData)
		{
			check(VelocityData->bPreviousLocalToWorldValid);
			VelocityData->LastFrameUsed = InternalFrameIndex;
			OutPreviousLocalToWorld = VelocityData->PreviousLocalToWorld;
			return true;
		}

		return false;
	}

	/** 
	 * Updates a primitives current LocalToWorld state.
	 */
	void UpdateTransform(FPrimitiveSceneInfo* PrimitiveSceneInfo, const FMatrix& LocalToWorld, const FMatrix& PreviousLocalToWorld)
	{
		check(PrimitiveSceneInfo->Proxy->IsMovable() || PrimitiveSceneInfo->Proxy->IsUsingWPOMaterial());

		FComponentVelocityData& VelocityData = ComponentData.FindOrAdd(PrimitiveSceneInfo->PrimitiveComponentId);
		VelocityData.LocalToWorld = LocalToWorld;
		VelocityData.LastFrameUsed = InternalFrameIndex;
		VelocityData.LastFrameUpdated = InternalFrameIndex;
		VelocityData.PrimitiveSceneInfo = PrimitiveSceneInfo;

		// If this transform state is newly added, use the passed in PreviousLocalToWorld for this frame
		if (!VelocityData.bPreviousLocalToWorldValid)
		{
			VelocityData.PreviousLocalToWorld = PreviousLocalToWorld;
			VelocityData.bPreviousLocalToWorldValid = true;
		}
	}

	void RemoveFromScene(FPrimitiveComponentId PrimitiveComponentId)
	{
		FComponentVelocityData* VelocityData = ComponentData.Find(PrimitiveComponentId);

		if (VelocityData)
		{
			VelocityData->PrimitiveSceneInfo = nullptr;
		}
	}

	/** 
	 * Overrides a primitive's previous LocalToWorld matrix for this frame only
	 */
	void OverridePreviousTransform(FPrimitiveComponentId PrimitiveComponentId, const FMatrix& PreviousLocalToWorld)
	{
		FComponentVelocityData* VelocityData = ComponentData.Find(PrimitiveComponentId);
		if (VelocityData)
		{
			VelocityData->PreviousLocalToWorld = PreviousLocalToWorld;
			VelocityData->bPreviousLocalToWorldValid = true;
		}
	}

	void ApplyOffset(FVector Offset)
	{
		for (TMap<FPrimitiveComponentId, FComponentVelocityData>::TIterator It(ComponentData); It; ++It)
		{
			FComponentVelocityData& VelocityData = It.Value();
			VelocityData.LocalToWorld.SetOrigin(VelocityData.LocalToWorld.GetOrigin() + Offset);
			VelocityData.PreviousLocalToWorld.SetOrigin(VelocityData.PreviousLocalToWorld.GetOrigin() + Offset);
		}
	}

private:

	uint64 InternalFrameIndex = 0;
	TMap<FPrimitiveComponentId, FComponentVelocityData> ComponentData;
};

class FLODSceneTree
{
public:
	FLODSceneTree(FScene* InScene)
		: Scene(InScene)
	{
	}

	/** Information about the primitives that are attached together. */
	struct FLODSceneNode
	{
		/** Children scene infos. */
		TArray<FPrimitiveSceneInfo*> ChildrenSceneInfos;

		/** The primitive. */
		FPrimitiveSceneInfo* SceneInfo;

		FLODSceneNode()
			: SceneInfo(nullptr)
		{
		}

		void AddChild(FPrimitiveSceneInfo* NewChild)
		{
			if (NewChild)
			{
				ChildrenSceneInfos.AddUnique(NewChild);
			}
		}

		void RemoveChild(FPrimitiveSceneInfo* ChildToDelete)
		{
			if (ChildToDelete)
			{
				ChildrenSceneInfos.Remove(ChildToDelete);
			}
		}
	};

	void AddChildNode(FPrimitiveComponentId ParentId, FPrimitiveSceneInfo* ChildSceneInfo);
	void RemoveChildNode(FPrimitiveComponentId ParentId, FPrimitiveSceneInfo* ChildSceneInfo);

	void UpdateNodeSceneInfo(FPrimitiveComponentId NodeId, FPrimitiveSceneInfo* SceneInfo);
	void UpdateVisibilityStates(FViewInfo& View);

	void ClearVisibilityState(FViewInfo& View);

	bool IsActive() const { return (SceneNodes.Num() > 0); }

private:

	/** Scene this Tree belong to */
	FScene* Scene;

	/** The LOD groups in the scene.  The map key is the current primitive who has children. */
	TMap<FPrimitiveComponentId, FLODSceneNode> SceneNodes;

	/** Recursive state updates */
	void ApplyNodeFadingToChildren(FSceneViewState* ViewState, FLODSceneNode& Node, FHLODSceneNodeVisibilityState& NodeVisibility, const bool bIsFading, const bool bIsFadingOut);
	void HideNodeChildren(FSceneViewState* ViewState, FLODSceneNode& Node);
};

class FCachedShadowMapData
{
public:
	FWholeSceneProjectedShadowInitializer Initializer;
	FShadowMapRenderTargetsRefCounted ShadowMap;
	float LastUsedTime;
	bool bCachedShadowMapHasPrimitives;

	FCachedShadowMapData(const FWholeSceneProjectedShadowInitializer& InInitializer, float InLastUsedTime) :
		Initializer(InInitializer),
		LastUsedTime(InLastUsedTime),
		bCachedShadowMapHasPrimitives(true)
	{}
};

#if WITH_EDITOR
class FPixelInspectorData
{
public:
	FPixelInspectorData();

	void InitializeBuffers(FRenderTarget* BufferFinalColor, FRenderTarget* BufferSceneColor, FRenderTarget* BufferDepth, FRenderTarget* BufferHDR, FRenderTarget* BufferA, FRenderTarget* BufferBCDEF, int32 bufferIndex);

	bool AddPixelInspectorRequest(FPixelInspectorRequest *PixelInspectorRequest);

	//Hold the buffer array
	TMap<FVector2D, FPixelInspectorRequest *> Requests;

	FRenderTarget* RenderTargetBufferDepth[2];
	FRenderTarget* RenderTargetBufferFinalColor[2];
	FRenderTarget* RenderTargetBufferHDR[2];
	FRenderTarget* RenderTargetBufferSceneColor[2];
	FRenderTarget* RenderTargetBufferA[2];
	FRenderTarget* RenderTargetBufferBCDEF[2];
};
#endif //WITH_EDITOR

class FPersistentUniformBuffers
{
public:
	FPersistentUniformBuffers()
		: CachedView(nullptr)
	{
	}

	void Initialize();
	void Clear();

	/** Compares the provided view against the cached view and updates the view uniform buffer
	 *  if the views differ. Returns whether uniform buffer was updated.
	 *  If bShouldWaitForPersistentViewUniformBufferExtensionsJobs == true, it calls Extension->BeginRenderView() which
	 *  waits on the potential jobs dispatched in Extension->PrepareView(). Currently it is false only in FMobileSceneRenderer::InitViews()
	 */
	bool UpdateViewUniformBuffer(const FViewInfo& View, bool bShouldWaitForPersistentViewUniformBufferExtensionsJobs = true);

	/** Updates view uniform buffer and invalidates the internally cached view instance. */
	void UpdateViewUniformBufferImmediate(const FViewUniformShaderParameters& Parameters);

	void InvalidateCachedView();

	const FViewInfo& GetInstancedView(const FViewInfo& View)
	{
		// When drawing the left eye in a stereo scene, copy the right eye view values into the instanced view uniform buffer.
		const EStereoscopicPass StereoPassIndex = IStereoRendering::IsStereoEyeView(View) ? eSSP_RIGHT_EYE : eSSP_FULL;

		return static_cast<const FViewInfo&>(View.Family->GetStereoEyeView(StereoPassIndex));
	}

	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
	TUniformBufferRef<FInstancedViewUniformShaderParameters> InstancedViewUniformBuffer;
	TUniformBufferRef<FReflectionCaptureShaderData> ReflectionCaptureUniformBuffer;
	TUniformBufferRef<FViewUniformShaderParameters> CSMShadowDepthViewUniformBuffer;
	TUniformBufferRef<FShadowDepthPassUniformParameters> CSMShadowDepthPassUniformBuffer;
	TUniformBufferRef<FViewUniformShaderParameters> VoxelizeVolumeViewUniformBuffer;
	TUniformBufferRef<FViewUniformShaderParameters> CustomDepthViewUniformBuffer;
	TUniformBufferRef<FInstancedViewUniformShaderParameters> InstancedCustomDepthViewUniformBuffer;
	TUniformBufferRef<FViewUniformShaderParameters> VirtualTextureViewUniformBuffer;

	TUniformBufferRef<FMobileBasePassUniformParameters> MobileOpaqueBasePassUniformBuffer;
	TUniformBufferRef<FMobileBasePassUniformParameters> MobileCSMOpaqueBasePassUniformBuffer;
	TUniformBufferRef<FMobileBasePassUniformParameters> MobileTranslucentBasePassUniformBuffer;
	TUniformBufferRef<FMobileShadowDepthPassUniformParameters> MobileCSMShadowDepthPassUniformBuffer;
	/** Mobile Directional Lighting uniform buffers, one for each lighting channel 
	  * The first is used for primitives with no lighting channels set.
	  */
	TUniformBufferRef<FMobileDirectionalLightShaderParameters> MobileDirectionalLightUniformBuffers[NUM_LIGHTING_CHANNELS+1];
	TUniformBufferRef<FMobileReflectionCaptureShaderParameters> MobileSkyReflectionUniformBuffer;

	// View from which ViewUniformBuffer was last updated.
	const FViewInfo* CachedView;
};

#if RHI_RAYTRACING
struct FMeshComputeDispatchCommand
{
	FMeshDrawShaderBindings ShaderBindings;
	TShaderRef<class FRayTracingDynamicGeometryConverterCS> MaterialShader;

	uint32 NumMaxVertices;
	FRWBuffer* TargetBuffer;
};
#endif

/** 
 * Renderer scene which is private to the renderer module.
 * Ordinarily this is the renderer version of a UWorld, but an FScene can be created for previewing in editors which don't have a UWorld as well.
 * The scene stores renderer state that is independent of any view or frame, with the primary actions being adding and removing of primitives and lights.
 */
class FScene : public FSceneInterface
{
public:

	/** An optional world associated with the scene. */
	UWorld* World;

	/** An optional FX system associated with the scene. */
	class FFXSystemInterface* FXSystem;

	FPersistentUniformBuffers UniformBuffers;

	/** Instancing state buckets.  These are stored on the scene as they are precomputed at FPrimitiveSceneInfo::AddToScene time. */
	FCriticalSection CachedMeshDrawCommandLock[EMeshPass::Num];
	FStateBucketMap CachedMeshDrawCommandStateBuckets[EMeshPass::Num];
	FCachedPassMeshDrawList CachedDrawLists[EMeshPass::Num];

#if RHI_RAYTRACING
	FCachedRayTracingMeshCommandStorage CachedRayTracingMeshCommands;
#endif

	/**
	 * The following arrays are densely packed primitive data needed by various
	 * rendering passes. PrimitiveSceneInfo->PackedIndex maintains the index
	 * where data is stored in these arrays for a given primitive.
	 */

	/** Packed array of primitives in the scene. */
	TArray<FPrimitiveSceneInfo*> Primitives;
	/** Packed array of all transforms in the scene. */
	TArray<FMatrix> PrimitiveTransforms;
	/** Packed array of primitive scene proxies in the scene. */
	TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
	/** Packed array of primitive bounds. */
	TArray<FPrimitiveBounds> PrimitiveBounds;
	/** Packed array of primitive flags. */
	TArray<FPrimitiveFlagsCompact> PrimitiveFlagsCompact;
	/** Packed array of precomputed primitive visibility IDs. */
	TArray<FPrimitiveVisibilityId> PrimitiveVisibilityIds;
	/** Packed array of primitive occlusion flags. See EOcclusionFlags. */
	TArray<uint8> PrimitiveOcclusionFlags;
	/** Packed array of primitive occlusion bounds. */
	TArray<FBoxSphereBounds> PrimitiveOcclusionBounds;
	/** Packed array of primitive components associated with the primitive. */
	TArray<FPrimitiveComponentId> PrimitiveComponentIds;
	/** Packed array of runtime virtual texture flags. */
	TArray<FPrimitiveVirtualTextureFlags> PrimitiveVirtualTextureFlags;
	/** Packed array of runtime virtual texture lod info. */
	TArray<FPrimitiveVirtualTextureLodInfo> PrimitiveVirtualTextureLod;

	TBitArray<> PrimitivesNeedingStaticMeshUpdate;
	TSet<FPrimitiveSceneInfo*> PrimitivesNeedingStaticMeshUpdateWithoutVisibilityCheck;

	struct FTypeOffsetTableEntry
	{
		FTypeOffsetTableEntry(SIZE_T InPrimitiveSceneProxyType, uint32 InOffset) : PrimitiveSceneProxyType(InPrimitiveSceneProxyType), Offset(InOffset) {}
		SIZE_T PrimitiveSceneProxyType;
		uint32 Offset; //(e.g. prefix sum where the next type starts)
	};
	/* During insertion and deletion, used to skip large chunks of items of the same type */
	TArray<FTypeOffsetTableEntry> TypeOffsetTable;

	/** The lights in the scene. */
	TSparseArray<FLightSceneInfoCompact> Lights;

	/** 
	 * Lights in the scene which are invisible, but still needed by the editor for previewing. 
	 * Lights in this array cannot be in the Lights array.  They also are not fully set up, as AddLightSceneInfo_RenderThread is not called for them.
	 */
	TSparseArray<FLightSceneInfoCompact> InvisibleLights;

	/** Shadow casting lights that couldn't get a shadowmap channel assigned and therefore won't have valid dynamic shadows, forward renderer only. */
	TArray<FName> OverflowingDynamicShadowedLights;

	/** Early Z pass mode. */
	EDepthDrawingMode EarlyZPassMode;

	/** Early Z pass movable. */
	bool bEarlyZPassMovable;

	/** Default base pass depth stencil access. */
	FExclusiveDepthStencil::Type DefaultBasePassDepthStencilAccess;

	/** Default base pass depth stencil access used to cache mesh draw commands. */
	FExclusiveDepthStencil::Type CachedDefaultBasePassDepthStencilAccess;

	/** True if a change to SkyLight / Lighting has occurred that requires static draw lists to be updated. */
	bool bScenesPrimitivesNeedStaticMeshElementUpdate;

	/** True if a change to the scene that requires to invalidate the path tracer buffers has happened. */
	bool bPathTracingNeedsInvalidation;

	/** The scene's sky light, if any. */
	FSkyLightSceneProxy* SkyLight;

	/** Contains the sky env map irradiance as spherical harmonics. */
	FRWBufferStructured SkyIrradianceEnvironmentMap;

	/** The SkyView LUT used when rendering sky material sampling this lut into the realtime capture sky env map. It must be generated at the skylight position*/
	TRefCountPtr<IPooledRenderTarget> RealTimeReflectionCaptureSkyAtmosphereViewLutTexture;

	/** The Camera 360 AP is used when rendering sky material sampling this lut or volumetric clouds into the realtime capture sky env map. It must be generated at the skylight position*/
	TRefCountPtr<IPooledRenderTarget> RealTimeReflectionCaptureCamera360APLutTexture;

	/** If sky light bRealTimeCaptureEnabled is true, used to render the sky env map (sky, sky dome mesh or clouds). */
	TRefCountPtr<IPooledRenderTarget> CapturedSkyRenderTarget;	// Needs to be a IPooledRenderTarget because it must be created before the View uniform buffer is created.

	/** These store the result of the sky env map GGX specular convolution. */
	TRefCountPtr<IPooledRenderTarget> ConvolvedSkyRenderTarget[2];

	/** The index of the ConvolvedSkyRenderTarget to use when rendering meshes. -1 when not initialised. */
	int32 ConvolvedSkyRenderTargetReadyIndex;

	/** We always enforce a complete one the first frame even with time slicing for correct start up lighting.*/
	enum class ERealTimeSlicedReflectionCaptureFirstFrameState
	{
		INIT = 0,
		FIRST_FRAME = 1,
		BEYOND_FIRST_FRAME = 2,
	} RealTimeSlicedReflectionCaptureFirstFrameState;

	/** The current progress of the real time reflection capture when time sliced. */
	int32 RealTimeSlicedReflectionCaptureState;

	/** Cache of Frame Number, used to detect first viewfamily */
	uint64 RealTimeSlicedReflectionCaptureFrameNumber;

	/**
	 * The path tracer uses its own representation of the skylight. These textures
	 * are updated lazily by the path tracer when missing. Any code that modifies
	 * the skylight appearance should simplify reset these pointers.
	 */
	TRefCountPtr<IPooledRenderTarget> PathTracingSkylightTexture;
	TRefCountPtr<IPooledRenderTarget> PathTracingSkylightPdf;

	/** Used to track the order that skylights were enabled in. */
	TArray<FSkyLightSceneProxy*> SkyLightStack;

	/** The directional light to use for simple dynamic lighting, if any. */
	FLightSceneInfo* SimpleDirectionalLight;

	/** For the mobile renderer, the first directional light in each lighting channel. */
	FLightSceneInfo* MobileDirectionalLights[NUM_LIGHTING_CHANNELS];

	/** The light sources for atmospheric effects, if any. */
	FLightSceneInfo* AtmosphereLights[NUM_ATMOSPHERE_LIGHTS];

	/** The decals in the scene. */
	TSparseArray<FDeferredDecalProxy*> Decals;

	/** Potential capsule shadow casters registered to the scene. */
	TArray<FPrimitiveSceneInfo*> DynamicIndirectCasterPrimitives; 

	TArray<class FPlanarReflectionSceneProxy*> PlanarReflections;
	TArray<class UPlanarReflectionComponent*> PlanarReflections_GameThread;

	/** State needed for the reflection environment feature. */
	FReflectionEnvironmentSceneData ReflectionSceneData;

	/** 
	 * Precomputed lighting volumes in the scene, used for interpolating dynamic object lighting.
	 * These are typically one per streaming level and they store volume lighting samples computed by Lightmass. 
	 */
	TArray<const FPrecomputedLightVolume*> PrecomputedLightVolumes;

	/** Interpolates and caches indirect lighting for dynamic objects. */
	FIndirectLightingCache IndirectLightingCache;

	FVolumetricLightmapSceneData VolumetricLightmapSceneData;
	
	FGPUScene GPUScene;

	/** Distance field object scene data. */
	FDistanceFieldSceneData DistanceFieldSceneData;

	/** Map from light id to the cached shadowmap data for that light. */
	TMap<int32, FCachedShadowMapData> CachedShadowMaps;

	TRefCountPtr<IPooledRenderTarget> PreShadowCacheDepthZ;

	/** Preshadows that are currently cached in the PreshadowCache render target. */
	TArray<TRefCountPtr<FProjectedShadowInfo> > CachedPreshadows;

	/** Texture layout that tracks current allocations in the PreshadowCache render target. */
	FTextureLayout PreshadowCacheLayout;

	/** The static meshes in the scene. */
	TSparseArray<FStaticMeshBatch*> StaticMeshes;

	/** The exponential fog components in the scene. */
	TArray<FExponentialHeightFogSceneInfo> ExponentialFogs;

	/** The atmospheric fog components in the scene. */
	FAtmosphericFogSceneInfo* AtmosphericFog;

	/** The sky/atmosphere components of the scene. */
	FSkyAtmosphereRenderSceneInfo* SkyAtmosphere;

	/** Used to track the order that skylights were enabled in. */
	TArray<FSkyAtmosphereSceneProxy*> SkyAtmosphereStack;

	/** The sky/atmosphere components of the scene. */
	FVolumetricCloudRenderSceneInfo* VolumetricCloud;

	/** Used to track the order that skylights were enabled in. */
	TArray<FVolumetricCloudSceneProxy*> VolumetricCloudStack;

	/** Global Field Manager */
	class FPhysicsFieldSceneProxy* PhysicsField = nullptr;

	/** The wind sources in the scene. */
	TArray<class FWindSourceSceneProxy*> WindSources;

	/** Wind source components, tracked so the game thread can also access wind parameters */
	TArray<UWindDirectionalSourceComponent*> WindComponents_GameThread;

	/** SpeedTree wind objects in the scene. FLocalVertexFactoryShaderParametersBase needs to lookup by FVertexFactory, but wind objects are per tree (i.e. per UStaticMesh)*/
	TMap<const UStaticMesh*, struct FSpeedTreeWindComputation*> SpeedTreeWindComputationMap;
	TMap<FVertexFactory*, const UStaticMesh*> SpeedTreeVertexFactoryMap;

	/** The attachment groups in the scene.  The map key is the attachment group's root primitive. */
	TMap<FPrimitiveComponentId,FAttachmentGroupSceneInfo> AttachmentGroups;

	/** Precomputed visibility data for the scene. */
	const FPrecomputedVisibilityHandler* PrecomputedVisibilityHandler;

	/** An octree containing the shadow-casting local lights in the scene. */
	FSceneLightOctree LocalShadowCastingLightOctree;
	/** An array containing IDs of shadow-casting directional lights in the scene. */
	TArray<int32> DirectionalShadowCastingLightIDs;

	/** An octree containing the primitives in the scene. */
	FScenePrimitiveOctree PrimitiveOctree;

	/** Indicates whether this scene requires hit proxy rendering. */
	bool bRequiresHitProxies;

	/** Whether this is an editor scene. */
	bool bIsEditorScene;

	/** Set by the rendering thread to signal to the game thread that the scene needs a static lighting build. */
	volatile mutable int32 NumUncachedStaticLightingInteractions;

	volatile mutable int32 NumUnbuiltReflectionCaptures;

	/** Track numbers of various lights types on mobile, used to show warnings for disabled shader permutations. */
	int32 NumMobileStaticAndCSMLights_RenderThread;
	int32 NumMobileMovableDirectionalLights_RenderThread;

	/** Cached shadow atlas size for mobile, since if the resolution is changed we have to update the movable point lights uniform buffer. */
	FIntPoint MobileWholeSceneShadowAtlasSize;

	FSceneVelocityData VelocityData;

	/** GPU Skinning cache, if enabled */
	class FGPUSkinCache* GPUSkinCache;

	/** Uniform buffers for parameter collections with the corresponding Ids. */
	TMap<FGuid, FUniformBufferRHIRef> ParameterCollections;

	/** LOD Tree Holder for massive LOD system */
	FLODSceneTree SceneLODHierarchy;

	/** The runtime virtual textures in the scene. */
	TSparseArray<FRuntimeVirtualTextureSceneProxy*> RuntimeVirtualTextures;

	/** Mask used to determine whether primitives that draw to a runtime virtual texture should also be drawn in the main pass. */
	uint8 RuntimeVirtualTexturePrimitiveHideMaskEditor;
	uint8 RuntimeVirtualTexturePrimitiveHideMaskGame;

	float DefaultMaxDistanceFieldOcclusionDistance;

	float GlobalDistanceFieldViewDistance;

	float DynamicIndirectShadowsSelfShadowingIntensity;

	const FReadOnlyCVARCache& ReadOnlyCVARCache;

	TAllocatorFixedSizeFreeList<sizeof(FLightPrimitiveInteraction), 16384 / sizeof(FLightPrimitiveInteraction)> LightPrimitiveInteractionAllocator;

#if WITH_EDITOR
	/** Editor Pixel inspector */
	FPixelInspectorData PixelInspectorData;
#endif //WITH_EDITOR

#if RHI_RAYTRACING
	class FRayTracingDynamicGeometryCollection* RayTracingDynamicGeometryCollection;
	FHaltonSequence HaltonSequence;
	FHaltonPrimesResource HaltonPrimesResource;
#endif

	/** Initialization constructor. */
	FScene(UWorld* InWorld, bool bInRequiresHitProxies,bool bInIsEditorScene, bool bCreateFXSystem, ERHIFeatureLevel::Type InFeatureLevel);

	virtual ~FScene();

	// FSceneInterface interface.
	virtual void AddPrimitive(UPrimitiveComponent* Primitive) override;
	virtual void RemovePrimitive(UPrimitiveComponent* Primitive) override;
	virtual void ReleasePrimitive(UPrimitiveComponent* Primitive) override;
	virtual void UpdateAllPrimitiveSceneInfos(FRHICommandListImmediate& RHICmdList, bool bAsyncCreateLPIs = false) override;
	virtual void UpdatePrimitiveTransform(UPrimitiveComponent* Primitive) override;
	virtual void UpdatePrimitiveAttachment(UPrimitiveComponent* Primitive) override;
	virtual void UpdateCustomPrimitiveData(UPrimitiveComponent* Primitive) override;
	virtual void UpdatePrimitiveDistanceFieldSceneData_GameThread(UPrimitiveComponent* Primitive) override;
	virtual FPrimitiveSceneInfo* GetPrimitiveSceneInfo(int32 PrimitiveIndex) override;
	virtual bool GetPreviousLocalToWorld(const FPrimitiveSceneInfo* PrimitiveSceneInfo, FMatrix& OutPreviousLocalToWorld) const override;
	virtual void AddLight(ULightComponent* Light) override;
	virtual void RemoveLight(ULightComponent* Light) override;
	virtual void AddInvisibleLight(ULightComponent* Light) override;
	virtual void SetSkyLight(FSkyLightSceneProxy* Light) override;
	virtual void DisableSkyLight(FSkyLightSceneProxy* Light) override;
	virtual bool HasSkyLightRequiringLightingBuild() const override;
	virtual bool HasAtmosphereLightRequiringLightingBuild() const override;
	virtual void AddDecal(UDecalComponent* Component) override;
	virtual void RemoveDecal(UDecalComponent* Component) override;
	virtual void UpdateDecalTransform(UDecalComponent* Decal) override;
	virtual void UpdateDecalFadeOutTime(UDecalComponent* Decal) override;
	virtual void UpdateDecalFadeInTime(UDecalComponent* Decal) override;
	virtual void AddReflectionCapture(UReflectionCaptureComponent* Component) override;
	virtual void RemoveReflectionCapture(UReflectionCaptureComponent* Component) override;
	virtual void GetReflectionCaptureData(UReflectionCaptureComponent* Component, class FReflectionCaptureData& OutCaptureData) override;
	virtual void UpdateReflectionCaptureTransform(UReflectionCaptureComponent* Component) override;
	virtual void ReleaseReflectionCubemap(UReflectionCaptureComponent* CaptureComponent) override;
	virtual void AddPlanarReflection(class UPlanarReflectionComponent* Component) override;
	virtual void RemovePlanarReflection(UPlanarReflectionComponent* Component) override;
	virtual void UpdatePlanarReflectionTransform(UPlanarReflectionComponent* Component) override;
	virtual void UpdateSceneCaptureContents(class USceneCaptureComponent2D* CaptureComponent) override;
	virtual void UpdateSceneCaptureContents(class USceneCaptureComponentCube* CaptureComponent) override;
	virtual void UpdatePlanarReflectionContents(UPlanarReflectionComponent* CaptureComponent, FSceneRenderer& MainSceneRenderer) override;
	virtual void AllocateReflectionCaptures(const TArray<UReflectionCaptureComponent*>& NewCaptures, const TCHAR* CaptureReason, bool bVerifyOnlyCapturing, bool bCapturingForMobile) override;
	virtual void UpdateSkyCaptureContents(const USkyLightComponent* CaptureComponent, bool bCaptureEmissiveOnly, UTextureCube* SourceCubemap, FTexture* OutProcessedTexture, float& OutAverageBrightness, FSHVectorRGB3& OutIrradianceEnvironmentMap, TArray<FFloat16Color>* OutRadianceMap) override; 
	virtual void AllocateAndCaptureFrameSkyEnvMap(FRDGBuilder& GraphBuilder, FSceneRenderer& SceneRenderer, FViewInfo& MainView, bool bShouldRenderSkyAtmosphere, bool bShouldRenderVolumetricCloud) override;
	virtual void ValidateSkyLightRealTimeCapture(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorTexture) override;
	virtual void AddPrecomputedLightVolume(const class FPrecomputedLightVolume* Volume) override;
	virtual void RemovePrecomputedLightVolume(const class FPrecomputedLightVolume* Volume) override;
	virtual bool HasPrecomputedVolumetricLightmap_RenderThread() const override;
	virtual void AddPrecomputedVolumetricLightmap(const class FPrecomputedVolumetricLightmap* Volume, bool bIsPersistentLevel) override;
	virtual void RemovePrecomputedVolumetricLightmap(const class FPrecomputedVolumetricLightmap* Volume) override;
	virtual void AddRuntimeVirtualTexture(class URuntimeVirtualTextureComponent* Component) override;
	virtual void RemoveRuntimeVirtualTexture(class URuntimeVirtualTextureComponent* Component) override;
	virtual void GetRuntimeVirtualTextureHidePrimitiveMask(uint8& bHideMaskEditor, uint8& bHideMaskGame) const override;
	virtual void InvalidateRuntimeVirtualTexture(class URuntimeVirtualTextureComponent* Component, FBoxSphereBounds const& WorldBounds) override;
	virtual void GetPrimitiveUniformShaderParameters_RenderThread(const FPrimitiveSceneInfo* PrimitiveSceneInfo, bool& bHasPrecomputedVolumetricLightmap, FMatrix& PreviousLocalToWorld, int32& SingleCaptureIndex, bool& bOutputVelocity) const override;
	virtual void UpdateLightTransform(ULightComponent* Light) override;
	virtual void UpdateLightColorAndBrightness(ULightComponent* Light) override;
	virtual void AddExponentialHeightFog(UExponentialHeightFogComponent* FogComponent) override;
	virtual void RemoveExponentialHeightFog(UExponentialHeightFogComponent* FogComponent) override;
	virtual bool HasAnyExponentialHeightFog() const override;

	virtual void AddSkyAtmosphere(FSkyAtmosphereSceneProxy* SkyAtmosphereSceneProxy, bool bStaticLightingBuilt) override;
	virtual void RemoveSkyAtmosphere(FSkyAtmosphereSceneProxy* SkyAtmosphereSceneProxy) override;
	virtual FSkyAtmosphereRenderSceneInfo* GetSkyAtmosphereSceneInfo() override { return SkyAtmosphere; }
	virtual const FSkyAtmosphereRenderSceneInfo* GetSkyAtmosphereSceneInfo() const override { return SkyAtmosphere; }

	virtual void SetPhysicsField(class FPhysicsFieldSceneProxy* PhysicsFieldSceneProxy) override;
	virtual void ResetPhysicsField() override;
	virtual void UpdatePhysicsField(FRHICommandListImmediate& RHICmdList, FViewInfo& View) override;

	virtual void AddVolumetricCloud(FVolumetricCloudSceneProxy* VolumetricCloudSceneProxy) override;
	virtual void RemoveVolumetricCloud(FVolumetricCloudSceneProxy* VolumetricCloudSceneProxy) override;
	virtual FVolumetricCloudRenderSceneInfo* GetVolumetricCloudSceneInfo() override { return VolumetricCloud; }
	virtual const FVolumetricCloudRenderSceneInfo* GetVolumetricCloudSceneInfo() const override { return VolumetricCloud; }

	virtual void AddWindSource(UWindDirectionalSourceComponent* WindComponent) override;
	virtual void RemoveWindSource(UWindDirectionalSourceComponent* WindComponent) override;
	virtual void UpdateWindSource(UWindDirectionalSourceComponent* WindComponent) override;
	virtual const TArray<FWindSourceSceneProxy*>& GetWindSources_RenderThread() const override;
	virtual void GetWindParameters(const FVector& Position, FVector& OutDirection, float& OutSpeed, float& OutMinGustAmt, float& OutMaxGustAmt) const override;
	virtual void GetWindParameters_GameThread(const FVector& Position, FVector& OutDirection, float& OutSpeed, float& OutMinGustAmt, float& OutMaxGustAmt) const override;
	virtual void GetDirectionalWindParameters(FVector& OutDirection, float& OutSpeed, float& OutMinGustAmt, float& OutMaxGustAmt) const override;
	virtual void AddSpeedTreeWind(FVertexFactory* VertexFactory, const UStaticMesh* StaticMesh) override;
	virtual void RemoveSpeedTreeWind_RenderThread(FVertexFactory* VertexFactory, const UStaticMesh* StaticMesh) override;
	virtual void UpdateSpeedTreeWind(double CurrentTime) override;
	virtual FRHIUniformBuffer* GetSpeedTreeUniformBuffer(const FVertexFactory* VertexFactory) const override;
	virtual void DumpUnbuiltLightInteractions( FOutputDevice& Ar ) const override;
	virtual void UpdateParameterCollections(const TArray<FMaterialParameterCollectionInstanceResource*>& InParameterCollections) override;

	/** Determines whether the scene has atmospheric fog and sun light. */
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bool HasAtmosphericFog() const
	{
		return (AtmosphericFog != NULL); // Use default value when Sun Light is not existing
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	bool HasSkyAtmosphere() const
	{
		return (SkyAtmosphere != NULL);
	}
	bool HasVolumetricCloud() const
	{
		return (VolumetricCloud != NULL);
	}

	bool IsSecondAtmosphereLightEnabled()
	{
		// If the second light is not null then we enable the second light.
		// We do not do any light1 to light0 remapping if light0 is null.
		return AtmosphereLights[1] != nullptr;
	}

	// Reset all the light to default state "not being affected by atmosphere". Should only be called from render side.
	void ResetAtmosphereLightsProperties();

	/**
	 * Retrieves the lights interacting with the passed in primitive and adds them to the out array.
	 *
	 * @param	Primitive				Primitive to retrieve interacting lights for
	 * @param	RelevantLights	[out]	Array of lights interacting with primitive
	 */
	virtual void GetRelevantLights( UPrimitiveComponent* Primitive, TArray<const ULightComponent*>* RelevantLights ) const override;

	/** Sets the precomputed visibility handler for the scene, or NULL to clear the current one. */
	virtual void SetPrecomputedVisibility(const FPrecomputedVisibilityHandler* InPrecomputedVisibilityHandler) override;

	/** Updates all static draw lists. */
	virtual void UpdateStaticDrawLists() override;

	/** Update render states that possibly cached inside renderer, like mesh draw commands. More lightweight than re-registering the scene proxy. */
	virtual void UpdateCachedRenderStates(FPrimitiveSceneProxy* SceneProxy) override;

	virtual void Release() override;
	virtual UWorld* GetWorld() const override { return World; }

	/** Finds the closest reflection capture to a point in space. */
	const FReflectionCaptureProxy* FindClosestReflectionCapture(FVector Position) const;

	const class FPlanarReflectionSceneProxy* FindClosestPlanarReflection(const FBoxSphereBounds& Bounds) const;

	const class FPlanarReflectionSceneProxy* GetForwardPassGlobalPlanarReflection() const;

	void FindClosestReflectionCaptures(FVector Position, const FReflectionCaptureProxy* (&SortedByDistanceOUT)[FPrimitiveSceneInfo::MaxCachedReflectionCaptureProxies]) const;

	int64 GetCachedWholeSceneShadowMapsSize() const;

	void UpdateEarlyZPassMode();

	/**
	 * Marks static mesh elements as needing an update if necessary.
	 */
	void ConditionalMarkStaticMeshElementsForUpdate();

	/**
	 * @return		true if hit proxies should be rendered in this scene.
	 */
	virtual bool RequiresHitProxies() const override;

	SIZE_T GetSizeBytes() const;

	/**
	* Return the scene to be used for rendering
	*/
	virtual class FScene* GetRenderScene() override
	{
		return this;
	}
	virtual void OnWorldCleanup() override;


	virtual void UpdateSceneSettings(AWorldSettings* WorldSettings) override;

	virtual class FGPUSkinCache* GetGPUSkinCache() override
	{
		return GPUSkinCache;
	}

#if RHI_RAYTRACING
	virtual FRayTracingDynamicGeometryCollection* GetRayTracingDynamicGeometryCollection() override
	{
		return RayTracingDynamicGeometryCollection;
	}
#endif

	/**
	 * Sets the FX system associated with the scene.
	 */
	virtual void SetFXSystem( class FFXSystemInterface* InFXSystem ) override;

	/**
	 * Get the FX system associated with the scene.
	 */
	virtual class FFXSystemInterface* GetFXSystem() override;

	/**
	 * Exports the scene.
	 *
	 * @param	Ar		The Archive used for exporting.
	 **/
	virtual void Export( FArchive& Ar ) const override;

	FRHIUniformBuffer* GetParameterCollectionBuffer(const FGuid& InId) const
	{
		const FUniformBufferRHIRef* ExistingUniformBuffer = ParameterCollections.Find(InId);

		if (ExistingUniformBuffer)
		{
			return *ExistingUniformBuffer;
		}

		return nullptr;
	}

	virtual void ApplyWorldOffset(FVector InOffset) override;

	virtual void OnLevelAddedToWorld(FName InLevelName, UWorld* InWorld, bool bIsLightingScenario) override;
	virtual void OnLevelRemovedFromWorld(UWorld* InWorld, bool bIsLightingScenario) override;

	virtual bool HasAnyLights() const override 
	{ 
		check(IsInGameThread());
		return NumVisibleLights_GameThread > 0 || NumEnabledSkylights_GameThread > 0; 
	}

	virtual bool IsEditorScene() const override { return bIsEditorScene; }

	bool ShouldRenderSkylightInBasePass(EBlendMode BlendMode) const
	{
		bool bRenderSkyLight = SkyLight && !SkyLight->bHasStaticLighting && !(ShouldRenderRayTracingSkyLight(SkyLight) && !IsForwardShadingEnabled(GetShaderPlatform()));

		if (IsTranslucentBlendMode(BlendMode))
		{
			// Both stationary and movable skylights are applied in base pass for translucent materials
			bRenderSkyLight = bRenderSkyLight
				&& (ReadOnlyCVARCache.bEnableStationarySkylight || !SkyLight->bWantsStaticShadowing);
		}
		else
		{
			// For opaque materials, stationary skylight is applied in base pass but movable skylight
			// is applied in a separate render pass (bWantssStaticShadowing means stationary skylight)
			bRenderSkyLight = bRenderSkyLight
				&& ((ReadOnlyCVARCache.bEnableStationarySkylight && SkyLight->bWantsStaticShadowing)
					|| (!SkyLight->bWantsStaticShadowing
						&& (IsAnyForwardShadingEnabled(GetShaderPlatform()) || IsMobilePlatform(GetShaderPlatform()))));
		}

		return bRenderSkyLight;
	}

	virtual TArray<FPrimitiveComponentId> GetScenePrimitiveComponentIds() const override
	{
		return PrimitiveComponentIds;
	}

	/** Get the scene index of the FRuntimeVirtualTextureSceneProxy associated with the producer. */
	uint32 GetRuntimeVirtualTextureSceneIndex(uint32 ProducerId);

	/** Flush any dirty runtime virtual texture pages */
	void FlushDirtyRuntimeVirtualTextures();

#if WITH_EDITOR
	virtual bool InitializePixelInspector(FRenderTarget* BufferFinalColor, FRenderTarget* BufferSceneColor, FRenderTarget* BufferDepth, FRenderTarget* BufferHDR, FRenderTarget* BufferA, FRenderTarget* BufferBCDEF, int32 BufferIndex) override;

	virtual bool AddPixelInspectorRequest(FPixelInspectorRequest *PixelInspectorRequest) override;
#endif //WITH_EDITOR

	virtual void StartFrame() override
	{
		VelocityData.StartFrame(this);
	}

	virtual uint32 GetFrameNumber() const override
	{
		return SceneFrameNumber;
	}

	virtual void IncrementFrameNumber() override
	{
		++SceneFrameNumber;
	}

	/** Debug function to abtest lazy static mesh drawlists. */
	void UpdateDoLazyStaticMeshUpdate(FRHICommandListImmediate& CmdList);

	void DumpMeshDrawCommandMemoryStats();

	void CreateLightPrimitiveInteractionsForPrimitive(FPrimitiveSceneInfo* PrimitiveInfo, bool bAsyncCreateLPIs);

	void FlushAsyncLightPrimitiveInteractionCreation() const;

protected:
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual void AddAtmosphericFog_Impl(UAtmosphericFogComponent* FogComponent) override;
	virtual void RemoveAtmosphericFog_Impl(UAtmosphericFogComponent* FogComponent) override;
	virtual void RemoveAtmosphericFogResource_RenderThread_Impl(FRenderResource* FogResource) override;
	virtual FAtmosphericFogSceneInfo* GetAtmosphericFogSceneInfo_Impl() override { return AtmosphericFog; }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

private:

	/**
	 * Ensures the packed primitive arrays contain the same number of elements.
	 */
	void CheckPrimitiveArrays(int MaxTypeOffsetIndex = -1);

	/**
	 * Retrieves the lights interacting with the passed in primitive and adds them to the out array.
	 * Render thread version of function.
	 * @param	Primitive				Primitive to retrieve interacting lights for
	 * @param	RelevantLights	[out]	Array of lights interacting with primitive
	 */
	void GetRelevantLights_RenderThread( UPrimitiveComponent* Primitive, TArray<const ULightComponent*>* RelevantLights ) const;

	/**
	 * Adds a primitive to the scene.  Called in the rendering thread by AddPrimitive.
	 * @param PrimitiveSceneInfo - The primitive being added.
	 */
	void AddPrimitiveSceneInfo_RenderThread(FPrimitiveSceneInfo* PrimitiveSceneInfo, const TOptional<FTransform>& PreviousTransform);

	/**
	 * Removes a primitive from the scene.  Called in the rendering thread by RemovePrimitive.
	 * @param PrimitiveSceneInfo - The primitive being removed.
	 */
	void RemovePrimitiveSceneInfo_RenderThread(FPrimitiveSceneInfo* PrimitiveSceneInfo);

	/** Updates a primitive's transform, called on the rendering thread. */
	void UpdatePrimitiveTransform_RenderThread(FPrimitiveSceneProxy* PrimitiveSceneProxy, const FBoxSphereBounds& WorldBounds, const FBoxSphereBounds& LocalBounds, const FMatrix& LocalToWorld, const FVector& OwnerPosition, const TOptional<FTransform>& PreviousTransform);

	/** Updates a single primitive's lighting attachment root. */
	void UpdatePrimitiveLightingAttachmentRoot(UPrimitiveComponent* Primitive);

	void AssignAvailableShadowMapChannelForLight(FLightSceneInfo* LightSceneInfo);

	/**
	 * Adds a light to the scene.  Called in the rendering thread by AddLight.
	 * @param LightSceneInfo - The light being added.
	 */
	void AddLightSceneInfo_RenderThread(FLightSceneInfo* LightSceneInfo);

	/**
	 * Adds a decal to the scene.  Called in the rendering thread by AddDecal or RemoveDecal.
	 * @param Component - The object that should being added or removed.
	 * @param bAdd true:add, FASLE:remove
	 */
	void AddOrRemoveDecal_RenderThread(FDeferredDecalProxy* Component, bool bAdd);

	/**
	 * Removes a light from the scene.  Called in the rendering thread by RemoveLight.
	 * @param LightSceneInfo - The light being removed.
	 */
	void RemoveLightSceneInfo_RenderThread(FLightSceneInfo* LightSceneInfo);

	void UpdateLightTransform_RenderThread(FLightSceneInfo* LightSceneInfo, const struct FUpdateLightTransformParameters& Parameters);

	/**
	 * Deletes the internal AtmosphericFog scene info and operates required operations.
	 */
	void DeleteAtmosphericFogSceneInfo();

	/** 
	* Updates the contents of the given reflection capture by rendering the scene. 
	* This must be called on the game thread.
	*/
	void CaptureOrUploadReflectionCapture(UReflectionCaptureComponent* CaptureComponent, int32 ReflectionCaptureSize, bool bVerifyOnlyCapturing, bool bCapturingForMobile);

	/** Updates the contents of all reflection captures in the scene.  Must be called from the game thread. */
	void UpdateAllReflectionCaptures(const TCHAR* CaptureReason, int32 ReflectionCaptureSize, bool bVerifyOnlyCapturing, bool bCapturingForMobile);

	/** Updates all static draw lists. */
	void UpdateStaticDrawLists_RenderThread(FRHICommandListImmediate& RHICmdList);

	/** Add a runtime virtual texture proxy to the scene. Called in the rendering thread by AddRuntimeVirtualTexture. */
	void AddRuntimeVirtualTexture_RenderThread(FRuntimeVirtualTextureSceneProxy* SceneProxy);
	/** Update a runtime virtual texture proxy to the scene. Called in the rendering thread by AddRuntimeVirtualTexture. */
	void UpdateRuntimeVirtualTexture_RenderThread(FRuntimeVirtualTextureSceneProxy* SceneProxy, FRuntimeVirtualTextureSceneProxy* SceneProxyToReplace);
	/** Remove a runtime virtual texture proxy from the scene. Called in the rendering thread by RemoveRuntimeVirtualTexture. */
	void RemoveRuntimeVirtualTexture_RenderThread(FRuntimeVirtualTextureSceneProxy* SceneProxy);
	/** Update the scene primitive data after completing operations that add or remove runtime virtual textures. This can be slow and should be called rarely. */
	void UpdateRuntimeVirtualTextureForAllPrimitives_RenderThread();

	/**
	 * Shifts scene data by provided delta
	 * Called on world origin changes
	 * 
	 * @param	InOffset	Delta to shift scene by
	 */
	void ApplyWorldOffset_RenderThread(const FVector& InOffset);

	/**
	 * Notification from game thread that level was added to a world
	 *
	 * @param	InLevelName		Level name
	 */
	void OnLevelAddedToWorld_RenderThread(FName InLevelName);

	void ProcessAtmosphereLightRemoval_RenderThread(FLightSceneInfo* LightSceneInfo);
	void ProcessAtmosphereLightAddition_RenderThread(FLightSceneInfo* LightSceneInfo);

private:
	struct FUpdateTransformCommand
	{
		FBoxSphereBounds WorldBounds;
		FBoxSphereBounds LocalBounds; 
		FMatrix LocalToWorld; 
		FVector AttachmentRootPosition;
	};

	TMap<FPrimitiveSceneInfo*, FPrimitiveComponentId> UpdatedAttachmentRoots;
	TMap<FPrimitiveSceneProxy*, FCustomPrimitiveData> UpdatedCustomPrimitiveParams;
	TMap<FPrimitiveSceneProxy*, FUpdateTransformCommand> UpdatedTransforms;
	TMap<FPrimitiveSceneInfo*, FMatrix> OverridenPreviousTransforms;
	TSet<FPrimitiveSceneInfo*> AddedPrimitiveSceneInfos;
	TSet<FPrimitiveSceneInfo*> RemovedPrimitiveSceneInfos;
	TSet<FPrimitiveSceneInfo*> DistanceFieldSceneDataUpdates;

	FAsyncTask<class FAsyncCreateLightPrimitiveInteractionsTask>* AsyncCreateLightPrimitiveInteractionsTask;

	/** 
	 * The number of visible lights in the scene
	 * Note: This is tracked on the game thread!
	 */
	int32 NumVisibleLights_GameThread;

	/** 
	 * Whether the scene has a valid sky light.
	 * Note: This is tracked on the game thread!
	 */
	int32 NumEnabledSkylights_GameThread;

	/** Frame number incremented per-family viewing this scene. */
	uint32 SceneFrameNumber;
};

inline bool ShouldIncludeDomainInMeshPass(EMaterialDomain Domain)
{
	// Non-Surface domains can be applied to static meshes for thumbnails or material editor preview
	// Volume domain materials however must only be rendered in the voxelization pass
	return Domain != MD_Volume;
}

#include "BasePassRendering.inl"

