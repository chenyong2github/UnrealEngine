// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlEditor.h"


FDisplayClusterClusterNodeCtrlEditor::FDisplayClusterClusterNodeCtrlEditor(const FString& CtrlName, const FString& NodeName)
	: FDisplayClusterClusterNodeCtrlMaster(CtrlName, NodeName)
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
