// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoDX12.h"
#include "Render/Presentation/DisplayClusterPresentationDX12.h"

#include "DisplayClusterLog.h"


FDisplayClusterDeviceQuadBufferStereoDX12::FDisplayClusterDeviceQuadBufferStereoDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceQuadBufferStereoDX12::~FDisplayClusterDeviceQuadBufferStereoDX12()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceQuadBufferStereoDX12::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX12(Viewport, SyncPolicy);
}
