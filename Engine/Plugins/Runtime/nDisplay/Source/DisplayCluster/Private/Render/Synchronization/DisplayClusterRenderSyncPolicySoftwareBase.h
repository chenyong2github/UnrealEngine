// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyBase.h"


/**
 * Base network synchronization policy (soft sync)
 */
class FDisplayClusterRenderSyncPolicySoftwareBase
	: public FDisplayClusterRenderSyncPolicyBase
{
public:
	FDisplayClusterRenderSyncPolicySoftwareBase();
	virtual ~FDisplayClusterRenderSyncPolicySoftwareBase() = 0;
};
