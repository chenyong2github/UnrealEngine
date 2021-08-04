// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/DisplayClusterClusterManager.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/DisplayClusterClusterEventHandler.h"

#include "Cluster/IDisplayClusterClusterSyncObject.h"
#include "Cluster/IDisplayClusterClusterEventListener.h"
#include "Cluster/Controller/DisplayClusterClusterNodeCtrlMaster.h"
#include "Cluster/Controller/DisplayClusterClusterNodeCtrlSlave.h"
#include "Cluster/Controller/DisplayClusterClusterNodeCtrlEditor.h"

#include "Config/IPDisplayClusterConfigManager.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Dom/JsonObject.h"

#include "Misc/DisplayClusterTypesConverter.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "UObject/Interface.h"


FDisplayClusterClusterManager::FDisplayClusterClusterManager()
{
	ObjectsToSync.Emplace(EDisplayClusterSyncGroup::PreTick).Reserve(64);
	ObjectsToSync.Emplace(EDisplayClusterSyncGroup::Tick).Reserve(64);
	ObjectsToSync.Emplace(EDisplayClusterSyncGroup::PostTick).Reserve(64);

	// Set cluster event handlers. These are the entry points for any incoming cluster events.
	OnClusterEventJson.AddRaw(this, &FDisplayClusterClusterManager::OnClusterEventJsonHandler);
	OnClusterEventBinary.AddRaw(this, &FDisplayClusterClusterManager::OnClusterEventBinaryHandler);

	// Set internal system events handler
	OnClusterEventJson.Add(FDisplayClusterClusterEventHandler::Get().GetJsonListenerDelegate());

	NativeInputDataAvailableEvent = FPlatformProcess::GetSynchEventFromPool(true);
}

FDisplayClusterClusterManager::~FDisplayClusterClusterManager()
{
	FPlatformProcess::ReturnSynchEventToPool(NativeInputDataAvailableEvent);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterClusterManager::Init(EDisplayClusterOperationMode OperationMode)
{
	CurrentOperationMode = OperationMode;
	return true;
}

void FDisplayClusterClusterManager::Release()
{
}

bool FDisplayClusterClusterManager::StartSession(UDisplayClusterConfigurationData* InConfigData, const FString& InNodeId)
{
	ClusterNodeId = InNodeId;

	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Node ID: %s"), *ClusterNodeId);

	// Node name must be valid
	if (ClusterNodeId.IsEmpty())
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Node ID was not specified"));
		return false;
	}

	// Get configuration data
	const UDisplayClusterConfigurationData* ConfigData = GDisplayCluster->GetPrivateConfigMgr()->GetConfig();
	if (!ConfigData)
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Couldn't get configuration data"));
		return false;
	}

	if (!ConfigData->Cluster->Nodes.Contains(ClusterNodeId))
	{
		UE_LOG(LogDisplayClusterCluster, Error, TEXT("Node '%s' not found in the configuration data"), *ClusterNodeId);
		return false;
	}

	// Save node IDs
	ConfigData->Cluster->Nodes.GenerateKeyArray(ClusterNodeIds);

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
	{
		FScopeLock Lock(&InternalsSyncScope);

		if (Controller)
		{
			Controller->Release();
			Controller.Reset();
		}
	}

	ClusterNodeIds.Reset();
	ClusterNodeId.Empty();
}

bool FDisplayClusterClusterManager::StartScene(UWorld* InWorld)
{
	check(InWorld);
	CurrentWorld = InWorld;

	return true;
}

void FDisplayClusterClusterManager::EndScene()
{
	{
		FScopeLock Lock(&ObjectsToSyncCritSec);
		for (auto& SyncGroupPair : ObjectsToSync)
		{
			SyncGroupPair.Value.Reset();
		}
	}

	{
		FScopeLock Lock(&ClusterEventListenersCritSec);
		ClusterEventListeners.Reset();
	}

	NativeInputDataCache.Reset();
	CurrentWorld = nullptr;
}

void FDisplayClusterClusterManager::EndFrame(uint64 FrameNum)
{
	if (Controller)
	{
		Controller->ClearCache();
	}

	NativeInputDataAvailableEvent->Reset();
}

void FDisplayClusterClusterManager::PreTick(float DeltaSeconds)
{
	// Move cluster events from the primary pool to the output pool.
	// They will be replicated on the current frame.

	// Json events
	{
		FScopeLock Lock(&ClusterEventsJsonCritSec);

		ClusterEventsJsonPoolOut.Reset();
		ClusterEventsJsonPoolOut = ClusterEventsJsonPoolMain;
		ClusterEventsJsonPoolMain.Reset();

		ClusterEventsJsonNonDiscardedPoolOut.Reset();
		ClusterEventsJsonNonDiscardedPoolOut = ClusterEventsJsonNonDiscardedPoolMain;
		ClusterEventsJsonNonDiscardedPoolMain.Reset();
	}

	// Binary events
	{
		FScopeLock Lock(&ClusterEventsBinaryCritSec);

		ClusterEventsBinaryPoolOut.Reset();
		ClusterEventsBinaryPoolOut = ClusterEventsBinaryPoolMain;
		ClusterEventsBinaryPoolMain.Reset();

		ClusterEventsBinaryNonDiscardedPoolOut.Reset();
		ClusterEventsBinaryNonDiscardedPoolOut = ClusterEventsBinaryNonDiscardedPoolMain;
		ClusterEventsBinaryNonDiscardedPoolMain.Reset();
	}

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
IDisplayClusterNodeController* FDisplayClusterClusterManager::GetController() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return Controller ? Controller.Get() : nullptr;
}

bool FDisplayClusterClusterManager::IsMaster() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return Controller ? Controller->GetClusterRole() == EDisplayClusterNodeRole::Master : false;
}

bool FDisplayClusterClusterManager::IsSlave() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return Controller ? Controller->GetClusterRole() == EDisplayClusterNodeRole::Slave : false;
}

bool FDisplayClusterClusterManager::IsBackup() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return Controller ? Controller->GetClusterRole() == EDisplayClusterNodeRole::Backup : false;
}

EDisplayClusterNodeRole FDisplayClusterClusterManager::GetClusterRole() const
{
	FScopeLock Lock(&InternalsSyncScope);
	return Controller ? Controller->GetClusterRole() : EDisplayClusterNodeRole::None;
}

void FDisplayClusterClusterManager::GetNodeIds(TArray<FString>& OutNodeIds) const
{
	FScopeLock Lock(&InternalsSyncScope);
	OutNodeIds = ClusterNodeIds;
}

void FDisplayClusterClusterManager::RegisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj, EDisplayClusterSyncGroup SyncGroup)
{
	FScopeLock Lock(&ObjectsToSyncCritSec);

	if (SyncObj)
	{
		ObjectsToSync[SyncGroup].Add(SyncObj);
		UE_LOG(LogDisplayClusterCluster, Log, TEXT("Registered sync object: %s"), *SyncObj->GetSyncId());
	}
}

void FDisplayClusterClusterManager::UnregisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj)
{
	if (SyncObj)
	{
		FScopeLock Lock(&ObjectsToSyncCritSec);

		for (auto& GroupPair : ObjectsToSync)
		{
			GroupPair.Value.Remove(SyncObj);
		}

		UE_LOG(LogDisplayClusterCluster, Log, TEXT("Unregistered sync object: %s"), *SyncObj->GetSyncId());
	}
}

void FDisplayClusterClusterManager::AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener)
{
	FScopeLock Lock(&ClusterEventListenersCritSec);
	if (Listener.GetObject() && !Listener.GetObject()->IsPendingKillOrUnreachable())
	{
		ClusterEventListeners.Add(Listener);
	}
}

void FDisplayClusterClusterManager::RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener)
{
	FScopeLock Lock(&ClusterEventListenersCritSec);
	if (ClusterEventListeners.Contains(Listener))
	{
		ClusterEventListeners.Remove(Listener);
		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Cluster event listeners left: %d"), ClusterEventListeners.Num());
	}
}

void FDisplayClusterClusterManager::AddClusterEventJsonListener(const FOnClusterEventJsonListener& Listener)
{
	FScopeLock Lock(&ClusterEventListenersCritSec);
	OnClusterEventJson.Add(Listener);
}

void FDisplayClusterClusterManager::RemoveClusterEventJsonListener(const FOnClusterEventJsonListener& Listener)
{
	FScopeLock Lock(&ClusterEventListenersCritSec);
	OnClusterEventJson.Remove(Listener.GetHandle());
}

void FDisplayClusterClusterManager::AddClusterEventBinaryListener(const FOnClusterEventBinaryListener& Listener)
{
	FScopeLock Lock(&ClusterEventListenersCritSec);
	OnClusterEventBinary.Add(Listener);
}

void FDisplayClusterClusterManager::RemoveClusterEventBinaryListener(const FOnClusterEventBinaryListener& Listener)
{
	FScopeLock Lock(&ClusterEventListenersCritSec);
	OnClusterEventBinary.Remove(Listener.GetHandle());
}

void FDisplayClusterClusterManager::EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event, bool bMasterOnly)
{
	FScopeLock Lock(&ClusterEventsJsonCritSec);

	if (CurrentOperationMode == EDisplayClusterOperationMode::Cluster || CurrentOperationMode == EDisplayClusterOperationMode::Editor)
	{
		// [Master] Since we receive cluster events asynchronously, we push it to a primary events pool
		if (IsMaster())
		{
			// Generate event ID
			const FString EventId = FString::Printf(TEXT("%s-%s-%s"), *Event.Category, *Event.Type, *Event.Name);
			// Make it shared ptr
			TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe> EventPtr = MakeShared<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>(Event);
			// Store event object
			if (EventPtr->bShouldDiscardOnRepeat)
			{
				ClusterEventsJsonPoolMain.FindOrAdd(EventPtr->bIsSystemEvent).Emplace(EventId, EventPtr);
			}
			else
			{
				ClusterEventsJsonNonDiscardedPoolMain.Add(EventPtr);
			}
		}
		// [Slave] Send event to the master
		else
		{
			// An event will be emitted from a slave node if it's explicitly specified by MasterOnly=false
			if (!bMasterOnly && Controller)
			{
				Controller->EmitClusterEventJson(Event);
			}
		}
	}
}

void FDisplayClusterClusterManager::EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly)
{
	FScopeLock Lock(&ClusterEventsBinaryCritSec);

	if (CurrentOperationMode == EDisplayClusterOperationMode::Cluster || CurrentOperationMode == EDisplayClusterOperationMode::Editor)
	{
		// [Master] Since we receive cluster events asynchronously, we push it to a primary events pool
		if (IsMaster())
		{
			// Make it shared ptr
			TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe> EventPtr = MakeShared<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>(Event);

			if (EventPtr->bShouldDiscardOnRepeat)
			{
				ClusterEventsBinaryPoolMain.FindOrAdd(EventPtr->bIsSystemEvent).Emplace(EventPtr->EventId, EventPtr);
			}
			else
			{
				ClusterEventsBinaryNonDiscardedPoolMain.Add(EventPtr);
			}
		}
		// [Slave] Send event to the master
		else
		{
			// An event will be emitted from a slave node if it's explicitly specified by MasterOnly=false
			if (!bMasterOnly && Controller)
			{
				Controller->EmitClusterEventBinary(Event);
			}
		}
	}
}

void FDisplayClusterClusterManager::SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly)
{
	if (CurrentOperationMode == EDisplayClusterOperationMode::Cluster || CurrentOperationMode == EDisplayClusterOperationMode::Editor)
	{
		if (Controller)
		{
			if (IsMaster() || !bMasterOnly)
			{
				Controller->SendClusterEventTo(Address, Port, Event, bMasterOnly);
			}
		}
	}
}

void FDisplayClusterClusterManager::SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly)
{
	if (CurrentOperationMode == EDisplayClusterOperationMode::Cluster || CurrentOperationMode == EDisplayClusterOperationMode::Editor)
	{
		if (Controller)
		{
			if (IsMaster() || !bMasterOnly)
			{
				Controller->SendClusterEventTo(Address, Port, Event, bMasterOnly);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterManager::ExportSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) const
{
	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Exporting sync data for sync group: %d, items to sync: %d"), (int)SyncGroup, ObjectsToSync[SyncGroup].Num());

	{
		FScopeLock Lock(&ObjectsToSyncCritSec);

		SyncData.Reset();

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

void FDisplayClusterClusterManager::ImportSyncData(const TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
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

void FDisplayClusterClusterManager::ExportEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents)
{
	// Clear output containers
	JsonEvents.Reset();
	BinaryEvents.Reset();

	// Export JSON events
	{
		FScopeLock Lock(&ClusterEventsJsonCritSec);

		// Export all system and non-system json events that have 'discard on repeat' flag
		for (const auto& it : ClusterEventsJsonPoolOut)
		{
			TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>> JsonEventsToExport;
			it.Value.GenerateValueArray(JsonEventsToExport);
			JsonEvents.Append(JsonEventsToExport);
		}

		// Export all json events that don't have 'discard on repeat' flag
		JsonEvents.Append(ClusterEventsJsonNonDiscardedPoolOut);
	}

	// Export binary events
	{
		FScopeLock Lock(&ClusterEventsBinaryCritSec);

		// Export all binary events that have 'discard on repeat' flag
		for (const auto& it : ClusterEventsBinaryPoolOut)
		{
			TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>> BinaryEventsToExport;
			it.Value.GenerateValueArray(BinaryEventsToExport);
			BinaryEvents.Append(BinaryEventsToExport);
		}

		// Export all binary events that don't have 'discard on repeat' flag
		BinaryEvents.Append(ClusterEventsBinaryNonDiscardedPoolOut);
	}
}

void FDisplayClusterClusterManager::ImportEventsData(const TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, const TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents)
{
	// Process and fire all JSON events
	if (JsonEvents.Num() > 0)
	{
		FScopeLock LockEvents(&ClusterEventsJsonCritSec);
		FScopeLock LockListeners(&ClusterEventListenersCritSec);

		for (const auto& it : JsonEvents)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Processing json event %s|%s|%s|s%d|d%d..."), *it->Category, *it->Type, *it->Name, it->bIsSystemEvent ? 1 : 0, it->bShouldDiscardOnRepeat ? 1 : 0);
			// Fire event
			OnClusterEventJson.Broadcast(*it);
		}
	}

	// Process and fire all binary events
	if (BinaryEvents.Num() > 0)
	{
		FScopeLock LockEvents(&ClusterEventsBinaryCritSec);
		FScopeLock LockListeners(&ClusterEventListenersCritSec);

		for (const auto& it : BinaryEvents)
		{
			UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Processing binary event %d..."), it->EventId);
			// Fire event
			OnClusterEventBinary.Broadcast(*it);
		}
	}
}

void FDisplayClusterClusterManager::SyncObjects(EDisplayClusterSyncGroup SyncGroup)
{
	if (Controller)
	{
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
}

void FDisplayClusterClusterManager::SyncEvents()
{
	if (Controller)
	{
		TArray<TSharedPtr<FDisplayClusterClusterEventJson,   ESPMode::ThreadSafe>> JsonEvents;
		TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>> BinaryEvents;

		// Get events data from a provider
		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading synchronization data (events)..."));
		Controller->GetEventsData(JsonEvents, BinaryEvents);
		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading finished. Available events: json=%d binary=%d"), JsonEvents.Num(), BinaryEvents.Num());

		// Import and process them
		ImportEventsData(JsonEvents, BinaryEvents);
	}
}

void FDisplayClusterClusterManager::ProvideNativeInputData(const TMap<FString, FString>& NativeInputData)
{
	UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("SyncNativeInput - data available trigger. NativeInput records amount %d"), NativeInputData.Num());

	NativeInputDataCache = NativeInputData;
	NativeInputDataAvailableEvent->Trigger();
}

void FDisplayClusterClusterManager::SyncNativeInput(TMap<FString, FString>& NativeInputData)
{
	if (IsMaster())
	{
		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Returning native input data, records amount %d"), NativeInputDataCache.Num());

		NativeInputDataAvailableEvent->Wait();

		NativeInputData = NativeInputDataCache;
	}
	else
	{
		UE_LOG(LogDisplayClusterCluster, Verbose, TEXT("Downloading native input data..."));
		if (Controller)
		{
			Controller->GetNativeInputData(NativeInputData);
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterClusterManager
//////////////////////////////////////////////////////////////////////////////////////////////
TUniquePtr<IDisplayClusterNodeController> FDisplayClusterClusterManager::CreateController() const
{
	UE_LOG(LogDisplayClusterCluster, Log, TEXT("Current operation mode: %s"), *DisplayClusterTypesConverter::template ToString(CurrentOperationMode));

	// Instantiate appropriate controller depending on operation mode and cluster role
	FDisplayClusterNodeCtrlBase* NewController = nullptr;
	if (CurrentOperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Master or slave
		if (ClusterNodeId.Equals(GDisplayCluster->GetPrivateConfigMgr()->GetMasterNodeId(), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDisplayClusterCluster, Log, TEXT("Instantiating cluster master controller..."));
			NewController = new FDisplayClusterClusterNodeCtrlMaster(FString("[CTRL-M]"), ClusterNodeId);
		}
		else
		{
			UE_LOG(LogDisplayClusterCluster, Log, TEXT("Instantiating cluster slave controller..."));
			NewController = new FDisplayClusterClusterNodeCtrlSlave(FString("[CTRL-S]"), ClusterNodeId);
		}
	}
	else if (CurrentOperationMode == EDisplayClusterOperationMode::Editor)
	{
		UE_LOG(LogDisplayClusterCluster, Log, TEXT("Instantiating editor controller..."));
		NewController = new FDisplayClusterClusterNodeCtrlEditor(FString("[CTRL-EDTR]"), FString("editor"));
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
	return TUniquePtr<IDisplayClusterNodeController>(NewController);
}

void FDisplayClusterClusterManager::OnClusterEventJsonHandler(const FDisplayClusterClusterEventJson& Event)
{
	FScopeLock Lock(&ClusterEventListenersCritSec);

	decltype(ClusterEventListeners) InvalidListeners;

	for (auto Listener : ClusterEventListeners)
	{
		if (!Listener.GetObject() || Listener.GetObject()->IsPendingKillOrUnreachable()) // Note: .GetInterface() is always returning null when intefrace is added to class in the Blueprint.
		{
			UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Will remove invalid cluster event listener"));
			InvalidListeners.Add(Listener);
			continue;
		}
		Listener->Execute_OnClusterEventJson(Listener.GetObject(), Event);
	}

	for (auto& InvalidListener : InvalidListeners)
	{
		ClusterEventListeners.Remove(InvalidListener);
	}
}

void FDisplayClusterClusterManager::OnClusterEventBinaryHandler(const FDisplayClusterClusterEventBinary& Event)
{
	FScopeLock Lock(&ClusterEventListenersCritSec);

	decltype(ClusterEventListeners) InvalidListeners;

	for (auto Listener : ClusterEventListeners)
	{
		if (!Listener.GetObject() || Listener.GetObject()->IsPendingKillOrUnreachable()) // Note: .GetInterface() is always returning null when intefrace is added to class in the Blueprint.
		{
			UE_LOG(LogDisplayClusterCluster, Warning, TEXT("Will remove invalid cluster event listener"));
			InvalidListeners.Add(Listener);
			continue;
		}

		Listener->Execute_OnClusterEventBinary(Listener.GetObject(), Event);
	}

	for (auto& InvalidListener : InvalidListeners)
	{
		ClusterEventListeners.Remove(InvalidListener);
	}
}
