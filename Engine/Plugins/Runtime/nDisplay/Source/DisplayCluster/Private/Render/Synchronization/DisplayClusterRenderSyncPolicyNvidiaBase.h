// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"


/**
 * Base NVIDIA FrameLock & SwapSync synchronization policy
 */
class FDisplayClusterRenderSyncPolicyNvidiaBase
	: public FDisplayClusterRenderSyncPolicyBase
{
public:
	FDisplayClusterRenderSyncPolicyNvidiaBase();
	virtual ~FDisplayClusterRenderSyncPolicyNvidiaBase() = 0;

protected:
	// Is NVIDIA API related initialization has been performed already
	bool bNVAPIInitialized = false;
};
