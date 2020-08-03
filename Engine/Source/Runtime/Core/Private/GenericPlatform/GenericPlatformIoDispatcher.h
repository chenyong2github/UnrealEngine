// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/List.h"
#include "HAL/CriticalSection.h"
#include "Templates/Tuple.h"
#include "HAL/Runnable.h"
#include "Containers/Map.h"
#include "IO/IoDispatcher.h"

struct FFileIoStoreReadRequest;
class FFileIoStoreBufferAllocator;
class FFileIoStoreBlockCache;
class FFileIoStoreRequestQueue;

class FGenericIoDispatcherEventQueue
{
public:
	FGenericIoDispatcherEventQueue();
	~FGenericIoDispatcherEventQueue();
	void DispatcherNotify();
	void DispatcherWait();
	void DispatcherWaitForIo()
	{
		DispatcherWait();
	}
	void ServiceNotify();
	void ServiceWait();

private:
	FEvent* DispatcherEvent = nullptr;
	FEvent* ServiceEvent = nullptr;
};

class FGenericFileIoStoreImpl
{
public:
	FGenericFileIoStoreImpl(FGenericIoDispatcherEventQueue& InEventQueue, FFileIoStoreBufferAllocator& InBufferAllocator, FFileIoStoreBlockCache& InBlockCache);
	~FGenericFileIoStoreImpl();
	bool OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize);
	bool StartRequests(FFileIoStoreRequestQueue& RequestQueue);
	FFileIoStoreReadRequest* GetCompletedRequests();

private:
	FGenericIoDispatcherEventQueue& EventQueue;
	FFileIoStoreBufferAllocator& BufferAllocator;
	FFileIoStoreBlockCache& BlockCache;

	FCriticalSection CompletedRequestsCritical;
	FFileIoStoreReadRequest* CompletedRequestsHead = nullptr;
	FFileIoStoreReadRequest* CompletedRequestsTail = nullptr;
};

