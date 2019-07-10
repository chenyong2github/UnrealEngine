// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareOpenGL.h"



FDisplayClusterRenderSyncPolicySoftwareOpenGL::FDisplayClusterRenderSyncPolicySoftwareOpenGL()
{
}

FDisplayClusterRenderSyncPolicySoftwareOpenGL::~FDisplayClusterRenderSyncPolicySoftwareOpenGL()
{
}

bool FDisplayClusterRenderSyncPolicySoftwareOpenGL::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	// Synchronize by a barrier only
	SyncBarrierRenderThread();
	// Tell a caller that he is still need to present a frame
	return true;
}
