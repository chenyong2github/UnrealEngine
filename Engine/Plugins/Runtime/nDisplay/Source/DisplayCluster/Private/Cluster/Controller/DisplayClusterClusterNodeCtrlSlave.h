// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlBase.h"

class FDisplayClusterClusterSyncClient;
class FDisplayClusterRenderSyncClient;
class FDisplayClusterClusterEventsJsonClient;
class FDisplayClusterClusterEventsBinaryClient;


/**
 * Slave node controller implementation (cluster mode). . Manages clients on client side.
 */
class FDisplayClusterClusterNodeCtrlSlave
	: public FDisplayClusterClusterNodeCtrlBase
{
public:
	FDisplayClusterClusterNodeCtrlSlave(const FString& CtrlName, const FString& NodeName);
	virtual ~FDisplayClusterClusterNodeCtrlSlave();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual EDisplayClusterNodeRole GetClusterRole() const override
	{
		return EDisplayClusterNodeRole::Slave;
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolClusterSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual EDisplayClusterCommResult WaitForGameStart() override final;
	virtual EDisplayClusterCommResult WaitForFrameStart() override final;
	virtual EDisplayClusterCommResult WaitForFrameEnd() override final;
	virtual EDisplayClusterCommResult GetTimeData(double& OutDeltaTime, double& OutGameTime, TOptional<FQualifiedFrameTime>& OutFrameTime) override;
	virtual EDisplayClusterCommResult GetObjectsData(const EDisplayClusterSyncGroup InSyncGroup, TMap<FString, FString>& OutObjectsData) override;
	virtual EDisplayClusterCommResult GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& OutJsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& OutBinaryEvents) override;
	virtual EDisplayClusterCommResult GetNativeInputData(TMap<FString, FString>& OutNativeInputData) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolRenderSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual EDisplayClusterCommResult WaitForSwapSync() override final;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsJson
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual EDisplayClusterCommResult EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsBinary
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual EDisplayClusterCommResult EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event) override;

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
	// Cluster node clients
	TUniquePtr<FDisplayClusterClusterSyncClient>         ClusterSyncClient;
	TUniquePtr<FDisplayClusterRenderSyncClient>          RenderSyncClient;
	TUniquePtr<FDisplayClusterClusterEventsJsonClient>   ClusterEventsJsonClient;
	TUniquePtr<FDisplayClusterClusterEventsBinaryClient> ClusterEventsBinaryClient;
};
