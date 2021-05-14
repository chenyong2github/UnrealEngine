// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NaniteResources.h"
#include "UnifiedBuffer.h"
#include "RenderGraphResources.h"
#include "RHIGPUReadback.h"

class IFileCacheHandle;

namespace Nanite
{

struct FPageKey
{
	uint32 RuntimeResourceID;
	uint32 PageIndex;
};

FORCEINLINE uint32 GetTypeHash( const FPageKey& Key )
{
	return Key.RuntimeResourceID * 0xFC6014F9u + Key.PageIndex * 0x58399E77u;
}

FORCEINLINE bool operator==( const FPageKey& A, const FPageKey& B )
{
	return A.RuntimeResourceID == B.RuntimeResourceID && A.PageIndex == B.PageIndex;
}

FORCEINLINE bool operator!=(const FPageKey& A, const FPageKey& B)
{
	return !(A == B);
}


// Before deduplication
struct FGPUStreamingRequest
{
	uint32		RuntimeResourceID;
	uint32		PageIndex_NumPages;
	uint32		Priority;
};

// After deduplication
struct FStreamingRequest
{
	FPageKey	Key;
	uint32		Priority;
};

struct FStreamingPageInfo
{
	FStreamingPageInfo* Next;
	FStreamingPageInfo* Prev;

	FPageKey	RegisteredKey;
	FPageKey	ResidentKey;
	
	uint32		GPUPageIndex;
	uint32		LatestUpdateIndex;
	uint32		RefCount;
};

struct FRootPageInfo
{
	uint32	RuntimeResourceID;
	uint32	NumClusters;
};

struct FPendingPage
{
#if !WITH_EDITOR
	uint8*					MemoryPtr;
	FIoRequest				Request;

	// Legacy compatibility
	// Delete when we can rely on IoStore
	IAsyncReadFileHandle*	AsyncHandle;
	IAsyncReadRequest*		AsyncRequest;
#endif

	uint32					GPUPageIndex;
	FPageKey				InstallKey;
#if !UE_BUILD_SHIPPING
	uint32					BytesLeftToStream;
#endif
};

class FRequestsHashTable;
class FStreamingPageUploader;

struct FAsyncState
{
	FRHIGPUBufferReadback*	LatestReadbackBuffer		= nullptr;
	const uint32*			LatestReadbackBufferPtr		= nullptr;
	uint32					NumReadyPages				= 0;
	bool					bUpdateActive				= false;
	bool					bBuffersTransitionedToWrite = false;
};

/*
 * Streaming manager for Nanite.
 */
class FStreamingManager : public FRenderResource
{
public:
	FStreamingManager();
	
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	void	Add( FResources* Resources );
	void	Remove( FResources* Resources );

	ENGINE_API void BeginAsyncUpdate(FRDGBuilder& GraphBuilder);			// Called once per frame before any Nanite rendering has occurred. Must be called before EndUpdate.
	ENGINE_API void EndAsyncUpdate(FRDGBuilder& GraphBuilder);				// Called once per frame before any Nanite rendering has occurred. Must be called after BeginUpdate.
	ENGINE_API bool IsAsyncUpdateInProgress();
	ENGINE_API void	SubmitFrameStreamingRequests(FRDGBuilder& GraphBuilder);		// Called once per frame after the last request has been added.

	const TRefCountPtr< FRDGPooledBuffer >&	GetStreamingRequestsBuffer()	{ return StreamingRequestsBuffer; }

	FRHIShaderResourceView*				GetClusterPageDataSRV() const		{ return ClusterPageData.DataBuffer.SRV; }
	FRHIShaderResourceView*				GetClusterPageHeadersSRV() const	{ return ClusterPageHeaders.DataBuffer.SRV; }
	FRHIShaderResourceView*				GetHierarchySRV() const				{ return Hierarchy.DataBuffer.SRV; }
	FRHIShaderResourceView*				GetRootPagesSRV() const				{ return RootPages.DataBuffer.SRV; }

	inline bool HasResourceEntries() const
	{
		return !RuntimeResourceMap.IsEmpty();
	}

private:
	friend class FStreamingUpdateTask;

	struct FHeapBuffer
	{
		int32					TotalUpload = 0;

		FGrowOnlySpanAllocator	Allocator;

		FScatterUploadBuffer	UploadBuffer;
		FRWByteAddressBuffer	DataBuffer;

		void	Release()
		{
			UploadBuffer.Release();
			DataBuffer.Release();
		}
	};

	FHeapBuffer				ClusterPageData;	// FPackedCluster*, GeometryData { Index, Position, TexCoord, TangentX, TangentZ }*
	FHeapBuffer				ClusterPageHeaders;
	FScatterUploadBuffer	ClusterFixupUploadBuffer;
	FHeapBuffer				Hierarchy;
	FHeapBuffer				RootPages;
	TRefCountPtr< FRDGPooledBuffer > StreamingRequestsBuffer;
	
	uint32					MaxStreamingPages;
	uint32					MaxPendingPages;
	uint32					MaxPageInstallsPerUpdate;
	uint32					MaxStreamingReadbackBuffers;

	uint32					ReadbackBuffersWriteIndex;
	uint32					ReadbackBuffersNumPending;

	TArray<uint32>			NextRootPageVersion;
	uint32					NextUpdateIndex;
	uint32					NumRegisteredStreamingPages;
	uint32					NumPendingPages;
	uint32					NextPendingPageIndex;

	TArray<FRootPageInfo>	RootPageInfos;

#if !UE_BUILD_SHIPPING
	uint64					PrevUpdateTick;
#endif

	TArray< FRHIGPUBufferReadback* >		StreamingRequestReadbackBuffers;
	TArray< FResources* >					PendingAdds;

	TMap< uint32, FResources* >				RuntimeResourceMap;
	TMap< FPageKey, FStreamingPageInfo* >	RegisteredStreamingPagesMap;		// This is updated immediately.
	TMap< FPageKey, FStreamingPageInfo* >	CommittedStreamingPageMap;			// This update is deferred to the point where the page has been loaded and committed to memory.
	TArray< FStreamingRequest >				PrioritizedRequestsHeap;
	FStreamingPageInfo						StreamingPageLRU;

	FStreamingPageInfo*						StreamingPageInfoFreeList;
	TArray< FStreamingPageInfo >			StreamingPageInfos;
	TArray< FFixupChunk* >					StreamingPageFixupChunks;			// Fixup information for resident streaming pages. We need to keep this around to be able to uninstall pages.

	TArray< FPendingPage >					PendingPages;
#if !WITH_EDITOR
	TArray< uint8 >							PendingPageStagingMemory;
#endif
	TArray< uint8 >							PendingPageStagingMemoryLZ;

	FRequestsHashTable*						RequestsHashTable = nullptr;
	FStreamingPageUploader*					PageUploader = nullptr;

	FGraphEventArray						AsyncTaskEvents;
	FAsyncState								AsyncState;

	void CollectDependencyPages( FResources* Resources, TSet< FPageKey >& DependencyPages, const FPageKey& Key );
	void SelectStreamingPages( FResources* Resources, TArray< FPageKey >& SelectedPages, TSet<FPageKey>& SelectedPagesSet, uint32 RuntimeResourceID, uint32 PageIndex, uint32 MaxSelectedPages );

	void RegisterStreamingPage( FStreamingPageInfo* Page, const FPageKey& Key );
	void UnregisterPage( const FPageKey& Key );
	void MovePageToFreeList( FStreamingPageInfo* Page );

	void ApplyFixups( const FFixupChunk& FixupChunk, const FResources& Resources, uint32 PageIndex, uint32 GPUPageIndex );

	bool ArePageDependenciesCommitted(uint32 RuntimeResourceID, uint32 PageIndex, uint32 DependencyPageStart, uint32 DependencyPageNum);

	// Returns whether any work was done and page/hierarchy buffers were transitioned to compute writable state
	bool ProcessNewResources( FRDGBuilder& GraphBuilder);
	
	uint32 DetermineReadyPages();
	void InstallReadyPages( uint32 NumReadyPages );

	void AsyncUpdate();

	void ClearStreamingRequestCount(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef BufferUAVRef);
#if DO_CHECK
	void VerifyPageLRU( FStreamingPageInfo& List, uint32 TargetListLength, bool bCheckUpdateIndex );
#endif
};

extern ENGINE_API TGlobalResource< FStreamingManager > GStreamingManager;

} // namespace Nanite