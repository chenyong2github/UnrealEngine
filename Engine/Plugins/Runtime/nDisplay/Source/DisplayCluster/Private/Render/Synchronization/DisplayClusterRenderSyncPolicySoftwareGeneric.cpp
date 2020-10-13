// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareGeneric.h"


// Empty by default. Use initialization list to add some params. Used by FDisplayClusterRenderManager
TMap<FString, FString> FDisplayClusterRenderSyncPolicySoftwareGeneric::DefaultParameters;


FDisplayClusterRenderSyncPolicySoftwareGeneric::FDisplayClusterRenderSyncPolicySoftwareGeneric(const TMap<FString, FString>& Parameters)
	: FDisplayClusterRenderSyncPolicySoftwareBase(Parameters)
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
