// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/NaniteStreamingManager.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "UnifiedBuffer.h"
#include "CommonRenderResources.h"
#include "FileCache/FileCache.h"
#include "DistanceFieldAtlas.h"
#include "ClearQuad.h"
#include "RenderGraphUtils.h"
#include "Logging/LogMacros.h"

#define MAX_STREAMING_PAGES_BITS		11u
#define MAX_STREAMING_PAGES				(1u << MAX_STREAMING_PAGES_BITS)

#define MIN_ROOT_PAGES_CAPACITY			2048u

#define MAX_PENDING_PAGES				32u
#define MAX_INSTALLS_PER_UPDATE			16u

#define MAX_REQUESTS_HASH_TABLE_SIZE	(MAX_STREAMING_REQUESTS << 1)
#define MAX_REQUESTS_HASH_TABLE_MASK	(MAX_REQUESTS_HASH_TABLE_SIZE - 1)
#define INVALID_HASH_ENTRY				0xFFFFFFFFu

#define INVALID_RUNTIME_RESOURCE_ID		0xFFFFFFFFu
#define INVALID_PAGE_INDEX				0xFFFFFFFFu

float GNaniteStreamingBandwidthLimit = -1.0f;
static FAutoConsoleVariableRef CVarNaniteStreamingBandwidthLimit(
	TEXT( "r.Nanite.StreamingBandwidthLimit" ),
	GNaniteStreamingBandwidthLimit,
	TEXT( "Streaming bandwidth limit in megabytes per second. Negatives values are interpreted as unlimited. " )
);

DECLARE_CYCLE_STAT( TEXT("StreamingManager_Update"),STAT_NaniteStreamingManagerUpdate,	STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("ProcessReadback"),		STAT_NaniteProcessReadback,			STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("UpdatePriorities"),		STAT_NaniteUpdatePriorities,		STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("DeduplicateRequests"),	STAT_NaniteDeduplicateRequests,		STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("SelectStreamingPages"),	STAT_NaniteSelectStreamingPages,	STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("VerifyLRU"),				STAT_NaniteVerifyLRU,				STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("PrioritySort"),			STAT_NanitePrioritySort,			STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("UpdateLRU"),				STAT_NaniteUpdateLRU,				STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("Upload" ),				STAT_NaniteUpload,					STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("CheckReadyPages" ),		STAT_NaniteCheckReadyPages,			STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("InstallStreamingPages" ),	STAT_NaniteInstallStreamingPages,	STATGROUP_Nanite );
DECLARE_CYCLE_STAT( TEXT("InstallNewResources" ),	STAT_NaniteInstallNewResources,		STATGROUP_Nanite );

DECLARE_DWORD_COUNTER_STAT(		TEXT("PageInstalls"),				STAT_NanitePageInstalls,					STATGROUP_Nanite );
DECLARE_DWORD_COUNTER_STAT(		TEXT("StreamingRequests"),			STAT_NaniteStreamingRequests,				STATGROUP_Nanite );
DECLARE_DWORD_COUNTER_STAT(		TEXT("UniqueStreamingRequests"),	STAT_NaniteUniqueStreamingRequests,			STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("TotalPages"),					STAT_NaniteTotalPages,						STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("RegisteredStreamingPages"),	STAT_NaniteRegisteredStreamingPages,		STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("InstalledPages"),				STAT_NaniteInstalledPages,					STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("PendingPages"),				STAT_NanitePendingPages,					STATGROUP_Nanite );
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("RootPages"),					STAT_NaniteRootPages,						STATGROUP_Nanite );

DECLARE_LOG_CATEGORY_EXTERN(LogNaniteStreaming, Log, All);
DEFINE_LOG_CATEGORY(LogNaniteStreaming);

namespace Nanite
{


// Lean hash table for deduplicating requests.
// Linear probing hash table that only supports add and never grows.
// This is intended to be kept alive over the duration of the program, so allocation and clearing only has to happen once.
// TODO: Unify with VT?
class FRequestsHashTable
{
	FStreamingRequest*		HashTable;
	uint32*					ElementIndices;	// List of indices to unique elements of HashTable
	uint32					NumElements;	// Number of unique elements in HashTable
public:
	FRequestsHashTable()
	{
		check(FMath::IsPowerOfTwo(MAX_REQUESTS_HASH_TABLE_SIZE));
		HashTable = new FStreamingRequest[MAX_REQUESTS_HASH_TABLE_SIZE];
		ElementIndices = new uint32[MAX_REQUESTS_HASH_TABLE_SIZE];
		for(uint32 i = 0; i < MAX_REQUESTS_HASH_TABLE_SIZE; i++)
		{
			HashTable[i].Key.RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;
		}
		NumElements = 0;
	}
	~FRequestsHashTable()
	{
		delete[] HashTable;
		delete[] ElementIndices;
		HashTable = nullptr;
		ElementIndices = nullptr;
	}

	FORCEINLINE void AddRequest(const FStreamingRequest& Request)
	{
		uint32 TableIndex = GetTypeHash(Request.Key) & MAX_REQUESTS_HASH_TABLE_MASK;

		while(true)
		{
			FStreamingRequest& TableEntry = HashTable[TableIndex];
			if(TableEntry.Key == Request.Key)
			{
				// Found it. Just update the key.
				TableEntry.Priority = FMath::Max( TableEntry.Priority, Request.Priority );
				return;
			}

			if(TableEntry.Key.RuntimeResourceID == INVALID_RUNTIME_RESOURCE_ID)
			{
				// Empty slot. Take it and add this to cell to the elements list.
				TableEntry = Request;
				ElementIndices[NumElements++] = TableIndex;
				return;
			}

			// Slot was taken by someone else. Move on to next slot.
			TableIndex = (TableIndex + 1) & MAX_REQUESTS_HASH_TABLE_MASK;
		}
	}

	uint32 GetNumElements() const
	{
		return NumElements;
	}

	const FStreamingRequest& GetElement(uint32 Index) const
	{
		check( Index < NumElements );
		return HashTable[ElementIndices[Index]];
	}

	// Clear by looping through unique elements. Cost is proportional to number of unique elements, not the whole table.
	void Clear()
	{
		for( uint32 i = 0; i < NumElements; i++ )
		{
			FStreamingRequest& Request = HashTable[ ElementIndices[ i ] ];
			Request.Key.RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;
		}
		NumElements = 0;
	}
};


FStreamingManager::FStreamingManager() :
	MaxStreamingPages(MAX_STREAMING_PAGES),
	MaxPendingPages(MAX_PENDING_PAGES),
	MaxStreamingReadbackBuffers(4u),
	ReadbackBuffersWriteIndex(0),
	ReadbackBuffersNumPending(0),
	NextRuntimeResourceID(0),
	NextUpdateIndex(0),
	NumRegisteredStreamingPages(0),
	NumPendingPages(0),
	NextPendingPageIndex(0)
#if !UE_BUILD_SHIPPING
	,PrevUpdateTick(0)
#endif
{
	LLM_SCOPE(ELLMTag::Nanite);

	check( MaxStreamingPages <= MAX_GPU_PAGES );
	StreamingRequestReadbackBuffers.AddZeroed( MaxStreamingReadbackBuffers );

	// Initialize pages
	StreamingPageInfos.AddUninitialized( MaxStreamingPages );
	for( uint32 i = 0; i < MaxStreamingPages; i++ )
	{
		FStreamingPageInfo& Page = StreamingPageInfos[ i ];
		Page.RegisteredKey = { INVALID_RUNTIME_RESOURCE_ID, INVALID_PAGE_INDEX };
		Page.ResidentKey = { INVALID_RUNTIME_RESOURCE_ID, INVALID_PAGE_INDEX };
		Page.GPUPageIndex = i;
	}

	// Add pages to free list
	StreamingPageInfoFreeList = &StreamingPageInfos[0];
	for( uint32 i = 1; i < MaxStreamingPages; i++ )
	{
		StreamingPageInfos[ i - 1 ].Next = &StreamingPageInfos[ i ];
	}
	StreamingPageInfos[ MaxStreamingPages - 1 ].Next = nullptr;

	// Initialize LRU sentinels
	StreamingPageLRU.RegisteredKey		= { INVALID_RUNTIME_RESOURCE_ID, INVALID_PAGE_INDEX };
	StreamingPageLRU.ResidentKey		= { INVALID_RUNTIME_RESOURCE_ID, INVALID_PAGE_INDEX };
	StreamingPageLRU.GPUPageIndex		= INVALID_PAGE_INDEX;
	StreamingPageLRU.LatestUpdateIndex	= 0xFFFFFFFFu;
	StreamingPageLRU.RefCount			= 0xFFFFFFFFu;
	StreamingPageLRU.Next				= &StreamingPageLRU;
	StreamingPageLRU.Prev				= &StreamingPageLRU;

	StreamingPageFixupChunks.SetNumUninitialized( MaxStreamingPages );

	PendingPages.SetNum( MaxPendingPages );

	RequestsHashTable = new FRequestsHashTable();
}

FStreamingManager::~FStreamingManager()
{
	delete RequestsHashTable;
}

void FStreamingManager::InitRHI()
{
	LLM_SCOPE(ELLMTag::Nanite);
	ClusterPageData.DataBuffer.Initialize( sizeof(uint32), 0, TEXT("FStreamingManagerClusterPageDataInitial") );
	ClusterPageHeaders.DataBuffer.Initialize( sizeof(uint32), 0, TEXT("FStreamingManagerClusterPageHeadersInitial") );
	Hierarchy.DataBuffer.Initialize( sizeof(uint32), 0, TEXT("FStreamingManagerHierarchyInitial") );	// Dummy allocation to make sure it is a valid resource
}

void FStreamingManager::ReleaseRHI()
{
	LLM_SCOPE(ELLMTag::Nanite);
	for( uint32 i = 0; i < MaxStreamingReadbackBuffers; i++ )
	{
		if( StreamingRequestReadbackBuffers[i] )
		{
			delete StreamingRequestReadbackBuffers[i];
			StreamingRequestReadbackBuffers[i] = nullptr;
		}
	}

	ClusterPageData.Release();
	ClusterPageHeaders.Release();
	Hierarchy.Release();
	ClusterFixupUploadBuffer.Release();
	StreamingRequestsBuffer.SafeRelease();
}

void FStreamingManager::Add( FResources* Resources )
{
	LLM_SCOPE(ELLMTag::Nanite);
	if( Resources->RuntimeResourceID == INVALID_RUNTIME_RESOURCE_ID )
	{
		Resources->HierarchyOffset = Hierarchy.Allocator.Allocate(Resources->HierarchyNodes.Num());
		Hierarchy.TotalUpload += Resources->HierarchyNodes.Num();
		INC_DWORD_STAT_BY( STAT_NaniteTotalPages, Resources->PageStreamingStates.Num() );
		INC_DWORD_STAT_BY( STAT_NaniteRootPages, 1 );

		Resources->RootPageIndex = RootPagesAllocator.Allocate( 1 );

		Resources->RuntimeResourceID = NextRuntimeResourceID++;
		RuntimeResourceMap.Add( Resources->RuntimeResourceID, Resources );
		
		PendingAdds.Add( Resources );
	}
}

void FStreamingManager::Remove( FResources* Resources )
{
	LLM_SCOPE(ELLMTag::Nanite);
	if( Resources->RuntimeResourceID != INVALID_RUNTIME_RESOURCE_ID )
	{
		Hierarchy.Allocator.Free( Resources->HierarchyOffset, Resources->HierarchyNodes.Num() );
		Resources->HierarchyOffset = -1;

		RootPagesAllocator.Free( Resources->RootPageIndex, 1 );
		Resources->RootPageIndex = -1;

		uint32 NumResourcePages = Resources->PageStreamingStates.Num();
		INC_DWORD_STAT_BY( STAT_NaniteTotalPages, NumResourcePages );
		DEC_DWORD_STAT_BY( STAT_NaniteRootPages, 1 );

		// Move all registered pages to the free list. No need to properly uninstall them as they are no longer referenced from the hierarchy.
		for( uint32 PageIndex = 0; PageIndex < NumResourcePages; PageIndex++ )
		{
			FPageKey Key = { Resources->RuntimeResourceID, PageIndex };
			FStreamingPageInfo* Page;
			if( RegisteredStreamingPagesMap.RemoveAndCopyValue(Key, Page) )
			{
				Page->RegisteredKey.RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;	// Mark as free, so we won't try to uninstall it later
				MovePageToFreeList( Page );
			}
		}

		RuntimeResourceMap.Remove( Resources->RuntimeResourceID );
		Resources->RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;
		PendingAdds.Remove( Resources );
	}
}

FORCEINLINE bool IsRootPage( uint32 PageIndex )	// Keep in sync with ClusterCulling.usf
{
	return PageIndex == 0;
}

void FStreamingManager::CollectDependencyPages( FResources* Resources, TSet< FPageKey >& DependencyPages, const FPageKey& Key )
{
	LLM_SCOPE(ELLMTag::Nanite);
	if( DependencyPages.Find( Key ) )
		return;

	DependencyPages.Add( Key );

	FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[ Key.PageIndex ];

	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = Resources->PageDependencies[ PageStreamingState.DependenciesStart + i ];

		if( IsRootPage( DependencyPageIndex ) )
			continue;

		FPageKey ChildKey = { Key.RuntimeResourceID, DependencyPageIndex };
		if( DependencyPages.Find( ChildKey ) == nullptr )
		{
			CollectDependencyPages( Resources, DependencyPages, ChildKey );
		}
	}
}

void FStreamingManager::SelectStreamingPages( FResources* Resources, TArray< FPageKey >& SelectedPages, TSet<FPageKey>& SelectedPagesSet, uint32 RuntimeResourceID, uint32 PageIndex, uint32 Priority, uint32 MaxSelectedPages )
{
	LLM_SCOPE(ELLMTag::Nanite);
	FPageKey Key = { RuntimeResourceID, PageIndex };
	if( SelectedPagesSet.Find( Key ) || (uint32)SelectedPages.Num() >= MaxSelectedPages )
		return;

	SelectedPagesSet.Add( Key );

	const FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[ PageIndex ];
	
	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = Resources->PageDependencies[ PageStreamingState.DependenciesStart + i ];
		if( IsRootPage( DependencyPageIndex ) )
			continue;

		FPageKey DependencyKey = { RuntimeResourceID, DependencyPageIndex };
		if( RegisteredStreamingPagesMap.Find( DependencyKey ) == nullptr )
		{
			SelectStreamingPages( Resources, SelectedPages, SelectedPagesSet, RuntimeResourceID, DependencyPageIndex, Priority + 100u, MaxSelectedPages );
		}
	}

	if( (uint32)SelectedPages.Num() < MaxSelectedPages )
	{
		SelectedPages.Push( { RuntimeResourceID, PageIndex } );	// We need to write ourselves after our dependencies
	}
}

void FStreamingManager::RegisterStreamingPage( FStreamingPageInfo* Page, const FPageKey& Key )
{
	LLM_SCOPE(ELLMTag::Nanite);
	check( !IsRootPage( Key.PageIndex ) );

	FResources** Resources = RuntimeResourceMap.Find( Key.RuntimeResourceID );
	check( Resources != nullptr );
	
	TArray< FPageStreamingState >& PageStreamingStates = (*Resources)->PageStreamingStates;
	FPageStreamingState& PageStreamingState = PageStreamingStates[ Key.PageIndex ];
	
	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = ( *Resources )->PageDependencies[ PageStreamingState.DependenciesStart + i ];
		if( IsRootPage( DependencyPageIndex ) )
			continue;

		FPageKey DependencyKey = { Key.RuntimeResourceID, DependencyPageIndex };
		FStreamingPageInfo** DependencyPage = RegisteredStreamingPagesMap.Find( DependencyKey );
		check( DependencyPage != nullptr );
		(*DependencyPage)->RefCount++;
	}

	// Insert at the front of the LRU
	FStreamingPageInfo& LRUSentinel = StreamingPageLRU;

	Page->Prev = &LRUSentinel;
	Page->Next = LRUSentinel.Next;
	LRUSentinel.Next->Prev = Page;
	LRUSentinel.Next = Page;

	Page->RegisteredKey = Key;
	Page->LatestUpdateIndex = NextUpdateIndex;
	Page->RefCount = 0;

	// Register Page
	RegisteredStreamingPagesMap.Add(Key, Page);

	NumRegisteredStreamingPages++;
	INC_DWORD_STAT( STAT_NaniteRegisteredStreamingPages );
}

void FStreamingManager::UnregisterPage( const FPageKey& Key )
{
	LLM_SCOPE(ELLMTag::Nanite);
	check( !IsRootPage( Key.PageIndex ) );

	FResources** Resources = RuntimeResourceMap.Find( Key.RuntimeResourceID );
	check( Resources != nullptr );

	FStreamingPageInfo** PagePtr = RegisteredStreamingPagesMap.Find( Key );
	check( PagePtr != nullptr );
	FStreamingPageInfo* Page = *PagePtr;
	
	// Decrement reference counts of dependencies.
	TArray< FPageStreamingState >& PageStreamingStates = ( *Resources )->PageStreamingStates;
	FPageStreamingState& PageStreamingState = PageStreamingStates[ Key.PageIndex ];
	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = ( *Resources )->PageDependencies[ PageStreamingState.DependenciesStart + i ];
		if( IsRootPage( DependencyPageIndex ) )
			continue;

		FPageKey DependencyKey = { Key.RuntimeResourceID, DependencyPageIndex };
		FStreamingPageInfo** DependencyPage = RegisteredStreamingPagesMap.Find( DependencyKey );
		check( DependencyPage != nullptr );
		( *DependencyPage )->RefCount--;
	}

	RegisteredStreamingPagesMap.Remove( Key );
	MovePageToFreeList( Page );
}

void FStreamingManager::MovePageToFreeList( FStreamingPageInfo* Page )
{
	// Unlink
	FStreamingPageInfo* OldNext = Page->Next;
	FStreamingPageInfo* OldPrev = Page->Prev;
	OldNext->Prev = OldPrev;
	OldPrev->Next = OldNext;

	// Add to free list
	Page->Next = StreamingPageInfoFreeList;
	StreamingPageInfoFreeList = Page;

	NumRegisteredStreamingPages--;
	DEC_DWORD_STAT( STAT_NaniteRegisteredStreamingPages );
}

bool FStreamingManager::ArePageDependenciesCommitted(uint32 RuntimeResourceID, uint32 PageIndex, uint32 DependencyPageStart, uint32 DependencyPageNum)
{
	bool bResult = true;

	if (DependencyPageNum == 1)
	{
		// If there is only one dependency, we don't have to check as it is the page we are about to install.
		check(DependencyPageStart == PageIndex);
	}
	else if (DependencyPageNum > 1)	
	{
		for (uint32 i = 0; i < DependencyPageNum; i++)
		{
			uint32 DependencyPage = DependencyPageStart + i;
			FPageKey DependencyKey = { RuntimeResourceID, DependencyPage };
			FStreamingPageInfo** DependencyPagePtr = CommittedStreamingPageMap.Find(DependencyKey);
			if (DependencyPagePtr == nullptr || (*DependencyPagePtr)->ResidentKey != DependencyKey)	// Is the page going to be committed after this batch and does it already have its fixupchunk loaded?
			{
				bResult = false;
				break;
			}
		}
	}

	return bResult;
}

// Applies the fixups required to install/uninstall a page.
// Hierarchy references are patched up and leaf flags of parent clusters are set accordingly.
// GPUPageIndex == INVALID_PAGE_INDEX signals that the page should be uninstalled.
void FStreamingManager::ApplyFixups( const FFixupChunk& FixupChunk, const FResources& Resources, uint32 PageIndex, uint32 GPUPageIndex )
{
	LLM_SCOPE(ELLMTag::Nanite);

	const uint32 RuntimeResourceID = Resources.RuntimeResourceID;
	const uint32 HierarchyOffset = Resources.HierarchyOffset;
	bool bIsUninstall = ( GPUPageIndex == INVALID_PAGE_INDEX );
	uint32 Flags = bIsUninstall ? NANITE_CLUSTER_FLAG_LEAF : 0;

	// Fixup clusters
	for( uint32 i = 0; i < FixupChunk.Header.NumClusterFixups; i++ )
	{
		const FClusterFixup& Fixup = FixupChunk.GetClusterFixup( i );

		bool bPageDependenciesCommitted = bIsUninstall || ArePageDependenciesCommitted(RuntimeResourceID, PageIndex, Fixup.GetPageDependencyStart(), Fixup.GetPageDependencyNum());
		if (!bPageDependenciesCommitted)
			continue;
		
		uint32 TargetPageIndex = Fixup.GetPageIndex();
		uint32 TargetGPUPageIndex = INVALID_PAGE_INDEX;
		uint32 NumTargetPageClusters = 0;

		if( IsRootPage( TargetPageIndex ) )
		{
			TargetGPUPageIndex = MaxStreamingPages + Resources.RootPageIndex;
			NumTargetPageClusters = RootPageInfos[ Resources.RootPageIndex ].NumClusters;
		}
		else
		{
			FPageKey TargetKey = { RuntimeResourceID, TargetPageIndex };
			FStreamingPageInfo** TargetPagePtr = CommittedStreamingPageMap.Find( TargetKey );

			check( bIsUninstall || TargetPagePtr );
			if (TargetPagePtr)
			{
				FStreamingPageInfo* TargetPage = *TargetPagePtr;
				FFixupChunk& TargetFixupChunk = StreamingPageFixupChunks[TargetPage->GPUPageIndex];
				check(StreamingPageInfos[TargetPage->GPUPageIndex].ResidentKey == TargetKey);

				NumTargetPageClusters = TargetFixupChunk.Header.NumClusters;
				check(Fixup.GetClusterIndex() < NumTargetPageClusters);

				TargetGPUPageIndex = TargetPage->GPUPageIndex;
			}
		}
		
		if(TargetGPUPageIndex != INVALID_PAGE_INDEX)
		{
			uint32 ClusterIndex = Fixup.GetClusterIndex();
			uint32 FlagsOffset = offsetof( FPackedTriCluster, Flags );
			uint32 Offset = ( TargetGPUPageIndex << CLUSTER_PAGE_SIZE_BITS ) + ( ( FlagsOffset >> 4 ) * NumTargetPageClusters + ClusterIndex ) * 16 + ( FlagsOffset & 15 );
			ClusterFixupUploadBuffer.Add( Offset / sizeof( uint32 ), &Flags, 1 );
		}
	}

	// Fixup hierarchy
	for( uint32 i = 0; i < FixupChunk.Header.NumHierachyFixups; i++ )
	{
		const FHierarchyFixup& Fixup = FixupChunk.GetHierarchyFixup( i );

		bool bPageDependenciesCommitted = bIsUninstall || ArePageDependenciesCommitted(RuntimeResourceID, PageIndex, Fixup.GetPageDependencyStart(), Fixup.GetPageDependencyNum());
		if (!bPageDependenciesCommitted)
			continue;

		FPageKey TargetKey = { RuntimeResourceID, Fixup.GetPageIndex() };
		uint32 TargetGPUPageIndex = INVALID_PAGE_INDEX;
		if (!bIsUninstall)
		{
			if (IsRootPage(TargetKey.PageIndex))
			{
				TargetGPUPageIndex = MaxStreamingPages + Resources.RootPageIndex;
			}
			else
			{
				FStreamingPageInfo** TargetPagePtr = CommittedStreamingPageMap.Find(TargetKey);
				check(TargetPagePtr);
				check((*TargetPagePtr)->ResidentKey == TargetKey);
				TargetGPUPageIndex = (*TargetPagePtr)->GPUPageIndex;
			}
		}
		
		// Uninstalls are unconditional. The same uninstall might happen more than once.
		// If this page is getting uninstalled it also means it wont be reinstalled and any split groups can't be satisfied, so we can safely uninstall them.	
		
		uint32 HierarchyNodeIndex = Fixup.GetNodeIndex();
		check( HierarchyNodeIndex < (uint32)Resources.HierarchyNodes.Num() );
		uint32 ChildIndex = Fixup.GetChildIndex();
		uint32 ChildStartReference = bIsUninstall ? 0xFFFFFFFFu : ( ( TargetGPUPageIndex << MAX_CLUSTERS_PER_PAGE_BITS ) | Fixup.GetClusterGroupPartStartIndex() );
		uint32 Offset = ( size_t )&( ( (FPackedHierarchyNode*)0 )[ HierarchyOffset + HierarchyNodeIndex ].Misc[ ChildIndex ].ChildStartReference );
		Hierarchy.UploadBuffer.Add( Offset / sizeof( uint32 ), &ChildStartReference );
	}
}

bool FStreamingManager::ProcessPendingPages( FRHICommandListImmediate& RHICmdList )
{
	LLM_SCOPE(ELLMTag::Nanite);
	SCOPED_GPU_STAT(RHICmdList, NaniteStreaming);

	uint32 NumReadyPages = 0;
	uint32 StartPendingPageIndex = ( NextPendingPageIndex + MaxPendingPages - NumPendingPages ) % MaxPendingPages;

#if !UE_BUILD_SHIPPING
	uint64 UpdateTick = FPlatformTime::Cycles64();
	uint64 DeltaTick = PrevUpdateTick ? UpdateTick - PrevUpdateTick : 0;
	uint32 SimulatedBytesRemaining = FPlatformTime::ToSeconds64( DeltaTick ) * GNaniteStreamingBandwidthLimit * 1048576.0;
	PrevUpdateTick = UpdateTick;
#endif

	// Check how many pages are ready
	{
		SCOPE_CYCLE_COUNTER( STAT_NaniteCheckReadyPages );

		for( uint32 i = 0; i < NumPendingPages; i++ )
		{
			uint32 PendingPageIndex = ( StartPendingPageIndex + i ) % MaxPendingPages;
			FPendingPage& PendingPage = PendingPages[ PendingPageIndex ];
			bool bIsReady = true;

#if WITH_EDITOR == 0
			for( FGraphEventRef& EventRef : PendingPage.CompletionEvents )
			{
				if( !EventRef->IsComplete() )
				{
					bIsReady = false;
					break;
				}
			}

			if( !bIsReady )
				break;

			PendingPage.CompletionEvents.Empty();
#endif

#if !UE_BUILD_SHIPPING
			if( GNaniteStreamingBandwidthLimit >= 0.0 )
			{
				uint32 SimulatedBytesRead = FMath::Min( PendingPage.BytesLeftToStream, SimulatedBytesRemaining );
				PendingPage.BytesLeftToStream -= SimulatedBytesRead;
				SimulatedBytesRemaining -= SimulatedBytesRead;
				if( PendingPage.BytesLeftToStream > 0 )
					break;
			}
#endif

			NumReadyPages++;

			if( NumReadyPages >= MAX_INSTALLS_PER_UPDATE )
				break;
		}
	}
	
	if( NumReadyPages == 0 )
		return false;

	// Install ready pages
	{
		ClusterFixupUploadBuffer.Init(MAX_INSTALLS_PER_UPDATE * MAX_CLUSTERS_PER_PAGE, sizeof(uint32), false, TEXT("ClusterFixupUploadBuffer"));	// No more parents than children, so no more than MAX_CLUSTER_PER_PAGE parents need to be fixed
		ClusterPageData.UploadBuffer.Init(MAX_INSTALLS_PER_UPDATE, CLUSTER_PAGE_SIZE, false, TEXT("ClusterPageDataUploadBuffer"));
		ClusterPageHeaders.UploadBuffer.Init(MAX_INSTALLS_PER_UPDATE, sizeof(uint32), false, TEXT("ClusterPageHeadersUploadBuffer"));
		Hierarchy.UploadBuffer.Init(2 * MAX_INSTALLS_PER_UPDATE  * MAX_CLUSTERS_PER_PAGE, sizeof(uint32), false, TEXT("HierarchyUploadBuffer"));	// Allocate enough to load all selected pages and evict old pages

		SCOPE_CYCLE_COUNTER( STAT_NaniteInstallStreamingPages );

		// Batched page install:
		// GPU uploads are unordered, so we need to make sure we have no overlapping writes.
		// For actual page uploads, we only upload the last page that ends up on a given GPU page.

		// Fixups are handled with set of UploadBuffers that are executed AFTER page upload.
		// To ensure we don't end up fixing up the same addresses more than once, we only perform the fixup associated with the first uninstall and the last install on a given GPU page.
		// If a page ends up being both installed and uninstalled in the same frame, we only install it to prevent a race.
		// Uninstall fixup depends on StreamingPageFixupChunks that is also updated by installs. To prevent races we perform all uninstalls before installs.
		
		// Calculate first and last Pending Page Index update for each GPU page.
		TMap<uint32, uint32> GPUPageToLastPendingPageIndex;
		for (uint32 i = 0; i < NumReadyPages; i++)
		{
			uint32 PendingPageIndex = (StartPendingPageIndex + i) % MaxPendingPages;
			FPendingPage& PendingPage = PendingPages[PendingPageIndex];
			
			// Update when the GPU page was touched for the last time.
			FResources** Resources = RuntimeResourceMap.Find(PendingPage.InstallKey.RuntimeResourceID);
			if(Resources)
			{
				GPUPageToLastPendingPageIndex.Add(PendingPage.GPUPageIndex, PendingPageIndex);
			}
		}

		TSet<FPageKey> BatchNewPageKeys;
		for (auto& Elem : GPUPageToLastPendingPageIndex)
		{
			uint32 GPUPageIndex = Elem.Key;

			// Remove uninstalled pages from streaming map, so we won't try to do uninstall fixup on them.
			FStreamingPageInfo& StreamingPageInfo = StreamingPageInfos[GPUPageIndex];
			if (StreamingPageInfo.ResidentKey.RuntimeResourceID != INVALID_RUNTIME_RESOURCE_ID)
			{
				CommittedStreamingPageMap.Remove(StreamingPageInfo.ResidentKey);
			}

			// Mark newly installed page
			FPendingPage& PendingPage = PendingPages[Elem.Value];
			BatchNewPageKeys.Add(PendingPage.InstallKey);
		}

		// Uninstall pages
		// We are uninstalling pages in a separate pass as installs will also overwrite the GPU page fixup information we need for uninstalls.
		for (auto& Elem : GPUPageToLastPendingPageIndex)
		{
			uint32 GPUPageIndex = Elem.Key;
			FStreamingPageInfo& StreamingPageInfo = StreamingPageInfos[GPUPageIndex];

			// Uninstall GPU page
			if (StreamingPageInfo.ResidentKey.RuntimeResourceID != INVALID_RUNTIME_RESOURCE_ID)
			{
				// Apply fixups to uninstall page. No need to fix up anything if resource is gone.
				FResources** Resources = RuntimeResourceMap.Find(StreamingPageInfo.ResidentKey.RuntimeResourceID);
				if (Resources)
				{
					// Prevent race between installs and uninstalls of the same page. Only uninstall if the page is not going to be installed again.
					if (!BatchNewPageKeys.Contains(StreamingPageInfo.ResidentKey))
					{
						ApplyFixups(StreamingPageFixupChunks[GPUPageIndex], **Resources, INVALID_PAGE_INDEX, INVALID_PAGE_INDEX);
					}
				}
			}

			StreamingPageInfo.ResidentKey.RuntimeResourceID = INVALID_RUNTIME_RESOURCE_ID;	// Only uninstall it the first time.
			DEC_DWORD_STAT(STAT_NaniteInstalledPages);
		}

		// Commit to streaming map, so install fixups will happen on all pages
		for (auto& Elem : GPUPageToLastPendingPageIndex)
		{
			uint32 GPUPageIndex = Elem.Key;
			uint32 LastPendingPageIndex = Elem.Value;
			FPendingPage& PendingPage = PendingPages[LastPendingPageIndex];

			FResources** Resources = RuntimeResourceMap.Find(PendingPage.InstallKey.RuntimeResourceID);
			if (Resources)
			{
				CommittedStreamingPageMap.Add(PendingPage.InstallKey, &StreamingPageInfos[GPUPageIndex]);
			}
		}

		// Install pages
		// Must be processed in PendingPages order so FFixupChunks are loaded when we need them.
		for (uint32 i = 0; i < NumReadyPages; i++)
		{
			uint32 LastPendingPageIndex = (StartPendingPageIndex + i) % MaxPendingPages;
			uint32* PagePtr = GPUPageToLastPendingPageIndex.Find(PendingPages[LastPendingPageIndex].GPUPageIndex);
			if (PagePtr == nullptr || *PagePtr != LastPendingPageIndex)
				continue;

			FPendingPage& PendingPage = PendingPages[LastPendingPageIndex];
			FStreamingPageInfo& StreamingPageInfo = StreamingPageInfos[PendingPage.GPUPageIndex];
			
			FResources** Resources = RuntimeResourceMap.Find( PendingPage.InstallKey.RuntimeResourceID );
			check(Resources);

			TArray< FPageStreamingState >& PageStreamingStates = ( *Resources )->PageStreamingStates;
			const FPageStreamingState& PageStreamingState = PageStreamingStates[ PendingPage.InstallKey.PageIndex ];
			FStreamingPageInfo* StreamingPage = &StreamingPageInfos[ PendingPage.GPUPageIndex ];

			CommittedStreamingPageMap.Add(PendingPage.InstallKey, StreamingPage);
#if WITH_EDITOR
			FByteBulkData& BulkData = ( *Resources )->StreamableClusterPages;
			check( BulkData.IsBulkDataLoaded() && BulkData.GetBulkDataSize() > 0 );

			uint8* Ptr = (uint8*)BulkData.LockReadOnly() + PageStreamingState.BulkOffset;
			uint32 FixupChunkSize = ( (FFixupChunk*)Ptr )->GetSize();

			FFixupChunk* FixupChunk = &StreamingPageFixupChunks[ PendingPage.GPUPageIndex ];
			FMemory::Memcpy( FixupChunk, Ptr, FixupChunkSize );

			void* Dst = ClusterPageData.UploadBuffer.Add_GetRef( PendingPage.GPUPageIndex );
			FMemory::Memcpy( Dst, Ptr + FixupChunkSize, PageStreamingState.BulkSize - FixupChunkSize );
#else
			// Read header of FixupChunk so the length can be calculated
			FFixupChunk* FixupChunk = &StreamingPageFixupChunks[ PendingPage.GPUPageIndex ];

			PendingPage.ReadStream->CopyTo( FixupChunk, 0, sizeof( FFixupChunk::Header ) );
			uint32 FixupChunkSize = FixupChunk->GetSize();

			// Read the rest of FixupChunk
			PendingPage.ReadStream->CopyTo( FixupChunk->Data, sizeof( FFixupChunk::Header ), FixupChunkSize - sizeof( FFixupChunk::Header ) );

			// Read GPU data
			void* Dst = ClusterPageData.UploadBuffer.Add_GetRef( PendingPage.GPUPageIndex );
			PendingPage.ReadStream->CopyTo( Dst, FixupChunkSize, PageStreamingState.BulkSize - FixupChunkSize );
#endif

			// Update page headers
			uint32 NumPageClusters = FixupChunk->Header.NumClusters;
			ClusterPageHeaders.UploadBuffer.Add( PendingPage.GPUPageIndex, &NumPageClusters );

			// Apply fixups to install page
			StreamingPage->ResidentKey = PendingPage.InstallKey;
			ApplyFixups( *FixupChunk, **Resources, PendingPage.InstallKey.PageIndex, PendingPage.GPUPageIndex );

			INC_DWORD_STAT( STAT_NaniteInstalledPages );

#if WITH_EDITOR
			BulkData.Unlock();
#endif

			INC_DWORD_STAT(STAT_NanitePageInstalls);
		}
	}

	// Clean up IO handles
#if !WITH_EDITOR
	for (uint32 i = 0; i < NumReadyPages; i++)
	{
		uint32 PendingPageIndex = (StartPendingPageIndex + i) % MaxPendingPages;
		FPendingPage& PendingPage = PendingPages[PendingPageIndex];
		PendingPage.ReadStream.SafeRelease();
		delete PendingPage.Handle;
		PendingPage.Handle = nullptr;
	}
#endif

	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteUpload);

		{
			FRHIUnorderedAccessView* UAVs[] = { ClusterPageData.DataBuffer.UAV, ClusterPageHeaders.DataBuffer.UAV, Hierarchy.DataBuffer.UAV };
			RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs));
		}
		ClusterPageData.UploadBuffer.ResourceUploadTo(RHICmdList, ClusterPageData.DataBuffer, false);
		ClusterPageHeaders.UploadBuffer.ResourceUploadTo(RHICmdList, ClusterPageHeaders.DataBuffer, false);
		Hierarchy.UploadBuffer.ResourceUploadTo(RHICmdList, Hierarchy.DataBuffer, false);

		// NOTE: We need an additional barrier here to make sure pages are finished uploading before fixups can be applied.
		{
			FRHIUnorderedAccessView* UAVs[] = { ClusterPageData.DataBuffer.UAV };
			RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs));
		}
		ClusterFixupUploadBuffer.ResourceUploadTo(RHICmdList, ClusterPageData.DataBuffer, false);
	}


	NumPendingPages -= NumReadyPages;
	DEC_DWORD_STAT_BY( STAT_NanitePendingPages, NumReadyPages );

	return true;
}

#if DO_CHECK
void FStreamingManager::VerifyPageLRU( FStreamingPageInfo& List, uint32 TargetListLength, bool bCheckUpdateIndex )
{
	SCOPE_CYCLE_COUNTER( STAT_NaniteVerifyLRU );

	uint32 ListLength = 0u;
	uint32 PrevUpdateIndex = 0u;
	FStreamingPageInfo* Ptr = List.Prev;
	while( Ptr != &List )
	{
		if( bCheckUpdateIndex )
		{
			check( Ptr->LatestUpdateIndex >= PrevUpdateIndex );
			PrevUpdateIndex = Ptr->LatestUpdateIndex;
		}

		ListLength++;
		Ptr = Ptr->Prev;
	}

	check( ListLength == TargetListLength );
}
#endif

bool FStreamingManager::ProcessNewResources( FRHICommandListImmediate& RHICmdList )
{
	LLM_SCOPE(ELLMTag::Nanite);

	if( PendingAdds.Num() == 0 )
		return false;

	SCOPE_CYCLE_COUNTER( STAT_NaniteInstallNewResources );
	SCOPED_GPU_STAT(RHICmdList, NaniteStreaming);

	// Upload hierarchy for pending resources
	ResizeResourceIfNeeded( RHICmdList, Hierarchy.DataBuffer, FMath::RoundUpToPowerOfTwo( Hierarchy.Allocator.GetMaxSize() ) * sizeof( FPackedHierarchyNode ), TEXT("FStreamingManagerHierarchy") );

	check( MaxStreamingPages <= MAX_GPU_PAGES );
	uint32 MaxRootPages = MAX_GPU_PAGES - MaxStreamingPages;
	uint32 NumAllocatedRootPages = FMath::Clamp( FMath::RoundUpToPowerOfTwo( RootPagesAllocator.GetMaxSize() ), MIN_ROOT_PAGES_CAPACITY, MaxRootPages );
	check( NumAllocatedRootPages >= (uint32)RootPagesAllocator.GetMaxSize() );	// Root pages just don't fit!

	uint32 NumAllocatedPages = MaxStreamingPages + NumAllocatedRootPages;
	check( NumAllocatedPages <= MAX_GPU_PAGES );
	ResizeResourceIfNeeded( RHICmdList, ClusterPageHeaders.DataBuffer, NumAllocatedPages * sizeof( uint32 ), TEXT("FStreamingManagerClusterPageHeaders") );
	ResizeResourceIfNeeded( RHICmdList, ClusterPageData.DataBuffer, NumAllocatedPages << CLUSTER_PAGE_SIZE_BITS, TEXT("FStreamingManagerClusterPageData") );

	check( NumAllocatedPages <= ( 1u << ( 31 - CLUSTER_PAGE_SIZE_BITS ) ) );	// 2GB seems to be some sort of limit.
																				// TODO: Is it a GPU/API limit or is it a signed integer bug on our end?

	RootPageInfos.SetNum( NumAllocatedRootPages );

	uint32 NumPendingAdds = PendingAdds.Num();

	// TODO: These uploads can end up being quite large.
	// We should try to change the high level logic so the proxy is not considered loaded until the root page has been loaded, so we can split this over multiple frames.
	ClusterPageData.UploadBuffer.Init( NumPendingAdds, CLUSTER_PAGE_SIZE, false, TEXT("FStreamingManagerClusterPageDataUpload") );
	ClusterPageHeaders.UploadBuffer.Init( NumPendingAdds, sizeof( uint32 ), false, TEXT("FStreamingManagerClusterPageHeadersUpload") );
	Hierarchy.UploadBuffer.Init( Hierarchy.TotalUpload, sizeof( FPackedHierarchyNode ), false, TEXT("FStreamingManagerHierarchyUpload"));

	for( FResources* Resources : PendingAdds )
	{
		uint32 GPUPageIndex = MaxStreamingPages + Resources->RootPageIndex;
		uint8* Ptr = Resources->RootClusterPage.GetData();
		FFixupChunk& FixupChunk = *(FFixupChunk*)Ptr;
		uint32 FixupChunkSize = FixupChunk.GetSize();
		uint32 NumClusters = FixupChunk.Header.NumClusters;

		void* Dst = ClusterPageData.UploadBuffer.Add_GetRef( GPUPageIndex );
		FMemory::Memcpy( Dst, Ptr + FixupChunkSize, Resources->PageStreamingStates[0].BulkSize - FixupChunkSize );
		ClusterPageHeaders.UploadBuffer.Add( GPUPageIndex, &NumClusters );

		// Root node should only have fixups that depend on other pages and cannot be satisfied yet.

		// Fixup hierarchy
		for(uint32 i = 0; i < FixupChunk.Header.NumHierachyFixups; i++)
		{
			const FHierarchyFixup& Fixup = FixupChunk.GetHierarchyFixup( i );
			uint32 HierarchyNodeIndex = Fixup.GetNodeIndex();
			check( HierarchyNodeIndex < (uint32)Resources->HierarchyNodes.Num() );
			uint32 ChildIndex = Fixup.GetChildIndex();
			uint32 GroupStartIndex = Fixup.GetClusterGroupPartStartIndex();
			uint32 ChildStartReference = ( GPUPageIndex << MAX_CLUSTERS_PER_PAGE_BITS ) | Fixup.GetClusterGroupPartStartIndex();

			if(Fixup.GetPageDependencyNum() == 0)	// Only install part if it has no other dependencies
			{
				Resources->HierarchyNodes[HierarchyNodeIndex].Misc[ChildIndex].ChildStartReference = ChildStartReference;
			}
		}
		
		Hierarchy.UploadBuffer.Add( Resources->HierarchyOffset, &Resources->HierarchyNodes[ 0 ], Resources->HierarchyNodes.Num() );

		FRootPageInfo& RootPageInfo = RootPageInfos[ Resources->RootPageIndex ];
		RootPageInfo.RuntimeResourceID = Resources->RuntimeResourceID;
		RootPageInfo.NumClusters = NumClusters;
		Resources->RootClusterPage.Empty();
	}

	{
		SCOPE_CYCLE_COUNTER( STAT_NaniteUpload );

		FRHIUnorderedAccessView* UAVs[] = { ClusterPageData.DataBuffer.UAV, ClusterPageHeaders.DataBuffer.UAV, Hierarchy.DataBuffer.UAV };
		RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs));

		Hierarchy.TotalUpload = 0;
		Hierarchy.UploadBuffer.ResourceUploadTo( RHICmdList, Hierarchy.DataBuffer, false );
		ClusterPageHeaders.UploadBuffer.ResourceUploadTo( RHICmdList, ClusterPageHeaders.DataBuffer, false );
		ClusterPageData.UploadBuffer.ResourceUploadTo( RHICmdList, ClusterPageData.DataBuffer, false );
	}

	PendingAdds.Reset();
	if( NumPendingAdds > 1 )
	{
		ClusterPageData.UploadBuffer.Release();	// Release large buffers. On uploads RHI ends up using the full size of the buffer, NOT just the size of the update, so we need to keep the size down.
	}
	

	return true;
}

void FStreamingManager::Update( FRHICommandListImmediate& RHICmdList )
{	
	LLM_SCOPE(ELLMTag::Nanite);
	SCOPED_NAMED_EVENT( STAT_NaniteStreamingManagerUpdate, FColor::Red );
	SCOPE_CYCLE_COUNTER( STAT_NaniteStreamingManagerUpdate );
	SCOPED_GPU_STAT(RHICmdList, NaniteStreaming);

	if( !StreamingRequestsBuffer.IsValid() )
	{
		// Init and clear StreamingRequestsBuffer.
		// Can't do this in InitRHI as RHICmdList doesn't have a valid context yet.
		FRDGBuilder GraphBuilder( RHICmdList );
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(4, 3 * MAX_STREAMING_REQUESTS);
		Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy);
		FRDGBufferRef StreamingRequestsBufferRef = GraphBuilder.CreateBuffer( Desc, TEXT( "StreamingRequests" ) );	// TODO: Can't be a structured buffer as EnqueueCopy is only defined for vertex buffers
		AddClearUAVPass( GraphBuilder, GraphBuilder.CreateUAV( StreamingRequestsBufferRef, PF_R32_UINT ), 0 );
		GraphBuilder.QueueBufferExtraction( StreamingRequestsBufferRef, &StreamingRequestsBuffer);
		GraphBuilder.Execute();
	}

	bool bBuffersTransitionedToWrite = false;

	bBuffersTransitionedToWrite |= ProcessNewResources( RHICmdList  );

#if WITH_EDITOR == 0
	bBuffersTransitionedToWrite |= ProcessPendingPages( RHICmdList );
#endif

	// Process readback
	FRHIGPUBufferReadback* LatestReadbackBuffer = nullptr;
	
	{
		// Find latest buffer that is ready
		uint32 Index = ( ReadbackBuffersWriteIndex + MaxStreamingReadbackBuffers - ReadbackBuffersNumPending ) % MaxStreamingReadbackBuffers;
		while( ReadbackBuffersNumPending > 0 )
		{
			if( StreamingRequestReadbackBuffers[ Index ]->IsReady() )	//TODO: process all buffers or just the latest?
			{
				ReadbackBuffersNumPending--;
				LatestReadbackBuffer = StreamingRequestReadbackBuffers[ Index ];
			}
			else
			{
				break;
			}
		}
	}
	
	auto StreamingPriorityPredicate = []( const FStreamingRequest& A, const FStreamingRequest& B ) { return A.Priority > B.Priority; };

	PrioritizedRequestsHeap.Empty( MAX_STREAMING_REQUESTS );

	if( LatestReadbackBuffer )
	{
		SCOPE_CYCLE_COUNTER( STAT_NaniteProcessReadback );
		const uint32* BufferPtr = (const uint32*) LatestReadbackBuffer->Lock( MAX_STREAMING_REQUESTS * sizeof( uint32 ) * 3 );
		uint32 NumStreamingRequests = FMath::Min( BufferPtr[ 0 ], MAX_STREAMING_REQUESTS - 1u );	// First request is reserved for counter

		if( NumStreamingRequests > 0 )
		{
			// Update priorities
			FGPUStreamingRequest* StreamingRequestsPtr = ( ( FGPUStreamingRequest* ) BufferPtr + 1 );

			{
				SCOPE_CYCLE_COUNTER( STAT_NaniteDeduplicateRequests );
				RequestsHashTable->Clear();
				for( uint32 Index = 0; Index < NumStreamingRequests; Index++ )
				{
					const FGPUStreamingRequest& GPURequest = StreamingRequestsPtr[ Index ];
					uint32 NumPages = GPURequest.PageIndex_NumPages & MAX_GROUP_PARTS_MASK;
					uint32 PageStartIndex = GPURequest.PageIndex_NumPages >> MAX_GROUP_PARTS_BITS;
					
					FStreamingRequest Request;
					Request.Key.RuntimeResourceID = GPURequest.RuntimeResourceID;
					Request.Priority = GPURequest.Priority;
					for (uint32 i = 0; i < NumPages; i++)
					{
						Request.Key.PageIndex = PageStartIndex + i;
						check(!IsRootPage(Request.Key.PageIndex));
						RequestsHashTable->AddRequest(Request);
					}
				}
			}

			uint32 NumUniqueStreamingRequests = RequestsHashTable->GetNumElements();
#if 0
			// Verify against TMap
			{
				TMap< FPageKey, uint32 > UniqueRequestsMap;	
				{
					for( uint32 Index = 0; Index < NumStreamingRequests; Index++ )
					{
						const FStreamingRequest& Request = StreamingRequestsPtr[ Index ];
						check( Request.Key.PageIndex != 0 );
						uint32* Priority = UniqueRequestsMap.Find( Request.Key );
						if( Priority )
							*Priority = FMath::Max( *Priority, Request.Priority );
						else
							UniqueRequestsMap.Add( Request.Key, Request.Priority );
					}
				}

				check( UniqueRequestsMap.Num() == NumUniqueStreamingRequests );
				for( uint32 i = 0; i < NumUniqueStreamingRequests; i++ )
				{
					const FStreamingRequest& Request = RequestsHashTable->GetElement(i);
					uint32* Priority = UniqueRequestsMap.Find( Request.Key );
					check( Priority );
					check( *Priority == Request.Priority );
				}
			}
#endif

			INC_DWORD_STAT_BY( STAT_NaniteStreamingRequests, NumStreamingRequests );
			INC_DWORD_STAT_BY( STAT_NaniteUniqueStreamingRequests, NumUniqueStreamingRequests );

			{
				SCOPE_CYCLE_COUNTER( STAT_NaniteUpdatePriorities );
				
				struct FPrioritizedStreamingPage
				{
					FStreamingPageInfo* Page;
					uint32 Priority;
				};

				TArray< FPrioritizedStreamingPage > UpdatedPages;
				for(uint32 UniqueRequestIndex = 0; UniqueRequestIndex < NumUniqueStreamingRequests; UniqueRequestIndex++)
				{
					const FStreamingRequest& Request = RequestsHashTable->GetElement(UniqueRequestIndex);
					FStreamingPageInfo** StreamingPage = RegisteredStreamingPagesMap.Find( Request.Key );
					if( StreamingPage )
					{
						// Update index and move to front of LRU.
						(*StreamingPage)->LatestUpdateIndex = NextUpdateIndex;
						UpdatedPages.Push( { *StreamingPage, Request.Priority } );
					}
					else
					{
						// Page isn't there. Is the resource still here?
						FResources** Resources = RuntimeResourceMap.Find( Request.Key.RuntimeResourceID );
						if( Resources )
						{
							// ResourcesID is valid, so add request to the queue
							PrioritizedRequestsHeap.Push( Request );
						}
					}
				}

				PrioritizedRequestsHeap.Heapify( StreamingPriorityPredicate );

				{
					SCOPE_CYCLE_COUNTER( STAT_NanitePrioritySort );
					UpdatedPages.Sort( []( const FPrioritizedStreamingPage& A, const FPrioritizedStreamingPage& B ) { return A.Priority < B.Priority; } );
				}

				{
					SCOPE_CYCLE_COUNTER( STAT_NaniteUpdateLRU );

					for( const FPrioritizedStreamingPage& PrioritizedPage : UpdatedPages )
					{
						FStreamingPageInfo* Page = PrioritizedPage.Page;

						// Unlink
						FStreamingPageInfo* OldNext = Page->Next;
						FStreamingPageInfo* OldPrev = Page->Prev;
						OldNext->Prev = OldPrev;
						OldPrev->Next = OldNext;

						// Insert at the front of the LRU
						Page->Prev = &StreamingPageLRU;
						Page->Next = StreamingPageLRU.Next;
						StreamingPageLRU.Next->Prev = Page;
						StreamingPageLRU.Next = Page;
					}
				}
			}
		}
		LatestReadbackBuffer->Unlock();

#if DO_CHECK
		VerifyPageLRU( StreamingPageLRU, NumRegisteredStreamingPages, true );
#endif
			
		uint32 MaxSelectedPages = MaxPendingPages - NumPendingPages;
		if( PrioritizedRequestsHeap.Num() > 0 )
		{
			TArray< FPageKey > SelectedPages;
			TSet< FPageKey > SelectedPagesSet;
			
			{
				SCOPE_CYCLE_COUNTER( STAT_NaniteSelectStreamingPages );

				// Add low priority pages based on prioritized requests
				while( (uint32)SelectedPages.Num() < MaxSelectedPages && PrioritizedRequestsHeap.Num() > 0 )
				{
					FStreamingRequest SelectedRequest;
					PrioritizedRequestsHeap.HeapPop( SelectedRequest, StreamingPriorityPredicate, false );
					FResources** Resources = RuntimeResourceMap.Find( SelectedRequest.Key.RuntimeResourceID );
					check( Resources != nullptr );

					SelectStreamingPages( *Resources, SelectedPages, SelectedPagesSet, SelectedRequest.Key.RuntimeResourceID, SelectedRequest.Key.PageIndex, SelectedRequest.Priority, MaxSelectedPages );
				}
				check( (uint32)SelectedPages.Num() <= MaxSelectedPages );
			}

			if( SelectedPages.Num() > 0 )
			{
				// Collect all pending registration dependencies so we are not going to remove them.
				TSet< FPageKey > RegistrationDependencyPages;
				for( const FPageKey& SelectedKey : SelectedPages )
				{
					FResources** Resources = RuntimeResourceMap.Find( SelectedKey.RuntimeResourceID );
					check( Resources != nullptr );

					CollectDependencyPages( *Resources, RegistrationDependencyPages, SelectedKey );	// Mark all dependencies as unremovable.
				}

				// Register Pages
				for( const FPageKey& SelectedKey : SelectedPages )
				{

					FPendingPage& PendingPage = PendingPages[ NextPendingPageIndex ];

					if( NumRegisteredStreamingPages >= MaxStreamingPages )
					{
						// No space. Free a page!
						FStreamingPageInfo* StreamingPage = StreamingPageLRU.Prev;
						while( StreamingPage != &StreamingPageLRU )
						{
							FStreamingPageInfo* PrevStreamingPage = StreamingPage->Prev;

							// Only remove leaf nodes. Make sure to never delete a node that was added this frame or is a dependency for a pending page registration.
							FPageKey FreeKey = PrevStreamingPage->RegisteredKey;
							if( PrevStreamingPage->RefCount == 0 && ( PrevStreamingPage->LatestUpdateIndex < NextUpdateIndex ) && RegistrationDependencyPages.Find( FreeKey ) == nullptr )
							{
								FStreamingPageInfo** Page = RegisteredStreamingPagesMap.Find( FreeKey );
								check( Page != nullptr );
								UnregisterPage( FreeKey );
								break;
							}
							StreamingPage = PrevStreamingPage;
						}
					}

					if( NumRegisteredStreamingPages >= MaxStreamingPages )
						break;

					FResources** Resources = RuntimeResourceMap.Find( SelectedKey.RuntimeResourceID );
					check( Resources );
					FByteBulkData& BulkData = ( *Resources )->StreamableClusterPages;
					const FPageStreamingState& PageStreamingState = ( *Resources )->PageStreamingStates[ SelectedKey.PageIndex ];
					check( !IsRootPage( SelectedKey.PageIndex ) );

#if WITH_EDITOR == 0
					// Start async IO
					PendingPage.Handle = IFileCacheHandle::CreateFileCacheHandle(BulkData.OpenAsyncReadHandle());
					PendingPage.ReadStream = PendingPage.Handle->ReadData( PendingPage.CompletionEvents, PageStreamingState.BulkOffset, PageStreamingState.BulkSize, AIOP_Normal );
					if(PendingPage.ReadStream == nullptr)
					{
						// IO can fail. Retry next frame if it does. We can't just proceed to the next request as it might depend on this one.
						UE_LOG(LogNaniteStreaming, Warning, TEXT("IFileCache.ReadData failed for %s"), *BulkData.GetFilename());
						delete PendingPage.Handle;
						PendingPage.Handle = nullptr;
						break;
					}
					check(PendingPage.ReadStream != nullptr);
#endif

					// Grab a free page
					check(StreamingPageInfoFreeList != nullptr);
					FStreamingPageInfo* Page = StreamingPageInfoFreeList;
					StreamingPageInfoFreeList = StreamingPageInfoFreeList->Next;


					PendingPage.InstallKey = SelectedKey;
					PendingPage.GPUPageIndex = Page->GPUPageIndex;

					NextPendingPageIndex = ( NextPendingPageIndex + 1 ) % MaxPendingPages;
					NumPendingPages++;
					INC_DWORD_STAT( STAT_NanitePendingPages );

#if !UE_BUILD_SHIPPING
					PendingPage.BytesLeftToStream = PageStreamingState.BulkSize;
#endif

					RegisterStreamingPage( Page, SelectedKey );
				}
			}
		}
	}

#if WITH_EDITOR
	bBuffersTransitionedToWrite |= ProcessPendingPages( RHICmdList );	// Process streaming requests immediately in editor
#endif

	// Transition resource back to read
	if( bBuffersTransitionedToWrite )
	{
		FRHIUnorderedAccessView* UAVs[] = { Hierarchy.DataBuffer.UAV , ClusterPageData.DataBuffer.UAV, ClusterPageHeaders.DataBuffer.UAV };
		RHICmdList.TransitionResources( EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UAVs, UE_ARRAY_COUNT(UAVs) );
	}

	NextUpdateIndex++;
}

BEGIN_SHADER_PARAMETER_STRUCT(FReadbackPassParameters, )
	SHADER_PARAMETER_RDG_BUFFER(, Input)
END_SHADER_PARAMETER_STRUCT()

void FStreamingManager::SubmitFrameStreamingRequests(FRDGBuilder& GraphBuilder)
{
	LLM_SCOPE(ELLMTag::Nanite);
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteStreaming);
	RDG_EVENT_SCOPE(GraphBuilder, "NaniteStreaming");

	if( ReadbackBuffersNumPending == MaxStreamingReadbackBuffers )
	{
		// Return when queue is full. It is NOT safe to EnqueueCopy on a buffer that already has a pending copy.
		return;
	}

	if( StreamingRequestReadbackBuffers[ ReadbackBuffersWriteIndex ] == nullptr )
	{
		FRHIGPUBufferReadback* GPUBufferReadback = new FRHIGPUBufferReadback( TEXT("Nanite streaming requests readback") );
		StreamingRequestReadbackBuffers[ ReadbackBuffersWriteIndex ] = GPUBufferReadback;
	}

	FRDGBufferRef Buffer = GraphBuilder.RegisterExternalBuffer(
		StreamingRequestsBuffer,
		TEXT("StreamingRequests"),
		ERDGParentResourceFlags::None,
		EResourceTransitionAccess::EReadable,
		EResourceTransitionAccess::EWritable);

	{
		FRHIGPUBufferReadback* ReadbackBuffer = StreamingRequestReadbackBuffers[ReadbackBuffersWriteIndex];

		FReadbackPassParameters* PassParameters = GraphBuilder.AllocParameters<FReadbackPassParameters>();
		PassParameters->Input = Buffer;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Readback"),
			PassParameters,
			ERDGPassFlags::Readback,
			[ReadbackBuffer, Buffer](FRHICommandList& RHICmdList)
		{
			Buffer->MarkResourceAsUsed();
			ReadbackBuffer->EnqueueCopy(RHICmdList, Buffer->GetRHIVertexBuffer(), 0u);
		});
	}

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Buffer, PF_R32_UINT), 0);

	ReadbackBuffersWriteIndex = ( ReadbackBuffersWriteIndex + 1u ) % MaxStreamingReadbackBuffers;
	ReadbackBuffersNumPending = FMath::Min( ReadbackBuffersNumPending + 1u, MaxStreamingReadbackBuffers );
}

TGlobalResource< FStreamingManager > GStreamingManager;

} // namespace Nanite
