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
	// Game start barrier
	virtual void WaitForGameStart(double* ThreadWaitTime, double* BarrierWaitTime) = 0;

	// Frame start barrier
	virtual void WaitForFrameStart(double* ThreadWaitTime, double* BarrierWaitTime) = 0;

	// Frame end barrier
	virtual void WaitForFrameEnd(double* ThreadWaitTime, double* BarrierWaitTime) = 0;

	// Provides with time delta for current frame
	virtual void GetDeltaTime(float& DeltaSeconds) = 0;

	// Get the Timecode value for the current frame.
	virtual void GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime) = 0;

	// Sync objects
	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) = 0;

	// Sync input
	virtual void GetInputData(TMap<FString, FString>& InputData) = 0;

	// Sync events
	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents) = 0;

	// Sync native UE4 input
	virtual void GetNativeInputData(TMap<FString, FString>& NativeInputData) = 0;
};
