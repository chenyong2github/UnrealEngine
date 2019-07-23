// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX12.h"
#include "DisplayClusterLog.h"


FDisplayClusterDeviceTopBottomDX12::FDisplayClusterDeviceTopBottomDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceTopBottomDX12::~FDisplayClusterDeviceTopBottomDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


bool FDisplayClusterDeviceTopBottomDX12::Present(int32& InOutSyncInterval)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	// Perform abstract synchronization on a higher level
	if (!FDisplayClusterDeviceBase::Present(InOutSyncInterval))
	{
		return false;
	}

	// A sync policy hasn't presented current frame so we do it ourselves
	return PresentImpl(MainViewport->GetViewportRHI(), InOutSyncInterval);
}
