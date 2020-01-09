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
	FEvent* CachedDeltaTimeEvent = nullptr;
	float   CachedDeltaTime = 0.f;

	// GetTimecode internals
	FEvent* CachedTimeCodeFrameRateEvent = nullptr;
	FTimecode  CachedTimecode;
	FFrameRate CachedFramerate;

	// GetSyncData internals
	TMap<EDisplayClusterSyncGroup, FEvent*> CachedSyncDataEvents;
	TMap<EDisplayClusterSyncGroup, FDisplayClusterMessage::DataType> CachedSyncData;

	// GetInputData internals
	FEvent* CachedInputDataEvent = nullptr;
	FDisplayClusterMessage::DataType CachedInputData;

	// GetEventsData internals
	FEvent* CachedEventsDataEvent = nullptr;
	FDisplayClusterMessage::DataType CachedEventsData;

private:
	mutable FCriticalSection InternalsSyncScope;
};

