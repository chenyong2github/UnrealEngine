// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cluster/Controller/IPDisplayClusterNodeController.h"

class FDisplayClusterClusterManager;
class FDisplayClusterServer;
class FDisplayClusterClient;


/**
 * Abstract node controller
 */
class FDisplayClusterNodeCtrlBase
	: public  IPDisplayClusterNodeController
{
	// This is needed to perform initialization from outside of constructor (polymorphic init)
	friend FDisplayClusterClusterManager;

public:
	FDisplayClusterNodeCtrlBase(const FString& ctrlName, const FString& nodeName);

	virtual ~FDisplayClusterNodeCtrlBase() = 0
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override final;
	virtual void Release() override final;

	virtual bool IsMaster() const override final
	{ return !IsSlave(); }
	
	virtual bool IsCluster() const override final
	{ return !IsStandalone(); }

	virtual FString GetNodeId() const override final
	{ return NodeName; }

	virtual FString GetControllerName() const override final
	{ return ControllerName; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterEventsProtocol - default overrides
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event) override
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterSyncProtocol - default overrides
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForGameStart() override
	{ }

	virtual void WaitForFrameStart() override
	{ }

	virtual void WaitForFrameEnd() override
	{ }

	virtual void WaitForTickEnd() override
	{ }

	virtual void GetDeltaTime(float& DeltaSeconds) override
	{ }

	virtual void GetTimecode(FTimecode& Timecode, FFrameRate& FrameRate) override
	{ }

	virtual void GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup) override
	{ }

	virtual void GetInputData(FDisplayClusterMessage::DataType& InputData) override
	{ }

	virtual void GetEventsData(FDisplayClusterMessage::DataType& EventsData) override
	{ }

	virtual void GetNativeInputData(FDisplayClusterMessage::DataType& NativeInputData) override
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterSwapSyncProtocol - default overrides
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForSwapSync(double* pThreadWaitTime, double* pBarrierWaitTime) override
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
	bool StartServerWithLogs(FDisplayClusterServer* Server) const;
	bool StartClientWithLogs(FDisplayClusterClient* Client, const FString& Addr, int32 Port, int32 ClientConnTriesAmount, int32 ClientConnRetryDelay) const;

private:
	const FString NodeName;
	const FString ControllerName;
};

