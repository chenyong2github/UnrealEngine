// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlSlave.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "Network/Service/ClusterEvents/DisplayClusterClusterEventsClient.h"
#include "Network/Service/ClusterSync/DisplayClusterClusterSyncClient.h"
#include "Network/Service/SwapSync/DisplayClusterSwapSyncClient.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterClusterNodeCtrlSlave::FDisplayClusterClusterNodeCtrlSlave(const FString& ctrlName, const FString& nodeName) :
	FDisplayClusterClusterNodeCtrlBase(ctrlName, nodeName)
{
}

FDisplayClusterClusterNodeCtrlSlave::~FDisplayClusterClusterNodeCtrlSlave()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterSyncProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlSlave::WaitForGameStart()
{
	ClusterSyncClient->WaitForGameStart();
}

void FDisplayClusterClusterNodeCtrlSlave::WaitForFrameStart()
{
	ClusterSyncClient->WaitForFrameStart();
}

void FDisplayClusterClusterNodeCtrlSlave::WaitForFrameEnd()
{
	ClusterSyncClient->WaitForFrameEnd();
}

void FDisplayClusterClusterNodeCtrlSlave::WaitForTickEnd()
{
	ClusterSyncClient->WaitForTickEnd();
}

void FDisplayClusterClusterNodeCtrlSlave::GetDeltaTime(float& DeltaSeconds)
{
	ClusterSyncClient->GetDeltaTime(DeltaSeconds);
}

void FDisplayClusterClusterNodeCtrlSlave::GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime)
{
	ClusterSyncClient->GetFrameTime(FrameTime);
}

void FDisplayClusterClusterNodeCtrlSlave::GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	ClusterSyncClient->GetSyncData(SyncData, SyncGroup);
}

void FDisplayClusterClusterNodeCtrlSlave::GetInputData(FDisplayClusterMessage::DataType& InputData)
{
	ClusterSyncClient->GetInputData(InputData);
}

void FDisplayClusterClusterNodeCtrlSlave::GetEventsData(FDisplayClusterMessage::DataType& EventsData)
{
	ClusterSyncClient->GetEventsData(EventsData);
}

void FDisplayClusterClusterNodeCtrlSlave::GetNativeInputData(FDisplayClusterMessage::DataType& NativeInputData)
{
	ClusterSyncClient->GetNativeInputData(NativeInputData);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterSwapSyncProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlSlave::WaitForSwapSync(double* pThreadWaitTime, double* pBarrierWaitTime)
{
	check(SwapSyncClient.IsValid());
	SwapSyncClient->WaitForSwapSync(pThreadWaitTime, pBarrierWaitTime);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterEventsProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlSlave::EmitClusterEvent(const FDisplayClusterClusterEvent& Event)
{
	ClusterEventsClient->EmitClusterEvent(Event);
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
	ClusterSyncClient.Reset(new FDisplayClusterClusterSyncClient);
	SwapSyncClient.Reset(new FDisplayClusterSwapSyncClient);
	ClusterEventsClient.Reset(new FDisplayClusterClusterEventsClient);

	return ClusterSyncClient.IsValid() && SwapSyncClient.IsValid() && ClusterEventsClient.IsValid();
}

bool FDisplayClusterClusterNodeCtrlSlave::StartClients()
{
	if (!FDisplayClusterClusterNodeCtrlBase::StartClients())
	{
		return false;
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - initializing slave clients..."), *GetControllerName());

	// Master config
	FDisplayClusterConfigClusterNode MasterCfg;
	if (GDisplayCluster->GetPrivateConfigMgr()->GetMasterClusterNode(MasterCfg) == false)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No master node configuration data found"));
		return false;
	}

	// Allow children to override master's address
	OverrideMasterAddr(MasterCfg.Addr);

	const FDisplayClusterConfigNetwork CfgNetwork = GDisplayCluster->GetPrivateConfigMgr()->GetConfigNetwork();

	return StartClientWithLogs(ClusterSyncClient.Get(),   MasterCfg.Addr, MasterCfg.Port_CS, CfgNetwork.ClientConnectTriesAmount, CfgNetwork.ClientConnectRetryDelay)
		&& StartClientWithLogs(SwapSyncClient.Get(),      MasterCfg.Addr, MasterCfg.Port_SS, CfgNetwork.ClientConnectTriesAmount, CfgNetwork.ClientConnectRetryDelay)
		&& StartClientWithLogs(ClusterEventsClient.Get(), MasterCfg.Addr, MasterCfg.Port_CE, CfgNetwork.ClientConnectTriesAmount, CfgNetwork.ClientConnectRetryDelay);
}

void FDisplayClusterClusterNodeCtrlSlave::StopClients()
{
	FDisplayClusterClusterNodeCtrlBase::StopClients();

	ClusterEventsClient->Disconnect();
	ClusterSyncClient->Disconnect();
	SwapSyncClient->Disconnect();
}
