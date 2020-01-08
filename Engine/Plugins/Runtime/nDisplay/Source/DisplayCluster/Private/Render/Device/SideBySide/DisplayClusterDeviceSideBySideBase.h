// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterDeviceStereoBase.h"


/**
 * Side-by-side passive stereoscopic device
 */
class FDisplayClusterDeviceSideBySideBase :
	public FDisplayClusterDeviceStereoBase
{
public:
	FDisplayClusterDeviceSideBySideBase();
	virtual ~FDisplayClusterDeviceSideBySideBase();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IStereoRendering
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const override;
};
