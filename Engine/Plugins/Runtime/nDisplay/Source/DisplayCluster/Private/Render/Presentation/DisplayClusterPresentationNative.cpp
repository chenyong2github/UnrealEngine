// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Presentation/DisplayClusterPresentationNative.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterPresentationNative::FDisplayClusterPresentationNative(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
	: FDisplayClusterPresentationBase(Viewport, SyncPolicy)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterPresentationNative::~FDisplayClusterPresentationNative()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


bool FDisplayClusterPresentationNative::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

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
