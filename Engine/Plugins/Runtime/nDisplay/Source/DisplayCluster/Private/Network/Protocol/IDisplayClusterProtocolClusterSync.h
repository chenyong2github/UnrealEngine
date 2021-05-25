// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterEnums.h"
#include "Misc/Optional.h"
#include "Misc/QualifiedFrameTime.h"

#include "Cluster/DisplayClusterClusterEvent.h"


/**
 * Cluster synchronization protocol. Used to synchronize/replicate any
 * DisplayCluster data on the game thread.
 */
class IDisplayClusterProtocolClusterSync
{
public:
	virtual ~IDisplayClusterProtocolClusterSync() = default;

public:
	// Game start barrier
	virtual void WaitForGameStart() = 0;

	// Frame start barrier
	virtual void WaitForFrameStart() = 0;

	// Frame end barrier
	virtual void WaitForFrameEnd() = 0;

	// Engine time
	virtual void GetTimeData(float& InOutDeltaTime, double& InOutGameTime, TOptional<FQualifiedFrameTime>& InOutFrameTime) = 0;

	// Sync objects
	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) = 0;

	// Sync events
	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) = 0;

	// Sync native UE input
	virtual void GetNativeInputData(TMap<FString, FString>& NativeInputData) = 0;
};
