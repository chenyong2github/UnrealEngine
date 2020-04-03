// Copyright Epic Games, Inc. All Rights Reserved.

#include "FileCache/FileCache.h"
#include "Containers/BinaryHeap.h"
#include "Containers/Queue.h"
#include "Containers/LockFreeList.h"
#include "Containers/Ticker.h"
#include "Templates/TypeHash.h"
#include "Misc/ScopeLock.h"
#include "Async/AsyncFileHandle.h"
#include "Async/TaskGraphInterfaces.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/IConsoleManager.h"

DECLARE_STATS_GROUP(TEXT("Streaming File Cache"), STATGROUP_SFC, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Create Handle"), STAT_SFC_CreateHandle, STATGROUP_SFC);
DECLARE_CYCLE_STAT(TEXT("Read Data"), STAT_SFC_ReadData, STATGROUP_SFC);
DECLARE_CYCLE_STAT(TEXT("EvictAll"), STAT_SFC_EvictAll, STATGROUP_SFC);

// These below are pretty high throughput and probably should be removed once the system gets more mature
DECLARE_CYCLE_STAT(TEXT("Find Eviction Candidate"), STAT_SFC_FindEvictionCandidate, STATGROUP_SFC);

DECLARE_MEMORY_STAT(TEXT("Allocate Size"), STAT_SFC_AllocatedSize, STATGROUP_SFC);

DECLARE_MEMORY_STAT(TEXT("Locked Size"), STAT_SFC_LockedSize, STATGROUP_SFC);
DECLARE_MEMORY_STAT(TEXT("Preloaded Size"), STAT_SFC_PreloadedSize, STATGROUP_SFC);
DECLARE_MEMORY_STAT(TEXT("Fine Preloaded Size"), STAT_SFC_FinePreloadedSize, STATGROUP_SFC);

DEFINE_LOG_CATEGORY_STATIC(LogStreamingFileCache, Log, All);

static const int CacheSlotCapacity = 64 * 1024;
static const int CacheLineSize = 16 * 1024;
static const int PreloadBlockSize = CacheLineSize / 64;

static int32 GNumFileCacheBlocks = 256;
static FAutoConsoleVariableRef CVarNumFileCacheBlocks(
	TEXT("fc.NumFileCacheBlocks"),
	GNumFileCacheBlocks,
	TEXT("Number of blocks in the global file cache object\n"),
	ECVF_RenderThreadSafe
);

static int32 GLineReleaseFrameThreshold = 300;
static FAutoConsoleVariableRef CVarLineReleaseFrameThreshold(
	TEXT("fc.LineReleaseFrameThreshold"),
	GLineReleaseFrameThreshold,
	TEXT(""),
	ECVF_RenderThreadSafe
);


// 
// Strongly typed ids to avoid confusion in the code
// 
template <int SetBlockSize, typename Parameter> class StrongBlockIdentifier
{
	static const int InvalidHandle = 0xFFFFFFFF;

public:
	static const int32 BlockSize = SetBlockSize;

	StrongBlockIdentifier() : Id(InvalidHandle) {}
	explicit StrongBlockIdentifier(int32 SetId) : Id(SetId) {}

	inline bool IsValid() const { return Id != InvalidHandle; }
	inline int32 Get() const { checkSlow(IsValid()); return Id; }

	inline StrongBlockIdentifier& operator++() { Id = Id + 1; return *this; }
	inline StrongBlockIdentifier& operator--() { Id = Id - 1; return *this; }
	inline StrongBlockIdentifier operator++(int) { StrongBlockIdentifier Temp(*this); operator++(); return Temp; }
	inline StrongBlockIdentifier operator--(int) { StrongBlockIdentifier Temp(*this); operator--(); return Temp; }

	// Get the offset in the file to read this block
	inline int64 GetOffset() const { checkSlow(IsValid()); return (int64)Id * (int64)BlockSize; }
	inline int64 GetSize() const { checkSlow(IsValid()); return BlockSize; }

	// Get the number of bytes that need to be read for this block
	// takes into account incomplete blocks at the end of the file
	inline int64 GetSize(int64 FileSize) const { checkSlow(IsValid()); return FMath::Min((int64)BlockSize, FileSize - GetOffset()); }

	friend inline uint32 GetTypeHash(const StrongBlockIdentifier<SetBlockSize, Parameter>& Info) { return GetTypeHash(Info.Id); }

	inline bool operator==(const StrongBlockIdentifier<SetBlockSize, Parameter>&Other) const { return Id == Other.Id; }
	inline bool operator!=(const StrongBlockIdentifier<SetBlockSize, Parameter>&Other) const { return Id != Other.Id; }

private:
	int32 Id;
};

using CacheLineID = StrongBlockIdentifier<CacheLineSize, struct CacheLineStrongType>; // Unique per file handle
using CacheSlotID = StrongBlockIdentifier<CacheLineSize, struct CacheSlotStrongType>; // Unique per cache

class FFileCacheHandle;

// Some terminology:
// A line: A fixed size block of a file on disc that can be brought into the cache
// Slot: A fixed size piece of memory that can contain the data for a certain line in memory

////////////////

class FFileCache
{
public:
	enum ESlotListType
	{
		SLOTLIST_Free,
		SLOTLIST_UnlockedAllocated,
		SLOTLIST_Num,
	};

	struct FSlotInfo
	{
		uint64 PreloadMask = 0u;
		FFileCacheHandle* Handle = nullptr;
		CacheLineID LineID;
		int32 NextSlotIndex = 0;
		int32 PrevSlotIndex = 0;
		uint32 LastUsedFrameNumber = 0u;
		int16 LockCount = 0;
	};

	explicit FFileCache(int32 NumSlots);
	~FFileCache();

	bool OnTick(float DeltaTime);

	uint8* GetSlotMemory(CacheSlotID SlotID);

	CacheSlotID AcquireAndLockSlot(FFileCacheHandle* InHandle, CacheLineID InLineID);
	bool IsSlotLocked(CacheSlotID InSlotID) const;
	void LockSlot(CacheSlotID InSlotID);
	void UnlockSlot(CacheSlotID InSlotID);
	void MarkSlotPreloadedRegion(CacheSlotID InSlotID, int64 InOffset, int64 InSize);
	void ClearSlotPreloadedRegion(CacheSlotID InSlotID, int64 InOffset, int64 InSize);

	void ReleaseMemory(int32 NumSlotsToRelease = 1);

	// if InFile is null, will evict all slots
	bool EvictAll(FFileCacheHandle* InFile = nullptr);

	void FlushCompletedRequests();

	void EvictFileCacheFromConsole()
	{
		EvictAll();
	}

	void PushCompletedRequest(IAsyncReadRequest* Request)
	{
		check(Request);
		CompletedRequests.Push(Request);
		if (((uint32)CompletedRequestsCounter.Increment() % 32u) == 0u)
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady([this]()
			{
				while (IAsyncReadRequest* CompletedRequest = this->CompletedRequests.Pop())
				{
					// Requests are added to this list from the completed callback, but the final completion flag is not set until after callback is finished
					// This means that there's a narrow window where the request is not technically considered to be complete yet
					CompletedRequest->WaitCompletion();
					delete CompletedRequest;
				}
			}, TStatId());
		}
	}

	inline void UnlinkSlot(int32 SlotIndex)
	{
		check(SlotIndex >= SLOTLIST_Num);
		FSlotInfo& Info = SlotInfo[SlotIndex];
		SlotInfo[Info.PrevSlotIndex].NextSlotIndex = Info.NextSlotIndex;
		SlotInfo[Info.NextSlotIndex].PrevSlotIndex = Info.PrevSlotIndex;
		Info.NextSlotIndex = Info.PrevSlotIndex = SlotIndex;
	}

	inline void LinkSlotTail(ESlotListType List, int32 SlotIndex)
	{
		check(SlotIndex >= SLOTLIST_Num);
		FSlotInfo& HeadInfo = SlotInfo[List];
		FSlotInfo& Info = SlotInfo[SlotIndex];
		check(Info.NextSlotIndex == SlotIndex);
		check(Info.PrevSlotIndex == SlotIndex);

		Info.NextSlotIndex = List;
		Info.PrevSlotIndex = HeadInfo.PrevSlotIndex;
		SlotInfo[HeadInfo.PrevSlotIndex].NextSlotIndex = SlotIndex;
		HeadInfo.PrevSlotIndex = SlotIndex;
	}

	inline void LinkSlotHead(ESlotListType List, int32 SlotIndex)
	{
		check(SlotIndex >= SLOTLIST_Num);
		FSlotInfo& HeadInfo = SlotInfo[List];
		FSlotInfo& Info = SlotInfo[SlotIndex];
		check(Info.NextSlotIndex == SlotIndex);
		check(Info.PrevSlotIndex == SlotIndex);

		Info.NextSlotIndex = HeadInfo.NextSlotIndex;
		Info.PrevSlotIndex = List;
		SlotInfo[HeadInfo.NextSlotIndex].PrevSlotIndex = SlotIndex;
		HeadInfo.NextSlotIndex = SlotIndex;
	}

	FCriticalSection CriticalSection;

	FAutoConsoleCommand EvictFileCacheCommand;

	FDelegateHandle TickHandle;

	TLockFreePointerListUnordered<IAsyncReadRequest, PLATFORM_CACHE_LINE_SIZE> CompletedRequests;
	FThreadSafeCounter CompletedRequestsCounter;

	// allocated with an extra dummy entry at index0 for linked list head
	TArray<FSlotInfo> SlotInfo;
	uint8* SlotMemory[CacheSlotCapacity];
	int32 SizeInBytes;
	int32 NumSlots;
	int32 NumAllocatedSlots;
};

static FFileCache &GetCache()
{
	static FFileCache TheCache(GNumFileCacheBlocks);
	return TheCache;
}

///////////////

class FFileCacheHandle : public IFileCacheHandle
{
public:

	FFileCacheHandle(IAsyncReadFileHandle* InHandle);
	virtual ~FFileCacheHandle() override;

	//
	// Block helper functions. These are just convenience around basic math.
	// 

 // templated uses of this may end up converting int64 to int32, but it's up to the user of the template to know
PRAGMA_DISABLE_UNSAFE_TYPECAST_WARNINGS
	/*
	 * Get the block id that contains the specified offset
	 */
	template<typename BlockIDType> inline BlockIDType GetBlock(int64 Offset)
	{
		return BlockIDType(FMath::DivideAndRoundDown(Offset, (int64)BlockIDType::BlockSize));
	}
PRAGMA_ENABLE_UNSAFE_TYPECAST_WARNINGS

	template<typename BlockIDType> inline int32 GetNumBlocks(int64 Offset, int64 Size)
	{
		BlockIDType FirstBlock = GetBlock<BlockIDType>(Offset);
		BlockIDType LastBlock = GetBlock<BlockIDType>(Offset + Size - 1);// Block containing the last byte
		return (LastBlock.Get() - FirstBlock.Get()) + 1;
	}

	// Returns the offset within the first block covering the byte range to read from
	template<typename BlockIDType> inline int64 GetBlockOffset(int64 Offset)
	{
		return Offset - FMath::DivideAndRoundDown(Offset, (int64)BlockIDType::BlockSize) *  BlockIDType::BlockSize;
	}

	// Returns the size within the first cache line covering the byte range to read
	template<typename BlockIDType> inline int64 GetBlockSize(int64 Offset, int64 Size)
	{
		int64 OffsetInSlot = GetBlockOffset<BlockIDType>(Offset);
		return FMath::Min((int64)(BlockIDType::BlockSize - OffsetInSlot), Size - Offset);
	}

	virtual IMemoryReadStreamRef ReadData(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority) override;
	virtual FGraphEventRef PreloadData(const FFileCachePreloadEntry* PreloadEntries, int32 NumEntries, int64 InOffset, EAsyncIOPriorityAndFlags Priority) override;
	virtual void ReleasePreloadedData(const FFileCachePreloadEntry* PreloadEntries, int32 NumEntries, int64 InOffset) override;

	IMemoryReadStreamRef ReadDataUncached(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority);

	void WaitAll() override;

	void Evict(CacheLineID Line);

private:
	struct FPendingRequest
	{
		FGraphEventRef Event;
	};

	void CheckForSizeRequestComplete();

	CacheSlotID AcquireSlotAndReadLine(FFileCache& Cache, CacheLineID LineID, EAsyncIOPriorityAndFlags Priority);
	void ReadLine(FFileCache& Cache, CacheSlotID SlotID, CacheLineID LineID, EAsyncIOPriorityAndFlags Priority, const FGraphEventRef& CompletionEvent);

	TArray<CacheSlotID> LineToSlot;
	TArray<FPendingRequest> LineToRequest;

	int64 NumSlots;
	int64 FileSize;
	IAsyncReadFileHandle* InnerHandle;
	FGraphEventRef SizeRequestEvent;
};

///////////////

FFileCache::FFileCache(int32 InNumSlots)
	: EvictFileCacheCommand(TEXT("r.VT.EvictFileCache"), TEXT("Evict all the file caches in the VT system."),
		FConsoleCommandDelegate::CreateRaw(this, &FFileCache::EvictFileCacheFromConsole))
	, SizeInBytes(InNumSlots * CacheSlotID::BlockSize)
	, NumSlots(InNumSlots)
	, NumAllocatedSlots(0)
{
	FMemory::Memzero(SlotMemory);

	SlotInfo.AddDefaulted(InNumSlots + SLOTLIST_Num);
	for (int32 i = 0; i < SLOTLIST_Num; ++i)
	{
		FSlotInfo& Info = SlotInfo[i];
		Info.NextSlotIndex = i;
		Info.PrevSlotIndex = i;
	}

	// All slots begin in free list
	SlotInfo[SLOTLIST_Free].PrevSlotIndex = InNumSlots;
	SlotInfo[InNumSlots].NextSlotIndex = SLOTLIST_Free;

	for (int32 i = SLOTLIST_Num; i < SlotInfo.Num(); ++i)
	{
		FSlotInfo& Info = SlotInfo[i];
		Info.NextSlotIndex = i + 1;
		Info.PrevSlotIndex = i - 1;
	}

	TickHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FFileCache::OnTick), 0.1f);
}

FFileCache::~FFileCache()
{
	SET_MEMORY_STAT(STAT_SFC_AllocatedSize, 0);
	for (int32 i = 0; i < CacheSlotCapacity; ++i)
	{
		if (SlotMemory[i])
		{
			FMemory::Free(SlotMemory[i]);
		}
	}

	FTicker::GetCoreTicker().RemoveTicker(TickHandle);
}

bool FFileCache::OnTick(float DeltaTime)
{
	FScopeLock Lock(&CriticalSection);
	ReleaseMemory(30);
	return true;
}

CacheSlotID FFileCache::AcquireAndLockSlot(FFileCacheHandle* InHandle, CacheLineID InLineID)
{
	int32 SlotIndex = SlotInfo[SLOTLIST_UnlockedAllocated].NextSlotIndex;
	if (SlotIndex == SLOTLIST_UnlockedAllocated)
	{
		SlotIndex = SlotInfo[SLOTLIST_Free].NextSlotIndex;
		if (SlotIndex == SLOTLIST_Free)
		{
			SlotIndex = SlotInfo.AddDefaulted();
			check(SlotIndex < CacheSlotCapacity);
			SlotInfo[SlotIndex].NextSlotIndex = SlotInfo[SlotIndex].PrevSlotIndex = SlotIndex;
		}
		else
		{
			UnlinkSlot(SlotIndex);
		}

		FSlotInfo& Info = SlotInfo[SlotIndex];
		check(!SlotMemory[SlotIndex]);
		check(!Info.Handle);

		INC_MEMORY_STAT_BY(STAT_SFC_AllocatedSize, CacheSlotID::BlockSize);
		SlotMemory[SlotIndex] = (uint8*)FMemory::Malloc(CacheSlotID::BlockSize);
		++NumAllocatedSlots;
	}
	else
	{
		UnlinkSlot(SlotIndex);
		FSlotInfo& Info = SlotInfo[SlotIndex];
		if (Info.Handle)
		{
			Info.Handle->Evict(Info.LineID);
			Info.Handle = nullptr;
		}
	}

	INC_MEMORY_STAT_BY(STAT_SFC_LockedSize, CacheSlotID::BlockSize);
	FSlotInfo& Info = SlotInfo[SlotIndex];
	check(Info.LockCount == 0); // slot should not be in free list if it's locked
	check(Info.PreloadMask == 0u);
	Info.LockCount = 1;
	Info.Handle = InHandle;
	Info.LineID = InLineID;

	check(SlotMemory[SlotIndex]);
	return CacheSlotID(SlotIndex - SLOTLIST_Num);
}

uint8* FFileCache::GetSlotMemory(CacheSlotID InSlotID)

{
	const int32 SlotIndex = InSlotID.Get() + SLOTLIST_Num;
	// May be called with no slot, SlotInfo array not safe to access here
	uint8* Memory = SlotMemory[SlotIndex];
	check(Memory);
	return Memory;
}

bool FFileCache::IsSlotLocked(CacheSlotID InSlotID) const
{
	const int32 SlotIndex = InSlotID.Get() + SLOTLIST_Num;
	const FSlotInfo& Info = SlotInfo[SlotIndex];
	return Info.LockCount > 0;
}

void FFileCache::LockSlot(CacheSlotID InSlotID)
{
	const int32 SlotIndex = InSlotID.Get() + SLOTLIST_Num;
	FSlotInfo& Info = SlotInfo[SlotIndex];
	check(SlotMemory[SlotIndex]);
	const int16 PrevLockCount = Info.LockCount;
	check(PrevLockCount < 0x7fff);
	if (PrevLockCount == 0)
	{
		INC_MEMORY_STAT_BY(STAT_SFC_LockedSize, CacheSlotID::BlockSize);
		if (Info.PreloadMask == 0u)
		{
			UnlinkSlot(SlotIndex);
		}
	}
	Info.LockCount = PrevLockCount + 1;
}

void FFileCache::UnlockSlot(CacheSlotID InSlotID)
{
	const int32 SlotIndex = InSlotID.Get() + SLOTLIST_Num;
	FSlotInfo& Info = SlotInfo[SlotIndex];
	const int16 PrevLockCount = Info.LockCount;
	check(SlotMemory[SlotIndex]);
	check(PrevLockCount > 0);

	if (PrevLockCount == 1)
	{
		DEC_MEMORY_STAT_BY(STAT_SFC_LockedSize, CacheSlotID::BlockSize);
		Info.LastUsedFrameNumber = GFrameNumber;

		// move slot back to the free list when it's unlocked
		if (Info.PreloadMask == 0u)
		{
			LinkSlotTail(SLOTLIST_UnlockedAllocated, SlotIndex);
		}
	}
	Info.LockCount = PrevLockCount - 1;
}

static uint64 MakePreloladMask(int64 InOffset, int64 InSize)
{
	checkSlow(InOffset >= 0 && InOffset < CacheLineSize);
	checkSlow(InSize > 0 && InSize <= CacheLineSize);
	const uint32 StartBlock = FMath::DivideAndRoundDown<uint32>((uint32)InOffset, PreloadBlockSize);
	const uint32 EndBlock = FMath::DivideAndRoundUp<uint32>((uint32)(InOffset + InSize), PreloadBlockSize);
	const uint32 NumBlocks = EndBlock - StartBlock;
	checkf(NumBlocks > 0u && NumBlocks <= 64u, TEXT("Invalid NumBlocks %d"), NumBlocks);
	if (NumBlocks < 64u)
	{
		const uint64 Mask = ((uint64)1u << NumBlocks) - 1u;
		return Mask << StartBlock;
	}
	check(StartBlock == 0u);
	return ~(uint64)0u;
}

void FFileCache::MarkSlotPreloadedRegion(CacheSlotID InSlotID, int64 InOffset, int64 InSize)
{
	const int32 SlotIndex = InSlotID.Get() + SLOTLIST_Num;
	FSlotInfo& Info = SlotInfo[SlotIndex];

	if (Info.PreloadMask == 0u)
	{
		INC_MEMORY_STAT_BY(STAT_SFC_PreloadedSize, CacheSlotID::BlockSize);
		if (Info.LockCount == 0)
		{
			UnlinkSlot(SlotIndex);
		}
	}

	const uint64 Mask = MakePreloladMask(InOffset, InSize);
	const uint64 BitsSet = Mask & ~Info.PreloadMask;
	INC_MEMORY_STAT_BY(STAT_SFC_FinePreloadedSize, FMath::CountBits(BitsSet) * PreloadBlockSize);
	Info.PreloadMask |= Mask;
}

void FFileCache::ClearSlotPreloadedRegion(CacheSlotID InSlotID, int64 InOffset, int64 InSize)
{
	const int32 SlotIndex = InSlotID.Get() + SLOTLIST_Num;
	FSlotInfo& Info = SlotInfo[SlotIndex];

	if (Info.PreloadMask != 0u)
	{
		const uint64 Mask = MakePreloladMask(InOffset, InSize);
		const uint64 BitsClear = Mask & Info.PreloadMask;
		DEC_MEMORY_STAT_BY(STAT_SFC_FinePreloadedSize, FMath::CountBits(BitsClear) * PreloadBlockSize);
		Info.PreloadMask &= ~Mask;
		if (Info.PreloadMask == 0u)
		{
			DEC_MEMORY_STAT_BY(STAT_SFC_PreloadedSize, CacheSlotID::BlockSize);
			if (Info.LockCount == 0)
			{
				LinkSlotTail(SLOTLIST_UnlockedAllocated, SlotIndex);
				Info.LastUsedFrameNumber = 0u;
			}
		}
	}
}

void FFileCache::ReleaseMemory(int32 InNumSlotsToRelease)
{
	const uint32 CurrentFrameNumber = GFrameNumber;
	int32 NumSlotsToRelease = FMath::Min(InNumSlotsToRelease, NumAllocatedSlots - NumSlots);
	while (NumSlotsToRelease > 0)
	{
		const int32 SlotIndex = SlotInfo[SLOTLIST_UnlockedAllocated].NextSlotIndex;
		FSlotInfo& Info = SlotInfo[SlotIndex];
		if (SlotIndex == SLOTLIST_UnlockedAllocated || Info.LastUsedFrameNumber + GLineReleaseFrameThreshold >= CurrentFrameNumber)
		{
			break;
		}

		check(SlotMemory[SlotIndex]);
		check(Info.LockCount == 0); // slot should not be in free list if it's locked
		check(Info.PreloadMask == 0u);
		if (Info.Handle)
		{
			Info.Handle->Evict(Info.LineID);
			Info.Handle = nullptr;
		}

		DEC_MEMORY_STAT_BY(STAT_SFC_AllocatedSize, CacheSlotID::BlockSize);
		FMemory::Free(SlotMemory[SlotIndex]);
		SlotMemory[SlotIndex] = nullptr;
		Info.LineID = CacheLineID();
		--NumAllocatedSlots;
		--NumSlotsToRelease;

		UnlinkSlot(SlotIndex);
		LinkSlotTail(SLOTLIST_Free, SlotIndex);
	}
}

bool FFileCache::EvictAll(FFileCacheHandle* InFile)
{
	SCOPE_CYCLE_COUNTER(STAT_SFC_EvictAll);

	FScopeLock Lock(&CriticalSection);

	bool bAllOK = true;
	for (int SlotIndex = 1; SlotIndex < SlotInfo.Num(); ++SlotIndex)
	{
		FSlotInfo& Info = SlotInfo[SlotIndex];
		if (Info.Handle && ((Info.Handle == InFile) || InFile == nullptr))
		{
			if (Info.LockCount == 0)
			{
				Info.Handle->Evict(Info.LineID);
				Info.Handle = nullptr;
				Info.LineID = CacheLineID();
				if (Info.PreloadMask > 0u)
				{
					DEC_MEMORY_STAT_BY(STAT_SFC_PreloadedSize, CacheSlotID::BlockSize);
					DEC_MEMORY_STAT_BY(STAT_SFC_FinePreloadedSize, FMath::CountBits(Info.PreloadMask) * PreloadBlockSize);
					Info.PreloadMask = 0u;
				}
	
				// move evicted slots to the front of list so they'll be re-used more quickly
				UnlinkSlot(SlotIndex);
				LinkSlotHead(SLOTLIST_UnlockedAllocated, SlotIndex);
			}
			else
			{
				bAllOK = false;
			}
		}
	}

	return bAllOK;
}

void FFileCache::FlushCompletedRequests()
{
	while (IAsyncReadRequest* Request = CompletedRequests.Pop())
	{
		Request->WaitCompletion();
		delete Request;
	}
}

FFileCacheHandle::~FFileCacheHandle()
{
	if (SizeRequestEvent)
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(SizeRequestEvent);
		SizeRequestEvent.SafeRelease();
	}

	if (InnerHandle)
	{
		WaitAll();

		const bool result = GetCache().EvictAll(this);
		check(result);

		// Need to ensure any request created by our async handle is destroyed before destroying the handle
		GetCache().FlushCompletedRequests();

		delete InnerHandle;
	}
}

FFileCacheHandle::FFileCacheHandle(IAsyncReadFileHandle* InHandle)
	: NumSlots(0)
	, FileSize(-1)
	, InnerHandle(InHandle)
{
	FGraphEventRef CompletionEvent = FGraphEvent::CreateGraphEvent();
	FAsyncFileCallBack SizeCallbackFunction = [this, CompletionEvent](bool bWasCancelled, IAsyncReadRequest* Request)
	{
		this->FileSize = Request->GetSizeResults();
		check(this->FileSize > 0);

		TArray<FBaseGraphTask*> NewTasks;
		CompletionEvent->DispatchSubsequents(NewTasks);
		GetCache().PushCompletedRequest(Request);
	};

	SizeRequestEvent = CompletionEvent;
	IAsyncReadRequest* SizeRequest = InHandle->SizeRequest(&SizeCallbackFunction);
	check(SizeRequest);
}

class FMemoryReadStreamAsyncRequest : public IMemoryReadStream
{
public:
	FMemoryReadStreamAsyncRequest(IAsyncReadRequest* InRequest, int64 InSize)
		: Request(InRequest), Size(InSize)
	{
	}

	virtual const void* Read(int64& OutSize, int64 InOffset, int64 InSize) override
	{
		const uint8* ResultData = Request->GetReadResults();

		check(InOffset < Size);
		OutSize = FMath::Min(InSize, Size - InOffset);
		return ResultData + InOffset;
	}

	virtual int64 GetSize() override
	{
		return Size;
	}

	virtual ~FMemoryReadStreamAsyncRequest()
	{
		Request->WaitCompletion();
		delete Request;
	}

	IAsyncReadRequest* Request;
	int64 Size;
};

IMemoryReadStreamRef FFileCacheHandle::ReadDataUncached(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority)
{
	FGraphEventRef CompletionEvent = FGraphEvent::CreateGraphEvent();

	FAsyncFileCallBack ReadCallbackFunction = [CompletionEvent](bool bWasCancelled, IAsyncReadRequest* Request)
	{
		TArray<FBaseGraphTask*> NewTasks;
		CompletionEvent->DispatchSubsequents(NewTasks);
	};

	OutCompletionEvents.Add(CompletionEvent);
	IAsyncReadRequest* AsyncRequest = InnerHandle->ReadRequest(Offset, BytesToRead, Priority, &ReadCallbackFunction);
	return new FMemoryReadStreamAsyncRequest(AsyncRequest, BytesToRead);
}

class FMemoryReadStreamCache : public IMemoryReadStream
{
public:
	virtual const void* Read(int64& OutSize, int64 InOffset, int64 InSize) override
	{
		FFileCache& Cache = GetCache();

		const int64 Offset = InitialSlotOffset + InOffset;
		const int32 SlotIndex = (int32)FMath::DivideAndRoundDown(Offset, (int64)CacheSlotID::BlockSize);
		const int32 OffsetInSlot = (int32)(Offset - SlotIndex * CacheSlotID::BlockSize);
		checkSlow(SlotIndex >= 0 && SlotIndex < NumCacheSlots);
		const uint8* SlotMemory = Cache.GetSlotMemory(CacheSlots[SlotIndex]);

		OutSize = FMath::Min(InSize, (int64)CacheSlotID::BlockSize - OffsetInSlot);
		return SlotMemory + OffsetInSlot;
	}

	virtual int64 GetSize() override
	{
		return Size;
	}

	virtual ~FMemoryReadStreamCache()
	{
		FFileCache& Cache = GetCache();
		FScopeLock CacheLock(&Cache.CriticalSection);

		int64 SizeRemaining = Size;
		int64 OffsetInSlot = InitialSlotOffset;
		int64 SizeInSlot = FMath::Min<int64>(Size, CacheLineSize - OffsetInSlot);
		for (int i = 0; i < NumCacheSlots; ++i)
		{
			const CacheSlotID& SlotID = CacheSlots[i];
			check(SlotID.IsValid());
			Cache.ClearSlotPreloadedRegion(SlotID, OffsetInSlot, SizeInSlot);
			Cache.UnlockSlot(SlotID);

			SizeRemaining -= SizeInSlot;
			check(SizeRemaining >= 0);
			OffsetInSlot = 0;
			SizeInSlot = FMath::Min<int64>(SizeRemaining, CacheLineSize);
		}

		check(SizeRemaining == 0);
	}

	void operator delete(void* InMem)
	{
		FMemory::Free(InMem);
	}

	int64 InitialSlotOffset;
	int64 Size;
	int32 NumCacheSlots;
	CacheSlotID CacheSlots[1]; // variable length, sized by NumCacheSlots
};

void FFileCacheHandle::CheckForSizeRequestComplete()
{
	if (SizeRequestEvent && SizeRequestEvent->IsComplete())
	{
		SizeRequestEvent.SafeRelease();

		check(FileSize > 0);

		// Make sure we haven't lazily allocated more slots than are in the file, then allocate the final number of slots
		const int64 TotalNumSlots = FMath::DivideAndRoundUp(FileSize, (int64)CacheLineSize);
		check(NumSlots <= TotalNumSlots);
		NumSlots = TotalNumSlots;
		// TArray is max signed int
		check(TotalNumSlots < MAX_int32);
		LineToSlot.SetNum((int32)TotalNumSlots, false);
		LineToRequest.SetNum((int32)TotalNumSlots, false);
	}
}

void FFileCacheHandle::ReadLine(FFileCache& Cache, CacheSlotID SlotID, CacheLineID LineID, EAsyncIOPriorityAndFlags Priority, const FGraphEventRef& CompletionEvent)
{
	check(FileSize >= 0);
	const int64 LineSizeInFile = LineID.GetSize(FileSize);
	const int64 LineOffsetInFile = LineID.GetOffset();
	uint8* CacheSlotMemory = Cache.GetSlotMemory(SlotID);

	// callback triggered when async read operation is complete, used to signal task graph event
	FAsyncFileCallBack ReadCallbackFunction = [CompletionEvent](bool bWasCancelled, IAsyncReadRequest* Request)
	{
		TArray<FBaseGraphTask*> NewTasks;
		CompletionEvent->DispatchSubsequents(NewTasks);
		GetCache().PushCompletedRequest(Request);
	};

	InnerHandle->ReadRequest(LineOffsetInFile, LineSizeInFile, Priority, &ReadCallbackFunction, CacheSlotMemory);
}

CacheSlotID FFileCacheHandle::AcquireSlotAndReadLine(FFileCache& Cache, CacheLineID LineID, EAsyncIOPriorityAndFlags Priority)
{
	SCOPED_LOADTIMER(FFileCacheHandle_AcquireSlotAndReadLine);

	// no valid slot for this line, grab a new slot from cache and start a read request
	CacheSlotID SlotID = Cache.AcquireAndLockSlot(this, LineID);

	FPendingRequest& PendingRequest = LineToRequest[LineID.Get()];
	if (PendingRequest.Event)
	{
		// previous async request/event (if any) should be completed, if this is back in the free list
		check(PendingRequest.Event->IsComplete());
	}

	FGraphEventRef CompletionEvent = FGraphEvent::CreateGraphEvent();
	PendingRequest.Event = CompletionEvent;
	if (FileSize >= 0)
	{
		// If FileSize >= 0, that means the async file size request has completed, we can perform the read immediately
		ReadLine(Cache, SlotID, LineID, Priority, CompletionEvent);
	}
	else
	{
		// Here we don't know the FileSize yet, so we schedule an async task to kick the read once the size request has completed
		// It's important to know the size of the file before performing the read, to ensure that we don't read past end-of-file
		FFunctionGraphTask::CreateAndDispatchWhenReady([this, SlotID, LineID, Priority, CompletionEvent]
		{
			this->ReadLine(GetCache(), SlotID, LineID, Priority, CompletionEvent);
		},
		TStatId(), SizeRequestEvent);
	}

	return SlotID;
}

IMemoryReadStreamRef FFileCacheHandle::ReadData(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority)
{
	SCOPE_CYCLE_COUNTER(STAT_SFC_ReadData);
	SCOPED_LOADTIMER(FFileCacheHandle_ReadData);

	const CacheLineID StartLine = GetBlock<CacheLineID>(Offset);
	const CacheLineID EndLine = GetBlock<CacheLineID>(Offset + BytesToRead - 1);

	CheckForSizeRequestComplete();

	int32 NumSlotsNeeded = EndLine.Get() + 1 - StartLine.Get();

	FFileCache& Cache = GetCache();

	FScopeLock CacheLock(&Cache.CriticalSection);
	/*if (NumSlotsNeeded > Cache.NumFreeSlots)
	{
		// not enough free slots in the cache to service this request
		CacheLock.Unlock();
		if((Priority & AIOP_PRIORITY_MASK) >= AIOP_CriticalPath)
		{
			// Critical request can't fail, just read the requested data directly
			return ReadDataUncached(OutCompletionEvents, Offset, BytesToRead, Priority);
		}
		return nullptr;
	}*/

	if (EndLine.Get() >= NumSlots)
	{
		// If we're still waiting on SizeRequest, may need to lazily allocate some slots to service this request
		// If this happens after SizeRequest has completed, that means something must have gone wrong
		check(SizeRequestEvent);
		NumSlots = EndLine.Get() + 1;
		// TArray is max signed int
		check(NumSlots < MAX_int32);
		LineToSlot.SetNum((int32)NumSlots, false);
		LineToRequest.SetNum((int32)NumSlots, false);
	}

	const int32 NumCacheSlots = EndLine.Get() + 1 - StartLine.Get();
	check(NumCacheSlots > 0);
	const uint32 AllocSize = sizeof(FMemoryReadStreamCache) + sizeof(CacheSlotID) * (NumCacheSlots - 1);
	void* ResultMemory = FMemory::Malloc(AllocSize, alignof(FMemoryReadStreamCache));
	FMemoryReadStreamCache* Result = new(ResultMemory) FMemoryReadStreamCache();
	Result->NumCacheSlots = NumCacheSlots;
	Result->InitialSlotOffset = GetBlockOffset<CacheLineID>(Offset);
	Result->Size = BytesToRead;

	bool bHasPendingSlot = false;
	for (CacheLineID LineID = StartLine; LineID.Get() <= EndLine.Get(); ++LineID)
	{
		CacheSlotID& SlotID = LineToSlot[LineID.Get()];
		if (!SlotID.IsValid())
		{
			// no valid slot for this line, grab a new slot from cache and start a read request
			SlotID = AcquireSlotAndReadLine(Cache, LineID, Priority);
		}
		else
		{
			Cache.LockSlot(SlotID);
		}

		check(SlotID.IsValid());
		Result->CacheSlots[LineID.Get() - StartLine.Get()] = SlotID;

		FPendingRequest& PendingRequest = LineToRequest[LineID.Get()];
		if (PendingRequest.Event && !PendingRequest.Event->IsComplete())
		{
			// this line has a pending async request to read data
			// will need to wait for this request to complete before data is valid
			OutCompletionEvents.Add(PendingRequest.Event);
			bHasPendingSlot = true;
		}
		else
		{
			PendingRequest.Event.SafeRelease();
		}
	}

	return Result;
}

struct FFileCachePreloadTask
{
	explicit FFileCachePreloadTask(TArray<CacheSlotID>&& InLockedSlots) : LockedSlots(InLockedSlots) {}
	TArray<CacheSlotID> LockedSlots;

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FFileCache& Cache = GetCache();
		FScopeLock CacheLock(&Cache.CriticalSection);
		for (int i = 0; i < LockedSlots.Num(); ++i)
		{
			const CacheSlotID& SlotID = LockedSlots[i];
			check(SlotID.IsValid());
			Cache.UnlockSlot(SlotID);
		}
	}

	FORCEINLINE static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }
	FORCEINLINE ENamedThreads::Type GetDesiredThread() { return ENamedThreads::AnyNormalThreadNormalTask; }
	FORCEINLINE TStatId GetStatId() const { return TStatId(); }
};

FGraphEventRef FFileCacheHandle::PreloadData(const FFileCachePreloadEntry* PreloadEntries, int32 NumEntries, int64 InOffset, EAsyncIOPriorityAndFlags Priority)
{
	SCOPED_LOADTIMER(FFileCacheHandle_PreloadData);

	check(NumEntries > 0);

	CheckForSizeRequestComplete();

	FFileCache& Cache = GetCache();

	FScopeLock CacheLock(&Cache.CriticalSection);

	{
		const FFileCachePreloadEntry& LastEntry = PreloadEntries[NumEntries - 1];
		const CacheLineID LastEndLine = GetBlock<CacheLineID>(LastEntry.Offset + LastEntry.Size - 1);
		if (LastEndLine.Get() >= NumSlots)
		{
			// If we're still waiting on SizeRequest, may need to lazily allocate some slots to service this request
			// If this happens after SizeRequest has completed, that means something must have gone wrong
			check(SizeRequestEvent);
			NumSlots = LastEndLine.Get() + 1;
			// TArray is max signed int
			check(NumSlots < MAX_int32);
			LineToSlot.SetNum((int32)NumSlots, false);
			LineToRequest.SetNum((int32)NumSlots, false);
		}
	}

	FGraphEventArray CompletionEvents;
	TArray<CacheSlotID> LockedSlots;
	LockedSlots.Empty(NumEntries);

	CacheLineID CurrentLine(0);
	int64 PrevOffset = -1;
	uint32 NumSlotsLoaded = 0u;
	for (int32 EntryIndex = 0; EntryIndex < NumEntries; ++EntryIndex)
	{
		const FFileCachePreloadEntry& Entry = PreloadEntries[EntryIndex];
		int64 EntryOffset = InOffset + Entry.Offset;
		const int64 EndOffset = EntryOffset + Entry.Size;
		const CacheLineID StartLine = GetBlock<CacheLineID>(EntryOffset);
		const CacheLineID EndLine = GetBlock<CacheLineID>(EndOffset - 1);

		checkf(Entry.Offset > PrevOffset, TEXT("Preload entries must be sorted by Offset [%lld, %lld), %lld"),
			Entry.Offset, Entry.Offset + Entry.Size, PrevOffset);
		PrevOffset = Entry.Offset;

		int64 OffsetInSlot = EntryOffset - StartLine.Get() * CacheLineSize;
		int64 SizeInSlot = FMath::Min<int64>(Entry.Size, CacheLineSize - OffsetInSlot);
		if (CurrentLine.Get() > StartLine.Get())
		{
			// Will hit this case if the last line of the previous entry is the same as the first line of this entry
			// In this case, we'll already have a slot allocated for the line, just need to mark additional preload region
			check(EntryIndex > 0);
			check(CurrentLine.Get() == StartLine.Get() + 1);
			const CacheSlotID SlotID = LineToSlot[StartLine.Get()];
			check(SlotID.IsValid());
			Cache.MarkSlotPreloadedRegion(SlotID, OffsetInSlot, SizeInSlot);
			EntryOffset += SizeInSlot;
			OffsetInSlot = 0;
			SizeInSlot = FMath::Min<int64>(EndOffset - EntryOffset, CacheLineSize);
		}
		else
		{
			CurrentLine = StartLine;
		}

		while (CurrentLine.Get() <= EndLine.Get())
		{
			CacheSlotID& SlotID = LineToSlot[CurrentLine.Get()];
			if (!SlotID.IsValid())
			{
				// no valid slot for this line, grab a new slot from cache and start a read request
				SlotID = AcquireSlotAndReadLine(Cache, CurrentLine, Priority);
				LockedSlots.Add(SlotID);
				++NumSlotsLoaded;
			}
		
			Cache.MarkSlotPreloadedRegion(SlotID, OffsetInSlot, SizeInSlot);

			FPendingRequest& PendingRequest = LineToRequest[CurrentLine.Get()];
			if (PendingRequest.Event && !PendingRequest.Event->IsComplete())
			{
				// this line has a pending async request to read data
				// will need to wait for this request to complete before data is valid
				CompletionEvents.Add(PendingRequest.Event);
			}
			else
			{
				PendingRequest.Event.SafeRelease();
			}

			++CurrentLine;
			EntryOffset += SizeInSlot;
			OffsetInSlot = 0;
			SizeInSlot = FMath::Min<int64>(EndOffset - EntryOffset, CacheLineSize);
		}
	}

	FGraphEventRef CompletionEvent;
	if (CompletionEvents.Num() > 0)
	{
		CompletionEvent = TGraphTask<FFileCachePreloadTask>::CreateTask(&CompletionEvents).ConstructAndDispatchWhenReady(MoveTemp(LockedSlots));
	}
	else if (LockedSlots.Num() > 0)
	{
		// Unusual case, we locked some slots, but the reads completed immediately, so we don't need to keep the slots locked
		for (const CacheSlotID& SlotID : LockedSlots)
		{
			Cache.UnlockSlot(SlotID);
		}
	}

	return CompletionEvent;
}

void FFileCacheHandle::ReleasePreloadedData(const FFileCachePreloadEntry* PreloadEntries, int32 NumEntries, int64 InOffset)
{
	check(NumEntries > 0);

	FFileCache& Cache = GetCache();

	FScopeLock CacheLock(&Cache.CriticalSection);

	int64 PrevOffset = -1;
	uint32 NumSlotsUnloaded = 0u;
	for (int32 EntryIndex = 0; EntryIndex < NumEntries; ++EntryIndex)
	{
		const FFileCachePreloadEntry& Entry = PreloadEntries[EntryIndex];
		int64 EntryOffset = InOffset + Entry.Offset;
		const int64 EndOffset = EntryOffset + Entry.Size;
		const CacheLineID StartLine = GetBlock<CacheLineID>(EntryOffset);
		const CacheLineID EndLine = GetBlock<CacheLineID>(EndOffset - 1);

		checkf(Entry.Offset > PrevOffset, TEXT("Preload entries must be sorted by Offset [%lld, %lld), %lld"),
			Entry.Offset, Entry.Offset + Entry.Size, PrevOffset);
		PrevOffset = Entry.Offset;

		int64 OffsetInSlot = EntryOffset - StartLine.Get() * CacheLineSize;
		int64 SizeInSlot = FMath::Min<int64>(Entry.Size, CacheLineSize - OffsetInSlot);
		CacheLineID CurrentLine = StartLine;
		while (CurrentLine.Get() <= EndLine.Get())
		{
			CacheSlotID& SlotID = LineToSlot[CurrentLine.Get()];
			if (SlotID.IsValid())
			{
				Cache.ClearSlotPreloadedRegion(SlotID, OffsetInSlot, SizeInSlot);
				++NumSlotsUnloaded;
			}

			++CurrentLine;
			EntryOffset += SizeInSlot;
			OffsetInSlot = 0;
			SizeInSlot = FMath::Min<int64>(EndOffset - EntryOffset, CacheLineSize);
		}
	}

	Cache.ReleaseMemory(NumSlotsUnloaded);
}

void FFileCacheHandle::Evict(CacheLineID LineID)
{
	LineToSlot[LineID.Get()] = CacheSlotID();

	FPendingRequest& PendingRequest = LineToRequest[LineID.Get()];
	if (PendingRequest.Event)
	{
		check(PendingRequest.Event->IsComplete());
		PendingRequest.Event.SafeRelease();
	}
}

void FFileCacheHandle::WaitAll()
{
	for (int i = 0; i < LineToRequest.Num(); ++i)
	{
		FPendingRequest& PendingRequest = LineToRequest[i];
		if (PendingRequest.Event)
		{
			check(PendingRequest.Event->IsComplete());
			PendingRequest.Event.SafeRelease();
		}
	}
}

void IFileCacheHandle::EvictAll()
{
	GetCache().EvictAll();
}

IFileCacheHandle* IFileCacheHandle::CreateFileCacheHandle(const TCHAR* InFileName)
{
	SCOPE_CYCLE_COUNTER(STAT_SFC_CreateHandle);

	IAsyncReadFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(InFileName);
	if (!FileHandle)
	{
		return nullptr;
	}

	return new FFileCacheHandle(FileHandle);
}

IFileCacheHandle* IFileCacheHandle::CreateFileCacheHandle(IAsyncReadFileHandle* FileHandle)
{
	SCOPE_CYCLE_COUNTER(STAT_SFC_CreateHandle);

	if (FileHandle != nullptr)
	{
		return new FFileCacheHandle(FileHandle);	
	}
	else
	{
		return nullptr;
	}
}

uint32 IFileCacheHandle::GetFileCacheSize()
{
	return GetCache().SizeInBytes;
}
