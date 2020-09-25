// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/DisplayClusterClient.h"
#include "Network/Protocol/IDisplayClusterProtocolEventsJson.h"
#include "Network/Packet/DisplayClusterPacketJson.h"


/**
 * JSON cluster events TCP client
 */
class FDisplayClusterClusterEventsJsonClient
	: public FDisplayClusterClient<FDisplayClusterPacketJson, false>
	, public IDisplayClusterProtocolEventsJson
{
public:
	FDisplayClusterClusterEventsJsonClient();
	FDisplayClusterClusterEventsJsonClient(const FString& InName);

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsJson
	//////////////////////////////////////////////////////////////////////////////////////////////
	void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event) override;
};
