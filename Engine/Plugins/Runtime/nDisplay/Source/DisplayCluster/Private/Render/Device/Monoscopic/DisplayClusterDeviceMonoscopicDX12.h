// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Render/Device/DisplayClusterDeviceMonoscopicBase.h"


/**
 * Monoscopic render device (DirectX 12)
 */
class FDisplayClusterDeviceMonoscopicDX12
	: public FDisplayClusterDeviceMonoscopicBase
{
public:
	FDisplayClusterDeviceMonoscopicDX12();
	virtual ~FDisplayClusterDeviceMonoscopicDX12();

protected:
	virtual FDisplayClusterPresentationBase* CreatePresentationObject(FViewport* const Viewport, TSharedPtr<IDisplayClusterRenderSyncPolicy>& SyncPolicy) override;
};
