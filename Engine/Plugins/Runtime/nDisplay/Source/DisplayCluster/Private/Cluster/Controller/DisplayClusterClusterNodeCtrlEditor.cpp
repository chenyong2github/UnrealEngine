// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlEditor.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Network/Service/ClusterEvents/DisplayClusterClusterEventsClient.h"
#include "Network/Service/ClusterEvents/DisplayClusterClusterEventsService.h"

#include "IPDisplayCluster.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterClusterNodeCtrlEditor::FDisplayClusterClusterNodeCtrlEditor(const FString& ctrlName, const FString& nodeName)
	: FDisplayClusterClusterNodeCtrlBase(ctrlName, nodeName)
{
}

FDisplayClusterClusterNodeCtrlEditor::~FDisplayClusterClusterNodeCtrlEditor()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterEventsProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlEditor::EmitClusterEvent(const FDisplayClusterClusterEvent& Event)
{
	UE_LOG(LogDisplayClusterCluster, Warning, TEXT("This should never be called!"));
	ClusterEventsClient->EmitClusterEvent(Event);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterNodeCtrlBase
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterClusterNodeCtrlEditor::InitializeServers()
{
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return false;
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - initializing editor servers..."), *GetControllerName());

	// Get config data
	FDisplayClusterConfigClusterNode CfgMasterNode;
	if (GDisplayCluster->GetPrivateConfigMgr()->GetMasterClusterNode(CfgMasterNode) == false)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No master node configuration data found"));
		return false;
	}

	// Editor controller uses 127.0.0.1 only
	CfgMasterNode.Addr = FString("127.0.0.1");

	// Instantiate node servers
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Servers: addr %s, port_ce %d"), *CfgMasterNode.Addr, CfgMasterNode.Port_CE);
	ClusterEventsServer.Reset(new FDisplayClusterClusterEventsService(CfgMasterNode.Addr, CfgMasterNode.Port_CE));

	return ClusterEventsServer.IsValid();
}

bool FDisplayClusterClusterNodeCtrlEditor::StartServers()
{
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - starting editor servers..."), *GetControllerName());
	return StartServerWithLogs(ClusterEventsServer.Get());
}

void FDisplayClusterClusterNodeCtrlEditor::StopServers()
{
	ClusterEventsServer->Shutdown();
}

bool FDisplayClusterClusterNodeCtrlEditor::InitializeClients()
{
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - initializing editor clients..."), *GetControllerName());

	// Instantiate local clients
	ClusterEventsClient.Reset(new FDisplayClusterClusterEventsClient);

	return ClusterEventsClient.IsValid();
}

bool FDisplayClusterClusterNodeCtrlEditor::StartClients()
{
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - starting editor clients..."), *GetControllerName());

	// Master config
	FDisplayClusterConfigClusterNode CfgMasterNode;
	if (GDisplayCluster->GetPrivateConfigMgr()->GetMasterClusterNode(CfgMasterNode) == false)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No master node configuration data found"));
		return false;
	}

	// Editor controller uses 127.0.0.1 only
	CfgMasterNode.Addr = FString("127.0.0.1");

	const FDisplayClusterConfigNetwork CfgNetwork = GDisplayCluster->GetPrivateConfigMgr()->GetConfigNetwork();

	return StartClientWithLogs(ClusterEventsClient.Get(), CfgMasterNode.Addr, CfgMasterNode.Port_CE, CfgNetwork.ClientConnectTriesAmount, CfgNetwork.ClientConnectRetryDelay);
}

void FDisplayClusterClusterNodeCtrlEditor::StopClients()
{
	ClusterEventsClient->Disconnect();
}
