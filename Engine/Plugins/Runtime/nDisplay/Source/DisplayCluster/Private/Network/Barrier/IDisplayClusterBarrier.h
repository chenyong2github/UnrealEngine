// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FDisplayClusterWatchdogTimer;


/**
 * Barrier wait result
 */
enum class EDisplayClusterBarrierWaitResult : uint8
{
	Ok,
	NotActive,
	TimeOut,
	NotAllowed,
};


/**
 * Thread barrier interface
 */
class IDisplayClusterBarrier
{
public:
	virtual ~IDisplayClusterBarrier() = default;

public:
	// Barrier name
	virtual const FString& GetName() const = 0;

	// Activate barrier
	virtual bool Activate() = 0;

	// Deactivate barrier, no threads will be blocked
	virtual void Deactivate() = 0;

	// Returns true if the barrier has been activated
	virtual bool IsActivated() const = 0;

	// Wait until all threads arrive
	virtual EDisplayClusterBarrierWaitResult Wait(const FString& ThreadMarker, double* OutThreadWaitTime = nullptr, double* OutBarrierWaitTime = nullptr) = 0;

	// Remove specified caller from the sync pipeline
	virtual void UnregisterSyncCaller(const FString& CallerId) = 0;

	// Barrier timout notification (provides BarrierName and CallersTimedOut in parameters)
	DECLARE_EVENT_TwoParams(IDisplayClusterBarrier, FDisplayClusterBarrierTimeoutEvent, const FString&, const TArray<FString>&);
	virtual FDisplayClusterBarrierTimeoutEvent& OnBarrierTimeout() = 0;
};
