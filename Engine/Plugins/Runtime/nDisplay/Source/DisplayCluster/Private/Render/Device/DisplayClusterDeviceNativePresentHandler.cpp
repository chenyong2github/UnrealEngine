// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterDeviceNativePresentHandler.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterDeviceNativePresentHandler::FDisplayClusterDeviceNativePresentHandler()
	: FDisplayClusterDeviceBase(1)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceNativePresentHandler::~FDisplayClusterDeviceNativePresentHandler()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


bool FDisplayClusterDeviceNativePresentHandler::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy = GDisplayCluster->GetRenderMgr()->GetCurrentSynchronizationPolicy();
	if (SyncPolicy.IsValid())
	{
		InOutSyncInterval = GetSwapInt();

		// If the synchronization object hasn't presented the frame, let the Engine do it
		return SyncPolicy->SynchronizeClusterRendering(InOutSyncInterval);
	}

	return true;
}
