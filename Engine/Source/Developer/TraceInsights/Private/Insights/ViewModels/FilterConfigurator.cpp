// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterConfigurator.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

FFilterConfigurator::FFilterConfigurator()
{
	RootNode = MakeShared<FFilterConfiguratorNode>(TEXT(""), true);
	AvailableFilters = MakeShared<TArray<TSharedPtr<FFilter>>>();
	RootNode->SetAvailableFilters(AvailableFilters);
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

	if (OnChangesCommitedCallback)
	{
		OnChangesCommitedCallback();
	}

	RootNode->ProcessFilter();

	return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFilterConfigurator::ApplyFilters(const FFilterContext& Context) const
{
	return RootNode->ApplyFilters(Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
