// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/Service/DisplayClusterService.h"
#include "Network/Session/IDisplayClusterSessionPacketHandler.h"
#include "Network/Protocol/IDisplayClusterProtocolRenderSync.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"

#include "Misc/DisplayClusterBarrier.h"

struct FIPv4Endpoint;


/**
 * Rendering synchronization TCP server
 */
class FDisplayClusterRenderSyncService
	: public    FDisplayClusterService
	, public    IDisplayClusterSessionPacketHandler<FDisplayClusterPacketInternal, true>
	, protected IDisplayClusterProtocolRenderSync
{
public:
	FDisplayClusterRenderSyncService();
	virtual ~FDisplayClusterRenderSyncService();

public:
	virtual bool Start(const FString& Address, int32 Port) override;
	virtual void Shutdown() override;

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
	// IDisplayClusterProtocolRenderSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForSwapSync() override;


private:
	// Swap sync barrier
	FDisplayClusterBarrier BarrierSwap;
};
