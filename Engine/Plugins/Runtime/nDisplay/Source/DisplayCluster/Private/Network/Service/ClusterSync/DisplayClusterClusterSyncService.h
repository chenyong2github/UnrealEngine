// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/Service/DisplayClusterService.h"
#include "Network/Session/IDisplayClusterSessionPacketHandler.h"
#include "Network/Protocol/IDisplayClusterProtocolClusterSync.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"

#include "Misc/DisplayClusterBarrier.h"


/**
 * Cluster synchronization TCP server
 */
class FDisplayClusterClusterSyncService
	: public    FDisplayClusterService
	, public    IDisplayClusterSessionPacketHandler<FDisplayClusterPacketInternal, true>
	, protected IDisplayClusterProtocolClusterSync
{
public:
	FDisplayClusterClusterSyncService();
	virtual ~FDisplayClusterClusterSyncService();

public:
	virtual bool Start(const FString& Address, int32 Port) override;
	void Shutdown() override;

protected:
	// Creates session instance for this service
	virtual TUniquePtr<IDisplayClusterSession> CreateSession(FSocket* Socket, const FIPv4Endpoint& Endpoint, uint64 SessionId) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterSessionStatusListener
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void NotifySessionClose(uint64 SessionId) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterSessionPacketHandler
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<FDisplayClusterPacketInternal> ProcessPacket(const TSharedPtr<FDisplayClusterPacketInternal>& Request) override;

private:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolClusterSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForGameStart() override;
	virtual void WaitForFrameStart() override;
	virtual void WaitForFrameEnd() override;
	virtual void GetTimeData(float& InOutDeltaTime, double& InOutGameTime, TOptional<FQualifiedFrameTime>& InOutFrameTime) override;
	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup)  override;
	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) override;
	virtual void GetNativeInputData(TMap<FString, FString>& NativeInputData) override;

private:
	// Game start sync barrier
	FDisplayClusterBarrier BarrierGameStart;
	// Frame start barrier
	FDisplayClusterBarrier BarrierFrameStart;
	// Frame end barrier
	FDisplayClusterBarrier BarrierFrameEnd;
};
