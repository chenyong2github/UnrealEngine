// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterDeviceStereoBase.h"


/**
 * Top-bottom passive stereoscopic device
 */
class FDisplayClusterDeviceTopBottomBase :
	public FDisplayClusterDeviceStereoBase
{
public:
	FDisplayClusterDeviceTopBottomBase();
	virtual ~FDisplayClusterDeviceTopBottomBase();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IStereoRendering
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const override;
};
