// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX12.h"
#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "DisplayClusterLog.h"


FDisplayClusterDeviceMonoscopicDX12::FDisplayClusterDeviceMonoscopicDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceMonoscopicDX12::~FDisplayClusterDeviceMonoscopicDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceMonoscopicDX12::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX12(Viewport, SyncPolicy);
}