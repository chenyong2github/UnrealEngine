// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Render/Device/DisplayClusterDeviceMonoscopicBase.h"
#include "Render/Presentation/DisplayClusterDevicePresentationDX12.h"


/**
 * Monoscopic render device (DirectX 12)
 */
class FDisplayClusterDeviceMonoscopicDX12
	: public FDisplayClusterDeviceMonoscopicBase
	, public FDisplayClusterDevicePresentationDX12
{
public:
	FDisplayClusterDeviceMonoscopicDX12();
	virtual ~FDisplayClusterDeviceMonoscopicDX12();

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Present(int32& InOutSyncInterval) override;
};
