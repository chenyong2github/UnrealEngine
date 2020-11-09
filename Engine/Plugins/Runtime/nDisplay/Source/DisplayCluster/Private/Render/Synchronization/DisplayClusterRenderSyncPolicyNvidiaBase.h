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
	FDisplayClusterRenderSyncPolicyNvidiaBase(const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterRenderSyncPolicyNvidiaBase() = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;

private:
	bool InitializeNvidiaSwapLock();

private:
	bool bNvApiInitialized = false;
	bool bNvApiBarrierSet  = false;

	bool bNvDiagInit    = false;
	bool bNvDiagPresent = false;
};
