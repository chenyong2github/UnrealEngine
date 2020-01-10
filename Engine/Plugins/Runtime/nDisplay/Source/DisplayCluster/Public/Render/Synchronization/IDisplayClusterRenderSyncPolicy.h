// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * Public render synchronization policy interface
 */
class IDisplayClusterRenderSyncPolicy
{
public:
	virtual ~IDisplayClusterRenderSyncPolicy() = 0
	{ }

public:

	/**
	* Synchronizes rendering threads in a cluster (optionally presents a frame)
	*
	* @param InOutSyncInterval - Sync interval (VSync)
	*
	* @return - true if we a caller needs to present frame by his own
	*/
	virtual bool SynchronizeClusterRendering(int32& InOutSyncInterval) = 0;
};
