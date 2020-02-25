// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/List.h"
#include "HAL/CriticalSection.h"
#include "Templates/Tuple.h"
#include "HAL/Runnable.h"
#include "Containers/Map.h"

struct FFileIoStoreRawBlock;
struct FFileIoStoreResolvedRequest;
class IAsyncReadRequest;

class FGenericIoDispatcherEventQueue
{
public:
	FGenericIoDispatcherEventQueue();
	~FGenericIoDispatcherEventQueue();
	void Notify();
	void Wait();
	void WaitForIo()
	{
		Wait();
	}

private:
	FEvent* Event;
};

class FGenericFileIoStoreImpl
{
public:
	FGenericFileIoStoreImpl(FGenericIoDispatcherEventQueue& InEventQueue, uint64 InReadBufferSize);
	~FGenericFileIoStoreImpl();
	bool OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize);
	void ReadBlockFromFile(uint8* Target, uint64 FileHandle, FFileIoStoreRawBlock* Block);
	void FlushReads() {};
	FFileIoStoreRawBlock* GetCompletedBlocks();

private:
	struct FCachedBlock
	{
		FCachedBlock* LruPrev = nullptr;
		FCachedBlock* LruNext = nullptr;
		uint64 Key;
		uint8* Buffer;
	};

	FGenericIoDispatcherEventQueue& EventQueue;
	const uint64 ReadBufferSize;

	FCriticalSection CompletedBlocksCritical;
	FFileIoStoreRawBlock* CompletedBlocksHead = nullptr;
	FFileIoStoreRawBlock* CompletedBlocksTail = nullptr;
	uint8* CacheMemory = nullptr;
	TMap<uint64, FCachedBlock*> CachedBlocks;
	FCachedBlock CacheLruHead;
	FCachedBlock CacheLruTail;

};

