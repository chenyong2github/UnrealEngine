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
	CachedSyncData.Emplace(EDisplayClusterSyncGroup::PreTick);
	CachedSyncData.Emplace(EDisplayClusterSyncGroup::Tick);
	CachedSyncData.Emplace(EDisplayClusterSyncGroup::PostTick);

	CachedSyncDataEvents.Emplace(EDisplayClusterSyncGroup::PreTick, FPlatformProcess::CreateSynchEvent(true));
	CachedSyncDataEvents.Emplace(EDisplayClusterSyncGroup::Tick, FPlatformProcess::CreateSynchEvent(true));
	CachedSyncDataEvents.Emplace(EDisplayClusterSyncGroup::PostTick, FPlatformProcess::CreateSynchEvent(true));

	CachedInputDataEvent  = FPlatformProcess::CreateSynchEvent(true);
	CachedDeltaTimeEvent  = FPlatformProcess::CreateSynchEvent(true);
	CachedEventsDataEvent = FPlatformProcess::CreateSynchEvent(true);
	CachedTimeCodeFrameRateEvent = FPlatformProcess::CreateSynchEvent(true);
}

FDisplayClusterClusterNodeCtrlMaster::~FDisplayClusterClusterNodeCtrlMaster()
{
	delete CachedInputDataEvent;
	delete CachedDeltaTimeEvent;
	delete CachedEventsDataEvent;
	delete CachedTimeCodeFrameRateEvent;

	for (auto& it : CachedSyncDataEvents)
	{
		delete it.Value;
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterSyncProtocol
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

void FDisplayClusterClusterNodeCtrlMaster::GetTimecode(FTimecode& Timecode, FFrameRate& FrameRate)
{
	if (IsInGameThread())
	{
		// This values are updated in UEngine::UpdateTimeAndHandleMaxTickRate (via UpdateTimecode).
		CachedTimecode  = FApp::GetTimecode();
		CachedFramerate = FApp::GetTimecodeFrameRate();

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetTimecode cached values: Timecode %s, FrameRate %s"), *CachedTimecode.ToString(), *FDisplayClusterTypesConverter::ToString(CachedFramerate));

		CachedTimeCodeFrameRateEvent->Trigger();
	}

	CachedTimeCodeFrameRateEvent->Wait();

	// Return cached value
	Timecode  = CachedTimecode;
	FrameRate = CachedFramerate;
}

void FDisplayClusterClusterNodeCtrlMaster::GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();

	if (IsInGameThread())
	{
		// Cache data so it will be the same for all requests within current frame
		FDisplayClusterMessage::DataType SyncDataToCache;
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

void FDisplayClusterClusterNodeCtrlMaster::GetInputData(FDisplayClusterMessage::DataType& InputData)
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

void FDisplayClusterClusterNodeCtrlMaster::GetEventsData(FDisplayClusterMessage::DataType& EventsData)
{
	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();

	if (IsInGameThread())
	{
		// Cache data so it will be the same for all requests within current frame
		ClusterMgr->ExportEventsData(CachedEventsData);

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetEventsData cached values amount: %d"), CachedEventsData.Num());

		int i = 0;
		for (auto it = CachedEventsData.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("GetEventsData cached value %d: %s - %s"), i++, *it->Key, *it->Value);
		}

		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("EventsData has %d records"), CachedEventsData.Num());

		// Notify data is available
		CachedEventsDataEvent->Trigger();
	}

	// Wait until data is available
	CachedEventsDataEvent->Wait();

	// Return cached value
	EventsData = CachedEventsData;
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

	// Reset all cache events
	CachedDeltaTimeEvent->Reset();
	CachedTimeCodeFrameRateEvent->Reset();
	CachedEventsDataEvent->Reset();
	CachedInputDataEvent->Reset();

	// Reset cache containers
	CachedInputData.Reset();
	CachedEventsData.Reset();

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

