// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Rendering synchronization protocol. Used to synchronize everything we need
 * on the render thread.
 */
class IDisplayClusterProtocolRenderSync
{
public:
	virtual ~IDisplayClusterProtocolRenderSync() = default;

public:
	// Swap sync barrier
	virtual void WaitForSwapSync() = 0;
};
