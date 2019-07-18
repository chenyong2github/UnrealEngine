// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX12.h"
#include "DisplayClusterLog.h"


FDisplayClusterDeviceMonoscopicDX12::FDisplayClusterDeviceMonoscopicDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceMonoscopicDX12::~FDisplayClusterDeviceMonoscopicDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

bool FDisplayClusterDeviceMonoscopicDX12::Present(int32& InOutSyncInterval)
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
