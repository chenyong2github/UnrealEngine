// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/SidebySide/DisplayClusterDeviceSideBySideDX12.h"
#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterDeviceSideBySideDX12::FDisplayClusterDeviceSideBySideDX12()
{
}

FDisplayClusterDeviceSideBySideDX12::~FDisplayClusterDeviceSideBySideDX12()
{
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceSideBySideDX12::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX12(Viewport, SyncPolicy);
}
