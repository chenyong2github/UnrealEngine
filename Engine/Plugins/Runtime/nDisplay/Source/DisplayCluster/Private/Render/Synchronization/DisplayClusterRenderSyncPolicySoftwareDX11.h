// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareBase.h"


/**
 * DX11 network synchronization policy
 */
class FDisplayClusterRenderSyncPolicySoftwareDX11
	: public FDisplayClusterRenderSyncPolicySoftwareBase
{
public:
	FDisplayClusterRenderSyncPolicySoftwareDX11(const TMap<FString, FString>& Parameters)
		: FDisplayClusterRenderSyncPolicySoftwareBase(Parameters)
	{ }

	virtual ~FDisplayClusterRenderSyncPolicySoftwareDX11()
	{ }

protected:
	virtual void WaitForFrameCompletion() override;
};
