// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlSlave.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Network/Service/ClusterEventsJson/DisplayClusterClusterEventsJsonClient.h"
#include "Network/Service/ClusterEventsBinary/DisplayClusterClusterEventsBinaryClient.h"
#include "Network/Service/ClusterSync/DisplayClusterClusterSyncClient.h"
#include "Network/Service/RenderSync/DisplayClusterRenderSyncClient.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterClusterNodeCtrlSlave::FDisplayClusterClusterNodeCtrlSlave(const FString& CtrlName, const FString& NodeName) :
	FDisplayClusterClusterNodeCtrlBase(CtrlName, NodeName)
{
}

FDisplayClusterClusterNodeCtrlSlave::~FDisplayClusterClusterNodeCtrlSlave()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolClusterSync
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlSlave::WaitForGameStart(double* ThreadWaitTime, double* BarrierWaitTime)
{
	check(ClusterSyncClient);
	ClusterSyncClient->WaitForGameStart(ThreadWaitTime, BarrierWaitTime);
}

void FDisplayClusterClusterNodeCtrlSlave::WaitForFrameStart(double* ThreadWaitTime, double* BarrierWaitTime)
{
	check(ClusterSyncClient);
	ClusterSyncClient->WaitForFrameStart(ThreadWaitTime, BarrierWaitTime);
}

void FDisplayClusterClusterNodeCtrlSlave::WaitForFrameEnd(double* ThreadWaitTime, double* BarrierWaitTime)
{
	check(ClusterSyncClient);
	ClusterSyncClient->WaitForFrameEnd(ThreadWaitTime, BarrierWaitTime);
}

void FDisplayClusterClusterNodeCtrlSlave::GetDeltaTime(float& DeltaSeconds)
{
	check(ClusterSyncClient);
	ClusterSyncClient->GetDeltaTime(DeltaSeconds);
}

void FDisplayClusterClusterNodeCtrlSlave::GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime)
{
	check(ClusterSyncClient);
	ClusterSyncClient->GetFrameTime(FrameTime);
}

void FDisplayClusterClusterNodeCtrlSlave::GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	check(ClusterSyncClient);
	ClusterSyncClient->GetSyncData(SyncData, SyncGroup);
}

void FDisplayClusterClusterNodeCtrlSlave::GetInputData(TMap<FString, FString>& InputData)
{
	check(ClusterSyncClient);
	ClusterSyncClient->GetInputData(InputData);
}

void FDisplayClusterClusterNodeCtrlSlave::GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents)
{
	check(ClusterSyncClient);
	ClusterSyncClient->GetEventsData(JsonEvents, BinaryEvents);
}

void FDisplayClusterClusterNodeCtrlSlave::GetNativeInputData(TMap<FString, FString>& NativeInputData)
{
	check(ClusterSyncClient);
	ClusterSyncClient->GetNativeInputData(NativeInputData);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolRenderSync
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlSlave::WaitForSwapSync(double* ThreadWaitTime, double* BarrierWaitTime)
{
	check(RenderSyncClient.IsValid());
	RenderSyncClient->WaitForSwapSync(ThreadWaitTime, BarrierWaitTime);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolEventsJson
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlSlave::EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event)
{
	check(ClusterEventsJsonClient);
	ClusterEventsJsonClient->EmitClusterEventJson(Event);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolEventsBinary
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlSlave::EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event)
{
	check(ClusterEventsBinaryClient);
	ClusterEventsBinaryClient->EmitClusterEventBinary(Event);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterNodeCtrlBase
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterClusterNodeCtrlSlave::InitializeServers()
{
	if (!FDisplayClusterClusterNodeCtrlBase::InitializeServers())
	{
		return false;
	}

	// Slave servers initialization
	// ...

	return true;
}

bool FDisplayClusterClusterNodeCtrlSlave::StartServers()
{
	if (!FDisplayClusterClusterNodeCtrlBase::StartServers())
	{
		return false;
	}

	// Slave servers start
	// ...

	return true;
}

void FDisplayClusterClusterNodeCtrlSlave::StopServers()
{
	FDisplayClusterClusterNodeCtrlBase::StopServers();

	// Slave servers stop
	// ...
}

bool FDisplayClusterClusterNodeCtrlSlave::InitializeClients()
{
	if (!FDisplayClusterClusterNodeCtrlBase::InitializeClients())
	{
		return false;
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - initializing slave clients..."), *GetControllerName());

	// Instantiate local clients
	ClusterSyncClient         = MakeUnique<FDisplayClusterClusterSyncClient>();
	RenderSyncClient          = MakeUnique<FDisplayClusterRenderSyncClient>();
	ClusterEventsJsonClient   = MakeUnique<FDisplayClusterClusterEventsJsonClient>();
	ClusterEventsBinaryClient = MakeUnique<FDisplayClusterClusterEventsBinaryClient>();

	return ClusterSyncClient && RenderSyncClient && ClusterEventsJsonClient && ClusterEventsBinaryClient;
}

bool FDisplayClusterClusterNodeCtrlSlave::StartClients()
{
	if (!FDisplayClusterClusterNodeCtrlBase::StartClients())
	{
		return false;
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - initializing slave clients..."), *GetControllerName());

	// Master config
	const UDisplayClusterConfigurationClusterNode* CfgMaster = GDisplayCluster->GetPrivateConfigMgr()->GetMasterNode();
	if (!CfgMaster)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No master node configuration data found"));
		return false;
	}

	// Get configuration data
	const UDisplayClusterConfigurationData* ConfigData = GDisplayCluster->GetPrivateConfigMgr()->GetConfig();
	if (!ConfigData)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't get configuration data"));
		return false;
	}

	// Allow children to override master's address
	FString HostToUse = CfgMaster->Host;
	OverrideMasterAddr(HostToUse);

	// Start the clients
	return StartClientWithLogs(ClusterSyncClient.Get(),         HostToUse, ConfigData->Cluster->MasterNode.Ports.ClusterSync,         ConfigData->Cluster->Network.ConnectRetriesAmount, ConfigData->Cluster->Network.ConnectRetryDelay)
		&& StartClientWithLogs(RenderSyncClient.Get(),          HostToUse, ConfigData->Cluster->MasterNode.Ports.RenderSync,          ConfigData->Cluster->Network.ConnectRetriesAmount, ConfigData->Cluster->Network.ConnectRetryDelay)
		&& StartClientWithLogs(ClusterEventsJsonClient.Get(),   HostToUse, ConfigData->Cluster->MasterNode.Ports.ClusterEventsJson,   ConfigData->Cluster->Network.ConnectRetriesAmount, ConfigData->Cluster->Network.ConnectRetryDelay)
		&& StartClientWithLogs(ClusterEventsBinaryClient.Get(), HostToUse, ConfigData->Cluster->MasterNode.Ports.ClusterEventsBinary, ConfigData->Cluster->Network.ConnectRetriesAmount, ConfigData->Cluster->Network.ConnectRetryDelay);
}

void FDisplayClusterClusterNodeCtrlSlave::StopClients()
{
	FDisplayClusterClusterNodeCtrlBase::StopClients();

	ClusterEventsJsonClient->Disconnect();
	ClusterEventsBinaryClient->Disconnect();
	ClusterSyncClient->Disconnect();
	RenderSyncClient->Disconnect();
}
