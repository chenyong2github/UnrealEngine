// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoStore.h"
#include "IO/IoDispatcherBackend.h"

struct FFileIoStoreCompressionContext;

struct FFileIoStoreContainerFilePartition
{
	uint64 FileHandle = 0;
	uint64 FileSize = 0;
	uint32 ContainerFileIndex = 0;
	FString FilePath;
	TUniquePtr<IMappedFileHandle> MappedFileHandle;
};

struct FFileIoStoreContainerFile
{
	uint64 PartitionSize = 0;
	uint64 CompressionBlockSize = 0;
	TArray<FName> CompressionMethods;
	TArray<FIoStoreTocCompressedBlockEntry> CompressionBlocks;
	FString FilePath;
	FGuid EncryptionKeyGuid;
	FAES::FAESKey EncryptionKey;
	EIoContainerFlags ContainerFlags;
	TArray<FSHAHash> BlockSignatureHashes;
	TArray<FFileIoStoreContainerFilePartition> Partitions;
	uint32 ContainerInstanceId = 0;

	void GetPartitionFileHandleAndOffset(uint64 TocOffset, uint64& OutFileHandle, uint64& OutOffset) const
	{
		int32 PartitionIndex = int32(TocOffset / PartitionSize);
		const FFileIoStoreContainerFilePartition& Partition = Partitions[PartitionIndex];
		OutFileHandle = Partition.FileHandle;
		OutOffset = TocOffset % PartitionSize;
	}
};

struct FFileIoStoreBuffer
{
	FFileIoStoreBuffer* Next = nullptr;
	uint8* Memory = nullptr;
};

struct FFileIoStoreBlockKey
{
	union
	{
		struct
		{
			uint32 FileIndex;
			uint32 BlockIndex;
		};
		uint64 Hash;
	};


	friend bool operator==(const FFileIoStoreBlockKey& A, const FFileIoStoreBlockKey& B)
	{
		return A.Hash == B.Hash;
	}

	friend uint32 GetTypeHash(const FFileIoStoreBlockKey& Key)
	{
		return GetTypeHash(Key.Hash);
	}
};

struct FFileIoStoreBlockScatter
{
	struct FFileIoStoreResolvedRequest* Request = nullptr;
	uint64 DstOffset = 0;
	uint64 SrcOffset = 0;
	uint64 Size = 0;
};

struct FFileIoStoreCompressedBlock
{
	FFileIoStoreCompressedBlock* Next = nullptr;
	FFileIoStoreBlockKey Key;
	FName CompressionMethod;
	uint64 RawOffset;
	uint32 UncompressedSize;
	uint32 CompressedSize;
	uint32 RawSize;
	uint32 RefCount = 0;
	uint32 UnfinishedRawBlocksCount = 0;
	TArray<struct FFileIoStoreReadRequest*, TInlineAllocator<2>> RawBlocks;
	TArray<FFileIoStoreBlockScatter, TInlineAllocator<2>> ScatterList;
	FFileIoStoreCompressionContext* CompressionContext = nullptr;
	uint8* CompressedDataBuffer = nullptr;
	FAES::FAESKey EncryptionKey;
	const FSHAHash* SignatureHash = nullptr;
	bool bFailed = false;
	bool bCancelled = false;
};

struct FFileIoStoreReadRequest
{
	enum EQueueStatus
	{
		QueueStatus_NotInQueue,
		QueueStatus_InQueue,
		QueueStatus_Started
	};

	FFileIoStoreReadRequest()
		: Sequence(NextSequence++)
		, CreationTime(FPlatformTime::Cycles64())
	{
	}
	FFileIoStoreReadRequest* Next = nullptr;
	FFileIoStoreReadRequest* Previous = nullptr;
	uint64 FileHandle = uint64(-1);
	uint64 Offset = uint64(-1);
	uint64 Size = uint64(-1);
	FFileIoStoreBlockKey Key;
	FFileIoStoreBuffer* Buffer = nullptr;
	uint32 RefCount = 0;
	uint32 BufferRefCount = 0;
	TArray<FFileIoStoreCompressedBlock*, TInlineAllocator<8>> CompressedBlocks;
	const uint32 Sequence;
	int32 Priority = 0;
	uint64 CreationTime;	// Potentially used to circuit break request ordering optimizations when outstanding requests have been delayed too long
	FFileIoStoreBlockScatter ImmediateScatter;
	bool bIsCacheable = false;
	bool bFailed = false;
	bool bCancelled = false;
	EQueueStatus QueueStatus = QueueStatus_NotInQueue;

#if DO_CHECK
	// For debug checks that we are in the correct owning list for our intrusive next/previous pointers
	uint32 ListCookie = 0;
#endif

private:
	static uint32 NextSequence;
};

// Iterator class for traversing and emptying a list FFileIoStoreReadRequestList at the same time
class FFileIoStoreReadRequestListStealingIterator
{
public:
	FFileIoStoreReadRequestListStealingIterator(const FFileIoStoreReadRequestListStealingIterator&) = delete;
	FFileIoStoreReadRequestListStealingIterator& operator=(const FFileIoStoreReadRequestListStealingIterator&) = delete;

	FFileIoStoreReadRequestListStealingIterator(FFileIoStoreReadRequestListStealingIterator&& Other)
	{
		Current = Other.Current;
		Next = Other.Next;

		Other.Current = Other.Next = nullptr;
	}

	FFileIoStoreReadRequestListStealingIterator& operator=(FFileIoStoreReadRequestListStealingIterator&& Other)
	{
		Current = Other.Current;
		Next = Other.Next;

		Other.Current = Other.Next = nullptr;

		return *this;
	}

	void operator++()
	{
		AdvanceTo(Next);
	}

	FFileIoStoreReadRequest* operator*()
	{
		return Current;
	}

	FFileIoStoreReadRequest* operator->()
	{
		return Current;
	}

	explicit operator bool() const
	{
		return Current != nullptr;
	}

private:
	friend class FFileIoStoreReadRequestList; // Only the list can construct us

	FFileIoStoreReadRequestListStealingIterator(FFileIoStoreReadRequest* InHead)
	{
#if DO_CHECK
		// None of these nodes are members of the list any more
		for (FFileIoStoreReadRequest* Cursor = InHead; Cursor; Cursor = Cursor->Next)
		{
			Cursor->ListCookie = 0;
		}
#endif

		AdvanceTo(InHead);
	}

	void AdvanceTo(FFileIoStoreReadRequest* NewCurrent)
	{
		Current = NewCurrent;
		if (Current)
		{
			// Copy off the next ptr and remove Current from its list so it can safely be added to another list
			Next = Current->Next;
			Current->Next = nullptr;
		}
		else
		{
			Next = nullptr;
		}
	}

	FFileIoStoreReadRequest* Current = nullptr;
	FFileIoStoreReadRequest* Next = nullptr;;
};

// Wrapper for doubly-linked intrusive list of FFileIoStoreReadRequest
#define CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP (DO_CHECK && 0)
class FFileIoStoreReadRequestList
{
public:
	FFileIoStoreReadRequestList()
#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
		: ListCookie(++NextListCookie)
#endif
	{
	}

	// Owns the Next/Previous pointers of the FFileIoStoreReadRequests it contains so it can't be copied
	FFileIoStoreReadRequestList(const FFileIoStoreReadRequestList& Other) = delete;
	FFileIoStoreReadRequestList& operator=(const FFileIoStoreReadRequestList& Other) = delete;

	FFileIoStoreReadRequestList(FFileIoStoreReadRequestList&& Other)
	{
		Head = Other.Head;
		Tail = Other.Tail;
		Other.Head = Other.Tail = nullptr;

#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
		for (FFileIoStoreReadRequest* Cursor = Head; Cursor; Cursor = Cursor->Next)
		{
			Cursor->ListCookie = ListCookie;
		}
#endif
	}

	FFileIoStoreReadRequestList& operator=(FFileIoStoreReadRequestList&& Other)
	{
		check(!Head && !Tail);

		Head = Other.Head;
		Tail = Other.Tail;
		Other.Head = Other.Tail = nullptr;

#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
		for (FFileIoStoreReadRequest* Cursor = Head; Cursor; Cursor = Cursor->Next)
		{
			Cursor->ListCookie = ListCookie;
		}
#endif

		return *this;
	}

#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
	~FFileIoStoreReadRequestList()
	{
		// If anything is left in this list it should still think we own it
		for (FFileIoStoreReadRequest* Cursor = Head; Cursor; Cursor = Cursor->Next)
		{
			check(Cursor->ListCookie == ListCookie);
		}
	}
#endif

	bool IsEmpty() const
	{
		return Head == nullptr;
	}

	// Steal the whole list for iteration and moving the contents to other lists
	FFileIoStoreReadRequestListStealingIterator Steal()
	{
#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
		for (FFileIoStoreReadRequest* Cursor = Head; Cursor; Cursor = Cursor->Next)
		{
			check(Cursor->ListCookie == ListCookie);
		}
#endif

		FFileIoStoreReadRequest* OldHead = Head;
		Clear();
		return FFileIoStoreReadRequestListStealingIterator(OldHead);
	}

	FFileIoStoreReadRequest* PeekHead() const
	{
		return Head;
	}

	void Add(FFileIoStoreReadRequest* Request)
	{
#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
		check(Request->ListCookie == 0);
		Request->ListCookie = ListCookie;
#endif

		if (Tail)
		{
			Tail->Next = Request;
			Request->Previous = Tail;
		}
		else
		{
			Head = Request;
			Request->Previous = nullptr;
		}
		Tail = Request;
		Request->Next = nullptr;
	}


	// Remove all FFileIoStoreReadRequests from List and add them to this list
	void AppendSteal(FFileIoStoreReadRequestList& List)
	{
		if (List.Head)
		{
			FFileIoStoreReadRequest *ListHead = List.Head, *ListTail = List.Tail;
			List.Clear();
			AppendSteal(ListHead, ListTail);
		}
	}

	void Remove(FFileIoStoreReadRequest* Request)
	{
#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
		check(Request);
		check(Request->ListCookie == ListCookie);
		Request->ListCookie = 0;
#endif

		if (Head == Request && Tail == Request)
		{
			check(Request->Next == nullptr);
			check(Request->Previous == nullptr);

			Head = Tail = nullptr;
		}
		else if (Head == Request)
		{
			check(Request->Previous == nullptr);

			Head = Request->Next;
			Head->Previous = nullptr;
			Request->Next = nullptr;
		}
		else if (Tail == Request)
		{
			check(Request->Next == nullptr);

			Tail = Request->Previous;
			Tail->Next = nullptr;
			Request->Previous = nullptr;
		}
		else
		{
			check(Request->Next != nullptr && Request->Previous != nullptr); // Neither head nor tail should mean both links are live

			Request->Next->Previous = Request->Previous;
			Request->Previous->Next = Request->Next;

			Request->Next = Request->Previous = nullptr;
		}
	}

	void Clear()
	{
#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
		for (FFileIoStoreReadRequest* Cursor = Head; Cursor; Cursor = Cursor->Next)
		{
			check(Cursor->ListCookie == ListCookie);
			Cursor->ListCookie = 0;
		}
#endif
		Head = Tail = nullptr;
	}

private:
	FFileIoStoreReadRequest* Head = nullptr;
	FFileIoStoreReadRequest* Tail = nullptr;

	uint32 ListCookie;
	static uint32 NextListCookie;
	
	void AppendSteal(FFileIoStoreReadRequest* ListHead, FFileIoStoreReadRequest* ListTail)
	{
		check(ListHead);
		check(ListTail);
		check(!ListTail->Next);
		check(!ListHead->Previous);
		check(ListTail == ListHead || ListTail->Previous != nullptr);
		check(ListTail == ListHead || ListHead->Next != nullptr);

#if CHECK_IO_STORE_READ_REQUEST_LIST_MEMBERSHIP
		for (FFileIoStoreReadRequest* Cursor = ListHead; Cursor; Cursor = Cursor->Next)
		{
			check(Cursor->ListCookie == 0);
			Cursor->ListCookie = ListCookie;
		}
#endif

		if (Tail)
		{
			Tail->Next = ListHead;
			ListHead->Previous = Tail;
		}
		else
		{
			Head = ListHead;
			ListHead->Previous = nullptr;
		}
		Tail = ListTail;
	}
};

class FFileIoStoreBufferAllocator
{
public:
	void Initialize(uint64 MemorySize, uint64 BufferSize, uint32 BufferAlignment);
	FFileIoStoreBuffer* AllocBuffer();
	void FreeBuffer(FFileIoStoreBuffer* Buffer);

private:
	uint8* BufferMemory = nullptr;
	FCriticalSection BuffersCritical;
	FFileIoStoreBuffer* FirstFreeBuffer = nullptr;
};

class FFileIoStoreBlockCache
{
public:
	FFileIoStoreBlockCache();
	~FFileIoStoreBlockCache();

	void Initialize(uint64 CacheMemorySize, uint64 ReadBufferSize);
	bool Read(FFileIoStoreReadRequest* Block);
	void Store(const FFileIoStoreReadRequest* Block);

private:
	struct FCachedBlock
	{
		FCachedBlock* LruPrev = nullptr;
		FCachedBlock* LruNext = nullptr;
		uint64 Key = 0;
		uint8* Buffer = nullptr;
		bool bLocked = false;
	};

	FCriticalSection CriticalSection;
	uint8* CacheMemory = nullptr;
	TMap<uint64, FCachedBlock*> CachedBlocks;
	FCachedBlock CacheLruHead;
	FCachedBlock CacheLruTail;
	uint64 ReadBufferSize = 0;
};

struct FFileIoStoreReadRequestSortKey
{
	uint64 Offset = 0;
	uint64 Handle = 0;
	int32 Priority = 0;

	FFileIoStoreReadRequestSortKey() {}
	FFileIoStoreReadRequestSortKey(FFileIoStoreReadRequest* Request)
		: Offset(Request->Offset), Handle(Request->FileHandle), Priority(Request->Priority)
	{
	}
};

// Stores FFileIoStoreReadRequest sorted by file handle & offset with a parallel list sorted by insertion order 
class FFileIoStoreOffsetSortedRequestQueue
{
public:
	FFileIoStoreOffsetSortedRequestQueue(int32 InPriority);
	FFileIoStoreOffsetSortedRequestQueue(const FFileIoStoreOffsetSortedRequestQueue&) = delete;
	FFileIoStoreOffsetSortedRequestQueue(FFileIoStoreOffsetSortedRequestQueue&&) = default;
	FFileIoStoreOffsetSortedRequestQueue& operator=(const FFileIoStoreOffsetSortedRequestQueue&) = delete;
	FFileIoStoreOffsetSortedRequestQueue& operator=(FFileIoStoreOffsetSortedRequestQueue&&) = default;

	int32 GetPriority() const { return Priority; }
	bool IsEmpty() const { return Requests.Num() == 0; }
	
	// Remove all requests from this container and return them, for switching to a different queue scheme
	TArray<FFileIoStoreReadRequest*> StealRequests();
	// Remove all requests whose priority has been changed to something other than the Priority of this queue
	TArray<FFileIoStoreReadRequest*> RemoveMisprioritizedRequests();

	FFileIoStoreReadRequest* Peek(FFileIoStoreReadRequestSortKey LastSortKey);
	FFileIoStoreReadRequest* Pop(FFileIoStoreReadRequestSortKey LastSortKey);
	void Push(FFileIoStoreReadRequest* Request);

private:
	int32 Priority;
	int32 PeekRequestIndex = INDEX_NONE;
	
	// Requests sorted by file handle & offset
	TArray<FFileIoStoreReadRequest*> Requests;
	
	// Requests sorted by insertion order 
	// We store this on the heap in case we get moved, FFileIoStoreReadRequest keeps pointers to FFileIoStoreReadRequestList for debugging
	FFileIoStoreReadRequestList RequestsBySequence;

	FFileIoStoreReadRequest* GetNextInternal(FFileIoStoreReadRequestSortKey LastSortKey, bool bPop);

	static FFileIoStoreReadRequestSortKey RequestSortProjection(FFileIoStoreReadRequest* Request) { return FFileIoStoreReadRequestSortKey(Request); }
	static bool RequestSortPredicate(const FFileIoStoreReadRequestSortKey& A, const FFileIoStoreReadRequestSortKey& B);
};

class FFileIoStoreRequestQueue
{
public:
	FFileIoStoreReadRequest* Peek();
	FFileIoStoreReadRequest* Pop();
	void Push(FFileIoStoreReadRequest& Request);	// Takes ownership of Request and rewrites its intrustive linked list pointers
	void Push(FFileIoStoreReadRequestList& Requests); // Consumes the request list and overwrites all intrustive linked list pointers
	void UpdateOrder();
	void Lock();
	void Unlock();
	void CancelRequestsWithFileHandle(uint64 FileHandle);

private:
	static bool QueueSortFunc(const FFileIoStoreReadRequest& A, const FFileIoStoreReadRequest& B)
	{
		if (A.Priority == B.Priority)
		{
			return A.Sequence < B.Sequence;
		}
		return A.Priority > B.Priority;
	}
	void UpdateSortRequestsByOffset(); // Check if we need to switch sorting schemes based on the CVar
	void PushToPriorityQueues(FFileIoStoreReadRequest* Request);
	static int32 QueuePriorityProjection(const FFileIoStoreOffsetSortedRequestQueue& A) { return A.GetPriority(); }

	bool bSortRequestsByOffset = false; // Cached value of CVar controlling whether we use Heap or SortedPriorityQueues
	
	// Heap sorted by request order
	TArray<FFileIoStoreReadRequest*> Heap;
	FCriticalSection CriticalSection;

	// Queues sorted by increasing priority
	TArray<FFileIoStoreOffsetSortedRequestQueue> SortedPriorityQueues; 
	// The last offset, file handle and priority we popped so that we can pop the closest forward read for the next IO operation
	FFileIoStoreReadRequestSortKey LastSortKey;

#if !UE_BUILD_SHIPPING
	TMap<int32, uint32> RequestPriorityCounts;
#endif
};

template <typename T, uint16 SlabSize = 4096>
class TIoDispatcherSingleThreadedSlabAllocator
{
public:
	TIoDispatcherSingleThreadedSlabAllocator()
	{
		CurrentSlab = new FSlab();
	}

	~TIoDispatcherSingleThreadedSlabAllocator()
	{
		check(CurrentSlab->Allocated == CurrentSlab->Freed);
		delete CurrentSlab;
	}

	template <typename... ArgsType>
	T* Construct(ArgsType&&... Args)
	{
		return new(Alloc()) T(Forward<ArgsType>(Args)...);
	}

	void Destroy(T* Ptr)
	{
		Ptr->~T();
		Free(Ptr);
	}

private:
	struct FSlab;

	struct FElement
	{
		TTypeCompatibleBytes<T> Data;
		FSlab* Slab = nullptr;
	};

	struct FSlab
	{
		uint16 Allocated = 0;
		uint16 Freed = 0;
		FElement Elements[SlabSize];
	};

	T* Alloc()
	{
		uint16 ElementIndex = CurrentSlab->Allocated++;
		check(ElementIndex < SlabSize);
		FElement* Element = CurrentSlab->Elements + ElementIndex;
		Element->Slab = CurrentSlab;
		if (CurrentSlab->Allocated == SlabSize)
		{
			//TRACE_CPUPROFILER_EVENT_SCOPE(AllocSlab);
			CurrentSlab = new FSlab();
		}
		return Element->Data.GetTypedPtr();
	}

	void Free(T* Ptr)
	{
		FElement* Element = reinterpret_cast<FElement*>(Ptr);
		FSlab* Slab = Element->Slab;
		if (++Slab->Freed == SlabSize)
		{
			//TRACE_CPUPROFILER_EVENT_SCOPE(FreeSlab);
			check(Slab->Freed == Slab->Allocated);
			delete Slab;
		}
	}

	FSlab* CurrentSlab = nullptr;
};

struct FFileIoStoreReadRequestLink
{
	FFileIoStoreReadRequestLink(FFileIoStoreReadRequest& InReadRequest)
		: ReadRequest(InReadRequest)
	{
	}

	FFileIoStoreReadRequestLink* Next = nullptr;
	FFileIoStoreReadRequest& ReadRequest;
};

class FFileIoStoreRequestAllocator
{
public:
	FFileIoStoreResolvedRequest* AllocResolvedRequest(
		FIoRequestImpl& InDispatcherRequest,
		const FFileIoStoreContainerFile& InContainerFile,
		uint64 InResolvedOffset,
		uint64 InResolvedSize)
	{
		return ResolvedRequestAllocator.Construct(
			InDispatcherRequest,
			InContainerFile,
			InResolvedOffset,
			InResolvedSize);
	}

	void Free(FFileIoStoreResolvedRequest* ResolvedRequest)
	{
		ResolvedRequestAllocator.Destroy(ResolvedRequest);
	}

	FFileIoStoreReadRequest* AllocReadRequest()
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(AllocReadRequest);
		return ReadRequestAllocator.Construct();
	}

	void Free(FFileIoStoreReadRequest* ReadRequest)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(FreeReadRequest);
		ReadRequestAllocator.Destroy(ReadRequest);
	}

	FFileIoStoreCompressedBlock* AllocCompressedBlock()
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(AllocCompressedBlock);
		return CompressedBlockAllocator.Construct();
	}

	void Free(FFileIoStoreCompressedBlock* CompressedBlock)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(FreeCompressedBlock);
		CompressedBlockAllocator.Destroy(CompressedBlock);
	}

	FFileIoStoreReadRequestLink* AllocRequestLink(FFileIoStoreReadRequest* ReadRequest)
	{
		check(ReadRequest);
		return RequestLinkAllocator.Construct(*ReadRequest);
	}

	void Free(FFileIoStoreReadRequestLink* RequestLink)
	{
		RequestLinkAllocator.Destroy(RequestLink);
	}

private:
	TIoDispatcherSingleThreadedSlabAllocator<FFileIoStoreResolvedRequest> ResolvedRequestAllocator;
	TIoDispatcherSingleThreadedSlabAllocator<FFileIoStoreReadRequest> ReadRequestAllocator;
	TIoDispatcherSingleThreadedSlabAllocator<FFileIoStoreCompressedBlock> CompressedBlockAllocator;
	TIoDispatcherSingleThreadedSlabAllocator<FFileIoStoreReadRequestLink> RequestLinkAllocator;
};

struct FFileIoStoreResolvedRequest
{
public:
	FFileIoStoreResolvedRequest(
		FIoRequestImpl& InDispatcherRequest,
		const FFileIoStoreContainerFile& InContainerFile,
		uint64 InResolvedOffset,
		uint64 InResolvedSize);

	const FFileIoStoreContainerFile& GetContainerFile() const
	{
		return ContainerFile;
	}

	uint64 GetResolvedOffset() const
	{
		return ResolvedOffset;
	}

	uint64 GetResolvedSize() const
	{
		return ResolvedSize;
	}

	int32 GetPriority() const
	{
		check(DispatcherRequest);
		return DispatcherRequest->Priority;
	}

	FIoBuffer& GetIoBuffer()
	{
		check(DispatcherRequest);
		return DispatcherRequest->IoBuffer;
	}

	void AddReadRequestLink(FFileIoStoreReadRequestLink* ReadRequestLink);

private:
	FIoRequestImpl* DispatcherRequest = nullptr;
	const FFileIoStoreContainerFile& ContainerFile;
	FFileIoStoreReadRequestLink* ReadRequestsHead = nullptr;
	FFileIoStoreReadRequestLink* ReadRequestsTail = nullptr;
	const uint64 ResolvedOffset;
	const uint64 ResolvedSize;
	uint32 UnfinishedReadsCount = 0;
	bool bFailed = false;
	bool bCancelled = false;

	friend class FFileIoStore;
	friend class FFileIoStoreRequestTracker;
};
