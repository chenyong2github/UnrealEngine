// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlMaster.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Failover/IDisplayClusterFailoverNodeController.h"
#include "Config/IPDisplayClusterConfigManager.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Network/Service/ClusterSync/DisplayClusterClusterSyncService.h"
#include "Network/Service/RenderSync/DisplayClusterRenderSyncService.h"
#include "Network/Service/ClusterEventsJson/DisplayClusterClusterEventsJsonService.h"
#include "Network/Service/ClusterEventsBinary/DisplayClusterClusterEventsBinaryService.h"
#include "Network/Listener/DisplayClusterTcpListener.h"
#include "Network/DisplayClusterNetworkTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterClusterNodeCtrlMaster::FDisplayClusterClusterNodeCtrlMaster(const FString& CtrlName, const FString& NodeName)
	: FDisplayClusterClusterNodeCtrlSlave(CtrlName, NodeName)
{
	// TCP connection listener for all nDisplay internal services
	TcpListener = MakeShared<FDisplayClusterTcpListener>(true, FString("nDisplay-TCP-listener"));
}

FDisplayClusterClusterNodeCtrlMaster::~FDisplayClusterClusterNodeCtrlMaster()
{
	UE_LOG(LogDisplayClusterNetwork, Log, TEXT("Releasing %s cluster controller..."), *GetControllerName());

	Shutdown();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolClusterSync
//////////////////////////////////////////////////////////////////////////////////////////////
EDisplayClusterCommResult FDisplayClusterClusterNodeCtrlMaster::GetTimeData(double& OutDeltaTime, double& OutGameTime, TOptional<FQualifiedFrameTime>& OutFrameTime)
{
	if (IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr())
	{
		ClusterMgr->ExportTimeData(OutDeltaTime, OutGameTime, OutFrameTime);
		return EDisplayClusterCommResult::Ok;
	}

	return EDisplayClusterCommResult::InternalError;
}

EDisplayClusterCommResult FDisplayClusterClusterNodeCtrlMaster::GetObjectsData(const EDisplayClusterSyncGroup InSyncGroup, TMap<FString, FString>& OutObjectsData)
{
	if (IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr())
	{
		ClusterMgr->ExportObjectsData(InSyncGroup, OutObjectsData);
		return EDisplayClusterCommResult::Ok;
	}

	return EDisplayClusterCommResult::InternalError;
}

EDisplayClusterCommResult FDisplayClusterClusterNodeCtrlMaster::GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& OutJsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& OutBinaryEvents)
{
	if (IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr())
	{
		ClusterMgr->ExportEventsData(OutJsonEvents, OutBinaryEvents);
		return EDisplayClusterCommResult::Ok;
	}

	return EDisplayClusterCommResult::InternalError;
}

EDisplayClusterCommResult FDisplayClusterClusterNodeCtrlMaster::GetNativeInputData(TMap<FString, FString>& OutNativeInputData)
{
	if (IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr())
	{
		ClusterMgr->ExportNativeInputData(OutNativeInputData);
		return EDisplayClusterCommResult::Ok;
	}

	return EDisplayClusterCommResult::InternalError;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterClusterNodeController
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterClusterNodeCtrlMaster::DropClusterNode(const FString& NodeId)
{
	// Kill all sessions of the requested node
	ClusterSyncServer->KillSession(NodeId);
	RenderSyncServer->KillSession(NodeId);
	ClusterEventsJsonServer->KillSession(NodeId);
	ClusterEventsBinaryServer->KillSession(NodeId);

	return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterNodeCtrlBase
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterClusterNodeCtrlMaster::InitializeServers()
{
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return false;
	}

	if (!FDisplayClusterClusterNodeCtrlSlave::InitializeServers())
	{
		return false;
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - initializing master servers..."), *GetControllerName());

	// Instantiate node servers
	ClusterSyncServer         = MakeUnique<FDisplayClusterClusterSyncService>       ();
	RenderSyncServer          = MakeUnique<FDisplayClusterRenderSyncService>        ();
	ClusterEventsJsonServer   = MakeUnique<FDisplayClusterClusterEventsJsonService> ();
	ClusterEventsBinaryServer = MakeUnique<FDisplayClusterClusterEventsBinaryService>();

	return ClusterSyncServer && RenderSyncServer && ClusterEventsJsonServer && ClusterEventsBinaryServer;
}

bool FDisplayClusterClusterNodeCtrlMaster::StartServers()
{
	if (!FDisplayClusterClusterNodeCtrlSlave::StartServers())
	{
		return false;
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - starting master servers..."), *GetControllerName());

	// Get config data
	const UDisplayClusterConfigurationData* ConfigData = GDisplayCluster->GetPrivateConfigMgr()->GetConfig();
	if (!ConfigData)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't get configuration data"));
		return false;
	}

	// Get master config data
	const UDisplayClusterConfigurationClusterNode* const CfgMaster = GDisplayCluster->GetPrivateConfigMgr()->GetMasterNode();
	if (!CfgMaster)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No master node configuration data found"));
		return false;
	}

	const FDisplayClusterConfigurationMasterNodePorts& Ports = ConfigData->Cluster->MasterNode.Ports;
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Servers: addr %s, port_cs %d, port_ce %d, port_ceb %d"), *CfgMaster->Host, Ports.ClusterSync, Ports.ClusterEventsJson, Ports.ClusterEventsBinary);

	// Connection validation lambda
	auto IsConnectionAllowedFunc = [](const FDisplayClusterSessionInfo& SessionInfo)
	{
		// Here we make sure the node belongs to the cluster
		TArray<FString> NodeIds;
		GDisplayCluster->GetPrivateClusterMgr()->GetNodeIds(NodeIds);
		return NodeIds.ContainsByPredicate([SessionInfo](const FString& Item)
			{
				return Item.Equals(SessionInfo.NodeId.Get(FString()), ESearchCase::IgnoreCase);
			});
	};

	// Set connection validation for internal sync servers
	ClusterSyncServer->OnIsConnectionAllowed().BindLambda(IsConnectionAllowedFunc);
	RenderSyncServer->OnIsConnectionAllowed().BindLambda(IsConnectionAllowedFunc);

	// Listen for node failure notifications
	ClusterSyncServer->OnNodeFailed().AddRaw(this, &FDisplayClusterClusterNodeCtrlMaster::ProcessNodeFailed);
	RenderSyncServer->OnNodeFailed().AddRaw(this, &FDisplayClusterClusterNodeCtrlMaster::ProcessNodeFailed);
	ClusterEventsJsonServer->OnNodeFailed().AddRaw(this, &FDisplayClusterClusterNodeCtrlMaster::ProcessNodeFailed);
	ClusterEventsBinaryServer->OnNodeFailed().AddRaw(this, &FDisplayClusterClusterNodeCtrlMaster::ProcessNodeFailed);

	// Start the servers
	return StartServerWithLogs(ClusterSyncServer.Get(),         TcpListener) // Start with shared listener
		&& StartServerWithLogs(RenderSyncServer.Get(),          TcpListener) // Start with shared listener
		&& StartServerWithLogs(ClusterEventsJsonServer.Get(),   CfgMaster->Host, Ports.ClusterEventsJson)
		&& StartServerWithLogs(ClusterEventsBinaryServer.Get(), CfgMaster->Host, Ports.ClusterEventsBinary)
		&& TcpListener->StartListening(CfgMaster->Host, Ports.ClusterSync); // Start shared listener as well
}

void FDisplayClusterClusterNodeCtrlMaster::StopServers()
{
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Cluster controller %s is shutting down the services..."), *GetControllerName());

	// Stop listening for incoming connections
	TcpListener->StopListening(true);

	// Stop services
	ClusterSyncServer->Shutdown();
	RenderSyncServer->Shutdown();
	ClusterEventsJsonServer->Shutdown();
	ClusterEventsBinaryServer->Shutdown();

	FDisplayClusterClusterNodeCtrlSlave::StopServers();
}

bool FDisplayClusterClusterNodeCtrlMaster::InitializeClients()
{
	return FDisplayClusterClusterNodeCtrlSlave::InitializeClients();
}

bool FDisplayClusterClusterNodeCtrlMaster::StartClients()
{
	return FDisplayClusterClusterNodeCtrlSlave::StartClients();
}

void FDisplayClusterClusterNodeCtrlMaster::StopClients()
{
	FDisplayClusterClusterNodeCtrlSlave::StopClients();
}

void FDisplayClusterClusterNodeCtrlMaster::ProcessNodeFailed(const FString& NodeId, IDisplayClusterServer::ENodeFailType NodeFailType)
{
	// We need to notify other services about this node (for example, we need to deactivate this node on all barriers of all services)
	DropClusterNode(NodeId);

	// We need to pass information about node failure so the failover controller will decide what to do
	if (IDisplayClusterFailoverNodeController* const FailoverCtrl = GDisplayCluster->GetPrivateClusterMgr()->GetFailoverNodeController())
	{
		FailoverCtrl->HandleNodeFailed(NodeId, NodeFailType);
	}
}
