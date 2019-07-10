// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareGeneric.h"



FDisplayClusterRenderSyncPolicySoftwareGeneric::FDisplayClusterRenderSyncPolicySoftwareGeneric()
{
}

FDisplayClusterRenderSyncPolicySoftwareGeneric::~FDisplayClusterRenderSyncPolicySoftwareGeneric()
{
}

bool FDisplayClusterRenderSyncPolicySoftwareGeneric::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	// Synchronize by a barrier only
	SyncBarrierRenderThread();
	// Tell a caller that he is still need to present a frame
	return true;
}
