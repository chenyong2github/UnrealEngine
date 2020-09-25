// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/DisplayClusterClient.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"
#include "Network/Protocol/IDisplayClusterProtocolClusterSync.h"


/**
 * Cluster synchronization TCP client
 */
class FDisplayClusterClusterSyncClient
	: public FDisplayClusterClient<FDisplayClusterPacketInternal, true>
	, public IDisplayClusterProtocolClusterSync
{
public:
	FDisplayClusterClusterSyncClient();
	FDisplayClusterClusterSyncClient(const FString& InName);

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolClusterSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForGameStart(double* ThreadWaitTime, double* BarrierWaitTime) override;
	virtual void WaitForFrameStart(double* ThreadWaitTime, double* BarrierWaitTime) override;
	virtual void WaitForFrameEnd(double* ThreadWaitTime, double* BarrierWaitTime) override;
	virtual void GetDeltaTime(float& DeltaSeconds) override;
	virtual void GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime) override;
	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void GetInputData(TMap<FString, FString>& InputData) override;
	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents) override;
	virtual void GetNativeInputData(TMap<FString, FString>& NativeInputData) override;
};
