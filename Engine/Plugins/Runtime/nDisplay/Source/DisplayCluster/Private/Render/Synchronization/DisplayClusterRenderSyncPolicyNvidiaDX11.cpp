// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaDX11.h"


FDisplayClusterRenderSyncPolicyNvidiaDX11::FDisplayClusterRenderSyncPolicyNvidiaDX11()
{
}

FDisplayClusterRenderSyncPolicyNvidiaDX11::~FDisplayClusterRenderSyncPolicyNvidiaDX11()
{
}


bool FDisplayClusterRenderSyncPolicyNvidiaDX11::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	//@todo implement hardware based solution
	SyncBarrierRenderThread();
	// Tell a caller that he still needs to present a frame
	return true;
}
