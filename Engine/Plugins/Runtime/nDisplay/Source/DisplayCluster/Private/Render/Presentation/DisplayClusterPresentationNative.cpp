// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterPresentationNative.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterPresentationNative::FDisplayClusterPresentationNative(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
	: FDisplayClusterPresentationBase(Viewport, SyncPolicy)
{
}

FDisplayClusterPresentationNative::~FDisplayClusterPresentationNative()
{
}


bool FDisplayClusterPresentationNative::Present(int32& InOutSyncInterval)
{
	TSharedPtr<IDisplayClusterRenderSyncPolicy> CurSyncPolicy = GetSyncPolicyObject();
	if (CurSyncPolicy.IsValid())
	{
		// Update swap interval with internal settings
		InOutSyncInterval = GetSwapInt();
		// If the synchronization object hasn't presented the frame, let the Engine do it
		return CurSyncPolicy->SynchronizeClusterRendering(InOutSyncInterval);
	}

	return true;
}
