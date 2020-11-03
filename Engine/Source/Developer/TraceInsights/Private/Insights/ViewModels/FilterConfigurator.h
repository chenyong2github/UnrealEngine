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

	~FFilterConfigurator() {}

	void AddFilter(EFilterField FilterKey)
	{
		AvailableFilters->Add(FFilterService::Get()->GetFilter(FilterKey));
	}

	FFilterConfiguratorNodePtr GetRootNode() { return RootNode; }

	bool ApplyFilters(const FFilterContext& Context) const;

	void SetOnChangesCommitedCallback(TFunction<void()> InCallback) {	OnChangesCommitedCallback = InCallback;	}

private:

	FFilterConfiguratorNodePtr RootNode;

	TSharedPtr<TArray<TSharedPtr<struct FFilter>>> AvailableFilters;

	TFunction<void()> OnChangesCommitedCallback = nullptr;
};

////////////////////////////////////////////////////////////////////////////////////////////////////