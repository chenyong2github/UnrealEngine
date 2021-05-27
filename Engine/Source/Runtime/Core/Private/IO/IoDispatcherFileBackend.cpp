// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcherFileBackend.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/IConsoleManager.h"
#include "Async/AsyncWork.h"
#include "Async/MappedFileHandle.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "Algo/AllOf.h"
#include "Algo/IsSorted.h"
#include "Algo/MinElement.h"
#include "Templates/Greater.h"

TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalBytesRead, TEXT("IoDispatcher/TotalBytesRead"));
TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalBytesScattered, TEXT("IoDispatcher/TotalBytesScattered"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheHits, TEXT("IoDispatcher/CacheHits"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheMisses, TEXT("IoDispatcher/CacheMisses"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherOutstandingReads, TEXT("IoDispatcher/OutstandingReads"));
TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherOutstandingBytesToRead, TEXT("IoDispatcher/OutstandingBytesToRead"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherLatencyCircuitBreaks, TEXT("IoDispatcher/LatencyCircuitBreaks"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherSeekDistanceCircuitBreaks, TEXT("IoDispatcher/SeekDistanceCircuitBreaks"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherNumPriorityQueues, TEXT("IoDispatcher/NumPriorityQueues"));

//PRAGMA_DISABLE_OPTIMIZATION

int32 GIoDispatcherBufferSizeKB = 256;
static FAutoConsoleVariableRef CVar_IoDispatcherBufferSizeKB(
	TEXT("s.IoDispatcherBufferSizeKB"),
	GIoDispatcherBufferSizeKB,
	TEXT("IoDispatcher read buffer size (in kilobytes).")
);

int32 GIoDispatcherBufferAlignment = 4096;
static FAutoConsoleVariableRef CVar_IoDispatcherBufferAlignment(
	TEXT("s.IoDispatcherBufferAlignment"),
	GIoDispatcherBufferAlignment,
	TEXT("IoDispatcher read buffer alignment.")
);

int32 GIoDispatcherBufferMemoryMB = 8;
static FAutoConsoleVariableRef CVar_IoDispatcherBufferMemoryMB(
	TEXT("s.IoDispatcherBufferMemoryMB"),
	GIoDispatcherBufferMemoryMB,
	TEXT("IoDispatcher buffer memory size (in megabytes).")
);

int32 GIoDispatcherDecompressionWorkerCount = 4;
static FAutoConsoleVariableRef CVar_IoDispatcherDecompressionWorkerCount(
	TEXT("s.IoDispatcherDecompressionWorkerCount"),
	GIoDispatcherDecompressionWorkerCount,
	TEXT("IoDispatcher decompression worker count.")
);

int32 GIoDispatcherCacheSizeMB = 0;
static FAutoConsoleVariableRef CVar_IoDispatcherCacheSizeMB(
	TEXT("s.IoDispatcherCacheSizeMB"),
	GIoDispatcherCacheSizeMB,
	TEXT("IoDispatcher cache memory size (in megabytes).")
);

int32 GIoDispatcherSortRequestsByOffset = 1;
static FAutoConsoleVariableRef CVar_IoDispatcherSortRequestsByOffset(
	TEXT("s.IoDispatcherSortRequestsByOffset"),
	GIoDispatcherSortRequestsByOffset,
	TEXT("If > 0, io dispatcher sorts the outstanding request queue by offset rather than sequence.")
);

int32 GIoDispatcherMaintainSortingOnPriorityChange = 1;
static FAutoConsoleVariableRef CVar_IoDispatcherMaintainSortingOnPriorityChange(
	TEXT("s.IoDispatcherMaintainSortingOnPriorityChange"),
	GIoDispatcherMaintainSortingOnPriorityChange,
	TEXT("If s.IoDispatcherSortRequestsByOffset > 0 and this > 0, io dispatcher remembers the last file handle/offset read from even when switching priority levels.")
);

int32 GIoDispatcherMaxForwardSeekKB = 0;
static FAutoConsoleVariableRef CVar_IoDispatcherMaxForwardSeekKB(
	TEXT("s.IoDispatcherMaxForwardSeekKB"),
	GIoDispatcherMaxForwardSeekKB,
	TEXT("If s.IoDispatcherSortRequestsByOffset is enabled and this is > 0, if the next sequential read is further than this offset from the last one, read the oldest request instead")
);

int32 GIoDispatcherRequestLatencyCircuitBreakerMS = 0;
static FAutoConsoleVariableRef CVar_IoDispatcherRequestLatencyCircuitBreakerMS(
	TEXT("s.IoDispatcherRequestLatencyCircuitBreakerMS"),
	GIoDispatcherRequestLatencyCircuitBreakerMS,
	TEXT("If s.IoDispatcherSortRequestsByOffset is enabled and this is >0, if the oldest request has been in the queue for this long, read it instead of the most optimal read")
);

uint32 FFileIoStoreReadRequest::NextSequence = 0;
#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
uint32 FFileIoStoreReadRequestList::NextListCookie = 0;
#endif
TAtomic<uint32> FFileIoStoreReader::GlobalPartitionIndex{ 0 };
TAtomic<uint32> FFileIoStoreReader::GlobalContainerInstanceId{ 0 };

class FMappedFileProxy final : public IMappedFileHandle
{
public:
	FMappedFileProxy(IMappedFileHandle* InSharedMappedFileHandle, uint64 InSize)
		: IMappedFileHandle(InSize)
		, SharedMappedFileHandle(InSharedMappedFileHandle)
	{
		check(InSharedMappedFileHandle != nullptr);
	}

	virtual ~FMappedFileProxy() { }

	virtual IMappedFileRegion* MapRegion(int64 Offset = 0, int64 BytesToMap = MAX_int64, bool bPreloadHint = false) override
	{
		return SharedMappedFileHandle->MapRegion(Offset, BytesToMap, bPreloadHint);
	}
private:
	IMappedFileHandle* SharedMappedFileHandle;
};

void FFileIoStoreBufferAllocator::Initialize(uint64 MemorySize, uint64 BufferSize, uint32 BufferAlignment)
{
	uint64 BufferCount = MemorySize / BufferSize;
	MemorySize = BufferCount * BufferSize;
	BufferMemory = reinterpret_cast<uint8*>(FMemory::Malloc(MemorySize, BufferAlignment));
	for (uint64 BufferIndex = 0; BufferIndex < BufferCount; ++BufferIndex)
	{
		FFileIoStoreBuffer* Buffer = new FFileIoStoreBuffer();
		Buffer->Memory = BufferMemory + BufferIndex * BufferSize;
		Buffer->Next = FirstFreeBuffer;
		FirstFreeBuffer = Buffer;
	}
}

FFileIoStoreBuffer* FFileIoStoreBufferAllocator::AllocBuffer()
{
	FScopeLock Lock(&BuffersCritical);
	FFileIoStoreBuffer* Buffer = FirstFreeBuffer;
	if (Buffer)
	{
		FirstFreeBuffer = Buffer->Next;
		return Buffer;
	}
	return nullptr;
}

void FFileIoStoreBufferAllocator::FreeBuffer(FFileIoStoreBuffer* Buffer)
{
	check(Buffer);
	FScopeLock Lock(&BuffersCritical);
	Buffer->Next = FirstFreeBuffer;
	FirstFreeBuffer = Buffer;
}

FFileIoStoreBlockCache::FFileIoStoreBlockCache()
{
	CacheLruHead.LruNext = &CacheLruTail;
	CacheLruTail.LruPrev = &CacheLruHead;
}

FFileIoStoreBlockCache::~FFileIoStoreBlockCache()
{
	FCachedBlock* CachedBlock = CacheLruHead.LruNext;
	while (CachedBlock != &CacheLruTail)
	{
		FCachedBlock* Next = CachedBlock->LruNext;
		delete CachedBlock;
		CachedBlock = Next;
	}
	FMemory::Free(CacheMemory);
}

void FFileIoStoreBlockCache::Initialize(uint64 InCacheMemorySize, uint64 InReadBufferSize)
{
	ReadBufferSize = InReadBufferSize;
	uint64 CacheBlockCount = InCacheMemorySize / InReadBufferSize;
	if (CacheBlockCount)
	{
		InCacheMemorySize = CacheBlockCount * InReadBufferSize;
		CacheMemory = reinterpret_cast<uint8*>(FMemory::Malloc(InCacheMemorySize));
		FCachedBlock* Prev = &CacheLruHead;
		for (uint64 CacheBlockIndex = 0; CacheBlockIndex < CacheBlockCount; ++CacheBlockIndex)
		{
			FCachedBlock* CachedBlock = new FCachedBlock();
			CachedBlock->Key = uint64(-1);
			CachedBlock->Buffer = CacheMemory + CacheBlockIndex * InReadBufferSize;
			Prev->LruNext = CachedBlock;
			CachedBlock->LruPrev = Prev;
			Prev = CachedBlock;
		}
		Prev->LruNext = &CacheLruTail;
		CacheLruTail.LruPrev = Prev;
	}
}

bool FFileIoStoreBlockCache::Read(FFileIoStoreReadRequest* Block)
{
	bool bIsCacheableBlock = CacheMemory != nullptr && Block->bIsCacheable;
	if (!bIsCacheableBlock)
	{
		return false;
	}
	check(Block->Buffer);
	FCachedBlock* CachedBlock = nullptr;
	{
		FScopeLock Lock(&CriticalSection);
		CachedBlock = CachedBlocks.FindRef(Block->Key.Hash);
		if (CachedBlock)
		{
			CachedBlock->bLocked = true;

			CachedBlock->LruPrev->LruNext = CachedBlock->LruNext;
			CachedBlock->LruNext->LruPrev = CachedBlock->LruPrev;

			CachedBlock->LruPrev = &CacheLruHead;
			CachedBlock->LruNext = CacheLruHead.LruNext;

			CachedBlock->LruPrev->LruNext = CachedBlock;
			CachedBlock->LruNext->LruPrev = CachedBlock;
		}
	}

	if (!CachedBlock)
	{
		TRACE_COUNTER_INCREMENT(IoDispatcherCacheMisses);
		return false;
	}
	check(CachedBlock->Buffer);
	FMemory::Memcpy(Block->Buffer->Memory, CachedBlock->Buffer, ReadBufferSize);
	{
		FScopeLock Lock(&CriticalSection);
		CachedBlock->bLocked = false;
	}
	TRACE_COUNTER_INCREMENT(IoDispatcherCacheHits);
	return true;
}

void FFileIoStoreBlockCache::Store(const FFileIoStoreReadRequest* Block)
{
	bool bIsCacheableBlock = CacheMemory != nullptr && Block->bIsCacheable;
	if (!bIsCacheableBlock)
	{
		return;
	}
	check(Block->Buffer);
	check(Block->Buffer->Memory);
	FCachedBlock* BlockToReplace = nullptr;
	{
		FScopeLock Lock(&CriticalSection);
		BlockToReplace = CacheLruTail.LruPrev;
		while (BlockToReplace != &CacheLruHead && BlockToReplace->bLocked)
		{
			BlockToReplace = BlockToReplace->LruPrev;
		}
		if (BlockToReplace == &CacheLruHead)
		{
			return;
		}
		CachedBlocks.Remove(BlockToReplace->Key);
		BlockToReplace->bLocked = true;
		BlockToReplace->Key = Block->Key.Hash;

		BlockToReplace->LruPrev->LruNext = BlockToReplace->LruNext;
		BlockToReplace->LruNext->LruPrev = BlockToReplace->LruPrev;

		BlockToReplace->LruPrev = &CacheLruHead;
		BlockToReplace->LruNext = CacheLruHead.LruNext;

		BlockToReplace->LruPrev->LruNext = BlockToReplace;
		BlockToReplace->LruNext->LruPrev = BlockToReplace;
	}
	check(BlockToReplace);
	check(BlockToReplace->Buffer);
	FMemory::Memcpy(BlockToReplace->Buffer, Block->Buffer->Memory, ReadBufferSize);
	{
		FScopeLock Lock(&CriticalSection);
		BlockToReplace->bLocked = false;
		CachedBlocks.Add(BlockToReplace->Key, BlockToReplace);
	}
}

bool FFileIoStoreOffsetSortedRequestQueue::RequestSortPredicate(const FFileIoStoreReadRequestSortKey& A, const FFileIoStoreReadRequestSortKey& B)
{
	if (A.Handle == B.Handle)
	{
		return A.Offset < B.Offset;
	}
	return A.Handle < B.Handle;
}

FFileIoStoreOffsetSortedRequestQueue::FFileIoStoreOffsetSortedRequestQueue(int32 InPriority)
	: Priority(InPriority)
{
}

TArray<FFileIoStoreReadRequest*> FFileIoStoreOffsetSortedRequestQueue::StealRequests()
{
	RequestsBySequence.Clear();
	PeekRequestIndex = INDEX_NONE;
	return MoveTemp(Requests); 
}

// This could be potentially optimized if the higher level keeps track of which requests it changes the priority of, or even just the old priorty levels
TArray<FFileIoStoreReadRequest*> FFileIoStoreOffsetSortedRequestQueue::RemoveMisprioritizedRequests()
{
	PeekRequestIndex = INDEX_NONE;
	TArray<FFileIoStoreReadRequest*> RequestsToReturn;
	for (int32 i = Requests.Num()-1; i >= 0; --i)
	{
		if (Requests[i]->Priority != Priority)
		{
			RequestsToReturn.Add(Requests[i]);
			RequestsBySequence.Remove(Requests[i]);
			Requests.RemoveAt(i, 1, false);
		}
	}

	return RequestsToReturn;
}

FFileIoStoreReadRequest* FFileIoStoreOffsetSortedRequestQueue::GetNextInternal(FFileIoStoreReadRequestSortKey LastSortKey, bool bPop)
{
	if (Requests.Num() == 0)
	{
		return nullptr;
	}

	int32 RequestIndex = INDEX_NONE;
	if (PeekRequestIndex != INDEX_NONE)
	{
		RequestIndex = PeekRequestIndex;
	}
	else 
	{
		bool bHeadRequestTooOld = false;
		if (GIoDispatcherRequestLatencyCircuitBreakerMS > 0)
		{
			// If our oldest request has been unserviced for too long, grab that instead of the next sequential read
			uint64 ThresholdCycles = uint64((GIoDispatcherRequestLatencyCircuitBreakerMS * 1000.0) / FPlatformTime::GetSecondsPerCycle64());
			bHeadRequestTooOld = (FPlatformTime::Cycles64() - RequestsBySequence.PeekHead()->CreationTime) >= ThresholdCycles;

			if (bPop)
			{
				TRACE_COUNTER_INCREMENT(IoDispatcherLatencyCircuitBreaks);
			}
		}

		const bool bChooseByOffset = 
				LastSortKey.Handle != 0 
			&&	!bHeadRequestTooOld 
			&&  (GIoDispatcherMaintainSortingOnPriorityChange || LastSortKey.Priority == Priority);
		if (bChooseByOffset)
		{
			// Pick the request with the closest offset to the last thing that we read
			RequestIndex = Algo::LowerBoundBy(Requests, LastSortKey, RequestSortProjection, RequestSortPredicate);
			if (Requests.IsValidIndex(RequestIndex)) // If all requests are before LastOffset we get back out-of-bounds
			{
				if (Requests[RequestIndex]->FileHandle != LastSortKey.Handle)
				{
					// Changing file handle so switch back to the oldest outstanding request 
					RequestIndex = INDEX_NONE;
				}
				else if (GIoDispatcherMaxForwardSeekKB > 0 && (LastSortKey.Offset - Requests[RequestIndex]->Offset) > uint64(GIoDispatcherMaxForwardSeekKB) * 1024)
				{
					// Large forward seek so switch back to the oldest outstanding request 
					RequestIndex = INDEX_NONE;

					if (bPop)
					{
						TRACE_COUNTER_INCREMENT(IoDispatcherSeekDistanceCircuitBreaks);
					}
				}
			}
		}

		if (!Requests.IsValidIndex(RequestIndex))
		{
			RequestIndex = Requests.Find(RequestsBySequence.PeekHead());
			check(Requests[RequestIndex] == RequestsBySequence.PeekHead());
		}
	}

	check(Requests.IsValidIndex(RequestIndex));

	FFileIoStoreReadRequest* Request = Requests[RequestIndex];
	if (bPop)
	{
		Requests.RemoveAt(RequestIndex);
		RequestsBySequence.Remove(Request);
		PeekRequestIndex = INDEX_NONE;
	}
	else
	{
		PeekRequestIndex = RequestIndex;
	}
	return Request;
}

FFileIoStoreReadRequest* FFileIoStoreOffsetSortedRequestQueue::Peek(FFileIoStoreReadRequestSortKey LastSortKey)
{
	return GetNextInternal(LastSortKey, false);
}

FFileIoStoreReadRequest* FFileIoStoreOffsetSortedRequestQueue::Pop(FFileIoStoreReadRequestSortKey LastSortKey)
{
	return GetNextInternal(LastSortKey, true);
}

void FFileIoStoreOffsetSortedRequestQueue::Push(FFileIoStoreReadRequest* Request)
{
	// Insert sorted by file handle & offset
	int32 InsertIndex = Algo::UpperBoundBy(Requests, RequestSortProjection(Request), RequestSortProjection, RequestSortPredicate);
	Requests.Insert(Request, InsertIndex);
	
	// Insert sorted by age
	RequestsBySequence.Add(Request);

	PeekRequestIndex = INDEX_NONE;
}

void FFileIoStoreRequestQueue::UpdateSortRequestsByOffset()
{
	// Must hold CriticalSection here
	if (bSortRequestsByOffset == bool(GIoDispatcherSortRequestsByOffset))
	{
		return;
	}

	bSortRequestsByOffset = bool(GIoDispatcherSortRequestsByOffset);
	if (bSortRequestsByOffset)
	{
		// Split things into separate heaps
		for (FFileIoStoreReadRequest* Request : Heap)
		{
			Push(*Request);
		}
		Heap.Empty();
	}
	else
	{
		// Put things back into the main heap
		TArray< FFileIoStoreReadRequest*> AllRequests;
		for (FFileIoStoreOffsetSortedRequestQueue& SubQueue : SortedPriorityQueues)
		{
			AllRequests.Append(SubQueue.StealRequests());
		}
		Algo::SortBy(AllRequests, [](FFileIoStoreReadRequest* Request) { return Request->Sequence; });
		for (FFileIoStoreReadRequest* Request : AllRequests)
		{
			Push(*Request);
		}
		check(Algo::AllOf(SortedPriorityQueues, [](const FFileIoStoreOffsetSortedRequestQueue& Q) { return Q.IsEmpty(); }));
		SortedPriorityQueues.Empty();
	}
}

FFileIoStoreReadRequest* FFileIoStoreRequestQueue::Peek()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RequestQueuePeek);
	FScopeLock _(&CriticalSection);
	UpdateSortRequestsByOffset();
	if (bSortRequestsByOffset)
	{
		if( SortedPriorityQueues.Num() == 0)
		{
			return nullptr;
		}

		FFileIoStoreOffsetSortedRequestQueue& SubQueue = SortedPriorityQueues.Last();
		check(!SubQueue.IsEmpty());
		FFileIoStoreReadRequest* Request = SubQueue.Peek(LastSortKey);
		check(Request);
		// Do not update LastUsedPriority here until we actually pop
		return Request;
	}
	else
	{
		if (Heap.Num() == 0)
		{
			return nullptr;
		}
		return Heap.HeapTop();
	}
}

FFileIoStoreReadRequest* FFileIoStoreRequestQueue::Pop()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RequestQueuePop);
	FScopeLock _(&CriticalSection);
	UpdateSortRequestsByOffset();
	FFileIoStoreReadRequest* Result = nullptr;
	if (bSortRequestsByOffset)
	{
		if (SortedPriorityQueues.Num() == 0)
		{
			return nullptr;
		}

		FFileIoStoreOffsetSortedRequestQueue& SubQueue = SortedPriorityQueues.Last();
		check(!SubQueue.IsEmpty());
		Result = SubQueue.Pop(LastSortKey);
		check(Result);
		LastSortKey = Result;
		if (SubQueue.IsEmpty())
		{
			SortedPriorityQueues.Pop();
			// SubQueue is invalid here
			TRACE_COUNTER_DECREMENT(IoDispatcherNumPriorityQueues);
		}
	}
	else
	{
		if (Heap.Num() == 0)
		{
			return nullptr;
		}
		Heap.HeapPop(Result, QueueSortFunc, false);
	}
	
	check(Result->QueueStatus == FFileIoStoreReadRequest::QueueStatus_InQueue);
	Result->QueueStatus = FFileIoStoreReadRequest::QueueStatus_Started;
	return Result;
}

void FFileIoStoreRequestQueue::PushToPriorityQueues(FFileIoStoreReadRequest* Request)
{
	int32 QueueIndex = Algo::LowerBoundBy(SortedPriorityQueues, Request->Priority, QueuePriorityProjection, TLess<int32>());
	if (!SortedPriorityQueues.IsValidIndex(QueueIndex) || SortedPriorityQueues[QueueIndex].GetPriority() != Request->Priority)
	{
		SortedPriorityQueues.Insert(FFileIoStoreOffsetSortedRequestQueue(Request->Priority), QueueIndex);
		TRACE_COUNTER_INCREMENT(IoDispatcherNumPriorityQueues);
	}
	check(Algo::IsSortedBy(SortedPriorityQueues, QueuePriorityProjection, TLess<int32>()));
	FFileIoStoreOffsetSortedRequestQueue& Queue = SortedPriorityQueues[QueueIndex];
	check(Queue.GetPriority() == Request->Priority);
	Queue.Push(Request);
}

void FFileIoStoreRequestQueue::Push(FFileIoStoreReadRequest& Request)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RequestQueuePush);
	FScopeLock _(&CriticalSection);
	UpdateSortRequestsByOffset();
	TRACE_COUNTER_INCREMENT(IoDispatcherOutstandingReads);
	TRACE_COUNTER_ADD(IoDispatcherOutstandingBytesToRead, Request.Size);
	
	check(Request.QueueStatus != FFileIoStoreReadRequest::QueueStatus_InQueue);
	Request.QueueStatus = FFileIoStoreReadRequest::QueueStatus_InQueue;
	if (bSortRequestsByOffset)
	{
		PushToPriorityQueues(&Request);
	}
	else
	{
		Heap.HeapPush(&Request, QueueSortFunc);
	}
}

void FFileIoStoreRequestQueue::Push(FFileIoStoreReadRequestList& Requests)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RequestQueuePush);
	FScopeLock _(&CriticalSection);
	UpdateSortRequestsByOffset();

	for (auto It = Requests.Steal(); It; ++It)
	{
		TRACE_COUNTER_INCREMENT(IoDispatcherOutstandingReads);
		TRACE_COUNTER_ADD(IoDispatcherOutstandingBytesToRead, It->Size);

		check(It->QueueStatus != FFileIoStoreReadRequest::QueueStatus_InQueue);
		It->QueueStatus = FFileIoStoreReadRequest::QueueStatus_InQueue;

		if (bSortRequestsByOffset)
		{
			PushToPriorityQueues(*It);
		}
		else
		{
			Heap.HeapPush(*It, QueueSortFunc);
		}
	}
}

void FFileIoStoreRequestQueue::UpdateOrder()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RequestQueueUpdateOrder);
	FScopeLock _(&CriticalSection);
	UpdateSortRequestsByOffset();
	if (bSortRequestsByOffset)
	{
		TArray<FFileIoStoreReadRequest*> Requests;
		for (FFileIoStoreOffsetSortedRequestQueue& SubQueue : SortedPriorityQueues)
		{
			TArray<FFileIoStoreReadRequest*> RequestsRemoved = SubQueue.RemoveMisprioritizedRequests();
			Requests.Append(RequestsRemoved);
		}

		// Pop/Peek rely on empty queues being culled
		SortedPriorityQueues.RemoveAll([](FFileIoStoreOffsetSortedRequestQueue& SubQueue) { return SubQueue.IsEmpty(); });

		Algo::SortBy(Requests, [](FFileIoStoreReadRequest* Request) { return Request->Sequence;  });
		for (FFileIoStoreReadRequest* Request : Requests)
		{
			PushToPriorityQueues(Request);
		}
	}
	else
	{
		Heap.Heapify(QueueSortFunc);
	}
}

void FFileIoStoreRequestQueue::Lock()
{
	CriticalSection.Lock();
}

void FFileIoStoreRequestQueue::Unlock()
{
	CriticalSection.Unlock();
}

void FFileIoStoreRequestQueue::CancelRequestsWithFileHandle(const uint64 FileHandle)
{
	FScopeLock _(&CriticalSection);
	for (FFileIoStoreReadRequest* Request : Heap)
	{
		if (Request->FileHandle == FileHandle)
		{
			Request->bCancelled = true;
		}
	}
}

FFileIoStoreReader::FFileIoStoreReader(FFileIoStoreImpl& InPlatformImpl)
	: PlatformImpl(InPlatformImpl)
{
}

FFileIoStoreReader::~FFileIoStoreReader()
{
	Close();
}

FIoStatus FFileIoStoreReader::Initialize(const TCHAR* InContainerPath, int32 InOrder)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	TStringBuilder<256> TocFilePath;
	TocFilePath.Append(InContainerPath);
	TocFilePath.Append(TEXT(".utoc"));
	ContainerFile.FilePath = TocFilePath;

	UE_LOG(LogIoDispatcher, Display, TEXT("Reading toc: %s"), *TocFilePath);

	TUniquePtr<FIoStoreTocResource> TocResourcePtr = MakeUnique<FIoStoreTocResource>();
	FIoStoreTocResource& TocResource = *TocResourcePtr;
	FIoStatus Status = FIoStoreTocResource::Read(*TocFilePath, EIoStoreTocReadOptions::Default, TocResource);
	if (!Status.IsOk())
	{
		return Status;
	}

	ContainerFile.PartitionSize = TocResource.Header.PartitionSize;
	ContainerFile.Partitions.SetNum(TocResource.Header.PartitionCount);
	for (uint32 PartitionIndex = 0; PartitionIndex < TocResource.Header.PartitionCount; ++PartitionIndex)
	{
		FFileIoStoreContainerFilePartition& Partition = ContainerFile.Partitions[PartitionIndex];
		TStringBuilder<256> ContainerFilePath;
		ContainerFilePath.Append(InContainerPath);
		if (PartitionIndex > 0)
		{
			ContainerFilePath.Appendf(TEXT("_s%d"), PartitionIndex);
		}
		ContainerFilePath.Append(TEXT(".ucas"));
		Partition.FilePath = ContainerFilePath;
		if (!PlatformImpl.OpenContainer(*ContainerFilePath, Partition.FileHandle, Partition.FileSize))
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
		}
		Partition.ContainerFileIndex = GlobalPartitionIndex++;
	}

	Toc.Reserve(TocResource.Header.TocEntryCount);

	for (uint32 ChunkIndex = 0; ChunkIndex < TocResource.Header.TocEntryCount; ++ChunkIndex)
	{
		const FIoOffsetAndLength& ChunkOffsetLength = TocResource.ChunkOffsetLengths[ChunkIndex];
		Toc.Add(TocResource.ChunkIds[ChunkIndex], ChunkOffsetLength);
	}
	
	ContainerFile.CompressionMethods	= MoveTemp(TocResource.CompressionMethods);
	ContainerFile.CompressionBlockSize	= TocResource.Header.CompressionBlockSize;
	ContainerFile.CompressionBlocks		= MoveTemp(TocResource.CompressionBlocks);
	ContainerFile.ContainerFlags		= TocResource.Header.ContainerFlags;
	ContainerFile.EncryptionKeyGuid		= TocResource.Header.EncryptionKeyGuid;
	ContainerFile.BlockSignatureHashes	= MoveTemp(TocResource.ChunkBlockSignatures);
	ContainerFile.ContainerInstanceId	= ++GlobalContainerInstanceId;

	ContainerId = TocResource.Header.ContainerId;
	Order = InOrder;
	return FIoStatus::Ok;
}

FIoStatus FFileIoStoreReader::Close()
{
	if (bClosed)
	{
		return FIoStatus::Ok;
	}

	for (FFileIoStoreContainerFilePartition& Partition : ContainerFile.Partitions)
	{
		PlatformImpl.CloseContainer(Partition.FileHandle);
	}

	Toc.Empty();
	ContainerFile = FFileIoStoreContainerFile();
	ContainerId = FIoContainerId();
	Order = INDEX_NONE;
	bClosed = true;

	return FIoStatus::Ok;
}

bool FFileIoStoreReader::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	check(!bClosed);
	return Toc.Find(ChunkId) != nullptr;
}

TIoStatusOr<uint64> FFileIoStoreReader::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	check(!bClosed);
	const FIoOffsetAndLength* OffsetAndLength = Toc.Find(ChunkId);

	if (OffsetAndLength != nullptr)
	{
		return OffsetAndLength->GetLength();
	}
	else
	{
		return FIoStatus(EIoErrorCode::NotFound);
	}
}

const FIoOffsetAndLength* FFileIoStoreReader::Resolve(const FIoChunkId& ChunkId) const
{
	check(!bClosed);
	return Toc.Find(ChunkId);
}

IMappedFileHandle* FFileIoStoreReader::GetMappedContainerFileHandle(uint64 TocOffset)
{
	check(!bClosed);
	int32 PartitionIndex = int32(TocOffset / ContainerFile.PartitionSize);
	FFileIoStoreContainerFilePartition& Partition = ContainerFile.Partitions[PartitionIndex];
	if (!Partition.MappedFileHandle)
	{
		IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
		Partition.MappedFileHandle.Reset(Ipf.OpenMapped(*Partition.FilePath));
	}

	check(Partition.FileSize > 0);
	return new FMappedFileProxy(Partition.MappedFileHandle.Get(), Partition.FileSize);
}

FFileIoStoreResolvedRequest::FFileIoStoreResolvedRequest(
	FIoRequestImpl& InDispatcherRequest,
	const FFileIoStoreContainerFile& InContainerFile,
	uint64 InResolvedOffset,
	uint64 InResolvedSize)
	: DispatcherRequest(&InDispatcherRequest)
	, ContainerFile(InContainerFile)
	, ResolvedOffset(InResolvedOffset)
	, ResolvedSize(InResolvedSize)
{

}

void FFileIoStoreResolvedRequest::AddReadRequestLink(FFileIoStoreReadRequestLink* ReadRequestLink)
{
	check(!ReadRequestLink->Next);
	if (ReadRequestsTail)
	{
		ReadRequestsTail->Next = ReadRequestLink;
	}
	else
	{
		ReadRequestsHead = ReadRequestLink;
	}
	ReadRequestsTail = ReadRequestLink;
}

FFileIoStoreRequestTracker::FFileIoStoreRequestTracker(FFileIoStoreRequestAllocator& InRequestAllocator, FFileIoStoreRequestQueue& InRequestQueue)
	: RequestAllocator(InRequestAllocator)
	, RequestQueue(InRequestQueue)
{

}

FFileIoStoreRequestTracker::~FFileIoStoreRequestTracker()
{

}

FFileIoStoreCompressedBlock* FFileIoStoreRequestTracker::FindOrAddCompressedBlock(FFileIoStoreBlockKey Key, bool& bOutWasAdded)
{
	bOutWasAdded = false;
	FFileIoStoreCompressedBlock*& Result = CompressedBlocksMap.FindOrAdd(Key);
	if (!Result)
	{
		Result = RequestAllocator.AllocCompressedBlock();
		Result->Key = Key;
		bOutWasAdded = true;
	}
	return Result;
}

FFileIoStoreReadRequest* FFileIoStoreRequestTracker::FindOrAddRawBlock(FFileIoStoreBlockKey Key, bool& bOutWasAdded)
{
	bOutWasAdded = false;
	FFileIoStoreReadRequest*& Result = RawBlocksMap.FindOrAdd(Key);
	if (!Result)
	{
		Result = RequestAllocator.AllocReadRequest();
		Result->Key = Key;
		bOutWasAdded = true;
	}
	return Result;
}

void FFileIoStoreRequestTracker::RemoveRawBlock(const FFileIoStoreReadRequest* RawBlock)
{
	if (!RawBlock->bCancelled)
	{
		RawBlocksMap.Remove(RawBlock->Key);
	}
}

void FFileIoStoreRequestTracker::AddReadRequestsToResolvedRequest(FFileIoStoreCompressedBlock* CompressedBlock, FFileIoStoreResolvedRequest& ResolvedRequest)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(AddReadRequestsToResolvedRequest);
	bool bUpdateQueueOrder = false;
	++ResolvedRequest.UnfinishedReadsCount;
	for (FFileIoStoreReadRequest* ReadRequest : CompressedBlock->RawBlocks)
	{
		FFileIoStoreReadRequestLink* Link = RequestAllocator.AllocRequestLink(ReadRequest);
		++ReadRequest->RefCount;
		ResolvedRequest.AddReadRequestLink(Link);
		if (ResolvedRequest.GetPriority() > ReadRequest->Priority)
		{
			ReadRequest->Priority = ResolvedRequest.GetPriority();
			bUpdateQueueOrder = true;
		}
	}
	if (bUpdateQueueOrder)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RequestTrackerAddIoRequestUpdateOrder);
		RequestQueue.UpdateOrder();
	}
}

void FFileIoStoreRequestTracker::AddReadRequestsToResolvedRequest(const FFileIoStoreReadRequestList& Requests, FFileIoStoreResolvedRequest& ResolvedRequest)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(RequestTrackerAddIoRequest);
	for (FFileIoStoreReadRequest* Request : Requests)
	{
		++ResolvedRequest.UnfinishedReadsCount;
		FFileIoStoreReadRequestLink* Link = RequestAllocator.AllocRequestLink(Request);
		++Request->RefCount;
		ResolvedRequest.AddReadRequestLink(Link);
		check(ResolvedRequest.GetPriority() == Request->Priority);
	}
}

void FFileIoStoreRequestTracker::RemoveCompressedBlock(const FFileIoStoreCompressedBlock* CompressedBlock)
{
	if (!CompressedBlock->bCancelled)
	{
		CompressedBlocksMap.Remove(CompressedBlock->Key);
	}
}

bool FFileIoStoreRequestTracker::CancelIoRequest(FFileIoStoreResolvedRequest& ResolvedRequest)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RequestTrackerCancelIoRequest);
	check(!ResolvedRequest.bCancelled);
	bool bShouldComplete = true;
	RequestQueue.Lock();
	FFileIoStoreReadRequestLink* Link = ResolvedRequest.ReadRequestsHead;
	while (Link)
	{
		FFileIoStoreReadRequest& ReadRequest = Link->ReadRequest;
		Link = Link->Next;

		if (ReadRequest.bCancelled)
		{
			continue;
		}

		if (ReadRequest.QueueStatus == FFileIoStoreReadRequest::QueueStatus_Started)
		{
			bShouldComplete = false;
			continue;
		}

		bool bCancelReadRequest = true;
		for (FFileIoStoreCompressedBlock* CompressedBlock : ReadRequest.CompressedBlocks)
		{
			if (CompressedBlock->bCancelled)
			{
				continue;
			}
			bool bCancelCompressedBlock = true;
			for (FFileIoStoreBlockScatter& Scatter : CompressedBlock->ScatterList)
			{
				if (Scatter.Size > 0 && Scatter.Request != &ResolvedRequest)
				{
					bCancelCompressedBlock = false;
					bCancelReadRequest = false;
				}
				else
				{
					Scatter.Size = 0;
				}
			}
			if (bCancelCompressedBlock)
			{
				CompressedBlock->bCancelled = true;
				CompressedBlocksMap.Remove(CompressedBlock->Key);
			}
		}
		if (bCancelReadRequest)
		{
			if (!ReadRequest.ImmediateScatter.Request)
			{
				RawBlocksMap.Remove(ReadRequest.Key);
			}
			ReadRequest.bCancelled = true;
#if DO_CHECK
			for (FFileIoStoreCompressedBlock* CompressedBlock : ReadRequest.CompressedBlocks)
			{
				check(CompressedBlock->bCancelled);
				for (FFileIoStoreBlockScatter& Scatter : CompressedBlock->ScatterList)
				{
					check(!Scatter.Request->DispatcherRequest || Scatter.Request->DispatcherRequest->IsCancelled());
				}
			}
#endif
		}
	}
	RequestQueue.Unlock();

	return bShouldComplete;
}

void FFileIoStoreRequestTracker::UpdatePriorityForIoRequest(FFileIoStoreResolvedRequest& ResolvedRequest)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RequestTrackerUpdatePriorityForIoRequest);
	bool bUpdateOrder = false;
	FFileIoStoreReadRequestLink* Link = ResolvedRequest.ReadRequestsHead;
	while (Link)
	{
		FFileIoStoreReadRequest& ReadRequest = Link->ReadRequest;
		Link = Link->Next;
		if (ResolvedRequest.GetPriority() > ReadRequest.Priority)
		{
			ReadRequest.Priority = ResolvedRequest.GetPriority();
			bUpdateOrder = true;
		}
	}
	if (bUpdateOrder)
	{
		RequestQueue.UpdateOrder();
	}
}

void FFileIoStoreRequestTracker::ReleaseIoRequestReferences(FFileIoStoreResolvedRequest& ResolvedRequest)
{
	FFileIoStoreReadRequestLink* Link = ResolvedRequest.ReadRequestsHead;
	while (Link)
	{
		FFileIoStoreReadRequestLink* Next = Link->Next;
		check(Link->ReadRequest.RefCount > 0);
		if (--Link->ReadRequest.RefCount == 0)
		{
			for (FFileIoStoreCompressedBlock* CompressedBlock : Link->ReadRequest.CompressedBlocks)
			{
				check(CompressedBlock->RefCount > 0);
				if (--CompressedBlock->RefCount == 0)
				{
					RequestAllocator.Free(CompressedBlock);
				}
			}
			RequestAllocator.Free(&Link->ReadRequest);
		}
		RequestAllocator.Free(Link);
		Link = Next;
	}
	ResolvedRequest.ReadRequestsHead = nullptr;
	ResolvedRequest.ReadRequestsTail = nullptr;
	RequestAllocator.Free(&ResolvedRequest);
}

FFileIoStore::FFileIoStore()
	: RequestTracker(RequestAllocator, RequestQueue)
	, PlatformImpl(EventQueue, BufferAllocator, BlockCache)
{
}

FFileIoStore::~FFileIoStore()
{
	delete Thread;
}

void FFileIoStore::Initialize(TSharedRef<const FIoDispatcherBackendContext> InContext)
{
	check(!Thread);
	BackendContext = InContext;
	bIsMultithreaded = InContext->bIsMultiThreaded;

	ReadBufferSize = (GIoDispatcherBufferSizeKB > 0 ? uint64(GIoDispatcherBufferSizeKB) << 10 : 256 << 10);

	uint64 BufferMemorySize = uint64(GIoDispatcherBufferMemoryMB) << 20ull;
	uint64 BufferSize = uint64(GIoDispatcherBufferSizeKB) << 10ull;
	uint32 BufferAlignment = uint32(GIoDispatcherBufferAlignment);
	BufferAllocator.Initialize(BufferMemorySize, BufferSize, BufferAlignment);

	uint64 CacheMemorySize = uint64(GIoDispatcherCacheSizeMB) << 20ull;
	BlockCache.Initialize(CacheMemorySize, BufferSize);

	PlatformImpl.Initialize(&BackendContext->WakeUpDispatcherThreadDelegate);

	uint64 DecompressionContextCount = uint64(GIoDispatcherDecompressionWorkerCount > 0 ? GIoDispatcherDecompressionWorkerCount : 4);
	for (uint64 ContextIndex = 0; ContextIndex < DecompressionContextCount; ++ContextIndex)
	{
		FFileIoStoreCompressionContext* Context = new FFileIoStoreCompressionContext();
		Context->Next = FirstFreeCompressionContext;
		FirstFreeCompressionContext = Context;
	}

	Thread = FRunnableThread::Create(this, TEXT("IoService"), 0, TPri_AboveNormal);
}

TIoStatusOr<FIoContainerId> FFileIoStore::Mount(const TCHAR* ContainerPath, int32 Order, const FGuid& EncryptionKeyGuid, const FAES::FAESKey& EncryptionKey)
{
	TUniquePtr<FFileIoStoreReader> Reader(new FFileIoStoreReader(PlatformImpl));
	FIoStatus IoStatus = Reader->Initialize(ContainerPath, Order);
	if (!IoStatus.IsOk())
	{
		return IoStatus;
	}

	if (Reader->IsEncrypted())
	{
		if (Reader->GetEncryptionKeyGuid() == EncryptionKeyGuid && EncryptionKey.IsValid())
		{
			Reader->SetEncryptionKey(EncryptionKey);
		}
		else
		{
			return FIoStatus(EIoErrorCode::InvalidEncryptionKey, *FString::Printf(TEXT("Invalid encryption key '%s' (container '%s', encryption key '%s')"),
				*EncryptionKeyGuid.ToString(), *FPaths::GetBaseFilename(ContainerPath), *Reader->GetEncryptionKeyGuid().ToString()));
		}
	}

	int32 InsertionIndex;
	FIoContainerId ContainerId = Reader->GetContainerId();
	{
		FWriteScopeLock _(IoStoreReadersLock);
		InsertionIndex = Algo::UpperBound(IoStoreReaders, Reader, [](const TUniquePtr<FFileIoStoreReader>& A, const TUniquePtr<FFileIoStoreReader>& B)
		{
			if (A->GetOrder() != B->GetOrder())
			{
				return A->GetOrder() > B->GetOrder();
			}
			return A->GetContainerInstanceId() > B->GetContainerInstanceId();
		});
		IoStoreReaders.Insert(MoveTemp(Reader), InsertionIndex);
		UE_LOG(LogIoDispatcher, Display, TEXT("Mounting container '%s' in location slot %d"), ContainerPath, InsertionIndex);
	}
	if (BackendContext && BackendContext->ContainerMountedDelegate.IsBound())
	{
		BackendContext->ContainerMountedDelegate.Broadcast(ContainerId);
	}
	return ContainerId;
}

TIoStatusOr<FIoContainerId> FFileIoStore::Unmount(const TCHAR* ContainerPath)
{
	FWriteScopeLock _(IoStoreReadersLock);
	
	FString FilePathToUnmount = FPaths::SetExtension(ContainerPath, FString(".utoc"));

	for (int32 Idx = 0; Idx < IoStoreReaders.Num(); ++Idx)
	{
		if (IoStoreReaders[Idx]->GetContainerFile().FilePath == FilePathToUnmount)
		{
			UE_LOG(LogIoDispatcher, Display, TEXT("Unmounting container '%s'"), *FPaths::GetBaseFilename(ContainerPath));

			// Cancel pending I/O requests trying to read from the container
			for (const FFileIoStoreContainerFilePartition& Partition : IoStoreReaders[Idx]->GetContainerFile().Partitions)
			{
				RequestQueue.CancelRequestsWithFileHandle(Partition.FileHandle);
			}

			FIoContainerId ContainerId = IoStoreReaders[Idx]->GetContainerId();
			IoStoreReaders.RemoveAt(Idx);
			
			return TIoStatusOr<FIoContainerId>(ContainerId);
		}
	}

	UE_LOG(LogIoDispatcher, Display, TEXT("Failed to unmount container '%s'"), *FPaths::GetBaseFilename(ContainerPath));
	
	return FIoStatus(EIoErrorCode::NotFound);
}

bool FFileIoStore::Resolve(FIoRequestImpl* Request)
{
	FReadScopeLock _(IoStoreReadersLock);
	for (const TUniquePtr<FFileIoStoreReader>& Reader : IoStoreReaders)
	{
		if (const FIoOffsetAndLength* OffsetAndLength = Reader->Resolve(Request->ChunkId))
		{
			uint64 RequestedOffset = Request->Options.GetOffset();
			uint64 ResolvedOffset = OffsetAndLength->GetOffset() + RequestedOffset;
			uint64 ResolvedSize = 0;
			if (RequestedOffset <= OffsetAndLength->GetLength())
			{
				ResolvedSize = FMath::Min(Request->Options.GetSize(), OffsetAndLength->GetLength() - RequestedOffset);
			}

			FFileIoStoreResolvedRequest* ResolvedRequest = RequestAllocator.AllocResolvedRequest(
				*Request,
				Reader->GetContainerFile(),
				ResolvedOffset,
				ResolvedSize);
			Request->BackendData = ResolvedRequest;

			if (ResolvedSize > 0)
			{
				if (void* TargetVa = Request->Options.GetTargetVa())
				{
					Request->IoBuffer = FIoBuffer(FIoBuffer::Wrap, TargetVa, ResolvedSize);
				}
				else
				{
					LLM_SCOPE(ELLMTag::FileSystem);
					TRACE_CPUPROFILER_EVENT_SCOPE(AllocMemoryForRequest);
					Request->IoBuffer = FIoBuffer(ResolvedSize);
				}

				FFileIoStoreReadRequestList CustomRequests;
				if (PlatformImpl.CreateCustomRequests(RequestAllocator, *ResolvedRequest, CustomRequests))
				{
					RequestTracker.AddReadRequestsToResolvedRequest(CustomRequests, *ResolvedRequest);
					RequestQueue.Push(CustomRequests);
					OnNewPendingRequestsAdded();
				}
				else
				{
					ReadBlocks(*ResolvedRequest);
				}
			}
			else
			{
				// Nothing to read
				CompleteDispatcherRequest(ResolvedRequest);
				RequestTracker.ReleaseIoRequestReferences(*ResolvedRequest);
			}

			return true;
		}
	}

	return false;
}

void FFileIoStore::CancelIoRequest(FIoRequestImpl* Request)
{
	if (Request->BackendData)
	{
		FFileIoStoreResolvedRequest* ResolvedRequest = static_cast<FFileIoStoreResolvedRequest*>(Request->BackendData);
		bool bShouldComplete = RequestTracker.CancelIoRequest(*ResolvedRequest);
		if (bShouldComplete)
		{
			ResolvedRequest->bCancelled = true;
			CompleteDispatcherRequest(ResolvedRequest);
		}
	}
}

void FFileIoStore::UpdatePriorityForIoRequest(FIoRequestImpl* Request)
{
	if (Request->BackendData)
	{
		FFileIoStoreResolvedRequest* ResolvedRequest = static_cast<FFileIoStoreResolvedRequest*>(Request->BackendData);
		RequestTracker.UpdatePriorityForIoRequest(*ResolvedRequest);
	}
}

bool FFileIoStore::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	FReadScopeLock _(IoStoreReadersLock);
	for (const TUniquePtr<FFileIoStoreReader>& Reader : IoStoreReaders)
	{
		if (Reader->DoesChunkExist(ChunkId))
		{
			return true;
		}
	}
	return false;
}

TIoStatusOr<uint64> FFileIoStore::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	FReadScopeLock _(IoStoreReadersLock);
	for (const TUniquePtr<FFileIoStoreReader>& Reader : IoStoreReaders)
	{
		TIoStatusOr<uint64> ReaderResult = Reader->GetSizeForChunk(ChunkId);
		if (ReaderResult.IsOk())
		{
			return ReaderResult;
		}
	}
	return FIoStatus(EIoErrorCode::NotFound);
}

FAutoConsoleTaskPriority CPrio_IoDispatcherTaskPriority(
	TEXT("TaskGraph.TaskPriorities.IoDispatcherAsyncTasks"),
	TEXT("Task and thread priority for IoDispatcher decompression."),
	ENamedThreads::BackgroundThreadPriority, // if we have background priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::NormalTaskPriority // if we don't have background threads, then use normal priority threads at normal task priority instead
);

ENamedThreads::Type FFileIoStore::FDecompressAsyncTask::GetDesiredThread()
{
	return CPrio_IoDispatcherTaskPriority.Get();
}

void FFileIoStore::ScatterBlock(FFileIoStoreCompressedBlock* CompressedBlock, bool bIsAsync)
{
	LLM_SCOPE(ELLMTag::FileSystem);
	TRACE_CPUPROFILER_EVENT_SCOPE(IoDispatcherScatter);
	
	check(!CompressedBlock->bFailed);

	FFileIoStoreCompressionContext* CompressionContext = CompressedBlock->CompressionContext;
	check(CompressionContext);
	uint8* CompressedBuffer;
	if (CompressedBlock->RawBlocks.Num() > 1)
	{
		check(CompressedBlock->CompressedDataBuffer);
		CompressedBuffer = CompressedBlock->CompressedDataBuffer;
	}
	else
	{
		FFileIoStoreReadRequest* RawBlock = CompressedBlock->RawBlocks[0];
		check(CompressedBlock->RawOffset >= RawBlock->Offset);
		uint64 OffsetInBuffer = CompressedBlock->RawOffset - RawBlock->Offset;
		CompressedBuffer = RawBlock->Buffer->Memory + OffsetInBuffer;
	}
	if (CompressedBlock->SignatureHash)
	{
		FSHAHash BlockHash;
		FSHA1::HashBuffer(CompressedBuffer, CompressedBlock->RawSize, BlockHash.Hash);
		if (*CompressedBlock->SignatureHash != BlockHash)
		{
			FIoSignatureError Error;
			{
				FReadScopeLock _(IoStoreReadersLock);
				for (const TUniquePtr<FFileIoStoreReader>& Reader : IoStoreReaders)
				{
					if (CompressedBlock->Key.FileIndex == Reader->GetContainerInstanceId())
					{
						Error.ContainerName = FPaths::GetBaseFilename(Reader->GetContainerFile().FilePath);
					}
				}
				Error.BlockIndex = CompressedBlock->Key.BlockIndex;
				Error.ExpectedHash = *CompressedBlock->SignatureHash;
				Error.ActualHash = BlockHash;
			}

			UE_LOG(LogIoDispatcher, Warning, TEXT("Signature error detected in container '%s' at block index '%d'"), *Error.ContainerName, Error.BlockIndex);

			check(BackendContext);
			if (BackendContext->SignatureErrorDelegate.IsBound())
			{
				BackendContext->SignatureErrorDelegate.Broadcast(Error);
			}
		}
	}
	if (!CompressedBlock->bFailed)
	{
		if (CompressedBlock->EncryptionKey.IsValid())
		{
			FAES::DecryptData(CompressedBuffer, CompressedBlock->RawSize, CompressedBlock->EncryptionKey);
		}
		uint8* UncompressedBuffer;
		if (CompressedBlock->CompressionMethod.IsNone())
		{
			UncompressedBuffer = CompressedBuffer;
		}
		else
		{
			if (CompressionContext->UncompressedBufferSize < CompressedBlock->UncompressedSize)
			{
				FMemory::Free(CompressionContext->UncompressedBuffer);
				CompressionContext->UncompressedBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(CompressedBlock->UncompressedSize));
				CompressionContext->UncompressedBufferSize = CompressedBlock->UncompressedSize;
			}
			UncompressedBuffer = CompressionContext->UncompressedBuffer;

			bool bFailed = !FCompression::UncompressMemory(CompressedBlock->CompressionMethod, UncompressedBuffer, int32(CompressedBlock->UncompressedSize), CompressedBuffer, int32(CompressedBlock->CompressedSize));
			if (bFailed)
			{
				UE_LOG(LogIoDispatcher, Warning, TEXT("Failed decompressing block"));
				CompressedBlock->bFailed = true;
			}
		}

		for (FFileIoStoreBlockScatter& Scatter : CompressedBlock->ScatterList)
		{
			if (Scatter.Size)
			{
				FMemory::Memcpy(Scatter.Request->GetIoBuffer().Data() + Scatter.DstOffset, UncompressedBuffer + Scatter.SrcOffset, Scatter.Size);
			}
		}
	}

	if (bIsAsync)
	{
		FScopeLock Lock(&DecompressedBlocksCritical);
		CompressedBlock->Next = FirstDecompressedBlock;
		FirstDecompressedBlock = CompressedBlock;

		BackendContext->WakeUpDispatcherThreadDelegate.Execute();
	}
}

void FFileIoStore::CompleteDispatcherRequest(FFileIoStoreResolvedRequest* ResolvedRequest)
{
	check(ResolvedRequest);
	check(ResolvedRequest->DispatcherRequest);
	FIoRequestImpl* DispatcherRequest = ResolvedRequest->DispatcherRequest;
	ResolvedRequest->DispatcherRequest = nullptr;
	if (ResolvedRequest->bFailed)
	{
		DispatcherRequest->SetFailed();
	}
	DispatcherRequest->BackendData = nullptr;
	if (!CompletedRequestsTail)
	{
		CompletedRequestsHead = CompletedRequestsTail = DispatcherRequest;
	}
	else
	{
		CompletedRequestsTail->NextRequest = DispatcherRequest;
		CompletedRequestsTail = DispatcherRequest;
	}
	CompletedRequestsTail->NextRequest = nullptr;
}

void FFileIoStore::FinalizeCompressedBlock(FFileIoStoreCompressedBlock* CompressedBlock)
{
	if (CompressedBlock->RawBlocks.Num() > 1)
	{
		check(CompressedBlock->CompressedDataBuffer || CompressedBlock->bCancelled || CompressedBlock->bFailed);
		if (CompressedBlock->CompressedDataBuffer)
		{
			FMemory::Free(CompressedBlock->CompressedDataBuffer);
		}
	}
	else
	{
		FFileIoStoreReadRequest* RawBlock = CompressedBlock->RawBlocks[0];
		check(RawBlock->BufferRefCount > 0);
		if (--RawBlock->BufferRefCount == 0)
		{
			check(RawBlock->Buffer || RawBlock->bCancelled);
			if (RawBlock->Buffer)
			{
				FreeBuffer(*RawBlock->Buffer);
				RawBlock->Buffer = nullptr;
			}
		}
	}
	check(CompressedBlock->CompressionContext || CompressedBlock->bCancelled || CompressedBlock->bFailed);
	if (CompressedBlock->CompressionContext)
	{
		FreeCompressionContext(CompressedBlock->CompressionContext);
	}
	for (int32 ScatterIndex = 0, ScatterCount = CompressedBlock->ScatterList.Num(); ScatterIndex < ScatterCount; ++ScatterIndex)
	{
		FFileIoStoreBlockScatter& Scatter = CompressedBlock->ScatterList[ScatterIndex];
		TRACE_COUNTER_ADD(IoDispatcherTotalBytesScattered, Scatter.Size);
		Scatter.Request->bFailed |= CompressedBlock->bFailed;
		check(!CompressedBlock->bCancelled || !Scatter.Request->DispatcherRequest || Scatter.Request->DispatcherRequest->IsCancelled());
		check(Scatter.Request->UnfinishedReadsCount > 0);
		if (--Scatter.Request->UnfinishedReadsCount == 0)
		{
			if (!Scatter.Request->bCancelled)
			{
				CompleteDispatcherRequest(Scatter.Request);
			}
			RequestTracker.ReleaseIoRequestReferences(*Scatter.Request);
		}
	}
}

FIoRequestImpl* FFileIoStore::GetCompletedRequests()
{
	LLM_SCOPE(ELLMTag::FileSystem);
	//TRACE_CPUPROFILER_EVENT_SCOPE(GetCompletedRequests);
	
	if (!bIsMultithreaded)
	{
		while (PlatformImpl.StartRequests(RequestQueue));
	}

	FFileIoStoreReadRequestList CompletedRequests;
	PlatformImpl.GetCompletedRequests(CompletedRequests);
	for (auto It = CompletedRequests.Steal(); It; ++It)
	{
		FFileIoStoreReadRequest* CompletedRequest = *It;

		TRACE_COUNTER_ADD(IoDispatcherTotalBytesRead, CompletedRequest->Size);
		TRACE_COUNTER_DECREMENT(IoDispatcherOutstandingReads);
		TRACE_COUNTER_SUBTRACT(IoDispatcherOutstandingBytesToRead, CompletedRequest->Size);

		if (!CompletedRequest->ImmediateScatter.Request)
		{
			check(CompletedRequest->Buffer || CompletedRequest->bCancelled);
			RequestTracker.RemoveRawBlock(CompletedRequest);
			
			//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessCompletedBlock);
			for (FFileIoStoreCompressedBlock* CompressedBlock : CompletedRequest->CompressedBlocks)
			{
				CompressedBlock->bFailed |= CompletedRequest->bFailed;
				CompressedBlock->bCancelled |= CompletedRequest->bCancelled;
				if (CompressedBlock->RawBlocks.Num() > 1)
				{
					//TRACE_CPUPROFILER_EVENT_SCOPE(HandleComplexBlock);
					if (!(CompressedBlock->bCancelled | CompressedBlock->bFailed))
					{
						check(CompletedRequest->Buffer);
						if (!CompressedBlock->CompressedDataBuffer)
						{
							CompressedBlock->CompressedDataBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(CompressedBlock->RawSize));
						}

						uint8* Src = CompletedRequest->Buffer->Memory;
						uint8* Dst = CompressedBlock->CompressedDataBuffer;
						uint64 CopySize = CompletedRequest->Size;
						int64 CompletedBlockOffsetInBuffer = int64(CompletedRequest->Offset) - int64(CompressedBlock->RawOffset);
						if (CompletedBlockOffsetInBuffer < 0)
						{
							Src -= CompletedBlockOffsetInBuffer;
							CopySize += CompletedBlockOffsetInBuffer;
						}
						else
						{
							Dst += CompletedBlockOffsetInBuffer;
						}
						uint64 CompressedBlockRawEndOffset = CompressedBlock->RawOffset + CompressedBlock->RawSize;
						uint64 CompletedBlockEndOffset = CompletedRequest->Offset + CompletedRequest->Size;
						if (CompletedBlockEndOffset > CompressedBlockRawEndOffset)
						{
							CopySize -= CompletedBlockEndOffset - CompressedBlockRawEndOffset;
						}
						FMemory::Memcpy(Dst, Src, CopySize);
					}
					check(CompletedRequest->BufferRefCount > 0);
					if (--CompletedRequest->BufferRefCount == 0)
					{
						if (CompletedRequest->Buffer)
						{
							FreeBuffer(*CompletedRequest->Buffer);
							CompletedRequest->Buffer = nullptr;
						}
					}
				}

				check(CompressedBlock->UnfinishedRawBlocksCount > 0);
				if (--CompressedBlock->UnfinishedRawBlocksCount == 0)
				{
					RequestTracker.RemoveCompressedBlock(CompressedBlock);
					if (!ReadyForDecompressionTail)
					{
						ReadyForDecompressionHead = ReadyForDecompressionTail = CompressedBlock;
					}
					else
					{
						ReadyForDecompressionTail->Next = CompressedBlock;
						ReadyForDecompressionTail = CompressedBlock;
					}
					CompressedBlock->Next = nullptr;
				}
			}
		}
		else
		{
			TRACE_COUNTER_ADD(IoDispatcherTotalBytesScattered, CompletedRequest->ImmediateScatter.Size);

			check(!CompletedRequest->Buffer);
			FFileIoStoreResolvedRequest* CompletedResolvedRequest = CompletedRequest->ImmediateScatter.Request;
			CompletedResolvedRequest->bFailed |= CompletedRequest->bFailed;
			check(!CompletedRequest->bCancelled || !CompletedResolvedRequest->DispatcherRequest || CompletedResolvedRequest->DispatcherRequest->IsCancelled());
			check(CompletedResolvedRequest->UnfinishedReadsCount > 0);
			if (--CompletedResolvedRequest->UnfinishedReadsCount == 0)
			{
				if (!CompletedResolvedRequest->bCancelled)
				{
					CompleteDispatcherRequest(CompletedResolvedRequest);
				}
				RequestTracker.ReleaseIoRequestReferences(*CompletedResolvedRequest);
			}
		}
	}
	
	FFileIoStoreCompressedBlock* BlockToReap;
	{
		FScopeLock Lock(&DecompressedBlocksCritical);
		BlockToReap = FirstDecompressedBlock;
		FirstDecompressedBlock = nullptr;
	}

	while (BlockToReap)
	{
		FFileIoStoreCompressedBlock* Next = BlockToReap->Next;
		FinalizeCompressedBlock(BlockToReap);
		BlockToReap = Next;
	}

	FFileIoStoreCompressedBlock* BlockToDecompress = ReadyForDecompressionHead;
	while (BlockToDecompress)
	{
		FFileIoStoreCompressedBlock* Next = BlockToDecompress->Next;
		if (BlockToDecompress->bFailed | BlockToDecompress->bCancelled)
		{
			FinalizeCompressedBlock(BlockToDecompress);
			BlockToDecompress = Next;
			continue;
		}
		
		BlockToDecompress->CompressionContext = AllocCompressionContext();
		if (!BlockToDecompress->CompressionContext)
		{
			break;
		}
		// Scatter block asynchronous when the block is compressed, encrypted or signed
		const bool bScatterAsync = bIsMultithreaded && (!BlockToDecompress->CompressionMethod.IsNone() || BlockToDecompress->EncryptionKey.IsValid() || BlockToDecompress->SignatureHash);
		if (bScatterAsync)
		{
			TGraphTask<FDecompressAsyncTask>::CreateTask().ConstructAndDispatchWhenReady(*this, BlockToDecompress);
		}
		else
		{
			ScatterBlock(BlockToDecompress, false);
			FinalizeCompressedBlock(BlockToDecompress);
		}
		BlockToDecompress = Next;
	}
	ReadyForDecompressionHead = BlockToDecompress;
	if (!ReadyForDecompressionHead)
	{
		ReadyForDecompressionTail = nullptr;
	}

	FIoRequestImpl* Result = CompletedRequestsHead;
	CompletedRequestsHead = CompletedRequestsTail = nullptr;
	return Result;
}

void FFileIoStore::AppendMountedContainers(TSet<FIoContainerId>& OutContainers)
{
	FReadScopeLock _(IoStoreReadersLock);
	for (TUniquePtr<FFileIoStoreReader>& Reader : IoStoreReaders)
	{
		OutContainers.Add(Reader->GetContainerId());
	}
}

TIoStatusOr<FIoMappedRegion> FFileIoStore::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	if (!FPlatformProperties::SupportsMemoryMappedFiles())
	{
		return FIoStatus(EIoErrorCode::Unknown, TEXT("Platform does not support memory mapped files"));
	}

	if (Options.GetTargetVa() != nullptr)
	{
		return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("Invalid read options"));
	}

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	FReadScopeLock _(IoStoreReadersLock);
	for (TUniquePtr<FFileIoStoreReader>& Reader : IoStoreReaders)
	{
		if (const FIoOffsetAndLength* OffsetAndLength = Reader->Resolve(ChunkId))
		{
			uint64 ResolvedOffset = OffsetAndLength->GetOffset();
			uint64 ResolvedSize = FMath::Min(Options.GetSize(), OffsetAndLength->GetLength());
			
			const FFileIoStoreContainerFile& ContainerFile = Reader->GetContainerFile();
			
			int32 BlockIndex = int32(ResolvedOffset / ContainerFile.CompressionBlockSize);
			const FIoStoreTocCompressedBlockEntry& CompressionBlockEntry = ContainerFile.CompressionBlocks[BlockIndex];
			const int64 BlockOffset = (int64)CompressionBlockEntry.GetOffset();
			check(BlockOffset > 0 && IsAligned(BlockOffset, FPlatformProperties::GetMemoryMappingAlignment()));

			IMappedFileHandle* MappedFileHandle = Reader->GetMappedContainerFileHandle(BlockOffset);
			IMappedFileRegion* MappedFileRegion = MappedFileHandle->MapRegion(BlockOffset + Options.GetOffset(), ResolvedSize);
			if (MappedFileRegion != nullptr)
			{
				check(IsAligned(MappedFileRegion->GetMappedPtr(), FPlatformProperties::GetMemoryMappingAlignment()));
				return FIoMappedRegion{ MappedFileHandle, MappedFileRegion };
			}
			else
			{
				return FIoStatus(EIoErrorCode::ReadError);
			}
		}
	}

	// We didn't find any entry for the ChunkId.
	return FIoStatus(EIoErrorCode::NotFound);
}

void FFileIoStore::OnNewPendingRequestsAdded()
{
	if (bIsMultithreaded)
	{
		EventQueue.ServiceNotify();
	}
}

void FFileIoStore::ReadBlocks(FFileIoStoreResolvedRequest& ResolvedRequest)
{
	/*TStringBuilder<256> ScopeName;
	ScopeName.Appendf(TEXT("ReadBlock %d"), BlockIndex);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*ScopeName);*/

	const FFileIoStoreContainerFile& ContainerFile = ResolvedRequest.GetContainerFile();
	const uint64 CompressionBlockSize = ContainerFile.CompressionBlockSize;
	const uint64 RequestEndOffset = ResolvedRequest.ResolvedOffset + ResolvedRequest.ResolvedSize;
	int32 RequestBeginBlockIndex = int32(ResolvedRequest.ResolvedOffset / CompressionBlockSize);
	int32 RequestEndBlockIndex = int32((RequestEndOffset - 1) / CompressionBlockSize);

	FFileIoStoreReadRequestList NewBlocks;

	uint64 RequestStartOffsetInBlock = ResolvedRequest.ResolvedOffset - RequestBeginBlockIndex * CompressionBlockSize;
	uint64 RequestRemainingBytes = ResolvedRequest.ResolvedSize;
	uint64 OffsetInRequest = 0;
	for (int32 CompressedBlockIndex = RequestBeginBlockIndex; CompressedBlockIndex <= RequestEndBlockIndex; ++CompressedBlockIndex)
	{
		FFileIoStoreBlockKey CompressedBlockKey;
		CompressedBlockKey.FileIndex = ResolvedRequest.GetContainerFile().ContainerInstanceId;
		CompressedBlockKey.BlockIndex = CompressedBlockIndex;
		bool bCompressedBlockWasAdded;
		FFileIoStoreCompressedBlock* CompressedBlock = RequestTracker.FindOrAddCompressedBlock(CompressedBlockKey, bCompressedBlockWasAdded);
		check(CompressedBlock);
		check(!CompressedBlock->bCancelled);
		if (bCompressedBlockWasAdded)
		{
			CompressedBlock->EncryptionKey = ContainerFile.EncryptionKey;
			bool bCacheable = OffsetInRequest > 0 || RequestRemainingBytes < CompressionBlockSize;

			const FIoStoreTocCompressedBlockEntry& CompressionBlockEntry = ContainerFile.CompressionBlocks[CompressedBlockIndex];
			CompressedBlock->UncompressedSize = CompressionBlockEntry.GetUncompressedSize();
			CompressedBlock->CompressedSize = CompressionBlockEntry.GetCompressedSize();
			CompressedBlock->CompressionMethod = ContainerFile.CompressionMethods[CompressionBlockEntry.GetCompressionMethodIndex()];
			CompressedBlock->SignatureHash = EnumHasAnyFlags(ContainerFile.ContainerFlags, EIoContainerFlags::Signed) ? &ContainerFile.BlockSignatureHashes[CompressedBlockIndex] : nullptr;
			CompressedBlock->RawSize = Align(CompressionBlockEntry.GetCompressedSize(), FAES::AESBlockSize); // The raw blocks size is always aligned to AES blocks size;

			int32 PartitionIndex = int32(CompressionBlockEntry.GetOffset() / ContainerFile.PartitionSize);
			const FFileIoStoreContainerFilePartition& Partition = ContainerFile.Partitions[PartitionIndex];
			uint64 PartitionRawOffset = CompressionBlockEntry.GetOffset() % ContainerFile.PartitionSize;
			CompressedBlock->RawOffset = PartitionRawOffset;
			const uint32 RawBeginBlockIndex = uint32(PartitionRawOffset / ReadBufferSize);
			const uint32 RawEndBlockIndex = uint32((PartitionRawOffset + CompressedBlock->RawSize - 1) / ReadBufferSize);
			const uint32 RawBlockCount = RawEndBlockIndex - RawBeginBlockIndex + 1;
			check(RawBlockCount > 0);
			for (uint32 RawBlockIndex = RawBeginBlockIndex; RawBlockIndex <= RawEndBlockIndex; ++RawBlockIndex)
			{
				FFileIoStoreBlockKey RawBlockKey;
				RawBlockKey.BlockIndex = RawBlockIndex;
				RawBlockKey.FileIndex = Partition.ContainerFileIndex;

				bool bRawBlockWasAdded;
				FFileIoStoreReadRequest* RawBlock = RequestTracker.FindOrAddRawBlock(RawBlockKey, bRawBlockWasAdded);
				check(RawBlock);
				check(!RawBlock->bCancelled);
				if (bRawBlockWasAdded)
				{
					RawBlock->Priority = ResolvedRequest.GetPriority();
					RawBlock->FileHandle = Partition.FileHandle;
					RawBlock->bIsCacheable = bCacheable;
					RawBlock->Offset = RawBlockIndex * ReadBufferSize;
					uint64 ReadSize = FMath::Min(Partition.FileSize, RawBlock->Offset + ReadBufferSize) - RawBlock->Offset;
					RawBlock->Size = ReadSize;
					NewBlocks.Add(RawBlock);
				}
				CompressedBlock->RawBlocks.Add(RawBlock);
				++CompressedBlock->UnfinishedRawBlocksCount;
				++CompressedBlock->RefCount;
				RawBlock->CompressedBlocks.Add(CompressedBlock);
				++RawBlock->BufferRefCount;
			}
		}
		check(CompressedBlock->UncompressedSize > RequestStartOffsetInBlock);
		uint64 RequestSizeInBlock = FMath::Min<uint64>(CompressedBlock->UncompressedSize - RequestStartOffsetInBlock, RequestRemainingBytes);
		check(OffsetInRequest + RequestSizeInBlock <= ResolvedRequest.GetIoBuffer().DataSize());
		check(RequestStartOffsetInBlock + RequestSizeInBlock <= CompressedBlock->UncompressedSize);

		FFileIoStoreBlockScatter& Scatter = CompressedBlock->ScatterList.AddDefaulted_GetRef();
		Scatter.Request = &ResolvedRequest;
		Scatter.DstOffset = OffsetInRequest;
		Scatter.SrcOffset = RequestStartOffsetInBlock;
		Scatter.Size = RequestSizeInBlock;

		RequestRemainingBytes -= RequestSizeInBlock;
		OffsetInRequest += RequestSizeInBlock;
		RequestStartOffsetInBlock = 0;

		RequestTracker.AddReadRequestsToResolvedRequest(CompressedBlock, ResolvedRequest);
	}

	if (!NewBlocks.IsEmpty())
	{
		RequestQueue.Push(NewBlocks);
		OnNewPendingRequestsAdded();
	}
}

void FFileIoStore::FreeBuffer(FFileIoStoreBuffer& Buffer)
{
	BufferAllocator.FreeBuffer(&Buffer);
	EventQueue.ServiceNotify();
}

FFileIoStoreCompressionContext* FFileIoStore::AllocCompressionContext()
{
	FFileIoStoreCompressionContext* Result = FirstFreeCompressionContext;
	if (Result)
	{
		FirstFreeCompressionContext = FirstFreeCompressionContext->Next;
	}
	return Result;
}

void FFileIoStore::FreeCompressionContext(FFileIoStoreCompressionContext* CompressionContext)
{
	CompressionContext->Next = FirstFreeCompressionContext;
	FirstFreeCompressionContext = CompressionContext;
}

void FFileIoStore::UpdateAsyncIOMinimumPriority()
{
	EAsyncIOPriorityAndFlags NewAsyncIOMinimumPriority = AIOP_MIN;
	if (FFileIoStoreReadRequest* NextRequest = RequestQueue.Peek())
	{
		if (NextRequest->Priority >= IoDispatcherPriority_High)
		{
			NewAsyncIOMinimumPriority = AIOP_MAX;
		}
		else if (NextRequest->Priority >= IoDispatcherPriority_Medium)
		{
			NewAsyncIOMinimumPriority = AIOP_Normal;
		}
	}
	if (NewAsyncIOMinimumPriority != CurrentAsyncIOMinimumPriority)
	{
		//TRACE_BOOKMARK(TEXT("SetAsyncMinimumPrioirity(%d)"), NewAsyncIOMinimumPriority);
		FPlatformFileManager::Get().GetPlatformFile().SetAsyncMinimumPriority(NewAsyncIOMinimumPriority);
		CurrentAsyncIOMinimumPriority = NewAsyncIOMinimumPriority;
	}
}

bool FFileIoStore::Init()
{
	return true;
}

void FFileIoStore::Stop()
{
	bStopRequested = true;
	EventQueue.ServiceNotify();
}

uint32 FFileIoStore::Run()
{
	while (!bStopRequested)
	{
		UpdateAsyncIOMinimumPriority();
		if (!PlatformImpl.StartRequests(RequestQueue))
		{
			UpdateAsyncIOMinimumPriority();
			EventQueue.ServiceWait();
		}
	}
	return 0;
}

TSharedRef<IIoDispatcherFileBackend> CreateIoDispatcherFileBackend()
{
	return MakeShared<FFileIoStore>();
}