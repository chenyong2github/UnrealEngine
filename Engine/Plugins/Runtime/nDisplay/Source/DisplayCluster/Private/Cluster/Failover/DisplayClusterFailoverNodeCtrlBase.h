// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Failover/IDisplayClusterFailoverNodeController.h"

struct FDisplayClusterSessionInfo;


/**
 * Base failover node controller class
 */
class FDisplayClusterFailoverNodeCtrlBase
	: public IDisplayClusterFailoverNodeController
{
public:
	FDisplayClusterFailoverNodeCtrlBase();

public:
	virtual EDisplayClusterFailoverPolicy GetFailoverPolicy() const override final
	{
		return FailoverPolicy;
	}

public:
	// Handles in-cluster communication results
	virtual void HandleCommResult(EDisplayClusterCommResult CommResult) override final;

	// Handles node failure
	virtual void HandleNodeFailed(const FString& NodeId, IDisplayClusterServer::ENodeFailType NodeFailType) override final;

protected:
	// EDisplayClusterFailoverPolicy::Disabled
	virtual void HandleCommResult_Disabled(EDisplayClusterCommResult CommResult) = 0;
	virtual void HandleNodeFailed_Disabled(const FString& NodeId, IDisplayClusterServer::ENodeFailType NodeFailType) = 0;

	// EDisplayClusterFailoverPolicy::Failover_v1_DropSecondaryNodesOnly
	virtual void HandleCommResult_Failover_v1(EDisplayClusterCommResult CommResult) = 0;
	virtual void HandleNodeFailed_Failover_v1(const FString& NodeId, IDisplayClusterServer::ENodeFailType NodeFailType) = 0;

private:
	// Converts failover policy from CVar integer into enum
	static EDisplayClusterFailoverPolicy GetFailoverPolicyFromCvarValue(int32 FailoverCvarNumber);
	// Returns failover policy name as readable string
	static FString FailoverPolicyAsString(const EDisplayClusterFailoverPolicy Policy);

private:
	// Current failover policy
	const EDisplayClusterFailoverPolicy FailoverPolicy;

	mutable FCriticalSection InternalsCS;
};
