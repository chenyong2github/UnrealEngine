// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaBase.h"


/**
 * DX12 NVIDIA SwapLock synchronization policy
 */
class FDisplayClusterRenderSyncPolicyNvidiaDX12
	: public FDisplayClusterRenderSyncPolicyNvidiaBase
{
public:
	FDisplayClusterRenderSyncPolicyNvidiaDX12(const TMap<FString, FString>& Parameters)
		: FDisplayClusterRenderSyncPolicyNvidiaBase(Parameters)
	{ }

	virtual ~FDisplayClusterRenderSyncPolicyNvidiaDX12()
	{ }

protected:
	virtual void WaitForFrameCompletion() override;
};
