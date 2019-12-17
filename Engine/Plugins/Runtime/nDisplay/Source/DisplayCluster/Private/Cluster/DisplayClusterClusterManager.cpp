// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Cluster/DisplayClusterClusterManager.h"

#include "Cluster/IDisplayClusterClusterSyncObject.h"
#include "Cluster/IDisplayClusterClusterEventListener.h"
#include "Cluster/Controller/DisplayClusterNodeCtrlStandalone.h"
#include "Cluster/Controller/DisplayClusterClusterNodeCtrlMaster.h"
#include "Cluster/Controller/DisplayClusterClusterNodeCtrlSlave.h"
#include "Cluster/Controller/DisplayClusterClusterNodeCtrlEditor.h"

#include "Config/IPDisplayClusterConfigManager.h"

#include "Dom/JsonObject.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"

#include "Input/IPDisplayClusterInputManager.h"

#include "UObject/Interface.h"

#include "DisplayClusterBuildConfig.h"
#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"
#include "DisplayClusterStrings.h"

#include "SocketSubsystem.h"


FDisplayClusterClusterManager::FDisplayClusterClusterManager()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	ObjectsToSync.Emplace(EDisplayClusterSyncGroup::PreTick).Reserve(64);
	ObjectsToSync.Emplace(EDisplayClusterSyncGroup::Tick).Reserve(64);
	ObjectsToSync.Emplace(EDisplayClusterSyncGroup::PostTick).Reserve(64);

	// Set main event handler. This is an entry point for any incoming cluster events.
	OnClusterEvent.AddRaw(this, &FDisplayClusterClusterManager::OnClusterEventHandler);

	NativeInputDataAvailableEvent = FPlatformProcess::CreateSynchEvent(true);
}

FDisplayClusterClusterManager::~FDisplayClusterClusterManager()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);
	delete NativeInputDataAvailableEvent;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterClusterManager::Init(EDisplayClusterOperationMode OperationMode)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	CurrentOperationMode = OperationMode;
	
	return true;
}

void FDisplayClusterClusterManager::Release()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);
}

bool FDisplayClusterClusterManager::StartSession(const FString& configPath, const FString& nodeId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	ConfigPath = configPath;
	ClusterNodeId = nodeId;

	if (CurrentOperationMode == EDisplayClusterOperationMode::Cluster)
	{
#ifdef DISPLAY_CLUSTER_USE_AUTOMATIC_NODE_ID_RESOLVE
		if (ClusterNodeId.IsEmpty())
		{
			UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Node name was not specified. Trying to resolve address from available interfaces..."));

			// Try to find the node ID by address (this won't work if you want to run several cluster nodes on the same address)
			FString resolvedNodeId;
			if (GetResolvedNodeId(resolvedNodeId))
			{
				DisplayClusterHelpers::str::TrimStringValue(resolvedNodeId);
				ClusterNodeId = resolvedNodeId;
			}
			else
			{
				UE_LOG(LogDisplayClusterCluster, Error, TEXT("Unable to resolve node ID by local addresses"));
				return false;
			}
		}
#endif
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Standalone)
	{
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Editor)
	{
		if (ConfigPath.IsEmpty() || ClusterNodeId.IsEmpty())
		{
			UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Wrong config path and/or node ID. Using default standalone config."));

#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
			ConfigPath = FString(DisplayClusterStrings::misc::DbgStubConfig);
			ClusterNodeId     = FString(DisplayClusterStrings::misc::DbgStubNodeId);
#endif
		}
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Disabled)
	{
		return true;
	}
	else
	{
		UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Unknown operation mode"));
		return false;
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Node ID: %s"), *ClusterNodeId);

	// Node name must be specified in cluster mode
	if (ClusterNodeId.IsEmpty())
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Node name was not specified"));
		return false;
	}

	// Save nodes amount
	NodesAmount = GDisplayCluster->GetPrivateConfigMgr()->GetClusterNodesAmount();

	// Instantiate node controller
	Controller = CreateController();
	if (!Controller)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't create a controller."));
		return false;
	}

	// Initialize the controller
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Initializing the controller..."));
	if (!Controller->Initialize())
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't initialize a controller."));
		Controller.Reset();
		return false;
	}

	return true;
}

void FDisplayClusterClusterManager::EndSession()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&InternalsSyncScope);
		if (Controller)
		{
			Controller->Release();
			Controller.Reset();
		}
	}
}

bool FDisplayClusterClusterManager::StartScene(UWorld* pWorld)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	check(pWorld);
	CurrentWorld = pWorld;

	return true;
}

void FDisplayClusterClusterManager::EndScene()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ObjectsToSyncCritSec);
		for (auto& SyncGroupPair: ObjectsToSync)
		{
			SyncGroupPair.Value.Reset();
		}
	}
}

void FDisplayClusterClusterManager::EndFrame(uint64 FrameNum)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	if (Controller)
	{
		Controller->ClearCache();
	}

	NativeInputDataAvailableEvent->Reset();
}

void FDisplayClusterClusterManager::PreTick(float DeltaSeconds)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	// Move cluster events from the primary pool to the output pool. These will be synchronized on the current frame.
	{
		FScopeLock lock(&ClusterEventsCritSec);

		// Clear the output pool since we have all data cached already
		ClusterEventsPoolOut.Empty(ClusterEventsPoolOut.Num() | 0x07);
		ClusterEventsPoolOut = MoveTemp(ClusterEventsPoolMain);
		ClusterEventsPoolMain.Empty(ClusterEventsPoolOut.Num() | 0x07);
	}

	// Update input state in the cluster
	SyncInput();

	// Sync cluster objects (PreTick)
	SyncObjects(EDisplayClusterSyncGroup::PreTick);

	// Sync cluster events
	SyncEvents();
}

void FDisplayClusterClusterManager::Tick(float DeltaSeconds)
{
	// Sync cluster objects (Tick)
	SyncObjects(EDisplayClusterSyncGroup::Tick);
}

void FDisplayClusterClusterManager::PostTick(float DeltaSeconds)
{
	// Sync cluster objects (PostTick)
	SyncObjects(EDisplayClusterSyncGroup::PostTick);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
IPDisplayClusterNodeController* FDisplayClusterClusterManager::GetController() const
{
	FScopeLock lock(&InternalsSyncScope);
	return Controller ? Controller.Get() : nullptr;
}

bool FDisplayClusterClusterManager::IsMaster() const
{
	return Controller ? Controller->IsMaster() : false;
}

bool FDisplayClusterClusterManager::IsSlave() const
{
	return Controller ? Controller->IsSlave() : false;
}

bool FDisplayClusterClusterManager::IsStandalone() const
{
	return Controller ? Controller->IsStandalone() : false;
}

bool FDisplayClusterClusterManager::IsCluster() const
{
	return Controller ? Controller->IsCluster() : false;
}

void FDisplayClusterClusterManager::RegisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj, EDisplayClusterSyncGroup SyncGroup)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ObjectsToSyncCritSec);

		if (SyncObj)
		{
			ObjectsToSync[SyncGroup].Add(SyncObj);
			UE_LOG(LogDisplayClusterCluster, Log, TEXT("Registered sync object: %s"), *SyncObj->GetSyncId());
		}
	}
}

void FDisplayClusterClusterManager::UnregisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ObjectsToSyncCritSec);

		for (auto& GroupPair : ObjectsToSync)
		{
			GroupPair.Value.Remove(SyncObj);
		}
	}

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Unregistered sync object: %s"), *SyncObj->GetSyncId());
}

void FDisplayClusterClusterManager::AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ClusterEventsCritSec);
		ClusterEventListeners.Add(Listener);
	}
}

void FDisplayClusterClusterManager::RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ClusterEventsCritSec);
		if (ClusterEventListeners.Contains(Listener))
		{
			ClusterEventListeners.Remove(Listener);
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Cluster event listeners left: %d"), ClusterEventListeners.Num());
		}
	}
}

void FDisplayClusterClusterManager::AddClusterEventListener(const FOnClusterEventListener& Listener)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ClusterEventsCritSec);
		OnClusterEvent.Add(Listener);
	}
}

void FDisplayClusterClusterManager::RemoveClusterEventListener(const FOnClusterEventListener& Listener)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ClusterEventsCritSec);
		OnClusterEvent.Remove(Listener.GetHandle());
	}
}

void FDisplayClusterClusterManager::EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ClusterEventsCritSec);

		// [Master] Since we receive cluster events asynchronously, we push it to a primary events pool
		if (IsMaster())
		{
			// New category event
			if (!ClusterEventsPoolMain.Contains(Event.Category))
			{
				ClusterEventsPoolMain.Add(Event.Category);
				ClusterEventsPoolMain[Event.Category].Add(Event.Type);
				ClusterEventsPoolMain[Event.Category][Event.Type].Add(Event.Name, Event);
			}
			// New type event
			else if (!ClusterEventsPoolMain[Event.Category].Contains(Event.Type))
			{
				ClusterEventsPoolMain[Event.Category].Add(Event.Type);
				ClusterEventsPoolMain[Event.Category][Event.Type].Add(Event.Name, Event);
			}
			else
			{
				ClusterEventsPoolMain[Event.Category][Event.Type].Add(Event.Name, Event);
			}
		}
		// [Slave] Send event to the master
		else
		{
			// An event will be emitted from a slave node if it's explicitly specified by MasterOnly=false
			if (!MasterOnly)
			{
				Controller->EmitClusterEvent(Event);
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterManager::ExportSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Exporting sync data for sync group: %d, items to sync: %d"), (int)SyncGroup, ObjectsToSync[SyncGroup].Num());

	{
		FScopeLock lock(&ObjectsToSyncCritSec);

		SyncData.Empty(SyncData.Num() | 0x7);

		for (IDisplayClusterClusterSyncObject* SyncObj : ObjectsToSync[SyncGroup])
		{
			if (SyncObj && SyncObj->IsActive())
			{
				if (SyncObj->IsDirty())
				{
					UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Adding object to sync: %s"), *SyncObj->GetSyncId());
					SyncData.Add(SyncObj->GetSyncId(), SyncObj->SerializeToString());
					SyncObj->ClearDirty();
				}
			}
		}
	}
}

void FDisplayClusterClusterManager::ImportSyncData(const FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	if (SyncData.Num() > 0)
	{
		for (auto it = SyncData.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("sync-data: %s=%s"), *it->Key, *it->Value);
		}

		for (auto SyncObj : ObjectsToSync[SyncGroup])
		{
			if (SyncObj && SyncObj->IsActive())
			{
				const FString SyncId = SyncObj->GetSyncId();
				if (!SyncData.Contains(SyncId))
				{
					UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("%s has nothing to update"), *SyncId);
					continue;
				}

				UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Found %s in sync data. Applying..."), *SyncId);
				if (!SyncObj->DeserializeFromString(SyncData[SyncId]))
				{
					UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't apply sync data for sync object %s"), *SyncId);
				}
			}
		}
	}
}

void FDisplayClusterClusterManager::ExportEventsData(FDisplayClusterMessage::DataType& EventsData) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ClusterEventsCritSec);

		// Cache the events data for current frame.
		if (ClusterEventsPoolOut.Num() != 0)
		{
			int ObjID = 0;
			for (const auto& CategorytMap : ClusterEventsPoolOut)
			{
				for (const auto& TypeMap : CategorytMap.Value)
				{
					for (const auto& NamedEvent : TypeMap.Value)
					{
						UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Adding event to sync: %s::%s"), *NamedEvent.Value.Name, *NamedEvent.Value.Type);
						EventsData.Add(FString::Printf(TEXT("EVENT_%d"), ObjID++), NamedEvent.Value.SerializeToString());
					}
				}
			}
		}
	}
}

void FDisplayClusterClusterManager::ImportEventsData(const FDisplayClusterMessage::DataType& data)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	if (data.Num() > 0)
	{
		FScopeLock lock(&ClusterEventsCritSec);

		for (const auto& it : data)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("evt-data: %s=%s"), *it.Key, *it.Value);

			FDisplayClusterClusterEvent ClusterEvent;
			if (ClusterEvent.DeserializeFromString(it.Value) == false)
			{
				UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Couldn't deserialize cluster event: %s=%s"), *it.Key, *it.Value);
				continue;
			}

			// Fire event
			OnClusterEvent.Broadcast(ClusterEvent);
		}
	}
}

void FDisplayClusterClusterManager::SyncObjects(EDisplayClusterSyncGroup SyncGroup)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	TMap<FString, FString> SyncData;

	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading synchronization data (objects)..."));
	Controller->GetSyncData(SyncData, SyncGroup);
	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading finished. Available %d records (objects)."), SyncData.Num());

	// We don't have to import data here unless sync data provider is located on master node
	if (IsSlave())
	{
		// Perform data load (objects state update)
		ImportSyncData(SyncData, SyncGroup);
	}
}

void FDisplayClusterClusterManager::SyncInput()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	TMap<FString, FString> InputData;

	// Get input data from a provider
	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading synchronization data (input)..."));
	Controller->GetInputData(InputData);
	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading finished. Available %d records (input)."), InputData.Num());

	// We don't have to import data here unless input data provider is located on master node
	if (IsSlave())
	{
		// Perform data load (objects state update)
		GDisplayCluster->GetPrivateInputMgr()->ImportInputData(InputData);
	}
}

void FDisplayClusterClusterManager::SyncEvents()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	TMap<FString, FString> EventsData;

	// Get events data from a provider
	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading synchronization data (events)..."));
	Controller->GetEventsData(EventsData);
	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading finished. Available %d records (events)."), EventsData.Num());

	// Import and process them
	ImportEventsData(EventsData);
}

void FDisplayClusterClusterManager::ProvideNativeInputData(const TMap<FString, FString>& NativeInputData)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("SyncNativeInput - data available trigger. NativeInput records amount %d"), NativeInputData.Num());

	NativeInputDataCache = NativeInputData;
	NativeInputDataAvailableEvent->Trigger();
}

void FDisplayClusterClusterManager::SyncNativeInput(TMap<FString, FString>& NativeInputData)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	if (IsMaster())
	{
		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Returning native input data, records amount %d"), NativeInputDataCache.Num());

		NativeInputDataAvailableEvent->Wait();

		NativeInputData = NativeInputDataCache;
	}
	else
	{
		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading native input data..."));
		Controller->GetNativeInputData(NativeInputData);
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterClusterManager::TController FDisplayClusterClusterManager::CreateController() const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Current operation mode: %s"), *FDisplayClusterTypesConverter::ToString(CurrentOperationMode));

	// Instantiate appropriate controller depending on operation mode and cluster role
	FDisplayClusterNodeCtrlBase* pController = nullptr;
	if (CurrentOperationMode == EDisplayClusterOperationMode::Cluster)
	{
		FDisplayClusterConfigClusterNode nodeCfg;
		if (GDisplayCluster->GetPrivateConfigMgr()->GetClusterNode(ClusterNodeId, nodeCfg) == false)
		{
			UE_LOG(LogDisplayClusterCluster, Error, TEXT("Configuration data for node %s not found"), *ClusterNodeId);
			return nullptr;
		}

		if (nodeCfg.IsMaster)
		{
			UE_LOG(LogDisplayClusterCluster, Log, TEXT("Instantiating cluster master controller..."));
			pController = new FDisplayClusterClusterNodeCtrlMaster(FString("[CTRL-M]"), ClusterNodeId);
		}
		else
		{
			UE_LOG(LogDisplayClusterCluster, Log, TEXT("Instantiating cluster slave controller..."));
			pController = new FDisplayClusterClusterNodeCtrlSlave(FString("[CTRL-S]"), ClusterNodeId);
		}
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Standalone)
	{
		UE_LOG(LogDisplayClusterCluster, Log, TEXT("Instantiating standalone controller"));
		pController = new FDisplayClusterNodeCtrlStandalone(FString("[CTRL-STNDA]"), FString("standalone"));
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Editor)
	{
		UE_LOG(LogDisplayClusterCluster, Log, TEXT("Instantiating editor controller..."));
		pController = new FDisplayClusterClusterNodeCtrlEditor(FString("[CTRL-EDTR]"), FString("editor"));
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Disabled)
	{
		UE_LOG(LogDisplayClusterCluster, Log, TEXT("Controller is not required"));
		return nullptr;
	}
	else
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Unknown operation mode"));
		return nullptr;
	}

	// Return the controller
	return TController(pController);
}

bool FDisplayClusterClusterManager::GetResolvedNodeId(FString& id) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	TArray<TSharedPtr<FInternetAddr>> addrs;
	if (!ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(addrs))
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't get local addresses list. Cannot find node ID by its address."));
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::KillImmediately, FString("Cluster manager init error"));
		return false;
	}

	if (addrs.Num() <= 0)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("No local addresses found"));
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::KillImmediately, FString("Cluster manager init error"));
		return false;
	}

	const TArray<FDisplayClusterConfigClusterNode> cnodes = GDisplayCluster->GetPrivateConfigMgr()->GetClusterNodes();

	// Look for associated node in config
	const FDisplayClusterConfigClusterNode* const pNode = cnodes.FindByPredicate([addrs](const FDisplayClusterConfigClusterNode& node)
	{
		for (auto addr : addrs)
		{
			const FIPv4Endpoint ep(addr);
			const FString epaddr = ep.Address.ToString();
			UE_LOG(LogDisplayClusterCluster, Log, TEXT("Comparing addresses: %s - %s"), *epaddr, *node.Addr);

			//@note: don't add "127.0.0.1" or "localhost" here. There will be a bug. It has been proved already.
			if (epaddr == node.Addr)
			{
				return true;
			}
		}

		return false;
	});

	if (!pNode)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't find any local address in config file"));
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::KillImmediately, FString("Cluster manager init error"));
		return false;
	}

	// Ok, we found the node ID by address (this won't work if you want to run several cluster nodes on the same address)
	id = pNode->Id;
	return true;
}

// This is cluster events root dispatcher. It forwards events to both BP and C++ event handlers.
void FDisplayClusterClusterManager::OnClusterEventHandler(const FDisplayClusterClusterEvent& Event)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterCluster);

	{
		FScopeLock lock(&ClusterEventsCritSec);
		for (auto Listener : ClusterEventListeners)
		{
			Listener->Execute_OnClusterEvent(Listener.GetObject(), Event);
		}
	}
}
