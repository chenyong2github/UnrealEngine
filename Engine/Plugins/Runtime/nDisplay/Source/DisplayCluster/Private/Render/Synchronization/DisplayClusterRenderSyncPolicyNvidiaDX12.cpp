// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaDX12.h"



FDisplayClusterRenderSyncPolicyNvidiaDX12::FDisplayClusterRenderSyncPolicyNvidiaDX12()
{
}

FDisplayClusterRenderSyncPolicyNvidiaDX12::~FDisplayClusterRenderSyncPolicyNvidiaDX12()
{
}

bool FDisplayClusterRenderSyncPolicyNvidiaDX12::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	//@todo implement hardware based solution
	SyncBarrierRenderThread();
	// Tell a caller that he still needs to present a frame
	return true;
}
