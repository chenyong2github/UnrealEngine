// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcher.h"

#ifndef PLATFORM_IMPLEMENTS_IO
#define PLATFORM_IMPLEMENTS_IO 0
#endif

#if PLATFORM_IMPLEMENTS_IO
#include COMPILED_PLATFORM_HEADER(PlatformIoDispatcher.h)
#else
#include "GenericPlatform/GenericPlatformIoDispatcher.h"
typedef FGenericIoDispatcherEventQueue FIoDispatcherEventQueue;
typedef FGenericFileIoStoreImpl FFileIoStoreImpl;
#endif

class FIoRequestImpl;

enum EIoStoreResolveResult
{
	IoStoreResolveResult_OK,
	IoStoreResolveResult_NotFound,
};

class FIoBatchImpl
{
public:
	TFunction<void()> Callback;
	FEvent* Event = nullptr;
	FGraphEventRef GraphEvent;
	TAtomic<uint32> UnfinishedRequestsCount{ 0 };
};

class FIoRequestImpl
{
public:
	FIoRequestImpl(FIoDispatcherImpl& InDispatcher)
		: Dispatcher(InDispatcher)
	{

	}

	void AddRef()
	{
		RefCount.IncrementExchange();
	}

	void ReleaseRef()
	{
		if (RefCount.DecrementExchange() == 1)
		{
			FreeRequest();
		}
	}

	FIoDispatcherImpl& Dispatcher;
	FIoBatchImpl* Batch = nullptr;
	FIoRequestImpl* NextRequest = nullptr;
	FIoChunkId ChunkId;
	FIoReadOptions Options;
	FIoBuffer IoBuffer;
	FIoReadCallback Callback;
	uint32 UnfinishedReadsCount = 0;
	int32 Priority = 0;
	TAtomic<EIoErrorCode> ErrorCode{ EIoErrorCode::Unknown };
	bool bFailed = false;

private:
	void FreeRequest();

	TAtomic<uint32> RefCount{ 0 };
};

