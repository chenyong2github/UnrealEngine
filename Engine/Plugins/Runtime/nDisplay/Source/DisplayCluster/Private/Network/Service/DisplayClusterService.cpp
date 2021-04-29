// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/DisplayClusterService.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HAL/IConsoleManager.h"


// nDisplay service threads priority
static TAutoConsoleVariable<int32> CVarServiceThreadsPriority(
	TEXT("nDisplay.Service.ThreadsPriority"),
	3,
	TEXT("Service threads priority:\n")
	TEXT("0 : Lowest\n")
	TEXT("1 : Below normal\n")
	TEXT("2 : Slightly below normal\n")
	TEXT("3 : Normal\n")
	TEXT("4 : Above normal\n")
	TEXT("5 : Highest\n")
	TEXT("6 : Time critical\n")
	,
	ECVF_Default
);


FDisplayClusterService::FDisplayClusterService(const FString& Name)
	: FDisplayClusterServer(Name)
{
}

EThreadPriority FDisplayClusterService::ConvertThreadPriorityFromCvarValue(int ThreadPriority)
{
	switch (ThreadPriority)
	{
	case 0:
		return EThreadPriority::TPri_Lowest;
	case 1:
		return EThreadPriority::TPri_BelowNormal;
	case 2:
		return EThreadPriority::TPri_SlightlyBelowNormal;
	case 3:
		return EThreadPriority::TPri_Normal;
	case 4:
		return EThreadPriority::TPri_AboveNormal;
	case 5:
		return EThreadPriority::TPri_Highest;
	case 6:
		return EThreadPriority::TPri_TimeCritical;
	default:
		break;
	}

	return EThreadPriority::TPri_Normal;
}

EThreadPriority FDisplayClusterService::GetThreadPriority()
{
	return FDisplayClusterService::ConvertThreadPriorityFromCvarValue(CVarServiceThreadsPriority.GetValueOnAnyThread());
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
