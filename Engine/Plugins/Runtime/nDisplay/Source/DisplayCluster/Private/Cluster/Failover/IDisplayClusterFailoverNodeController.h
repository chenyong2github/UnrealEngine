// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Network/DisplayClusterNetworkTypes.h"
#include "Network/IDisplayClusterServer.h"
#include "DisplayClusterEnums.h"


enum class EDisplayClusterFailoverPolicy : uint8
{
	// No failover operations performed. The whole cluster gets terminated in case of any error
	Disabled,

	// First implementation of failover feature. This policy allows to drop any secondary node
	// out of cluster in case it's failed, and let the others continue working. However, if
	// primary node fails, the whole cluster will be terminated.
	Failover_v1_DropSecondaryNodesOnly
};


/**
 * Failover controller interface
 */
class IDisplayClusterFailoverNodeController
{
public:
	virtual ~IDisplayClusterFailoverNodeController() = default;

public:
	// Returns current failover policy
	virtual EDisplayClusterFailoverPolicy GetFailoverPolicy() const = 0;

public:
	// Handles in-cluster communication results
	virtual void HandleCommResult(EDisplayClusterCommResult CommResult) = 0;

	// Handles node failure
	virtual void HandleNodeFailed(const FString& NodeId, IDisplayClusterServer::ENodeFailType NodeFailType) = 0;
};
