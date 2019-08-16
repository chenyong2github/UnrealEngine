// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareBase.h"


/**
 * OpenGL network synchronization policy
 */
class FDisplayClusterRenderSyncPolicySoftwareOpenGL
	: public FDisplayClusterRenderSyncPolicySoftwareBase
{
public:
	FDisplayClusterRenderSyncPolicySoftwareOpenGL();
	virtual ~FDisplayClusterRenderSyncPolicySoftwareOpenGL();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderSyncPolicy
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) override;
};
