// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/SidebySide/DisplayClusterDeviceSideBySideDX12.h"
#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "DisplayClusterLog.h"


FDisplayClusterDeviceSideBySideDX12::FDisplayClusterDeviceSideBySideDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceSideBySideDX12::~FDisplayClusterDeviceSideBySideDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceSideBySideDX12::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX12(Viewport, SyncPolicy);
}
