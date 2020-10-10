// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/Service/DisplayClusterService.h"
#include "Network/Session/IDisplayClusterSessionPacketHandler.h"
#include "Network/Protocol/IDisplayClusterProtocolEventsBinary.h"
#include "Network/Packet/DisplayClusterPacketBinary.h"


/**
 * Binary cluster events server
 */
class FDisplayClusterClusterEventsBinaryService
	: public    FDisplayClusterService
	, public    IDisplayClusterSessionPacketHandler<FDisplayClusterPacketBinary, false>
	, protected IDisplayClusterProtocolEventsBinary
{
public:
	FDisplayClusterClusterEventsBinaryService();
	virtual ~FDisplayClusterClusterEventsBinaryService();

protected:
	// Creates session instance for this service
	virtual TUniquePtr<IDisplayClusterSession> CreateSession(FSocket* Socket, const FIPv4Endpoint& Endpoint, uint64 SessionId) override;

	virtual bool IsConnectionAllowed(FSocket* Socket, const FIPv4Endpoint& Endpoint)
	{
		// Always allow, an event may come from anywhere
		return true;
	}

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterSessionPacketHandler
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual typename IDisplayClusterSessionPacketHandler<FDisplayClusterPacketBinary, false>::ReturnType ProcessPacket(const TSharedPtr<FDisplayClusterPacketBinary>& Request) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsBinary
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event) override;
};
