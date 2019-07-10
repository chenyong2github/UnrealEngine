// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomBase.h"
#include "Render/Presentation/DisplayClusterDevicePresentationDX11.h"


/**
 * Top-bottom passive stereoscopic device
 */
class FDisplayClusterDeviceTopBottomDX11
	: public FDisplayClusterDeviceTopBottomBase
	, public FDisplayClusterDevicePresentationDX11
{
public:
	FDisplayClusterDeviceTopBottomDX11();
	virtual ~FDisplayClusterDeviceTopBottomDX11();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
