// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cluster/Controller/DisplayClusterClusterNodeCtrlSlave.h"

class FDisplayClusterClusterSyncService;
class FDisplayClusterRenderSyncService;
class FDisplayClusterClusterEventsJsonService;
class FDisplayClusterClusterEventsBinaryService;


/**
 * Master node controller implementation (cluster mode). Manages servers on master side.
 */
class FDisplayClusterClusterNodeCtrlMaster
	: public FDisplayClusterClusterNodeCtrlSlave
{
public:
	FDisplayClusterClusterNodeCtrlMaster(const FString& CtrlName, const FString& NodeName);
	virtual ~FDisplayClusterClusterNodeCtrlMaster();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolClusterSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void GetDeltaTime(float& DeltaSeconds) override;
	virtual void GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime) override;
	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void GetInputData(TMap<FString, FString>& InputData) override;
	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents) override;
	virtual void GetNativeInputData(TMap<FString, FString>& EventsData) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void ClearCache() override;

	virtual bool IsSlave() const override final
	{
		return false;
	}

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterNodeCtrlBase
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool InitializeServers() override;
	virtual bool StartServers()      override;
	virtual void StopServers()       override;

	virtual bool InitializeClients() override;
	virtual bool StartClients()      override;
	virtual void StopClients()       override;

private:
	// Node servers
	TUniquePtr<FDisplayClusterClusterSyncService>         ClusterSyncServer;
	TUniquePtr<FDisplayClusterRenderSyncService>          RenderSyncServer;
	TUniquePtr<FDisplayClusterClusterEventsJsonService>   ClusterEventsJsonServer;
	TUniquePtr<FDisplayClusterClusterEventsBinaryService> ClusterEventsBinaryServer;

private:
	// GetDeltaTime internals
	FEvent* CachedDeltaTimeEvent = nullptr;
	float   CachedDeltaTime = 0.f;

	// GetTimecode internals
	FEvent* CachedFrameTimeEvent = nullptr;
	TOptional<FQualifiedFrameTime>  CachedFrameTime;

	// GetSyncData internals
	TMap<EDisplayClusterSyncGroup, FEvent*> CachedSyncDataEvents;
	TMap<EDisplayClusterSyncGroup, TMap<FString, FString>> CachedSyncData;

	// GetInputData internals
	FEvent* CachedInputDataEvent = nullptr;
	TMap<FString, FString> CachedInputData;

	// GetEventsData internals
	FEvent* CachedEventsDataEvent = nullptr;
	TArray<TSharedPtr<FDisplayClusterClusterEventJson>>   CachedJsonEvents;
	TArray<TSharedPtr<FDisplayClusterClusterEventBinary>> CachedBinaryEvents;

private:
	mutable FCriticalSection InternalsSyncScope;
};
