// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Controller/DisplayClusterNodeCtrlBase.h"


/**
 * Standalone node controller (no cluster)
 */
class FDisplayClusterNodeCtrlStandalone
	: public FDisplayClusterNodeCtrlBase
{
public:
	FDisplayClusterNodeCtrlStandalone(const FString& ctrlName, const FString& nodeName);
	virtual ~FDisplayClusterNodeCtrlStandalone();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsSlave() const override final
	{ return false; }

	virtual bool IsStandalone() const override final
	{ return true; }
};
