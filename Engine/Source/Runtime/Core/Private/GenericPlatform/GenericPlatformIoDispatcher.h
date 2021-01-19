// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/List.h"
#include "HAL/CriticalSection.h"
#include "Templates/Tuple.h"
#include "HAL/Runnable.h"
#include "Containers/Map.h"
#include "IO/IoDispatcher.h"
#include "IO/IoDispatcherFileBackendTypes.h"

class FGenericFileIoStoreEventQueue
{
public:
	FGenericFileIoStoreEventQueue();
	~FGenericFileIoStoreEventQueue();
	void ServiceNotify();
	void ServiceWait();

private:
	FEvent* ServiceEvent = nullptr;
};

class FGenericFileIoStoreImpl
{
public:
	FGenericFileIoStoreImpl(FGenericFileIoStoreEventQueue& InEventQueue, FFileIoStoreBufferAllocator& InBufferAllocator, FFileIoStoreBlockCache& InBlockCache);
	~FGenericFileIoStoreImpl();
	void Initialize(const FWakeUpIoDispatcherThreadDelegate* InWakeUpDispatcherThreadDelegate)
	{
		WakeUpDispatcherThreadDelegate = InWakeUpDispatcherThreadDelegate;
	}
	bool OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize);
	bool CreateCustomRequests(FFileIoStoreRequestAllocator& RequestAllocator, FFileIoStoreResolvedRequest& ResolvedRequest, FFileIoStoreReadRequestList& OutRequests)
	{
		return false;
	}
	bool StartRequests(FFileIoStoreRequestQueue& RequestQueue);
	void GetCompletedRequests(FFileIoStoreReadRequestList& OutRequests);

private:
	const FWakeUpIoDispatcherThreadDelegate* WakeUpDispatcherThreadDelegate = nullptr;
	FGenericFileIoStoreEventQueue& EventQueue;
	FFileIoStoreBufferAllocator& BufferAllocator;
	FFileIoStoreBlockCache& BlockCache;

	FCriticalSection CompletedRequestsCritical;
	FFileIoStoreReadRequestList CompletedRequests;
};

