// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Failover/DisplayClusterFailoverNodeCtrlBase.h"


/**
 * Failover controller (secondary node)
 */
class FDisplayClusterFailoverNodeCtrlSecondary
	: public FDisplayClusterFailoverNodeCtrlBase
{
public:
	FDisplayClusterFailoverNodeCtrlSecondary()
		: FDisplayClusterFailoverNodeCtrlBase()
	{ }

protected:
	// EDisplayClusterFailoverPolicy::Disabled
	virtual void HandleCommResult_Disabled(EDisplayClusterCommResult CommResult) override;
	virtual void HandleNodeFailed_Disabled(const FString& NodeId, IDisplayClusterServer::ENodeFailType NodeFailType) override;

	// EDisplayClusterFailoverPolicy::Failover_v1_DropSecondaryNodesOnly
	virtual void HandleCommResult_Failover_v1(EDisplayClusterCommResult CommResult) override;
	virtual void HandleNodeFailed_Failover_v1(const FString& NodeId, IDisplayClusterServer::ENodeFailType NodeFailType) override;
};
