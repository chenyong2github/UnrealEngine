// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoBase.h"
#include "Render/Presentation/DisplayClusterDevicePresentationDX11.h"


/**
 * Frame sequenced active stereo (DirectX 11)
 */
class FDisplayClusterDeviceQuadBufferStereoDX11
	: public FDisplayClusterDeviceQuadBufferStereoBase
	, public FDisplayClusterDevicePresentationDX11
{
public:
	FDisplayClusterDeviceQuadBufferStereoDX11();
	virtual ~FDisplayClusterDeviceQuadBufferStereoDX11();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
