// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertLogFilter.h"

class SWidget;

/**
 * A filter that is intended to be displayed in the UI. Every filter has one widget displaying it. */
class FConcertFrontendLogFilter : public FConcertLogFilter
{
public:

	/** Gets a widget that represents this filter */
	virtual TSharedRef<SWidget> GetFilterWidget() = 0;
};

/** Helper class to implement filters */
template<typename TConcertLogFilter, typename TWidgetType = SWidget>
class TConcertFrontendLogFilterAggregate : public FConcertFrontendLogFilter
{
public:

	//~ Begin FConcertLogFilter Interface
	virtual bool PassesFilter(const FConcertLogEntry& InItem) const final override { return Implementation.PassesFilter(InItem); }
	//~ End FConcertLogFilter Interface
	
	//~ Begin FConcertFrontendLogFilter Interface
	virtual TSharedRef<SWidget> GetFilterWidget() final override { return ChildSlot.ToSharedRef(); }
	//~ End FConcertLogFilter Interface
	
protected:

	template<typename... TArg>
	TConcertFrontendLogFilterAggregate(TArg&&... Arg)
		: Implementation(Forward<TArg>(Arg)...)
	{
		Implementation.OnChanged().AddLambda([this]()
		{
			OnChanged().Broadcast();
		});
	}

	TConcertLogFilter Implementation;
	TSharedPtr<TWidgetType> ChildSlot;
};
