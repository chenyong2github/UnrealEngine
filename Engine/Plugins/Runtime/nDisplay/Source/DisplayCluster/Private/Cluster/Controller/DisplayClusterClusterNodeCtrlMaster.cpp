// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlMaster.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Input/IPDisplayClusterInputManager.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Network/Service/ClusterSync/DisplayClusterClusterSyncService.h"
#include "Network/Service/RenderSync/DisplayClusterRenderSyncService.h"
#include "Network/Service/ClusterEventsJson/DisplayClusterClusterEventsJsonService.h"
#include "Network/Service/ClusterEventsBinary/DisplayClusterClusterEventsBinaryService.h"

#include "Engine/World.h"
#include "HAL/Event.h"

#include "Misc/App.h"

#include "DisplayClusterPlayerInput.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterClusterNodeCtrlMaster::FDisplayClusterClusterNodeCtrlMaster(const FString& CtrlName, const FString& NodeName) :
	FDisplayClusterClusterNodeCtrlSlave(CtrlName, NodeName)
{
	CachedSyncData.Emplace(EDisplayClusterSyncGroup::PreTick);
	CachedSyncData.Emplace(EDisplayClusterSyncGroup::Tick);
	CachedSyncData.Emplace(EDisplayClusterSyncGroup::PostTick);

	CachedSyncDataEvents.Emplace(EDisplayClusterSyncGroup::PreTick, FPlatformProcess::CreateSynchEvent(true));
	CachedSyncDataEvents.Emplace(EDisplayClusterSyncGroup::Tick, FPlatformProcess::CreateSynchEvent(true));
	CachedSyncDataEvents.Emplace(EDisplayClusterSyncGroup::PostTick, FPlatformProcess::CreateSynchEvent(true));

	CachedInputDataEvent  = FPlatformProcess::CreateSynchEvent(true);
	CachedDeltaTimeEvent  = FPlatformProcess::CreateSynchEvent(true);
	CachedEventsDataEvent = FPlatformProcess::CreateSynchEvent(true);
	CachedFrameTimeEvent  = FPlatformProcess::CreateSynchEvent(true);
}

FDisplayClusterClusterNodeCtrlMaster::~FDisplayClusterClusterNodeCtrlMaster()
{
	delete CachedInputDataEvent;
	delete CachedDeltaTimeEvent;
	delete CachedEventsDataEvent;
	delete CachedFrameTimeEvent;

	for (auto& it : CachedSyncDataEvents)
	{
		delete it.Value;
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolClusterSync
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlMaster::GetDeltaTime(float& DeltaSeconds)
{
	if (IsInGameThread())
	{
		// Cache data so it will be the same for all requests within current frame
		CachedDeltaTime = FApp::GetDeltaTime();
		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetDeltaTime cached values: DeltaSeconds %f"), CachedDeltaTime);
		CachedDeltaTimeEvent->Trigger();
	}

	// Wait until data is available
	CachedDeltaTimeEvent->Wait();

	// Return cached value
	DeltaSeconds = CachedDeltaTime;
}

void FDisplayClusterClusterNodeCtrlMaster::GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime)
{
	if (IsInGameThread())
	{
		CachedFrameTime = FApp::GetCurrentFrameTime();

		if (CachedFrameTime.IsSet())
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetFrameTime cached values: Seconds %f"), CachedFrameTime.GetValue().AsSeconds());
		}
		else
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetFrameTime cached values: [INVALID]"));
		}

		CachedFrameTimeEvent->Trigger();
	}

	// Wait until data is available
	CachedFrameTimeEvent->Wait();

	// Return cached value
	FrameTime = CachedFrameTime;
}

void FDisplayClusterClusterNodeCtrlMaster::GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();

	if (IsInGameThread())
	{
		// Cache data so it will be the same for all requests within current frame
		TMap<FString, FString> SyncDataToCache;
		ClusterMgr->ExportSyncData(SyncDataToCache, SyncGroup);
		CachedSyncData.Emplace(SyncGroup, SyncDataToCache);

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetSyncData cached values amount: %d"), SyncDataToCache.Num());

		int i = 0;
		for (auto it = SyncDataToCache.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetSyncData cached value %d: %s - %s"), i++, *it->Key, *it->Value);
		}

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("SyncData has %d records"), CachedSyncData[SyncGroup].Num());

		// Notify data is available
		CachedSyncDataEvents[SyncGroup]->Trigger();
	}

	// Wait until data is available
	CachedSyncDataEvents[SyncGroup]->Wait();

	// Return cached value
	SyncData = CachedSyncData[SyncGroup];
}

void FDisplayClusterClusterNodeCtrlMaster::GetInputData(TMap<FString, FString>& InputData)
{
	static IPDisplayClusterInputManager* const InputMgr = GDisplayCluster->GetPrivateInputMgr();

	if (IsInGameThread())
	{
		// Cache data so it will be the same for all requests within current frame
		InputMgr->ExportInputData(CachedInputData);

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetInputData cached values amount: %d"), CachedInputData.Num());

		int i = 0;
		for (auto it = CachedInputData.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetInputData cached value %d: %s - %s"), i++, *it->Key, *it->Value);
		}

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("InputData has %d records"), CachedInputData.Num());

		// Notify data is available
		CachedInputDataEvent->Trigger();
	}

	// Wait until data is available
	CachedInputDataEvent->Wait();

	// Return cached value
	InputData = CachedInputData;
}

void FDisplayClusterClusterNodeCtrlMaster::GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents)
{
	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();

	if (IsInGameThread())
	{
		// Cache data so it will be the same for all requests within current frame
		ClusterMgr->ExportEventsData(CachedJsonEvents, CachedBinaryEvents);

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetEventsData cached values amount: json=%d, binary=%d"), CachedJsonEvents.Num(), CachedBinaryEvents.Num());

		// Notify data is available
		CachedEventsDataEvent->Trigger();
	}

	// Wait until data is available
	CachedEventsDataEvent->Wait();

	// Return cached value
	JsonEvents   = CachedJsonEvents;
	BinaryEvents = CachedBinaryEvents;
}

void FDisplayClusterClusterNodeCtrlMaster::GetNativeInputData(TMap<FString, FString>& NativeInputData)
{
	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();
	ClusterMgr->SyncNativeInput(NativeInputData);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterNodeController
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlMaster::ClearCache()
{
	FScopeLock Lock(&InternalsSyncScope);

	// Reset all cache events
	CachedDeltaTimeEvent->Reset();
	CachedFrameTimeEvent->Reset();
	CachedEventsDataEvent->Reset();
	CachedInputDataEvent->Reset();

	// Reset cache containers
	CachedInputData.Reset();
	CachedJsonEvents.Reset();
	CachedBinaryEvents.Reset();

	for (auto& it : CachedSyncDataEvents)
	{
		it.Value->Reset();
	}

	for (auto& it : CachedSyncData)
	{
		it.Value.Reset();
	}

	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("FDisplayClusterClusterNodeCtrlMaster has cleaned the cache"));
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
	const UDisplayClusterConfigurationClusterNode* CfgMaster = GDisplayCluster->GetPrivateConfigMgr()->GetMasterNode();
	if (!CfgMaster)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No master node configuration data found"));
		return false;
	}

	// Allow children to override master's address
	FString HostToUse = CfgMaster->Host;
	OverrideMasterAddr(HostToUse);

	FDisplayClusterConfigurationMasterNodePorts Ports = ConfigData->Cluster->MasterNode.Ports;
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Servers: addr %s, port_cs %d, port_ss %d, port_ce %d, port_ceb %d"), *HostToUse, Ports.ClusterSync, Ports.RenderSync, Ports.ClusterEventsJson, Ports.ClusterEventsBinary);

	// Start the servers
	return StartServerWithLogs(ClusterSyncServer.Get(),         HostToUse, Ports.ClusterSync)
		&& StartServerWithLogs(RenderSyncServer.Get(),          HostToUse, Ports.RenderSync)
		&& StartServerWithLogs(ClusterEventsJsonServer.Get(),   HostToUse, Ports.ClusterEventsJson)
		&& StartServerWithLogs(ClusterEventsBinaryServer.Get(), HostToUse, Ports.ClusterEventsBinary);
}

void FDisplayClusterClusterNodeCtrlMaster::StopServers()
{
	FDisplayClusterClusterNodeCtrlSlave::StopServers();

	ClusterSyncServer->Shutdown();
	RenderSyncServer->Shutdown();
	ClusterEventsJsonServer->Shutdown();
	ClusterEventsBinaryServer->Shutdown();
}

bool FDisplayClusterClusterNodeCtrlMaster::InitializeClients()
{
	if (!FDisplayClusterClusterNodeCtrlSlave::InitializeClients())
	{
		return false;
	}

	// Master clients initialization
	// ...

	return true;
}

bool FDisplayClusterClusterNodeCtrlMaster::StartClients()
{
	if (!FDisplayClusterClusterNodeCtrlSlave::StartClients())
	{
		return false;
	}

	// Master clients start
	// ...

	return true;
}

void FDisplayClusterClusterNodeCtrlMaster::StopClients()
{
	FDisplayClusterClusterNodeCtrlSlave::StopClients();

	// Master clients stop
	// ...
}
