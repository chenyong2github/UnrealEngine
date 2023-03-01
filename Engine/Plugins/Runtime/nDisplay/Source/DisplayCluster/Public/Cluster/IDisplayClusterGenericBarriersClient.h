// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * Generic barriers client interface
 */
class IDisplayClusterGenericBarriersClient
{
public:
	virtual ~IDisplayClusterGenericBarriersClient() = default;

// Networking API
public:
	/** Connects to a server */
	virtual bool Connect() = 0;

	/** Terminates current connection */
	virtual void Disconnect() = 0;

	/** Returns connection status */
	virtual bool IsConnected() const = 0;

	/** Returns client name */
	virtual FString GetName() const = 0;

// Barrier API
public:
	/** Creates new barrier */
	virtual bool CreateBarrier(const FString& BarrierId, const TArray<FString>& UniqueThreadMarkers, uint32 Timeout) = 0;

	/** Wait until a barrier with specific ID is created and ready to go */
	virtual bool WaitUntilBarrierIsCreated(const FString& BarrierId) = 0;

	/** Checks if a specific barrier exists */
	virtual bool IsBarrierAvailable(const FString& BarrierId) = 0;

	/** Releases specific barrier */
	virtual bool ReleaseBarrier(const FString& BarrierId) = 0;

	/** Synchronize calling thread on a specific barrier */
	virtual bool Synchronize(const FString& BarrierId, const FString& UniqueThreadMarker) = 0;
};
