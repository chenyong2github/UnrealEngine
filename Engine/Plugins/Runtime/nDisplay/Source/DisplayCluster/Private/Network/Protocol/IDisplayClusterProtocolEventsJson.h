// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct FDisplayClusterClusterEventJson;


/**
 * JSON cluster events protocol
 */
class IDisplayClusterProtocolEventsJson
{
public:
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event) = 0;
};
