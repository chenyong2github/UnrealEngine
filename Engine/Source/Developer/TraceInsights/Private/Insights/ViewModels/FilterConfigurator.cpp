// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterConfigurator.h"

#include "Insights/Widgets/SFilterConfigurator.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterConfigurator::FFilterConfigurator()
{
	RootNode = MakeShared<FFilterConfiguratorNode>(TEXT(""), true);
	AvailableFilters = MakeShared<TArray<TSharedPtr<FFilter>>>();
	RootNode->SetAvailableFilters(AvailableFilters);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterConfigurator::~FFilterConfigurator()
{
	OnDestroyedEvent.Broadcast();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterConfigurator::FFilterConfigurator(const FFilterConfigurator& Other)
{
	*this = Other;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterConfigurator& FFilterConfigurator::operator=(const FFilterConfigurator& Other)
{
	RootNode = MakeShared<FFilterConfiguratorNode>(*Other.RootNode);
	RootNode->SetGroupPtrForChildren();
	AvailableFilters = Other.AvailableFilters;
	OnDestroyedEvent = Other.OnDestroyedEvent;

	OnChangesCommitedEvent.Broadcast();

	RootNode->ProcessFilter();

	return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFilterConfigurator::ApplyFilters(const FFilterContext& Context) const
{
	return RootNode->ApplyFilters(Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
