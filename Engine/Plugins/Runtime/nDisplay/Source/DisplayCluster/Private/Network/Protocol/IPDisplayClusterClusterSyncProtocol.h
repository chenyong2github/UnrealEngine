// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/DisplayClusterMessage.h"
#include "DisplayClusterEnums.h"

struct FTimecode;
struct FFrameRate;


/**
 * Cluster state synchronization protocol
 */
class IPDisplayClusterClusterSyncProtocol
{
public:
	// Game start barrier
	virtual void WaitForGameStart() = 0;

	// Frame start barrier
	virtual void WaitForFrameStart() = 0;

	// Frame end barrier
	virtual void WaitForFrameEnd() = 0;

	// Tick end barrier
	virtual void WaitForTickEnd() = 0;

	// Provides with time delta for current frame
	virtual void GetDeltaTime(float& DeltaSeconds) = 0;

	// Get the Timecode value for the current frame.
	virtual void GetTimecode(FTimecode& Timecode, FFrameRate& FrameRate) = 0;

	// Sync objects
	virtual void GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) = 0;

	// Sync input
	virtual void GetInputData(FDisplayClusterMessage::DataType& InputData) = 0;

	// Sync events
	virtual void GetEventsData(FDisplayClusterMessage::DataType& EventsData) = 0;

	// Sync native UE4 input
	virtual void GetNativeInputData(FDisplayClusterMessage::DataType& NativeInputData) = 0;
};
