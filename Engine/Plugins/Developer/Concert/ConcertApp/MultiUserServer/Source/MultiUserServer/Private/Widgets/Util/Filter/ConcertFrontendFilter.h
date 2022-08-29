// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertFilter.h"

class SWidget;

namespace UE::MultiUserServer
{
	/** A filter that is intended to be displayed in the UI. Every filter has one widget displaying it. */
	template<typename TFilterType>
	class TConcertFrontendFilter
		:
		public TConcertFilter<TFilterType>,
		public TSharedFromThis<TConcertFrontendFilter<TFilterType>>
	{
	public:

		/** Gets a widget that represents this filter */
		virtual TSharedRef<SWidget> GetFilterWidget() = 0;
	};

	/**
	 * Helper class to implement filters
	 *
	 * Intended pattern:
	 *	1. Subclass TConcertFilter<TFilterType> and implement filter logic in TConcertFilterImpl, e.g. text search. This will act as a "model" in MVC.
	 *  2. Subclass TConcertFrontendLogFilterAggregate and handle creating UI in it. This will act as a "View" in MVC.
	 */
	template<typename TConcertFilterImpl, typename TFilterType, typename TWidgetType = SWidget>
	class TConcertFrontendFilterAggregate : public TConcertFrontendFilter<TFilterType>
	{
		using Super = TConcertFrontendFilter<TFilterType>;
	public:

		//~ Begin FConcertLogFilter Interface
		virtual bool PassesFilter(TFilterType InItem) const final override { return Implementation.PassesFilter(InItem); }
		//~ End FConcertLogFilter Interface
	
		//~ Begin FConcertFrontendLogFilter Interface
		virtual TSharedRef<SWidget> GetFilterWidget() final override { return ChildSlot.ToSharedRef(); }
		//~ End FConcertLogFilter Interface
	
	protected:

		template<typename... TArg>
		TConcertFrontendFilterAggregate(TArg&&... Arg)
			: Implementation(Forward<TArg>(Arg)...)
		{
			Implementation.OnChanged().AddLambda([this]()
			{
				Super::template OnChanged().Broadcast();
			});
		}

		TConcertFilterImpl Implementation;
		TSharedPtr<TWidgetType> ChildSlot;
	};
}


