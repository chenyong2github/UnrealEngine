// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * Public display cluster presentation interface
 */
class IDisplayClusterPresentation
{
public:
	virtual ~IDisplayClusterPresentation() = default;

public:

	/** Called before presentation synchronization is initiated **/
	DECLARE_EVENT(IDisplayClusterPresentation, FDisplayClusterPresentationPreSynchronization_RHIThread);
	virtual FDisplayClusterPresentationPreSynchronization_RHIThread& OnDisplayClusterPresentationPreSynchronization_RHIThread() = 0;

	/** Called after presentation synchronization is completed **/
	DECLARE_EVENT(IDisplayClusterPresentation, FDisplayClusterPresentationPostSynchronization_RHIThread);
	virtual FDisplayClusterPresentationPostSynchronization_RHIThread& OnDisplayClusterPresentationPostSynchronization_RHIThread() = 0;
};
