// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/SidebySide/DisplayClusterDeviceSideBySideDX11.h"
#include "Render/Presentation/DisplayClusterPresentationDX11.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterDeviceSideBySideDX11::FDisplayClusterDeviceSideBySideDX11()
{
}

FDisplayClusterDeviceSideBySideDX11::~FDisplayClusterDeviceSideBySideDX11()
{
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceSideBySideDX11::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX11(Viewport, SyncPolicy);
}
