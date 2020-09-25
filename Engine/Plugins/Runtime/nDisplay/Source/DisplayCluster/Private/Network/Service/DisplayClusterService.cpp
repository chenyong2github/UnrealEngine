// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/DisplayClusterService.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"

#include "Interfaces\IPv4\IPv4Endpoint.h"


FDisplayClusterService::FDisplayClusterService(const FString& Name)
	: FDisplayClusterServer(Name)
{
}

bool FDisplayClusterService::IsClusterIP(const FIPv4Endpoint& Endpoint)
{
	// Get configuration data
	const UDisplayClusterConfigurationData* ConfigData = GDisplayCluster->GetPrivateConfigMgr()->GetConfig();
	if (!ConfigData)
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Couldn't get configuration data"));
		return false;
	}

	const FString Address = Endpoint.Address.ToString();
	for (const auto& Node : ConfigData->Cluster->Nodes)
	{
		//@todo IP + Hostname comparison
		if (Node.Value->Host.Equals(Address, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool FDisplayClusterService::IsConnectionAllowed(FSocket* Socket, const FIPv4Endpoint& Endpoint)
{
	// By default only cluster node IP addresses are allowed
	return FDisplayClusterService::IsClusterIP(Endpoint);
}
