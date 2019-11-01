// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterDeviceMonoscopicBase.h"


/**
 * Monoscopic render device (DirectX 11)
 */
class FDisplayClusterDeviceMonoscopicDX11
	: public FDisplayClusterDeviceMonoscopicBase
{
public:
	FDisplayClusterDeviceMonoscopicDX11();
	virtual ~FDisplayClusterDeviceMonoscopicDX11();

protected:
	virtual FDisplayClusterPresentationBase* CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy) override;
};
