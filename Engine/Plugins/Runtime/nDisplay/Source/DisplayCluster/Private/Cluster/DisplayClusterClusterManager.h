// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Misc/App.h"

class ADisplayClusterSettings;
class FJsonObject;
class FEvent;


/**
 * Cluster manager. Responsible for network communication and data replication.
 */
class FDisplayClusterClusterManager
	: public IPDisplayClusterClusterManager
{
public:
	FDisplayClusterClusterManager();
	virtual ~FDisplayClusterClusterManager();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Init(EDisplayClusterOperationMode OperationMode) override;
	virtual void Release() override;
	virtual bool StartSession(UDisplayClusterConfigurationData* InConfigData, const FString& NodeID) override;
	virtual void EndSession() override;
	virtual bool StartScene(UWorld* World) override;
	virtual void EndScene() override;
	virtual void EndFrame(uint64 FrameNum) override;
	virtual void PreTick(float DeltaSeconds) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostTick(float DeltaSeconds) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsMaster() const override;
	virtual bool IsSlave()  const override;
	virtual bool IsBackup() const override;
	virtual EDisplayClusterNodeRole GetClusterRole() const override;

	virtual FString GetNodeId() const override
	{
		return ClusterNodeId;
	}

	virtual uint32 GetNodesAmount() const override
	{
		return static_cast<uint32>(ClusterNodeIds.Num());
	}

	virtual void GetNodeIds(TArray<FString>& OutNodeIds) const override;

	virtual void RegisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void UnregisterSyncObject(IDisplayClusterClusterSyncObject* SyncObj) override;

	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener>) override;
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener>) override;

	virtual void AddClusterEventJsonListener(const FOnClusterEventJsonListener& Listener) override;
	virtual void RemoveClusterEventJsonListener(const FOnClusterEventJsonListener& Listener) override;

	virtual void AddClusterEventBinaryListener(const FOnClusterEventBinaryListener& Listener) override;
	virtual void RemoveClusterEventBinaryListener(const FOnClusterEventBinaryListener& Listener) override;

	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event, bool MasterOnly) override;
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) override;

	virtual void SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event,   bool bMasterOnly) override;
	virtual void SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual IDisplayClusterNodeController* GetController() const override;

	virtual void ExportSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) const override;
	virtual void ImportSyncData(const TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) override;

	virtual void ExportEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) override;
	virtual void ImportEventsData(const TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, const TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) override;

	virtual void SyncObjects(EDisplayClusterSyncGroup SyncGroup) override;
	virtual void SyncEvents()  override;

	virtual void ProvideNativeInputData(const TMap<FString, FString>& NativeInputData) override;
	virtual void SyncNativeInput(TMap<FString, FString>& NativeInputData) override;

private:
	// Factory method
	TUniquePtr<IDisplayClusterNodeController> CreateController() const;

	void OnClusterEventJsonHandler(const FDisplayClusterClusterEventJson& Event);
	void OnClusterEventBinaryHandler(const FDisplayClusterClusterEventBinary& Event);

private:
	// Controller implementation
	TUniquePtr<IDisplayClusterNodeController> Controller;

	// Custom list of cluster nodes
	TArray<FString> ClusterNodeIds;

	// Current operation mode
	EDisplayClusterOperationMode CurrentOperationMode;
	// Current node ID
	FString ClusterNodeId;
	// Current world
	UWorld* CurrentWorld;

	// Sync transforms
	TMap<EDisplayClusterSyncGroup, TSet<IDisplayClusterClusterSyncObject*>> ObjectsToSync;
	mutable FCriticalSection ObjectsToSyncCritSec;

	// Sync json events - data
	TMap<bool, TMap<FString, TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>> ClusterEventsJsonPoolMain;
	TMap<bool, TMap<FString, TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>> ClusterEventsJsonPoolOut;
	TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>> ClusterEventsJsonNonDiscardedPoolMain;
	TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>> ClusterEventsJsonNonDiscardedPoolOut;
	mutable FCriticalSection     ClusterEventsJsonCritSec;
	FOnClusterEventJson          OnClusterEventJson;
	
	// Sync binary events
	TMap<bool, TMap<int32, TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>> ClusterEventsBinaryPoolMain;
	TMap<bool, TMap<int32, TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>> ClusterEventsBinaryPoolOut;
	TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>> ClusterEventsBinaryNonDiscardedPoolMain;
	TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>> ClusterEventsBinaryNonDiscardedPoolOut;
	mutable FCriticalSection   ClusterEventsBinaryCritSec;
	FOnClusterEventBinary      OnClusterEventBinary;

	// Cluster event listeners
	mutable FCriticalSection ClusterEventListenersCritSec;
	TArray<TScriptInterface<IDisplayClusterClusterEventListener>> ClusterEventListeners;

	// Sync native input
	FEvent* NativeInputDataAvailableEvent = nullptr;
	TMap<FString, FString> NativeInputDataCache;

	mutable FCriticalSection InternalsSyncScope;
};
