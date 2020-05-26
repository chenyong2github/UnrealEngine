// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Swap synchronization protocol
 */
class IPDisplayClusterSwapSyncProtocol
{
public:
	// Swap sync barrier
	virtual void WaitForSwapSync(double* ThreadWaitTime, double* BarrierWaitTime) = 0;
};

