// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX11.h"

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Device/DisplayClusterRenderViewport.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterDeviceMonoscopicDX11::FDisplayClusterDeviceMonoscopicDX11()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceMonoscopicDX11::~FDisplayClusterDeviceMonoscopicDX11()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

bool FDisplayClusterDeviceMonoscopicDX11::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	FD3D11Viewport* Viewport = static_cast<FD3D11Viewport*>(MainViewport->GetViewportRHI().GetReference());
	check(Viewport);

#if !WITH_EDITOR
	// Issue frame event
	Viewport->IssueFrameEvent();
	// Wait until GPU finish last frame commands
	Viewport->WaitForFrameEventCompletion();
#endif

	// Update sync value with nDisplay value
	InOutSyncInterval = GetSwapInt();

	TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy = GDisplayCluster->GetRenderMgr()->GetCurrentSynchronizationPolicy();
	if (SyncPolicy.IsValid())
	{
		// False results means we don't need to present current frame, the sync object already presented it
		if (!SyncPolicy->SynchronizeClusterRendering(InOutSyncInterval))
		{
			return false;
		}
	}

	// Sync policy haven't presented current frame so we do it ourselves
	return PresentImpl(Viewport, InOutSyncInterval);
}
