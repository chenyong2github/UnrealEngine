// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX11.h"
#include "Render/Presentation/DisplayClusterPresentationDX11.h"

#include "DisplayClusterLog.h"


FDisplayClusterDeviceTopBottomDX11::FDisplayClusterDeviceTopBottomDX11()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceTopBottomDX11::~FDisplayClusterDeviceTopBottomDX11()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceTopBottomDX11::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX11(Viewport, SyncPolicy);
}
