// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX11.h"
#include "Render/Presentation/DisplayClusterPresentationDX11.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterDeviceMonoscopicDX11::FDisplayClusterDeviceMonoscopicDX11()
{
}

FDisplayClusterDeviceMonoscopicDX11::~FDisplayClusterDeviceMonoscopicDX11()
{
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceMonoscopicDX11::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX11(Viewport, SyncPolicy);
}
