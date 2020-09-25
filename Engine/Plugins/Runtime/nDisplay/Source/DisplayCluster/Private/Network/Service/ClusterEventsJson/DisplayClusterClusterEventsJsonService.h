// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/Service/DisplayClusterService.h"
#include "Network/Session/IDisplayClusterSessionPacketHandler.h"
#include "Network/Packet/DisplayClusterPacketJson.h"
#include "Network/Protocol/IDisplayClusterProtocolEventsJson.h"

#include "Dom/JsonObject.h"

/**
 * JSON cluster events server
 */
class FDisplayClusterClusterEventsJsonService
	: public    FDisplayClusterService
	, public    IDisplayClusterSessionPacketHandler<FDisplayClusterPacketJson, false>
	, protected IDisplayClusterProtocolEventsJson
{
public:
	FDisplayClusterClusterEventsJsonService();
	virtual ~FDisplayClusterClusterEventsJsonService();

public:
	enum class EDisplayClusterJsonError : uint8
	{
		Ok = 0,
		NotSupported = 1,
		MissedMandatoryFields = 2,
		UnknownError = 255
	};

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
	virtual typename IDisplayClusterSessionPacketHandler<FDisplayClusterPacketJson, false>::ReturnType ProcessPacket(const TSharedPtr<FDisplayClusterPacketJson>& Request) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsJson
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event) override;

private:
	FDisplayClusterClusterEventJson BuildClusterEventFromJson(const FString& EventName, const FString& EventType, const FString& EventCategory, const TSharedPtr<FJsonObject>& JsonPacket) const;
};
