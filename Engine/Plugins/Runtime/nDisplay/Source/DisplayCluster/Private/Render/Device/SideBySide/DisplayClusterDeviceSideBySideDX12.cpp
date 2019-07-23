// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/SidebySide/DisplayClusterDeviceSideBySideDX12.h"
#include "DisplayClusterLog.h"


FDisplayClusterDeviceSideBySideDX12::FDisplayClusterDeviceSideBySideDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceSideBySideDX12::~FDisplayClusterDeviceSideBySideDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


bool FDisplayClusterDeviceSideBySideDX12::Present(int32& InOutSyncInterval)
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
