// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Insights/ViewModels/Filters.h"
#include "Insights/ViewModels/FilterConfigurator.h"

namespace Insights
{
 
////////////////////////////////////////////////////////////////////////////////////////////////////

class FQuickFind
{
public:
	FQuickFind(TSharedPtr<FFilterConfigurator> InFilterConfiguratorViewModel);

	~FQuickFind();

	TSharedPtr<FFilterConfigurator> GetFilterConfigurator() { return FilterConfigurator; }

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// OnDestroyedEvent
public:
	/** The event to execute when an instance is destroyed. */
	DECLARE_MULTICAST_DELEGATE(FOnDestroyedEvent);
	FOnDestroyedEvent& GetOnDestroyedEvent() { return OnDestroyedEvent; }

private:
	FOnDestroyedEvent OnDestroyedEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// OnFindNextEvent
public:
	/** The event to execute when the user selects the "Find Next" option. */
	DECLARE_MULTICAST_DELEGATE(FOnFindNextEvent);
	FOnFindNextEvent& GetOnFindNextEvent() { return OnFindNextEvent; }

private:
	FOnFindNextEvent OnFindNextEvent;

	////////////////////////////////////////////////////////////////////////////////////////////////////

private:
	TSharedPtr<FFilterConfigurator> FilterConfigurator;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights