// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/SideBySide/DisplayClusterDeviceSideBySideBase.h"
#include "Render/Presentation/DisplayClusterDevicePresentationDX11.h"


/**
 * Side-by-side passive stereoscopic device
 */
class FDisplayClusterDeviceSideBySideDX11
	: public FDisplayClusterDeviceSideBySideBase
	, public FDisplayClusterDevicePresentationDX11
{
public:
	FDisplayClusterDeviceSideBySideDX11();
	virtual ~FDisplayClusterDeviceSideBySideDX11();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
