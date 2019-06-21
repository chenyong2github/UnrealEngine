// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Device/DisplayClusterDeviceBase.h"


/**
 * Base monoscopic render device
 */
class FDisplayClusterDeviceMonoscopicBase
	: public FDisplayClusterDeviceBase
{
public:
	FDisplayClusterDeviceMonoscopicBase();
	virtual ~FDisplayClusterDeviceMonoscopicBase();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IStereoRendering
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const override;
};
