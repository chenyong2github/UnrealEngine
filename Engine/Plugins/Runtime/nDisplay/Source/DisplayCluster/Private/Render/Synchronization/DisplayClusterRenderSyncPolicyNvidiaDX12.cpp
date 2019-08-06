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
	// Unfortunately NVIDIA doesn't export required functions in the public SDK. This make impossible
	// to implement the hardware base synchronization approach. As a fallback we use our own soft sync.
	// As soon as the required functions become available, the proper synchonization approach will be implemented.
	SyncBarrierRenderThread();
	// Tell a caller that he still needs to present a frame
	return true;
}
