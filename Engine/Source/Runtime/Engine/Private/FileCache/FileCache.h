// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/MemoryReadStream.h"

/**
 * Thready safety note: Once created a IFileCacheHandle is assumed to be only used from a single thread.
 * (i.e. the IFileCacheHandle interface is not thread safe, and the user will need to ensure serialization).
 * Of course you can create several IFileCacheHandle's on separate threads if needed. And obviously Internally threading
 * will also be used to do async IO and cache management.
 * 
 * Also note, if you create several IFileCacheHandle's to the same file on separate threads these will be considered
 * as individual separate files from the cache point of view and thus each will have their own cache data allocated.
 */
class IFileCacheHandle
{
public:
	static void EvictAll();

	static IFileCacheHandle *CreateFileCacheHandle(const TCHAR* InFileName);
	virtual ~IFileCacheHandle() {};

	/**
	 * Read a byte range form the file. This can be a high-throughput operation and done lots of times for small reads.
	 * The system will handle this efficiently.
	 * @param	OutCompletionEvents			Must wait until these events are complete before returned data is valid
	 * @return	Memory stream that contains the requested range.  May return nullptr in rare cases if the request could not be serviced
	 *			Data read from this stream will not be valid until all events returned in OutCompletionEvents are complete
	 */
	virtual IMemoryReadStreamRef ReadData(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority) = 0;

	/**
	 * Wait until all outstanding read requests complete. 
	 */
	virtual void WaitAll() = 0;
};