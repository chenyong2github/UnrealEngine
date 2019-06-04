// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace Trace
{

class IStore;
typedef uint64 FStoreSessionHandle;
typedef uint64 FRecorderSessionHandle;

struct FRecorderSessionInfo
{
	FRecorderSessionHandle Handle;
	FStoreSessionHandle StoreSessionHandle;
};

////////////////////////////////////////////////////////////////////////////////
class IRecorder
{
public:
	virtual ~IRecorder() = default;

	/** Checks if the recorder is actively recording, return false if it is not. */
	virtual bool IsRunning() const = 0;

	/** Starts recording, returning true if it was started successfully or if was already running. */
	virtual bool StartRecording() = 0;

	/** Stops a running recorder dead in its tracks. */
	virtual void StopRecording() = 0;

	/** Returns the number of current in-bound sessions. */
	virtual uint32 GetSessionCount() const = 0;

	/** Return information about the current in-bound sessions */
	virtual void GetActiveSessions(TArray<FRecorderSessionInfo>& OutSessions) const = 0;

	/** Toggles an event logger widlcard on or off for an active recording session */
	virtual bool ToggleEvent(FRecorderSessionHandle RecordingHandle, const TCHAR* LoggerWildcard, bool bState) = 0;
};

TRACEANALYSIS_API TSharedPtr<IRecorder> Recorder_Create(TSharedRef<IStore> Store);

} // namespace Trace
