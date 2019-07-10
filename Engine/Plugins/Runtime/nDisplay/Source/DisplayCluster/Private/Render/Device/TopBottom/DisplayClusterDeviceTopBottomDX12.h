// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/TopBottom/DisplayClusterDeviceTopBottomBase.h"
#include "Render/Presentation/DisplayClusterDevicePresentationDX12.h"


/**
 * Top-bottom passive stereoscopic device
 */
class FDisplayClusterDeviceTopBottomDX12
	: public FDisplayClusterDeviceTopBottomBase
	, public FDisplayClusterDevicePresentationDX12
{
public:
	FDisplayClusterDeviceTopBottomDX12();
	virtual ~FDisplayClusterDeviceTopBottomDX12();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
