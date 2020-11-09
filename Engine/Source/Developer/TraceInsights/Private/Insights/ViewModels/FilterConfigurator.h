// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Insights/ViewModels/Filters.h"
#include "Insights/ViewModels/FilterConfiguratorNode.h"
 
////////////////////////////////////////////////////////////////////////////////////////////////////

class FFilterConfigurator
{
public:
	FFilterConfigurator();

	FFilterConfigurator(const FFilterConfigurator& Other);
	FFilterConfigurator& operator=(const FFilterConfigurator& Other);

	~FFilterConfigurator();

	void AddFilter(EFilterField FilterKey)
	{
		AvailableFilters->Add(FFilterService::Get()->GetFilter(FilterKey));
	}

	FFilterConfiguratorNodePtr GetRootNode() { return RootNode; }

	bool ApplyFilters(const FFilterContext& Context) const;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// OnDestroyedEvent
public:
	/** The event to execute when an instance is destroyed. */
	DECLARE_MULTICAST_DELEGATE(FOnDestroyedEvent);
	FOnDestroyedEvent& GetOnDestroyedEvent() { return OnDestroyedEvent; }

private:
	FOnDestroyedEvent OnDestroyedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// OnChangesCommitedEvent
public:
	/** The event to execute when the changes to the Filter Widget are saved by clicking on the OK Button. */
	DECLARE_MULTICAST_DELEGATE(FOnChangesCommitedEvent);
	FOnChangesCommitedEvent& GetOnChangesCommitedEvent() { return OnChangesCommitedEvent; }

private:
	FOnChangesCommitedEvent OnChangesCommitedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////

private:

	FFilterConfiguratorNodePtr RootNode;

	TSharedPtr<TArray<TSharedPtr<struct FFilter>>> AvailableFilters;
};

////////////////////////////////////////////////////////////////////////////////////////////////////