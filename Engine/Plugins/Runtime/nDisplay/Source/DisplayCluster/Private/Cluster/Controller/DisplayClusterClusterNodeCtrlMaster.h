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
	virtual void GetTimeData(float& InOutDeltaTime, double& InOutGameTime, TOptional<FQualifiedFrameTime>& InOutFrameTime) override;
	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) override;
	virtual void GetNativeInputData(TMap<FString, FString>& EventsData) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void ClearCache() override;

	virtual EDisplayClusterNodeRole GetClusterRole() const override
	{
		return EDisplayClusterNodeRole::Master;
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
	// GetTimeData internals
	FEvent* CachedTimeDataEvent = nullptr;
	float   CachedDeltaTime = 0.f;
	double  CachedGameTime = 0.f;
	TOptional<FQualifiedFrameTime>  CachedFrameTime;

	// GetSyncData internals
	TMap<EDisplayClusterSyncGroup, FEvent*> CachedSyncDataEvents;
	TMap<EDisplayClusterSyncGroup, TMap<FString, FString>> CachedSyncData;

	// GetEventsData internals
	FEvent* CachedEventsDataEvent = nullptr;
	TArray<TSharedPtr<FDisplayClusterClusterEventJson,   ESPMode::ThreadSafe>>   CachedJsonEvents;
	TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>> CachedBinaryEvents;

private:
	mutable FCriticalSection InternalsSyncScope;
};
