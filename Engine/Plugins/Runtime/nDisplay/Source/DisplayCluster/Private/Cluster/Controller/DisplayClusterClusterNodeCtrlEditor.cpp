// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlEditor.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Network/Service/ClusterEvents/DisplayClusterClusterEventsClient.h"
#include "Network/Service/ClusterEvents/DisplayClusterClusterEventsService.h"

#include "IPDisplayCluster.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterClusterNodeCtrlEditor::FDisplayClusterClusterNodeCtrlEditor(const FString& ctrlName, const FString& nodeName)
	: FDisplayClusterClusterNodeCtrlMaster(ctrlName, nodeName)
{
}

FDisplayClusterClusterNodeCtrlEditor::~FDisplayClusterClusterNodeCtrlEditor()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterNodeCtrlBase
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterNodeCtrlEditor::OverrideMasterAddr(FString& Addr)
{
	// Editor controller uses 127.0.0.1 only
	Addr = FString("127.0.0.1");
}
