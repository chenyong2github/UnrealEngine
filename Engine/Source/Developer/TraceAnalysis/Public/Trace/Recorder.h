// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace Trace
{

class IStore;
typedef uint64 FStoreSessionHandle;
typedef uint64 FRecorderSessionHandle;

/**
 * Information about traces being recorded
 */
struct FRecorderSessionInfo
{
	/** Identifier of the recording session */
	FRecorderSessionHandle Handle;
	/** Store identifier of the trace in question */
	FStoreSessionHandle StoreSessionHandle;
};

/**
 * Interface to an object that can receive traces from remote runtimes and write
 * them into a store.
 */
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

	/** Return information about the current in-bound sessions being recorded
	 * @param OutSessions TArray instance to receive the current session info. */
	virtual void GetActiveSessions(TArray<FRecorderSessionInfo>& OutSessions) const = 0;

	/** Toggles an event logger wildcard on or off for an active recording session.
	 * @param RecordingHandle Identifier of the active session, from GetActiveSessions().
	 * @param EventSpec Event specification (see FControlClient::ToggleEvent)
	 * @param bState True to enable the event and false to disable it.
	 * @return True if the command was successfully sent. */
	virtual bool ToggleEvent(FRecorderSessionHandle RecordingHandle, const TCHAR* EventSpec, bool bState) = 0;

	/**
	 * Toggles one or more channels on and off. A channel or a comma separated list
	 * of channels can be controlled.
	 * @param RecordingHandle Identifier of the active session, from GetActiveSessions().
	 * @param Channels A single channel name or a comma separated list of channel names.
	 * @param bState True to enable channel(s), false to disable.
	 * @return True if the command was successfully sent.
	 */
	virtual bool ToggleChannels(FRecorderSessionHandle RecordingHandle, const TCHAR* Channels, bool bState) = 0;
};

/**
 * Creates a recorder that will allow runtimes to connect to it and send their trace events.
 * @param Store The store instance to save trace streams into.
 */
TRACEANALYSIS_API TSharedPtr<IRecorder> Recorder_Create(TSharedRef<IStore> Store);

} // namespace Trace
