// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlBase.h"

#include "Network/DisplayClusterMessage.h"

class FDisplayClusterClusterEventsClient;
class FDisplayClusterClusterEventsService;


/**
 * Editor node controller implementation.
 */
class FDisplayClusterClusterNodeCtrlEditor
	: public FDisplayClusterClusterNodeCtrlBase
{
public:
	FDisplayClusterClusterNodeCtrlEditor(const FString& ctrlName, const FString& nodeName);
	virtual ~FDisplayClusterClusterNodeCtrlEditor();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsSlave() const override final
	{ return false; }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterEventsProtocol
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event) override;

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
	// Node clients
	TUniquePtr<FDisplayClusterClusterEventsClient> ClusterEventsClient;

	// Node servers
	TUniquePtr<FDisplayClusterClusterEventsService> ClusterEventsServer;
};
