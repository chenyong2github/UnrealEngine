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
	virtual ~FDisplayClusterRenderSyncPolicyNvidiaBase();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;
	virtual FName GetName() const override;

private:
	bool InitializeNvidiaSwapLock();

private:
	bool bNvApiInitialized = false;
	bool bNvApiBarrierSet  = false;

	bool bNvDiagInit       = true;
	bool bNvDiagPresent    = true;
	bool bNvDiagWaitQueue  = false;
	bool bNvDiagCompletion = false;

	uint32 NvPresentBarrierCount      = 0;
	uint32 NvPresentBarrierCountLimit = 0;
};
