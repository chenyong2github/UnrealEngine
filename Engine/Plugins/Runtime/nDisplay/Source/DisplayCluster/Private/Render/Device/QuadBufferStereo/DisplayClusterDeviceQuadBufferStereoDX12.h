// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Render/Device/QuadBufferStereo/DisplayClusterDeviceQuadBufferStereoBase.h"
#include "Render/Presentation/DisplayClusterDevicePresentationDX12.h"


/**
 * Frame sequenced active stereo (DirectX 12)
 */
class FDisplayClusterDeviceQuadBufferStereoDX12
	: public FDisplayClusterDeviceQuadBufferStereoBase
	, public FDisplayClusterDevicePresentationDX12
{
public:
	FDisplayClusterDeviceQuadBufferStereoDX12();
	virtual ~FDisplayClusterDeviceQuadBufferStereoDX12();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
