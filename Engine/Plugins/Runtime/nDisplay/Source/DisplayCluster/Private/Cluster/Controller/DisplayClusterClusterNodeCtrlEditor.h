// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlMaster.h"

#include "Network/DisplayClusterMessage.h"

class FDisplayClusterClusterEventsClient;
class FDisplayClusterClusterEventsService;


/**
 * Editor node controller implementation.
 */
class FDisplayClusterClusterNodeCtrlEditor
	: public FDisplayClusterClusterNodeCtrlMaster
{
public:
	FDisplayClusterClusterNodeCtrlEditor(const FString& ctrlName, const FString& nodeName);
	virtual ~FDisplayClusterClusterNodeCtrlEditor();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterNodeCtrlBase
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void OverrideMasterAddr(FString& Addr) override;
};
