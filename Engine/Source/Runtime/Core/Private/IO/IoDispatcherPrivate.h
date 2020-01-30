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
	IoStoreResolveResult_Stalled,
	IoStoreResolveResult_NotFound,
};

class FIoBatchImpl
{
public:
	FIoRequestImpl* FirstRequest = nullptr;
};

class FIoRequestImpl
{
public:
	FIoBatchImpl* Batch = nullptr;
	FIoRequestImpl* NextRequest = nullptr;
	FIoRequestImpl* BatchNextRequest = nullptr;
	FIoStatus Status;
	FIoChunkId ChunkId;
	FIoReadOptions Options;
	FIoBuffer IoBuffer;
	uint32 UnfinishedReadsCount;
	TFunction<void(TIoStatusOr<FIoBuffer>)> Callback;
};

