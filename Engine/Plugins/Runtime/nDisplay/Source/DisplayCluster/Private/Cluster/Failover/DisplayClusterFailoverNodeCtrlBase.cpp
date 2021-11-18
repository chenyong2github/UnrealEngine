// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Failover/DisplayClusterFailoverNodeCtrlBase.h"

#include "Misc/DisplayClusterLog.h"
#include "Misc/ScopeLock.h"

#include "Network/DisplayClusterNetworkTypes.h"

#include "HAL/IConsoleManager.h"


// Failover settings
static TAutoConsoleVariable<int32> CVarFailover(
	TEXT("nDisplay.network.failover"),
	0,
	TEXT("Failover settings:\n")
	TEXT("\t0 : Disabled\n")
	TEXT("\t1 : Failover v1 - drop secondary nodes on fail\n")
);


FDisplayClusterFailoverNodeCtrlBase::FDisplayClusterFailoverNodeCtrlBase()
	: FailoverPolicy( FDisplayClusterFailoverNodeCtrlBase::GetFailoverPolicyFromCvarValue(CVarFailover.GetValueOnAnyThread()) )
{
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Failover: policy is %s"), *FDisplayClusterFailoverNodeCtrlBase::FailoverPolicyAsString(FailoverPolicy));
}


void FDisplayClusterFailoverNodeCtrlBase::HandleCommResult(EDisplayClusterCommResult CommResult)
{
	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Failover: CommResult - %u"), (uint32)CommResult);

	FScopeLock Lock(&InternalsCS);

	switch (GetFailoverPolicy())
	{
	case EDisplayClusterFailoverPolicy::Disabled:
		return HandleCommResult_Disabled(CommResult);

	case EDisplayClusterFailoverPolicy::Failover_v1_DropSecondaryNodesOnly:
		return HandleCommResult_Failover_v1(CommResult);
	}
}

void FDisplayClusterFailoverNodeCtrlBase::HandleNodeFailed(const FString& NodeId, IDisplayClusterServer::ENodeFailType NodeFailType)
{
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Failover: Cluster node failed - node=%s, fail_type=%u"), *NodeId, static_cast<uint32>(NodeFailType));

	FScopeLock Lock(&InternalsCS);

	switch (GetFailoverPolicy())
	{
	case EDisplayClusterFailoverPolicy::Disabled:
		return HandleNodeFailed_Disabled(NodeId, NodeFailType);

	case EDisplayClusterFailoverPolicy::Failover_v1_DropSecondaryNodesOnly:
		return HandleNodeFailed_Failover_v1(NodeId, NodeFailType);
	}
}

EDisplayClusterFailoverPolicy FDisplayClusterFailoverNodeCtrlBase::GetFailoverPolicyFromCvarValue(int32 FailoverCvarNumber)
{
	switch (FailoverCvarNumber)
	{
	case 1:
		return EDisplayClusterFailoverPolicy::Failover_v1_DropSecondaryNodesOnly;

	case 0:
	default:
		return EDisplayClusterFailoverPolicy::Disabled;
	}
}

FString FDisplayClusterFailoverNodeCtrlBase::FailoverPolicyAsString(const EDisplayClusterFailoverPolicy Policy)
{
	switch (Policy)
	{
	case EDisplayClusterFailoverPolicy::Disabled:
		return FString("Disabled");

	case EDisplayClusterFailoverPolicy::Failover_v1_DropSecondaryNodesOnly:
		return FString("Failover_v1");

	default:
		return FString("Unknown");
	}
}
