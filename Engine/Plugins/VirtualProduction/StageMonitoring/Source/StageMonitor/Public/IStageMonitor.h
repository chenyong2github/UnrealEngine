// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "StageMessages.h"
#include "UObject/StructOnScope.h"


class IStageDataCollection;

/**
 * Interface for the stage monitor. Users can get the instance from the stage monitor module
 */
class STAGEMONITOR_API IStageMonitor
{
public:

	virtual ~IStageMonitor() {}
	
	/**
	 * Returns a DataCollection interface to query and get notifications about new activities
	 */
	virtual TSharedPtr<IStageDataCollection> GetDataCollection() const = 0;

	/**
	 * Returns true if stage monitor currently in critical state (i.e. recording)
	 */
	virtual bool IsStageInCriticalState() const = 0;

	/**
	 * Returns true if the given time is part of a critical state time range.
	 */
	virtual bool IsTimePartOfCriticalState(double TimeInSeconds) const = 0;

	/**
	 * Returns Source name of the current critical state. Returns None if not active
	 */
	virtual FName GetCurrentCriticalStateSource() const = 0;

	/**
	 * Returns true if monitor is actively listening for activities
	 */
	virtual bool IsActive() const = 0;

	/**
	 * Returns a list of all sources that triggered a critical state
	 */
	virtual TArray<FName> GetCriticalStateHistorySources() const = 0;

	/**
	 * Returns a list of all sources that triggered a critical state during TimeInSeconds. 
	 * If provided time is not part of a critical state, returned array will be empty
	 */
	virtual TArray<FName> GetCriticalStateSources(double TimeInSeconds) const = 0;
};
