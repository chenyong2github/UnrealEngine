// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"
#include "Misc/DateTime.h"

namespace Trace
{

class IInDataStream;
class IOutDataStream;
typedef uint64 FStoreSessionHandle;

/**
 * Information about traces available in the trace store
 */
struct FStoreSessionInfo
{
	/** A handle to identify the trace */
	FStoreSessionHandle Handle = FStoreSessionHandle(-1);
	/** Fully qualified path to the trace store item */
	const TCHAR* Uri = nullptr;
	/** Friendly-ish name of the trace */
	const TCHAR* Name = nullptr;
	/** Timestamp of the trace */
	FDateTime TimeStamp;
	/** Size of the trace */
	uint64 Size = 0;
	/** True if the trace is currently being written to */
	bool bIsLive = false;
};

/**
 * Interface to a provider of storage for traces.
 */
class IStore
{
public:
	virtual ~IStore() = default;

	/** Enumerates the "sessions" (aka traces) that are currently available.
	 * @param OutSessions Recipient of the list of available traces. */
	virtual void GetAvailableSessions(TArray<FStoreSessionInfo>& OutSessions) const = 0;

	/** Creates a new trace that can be written to.
	 * @return A handle for identifying the "session" (aka trace) and an interface for writing arbitrary data to the trace */
	virtual TTuple<FStoreSessionHandle, IOutDataStream*> CreateNewSession() = 0;

	/** Opens an existing trace.
	 * @param Handle Identifier for the trace to open
	 * @return Interface to reading the arbitrary trace data */
	virtual IInDataStream* OpenSessionStream(FStoreSessionHandle Handle) = 0;
};

/**
 * Creates a file-based store at a given file system location location
 * @param StoreDir File system directory where the store will read/write traces
 */
TRACEANALYSIS_API TSharedPtr<IStore> Store_Create(const TCHAR* StoreDir);

}
