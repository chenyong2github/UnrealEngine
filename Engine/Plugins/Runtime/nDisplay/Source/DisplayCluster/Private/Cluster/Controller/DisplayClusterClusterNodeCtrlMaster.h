// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlSlave.h"

#include "Network/DisplayClusterMessage.h"


class FDisplayClusterClusterSyncService;
class FDisplayClusterSwapSyncService;
class FDisplayClusterClusterEventsService;


/**
 * Master node controller implementation (cluster mode). Manages servers on master side.
 */
class FDisplayClusterClusterNodeCtrlMaster
	: public FDisplayClusterClusterNodeCtrlSlave
{
public:
	FDisplayClusterClusterNodeCtrlMaster(const FString& ctrlName, const FString& nodeName);
	virtual ~FDisplayClusterClusterNodeCtrlMaster();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterSyncProtocol
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void GetDeltaTime(float& DeltaSeconds) override;
	virtual void GetTimecode(FTimecode& Timecode, FFrameRate& FrameRate) override;
	virtual void GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void GetInputData(FDisplayClusterMessage::DataType& InputData) override;
	virtual void GetEventsData(FDisplayClusterMessage::DataType& EventsData) override;
	virtual void GetNativeInputData(FDisplayClusterMessage::DataType& EventsData) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void ClearCache() override;

	virtual bool IsSlave() const override final
	{ return false; }

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
	TUniquePtr<FDisplayClusterClusterSyncService>   ClusterSyncServer;
	TUniquePtr<FDisplayClusterSwapSyncService>      SwapSyncServer;
	TUniquePtr<FDisplayClusterClusterEventsService> ClusterEventsServer;

private:
	// GetDeltaTime internals
	bool  bIsDataCached_GetDeltaTime = false;
	float CachedDeltaTime_GetDeltaTime = 0.f;

	// GetTimecode internals
	bool       bIsDataCached_GetTimecode = false;
	FTimecode  CachedTimecode_GetTimecode;
	FFrameRate CachedFramerate_GetTimecode;

	// GetSyncData internals
	TMap<EDisplayClusterSyncGroup, bool> IsDataCached_GetSyncData;
	TMap<EDisplayClusterSyncGroup, FDisplayClusterMessage::DataType> CachedSyncData_GetSyncData;

	// GetInputData internals
	bool bIsDataCached_GetInputData = false;
	FDisplayClusterMessage::DataType CachedInputData_GetInputData;

	// GetEventsData internals
	bool bIsDataCached_GetEventsData = false;
	FDisplayClusterMessage::DataType CachedEventsData_GetEventsData;

private:
	mutable FCriticalSection InternalsSyncScope;
};

