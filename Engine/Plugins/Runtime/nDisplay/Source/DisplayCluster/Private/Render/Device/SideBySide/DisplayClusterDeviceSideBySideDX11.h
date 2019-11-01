// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/SideBySide/DisplayClusterDeviceSideBySideBase.h"


/**
 * Side-by-side passive stereoscopic device
 */
class FDisplayClusterDeviceSideBySideDX11
	: public FDisplayClusterDeviceSideBySideBase
{
public:
	FDisplayClusterDeviceSideBySideDX11();
	virtual ~FDisplayClusterDeviceSideBySideDX11();

protected:
	virtual FDisplayClusterPresentationBase* CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy) override;
};
