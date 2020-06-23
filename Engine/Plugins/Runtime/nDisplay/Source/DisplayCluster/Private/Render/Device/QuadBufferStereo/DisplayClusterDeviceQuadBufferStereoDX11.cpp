// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoDX11.h"
#include "Render/Presentation/DisplayClusterPresentationDX11.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterDeviceQuadBufferStereoDX11::FDisplayClusterDeviceQuadBufferStereoDX11()
{
}

FDisplayClusterDeviceQuadBufferStereoDX11::~FDisplayClusterDeviceQuadBufferStereoDX11()
{
}

FDisplayClusterPresentationBase* FDisplayClusterDeviceQuadBufferStereoDX11::CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy)
{
	return new FDisplayClusterPresentationDX11(Viewport, SyncPolicy);
}
