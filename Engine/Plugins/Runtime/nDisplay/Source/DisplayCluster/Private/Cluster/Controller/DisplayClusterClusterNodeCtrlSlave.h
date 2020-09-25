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
	// IDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsSlave() const override
	{
		return true;
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolClusterSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForGameStart(double* ThreadWaitTime, double* BarrierWaitTime) override final;
	virtual void WaitForFrameStart(double* ThreadWaitTime, double* BarrierWaitTime) override final;
	virtual void WaitForFrameEnd(double* ThreadWaitTime, double* BarrierWaitTime) override final;
	virtual void GetDeltaTime(float& DeltaSeconds) override;
	virtual void GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime) override;
	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) override;
	virtual void GetInputData(TMap<FString, FString>& InputData) override;
	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents) override;
	virtual void GetNativeInputData(TMap<FString, FString>& NativeInputData) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolRenderSync
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForSwapSync(double* ThreadWaitTime, double* BarrierWaitTime) override final;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsJson
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsBinary
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event) override;

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
