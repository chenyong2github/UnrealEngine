// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Controller/DisplayClusterClusterNodeCtrlMaster.h"


/**
 * Editor node controller implementation.
 */
class FDisplayClusterClusterNodeCtrlEditor
	: public FDisplayClusterClusterNodeCtrlMaster
{
public:
	FDisplayClusterClusterNodeCtrlEditor(const FString& CtrlName, const FString& NodeName);
	virtual ~FDisplayClusterClusterNodeCtrlEditor();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterNodeCtrlBase
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void OverrideMasterAddr(FString& Addr) override;
};
