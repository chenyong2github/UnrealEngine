// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterDeviceBase.h"

class IDisplayClusterRenderSyncPolicy;


/**
 * Present stub to allow to synchronize a cluster with native rendering pipeline (no nDisplay stereo devices used)
 */
class FDisplayClusterDeviceNativePresentHandler : public FDisplayClusterDeviceBase
{
public:
	FDisplayClusterDeviceNativePresentHandler();
	virtual ~FDisplayClusterDeviceNativePresentHandler();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IStereoRenderTargetManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	// No separate RT, we don't even render anything
	virtual bool ShouldUseSeparateRenderTarget() const override
	{ return false; }


protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
