// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderResource.cpp: Render resource implementation.
=============================================================================*/

#include "RenderResource.h"
#include "Misc/ScopedEvent.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "CoreGlobals.h"

/** Whether to enable mip-level fading or not: +1.0f if enabled, -1.0f if disabled. */
float GEnableMipLevelFading = 1.0f;

// The maximum number of transient vertex buffer bytes to allocate before we start panic logging who is doing the allocations
int32 GMaxVertexBytesAllocatedPerFrame = 32 * 1024 * 1024;

FAutoConsoleVariableRef CVarMaxVertexBytesAllocatedPerFrame(
	TEXT("r.MaxVertexBytesAllocatedPerFrame"),
	GMaxVertexBytesAllocatedPerFrame,
	TEXT("The maximum number of transient vertex buffer bytes to allocate before we start panic logging who is doing the allocations"));

int32 GGlobalBufferNumFramesUnusedThresold = 30;
FAutoConsoleVariableRef CVarReadBufferNumFramesUnusedThresold(
	TEXT("r.NumFramesUnusedBeforeReleasingGlobalResourceBuffers"),
	GGlobalBufferNumFramesUnusedThresold ,
	TEXT("Number of frames after which unused global resource allocations will be discarded. Set 0 to ignore. (default=30)"));

FThreadSafeCounter FRenderResource::ResourceListIterationActive;

TArray<int32>& GetFreeIndicesList()
{
	static TArray<int32> FreeIndicesList;
	return FreeIndicesList;
}

TArray<FRenderResource*>& FRenderResource::GetResourceList()
{
	static TArray<FRenderResource*> RenderResourceList;
	return RenderResourceList;
}

/** Initialize all resources initialized before the RHI was initialized */
void FRenderResource::InitPreRHIResources()
{	
	// Notify all initialized FRenderResources that there's a valid RHI device to create their RHI resources for now.
	FRenderResource::InitRHIForAllResources();

#if !PLATFORM_NEEDS_RHIRESOURCELIST
	FRenderResource::GetResourceList().Empty();
#endif
}

void FRenderResource::ChangeFeatureLevel(ERHIFeatureLevel::Type NewFeatureLevel)
{
	ENQUEUE_RENDER_COMMAND(FRenderResourceChangeFeatureLevel)(
		[NewFeatureLevel](FRHICommandList& RHICmdList)
	{
		FRenderResource::ForAllResources([NewFeatureLevel](FRenderResource* Resource)
		{
			// Only resources configured for a specific feature level need to be updated
			if (Resource->HasValidFeatureLevel() && (Resource->FeatureLevel != NewFeatureLevel))
			{
				Resource->ReleaseRHI();
				Resource->ReleaseDynamicRHI();
				Resource->FeatureLevel = NewFeatureLevel;
				Resource->InitDynamicRHI();
				Resource->InitRHI();
			}
		});
	});
}

void FRenderResource::InitResource()
{
	check(IsInRenderingThread());
	if (ListIndex == INDEX_NONE)
	{
		TArray<FRenderResource*>& ResourceList = GetResourceList();
		TArray<int32>& FreeIndicesList = GetFreeIndicesList();

		// If resource list is currently being iterated, new resources must be added to the end of the list, to ensure they're processed during the iteration
		// Otherwise empty slots in the list may be re-used for new resources
		int32 LocalListIndex = INDEX_NONE;
		if (FreeIndicesList.Num() > 0 && ResourceListIterationActive.GetValue() == 0)
		{
			LocalListIndex = FreeIndicesList.Pop();
			check(ResourceList[LocalListIndex] == nullptr);
			ResourceList[LocalListIndex] = this;
		}
		else
		{
			LocalListIndex = ResourceList.Add(this);
		}

		if (GIsRHIInitialized)
		{
			CSV_SCOPED_TIMING_STAT_EXCLUSIVE(InitRenderResource);
			InitDynamicRHI();
			InitRHI();
		}

		FPlatformMisc::MemoryBarrier(); // there are some multithreaded reads of ListIndex
		ListIndex = LocalListIndex;
	}
}

void FRenderResource::ReleaseResource()
{
	if ( !GIsCriticalError )
	{
		check(IsInRenderingThread());
		if(ListIndex != INDEX_NONE)
		{
			if(GIsRHIInitialized)
			{
				ReleaseRHI();
				ReleaseDynamicRHI();
			}

			TArray<FRenderResource*>& ResourceList = GetResourceList();
			TArray<int32>& FreeIndicesList = GetFreeIndicesList();
			ResourceList[ListIndex] = nullptr;
			FreeIndicesList.Add(ListIndex);
			ListIndex = INDEX_NONE;
		}
	}
}

void FRenderResource::UpdateRHI()
{
	check(IsInRenderingThread());
	if(IsInitialized() && GIsRHIInitialized)
	{
		ReleaseRHI();
		ReleaseDynamicRHI();
		InitDynamicRHI();
		InitRHI();
	}
}

FRenderResource::~FRenderResource()
{
	if (IsInitialized() && !GIsCriticalError)
	{
		// Deleting an initialized FRenderResource will result in a crash later since it is still linked
		UE_LOG(LogRendererCore, Fatal,TEXT("A FRenderResource was deleted without being released first!"));
	}
}

void BeginInitResource(FRenderResource* Resource)
{
	ENQUEUE_RENDER_COMMAND(InitCommand)(
		[Resource](FRHICommandListImmediate& RHICmdList)
		{
			Resource->InitResource();
		});
}

void BeginUpdateResourceRHI(FRenderResource* Resource)
{
	ENQUEUE_RENDER_COMMAND(UpdateCommand)(
		[Resource](FRHICommandListImmediate& RHICmdList)
		{
			Resource->UpdateRHI();
		});
}

struct FBatchedReleaseResources
{
	enum 
	{
		NumPerBatch = 16
	};
	int32 NumBatch;
	FRenderResource* Resources[NumPerBatch];
	FBatchedReleaseResources()
	{
		Reset();
	}
	void Reset()
	{
		NumBatch = 0;
	}
	void Execute()
	{
		for (int32 Index = 0; Index < NumBatch; Index++)
		{
			Resources[Index]->ReleaseResource();
		}
		Reset();
	}
	void Flush()
	{
		if (NumBatch)
		{
			const FBatchedReleaseResources BatchedReleaseResources = *this;
			ENQUEUE_RENDER_COMMAND(BatchReleaseCommand)(
				[BatchedReleaseResources](FRHICommandList& RHICmdList)
				{
					((FBatchedReleaseResources&)BatchedReleaseResources).Execute();
				});
			Reset();
		}
	}
	void Add(FRenderResource* Resource)
	{
		if (NumBatch >= NumPerBatch)
		{
			Flush();
		}
		check(NumBatch < NumPerBatch);
		Resources[NumBatch] = Resource;
		NumBatch++;
	}
	bool IsEmpty()
	{
		return !NumBatch;
	}
};

static bool GBatchedReleaseIsActive = false;
static FBatchedReleaseResources GBatchedRelease;

void StartBatchedRelease()
{
	check(IsInGameThread() && !GBatchedReleaseIsActive && GBatchedRelease.IsEmpty());
	GBatchedReleaseIsActive = true;
}
void EndBatchedRelease()
{
	check(IsInGameThread() && GBatchedReleaseIsActive);
	GBatchedRelease.Flush();
	GBatchedReleaseIsActive = false;
}

void BeginReleaseResource(FRenderResource* Resource)
{
	if (GBatchedReleaseIsActive && IsInGameThread())
	{
		GBatchedRelease.Add(Resource);
		return;
	}
	ENQUEUE_RENDER_COMMAND(ReleaseCommand)(
		[Resource](FRHICommandList& RHICmdList)
		{
			Resource->ReleaseResource();
		});
}

void ReleaseResourceAndFlush(FRenderResource* Resource)
{
	// Send the release message.
	ENQUEUE_RENDER_COMMAND(ReleaseCommand)(
		[Resource](FRHICommandList& RHICmdList)
		{
			Resource->ReleaseResource();
		});

	FlushRenderingCommands();
}

FTextureReference::FTextureReference()
	: TextureReferenceRHI(NULL)
{
}

FTextureReference::~FTextureReference()
{
}

void FTextureReference::BeginInit_GameThread()
{
	bInitialized_GameThread = true;
	BeginInitResource(this);
}

void FTextureReference::BeginRelease_GameThread()
{
	BeginReleaseResource(this);
	bInitialized_GameThread = false;
}

void FTextureReference::InvalidateLastRenderTime()
{
	LastRenderTimeRHI.SetLastRenderTime(-FLT_MAX);
}

void FTextureReference::InitRHI()
{
	SCOPED_LOADTIMER(FTextureReference_InitRHI);
	TextureReferenceRHI = RHICreateTextureReference(&LastRenderTimeRHI);
}
	
void FTextureReference::ReleaseRHI()
{
	TextureReferenceRHI.SafeRelease();
}

FString FTextureReference::GetFriendlyName() const
{
	return TEXT("FTextureReference");
}

/** The global null color vertex buffer, which is set with a stride of 0 on meshes without a color component. */
TGlobalResource<FNullColorVertexBuffer> GNullColorVertexBuffer;

/** The global null vertex buffer, which is set with a stride of 0 on meshes */
TGlobalResource<FNullVertexBuffer> GNullVertexBuffer;

/*------------------------------------------------------------------------------
	FGlobalDynamicVertexBuffer implementation.
------------------------------------------------------------------------------*/

/**
 * An individual dynamic vertex buffer.
 */
class FDynamicVertexBuffer : public FVertexBuffer
{
public:
	/** The aligned size of all dynamic vertex buffers. */
	enum { ALIGNMENT = (1 << 16) }; // 64KB
	/** Pointer to the vertex buffer mapped in main memory. */
	uint8* MappedBuffer;
	/** Size of the vertex buffer in bytes. */
	uint32 BufferSize;
	/** Number of bytes currently allocated from the buffer. */
	uint32 AllocatedByteCount;
	/** Number of successive frames for which AllocatedByteCount == 0. Used as a metric to decide when to free the allocation. */
	int32 NumFramesUnused = 0;

	/** Default constructor. */
	explicit FDynamicVertexBuffer(uint32 InMinBufferSize)
		: MappedBuffer(NULL)
		, BufferSize(FMath::Max<uint32>(Align(InMinBufferSize,ALIGNMENT),ALIGNMENT))
		, AllocatedByteCount(0)
	{
	}

	/**
	 * Locks the vertex buffer so it may be written to.
	 */
	void Lock()
	{
		check(MappedBuffer == NULL);
		check(AllocatedByteCount == 0);
		check(IsValidRef(VertexBufferRHI));
		MappedBuffer = (uint8*)RHILockVertexBuffer(VertexBufferRHI, 0, BufferSize, RLM_WriteOnly);
	}

	/**
	 * Unocks the buffer so the GPU may read from it.
	 */
	void Unlock()
	{
		check(MappedBuffer != NULL);
		check(IsValidRef(VertexBufferRHI));
		RHIUnlockVertexBuffer(VertexBufferRHI);
		MappedBuffer = NULL;
		AllocatedByteCount = 0;
		NumFramesUnused = 0;
	}

	// FRenderResource interface.
	virtual void InitRHI() override
	{
		check(!IsValidRef(VertexBufferRHI));
		FRHIResourceCreateInfo CreateInfo;
		VertexBufferRHI = RHICreateVertexBuffer(BufferSize, BUF_Volatile, CreateInfo);
		MappedBuffer = NULL;
		AllocatedByteCount = 0;
	}

	virtual void ReleaseRHI() override
	{
		FVertexBuffer::ReleaseRHI();
		MappedBuffer = NULL;
		AllocatedByteCount = 0;
	}

	virtual FString GetFriendlyName() const override
	{
		return TEXT("FDynamicVertexBuffer");
	}
};

/**
 * A pool of dynamic vertex buffers.
 */
struct FDynamicVertexBufferPool
{
	/** List of vertex buffers. */
	TIndirectArray<FDynamicVertexBuffer> VertexBuffers;
	/** The current buffer from which allocations are being made. */
	FDynamicVertexBuffer* CurrentVertexBuffer;
	/** */
	TMap<int32, FDynamicVertexBuffer*> VertexBuffersUsedForInstancingBatch;

	/** Default constructor. */
	FDynamicVertexBufferPool()
		: CurrentVertexBuffer(NULL)
	{
		VertexBuffersUsedForInstancingBatch.Empty();
	}

	/** Destructor. */
	~FDynamicVertexBufferPool()
	{
		VertexBuffersUsedForInstancingBatch.Empty();

		int32 NumVertexBuffers = VertexBuffers.Num();
		for (int32 BufferIndex = 0; BufferIndex < NumVertexBuffers; ++BufferIndex)
		{
			VertexBuffers[BufferIndex].ReleaseResource();
		}
	}
};

FGlobalDynamicVertexBuffer::FGlobalDynamicVertexBuffer()
	: TotalAllocatedSinceLastCommit(0)
{
	Pool = new FDynamicVertexBufferPool();
}

FGlobalDynamicVertexBuffer::~FGlobalDynamicVertexBuffer()
{
	delete Pool;
	Pool = NULL;
}

FGlobalDynamicVertexBuffer::FAllocation FGlobalDynamicVertexBuffer::Allocate(uint32 SizeInBytes)
{
	FAllocation Allocation;

	TotalAllocatedSinceLastCommit += SizeInBytes;
	if (IsRenderAlarmLoggingEnabled())
	{
		UE_LOG(LogRendererCore, Warning, TEXT("FGlobalDynamicVertexBuffer::Allocate(%u), will have allocated %u total this frame"), SizeInBytes, TotalAllocatedSinceLastCommit);
	}

	FDynamicVertexBuffer* VertexBuffer = Pool->CurrentVertexBuffer;
	if (VertexBuffer == NULL || VertexBuffer->AllocatedByteCount + SizeInBytes > VertexBuffer->BufferSize)
	{
		// Find a buffer in the pool big enough to service the request.
		VertexBuffer = NULL;
		for (int32 BufferIndex = 0, NumBuffers = Pool->VertexBuffers.Num(); BufferIndex < NumBuffers; ++BufferIndex)
		{
			FDynamicVertexBuffer& VertexBufferToCheck = Pool->VertexBuffers[BufferIndex];

			// Exclude the vertex buffer used for instancing batch
			bool bUsedForInstancingBatch = false;
			for (TPair<int32, FDynamicVertexBuffer*>& Pair : Pool->VertexBuffersUsedForInstancingBatch)
			{
				if (Pair.Value == &VertexBufferToCheck)
				{
					bUsedForInstancingBatch = true;
					break;
				}
			}

			if (bUsedForInstancingBatch)
			{
				continue;
			}

			if (VertexBufferToCheck.AllocatedByteCount + SizeInBytes <= VertexBufferToCheck.BufferSize)
			{
				VertexBuffer = &VertexBufferToCheck;
				break;
			}
		}

		// Create a new vertex buffer if needed.
		if (VertexBuffer == NULL)
		{
			VertexBuffer = new FDynamicVertexBuffer(SizeInBytes);
			Pool->VertexBuffers.Add(VertexBuffer);
			VertexBuffer->InitResource();
		}

		// Lock the buffer if needed.
		if (VertexBuffer->MappedBuffer == NULL)
		{
			VertexBuffer->Lock();
		}

		// Remember this buffer, we'll try to allocate out of it in the future.
		Pool->CurrentVertexBuffer = VertexBuffer;
	}

	check(VertexBuffer != NULL);
	checkf(VertexBuffer->AllocatedByteCount + SizeInBytes <= VertexBuffer->BufferSize, TEXT("Global vertex buffer allocation failed: BufferSize=%d AllocatedByteCount=%d SizeInBytes=%d"), VertexBuffer->BufferSize, VertexBuffer->AllocatedByteCount, SizeInBytes);
	Allocation.Buffer = VertexBuffer->MappedBuffer + VertexBuffer->AllocatedByteCount;
	Allocation.VertexBuffer = VertexBuffer;
	Allocation.VertexOffset = VertexBuffer->AllocatedByteCount;
	VertexBuffer->AllocatedByteCount += SizeInBytes;

	return Allocation;
}

FGlobalDynamicVertexBuffer::FAllocation FGlobalDynamicVertexBuffer::Allocate(uint32 SizeInBytes, uint32& InOutInstancingBatchId)
{
	if (InOutInstancingBatchId <= 0)
	{
		return Allocate(SizeInBytes);
	}

	FDynamicVertexBuffer* VertexBuffer = NULL;
		
	if (Pool->VertexBuffersUsedForInstancingBatch.Contains(InOutInstancingBatchId))
	{
		VertexBuffer = Pool->VertexBuffersUsedForInstancingBatch[InOutInstancingBatchId];
	}

	if (VertexBuffer && VertexBuffer->AllocatedByteCount + SizeInBytes > VertexBuffer->BufferSize)
	{
		UE_LOG(LogRendererCore, Warning, TEXT("The VertexBuffer for batch instancing was exceeded the limit: %d"), InOutInstancingBatchId);

		InOutInstancingBatchId = 0;

		// Fallback to allocate with no InstancingBatchId
		return Allocate(SizeInBytes);
	}

	if (VertexBuffer == NULL)
	{
		VertexBuffer = new FDynamicVertexBuffer(SizeInBytes);
		Pool->VertexBuffers.Add(VertexBuffer);
		VertexBuffer->InitResource();
			
		Pool->VertexBuffersUsedForInstancingBatch.Add(InOutInstancingBatchId, VertexBuffer);
		UE_LOG(LogRendererCore, Warning, TEXT("A new VertexBuffer was created for batch instancing: %d"), InOutInstancingBatchId);
	}

	// Lock the buffer if needed.
	if (VertexBuffer->MappedBuffer == NULL)
	{
		VertexBuffer->Lock();
	}

	check(VertexBuffer != NULL);
	checkf(VertexBuffer->AllocatedByteCount + SizeInBytes <= VertexBuffer->BufferSize, TEXT("Global vertex buffer allocation failed: BufferSize=%d AllocatedByteCount=%d SizeInBytes=%d"), VertexBuffer->BufferSize, VertexBuffer->AllocatedByteCount, SizeInBytes);

	TotalAllocatedSinceLastCommit += SizeInBytes;
	if (IsRenderAlarmLoggingEnabled())
	{
		UE_LOG(LogRendererCore, Warning, TEXT("FGlobalDynamicVertexBuffer::Allocate(%u), will have allocated %u total this frame"), SizeInBytes, TotalAllocatedSinceLastCommit);
	}

	FAllocation Allocation;
	Allocation.Buffer = VertexBuffer->MappedBuffer + VertexBuffer->AllocatedByteCount;
	Allocation.VertexBuffer = VertexBuffer;
	Allocation.VertexOffset = VertexBuffer->AllocatedByteCount;
	VertexBuffer->AllocatedByteCount += SizeInBytes;

	return Allocation;
}

bool FGlobalDynamicVertexBuffer::IsRenderAlarmLoggingEnabled() const
{
	return GMaxVertexBytesAllocatedPerFrame > 0 && TotalAllocatedSinceLastCommit >= (size_t)GMaxVertexBytesAllocatedPerFrame;
}

void FGlobalDynamicVertexBuffer::Commit()
{
	for (int32 BufferIndex = 0, NumBuffers = Pool->VertexBuffers.Num(); BufferIndex < NumBuffers; ++BufferIndex)
	{
		FDynamicVertexBuffer& VertexBuffer = Pool->VertexBuffers[BufferIndex];
		if (VertexBuffer.MappedBuffer != NULL)
		{
			VertexBuffer.Unlock();
		}
		else if (GGlobalBufferNumFramesUnusedThresold && !VertexBuffer.AllocatedByteCount)
		{
			++VertexBuffer.NumFramesUnused;
			if (VertexBuffer.NumFramesUnused >= GGlobalBufferNumFramesUnusedThresold)
			{
				// Set the vertex buffer used for instancing batch to NULL, just like weak ptr.
				for (TPair<int32, FDynamicVertexBuffer*>& Pair : Pool->VertexBuffersUsedForInstancingBatch)
				{
					if (Pair.Value == &VertexBuffer)
					{
						Pair.Value = NULL;

						UE_LOG(LogRendererCore, Warning, TEXT("The VertexBuffer was released for batch instancing: %d"), Pair.Key);
					}
				}

				// Remove the buffer, assumes they are unordered.
				VertexBuffer.ReleaseResource();
				Pool->VertexBuffers.RemoveAtSwap(BufferIndex);
				--BufferIndex;
				--NumBuffers;
			}
		}
	}
	Pool->CurrentVertexBuffer = NULL;
	TotalAllocatedSinceLastCommit = 0;
}

FGlobalDynamicVertexBuffer InitViewDynamicVertexBuffer;
FGlobalDynamicVertexBuffer InitShadowViewDynamicVertexBuffer;

/*------------------------------------------------------------------------------
	FGlobalDynamicIndexBuffer implementation.
------------------------------------------------------------------------------*/

/**
 * An individual dynamic index buffer.
 */
class FDynamicIndexBuffer : public FIndexBuffer
{
public:
	/** The aligned size of all dynamic index buffers. */
	enum { ALIGNMENT = (1 << 16) }; // 64KB
	/** Pointer to the index buffer mapped in main memory. */
	uint8* MappedBuffer;
	/** Size of the index buffer in bytes. */
	uint32 BufferSize;
	/** Number of bytes currently allocated from the buffer. */
	uint32 AllocatedByteCount;
	/** Stride of the buffer in bytes. */
	uint32 Stride;
	/** Number of successive frames for which AllocatedByteCount == 0. Used as a metric to decide when to free the allocation. */
	int32 NumFramesUnused = 0;

	/** Initialization constructor. */
	explicit FDynamicIndexBuffer(uint32 InMinBufferSize, uint32 InStride)
		: MappedBuffer(NULL)
		, BufferSize(FMath::Max<uint32>(Align(InMinBufferSize,ALIGNMENT),ALIGNMENT))
		, AllocatedByteCount(0)
		, Stride(InStride)
	{
	}

	/**
	 * Locks the vertex buffer so it may be written to.
	 */
	void Lock()
	{
		check(MappedBuffer == NULL);
		check(AllocatedByteCount == 0);
		check(IsValidRef(IndexBufferRHI));
		MappedBuffer = (uint8*)RHILockIndexBuffer(IndexBufferRHI, 0, BufferSize, RLM_WriteOnly);
	}

	/**
	 * Unocks the buffer so the GPU may read from it.
	 */
	void Unlock()
	{
		check(MappedBuffer != NULL);
		check(IsValidRef(IndexBufferRHI));
		RHIUnlockIndexBuffer(IndexBufferRHI);
		MappedBuffer = NULL;
		AllocatedByteCount = 0;
		NumFramesUnused = 0;
	}

	// FRenderResource interface.
	virtual void InitRHI() override
	{
		check(!IsValidRef(IndexBufferRHI));
		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateIndexBuffer(Stride, BufferSize, BUF_Volatile, CreateInfo);
		MappedBuffer = NULL;
		AllocatedByteCount = 0;
	}

	virtual void ReleaseRHI() override
	{
		FIndexBuffer::ReleaseRHI();
		MappedBuffer = NULL;
		AllocatedByteCount = 0;
	}

	virtual FString GetFriendlyName() const override
	{
		return TEXT("FDynamicIndexBuffer");
	}
};

/**
 * A pool of dynamic index buffers.
 */
struct FDynamicIndexBufferPool
{
	/** List of index buffers. */
	TIndirectArray<FDynamicIndexBuffer> IndexBuffers;
	/** The current buffer from which allocations are being made. */
	FDynamicIndexBuffer* CurrentIndexBuffer;
	/** Stride of buffers in this pool. */
	uint32 BufferStride;

	/** Initialization constructor. */
	explicit FDynamicIndexBufferPool(uint32 InBufferStride)
		: CurrentIndexBuffer(NULL)
		, BufferStride(InBufferStride)
	{
	}

	/** Destructor. */
	~FDynamicIndexBufferPool()
	{
		int32 NumIndexBuffers = IndexBuffers.Num();
		for (int32 BufferIndex = 0; BufferIndex < NumIndexBuffers; ++BufferIndex)
		{
			IndexBuffers[BufferIndex].ReleaseResource();
		}
	}
};

FGlobalDynamicIndexBuffer::FGlobalDynamicIndexBuffer()
{
	Pools[0] = new FDynamicIndexBufferPool(sizeof(uint16));
	Pools[1] = new FDynamicIndexBufferPool(sizeof(uint32));
}

FGlobalDynamicIndexBuffer::~FGlobalDynamicIndexBuffer()
{
	for (int32 i = 0; i < 2; ++i)
	{
		delete Pools[i];
		Pools[i] = NULL;
	}
}

FGlobalDynamicIndexBuffer::FAllocation FGlobalDynamicIndexBuffer::Allocate(uint32 NumIndices, uint32 IndexStride)
{
	FAllocation Allocation;

	if (IndexStride != 2 && IndexStride != 4)
	{
		return Allocation;
	}

	FDynamicIndexBufferPool* Pool = Pools[IndexStride >> 2]; // 2 -> 0, 4 -> 1

	uint32 SizeInBytes = NumIndices * IndexStride;
	FDynamicIndexBuffer* IndexBuffer = Pool->CurrentIndexBuffer;
	if (IndexBuffer == NULL || IndexBuffer->AllocatedByteCount + SizeInBytes > IndexBuffer->BufferSize)
	{
		// Find a buffer in the pool big enough to service the request.
		IndexBuffer = NULL;
		for (int32 BufferIndex = 0, NumBuffers = Pool->IndexBuffers.Num(); BufferIndex < NumBuffers; ++BufferIndex)
		{
			FDynamicIndexBuffer& IndexBufferToCheck = Pool->IndexBuffers[BufferIndex];
			if (IndexBufferToCheck.AllocatedByteCount + SizeInBytes <= IndexBufferToCheck.BufferSize)
			{
				IndexBuffer = &IndexBufferToCheck;
				break;
			}
		}

		// Create a new index buffer if needed.
		if (IndexBuffer == NULL)
		{
			IndexBuffer = new FDynamicIndexBuffer(SizeInBytes, Pool->BufferStride);
			Pool->IndexBuffers.Add(IndexBuffer);
			IndexBuffer->InitResource();
		}

		// Lock the buffer if needed.
		if (IndexBuffer->MappedBuffer == NULL)
		{
			IndexBuffer->Lock();
		}
		Pool->CurrentIndexBuffer = IndexBuffer;
	}

	check(IndexBuffer != NULL);
	checkf(IndexBuffer->AllocatedByteCount + SizeInBytes <= IndexBuffer->BufferSize, TEXT("Global index buffer allocation failed: BufferSize=%d AllocatedByteCount=%d SizeInBytes=%d"), IndexBuffer->BufferSize, IndexBuffer->AllocatedByteCount, SizeInBytes);

	Allocation.Buffer = IndexBuffer->MappedBuffer + IndexBuffer->AllocatedByteCount;
	Allocation.IndexBuffer = IndexBuffer;
	Allocation.FirstIndex = IndexBuffer->AllocatedByteCount / IndexStride;
	IndexBuffer->AllocatedByteCount += SizeInBytes;

	return Allocation;
}

void FGlobalDynamicIndexBuffer::Commit()
{
	for (int32 i = 0; i < 2; ++i)
	{
		FDynamicIndexBufferPool* Pool = Pools[i];

		for (int32 BufferIndex = 0, NumBuffers = Pool->IndexBuffers.Num(); BufferIndex < NumBuffers; ++BufferIndex)
		{
			FDynamicIndexBuffer& IndexBuffer = Pool->IndexBuffers[BufferIndex];
			if (IndexBuffer.MappedBuffer != NULL)
			{
				IndexBuffer.Unlock();
			}
			else if (GGlobalBufferNumFramesUnusedThresold && !IndexBuffer.AllocatedByteCount)
			{
				++IndexBuffer.NumFramesUnused;
				if (IndexBuffer.NumFramesUnused >= GGlobalBufferNumFramesUnusedThresold)
				{
					// Remove the buffer, assumes they are unordered.
					IndexBuffer.ReleaseResource();
					Pool->IndexBuffers.RemoveAtSwap(BufferIndex);
					--BufferIndex;
					--NumBuffers;
				}
			}
		}
		Pool->CurrentIndexBuffer = NULL;
	}
}

/*=============================================================================
	FMipBiasFade class
=============================================================================*/

/** Global mip fading settings, indexed by EMipFadeSettings. */
FMipFadeSettings GMipFadeSettings[MipFade_NumSettings] =
{ 
	FMipFadeSettings(0.3f, 0.1f),	// MipFade_Normal
	FMipFadeSettings(2.0f, 1.0f)	// MipFade_Slow
};

/** How "old" a texture must be to be considered a "new texture", in seconds. */
float GMipLevelFadingAgeThreshold = 0.5f;

/**
 *	Sets up a new interpolation target for the mip-bias.
 *	@param ActualMipCount	Number of mip-levels currently in memory
 *	@param TargetMipCount	Number of mip-levels we're changing to
 *	@param LastRenderTime	Timestamp when it was last rendered (FApp::CurrentTime time space)
 *	@param FadeSetting		Which fade speed settings to use
 */
void FMipBiasFade::SetNewMipCount( float ActualMipCount, float TargetMipCount, double LastRenderTime, EMipFadeSettings FadeSetting )
{
	check( ActualMipCount >=0 && TargetMipCount <= ActualMipCount );

	float TimeSinceLastRendered = float(FApp::GetCurrentTime() - LastRenderTime);

	// Is this a new texture or is this not in-game?
	if ( TotalMipCount == 0 || TimeSinceLastRendered >= GMipLevelFadingAgeThreshold || GEnableMipLevelFading < 0.0f )
	{
		// No fading.
		TotalMipCount = ActualMipCount;
		MipCountDelta = 0.0f;
		MipCountFadingRate = 0.0f;
		StartTime = GRenderingRealtimeClock.GetCurrentTime();
		BiasOffset = 0.0f;
		return;
	}

	// Calculate the mipcount we're interpolating towards.
	float CurrentTargetMipCount = TotalMipCount - BiasOffset + MipCountDelta;

	// Is there no change?
	if ( FMath::IsNearlyEqual(TotalMipCount, ActualMipCount) && FMath::IsNearlyEqual(TargetMipCount, CurrentTargetMipCount) )
	{
		return;
	}

	// Calculate the mip-count at our current interpolation point.
	float CurrentInterpolatedMipCount = TotalMipCount - CalcMipBias();

	// Clamp it against the available mip-levels.
	CurrentInterpolatedMipCount = FMath::Clamp<float>(CurrentInterpolatedMipCount, 0, ActualMipCount);

	// Set up a new interpolation from CurrentInterpolatedMipCount to TargetMipCount.
	StartTime = GRenderingRealtimeClock.GetCurrentTime();
	TotalMipCount = ActualMipCount;
	MipCountDelta = TargetMipCount - CurrentInterpolatedMipCount;

	// Don't fade if we're already at the target mip-count.
	if ( FMath::IsNearlyZero(MipCountDelta) )
	{
		MipCountDelta = 0.0f;
		BiasOffset = 0.0f;
		MipCountFadingRate = 0.0f;
	}
	else
	{
		BiasOffset = TotalMipCount - CurrentInterpolatedMipCount;
		if ( MipCountDelta > 0.0f )
		{
			MipCountFadingRate = 1.0f / (GMipFadeSettings[FadeSetting].FadeInSpeed * MipCountDelta);
		}
		else
		{
			MipCountFadingRate = -1.0f / (GMipFadeSettings[FadeSetting].FadeOutSpeed * MipCountDelta);
		}
	}
}

class FTextureSamplerStateCache : public FRenderResource
{
public:
	TMap<FSamplerStateInitializerRHI, FRHISamplerState*> Samplers;

	virtual void ReleaseRHI() override
	{
		for (auto Pair : Samplers)
		{
			Pair.Value->Release();
		}
		Samplers.Empty();
	}
};

TGlobalResource<FTextureSamplerStateCache> GTextureSamplerStateCache;

FRHISamplerState* FTexture::GetOrCreateSamplerState(const FSamplerStateInitializerRHI& Initializer)
{
	// This sampler cache is supposed to be used only from RT
	// Add a lock here if it's used from multiple threads
	check(IsInRenderingThread());
	
	FRHISamplerState** Found = GTextureSamplerStateCache.Samplers.Find(Initializer);
	if (Found)
	{
		return *Found;
	}
	
	FSamplerStateRHIRef NewState = RHICreateSamplerState(Initializer);
	
	// Add an extra reference so we don't have TRefCountPtr in the maps
	NewState->AddRef();
	GTextureSamplerStateCache.Samplers.Add(Initializer, NewState);
	return NewState;
}

bool IsRayTracingEnabled()
{
	checkf(GIsRHIInitialized, TEXT("IsRayTracingEnabled() may only be called once RHI is initialized."));

#if DO_CHECK && WITH_EDITOR
	{
		FString Commandline = FCommandLine::Get();
		bool bIsCookCommandlet = IsRunningCommandlet() && Commandline.Contains(TEXT("run=cook"));
		// This function must not be called while cooking
		if (bIsCookCommandlet)
		{
			return false;
		}
	}
#endif // DO_CHECK && WITH_EDITOR

	extern RENDERCORE_API bool GUseRayTracing;
	return GUseRayTracing;
}

