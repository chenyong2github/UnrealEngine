// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterNodeCtrlBase.h"

#include "Network/IDisplayClusterServer.h"
#include "Network/IDisplayClusterClient.h"

#include "Network/Service/ClusterEventsJson/DisplayClusterClusterEventsJsonClient.h"
#include "Network/Service/ClusterEventsBinary/DisplayClusterClusterEventsBinaryClient.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterNodeCtrlBase::FDisplayClusterNodeCtrlBase(const FString& CtrlName, const FString& NodeName)
	: NodeName(NodeName)
	, ControllerName(CtrlName)
	, ExternalEventsClientJson(MakeUnique<FDisplayClusterClusterEventsJsonClient>())
{
}

FDisplayClusterNodeCtrlBase::~FDisplayClusterNodeCtrlBase()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterNodeController
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterNodeCtrlBase::Initialize()
{
	if (!InitializeServers())
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Servers initialization failed"));
		return false;
	}

	if (!InitializeClients())
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Clients initialization failed"));
		return false;
	}

	if (!StartServers())
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("An error occurred during servers start"));
		return false;
	}

	if (!StartClients())
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("An error occurred during clients start"));
		return false;
	}

	return true;
}

void FDisplayClusterNodeCtrlBase::Release()
{
	StopServers();
	StopClients();
}

void FDisplayClusterNodeCtrlBase::SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly)
{
	// We should synchronize access to the client
	FScopeLock Lock(&ExternEventsClientJsonGuard);

	// One-shot connection
	ExternalEventsClientJson->Connect(Address, Port, 1, 0.f);
	ExternalEventsClientJson->EmitClusterEventJson(Event);
	ExternalEventsClientJson->Disconnect();
}

void FDisplayClusterNodeCtrlBase::SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly)
{
	// We should synchronize access to the client
	FScopeLock Lock(&ExternEventsClientBinaryGuard);

	// One-shot connection
	ExternalEventsClientBinary->Connect(Address, Port, 1, 0.f);
	ExternalEventsClientBinary->EmitClusterEventBinary(Event);
	ExternalEventsClientBinary->Disconnect();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterNodeCtrlBase
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterNodeCtrlBase::StartServerWithLogs(IDisplayClusterServer* Server, const FString& Address, int32 Port) const
{
	if (!Server)
	{
		UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Invalid server instance (nullptr)"));
		return false;
	}

	const bool bResult = Server->Start(Address, Port);

	if (bResult)
	{
		UE_LOG(LogDisplayClusterCluster, Log, TEXT("Server %s has started"), *Server->GetName());
	}
	else
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Server %s failed to start"), *Server->GetName());
	}

	return bResult;
}

bool FDisplayClusterNodeCtrlBase::StartClientWithLogs(IDisplayClusterClient* Client, const FString& Address, int32 Port, int32 ClientConnTriesAmount, int32 ClientConnRetryDelay) const
{
	if (!Client)
	{
		UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Invalid client instance (nullptr)"));
		return false;
	}

	const bool bResult = Client->Connect(Address, Port, ClientConnTriesAmount, ClientConnRetryDelay);

	if (bResult)
	{
		UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s connected to the server %s:%d"), *Client->GetName(), *Address, Port);
	}
	else
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("%s couldn't connect to the server %s:%d"), *Client->GetName(), *Address, Port);
	}

	return bResult;
}
