// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRendering.cpp: Scene rendering.
=============================================================================*/

#include "SceneOcclusion.h"
#include "EngineGlobals.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "DynamicPrimitiveDrawing.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "PlanarReflectionSceneProxy.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VisualizeTexture.h"


/*-----------------------------------------------------------------------------
	Globals
-----------------------------------------------------------------------------*/

int32 GAllowPrecomputedVisibility = 1;
static FAutoConsoleVariableRef CVarAllowPrecomputedVisibility(
	TEXT("r.AllowPrecomputedVisibility"),
	GAllowPrecomputedVisibility,
	TEXT("If zero, precomputed visibility will not be used to cull primitives."),
	ECVF_RenderThreadSafe
	);

static int32 GShowPrecomputedVisibilityCells = 0;
static FAutoConsoleVariableRef CVarShowPrecomputedVisibilityCells(
	TEXT("r.ShowPrecomputedVisibilityCells"),
	GShowPrecomputedVisibilityCells,
	TEXT("If not zero, draw all precomputed visibility cells."),
	ECVF_RenderThreadSafe
	);

static int32 GShowRelevantPrecomputedVisibilityCells = 0;
static FAutoConsoleVariableRef CVarShowRelevantPrecomputedVisibilityCells(
	TEXT("r.ShowRelevantPrecomputedVisibilityCells"),
	GShowRelevantPrecomputedVisibilityCells,
	TEXT("If not zero, draw relevant precomputed visibility cells only."),
	ECVF_RenderThreadSafe
	);

int32 GOcclusionCullCascadedShadowMaps = 0;
FAutoConsoleVariableRef CVarOcclusionCullCascadedShadowMaps(
	TEXT("r.Shadow.OcclusionCullCascadedShadowMaps"),
	GOcclusionCullCascadedShadowMaps,
	TEXT("Whether to use occlusion culling on cascaded shadow maps.  Disabled by default because rapid view changes reveal new regions too quickly for latent occlusion queries to work with."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarMobileAllowSoftwareOcclusion(
	TEXT("r.Mobile.AllowSoftwareOcclusion"),
	0,
	TEXT("Whether to allow rasterizing scene on CPU for primitive occlusion.\n"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<bool> CVarMobileEnableOcclusionExtraFrame(
	TEXT("r.Mobile.EnableOcclusionExtraFrame"),
	true,
	TEXT("Whether to allow extra frame for occlusion culling (enabled by default)"),
	ECVF_RenderThreadSafe
	);

DEFINE_GPU_STAT(HZB);

/** Random table for occlusion **/
FOcclusionRandomStream GOcclusionRandomStream;

int32 FOcclusionQueryHelpers::GetNumBufferedFrames(ERHIFeatureLevel::Type FeatureLevel)
{
	int32 NumGPUS = 1;
#if WITH_SLI || WITH_MGPU
	// If we're running with SLI, assume throughput is more important than latency, and buffer an extra frame
	ensure(GNumAlternateFrameRenderingGroups <= (int32)FOcclusionQueryHelpers::MaxBufferedOcclusionFrames);
	return FMath::Min<int32>(GNumAlternateFrameRenderingGroups, (int32)FOcclusionQueryHelpers::MaxBufferedOcclusionFrames);
#endif
	static const auto NumBufferedQueriesVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.NumBufferedOcclusionQueries"));
	EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];

	int32 NumExtraMobileFrames = 0;
	if ((FeatureLevel <= ERHIFeatureLevel::ES3_1 || IsVulkanMobileSM5Platform(ShaderPlatform)) && CVarMobileEnableOcclusionExtraFrame.GetValueOnAnyThread())
	{
		NumExtraMobileFrames++; // the mobile renderer just doesn't do much after the basepass, and hence it will be asking for the query results almost immediately; the results can't possibly be ready in 1 frame.
		
		bool bNeedsAnotherExtraMobileFrame = IsVulkanPlatform(ShaderPlatform); // || IsOpenGLPlatform(ShaderPlatform)
		bNeedsAnotherExtraMobileFrame = bNeedsAnotherExtraMobileFrame || IsVulkanMobileSM5Platform(ShaderPlatform);
		bNeedsAnotherExtraMobileFrame = bNeedsAnotherExtraMobileFrame || FDataDrivenShaderPlatformInfo::GetNeedsExtraMobileFrames(ShaderPlatform);
		bNeedsAnotherExtraMobileFrame = bNeedsAnotherExtraMobileFrame && IsRunningRHIInSeparateThread();

		if (bNeedsAnotherExtraMobileFrame)
		{
			// Android, unfortunately, requires the RHIThread to mediate the readback of queries. Therefore we need an extra frame to avoid a stall in either thread. 
			// The RHIT needs to do read back after the queries are ready and before the RT needs them to avoid stalls. The RHIT may be busy when the queries become ready, so this is all very complicated.
			NumExtraMobileFrames++;
		}
	}

	return FMath::Clamp<int32>(NumExtraMobileFrames + NumBufferedQueriesVar->GetValueOnAnyThread() * NumGPUS, 1, (int32)FOcclusionQueryHelpers::MaxBufferedOcclusionFrames);
}


// default, non-instanced shader implementation
IMPLEMENT_SHADER_TYPE(,FOcclusionQueryVS,TEXT("/Engine/Private/OcclusionQueryVertexShader.usf"),TEXT("Main"),SF_Vertex);
IMPLEMENT_SHADER_TYPE(,FOcclusionQueryPS,TEXT("/Engine/Private/OcclusionQueryPixelShader.usf"),TEXT("Main"),SF_Pixel);

static FGlobalBoundShaderState GOcclusionTestBoundShaderState;

/** 
 * Returns an array of visibility data for the given view position, or NULL if none exists. 
 * The data bits are indexed by VisibilityId of each primitive in the scene.
 * This method decompresses data if necessary and caches it based on the bucket and chunk index in the view state.
 */
const uint8* FSceneViewState::GetPrecomputedVisibilityData(FViewInfo& View, const FScene* Scene)
{
	const uint8* PrecomputedVisibilityData = NULL;
	if (Scene->PrecomputedVisibilityHandler && GAllowPrecomputedVisibility && View.Family->EngineShowFlags.PrecomputedVisibility)
	{
		const FPrecomputedVisibilityHandler& Handler = *Scene->PrecomputedVisibilityHandler;
		FViewElementPDI VisibilityCellsPDI(&View, nullptr, nullptr);

		// Draw visibility cell bounds for debugging if enabled
		if ((GShowPrecomputedVisibilityCells || View.Family->EngineShowFlags.PrecomputedVisibilityCells) && !GShowRelevantPrecomputedVisibilityCells)
		{
			for (int32 BucketIndex = 0; BucketIndex < Handler.PrecomputedVisibilityCellBuckets.Num(); BucketIndex++)
			{
				for (int32 CellIndex = 0; CellIndex < Handler.PrecomputedVisibilityCellBuckets[BucketIndex].Cells.Num(); CellIndex++)
				{
					const FPrecomputedVisibilityCell& CurrentCell = Handler.PrecomputedVisibilityCellBuckets[BucketIndex].Cells[CellIndex];
					// Construct the cell's bounds
					const FBox CellBounds(CurrentCell.Min, CurrentCell.Min + FVector(Handler.PrecomputedVisibilityCellSizeXY, Handler.PrecomputedVisibilityCellSizeXY, Handler.PrecomputedVisibilityCellSizeZ));
					if (View.ViewFrustum.IntersectBox(CellBounds.GetCenter(), CellBounds.GetExtent()))
					{
						DrawWireBox(&VisibilityCellsPDI, CellBounds, FColor(50, 50, 255), SDPG_World);
					}
				}
			}
		}

		// Calculate the bucket that ViewOrigin falls into
		// Cells are hashed into buckets to reduce search time
		const float FloatOffsetX = (View.ViewMatrices.GetViewOrigin().X - Handler.PrecomputedVisibilityCellBucketOriginXY.X) / Handler.PrecomputedVisibilityCellSizeXY;
		// FMath::TruncToInt rounds toward 0, we want to always round down
		const int32 BucketIndexX = FMath::Abs((FMath::TruncToInt(FloatOffsetX) - (FloatOffsetX < 0.0f ? 1 : 0)) / Handler.PrecomputedVisibilityCellBucketSizeXY % Handler.PrecomputedVisibilityNumCellBuckets);
		const float FloatOffsetY = (View.ViewMatrices.GetViewOrigin().Y -Handler.PrecomputedVisibilityCellBucketOriginXY.Y) / Handler.PrecomputedVisibilityCellSizeXY;
		const int32 BucketIndexY = FMath::Abs((FMath::TruncToInt(FloatOffsetY) - (FloatOffsetY < 0.0f ? 1 : 0)) / Handler.PrecomputedVisibilityCellBucketSizeXY % Handler.PrecomputedVisibilityNumCellBuckets);
		const int32 PrecomputedVisibilityBucketIndex = BucketIndexY * Handler.PrecomputedVisibilityCellBucketSizeXY + BucketIndexX;

		check(PrecomputedVisibilityBucketIndex < Handler.PrecomputedVisibilityCellBuckets.Num());
		const FPrecomputedVisibilityBucket& CurrentBucket = Handler.PrecomputedVisibilityCellBuckets[PrecomputedVisibilityBucketIndex];
		for (int32 CellIndex = 0; CellIndex < CurrentBucket.Cells.Num(); CellIndex++)
		{
			const FPrecomputedVisibilityCell& CurrentCell = CurrentBucket.Cells[CellIndex];
			// Construct the cell's bounds
			const FBox CellBounds(CurrentCell.Min, CurrentCell.Min + FVector(Handler.PrecomputedVisibilityCellSizeXY, Handler.PrecomputedVisibilityCellSizeXY, Handler.PrecomputedVisibilityCellSizeZ));
			// Check if ViewOrigin is inside the current cell
			if (CellBounds.IsInside(View.ViewMatrices.GetViewOrigin()))
			{
				// Reuse a cached decompressed chunk if possible
				if (CachedVisibilityChunk
					&& CachedVisibilityHandlerId == Scene->PrecomputedVisibilityHandler->GetId()
					&& CachedVisibilityBucketIndex == PrecomputedVisibilityBucketIndex
					&& CachedVisibilityChunkIndex == CurrentCell.ChunkIndex)
				{
					checkSlow(CachedVisibilityChunk->Num() >= CurrentCell.DataOffset + CurrentBucket.CellDataSize);
					PrecomputedVisibilityData = &(*CachedVisibilityChunk)[CurrentCell.DataOffset];
				}
				else
				{
					const FCompressedVisibilityChunk& CompressedChunk = Handler.PrecomputedVisibilityCellBuckets[PrecomputedVisibilityBucketIndex].CellDataChunks[CurrentCell.ChunkIndex];
					CachedVisibilityBucketIndex = PrecomputedVisibilityBucketIndex;
					CachedVisibilityChunkIndex = CurrentCell.ChunkIndex;
					CachedVisibilityHandlerId = Scene->PrecomputedVisibilityHandler->GetId();

					if (CompressedChunk.bCompressed)
					{
						// Decompress the needed visibility data chunk
						DecompressedVisibilityChunk.Reset();
						DecompressedVisibilityChunk.AddUninitialized(CompressedChunk.UncompressedSize);
						verify(FCompression::UncompressMemory(
							NAME_Zlib, 
							DecompressedVisibilityChunk.GetData(),
							CompressedChunk.UncompressedSize,
							CompressedChunk.Data.GetData(),
							CompressedChunk.Data.Num()));
						CachedVisibilityChunk = &DecompressedVisibilityChunk;
					}
					else
					{
						CachedVisibilityChunk = &CompressedChunk.Data;
					}

					checkSlow(CachedVisibilityChunk->Num() >= CurrentCell.DataOffset + CurrentBucket.CellDataSize);
					// Return a pointer to the cell containing ViewOrigin's decompressed visibility data
					PrecomputedVisibilityData = &(*CachedVisibilityChunk)[CurrentCell.DataOffset];
				}

				if (GShowRelevantPrecomputedVisibilityCells)
				{
					// Draw the currently used visibility cell with green wireframe for debugging
					DrawWireBox(&VisibilityCellsPDI, CellBounds, FColor(50, 255, 50), SDPG_Foreground);
				}
				else
				{
					break;
				}
			}
			else if (GShowRelevantPrecomputedVisibilityCells)
			{
				// Draw all cells in the current visibility bucket as blue wireframe
				DrawWireBox(&VisibilityCellsPDI, CellBounds, FColor(50, 50, 255), SDPG_World);
			}
		}
	}
	return PrecomputedVisibilityData;
}

void FSceneViewState::TrimOcclusionHistory(float CurrentTime, float MinHistoryTime, float MinQueryTime, int32 FrameNumber)
{
	// Only trim every few frames, since stale entries won't cause problems
	if (FrameNumber % 6 == 0)
	{
		int32 NumBufferedFrames = FOcclusionQueryHelpers::GetNumBufferedFrames(GetFeatureLevel());

		for(TSet<FPrimitiveOcclusionHistory,FPrimitiveOcclusionHistoryKeyFuncs>::TIterator PrimitiveIt(PrimitiveOcclusionHistorySet);
			PrimitiveIt;
			++PrimitiveIt
			)
		{
			// If the primitive has an old pending occlusion query, release it.
			if(PrimitiveIt->LastConsideredTime < MinQueryTime)
			{
				PrimitiveIt->ReleaseStaleQueries(FrameNumber, NumBufferedFrames);
			}

			// If the primitive hasn't been considered for visibility recently, remove its history from the set.
			if (PrimitiveIt->LastConsideredTime < MinHistoryTime || PrimitiveIt->LastConsideredTime > CurrentTime)
			{
				PrimitiveIt.RemoveCurrent();
			}
		}
	}
}

bool FSceneViewState::IsShadowOccluded(FRHICommandListImmediate& RHICmdList, FSceneViewState::FProjectedShadowKey ShadowKey, int32 NumBufferedFrames) const
{
	// Find the shadow's occlusion query from the previous frame.
	// Get the oldest occlusion query	
	const uint32 QueryIndex = FOcclusionQueryHelpers::GetQueryLookupIndex(PendingPrevFrameNumber, NumBufferedFrames);
	const FSceneViewState::ShadowKeyOcclusionQueryMap& ShadowOcclusionQueryMap = ShadowOcclusionQueryMaps[QueryIndex];	
	const FRHIPooledRenderQuery* Query = ShadowOcclusionQueryMap.Find(ShadowKey);

	// Read the occlusion query results.
	uint64 NumSamples = 0;
	// Only block on the query if not running SLI
	const bool bWaitOnQuery = GNumAlternateFrameRenderingGroups == 1;

	if (Query && RHICmdList.GetRenderQueryResult(Query->GetQuery(), NumSamples, bWaitOnQuery))
	{
		// If the shadow's occlusion query didn't have any pixels visible the previous frame, it's occluded.
		return NumSamples == 0;
	}
	else
	{
		// If the shadow wasn't queried the previous frame, it isn't occluded.

		return false;
	}
}

void FSceneViewState::ConditionallyAllocateSceneSoftwareOcclusion(ERHIFeatureLevel::Type InFeatureLevel)
{
	bool bMobileAllowSoftwareOcclusion = CVarMobileAllowSoftwareOcclusion.GetValueOnAnyThread() != 0;
	bool bShouldBeEnabled = InFeatureLevel <= ERHIFeatureLevel::ES3_1 && bMobileAllowSoftwareOcclusion;

	if (bShouldBeEnabled && !SceneSoftwareOcclusion)
	{
		SceneSoftwareOcclusion = MakeUnique<FSceneSoftwareOcclusion>();
	}
	else if (!bShouldBeEnabled && SceneSoftwareOcclusion)
	{
		SceneSoftwareOcclusion.Reset();
	}
}

void FSceneViewState::Destroy()
{
	FSceneViewState* self = this;
	ENQUEUE_RENDER_COMMAND(FSceneViewState_Destroy)(
	[self](FRHICommandListImmediate& RHICmdList)
	{
		// Release the occlusion query data.
		self->ReleaseResource();
		// Defer deletion of the view state until the rendering thread is done with it.
		delete self;
	});
}

SIZE_T FSceneViewState::GetSizeBytes() const
{
	uint32 ShadowOcclusionQuerySize = ShadowOcclusionQueryMaps.GetAllocatedSize();
	for (int32 i = 0; i < ShadowOcclusionQueryMaps.Num(); ++i)
	{
		ShadowOcclusionQuerySize += ShadowOcclusionQueryMaps[i].GetAllocatedSize();
	}

	return sizeof(*this) 
		+ ShadowOcclusionQuerySize
		+ ParentPrimitives.GetAllocatedSize() 
		+ PrimitiveFadingStates.GetAllocatedSize()
		+ PrimitiveOcclusionHistorySet.GetAllocatedSize();
}

class FOcclusionQueryIndexBuffer : public FIndexBuffer
{
public:
	virtual void InitRHI() override
	{
		const uint32 MaxBatchedPrimitives = FOcclusionQueryBatcher::OccludedPrimitiveQueryBatchSize;
		const uint32 Stride = sizeof(uint16);
		const uint32 SizeInBytes = MaxBatchedPrimitives * NUM_CUBE_VERTICES * Stride;

		FRHIResourceCreateInfo CreateInfo;

		void* BufferData;
		IndexBufferRHI = RHICreateAndLockIndexBuffer(Stride, SizeInBytes, BUF_Static, CreateInfo, BufferData);
		uint16* RESTRICT Indices = (uint16*)BufferData;

		for(uint32 PrimitiveIndex = 0;PrimitiveIndex < MaxBatchedPrimitives;PrimitiveIndex++)
		{
			for(int32 Index = 0;Index < NUM_CUBE_VERTICES;Index++)
			{
				Indices[PrimitiveIndex * NUM_CUBE_VERTICES + Index] = PrimitiveIndex * 8 + GCubeIndices[Index];
			}
		}
		RHIUnlockIndexBuffer(IndexBufferRHI);
	}
};
TGlobalResource<FOcclusionQueryIndexBuffer> GOcclusionQueryIndexBuffer;

FRHIRenderQuery* FFrameBasedOcclusionQueryPool::AllocateQuery()
{
	FFrameOcclusionQueries& CurrentFrame = FrameQueries[CurrentFrameIndex];

	// If we have a free query in the current frame pool, just take it
	if (CurrentFrame.FirstFreeIndex < CurrentFrame.Queries.Num())
	{
		return CurrentFrame.Queries[CurrentFrame.FirstFreeIndex++];
	}

	// If current frame runs out of queries, try to get some from other frames
	for (uint32 Index = 0; Index < UE_ARRAY_COUNT(FrameQueries); ++Index)
	{
		if (Index != CurrentFrameIndex)
		{
			FFrameOcclusionQueries& OtherFrame = FrameQueries[Index];
			while (OtherFrame.FirstFreeIndex < OtherFrame.Queries.Num())
			{
				CurrentFrame.Queries.Add(OtherFrame.Queries.Pop(false));
			}

			if (CurrentFrame.FirstFreeIndex < CurrentFrame.Queries.Num())
			{
				return CurrentFrame.Queries[CurrentFrame.FirstFreeIndex++];
			}
		}
	}

	// If all fails, create a new query
	FRenderQueryRHIRef NewQuery = GDynamicRHI->RHICreateRenderQuery(RQT_Occlusion);
	if (NewQuery)
	{
		CurrentFrame.Queries.Add(MoveTemp(NewQuery));
		return CurrentFrame.Queries[CurrentFrame.FirstFreeIndex++];
	}
	else
	{
		return nullptr;
	}
}

void FFrameBasedOcclusionQueryPool::AdvanceFrame(uint32 InOcclusionFrameCounter, uint32 InNumBufferedFrames, bool bStereoRoundRobin)
{
	if (InOcclusionFrameCounter == OcclusionFrameCounter)
	{
		return;
	}

	OcclusionFrameCounter = InOcclusionFrameCounter;

	if (bStereoRoundRobin)
	{
		InNumBufferedFrames *= 2;
	}

	if (InNumBufferedFrames != NumBufferedFrames)
	{
		FFrameOcclusionQueries TmpFrameQueries[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames * 2];

		for (uint32 Index = 0; Index < NumBufferedFrames; ++Index)
		{
			FFrameOcclusionQueries& Frame = FrameQueries[Index];
			const uint32 NewIndex = FOcclusionQueryHelpers::GetQueryIssueIndex(Frame.OcclusionFrameCounter, InNumBufferedFrames);
			FFrameOcclusionQueries& NewFrame = TmpFrameQueries[NewIndex];

			if (Frame.OcclusionFrameCounter > NewFrame.OcclusionFrameCounter)
			{
				Frame.Queries.Append(MoveTemp(NewFrame.Queries));
				FMemory::Memswap(&Frame, &NewFrame, sizeof(FFrameOcclusionQueries));
			}
			else
			{
				NewFrame.Queries.Append(MoveTemp(Frame.Queries));
			}
		}

		FMemory::Memswap(FrameQueries, TmpFrameQueries, sizeof(FrameQueries));
		NumBufferedFrames = InNumBufferedFrames;
	}
	
	CurrentFrameIndex = FOcclusionQueryHelpers::GetQueryIssueIndex(OcclusionFrameCounter, NumBufferedFrames);
	check(CurrentFrameIndex < FOcclusionQueryHelpers::MaxBufferedOcclusionFrames * 2);

	FrameQueries[CurrentFrameIndex].FirstFreeIndex = 0;
	FrameQueries[CurrentFrameIndex].OcclusionFrameCounter = OcclusionFrameCounter;
}

FOcclusionQueryBatcher::FOcclusionQueryBatcher(class FSceneViewState* ViewState,uint32 InMaxBatchedPrimitives)
:	CurrentBatchOcclusionQuery(NULL)
,	MaxBatchedPrimitives(InMaxBatchedPrimitives)
,	NumBatchedPrimitives(0)
,	OcclusionQueryPool(ViewState ? &ViewState->PrimitiveOcclusionQueryPool : NULL)
{}

FOcclusionQueryBatcher::~FOcclusionQueryBatcher()
{
	check(!BatchOcclusionQueries.Num());
}

void FOcclusionQueryBatcher::Flush(FRHICommandList& RHICmdList)
{
	if(BatchOcclusionQueries.Num())
	{
		FMemMark MemStackMark(FMemStack::Get());

		// Create the indices for MaxBatchedPrimitives boxes.
		FRHIIndexBuffer* IndexBufferRHI = GOcclusionQueryIndexBuffer.IndexBufferRHI;

		// Draw the batches.
		for(int32 BatchIndex = 0, NumBatches = BatchOcclusionQueries.Num();BatchIndex < NumBatches;BatchIndex++)
		{
			FOcclusionBatch& Batch = BatchOcclusionQueries[BatchIndex];
			FRHIRenderQuery* BatchOcclusionQuery = Batch.Query;
			FRHIVertexBuffer* VertexBufferRHI = Batch.VertexAllocation.VertexBuffer->VertexBufferRHI;
			uint32 VertexBufferOffset = Batch.VertexAllocation.VertexOffset;
			const int32 NumPrimitivesThisBatch = (BatchIndex != (NumBatches-1)) ? MaxBatchedPrimitives : NumBatchedPrimitives;
				
			RHICmdList.BeginRenderQuery(BatchOcclusionQuery);
			RHICmdList.SetStreamSource(0, VertexBufferRHI, VertexBufferOffset);
			RHICmdList.DrawIndexedPrimitive(
				IndexBufferRHI,
				/*BaseVertexIndex=*/ 0,
				/*MinIndex=*/ 0,
				/*NumVertices=*/ 8 * NumPrimitivesThisBatch,
				/*StartIndex=*/ 0,
				/*NumPrimitives=*/ 12 * NumPrimitivesThisBatch,
				/*NumInstances=*/ 1
				);
			RHICmdList.EndRenderQuery(BatchOcclusionQuery);
		}
		INC_DWORD_STAT_BY(STAT_OcclusionQueries,BatchOcclusionQueries.Num());

		// Reset the batch state.
		BatchOcclusionQueries.Empty(BatchOcclusionQueries.Num());
		CurrentBatchOcclusionQuery = NULL;
	}
}

FRHIRenderQuery* FOcclusionQueryBatcher::BatchPrimitive(const FVector& BoundsOrigin,const FVector& BoundsBoxExtent, FGlobalDynamicVertexBuffer& DynamicVertexBuffer)
{
	// Check if the current batch is full.
	if(CurrentBatchOcclusionQuery == NULL || NumBatchedPrimitives >= MaxBatchedPrimitives)
	{
		check(OcclusionQueryPool);
		CurrentBatchOcclusionQuery = new(BatchOcclusionQueries) FOcclusionBatch;
		CurrentBatchOcclusionQuery->Query = OcclusionQueryPool->AllocateQuery();
		CurrentBatchOcclusionQuery->VertexAllocation = DynamicVertexBuffer.Allocate(MaxBatchedPrimitives * 8 * sizeof(FVector));
		check(CurrentBatchOcclusionQuery->VertexAllocation.IsValid());
		NumBatchedPrimitives = 0;
	}

	// Add the primitive's bounding box to the current batch's vertex buffer.
	const FVector PrimitiveBoxMin = BoundsOrigin - BoundsBoxExtent;
	const FVector PrimitiveBoxMax = BoundsOrigin + BoundsBoxExtent;
	float* RESTRICT Vertices = (float*)CurrentBatchOcclusionQuery->VertexAllocation.Buffer;
	Vertices[ 0] = PrimitiveBoxMin.X; Vertices[ 1] = PrimitiveBoxMin.Y; Vertices[ 2] = PrimitiveBoxMin.Z;
	Vertices[ 3] = PrimitiveBoxMin.X; Vertices[ 4] = PrimitiveBoxMin.Y; Vertices[ 5] = PrimitiveBoxMax.Z;
	Vertices[ 6] = PrimitiveBoxMin.X; Vertices[ 7] = PrimitiveBoxMax.Y; Vertices[ 8] = PrimitiveBoxMin.Z;
	Vertices[ 9] = PrimitiveBoxMin.X; Vertices[10] = PrimitiveBoxMax.Y; Vertices[11] = PrimitiveBoxMax.Z;
	Vertices[12] = PrimitiveBoxMax.X; Vertices[13] = PrimitiveBoxMin.Y; Vertices[14] = PrimitiveBoxMin.Z;
	Vertices[15] = PrimitiveBoxMax.X; Vertices[16] = PrimitiveBoxMin.Y; Vertices[17] = PrimitiveBoxMax.Z;
	Vertices[18] = PrimitiveBoxMax.X; Vertices[19] = PrimitiveBoxMax.Y; Vertices[20] = PrimitiveBoxMin.Z;
	Vertices[21] = PrimitiveBoxMax.X; Vertices[22] = PrimitiveBoxMax.Y; Vertices[23] = PrimitiveBoxMax.Z;

	// Bump the batches buffer pointer.
	Vertices += 24;
	CurrentBatchOcclusionQuery->VertexAllocation.Buffer = (uint8*)Vertices;
	NumBatchedPrimitives++;

	return CurrentBatchOcclusionQuery->Query;
}

enum EShadowOcclusionQueryIntersectionMode
{
	SOQ_None,
	SOQ_LightInfluenceSphere,
	SOQ_NearPlaneVsShadowFrustum
};

static bool AllocateProjectedShadowOcclusionQuery(
	FViewInfo& View, 
	const FProjectedShadowInfo& ProjectedShadowInfo, 
	int32 NumBufferedFrames, 
	EShadowOcclusionQueryIntersectionMode IntersectionMode,
	FRHIRenderQuery*& ShadowOcclusionQuery)
{
	bool bIssueQuery = true;

	if (IntersectionMode == SOQ_LightInfluenceSphere)
	{
	FLightSceneProxy& LightProxy = *(ProjectedShadowInfo.GetLightSceneInfo().Proxy);
	
	// Query one pass point light shadows separately because they don't have a shadow frustum, they have a bounding sphere instead.
	FSphere LightBounds = LightProxy.GetBoundingSphere();
	
	const bool bCameraInsideLightGeometry = ((FVector)View.ViewMatrices.GetViewOrigin() - LightBounds.Center).SizeSquared() < FMath::Square(LightBounds.W * 1.05f + View.NearClippingDistance * 2.0f);
		bIssueQuery = !bCameraInsideLightGeometry;
	}
	else if (IntersectionMode == SOQ_NearPlaneVsShadowFrustum)
	{
		// The shadow transforms and view transforms are relative to different origins, so the world coordinates need to
		// be translated.
		const FVector4 PreShadowToPreViewTranslation(View.ViewMatrices.GetPreViewTranslation() - ProjectedShadowInfo.PreShadowTranslation,0);
	
		// If the shadow frustum is farther from the view origin than the near clipping plane,
		// it can't intersect the near clipping plane.
		const bool bIntersectsNearClippingPlane = ProjectedShadowInfo.ReceiverFrustum.IntersectSphere(
			View.ViewMatrices.GetViewOrigin() + ProjectedShadowInfo.PreShadowTranslation,
			View.NearClippingDistance * FMath::Sqrt(3.0f)
			);

		bIssueQuery = !bIntersectsNearClippingPlane;
	}

	if (bIssueQuery)
	{
		FSceneViewState* ViewState = (FSceneViewState*)View.State;

		// Allocate an occlusion query for the primitive from the occlusion query pool.
		FSceneViewState::FProjectedShadowKey Key(ProjectedShadowInfo);
		const uint32 QueryIndex = FOcclusionQueryHelpers::GetQueryIssueIndex(ViewState->PendingPrevFrameNumber, NumBufferedFrames);
		FSceneViewState::ShadowKeyOcclusionQueryMap& ShadowOcclusionQueryMap = ViewState->ShadowOcclusionQueryMaps[QueryIndex];

		checkSlow(ShadowOcclusionQueryMap.Find(Key) == NULL);
		FRHIPooledRenderQuery PooledShadowOcclusionQuery = ViewState->OcclusionQueryPool->AllocateQuery();
		ShadowOcclusionQuery = PooledShadowOcclusionQuery.GetQuery();
		ShadowOcclusionQueryMap.Add(Key, MoveTemp(PooledShadowOcclusionQuery));
	}
	
	return bIssueQuery;
}


static void ExecutePointLightShadowOcclusionQuery(FRHICommandList& RHICmdList, FViewInfo& View, const FProjectedShadowInfo& ProjectedShadowInfo, const TShaderRef<FOcclusionQueryVS>& VertexShader, FRHIRenderQuery* ShadowOcclusionQuery)
{
	FLightSceneProxy& LightProxy = *(ProjectedShadowInfo.GetLightSceneInfo().Proxy);
	
	// Query one pass point light shadows separately because they don't have a shadow frustum, they have a bounding sphere instead.
	FSphere LightBounds = LightProxy.GetBoundingSphere();

	RHICmdList.BeginRenderQuery(ShadowOcclusionQuery);
	
	// Draw bounding sphere
	VertexShader->SetParametersWithBoundingSphere(RHICmdList, View, LightBounds);
	StencilingGeometry::DrawVectorSphere(RHICmdList);
		
	RHICmdList.EndRenderQuery(ShadowOcclusionQuery);
}

static void PrepareDirectionalLightShadowOcclusionQuery(uint32& BaseVertexIndex, FVector* DestinationBuffer, const FViewInfo& View, const FProjectedShadowInfo& ProjectedShadowInfo)
{
	const FMatrix& ViewMatrix = View.ShadowViewMatrices.GetViewMatrix();
	const FMatrix& ProjectionMatrix = View.ShadowViewMatrices.GetProjectionMatrix();
	const FVector CameraDirection = ViewMatrix.GetColumn(2);
	const float SplitNear = ProjectedShadowInfo.CascadeSettings.SplitNear;

	float AspectRatio = ProjectionMatrix.M[1][1] / ProjectionMatrix.M[0][0];
	float HalfFOV = View.ShadowViewMatrices.IsPerspectiveProjection() ? FMath::Atan(1.0f / ProjectionMatrix.M[0][0]) : PI / 4.0f;

	// Build the camera frustum for this cascade
	const float StartHorizontalLength = SplitNear * FMath::Tan(HalfFOV);
	const FVector StartCameraRightOffset = ViewMatrix.GetColumn(0) * StartHorizontalLength;
	const float StartVerticalLength = StartHorizontalLength / AspectRatio;
	const FVector StartCameraUpOffset = ViewMatrix.GetColumn(1) * StartVerticalLength;

	FVector Verts[4] =
	{
		CameraDirection * SplitNear + StartCameraRightOffset + StartCameraUpOffset,
		CameraDirection * SplitNear + StartCameraRightOffset - StartCameraUpOffset,
		CameraDirection * SplitNear - StartCameraRightOffset - StartCameraUpOffset,
		CameraDirection * SplitNear - StartCameraRightOffset + StartCameraUpOffset
	};

	DestinationBuffer[BaseVertexIndex + 0] = Verts[0];
	DestinationBuffer[BaseVertexIndex + 1] = Verts[3];
	DestinationBuffer[BaseVertexIndex + 2] = Verts[2];
	DestinationBuffer[BaseVertexIndex + 3] = Verts[0];
	DestinationBuffer[BaseVertexIndex + 4] = Verts[2];
	DestinationBuffer[BaseVertexIndex + 5] = Verts[1];
	BaseVertexIndex += 6;
}

static void ExecuteDirectionalLightShadowOcclusionQuery(FRHICommandList& RHICmdList, uint32& BaseVertexIndex, FRHIRenderQuery* ShadowOcclusionQuery)
{
	RHICmdList.BeginRenderQuery(ShadowOcclusionQuery);

	RHICmdList.DrawPrimitive(BaseVertexIndex, 2, 1);
	BaseVertexIndex += 6;

	RHICmdList.EndRenderQuery(ShadowOcclusionQuery);
}

static void PrepareProjectedShadowOcclusionQuery(uint32& BaseVertexIndex, FVector* DestinationBuffer, const FViewInfo& View, const FProjectedShadowInfo& ProjectedShadowInfo)
{
	// The shadow transforms and view transforms are relative to different origins, so the world coordinates need to
	// be translated.
	const FVector4 PreShadowToPreViewTranslation(View.ViewMatrices.GetPreViewTranslation() - ProjectedShadowInfo.PreShadowTranslation, 0);

	FVector* Vertices = &DestinationBuffer[BaseVertexIndex];
	// Generate vertices for the shadow's frustum.
	for (uint32 Z = 0; Z < 2; Z++)
	{
		for (uint32 Y = 0; Y < 2; Y++)
		{
			for (uint32 X = 0; X < 2; X++)
			{
				const FVector4 UnprojectedVertex = ProjectedShadowInfo.InvReceiverMatrix.TransformFVector4(
					FVector4(
						(X ? -1.0f : 1.0f),
						(Y ? -1.0f : 1.0f),
						(Z ?  1.0f : 0.0f),
						1.0f)
				);
				const FVector ProjectedVertex = UnprojectedVertex / UnprojectedVertex.W + PreShadowToPreViewTranslation;
				Vertices[GetCubeVertexIndex(X, Y, Z)] = ProjectedVertex;
			}
		}
	}

	BaseVertexIndex += 8;
}

static void ExecuteProjectedShadowOcclusionQuery(FRHICommandList& RHICmdList, uint32& BaseVertexIndex, FRHIRenderQuery* ShadowOcclusionQuery)
{	
	// Draw the primitive's bounding box, using the occlusion query.
	RHICmdList.BeginRenderQuery(ShadowOcclusionQuery);

	RHICmdList.DrawIndexedPrimitive(GCubeIndexBuffer.IndexBufferRHI, BaseVertexIndex, 0, 8, 0, 12, 1);
	BaseVertexIndex += 8;

	RHICmdList.EndRenderQuery(ShadowOcclusionQuery);
}

static bool AllocatePlanarReflectionOcclusionQuery(const FViewInfo& View, const FPlanarReflectionSceneProxy* SceneProxy, int32 NumBufferedFrames, FRHIRenderQuery*& OcclusionQuery)
{
	FSceneViewState* ViewState = (FSceneViewState*)View.State;
	
	bool bAllowBoundsTest = false;
	
	if (View.ViewFrustum.IntersectBox(SceneProxy->WorldBounds.GetCenter(), SceneProxy->WorldBounds.GetExtent()))
	{
		const FBoxSphereBounds OcclusionBounds(SceneProxy->WorldBounds);
		
		if (View.bHasNearClippingPlane)
		{
			bAllowBoundsTest = View.NearClippingPlane.PlaneDot(OcclusionBounds.Origin) <
			-(FVector::BoxPushOut(View.NearClippingPlane, OcclusionBounds.BoxExtent));
			
		}
		else if (!View.IsPerspectiveProjection())
		{
			// Transform parallel near plane
			static_assert((int32)ERHIZBuffer::IsInverted != 0, "Check equation for culling!");
			bAllowBoundsTest = View.WorldToScreen(OcclusionBounds.Origin).Z - View.ViewMatrices.GetProjectionMatrix().M[2][2] * OcclusionBounds.SphereRadius < 1;
		}
		else
		{
			bAllowBoundsTest = OcclusionBounds.SphereRadius < HALF_WORLD_MAX;
		}
	}
	
	uint32 OcclusionFrameCounter = ViewState->OcclusionFrameCounter;
	FIndividualOcclusionHistory& OcclusionHistory = ViewState->PlanarReflectionOcclusionHistories.FindOrAdd(SceneProxy->PlanarReflectionId);
	OcclusionHistory.ReleaseQuery(OcclusionFrameCounter, NumBufferedFrames);
	
	if (bAllowBoundsTest)
	{
		// Allocate an occlusion query for the primitive from the occlusion query pool.
		FRHIPooledRenderQuery PooledOcclusionQuery = ViewState->OcclusionQueryPool->AllocateQuery();
		OcclusionQuery = PooledOcclusionQuery.GetQuery();

		OcclusionHistory.SetCurrentQuery(OcclusionFrameCounter, MoveTemp(PooledOcclusionQuery), NumBufferedFrames);
	}
	else
	{
		OcclusionHistory.SetCurrentQuery(OcclusionFrameCounter, FRHIPooledRenderQuery(), NumBufferedFrames);
	}
	
	return bAllowBoundsTest;
}

static void PreparePlanarReflectionOcclusionQuery(uint32& BaseVertexIndex, FVector* DestinationBuffer, const FViewInfo& View, const FPlanarReflectionSceneProxy* SceneProxy)
{
	float* Vertices = (float*)(&DestinationBuffer[BaseVertexIndex]);

	const FVector PrimitiveBoxMin = SceneProxy->WorldBounds.Min + View.ViewMatrices.GetPreViewTranslation();
	const FVector PrimitiveBoxMax = SceneProxy->WorldBounds.Max + View.ViewMatrices.GetPreViewTranslation();
	Vertices[0] = PrimitiveBoxMin.X; Vertices[1] = PrimitiveBoxMin.Y; Vertices[2] = PrimitiveBoxMin.Z;
	Vertices[3] = PrimitiveBoxMin.X; Vertices[4] = PrimitiveBoxMin.Y; Vertices[5] = PrimitiveBoxMax.Z;
	Vertices[6] = PrimitiveBoxMin.X; Vertices[7] = PrimitiveBoxMax.Y; Vertices[8] = PrimitiveBoxMin.Z;
	Vertices[9] = PrimitiveBoxMin.X; Vertices[10] = PrimitiveBoxMax.Y; Vertices[11] = PrimitiveBoxMax.Z;
	Vertices[12] = PrimitiveBoxMax.X; Vertices[13] = PrimitiveBoxMin.Y; Vertices[14] = PrimitiveBoxMin.Z;
	Vertices[15] = PrimitiveBoxMax.X; Vertices[16] = PrimitiveBoxMin.Y; Vertices[17] = PrimitiveBoxMax.Z;
	Vertices[18] = PrimitiveBoxMax.X; Vertices[19] = PrimitiveBoxMax.Y; Vertices[20] = PrimitiveBoxMin.Z;
	Vertices[21] = PrimitiveBoxMax.X; Vertices[22] = PrimitiveBoxMax.Y; Vertices[23] = PrimitiveBoxMax.Z;

	BaseVertexIndex += 8;
}

static void ExecutePlanarReflectionOcclusionQuery(FRHICommandList& RHICmdList, uint32& BaseVertexIndex, FRHIRenderQuery* OcclusionQuery)
{
	// Draw the primitive's bounding box, using the occlusion query.
	RHICmdList.BeginRenderQuery(OcclusionQuery);

	RHICmdList.DrawIndexedPrimitive(GCubeIndexBuffer.IndexBufferRHI, BaseVertexIndex, 0, 8, 0, 12, 1);
	BaseVertexIndex += 8;

	RHICmdList.EndRenderQuery(OcclusionQuery);
}

FHZBOcclusionTester::FHZBOcclusionTester()
	: ResultsBuffer( NULL )
{
	SetInvalidFrameNumber();
}

bool FHZBOcclusionTester::IsValidFrame(uint32 FrameNumber) const
{
	return (FrameNumber & FrameNumberMask) == ValidFrameNumber;
}

void FHZBOcclusionTester::SetValidFrameNumber(uint32 FrameNumber)
{
	ValidFrameNumber = FrameNumber & FrameNumberMask;

	checkSlow(!IsInvalidFrame());
}

bool FHZBOcclusionTester::IsInvalidFrame() const
{
	return ValidFrameNumber == InvalidFrameNumber;
}

void FHZBOcclusionTester::SetInvalidFrameNumber()
{
	// this number cannot be set by SetValidFrameNumber()
	ValidFrameNumber = InvalidFrameNumber;

	checkSlow(IsInvalidFrame());
}

void FHZBOcclusionTester::InitDynamicRHI()
{
	if (GetFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		FPooledRenderTargetDesc Desc( FPooledRenderTargetDesc::Create2DDesc( FIntPoint( SizeX, SizeY ), PF_B8G8R8A8, FClearValueBinding::None, TexCreate_CPUReadback | TexCreate_HideInVisualizeTexture, TexCreate_None, false ) );
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ResultsTextureCPU, TEXT("HZBResultsCPU"), ERenderTargetTransience::NonTransient );
		Fence = RHICreateGPUFence(TEXT("HZBGPUFence"));
	}
}

void FHZBOcclusionTester::ReleaseDynamicRHI()
{
	if (GetFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		GRenderTargetPool.FreeUnusedResource( ResultsTextureCPU );
		Fence.SafeRelease();
	}
}

uint32 FHZBOcclusionTester::AddBounds( const FVector& BoundsCenter, const FVector& BoundsExtent )
{
	uint32 Index = Primitives.AddUninitialized();
	check( Index < SizeX * SizeY );
	Primitives[ Index ].Center = BoundsCenter;
	Primitives[ Index ].Extent = BoundsExtent;
	return Index;
}

void FHZBOcclusionTester::MapResults(FRHICommandListImmediate& RHICmdList)
{
	check( !ResultsBuffer );

	if (!IsInvalidFrame() )
	{
		uint32 IdleStart = FPlatformTime::Cycles();

		int32 Width = 0;
		int32 Height = 0;

		RHICmdList.MapStagingSurface(ResultsTextureCPU->GetRenderTargetItem().ShaderResourceTexture, Fence.GetReference(), *(void**)&ResultsBuffer, Width, Height);

		// RHIMapStagingSurface will block until the results are ready (from the previous frame) so we need to consider this RT idle time
		GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForGPUQuery] += FPlatformTime::Cycles() - IdleStart;
		GRenderThreadNumIdle[ERenderThreadIdleTypes::WaitingForGPUQuery]++;
	}
	
	// Can happen because of device removed, we might crash later but this occlusion culling system can behave gracefully.
	if( ResultsBuffer == NULL )
	{
		// First frame
		static uint8 FirstFrameBuffer[] = { 255 };
		ResultsBuffer = FirstFrameBuffer;
		SetInvalidFrameNumber();
	}
}

void FHZBOcclusionTester::UnmapResults(FRHICommandListImmediate& RHICmdList)
{
	check( ResultsBuffer );
	if(!IsInvalidFrame())
	{
		RHICmdList.UnmapStagingSurface(ResultsTextureCPU->GetRenderTargetItem().ShaderResourceTexture);
	}
	ResultsBuffer = NULL;
}

bool FHZBOcclusionTester::IsVisible( uint32 Index ) const
{
	checkSlow( ResultsBuffer );
	checkSlow( Index < SizeX * SizeY );
	
	// TODO shader compress to bits

#if 0
	return ResultsBuffer[ 4 * Index ] != 0;
#elif 0
	uint32 x = FMath::ReverseMortonCode2( Index >> 0 );
	uint32 y = FMath::ReverseMortonCode2( Index >> 1 );
	uint32 m = x + y * SizeX;
	return ResultsBuffer[ 4 * m ] != 0;
#else
	// TODO put block constants in class
	// TODO optimize
	const uint32 BlockSize = 8;
	const uint32 SizeInBlocksX = SizeX / BlockSize;
	const uint32 SizeInBlocksY = SizeY / BlockSize;

	const int32 BlockIndex = Index / (BlockSize * BlockSize);
	const int32 BlockX = BlockIndex % SizeInBlocksX;
	const int32 BlockY = BlockIndex / SizeInBlocksY;

	const int32 b = Index % (BlockSize * BlockSize);
	const int32 x = BlockX * BlockSize + b % BlockSize;
	const int32 y = BlockY * BlockSize + b / BlockSize;

	return ResultsBuffer[ 4 * (x + y * SizeY) ] != 0;
#endif
}

class FHZBTestPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FHZBTestPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FHZBTestPS() {}

public:
	LAYOUT_FIELD(FShaderParameter, HZBUvFactor)
	LAYOUT_FIELD(FShaderParameter, HZBSize)
	LAYOUT_FIELD(FShaderResourceParameter, HZBTexture)
	LAYOUT_FIELD(FShaderResourceParameter, HZBSampler)
	LAYOUT_FIELD(FShaderResourceParameter, BoundsCenterTexture)
	LAYOUT_FIELD(FShaderResourceParameter, BoundsCenterSampler)
	LAYOUT_FIELD(FShaderResourceParameter, BoundsExtentTexture)
	LAYOUT_FIELD(FShaderResourceParameter, BoundsExtentSampler)

	FHZBTestPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		HZBUvFactor.Bind( Initializer.ParameterMap, TEXT("HZBUvFactor") );
		HZBSize.Bind( Initializer.ParameterMap, TEXT("HZBSize") );
		HZBTexture.Bind( Initializer.ParameterMap, TEXT("HZBTexture") );
		HZBSampler.Bind( Initializer.ParameterMap, TEXT("HZBSampler") );
		BoundsCenterTexture.Bind( Initializer.ParameterMap, TEXT("BoundsCenterTexture") );
		BoundsCenterSampler.Bind( Initializer.ParameterMap, TEXT("BoundsCenterSampler") );
		BoundsExtentTexture.Bind( Initializer.ParameterMap, TEXT("BoundsExtentTexture") );
		BoundsExtentSampler.Bind( Initializer.ParameterMap, TEXT("BoundsExtentSampler") );
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, FRHITexture* BoundsCenter, FRHITexture* BoundsExtent )
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		/*
		 * Defines the maximum number of mipmaps the HZB test is considering
		 * to avoid memory cache trashing when rendering on high resolution.
		 */
		const float kHZBTestMaxMipmap = 9.0f;

		const float HZBMipmapCounts = FMath::Log2(FMath::Max(View.HZBMipmap0Size.X, View.HZBMipmap0Size.Y));
		const FVector HZBUvFactorValue(
			float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
			float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y),
			FMath::Max(HZBMipmapCounts - kHZBTestMaxMipmap, 0.0f)
			);
		const FVector4 HZBSizeValue(
			View.HZBMipmap0Size.X,
			View.HZBMipmap0Size.Y,
			1.0f / float(View.HZBMipmap0Size.X),
			1.0f / float(View.HZBMipmap0Size.Y)
			);
		SetShaderValue(RHICmdList, ShaderRHI, HZBUvFactor, HZBUvFactorValue);
		SetShaderValue(RHICmdList, ShaderRHI, HZBSize, HZBSizeValue);

		SetTextureParameter(RHICmdList, ShaderRHI, HZBTexture, HZBSampler, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), View.HZB->GetRenderTargetItem().ShaderResourceTexture );

		SetTextureParameter(RHICmdList, ShaderRHI, BoundsCenterTexture, BoundsCenterSampler, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), BoundsCenter );
		SetTextureParameter(RHICmdList, ShaderRHI, BoundsExtentTexture, BoundsExtentSampler, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), BoundsExtent );
	}
};

IMPLEMENT_SHADER_TYPE(,FHZBTestPS,TEXT("/Engine/Private/HZBOcclusion.usf"),TEXT("HZBTestPS"),SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FHZBOcclusionUpdateTexturesParameters, )
	RDG_TEXTURE_ACCESS(BoundsCenterTexture, ERHIAccess::CopyDest)
	RDG_TEXTURE_ACCESS(BoundsExtentTexture, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FHZBOcclusionTestHZBParameters, )
	RDG_TEXTURE_ACCESS(BoundsCenterTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(BoundsExtentTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(HZBTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FHZBOcclusionTester::Submit(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	RDG_EVENT_SCOPE(GraphBuilder, "SubmitHZB");

	FSceneViewState* ViewState = (FSceneViewState*)View.State;
	if( !ViewState )
	{
		return;
	}

	FRDGTextureRef BoundsCenterTexture = nullptr;
	FRDGTextureRef BoundsExtentTexture = nullptr;

	{
		const FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(FIntPoint(SizeX, SizeY), PF_A32B32G32R32F, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource));
		BoundsCenterTexture = GraphBuilder.CreateTexture(Desc, TEXT("HZBBoundsCenter"));
		BoundsExtentTexture = GraphBuilder.CreateTexture(Desc, TEXT("HZBBoundsExtent"));
	}

	FRDGTextureRef ResultsTextureGPU = nullptr;
	{
		const FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(FIntPoint(SizeX, SizeY), PF_B8G8R8A8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable));
		ResultsTextureGPU = GraphBuilder.CreateTexture(Desc, TEXT("HZBResultsGPU"));
	}

	{
		auto* PassParameters = GraphBuilder.AllocParameters<FHZBOcclusionUpdateTexturesParameters>();
		PassParameters->BoundsCenterTexture = BoundsCenterTexture;
		PassParameters->BoundsExtentTexture = BoundsExtentTexture;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("UpdateTextures"),
			PassParameters,
			ERDGPassFlags::Copy,
			[this, BoundsCenterTexture, BoundsExtentTexture](FRHICommandListImmediate& RHICmdList)
		{
			// Update in blocks to avoid large update
			const uint32 BlockSize = 8;
			const uint32 SizeInBlocksX = SizeX / BlockSize;
			const uint32 SizeInBlocksY = SizeY / BlockSize;
			const uint32 BlockStride = BlockSize * 4 * sizeof(float);

			float CenterBuffer[BlockSize * BlockSize][4];
			float ExtentBuffer[BlockSize * BlockSize][4];

			const uint32 NumPrimitives = Primitives.Num();
			for (uint32 i = 0; i < NumPrimitives; i += BlockSize * BlockSize)
			{
				const uint32 BlockEnd = FMath::Min(BlockSize * BlockSize, NumPrimitives - i);
				for (uint32 b = 0; b < BlockEnd; b++)
				{
					const FOcclusionPrimitive& Primitive = Primitives[i + b];

					CenterBuffer[b][0] = Primitive.Center.X;
					CenterBuffer[b][1] = Primitive.Center.Y;
					CenterBuffer[b][2] = Primitive.Center.Z;
					CenterBuffer[b][3] = 0.0f;

					ExtentBuffer[b][0] = Primitive.Extent.X;
					ExtentBuffer[b][1] = Primitive.Extent.Y;
					ExtentBuffer[b][2] = Primitive.Extent.Z;
					ExtentBuffer[b][3] = 1.0f;
				}

				// Clear rest of block
				if (BlockEnd < BlockSize * BlockSize)
				{
					FMemory::Memset((float*)CenterBuffer + BlockEnd * 4, 0, sizeof(CenterBuffer) - BlockEnd * 4 * sizeof(float));
					FMemory::Memset((float*)ExtentBuffer + BlockEnd * 4, 0, sizeof(ExtentBuffer) - BlockEnd * 4 * sizeof(float));
				}

				const int32 BlockIndex = i / (BlockSize * BlockSize);
				const int32 BlockX = BlockIndex % SizeInBlocksX;
				const int32 BlockY = BlockIndex / SizeInBlocksY;

				FUpdateTextureRegion2D Region(BlockX * BlockSize, BlockY * BlockSize, 0, 0, BlockSize, BlockSize);
				RHIUpdateTexture2D((FRHITexture2D*)BoundsCenterTexture->GetRHI(), 0, Region, BlockStride, (uint8*)CenterBuffer);
				RHIUpdateTexture2D((FRHITexture2D*)BoundsExtentTexture->GetRHI(), 0, Region, BlockStride, (uint8*)ExtentBuffer);
			}

			Primitives.Empty();
		});
	}

	// Draw test
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FHZBOcclusionTestHZBParameters>();
		PassParameters->BoundsCenterTexture = BoundsCenterTexture;
		PassParameters->BoundsExtentTexture = BoundsExtentTexture;
		PassParameters->HZBTexture = GraphBuilder.RegisterExternalTexture(View.HZB);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ResultsTextureGPU, ERenderTargetLoadAction::ENoAction);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("TestHZB"),
			PassParameters,
			ERDGPassFlags::Raster,
			[this, &View, BoundsCenterTexture, BoundsExtentTexture](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			TShaderMapRef< FScreenVS >	VertexShader(View.ShaderMap);
			TShaderMapRef< FHZBTestPS >	PixelShader(View.ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			PixelShader->SetParameters(RHICmdList, View, BoundsCenterTexture->GetRHI(), BoundsExtentTexture->GetRHI());

			RHICmdList.SetViewport(0, 0, 0.0f, SizeX, SizeY, 1.0f);

			// TODO draw quads covering blocks added above
			DrawRectangle(
				RHICmdList,
				0, 0,
				SizeX, SizeY,
				0, 0,
				SizeX, SizeY,
				FIntPoint(SizeX, SizeY),
				FIntPoint(SizeX, SizeY),
				VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}

	// Transfer memory GPU -> CPU
	AddCopyToResolveTargetPass(GraphBuilder, ResultsTextureGPU, GraphBuilder.RegisterExternalTexture(ResultsTextureCPU), FResolveParams());

	AddPass(GraphBuilder, [this](FRHICommandList& RHICmdList)
	{
		RHICmdList.WriteGPUFence(Fence);
	});
}

static FViewOcclusionQueriesPerView AllocateOcclusionTests(const FScene* Scene, TArrayView<const FVisibleLightInfo> VisibleLightInfos, TArrayView<FViewInfo> Views)
{
	SCOPED_NAMED_EVENT(FSceneRenderer_AllocateOcclusionTestsOcclusionTests, FColor::Emerald);

	const ERHIFeatureLevel::Type FeatureLevel = Scene->GetFeatureLevel();
	const int32 NumBufferedFrames = FOcclusionQueryHelpers::GetNumBufferedFrames(FeatureLevel);

	bool bBatchedQueries = false;

	FViewOcclusionQueriesPerView QueriesPerView;
	QueriesPerView.AddDefaulted(Views.Num());

	// Perform occlusion queries for each view
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		FViewOcclusionQueries& ViewQuery = QueriesPerView[ViewIndex];
		FSceneViewState* ViewState = (FSceneViewState*)View.State;
		const FSceneViewFamily& ViewFamily = *View.Family;

		if (ViewState && !View.bDisableQuerySubmissions)
		{
			// Issue this frame's occlusion queries (occlusion queries from last frame may still be in flight)
			const uint32 QueryIndex = FOcclusionQueryHelpers::GetQueryIssueIndex(ViewState->PendingPrevFrameNumber, NumBufferedFrames);
			FSceneViewState::ShadowKeyOcclusionQueryMap& ShadowOcclusionQueryMap = ViewState->ShadowOcclusionQueryMaps[QueryIndex];

			// Clear primitives which haven't been visible recently out of the occlusion history, and reset old pending occlusion queries.
			ViewState->TrimOcclusionHistory(ViewFamily.CurrentRealTime, ViewFamily.CurrentRealTime - GEngine->PrimitiveProbablyVisibleTime, ViewFamily.CurrentRealTime, ViewState->OcclusionFrameCounter);

			// Give back all these occlusion queries to the pool.
			ShadowOcclusionQueryMap.Reset();

			if (FeatureLevel > ERHIFeatureLevel::ES3_1)
			{
				for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
				{
					const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightIt.GetIndex()];

					for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.AllProjectedShadows.Num(); ShadowIndex++)
					{
						const FProjectedShadowInfo& ProjectedShadowInfo = *VisibleLightInfo.AllProjectedShadows[ShadowIndex];

						if (ProjectedShadowInfo.DependentView && ProjectedShadowInfo.DependentView != &View)
						{
							continue;
						}

						if (!IsShadowCacheModeOcclusionQueryable(ProjectedShadowInfo.CacheMode))
						{
							// Only query one of the cache modes for each shadow
							continue;
						}

						if (ProjectedShadowInfo.bOnePassPointLightShadow)
						{
							FRHIRenderQuery* ShadowOcclusionQuery;
							if (AllocateProjectedShadowOcclusionQuery(View, ProjectedShadowInfo, NumBufferedFrames, SOQ_LightInfluenceSphere, ShadowOcclusionQuery))
							{
								ViewQuery.PointLightQueryInfos.Add(&ProjectedShadowInfo);
								ViewQuery.PointLightQueries.Add(ShadowOcclusionQuery);
								checkSlow(ViewQuery.PointLightQueryInfos.Num() == ViewQuery.PointLightQueries.Num());
								bBatchedQueries = true;
							}
						}
						else if (ProjectedShadowInfo.IsWholeSceneDirectionalShadow())
						{
							// Don't query the first cascade, it is always visible
							if (GOcclusionCullCascadedShadowMaps && ProjectedShadowInfo.CascadeSettings.ShadowSplitIndex > 0)
							{
								FRHIRenderQuery* ShadowOcclusionQuery;
								if (AllocateProjectedShadowOcclusionQuery(View, ProjectedShadowInfo, NumBufferedFrames, SOQ_None, ShadowOcclusionQuery))
								{
									ViewQuery.CSMQueryInfos.Add(&ProjectedShadowInfo);
									ViewQuery.CSMQueries.Add(ShadowOcclusionQuery);
									checkSlow(ViewQuery.CSMQueryInfos.Num() == ViewQuery.CSMQueries.Num());
									bBatchedQueries = true;
								}
							}
						}
						else if (
							// Don't query preshadows, since they are culled if their subject is occluded.
							!ProjectedShadowInfo.bPreShadow
							// Don't query if any subjects are visible because the shadow frustum will be definitely unoccluded
							&& !ProjectedShadowInfo.SubjectsVisible(View))
						{
							FRHIRenderQuery* ShadowOcclusionQuery;
							if (AllocateProjectedShadowOcclusionQuery(View, ProjectedShadowInfo, NumBufferedFrames, SOQ_NearPlaneVsShadowFrustum, ShadowOcclusionQuery))
							{
								ViewQuery.ShadowQuerieInfos.Add(&ProjectedShadowInfo);
								ViewQuery.ShadowQueries.Add(ShadowOcclusionQuery);
								checkSlow(ViewQuery.ShadowQuerieInfos.Num() == ViewQuery.ShadowQueries.Num());
								bBatchedQueries = true;
							}
						}
					}

					// Issue occlusion queries for all per-object projected shadows that we would have rendered but were occluded last frame.
					for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.OccludedPerObjectShadows.Num(); ShadowIndex++)
					{
						const FProjectedShadowInfo& ProjectedShadowInfo = *VisibleLightInfo.OccludedPerObjectShadows[ShadowIndex];
						FRHIRenderQuery* ShadowOcclusionQuery;
						if (AllocateProjectedShadowOcclusionQuery(View, ProjectedShadowInfo, NumBufferedFrames, SOQ_NearPlaneVsShadowFrustum, ShadowOcclusionQuery))
						{
							ViewQuery.ShadowQuerieInfos.Add(&ProjectedShadowInfo);
							ViewQuery.ShadowQueries.Add(ShadowOcclusionQuery);
							checkSlow(ViewQuery.ShadowQuerieInfos.Num() == ViewQuery.ShadowQueries.Num());
							bBatchedQueries = true;
						}
					}
				}
			}

			if (FeatureLevel > ERHIFeatureLevel::ES3_1 &&
				!View.bIsPlanarReflection &&
				!View.bIsSceneCapture &&
				!View.bIsReflectionCapture)
			{
				// +1 to buffered frames because the query is submitted late into the main frame, but read at the beginning of a frame
				const int32 NumReflectionBufferedFrames = NumBufferedFrames + 1;

				for (int32 ReflectionIndex = 0; ReflectionIndex < Scene->PlanarReflections.Num(); ReflectionIndex++)
				{
					FPlanarReflectionSceneProxy* SceneProxy = Scene->PlanarReflections[ReflectionIndex];
					FRHIRenderQuery* ShadowOcclusionQuery;
					if (AllocatePlanarReflectionOcclusionQuery(View, SceneProxy, NumReflectionBufferedFrames, ShadowOcclusionQuery))
					{
						ViewQuery.ReflectionQuerieInfos.Add(SceneProxy);
						ViewQuery.ReflectionQueries.Add(ShadowOcclusionQuery);
						checkSlow(ViewQuery.ReflectionQuerieInfos.Num() == ViewQuery.ReflectionQueries.Num());
						bBatchedQueries = true;
					}
				}
			}

			// Don't do primitive occlusion if we have a view parent or are frozen - only applicable to Debug & Development.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			ViewQuery.bFlushQueries &= (!ViewState->HasViewParent() && !ViewState->bIsFrozen);
#endif

			bBatchedQueries |= (View.IndividualOcclusionQueries.HasBatches() || View.GroupedOcclusionQueries.HasBatches() || ViewQuery.bFlushQueries);
		}
	}

	// Return an empty array if no queries exist.
	if (!bBatchedQueries)
	{
		QueriesPerView.Empty();
	}
	return MoveTemp(QueriesPerView);
}

static void BeginOcclusionTests(
	FRHICommandListImmediate& RHICmdList,
	TArrayView<FViewInfo> Views,
	ERHIFeatureLevel::Type FeatureLevel,
	const FViewOcclusionQueriesPerView& QueriesPerView,
	uint32 DownsampleFactor)
{
	check(RHICmdList.IsInsideRenderPass());
	check(QueriesPerView.Num() == Views.Num());

	SCOPE_CYCLE_COUNTER(STAT_BeginOcclusionTestsTime);
	SCOPED_DRAW_EVENT(RHICmdList, BeginOcclusionTests);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
	// Depth tests, no depth writes, no color writes, opaque
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector3();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, ViewOcclusionTests, TEXT("ViewOcclusionTests %d"), ViewIndex);

		FViewInfo& View = Views[ViewIndex];
		const FViewOcclusionQueries& ViewQuery = QueriesPerView[ViewIndex];
		FSceneViewState* ViewState = (FSceneViewState*)View.State;
		SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

		// We only need to render the front-faces of the culling geometry (this halves the amount of pixels we touch)
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();

		const FIntRect ViewRect = GetDownscaledRect(View.ViewRect, DownsampleFactor);
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		// Lookup the vertex shader.
		TShaderMapRef<FOcclusionQueryVS> VertexShader(View.ShaderMap);
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();

		if (View.Family->EngineShowFlags.OcclusionMeshes)
		{
			TShaderMapRef<FOcclusionQueryPS> PixelShader(View.ShaderMap);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA>::GetRHI();
		}

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		if (FeatureLevel > ERHIFeatureLevel::ES3_1)
		{
			SCOPED_DRAW_EVENT(RHICmdList, ShadowFrustumQueries);
			for (int i = 0; i < ViewQuery.PointLightQueries.Num(); i++)
			{
				ExecutePointLightShadowOcclusionQuery(RHICmdList, View, *ViewQuery.PointLightQueryInfos[i], VertexShader, ViewQuery.PointLightQueries[i]);
			}
		}

		uint32 NumVertices = ViewQuery.CSMQueries.Num() * 6 // Plane 
			+ ViewQuery.ShadowQueries.Num() * 8 // Cube
			+ ViewQuery.ReflectionQueries.Num() * 8; // Cube

		if (NumVertices > 0)
		{
			uint32 BaseVertexOffset = 0;
			FRHIResourceCreateInfo CreateInfo;
			FVertexBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector) * NumVertices, BUF_Volatile, CreateInfo);
			void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FVector) * NumVertices, RLM_WriteOnly);

			{
				FVector* Vertices = reinterpret_cast<FVector*>(VoidPtr);
				for (FProjectedShadowInfo const* Query : ViewQuery.CSMQueryInfos)
				{
					PrepareDirectionalLightShadowOcclusionQuery(BaseVertexOffset, Vertices, View, *Query);
					checkSlow(BaseVertexOffset <= NumVertices);
				}

				for (FProjectedShadowInfo const* Query : ViewQuery.ShadowQuerieInfos)
				{
					PrepareProjectedShadowOcclusionQuery(BaseVertexOffset, Vertices, View, *Query);
					checkSlow(BaseVertexOffset <= NumVertices);
				}

				for (FPlanarReflectionSceneProxy const* Query : ViewQuery.ReflectionQuerieInfos)
				{
					PreparePlanarReflectionOcclusionQuery(BaseVertexOffset, Vertices, View, Query);
					checkSlow(BaseVertexOffset <= NumVertices);
				}
			}

			RHIUnlockVertexBuffer(VertexBufferRHI);

			{
				SCOPED_DRAW_EVENT(RHICmdList, ShadowFrustumQueries);
				VertexShader->SetParameters(RHICmdList, View);
				RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
				BaseVertexOffset = 0;

				for (FRHIRenderQuery* const& Query : ViewQuery.CSMQueries)
				{
					ExecuteDirectionalLightShadowOcclusionQuery(RHICmdList, BaseVertexOffset, Query);
					checkSlow(BaseVertexOffset <= NumVertices);
				}

				for (FRHIRenderQuery* const& Query : ViewQuery.ShadowQueries)
				{
					ExecuteProjectedShadowOcclusionQuery(RHICmdList, BaseVertexOffset, Query);
					checkSlow(BaseVertexOffset <= NumVertices);
				}
			}

			if (FeatureLevel > ERHIFeatureLevel::ES3_1)
			{
				SCOPED_DRAW_EVENT(RHICmdList, PlanarReflectionQueries);
				for (FRHIRenderQuery* const& Query : ViewQuery.ReflectionQueries)
				{
					ExecutePlanarReflectionOcclusionQuery(RHICmdList, BaseVertexOffset, Query);
					check(BaseVertexOffset <= NumVertices);
				}
			}

			VertexBufferRHI.SafeRelease();
		}

		if (ViewQuery.bFlushQueries)
		{
			VertexShader->SetParameters(RHICmdList, View);

			{
				SCOPED_DRAW_EVENT(RHICmdList, GroupedQueries);
				View.GroupedOcclusionQueries.Flush(RHICmdList);
			}
			{
				SCOPED_DRAW_EVENT(RHICmdList, IndividualQueries);
				View.IndividualOcclusionQueries.Flush(RHICmdList);
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderOcclusion(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepthTexture,
	FRDGTextureRef SmallDepthTexture,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	bool bIsOcclusionTesting)
{
	if (bIsOcclusionTesting)
	{
		check(SceneDepthTexture);
		check(SmallDepthTexture);

		RDG_GPU_STAT_SCOPE(GraphBuilder, HZB);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

		uint32 DownsampleFactor = 1;
		FRDGTextureRef OcclusionDepthTexture = SceneDepthTexture;

		// Update the quarter-sized depth buffer with the current contents of the scene depth texture.
		// This needs to happen before occlusion tests, which makes use of the small depth buffer.
		if (SceneContext.UseDownsizedOcclusionQueries())
		{
			DownsampleFactor = SceneContext.GetSmallColorDepthDownsampleFactor();
			OcclusionDepthTexture = SmallDepthTexture;

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				const FViewInfo& View = Views[ViewIndex];
				RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

				const FScreenPassTexture SceneDepth(SceneDepthTexture, View.ViewRect);
				const FScreenPassRenderTarget SmallDepth(SmallDepthTexture, GetDownscaledRect(View.ViewRect, DownsampleFactor), ERenderTargetLoadAction::ELoad);
				AddDownsampleDepthPass(GraphBuilder, View, SceneDepth, SmallDepth, EDownsampleDepthFilter::Max);
			}
		}

		// Issue occlusion queries. This is done after the downsampled depth buffer is created so that it can be used for issuing queries.
		FViewOcclusionQueriesPerView QueriesPerView = AllocateOcclusionTests(Scene, VisibleLightInfos, Views);

		if (QueriesPerView.Num())
		{
			int32 NumQueriesForBatch = 0;

			{
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewOcclusionQueries& ViewQuery = QueriesPerView[ViewIndex];
					NumQueriesForBatch += ViewQuery.PointLightQueries.Num();
					NumQueriesForBatch += ViewQuery.CSMQueries.Num();
					NumQueriesForBatch += ViewQuery.ShadowQueries.Num();
					NumQueriesForBatch += ViewQuery.ReflectionQueries.Num();

					FViewInfo& View = Views[ViewIndex];
					FSceneViewState* ViewState = (FSceneViewState*)View.State;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					if (!ViewState->HasViewParent() && !ViewState->bIsFrozen)
#endif
					{
						NumQueriesForBatch += View.IndividualOcclusionQueries.GetNumBatchOcclusionQueries();
						NumQueriesForBatch += View.GroupedOcclusionQueries.GetNumBatchOcclusionQueries();
					}
				}
			}

			auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(OcclusionDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilWrite);
			PassParameters->RenderTargets.NumOcclusionQueries = NumQueriesForBatch;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("BeginOcclusionTests"),
				PassParameters,
				ERDGPassFlags::Raster,
				[this, LocalQueriesPerView = MoveTemp(QueriesPerView), DownsampleFactor](FRHICommandListImmediate& RHICmdList)
			{
				BeginOcclusionTests(RHICmdList, Views, FeatureLevel, LocalQueriesPerView, DownsampleFactor);
			});
		}
	}

	const bool bUseHzbOcclusion = RenderHzb(GraphBuilder, SceneTexturesUniformBuffer);

	if (bUseHzbOcclusion || bIsOcclusionTesting)
	{
		// Hint to the RHI to submit commands up to this point to the GPU if possible.  Can help avoid CPU stalls next frame waiting
		// for these query results on some platforms.
		AddPass(GraphBuilder, [](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SubmitCommandsHint();
		});
	}

	if (bIsOcclusionTesting)
	{
		FenceOcclusionTests(GraphBuilder);
	}
}

void FMobileSceneRenderer::RenderOcclusion(FRHICommandListImmediate& RHICmdList)
{
	if (!DoOcclusionQueries(FeatureLevel))
	{
		return;
	}

	{
		SCOPED_NAMED_EVENT(FMobileSceneRenderer_BeginOcclusionTests, FColor::Emerald);
		const FViewOcclusionQueriesPerView QueriesPerView = AllocateOcclusionTests(Scene, VisibleLightInfos, Views);

		if (QueriesPerView.Num())
		{
			BeginOcclusionTests(RHICmdList, Views, FeatureLevel, QueriesPerView, 1.0f);
		}
	}

	FRDGBuilder GraphBuilder(RHICmdList);
	FenceOcclusionTests(GraphBuilder);
	GraphBuilder.Execute();
}

DECLARE_CYCLE_STAT(TEXT("OcclusionSubmittedFence Dispatch"), STAT_OcclusionSubmittedFence_Dispatch, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("OcclusionSubmittedFence Wait"), STAT_OcclusionSubmittedFence_Wait, STATGROUP_SceneRendering);

void FSceneRenderer::FenceOcclusionTests(FRDGBuilder& GraphBuilder)
{
	if (IsRunningRHIInSeparateThread())
	{
		AddPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
		{
			SCOPE_CYCLE_COUNTER(STAT_OcclusionSubmittedFence_Dispatch);
			int32 NumFrames = FOcclusionQueryHelpers::GetNumBufferedFrames(FeatureLevel);
			for (int32 Dest = NumFrames - 1; Dest >= 1; Dest--)
			{
				CA_SUPPRESS(6385);
				OcclusionSubmittedFence[Dest] = OcclusionSubmittedFence[Dest - 1];
			}
			OcclusionSubmittedFence[0] = RHICmdList.RHIThreadFence();
			RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
			RHICmdList.PollRenderQueryResults();
		});
	}
}

void FSceneRenderer::WaitOcclusionTests(FRHICommandListImmediate& RHICmdList)
{
	if (IsRunningRHIInSeparateThread())
	{
		SCOPE_CYCLE_COUNTER(STAT_OcclusionSubmittedFence_Wait);
		int32 BlockFrame = FOcclusionQueryHelpers::GetNumBufferedFrames(FeatureLevel) - 1;
		FRHICommandListExecutor::WaitOnRHIThreadFence(OcclusionSubmittedFence[BlockFrame]);
		OcclusionSubmittedFence[BlockFrame] = nullptr;
	}
}
