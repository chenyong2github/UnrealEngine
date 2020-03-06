// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Tickable.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Delegates/IDelegateInstance.h"

class UDataSourceFilter;
class UWorld;
class AActor;
class UTraceSourceFilteringSettings;
class USourceFilterCollection;

/** Per-UWorld object that keeps track of the its contained AActor's filtering states */
class FSourceFilterManager : public FTickableGameObject
{
	friend class FTraceSourceFiltering;
public:
	FSourceFilterManager(UWorld* InWorld);
	~FSourceFilterManager();

	/** Begin FTickableGameObject overrides */
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const;
	virtual bool IsTickableInEditor() const { return true; }
	/** End FTickableGameObject overrides */

protected:
	/** Applies all UDataSourceFilters to the specified AActor, and updates it filtering state accordingly */
	void ApplyFilters(const AActor* Actor);

protected:
	/** Registered delegate for whenever an AActor is spawned within World */
	FDelegateHandle ActorSpawningDelegateHandle;

	/** Filtering settings for the running instance */
	const UTraceSourceFilteringSettings* Settings;
	/** Filter collection, containing the UDataSourceFilters, for the running instance */
	const USourceFilterCollection* FilterCollection;

	/** UWorld instance that this instance corresponds to */
	UWorld* World;
};
