// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatformIoDispatcher.h"
#include "IO/IoDispatcherFileBackend.h"
#include "Async/AsyncFileHandle.h"
#include "HAL/Event.h"
#include "HAL/PlatformFilemanager.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/RunnableThread.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"

//PRAGMA_DISABLE_OPTIMIZATION

TRACE_DECLARE_INT_COUNTER(IoDispatcherSequentialReads, TEXT("IoDispatcher/SequentialReads"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherForwardSeeks, TEXT("IoDispatcher/ForwardSeeks"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherBackwardSeeks, TEXT("IoDispatcher/BackwardSeeks"));
TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalSeekDistance, TEXT("IoDispatcher/TotalSeekDistance"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheHits, TEXT("IoDispatcher/CacheHits"));

int32 GIoDispatcherCacheSizeMB = 0;
static FAutoConsoleVariableRef CVar_IoDispatcherCacheSizeMB(
	TEXT("s.IoDispatcherCacheSizeMB"),
	GIoDispatcherCacheSizeMB,
	TEXT("IoDispatcher cache memory size (in megabytes).")
);

FGenericIoDispatcherEventQueue::FGenericIoDispatcherEventQueue()
	: Event(FPlatformProcess::GetSynchEventFromPool())
{
}

FGenericIoDispatcherEventQueue::~FGenericIoDispatcherEventQueue()
{
	FPlatformProcess::ReturnSynchEventToPool(Event);
}

void FGenericIoDispatcherEventQueue::Notify()
{
	Event->Trigger();
}

void FGenericIoDispatcherEventQueue::Wait()
{
	Event->Wait();
}

FGenericFileIoStoreImpl::FGenericFileIoStoreImpl(FGenericIoDispatcherEventQueue& InEventQueue, uint64 InReadBufferSize)
	: EventQueue(InEventQueue)
	, ReadBufferSize(InReadBufferSize)
{
	uint64 CacheMemorySize = GIoDispatcherCacheSizeMB > 0 ? uint64(GIoDispatcherCacheSizeMB) << 20 : 0;
	uint64 CacheBlockCount = CacheMemorySize / InReadBufferSize;
	if (CacheBlockCount)
	{
		CacheMemorySize = CacheBlockCount * InReadBufferSize;
		CacheMemory = reinterpret_cast<uint8*>(FMemory::Malloc(CacheMemorySize));
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
	else
	{
		CacheLruHead.LruNext = &CacheLruTail;
		CacheLruTail.LruPrev = &CacheLruHead;
	}
}

FGenericFileIoStoreImpl::~FGenericFileIoStoreImpl()
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

bool FGenericFileIoStoreImpl::OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	IFileHandle* FileHandle = Ipf.OpenReadNoBuffering(ContainerFilePath);
	if (!FileHandle)
	{
		return false;
	}
	ContainerFileHandle = reinterpret_cast<UPTRINT>(FileHandle);
	ContainerFileSize = FileHandle->Size();
	return true;
}

void FGenericFileIoStoreImpl::ReadBlockFromFile(uint8* Target, uint64 InFileHandle, FFileIoStoreRawBlock* Block)
{
	FCachedBlock* CachedBlock = nullptr;
	bool bIsCacheableBlock = CacheMemory != nullptr && ((Block->Flags & FFileIoStoreRawBlock::Cacheable) > 0);
	if (bIsCacheableBlock)
	{
		CachedBlock = CachedBlocks.FindRef(Block->Key.Hash);
	}

	if (CachedBlock)
	{
		FMemory::Memcpy(Target, CachedBlock->Buffer, ReadBufferSize);
		CachedBlock->LruPrev->LruNext = CachedBlock->LruNext;
		CachedBlock->LruNext->LruPrev = CachedBlock->LruPrev;
		
		CachedBlock->LruPrev = &CacheLruHead;
		CachedBlock->LruNext = CacheLruHead.LruNext;
		
		CachedBlock->LruPrev->LruNext = CachedBlock;
		CachedBlock->LruNext->LruPrev = CachedBlock;

		TRACE_COUNTER_INCREMENT(IoDispatcherCacheHits);
	}
	else
	{
		IFileHandle* FileHandle = reinterpret_cast<IFileHandle*>(static_cast<UPTRINT>(InFileHandle));
		if (FileHandle->Tell() != Block->Offset)
		{
			if (uint64(FileHandle->Tell()) > Block->Offset)
			{
				TRACE_COUNTER_INCREMENT(IoDispatcherBackwardSeeks);
			}
			else
			{
				TRACE_COUNTER_INCREMENT(IoDispatcherForwardSeeks);
			}
			TRACE_COUNTER_ADD(IoDispatcherTotalSeekDistance, FMath::Abs(FileHandle->Tell() - int64(Block->Offset)));
			FileHandle->Seek(Block->Offset);
		}
		else
		{
			TRACE_COUNTER_INCREMENT(IoDispatcherSequentialReads);
		}
		FileHandle->Read(Target, Block->Size);

		if (bIsCacheableBlock)
		{
			FCachedBlock* BlockToReplace = CacheLruTail.LruPrev;
			CachedBlocks.Remove(BlockToReplace->Key);
			BlockToReplace->Key = Block->Key.Hash;
			CachedBlocks.Add(BlockToReplace->Key, BlockToReplace);
			
			BlockToReplace->LruPrev->LruNext = BlockToReplace->LruNext;
			BlockToReplace->LruNext->LruPrev = BlockToReplace->LruPrev;

			BlockToReplace->LruPrev = &CacheLruHead;
			BlockToReplace->LruNext = CacheLruHead.LruNext;

			BlockToReplace->LruPrev->LruNext = BlockToReplace;
			BlockToReplace->LruNext->LruPrev = BlockToReplace;
			
			FMemory::Memcpy(BlockToReplace->Buffer, Target, ReadBufferSize);
		}
	}
	{
		FScopeLock _(&CompletedBlocksCritical);
		if (!CompletedBlocksHead)
		{
			CompletedBlocksHead = CompletedBlocksTail = Block;
		}
		else
		{
			CompletedBlocksTail->Next = Block;
			CompletedBlocksTail = Block;
		}
		Block->Next = nullptr;
	}
	EventQueue.Notify();
}

FFileIoStoreRawBlock* FGenericFileIoStoreImpl::GetCompletedBlocks()
{
	FScopeLock _(&CompletedBlocksCritical);
	FFileIoStoreRawBlock* Result = CompletedBlocksHead;
	CompletedBlocksHead = CompletedBlocksTail = nullptr;
	return Result;
}
