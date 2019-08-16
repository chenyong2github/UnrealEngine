// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterDeviceMonoscopicBase.h"
#include "Render/Presentation/DisplayClusterDevicePresentationDX11.h"


/**
 * Monoscopic render device (DirectX 11)
 */
class FDisplayClusterDeviceMonoscopicDX11
	: public FDisplayClusterDeviceMonoscopicBase
	, public FDisplayClusterDevicePresentationDX11
{
public:
	FDisplayClusterDeviceMonoscopicDX11();
	virtual ~FDisplayClusterDeviceMonoscopicDX11();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
