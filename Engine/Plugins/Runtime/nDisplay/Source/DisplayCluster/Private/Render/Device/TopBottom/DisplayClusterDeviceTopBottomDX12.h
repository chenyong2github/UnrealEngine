// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomBase.h"


/**
 * Top-bottom passive stereoscopic device
 */
class FDisplayClusterDeviceTopBottomDX12
	: public FDisplayClusterDeviceTopBottomBase
{
public:
	FDisplayClusterDeviceTopBottomDX12();
	virtual ~FDisplayClusterDeviceTopBottomDX12();

protected:
	virtual FDisplayClusterPresentationBase* CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy) override;
};
