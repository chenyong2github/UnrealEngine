// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomBase.h"


/**
 * Top-bottom passive stereoscopic device
 */
class FDisplayClusterDeviceTopBottomDX11
	: public FDisplayClusterDeviceTopBottomBase
{
public:
	FDisplayClusterDeviceTopBottomDX11();
	virtual ~FDisplayClusterDeviceTopBottomDX11();

protected:
	virtual FDisplayClusterPresentationBase* CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy) override;
};
