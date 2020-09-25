// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNone.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterRenderSyncPolicyNone::FDisplayClusterRenderSyncPolicyNone(const TMap<FString, FString>& Parameters)
	: FDisplayClusterRenderSyncPolicyBase(Parameters)
{
}

FDisplayClusterRenderSyncPolicyNone::~FDisplayClusterRenderSyncPolicyNone()
{
}

bool FDisplayClusterRenderSyncPolicyNone::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	UE_LOG(LogDisplayClusterRenderSync, VeryVerbose, TEXT("Sync policy is 'None'. UE4 will swap the buffers with swap interval 0"));

	// Override sync interval with 0 to show a frame ASAP. We don't care about tearing in this policy.
	InOutSyncInterval = 0;
	// Tell a caller that he still needs to present a frame
	return true;
}
