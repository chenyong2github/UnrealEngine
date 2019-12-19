// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/List.h"
#include "HAL/CriticalSection.h"
#include "Templates/Tuple.h"

struct FFileIoStoreReadBlock;
struct FFileIoStoreResolvedRequest;
class IAsyncReadRequest;

class FGenericIoDispatcherEventQueue
{
public:
	FGenericIoDispatcherEventQueue();
	~FGenericIoDispatcherEventQueue();
	void Notify();
	void ProcessEvents();

private:
	FEvent* Event;
};

class FGenericFileIoStoreImpl
{
public:
	FGenericFileIoStoreImpl(FGenericIoDispatcherEventQueue& InEventQueue);
	bool OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize);
	void BeginReadsForRequest(FFileIoStoreResolvedRequest& ResolvedRequest);
	void ReadBlockFromFile(FFileIoStoreReadBlock* Block, uint8* Destination, uint64 FileHandle, uint64 Size, uint64 Offset);
	void EndReadsForRequest();
	FFileIoStoreReadBlock* GetNextCompletedBlock();

private:
	FGenericIoDispatcherEventQueue& EventQueue;
	FCriticalSection CompletedReadRequestsCritical;
	TDoubleLinkedList<TTuple<IAsyncReadRequest*, FFileIoStoreReadBlock*>> CompletedReadRequests;
};

