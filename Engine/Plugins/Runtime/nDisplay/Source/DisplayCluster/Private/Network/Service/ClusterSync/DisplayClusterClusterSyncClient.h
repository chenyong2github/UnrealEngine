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
	virtual void WaitForGameStart() override;
	virtual void WaitForFrameStart() override;
	virtual void WaitForFrameEnd() override;
	virtual void GetTimeData(float& InOutDeltaTime, double& InOutGameTime, TOptional<FQualifiedFrameTime>& InOutFrameTime) override;
	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) override;
	virtual void GetNativeInputData(TMap<FString, FString>& NativeInputData) override;
};
