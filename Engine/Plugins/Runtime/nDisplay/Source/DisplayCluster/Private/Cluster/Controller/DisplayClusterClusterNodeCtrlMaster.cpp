// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlMaster.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Input/IPDisplayClusterInputManager.h"

#include "Network/Service/ClusterSync/DisplayClusterClusterSyncService.h"
#include "Network/Service/SwapSync/DisplayClusterSwapSyncService.h"
#include "Network/Service/ClusterEvents/DisplayClusterClusterEventsService.h"

#include "Engine/World.h"
#include "HAL/Event.h"

#include "Misc/App.h"

#include "DisplayClusterPlayerInput.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterClusterNodeCtrlMaster::FDisplayClusterClusterNodeCtrlMaster(const FString& ctrlName, const FString& nodeName) :
	FDisplayClusterClusterNodeCtrlSlave(ctrlName, nodeName)
{
	IsDataCached_GetSyncData.Emplace(EDisplayClusterSyncGroup::PreTick,  false);
	IsDataCached_GetSyncData.Emplace(EDisplayClusterSyncGroup::Tick,     false);
	IsDataCached_GetSyncData.Emplace(EDisplayClusterSyncGroup::PostTick, false);

	CachedSyncData_GetSyncData.Emplace(EDisplayClusterSyncGroup::PreTick);
	CachedSyncData_GetSyncData.Emplace(EDisplayClusterSyncGroup::Tick);
	CachedSyncData_GetSyncData.Emplace(EDisplayClusterSyncGroup::PostTick);
}

FDisplayClusterClusterNodeCtrlMaster::~FDisplayClusterClusterNodeCtrlMaster()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterSyncProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlMaster::GetDeltaTime(float& DeltaSeconds)
{
	FScopeLock lock(&InternalsSyncScope);

	// Cache data so it will be the same for all requests within current frame
	if (!bIsDataCached_GetDeltaTime)
	{
		bIsDataCached_GetDeltaTime = true;

		CachedDeltaTime_GetDeltaTime = FApp::GetDeltaTime();

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetDeltaTime cached values: DeltaSeconds %f"), CachedDeltaTime_GetDeltaTime);
	}

	// Return cached value
	DeltaSeconds = CachedDeltaTime_GetDeltaTime;
}

void FDisplayClusterClusterNodeCtrlMaster::GetTimecode(FTimecode& Timecode, FFrameRate& FrameRate)
{
	FScopeLock lock(&InternalsSyncScope);

	// Cache data so it will be the same for all requests within current frame
	if (!bIsDataCached_GetTimecode)
	{
		bIsDataCached_GetTimecode = true;

		// This values are updated in UEngine::UpdateTimeAndHandleMaxTickRate (via UpdateTimecode).
		CachedTimecode_GetTimecode  = FApp::GetTimecode();
		CachedFramerate_GetTimecode = FApp::GetTimecodeFrameRate();

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetDeltaTime cached values: DeltaSeconds %f"), CachedDeltaTime_GetDeltaTime);
	}

	// Return cached value
	Timecode  = CachedTimecode_GetTimecode;
	FrameRate = CachedFramerate_GetTimecode;
}

void FDisplayClusterClusterNodeCtrlMaster::GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	FScopeLock lock(&InternalsSyncScope);

	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();

	// Cache data so it will be the same for all requests within current frame
	if (!IsDataCached_GetSyncData[SyncGroup])
	{
		IsDataCached_GetSyncData[SyncGroup] = true;

		FDisplayClusterMessage::DataType SyncDataToCache;
		ClusterMgr->ExportSyncData(SyncDataToCache, SyncGroup);
		CachedSyncData_GetSyncData.Emplace(SyncGroup, SyncDataToCache);

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetSyncData cached values amount: %d"), SyncDataToCache.Num());

		int i = 0;
		for (auto it = SyncDataToCache.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetSyncData cached value %d: %s - %s"), i++, *it->Key, *it->Value);
		}
	}

	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("SyncData has %d records"), CachedSyncData_GetSyncData[SyncGroup].Num());

	// Return cached value
	SyncData = CachedSyncData_GetSyncData[SyncGroup];
}

void FDisplayClusterClusterNodeCtrlMaster::GetInputData(FDisplayClusterMessage::DataType& InputData)
{
	FScopeLock lock(&InternalsSyncScope);

	static IPDisplayClusterInputManager* const InputMgr = GDisplayCluster->GetPrivateInputMgr();

	// Cache data so it will be the same for all requests within current frame
	if (!bIsDataCached_GetInputData)
	{
		bIsDataCached_GetInputData = true;

		InputMgr->ExportInputData(CachedInputData_GetInputData);

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetInputData cached values amount: %d"), CachedInputData_GetInputData.Num());

		int i = 0;
		for (auto it = CachedInputData_GetInputData.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetInputData cached value %d: %s - %s"), i++, *it->Key, *it->Value);
		}
	}

	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("InputData has %d records"), CachedInputData_GetInputData.Num());

	// Return cached value
	InputData = CachedInputData_GetInputData;
}

void FDisplayClusterClusterNodeCtrlMaster::GetEventsData(FDisplayClusterMessage::DataType& EventsData)
{
	FScopeLock lock(&InternalsSyncScope);

	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();

	// Cache data so it will be the same for all requests within current frame
	if (!bIsDataCached_GetEventsData)
	{
		bIsDataCached_GetEventsData = true;

		ClusterMgr->ExportEventsData(CachedEventsData_GetEventsData);

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetEventsData cached values amount: %d"), CachedEventsData_GetEventsData.Num());

		int i = 0;
		for (auto it = CachedEventsData_GetEventsData.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetEventsData cached value %d: %s - %s"), i++, *it->Key, *it->Value);
		}
	}

	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("EventsData has %d records"), CachedEventsData_GetEventsData.Num());

	// Return cached value
	EventsData = CachedEventsData_GetEventsData;
}

void FDisplayClusterClusterNodeCtrlMaster::GetNativeInputData(FDisplayClusterMessage::DataType& NativeInputData)
{
	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();
	ClusterMgr->SyncNativeInput(NativeInputData);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterNodeController
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlMaster::ClearCache()
{
	FScopeLock lock(&InternalsSyncScope);

	// Reset all cache flags
	bIsDataCached_GetDeltaTime  = false;
	bIsDataCached_GetTimecode   = false;
	bIsDataCached_GetInputData  = false;
	bIsDataCached_GetEventsData = false;

	// Reset cache containers
	CachedInputData_GetInputData.Reset();
	CachedEventsData_GetEventsData.Reset();

	for (auto& it : IsDataCached_GetSyncData)
	{
		it.Value = false;
	}

	for (auto& it : CachedSyncData_GetSyncData)
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

	// Get config data
	FDisplayClusterConfigClusterNode masterCfg;
	if (GDisplayCluster->GetPrivateConfigMgr()->GetMasterClusterNode(masterCfg) == false)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No master node configuration data found"));
		return false;
	}

	// Instantiate node servers
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Servers: addr %s, port_cs %d, port_ss %d, port_ce %d"), *masterCfg.Addr, masterCfg.Port_CS, masterCfg.Port_SS, masterCfg.Port_CE);
	ClusterSyncServer.Reset(new FDisplayClusterClusterSyncService(masterCfg.Addr, masterCfg.Port_CS));
	SwapSyncServer.Reset(new FDisplayClusterSwapSyncService(masterCfg.Addr, masterCfg.Port_SS));
	ClusterEventsServer.Reset(new FDisplayClusterClusterEventsService(masterCfg.Addr, masterCfg.Port_CE));

	return ClusterSyncServer.IsValid() && SwapSyncServer.IsValid() && ClusterEventsServer.IsValid();
}

bool FDisplayClusterClusterNodeCtrlMaster::StartServers()
{
	if (!FDisplayClusterClusterNodeCtrlSlave::StartServers())
	{
		return false;
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("%s - starting master servers..."), *GetControllerName());

	// Start the servers
	return StartServerWithLogs(ClusterSyncServer.Get()) && StartServerWithLogs(SwapSyncServer.Get()) && StartServerWithLogs(ClusterEventsServer.Get());
}

void FDisplayClusterClusterNodeCtrlMaster::StopServers()
{
	FDisplayClusterClusterNodeCtrlSlave::StopServers();

	ClusterSyncServer->Shutdown();
	SwapSyncServer->Shutdown();
	ClusterEventsServer->Shutdown();
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

