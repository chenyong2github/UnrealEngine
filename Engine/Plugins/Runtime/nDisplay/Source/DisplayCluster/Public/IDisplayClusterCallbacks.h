// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterEnums.h"


/**
 * DisplayCluster callbacks API
 */
class IDisplayClusterCallbacks
{
public:
	virtual ~IDisplayClusterCallbacks() = default;

public:
	/** Called on session start **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterStartSessionEvent);
	virtual FDisplayClusterStartSessionEvent& OnDisplayClusterStartSession() = 0;

	/** Called on session end **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterEndSessionEvent);
	virtual FDisplayClusterEndSessionEvent& OnDisplayClusterEndSession() = 0;

	/** Called on start scene **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterStartSceneEvent);
	virtual FDisplayClusterStartSceneEvent& OnDisplayClusterStartScene() = 0;

	/** Called on end scene **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterEndSceneEvent);
	virtual FDisplayClusterEndSceneEvent& OnDisplayClusterEndScene() = 0;

	/** Called on DisplayCluster StartFrame **/
	DECLARE_EVENT_OneParam(IDisplayClusterCallbacks, FDisplayClusterStartFrameEvent, uint64);
	virtual FDisplayClusterStartFrameEvent& OnDisplayClusterStartFrame() = 0;

	/** Called on DisplayCluster EndFrame **/
	DECLARE_EVENT_OneParam(IDisplayClusterCallbacks, FDisplayClusterEndFrameEvent, uint64);
	virtual FDisplayClusterEndFrameEvent& OnDisplayClusterEndFrame() = 0;

	/** Called on DisplayCluster PreTick **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterPreTickEvent);
	virtual FDisplayClusterPreTickEvent& OnDisplayClusterPreTick() = 0;

	/** Called on DisplayCluster Tick **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterTickEvent);
	virtual FDisplayClusterTickEvent& OnDisplayClusterTick() = 0;

	/** Called on DisplayCluster PostTick **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterPostTickEvent);
	virtual FDisplayClusterPostTickEvent& OnDisplayClusterPostTick() = 0;

	/** Callback triggered when custom present handler was created **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterCustomPresentSetEvent);
	virtual FDisplayClusterCustomPresentSetEvent& OnDisplayClusterCustomPresentSet() = 0;

	/** Called before presentation synchronization is initiated **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterPresentationPreSynchronization_RHIThread);
	virtual FDisplayClusterPresentationPreSynchronization_RHIThread& OnDisplayClusterPresentationPreSynchronization_RHIThread() = 0;

	/** Called after presentation synchronization is completed **/
	DECLARE_EVENT(IDisplayClusterCallbacks, FDisplayClusterPresentationPostSynchronization_RHIThread);
	virtual FDisplayClusterPresentationPostSynchronization_RHIThread& OnDisplayClusterPresentationPostSynchronization_RHIThread() = 0;

	/** Failover notification **/
	DECLARE_EVENT_OneParam(IDisplayClusterCallbacks, FDisplayClusterFailoverNodeDown, const FString&);
	virtual FDisplayClusterFailoverNodeDown& OnDisplayClusterFailoverNodeDown() = 0;
};
