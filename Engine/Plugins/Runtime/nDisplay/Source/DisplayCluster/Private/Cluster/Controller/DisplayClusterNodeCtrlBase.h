// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cluster/Controller/IDisplayClusterNodeController.h"

class FDisplayClusterClusterManager;
class FDisplayClusterServer;
class IDisplayClusterClient;
class IDisplayClusterServer;



/**
 * Abstract node controller
 */
class FDisplayClusterNodeCtrlBase
	: public IDisplayClusterNodeController
{
	// This is needed to perform initialization from outside of constructor (polymorphic init)
	friend FDisplayClusterClusterManager;

public:
	FDisplayClusterNodeCtrlBase(const FString& CtrlName, const FString& NodeName);

	virtual ~FDisplayClusterNodeCtrlBase() = 0
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override final;
	virtual void Release() override final;

	virtual bool IsMaster() const override final
	{
		return !IsSlave();
	}

	virtual FString GetNodeId() const override final
	{
		return NodeName;
	}

	virtual FString GetControllerName() const override final
	{
		return ControllerName;
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsJson - default overrides
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event) override
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolEventsBinary - default overrides
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event) override
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolClusterSync - default overrides
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForGameStart(double* ThreadWaitTime, double* BarrierWaitTime) override
	{ }

	virtual void WaitForFrameStart(double* ThreadWaitTime, double* BarrierWaitTime) override
	{ }

	virtual void WaitForFrameEnd(double* ThreadWaitTime, double* BarrierWaitTime) override
	{ }

	virtual void GetDeltaTime(float& DeltaSeconds) override
	{ }

	virtual void GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime) override
	{ }

	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) override
	{ }

	virtual void GetInputData(TMap<FString, FString>& InputData) override
	{ }

	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents) override
	{ }

	virtual void GetNativeInputData(TMap<FString, FString>& NativeInputData) override
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolRenderSync - default overrides
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForSwapSync(double* ThreadWaitTime, double* BarrierWaitTime) override
	{ }

protected:
	virtual bool InitializeServers()
	{ return true; }

	virtual bool StartServers()
	{ return true; }

	virtual void StopServers()
	{ return; }

	virtual bool InitializeClients()
	{ return true; }

	virtual bool StartClients()
	{ return true; }
	
	virtual void StopClients()
	{ return; }

protected:
	bool StartServerWithLogs(IDisplayClusterServer* Server, const FString& Address, int32 Port) const;
	bool StartClientWithLogs(IDisplayClusterClient* Client, const FString& Address, int32 Port, int32 ClientConnTriesAmount, int32 ClientConnRetryDelay) const;

private:
	const FString NodeName;
	const FString ControllerName;
};
