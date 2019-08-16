// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/SideBySide/DisplayClusterDeviceSideBySideBase.h"
#include "Render/Presentation/DisplayClusterDevicePresentationDX12.h"


/**
 * Side-by-side passive stereoscopic device
 */
class FDisplayClusterDeviceSideBySideDX12
	: public FDisplayClusterDeviceSideBySideBase
	, public FDisplayClusterDevicePresentationDX12
{
public:
	FDisplayClusterDeviceSideBySideDX12();
	virtual ~FDisplayClusterDeviceSideBySideDX12();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
