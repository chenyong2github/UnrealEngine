// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareDX12.h"



FDisplayClusterRenderSyncPolicySoftwareDX12::FDisplayClusterRenderSyncPolicySoftwareDX12()
{
}

FDisplayClusterRenderSyncPolicySoftwareDX12::~FDisplayClusterRenderSyncPolicySoftwareDX12()
{
}

bool FDisplayClusterRenderSyncPolicySoftwareDX12::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	//@todo DWM based synchronization

	// As a temporary solution, synchronize render threads on a barrier only
	SyncBarrierRenderThread();
	// Tell a caller that he still needs to present a frame
	return true;
}
