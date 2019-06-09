// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareBase.h"


/**
 * DX12 network synchronization policy
 */
class FDisplayClusterRenderSyncPolicySoftwareDX12
	: public FDisplayClusterRenderSyncPolicySoftwareBase
{
public:
	FDisplayClusterRenderSyncPolicySoftwareDX12();
	virtual ~FDisplayClusterRenderSyncPolicySoftwareDX12();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;
};
