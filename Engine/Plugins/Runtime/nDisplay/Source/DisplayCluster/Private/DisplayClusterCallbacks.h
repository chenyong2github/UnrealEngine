// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDisplayClusterCallbacks.h"


/**
 * DisplayCluster callbacks API implementation
 */
class FDisplayClusterCallbacks :
	public  IDisplayClusterCallbacks
{
public:
	virtual FDisplayClusterStartSessionEvent& OnDisplayClusterStartSession() override
	{
		return DisplayClusterStartSessionEvent;
	}

	virtual FDisplayClusterEndSessionEvent& OnDisplayClusterEndSession() override
	{
		return DisplayClusterEndSessionEvent;
	}

	virtual FDisplayClusterStartFrameEvent& OnDisplayClusterStartFrame() override
	{
		return DisplayClusterStartFrameEvent;
	}

	virtual FDisplayClusterEndFrameEvent& OnDisplayClusterEndFrame() override
	{
		return DisplayClusterEndFrameEvent;
	}

	virtual FDisplayClusterPreTickEvent& OnDisplayClusterPreTick() override
	{
		return DisplayClusterPreTickEvent;
	}

	virtual FDisplayClusterTickEvent& OnDisplayClusterTick() override
	{
		return DisplayClusterTickEvent;
	}

	virtual FDisplayClusterPostTickEvent& OnDisplayClusterPostTick() override
	{
		return DisplayClusterPostTickEvent;
	}

	virtual FDisplayClusterStartSceneEvent& OnDisplayClusterStartScene() override
	{
		return DisplayClusterStartSceneEvent;
	}

	virtual FDisplayClusterEndSceneEvent& OnDisplayClusterEndScene() override
	{
		return DisplayClusterEndSceneEvent;
	}

	virtual FDisplayClusterCustomPresentSetEvent& OnDisplayClusterCustomPresentSet() override
	{
		return DisplayClusterCustomPresentSetEvent;
	}

	virtual FDisplayClusterPresentationPreSynchronization_RHIThread& OnDisplayClusterPresentationPreSynchronization_RHIThread() override
	{
		return DisplayClusterPresentationPreSynchronizationEvent;
	}

	virtual FDisplayClusterPresentationPostSynchronization_RHIThread& OnDisplayClusterPresentationPostSynchronization_RHIThread() override
	{
		return DisplayClusterPresentationPostSynchronizationEvent;
	}

	virtual FDisplayClusterFailoverNodeDown& OnDisplayClusterFailoverNodeDown() override
	{
		return DisplayClusterFailoverNodeDown;
	}

private:
	FDisplayClusterStartSessionEvent         DisplayClusterStartSessionEvent;
	FDisplayClusterEndSessionEvent           DisplayClusterEndSessionEvent;
	FDisplayClusterStartFrameEvent           DisplayClusterStartFrameEvent;
	FDisplayClusterEndFrameEvent             DisplayClusterEndFrameEvent;
	FDisplayClusterPreTickEvent              DisplayClusterPreTickEvent;
	FDisplayClusterTickEvent                 DisplayClusterTickEvent;
	FDisplayClusterPostTickEvent             DisplayClusterPostTickEvent;
	FDisplayClusterStartSceneEvent           DisplayClusterStartSceneEvent;
	FDisplayClusterEndSceneEvent             DisplayClusterEndSceneEvent;
	FDisplayClusterCustomPresentSetEvent     DisplayClusterCustomPresentSetEvent;
	FDisplayClusterPresentationPreSynchronization_RHIThread DisplayClusterPresentationPreSynchronizationEvent;
	FDisplayClusterPresentationPostSynchronization_RHIThread DisplayClusterPresentationPostSynchronizationEvent;
	FDisplayClusterFailoverNodeDown          DisplayClusterFailoverNodeDown;
};
