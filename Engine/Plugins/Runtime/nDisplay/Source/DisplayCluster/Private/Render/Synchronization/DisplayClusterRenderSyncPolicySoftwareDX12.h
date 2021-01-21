// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareBase.h"


/**
 * DX12 network synchronization policy
 */
class FDisplayClusterRenderSyncPolicySoftwareDX12
	: public FDisplayClusterRenderSyncPolicySoftwareBase
{
public:
	FDisplayClusterRenderSyncPolicySoftwareDX12(const TMap<FString, FString>& Parameters)
		: FDisplayClusterRenderSyncPolicySoftwareBase(Parameters)
	{ }

	virtual ~FDisplayClusterRenderSyncPolicySoftwareDX12()
	{ }

protected:
	virtual void WaitForFrameCompletion() override;
};
