// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Network/DisplayClusterMessage.h"
#include "Misc/App.h"

class ADisplayClusterGameMode;
class ADisplayClusterSettings;
class FJsonObject;
class FEvent;


/**
 * Cluster manager. Responsible for network communication and data replication.
 */
class FDisplayClusterClusterManager
	: public    IPDisplayClusterClusterManager
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
	virtual bool StartSession(const FString& configPath, const FString& nodeId) override;
	virtual void EndSession() override;
	virtual bool StartScene(UWorld* pWorld) override;
	virtual void EndScene() override;
	virtual void EndFrame(uint64 FrameNum) override;
	virtual void PreTick(float DeltaSeconds) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostTick(float DeltaSeconds) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsMaster()     const override;
	virtual bool IsSlave()      const override;
	virtual bool IsStandalone() const override;
	virtual bool IsCluster()    const override;

	virtual FString GetNodeId() const override
	{ return ClusterNodeId; }

	virtual uint32 GetNodesAmount() const override
	{ return NodesAmount; }

	virtual void RegisterSyncObject(IDisplayClusterClusterSyncObject* pSyncObj, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void UnregisterSyncObject(IDisplayClusterClusterSyncObject* pSyncObj) override;

	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener>) override;
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener>) override;

	virtual void AddClusterEventListener(const FOnClusterEventListener& Listener) override;
	virtual void RemoveClusterEventListener(const FOnClusterEventListener& Listener) override;

	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual IPDisplayClusterNodeController* GetController() const override;

	virtual void ExportSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) const override;
	virtual void ImportSyncData(const FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) override;

	virtual void ExportEventsData(FDisplayClusterMessage::DataType& EventsData) const override;
	virtual void ImportEventsData(const FDisplayClusterMessage::DataType& EventsData) override;

	virtual void SyncObjects(EDisplayClusterSyncGroup SyncGroup) override;
	virtual void SyncInput()   override;
	virtual void SyncEvents()  override;

	virtual void ProvideNativeInputData(const TMap<FString, FString>& NativeInputData) override;
	virtual void SyncNativeInput(TMap<FString, FString>& NativeInputData) override;

private:
	bool GetResolvedNodeId(FString& id) const;

	typedef TUniquePtr<IPDisplayClusterNodeController> TController;

	// Factory method
	TController CreateController() const;

	void OnClusterEventHandler(const FDisplayClusterClusterEvent& Event);

private:
	// Controller implementation
	TController Controller;
	// Cluster/node props
	uint32 NodesAmount = 0;

	// Current operation mode
	EDisplayClusterOperationMode CurrentOperationMode;
	// Current config path
	FString ConfigPath;
	// Current node ID
	FString ClusterNodeId;
	// Current world
	UWorld* CurrentWorld;

	// Sync transforms
	TMap<EDisplayClusterSyncGroup, TSet<IDisplayClusterClusterSyncObject*>> ObjectsToSync;
	mutable FCriticalSection                     ObjectsToSyncCritSec;

	// Sync events - types
	typedef TMap<FString, FDisplayClusterClusterEvent> FNamedEventMap;
	typedef TMap<FString, FNamedEventMap>              FTypedEventMap;
	typedef TMap<FString, FTypedEventMap>              FCategoricalMap;
	typedef FCategoricalMap                            FClusterEventsContainer;
	// Sync events - data
	FClusterEventsContainer                      ClusterEventsPoolMain;
	mutable FClusterEventsContainer              ClusterEventsPoolOut;
	mutable FDisplayClusterMessage::DataType     ClusterEventsCacheOut;
	mutable FCriticalSection                     ClusterEventsCritSec;
	FOnClusterEvent                              OnClusterEvent;
	TArray<TScriptInterface<IDisplayClusterClusterEventListener>> ClusterEventListeners;
	// Sync native input
	FEvent* NativeInputDataAvailableEvent = nullptr;
	TMap<FString, FString> NativeInputDataCache;

	mutable FCriticalSection InternalsSyncScope;
};
