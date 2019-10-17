// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterNodeCtrlBase.h"
#include "DisplayClusterLog.h"

#include "Network/DisplayClusterServer.h"
#include "Network/DisplayClusterClient.h"


FDisplayClusterNodeCtrlBase::FDisplayClusterNodeCtrlBase(const FString& ctrlName, const FString& nodeName)
	: NodeName(nodeName)
	, ControllerName(ctrlName)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterNodeController
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


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterNodeCtrlBase
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterNodeCtrlBase::StartServerWithLogs(FDisplayClusterServer* Server) const
{
	if (!Server)
	{
		UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Invalid server instance (nullptr)"));
		return false;
	}

	const bool bResult = Server->Start();

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

bool FDisplayClusterNodeCtrlBase::StartClientWithLogs(FDisplayClusterClient* Client, const FString& Addr, int32 Port, int32 ClientConnTriesAmount, int32 ClientConnRetryDelay) const
{
	if (!Client)
	{
		UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Invalid client instance (nullptr)"));
		return false;
	}

	const bool bResult = Client->Connect(Addr, Port, ClientConnTriesAmount, ClientConnRetryDelay);

	if (bResult)
	{
		UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s connected to the server %s:%d"), *Client->GetName(), *Addr, Port);
	}
	else
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("%s couldn't connect to the server %s:%d"), *Client->GetName(), *Addr, Port);
	}

	return bResult;
}
