// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/Monoscopic/DisplayClusterDeviceMonoscopicDX12.h"
#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterDeviceMonoscopicDX12::FDisplayClusterDeviceMonoscopicDX12()
{
}

FDisplayClusterDeviceMonoscopicDX12::~FDisplayClusterDeviceMonoscopicDX12()
{
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceMonoscopicDX12::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX12(Viewport, SyncPolicy);
}
