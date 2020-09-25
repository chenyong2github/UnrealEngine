// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/DisplayClusterServer.h"

class FSocket;
struct FIPv4Endpoint;


/**
 * Abstract DisplayCluster service
 */
class FDisplayClusterService
	: public FDisplayClusterServer
{
public:
	FDisplayClusterService(const FString& Name);

public:
	// Returns true if requested Endpoint is a part of nDisplay cluster (listed in a config file)
	static bool IsClusterIP(const FIPv4Endpoint& Endpoint);

protected:
	// Ask a server implementation if it's  allowed to accept incoming connections from non-cluster addresses
	virtual bool IsConnectionAllowed(FSocket* Socket, const FIPv4Endpoint& Endpoint) override;
};
