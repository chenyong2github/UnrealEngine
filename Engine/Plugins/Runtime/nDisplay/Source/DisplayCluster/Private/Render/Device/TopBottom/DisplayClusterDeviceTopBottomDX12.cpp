// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX12.h"
#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "DisplayClusterLog.h"


FDisplayClusterDeviceTopBottomDX12::FDisplayClusterDeviceTopBottomDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceTopBottomDX12::~FDisplayClusterDeviceTopBottomDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceTopBottomDX12::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX12(Viewport, SyncPolicy);
}
