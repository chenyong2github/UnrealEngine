// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/List.h"
#include "HAL/CriticalSection.h"
#include "Templates/Tuple.h"
#include "HAL/Runnable.h"

struct FFileIoStoreReadBlock;
struct FFileIoStoreResolvedRequest;
class IAsyncReadRequest;

class FGenericIoDispatcherEventQueue
{
public:
	FGenericIoDispatcherEventQueue();
	~FGenericIoDispatcherEventQueue();
	void Notify();
	void Wait();
	void Poll() {};

private:
	FEvent* Event;
};

class FGenericFileIoStoreImpl
	: public FRunnable
{
public:
	FGenericFileIoStoreImpl(FGenericIoDispatcherEventQueue& InEventQueue);
	~FGenericFileIoStoreImpl();
	bool OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize);
	void BeginReadsForRequest(FFileIoStoreResolvedRequest& ResolvedRequest);
	void ReadBlockFromFile(FFileIoStoreReadBlock* Block);
	void EndReadsForRequest();
	FFileIoStoreReadBlock* GetNextCompletedBlock();
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	FGenericIoDispatcherEventQueue& EventQueue;

	FCriticalSection PendingBlocksCritical;
	FFileIoStoreReadBlock* PendingBlocksHead = nullptr;
	FFileIoStoreReadBlock* PendingBlocksTail = nullptr;
	FCriticalSection CompletedBlocksCritical;
	FFileIoStoreReadBlock* CompletedBlocksHead = nullptr;
	FFileIoStoreReadBlock* CompletedBlocksTail = nullptr;
	FEvent* PendingBlockEvent;
	FRunnableThread* Thread;
	TAtomic<bool> bStopRequested{ false };
};

