// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/DisplayClusterNetDriverHelper.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterStrings.h"

#include "Network/Service/ClusterEventsBinary/DisplayClusterClusterEventsBinaryClient.h"

bool FDisplayClusterNetDriverHelper::RegisterClusterEventsBinaryClient(uint32 ClusterId, const FString& ClientAddress, uint16 ClientPort)
{	
	TSharedPtr<FDisplayClusterClusterEventsBinaryClient>* FoundClient = ClusterEventsBinaryClients.Find(ClusterId);

	if (FoundClient != nullptr)
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Couldn't not register event binary client for %s:%d. It already exists."), *ClientAddress, ClientPort);
		return false;
	}
	
	TSharedPtr<FDisplayClusterClusterEventsBinaryClient> Client = MakeShared<FDisplayClusterClusterEventsBinaryClient>();
	
	if (!Client->Connect(ClientAddress, ClientPort, 1, 0.f))
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Couldn't not connect to %s:%d"), *ClientAddress, ClientPort);

		return false;
	}

	ClusterEventsBinaryClients.Add(ClusterId, Client);

	return true;
}

bool FDisplayClusterNetDriverHelper::RemoveClusterEventsBinaryClient(uint32 СlusterId)
{
	TSharedPtr<FDisplayClusterClusterEventsBinaryClient>* FoundClient = ClusterEventsBinaryClients.Find(СlusterId);

	if (FoundClient == nullptr)
	{
		return false;
	}

	const int32 RemovedEntries = ClusterEventsBinaryClients.Remove(СlusterId);

	return RemovedEntries > 0;
}

bool FDisplayClusterNetDriverHelper::HasClient(uint32 ClusterId)
{
	return ClusterEventsBinaryClients.Contains(ClusterId);
}

bool FDisplayClusterNetDriverHelper::GetRequiredArguments(const FURL& URL, const TCHAR*& OutClusterId, const TCHAR*& OutPrimaryNodeId, const TCHAR*& OutPrimaryNodePort)
{
	if (!URL.HasOption(DisplayClusterStrings::uri_args::ClusterId) ||
		!URL.HasOption(DisplayClusterStrings::uri_args::PrimaryNodeId) ||
		!URL.HasOption(DisplayClusterStrings::uri_args::PrimaryNodePort))
	{
		return false;
	}

	OutClusterId = URL.GetOption(DisplayClusterStrings::uri_args::ClusterId, nullptr);
	OutPrimaryNodeId = URL.GetOption(DisplayClusterStrings::uri_args::PrimaryNodeId, nullptr);
	OutPrimaryNodePort = URL.GetOption(DisplayClusterStrings::uri_args::PrimaryNodePort, nullptr);

	return true;
}

bool FDisplayClusterNetDriverHelper::SendCommandToCluster(uint32 ClusterId, const FDisplayClusterClusterEventBinary& Event)
{
	TSharedPtr<FDisplayClusterClusterEventsBinaryClient>* ClientFound = ClusterEventsBinaryClients.Find(ClusterId);

	if (ClientFound == nullptr)
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Event binary client not found for ClusterId %d"), ClusterId);
		return false;
	}

	TSharedPtr<FDisplayClusterClusterEventsBinaryClient> Client = *ClientFound;

	const EDisplayClusterCommResult CommResult = Client->EmitClusterEventBinary(Event);

	return (CommResult == EDisplayClusterCommResult::Ok);
}

void FDisplayClusterNetDriverHelper::SendCommandToAllClusters(const FDisplayClusterClusterEventBinary& Event)
{
	for (const TPair<uint32, TSharedPtr<FDisplayClusterClusterEventsBinaryClient>>& Client : ClusterEventsBinaryClients)
	{
		Client.Value->EmitClusterEventBinary(Event);
	}
}
