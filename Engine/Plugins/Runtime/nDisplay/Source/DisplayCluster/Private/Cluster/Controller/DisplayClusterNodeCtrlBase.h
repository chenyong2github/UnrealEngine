// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cluster/Controller/IDisplayClusterNodeController.h"

class FDisplayClusterClusterManager;
class FDisplayClusterServer;
class IDisplayClusterClient;
class IDisplayClusterServer;
class FDisplayClusterClusterEventsJsonClient;
class FDisplayClusterClusterEventsBinaryClient;


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
	virtual ~FDisplayClusterNodeCtrlBase();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override final;
	virtual void Release() override final;

	virtual FString GetNodeId() const override final
	{
		return NodeName;
	}

	virtual FString GetControllerName() const override final
	{
		return ControllerName;
	}

	virtual void SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly) override;
	virtual void SendClusterEventTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) override;

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
	virtual void WaitForGameStart() override
	{ }

	virtual void WaitForFrameStart() override
	{ }

	virtual void WaitForFrameEnd() override
	{ }

	virtual void GetTimeData(float& InOutDeltaTime, double& InOutGameTime, TOptional<FQualifiedFrameTime>& InOutFrameTime) override
	{ }

	virtual void GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup) override
	{ }

	virtual void GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents) override
	{ }

	virtual void GetNativeInputData(TMap<FString, FString>& NativeInputData) override
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProtocolRenderSync - default overrides
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForSwapSync() override
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

	// JSON client for sending events outside of the cluster
	FCriticalSection ExternEventsClientJsonGuard;
	TUniquePtr<FDisplayClusterClusterEventsJsonClient>   ExternalEventsClientJson;

	// Binary client for sending events outside of the cluster
	FCriticalSection ExternEventsClientBinaryGuard;
	TUniquePtr<FDisplayClusterClusterEventsBinaryClient> ExternalEventsClientBinary;
};
