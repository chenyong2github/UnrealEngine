// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomDX12.h"
#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterDeviceTopBottomDX12::FDisplayClusterDeviceTopBottomDX12()
{
}

FDisplayClusterDeviceTopBottomDX12::~FDisplayClusterDeviceTopBottomDX12()
{
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceTopBottomDX12::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX12(Viewport, SyncPolicy);
}
