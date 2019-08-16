// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"


/**
 * Synchronization policy - None (no synchronization)
 */
class FDisplayClusterRenderSyncPolicyNone
	: public FDisplayClusterRenderSyncPolicyBase
{
public:
	FDisplayClusterRenderSyncPolicyNone();
	virtual ~FDisplayClusterRenderSyncPolicyNone();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;
};
