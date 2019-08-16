// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaBase.h"


/**
 * DX12 NVIDIA SwapLock synchronization policy
 */
class FDisplayClusterRenderSyncPolicyNvidiaDX12
	: public FDisplayClusterRenderSyncPolicyNvidiaBase
{
public:
	FDisplayClusterRenderSyncPolicyNvidiaDX12();
	virtual ~FDisplayClusterRenderSyncPolicyNvidiaDX12();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;
};
