// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertFilter.h"
#include "ConcertFrontendFilter.h"
#include "ConcertFrontendFilter_TextSearch.h"

#include "Algo/AllOf.h"
#include "Widgets/SBoxPanel.h"

namespace UE::MultiUserServer
{
	/** A filter that contains multiple UI filters */
	template<typename TFilterType, typename TTextSearchFilterType>
	class TConcertFrontendRootFilter 
		:
		public TConcertFilter<TFilterType>,
		public TSharedFromThis<TConcertFrontendRootFilter<TFilterType, TTextSearchFilterType>>
	{
		using Super = TConcertFilter<TFilterType>;
	public:

		using FConcertFilter = TConcertFilter<TFilterType>;
		using FConcertFrontendFilter = TConcertFrontendFilter<TFilterType>;
		using FConcertFrontendFilter_TextSearch = TTextSearchFilterType;

		TConcertFrontendRootFilter(
			TSharedRef<FConcertFrontendFilter_TextSearch> TextSearchFilter,
			TArray<TSharedRef<FConcertFrontendFilter>> FrontendFilters,
			const TArray<TSharedRef<FConcertFilter>>& NonVisualFilters = {}
			)
			: TextSearchFilter(MoveTemp(TextSearchFilter))
			, FrontendFilters(MoveTemp(FrontendFilters))
			, AllFilters(this->FrontendFilters)
		{
			AllFilters.Reserve(FrontendFilters.Num() + 1 + NonVisualFilters.Num());
			AllFilters.Add(TextSearchFilter);
			for (const TSharedRef<FConcertFilter>& NonVisualFilter : NonVisualFilters)
			{
				AllFilters.Add(NonVisualFilter);
			}
	
			for (const TSharedRef<FConcertFilter>& Filter : AllFilters)
			{
				Filter->OnChanged().AddRaw(this, &TConcertFrontendRootFilter<TFilterType, TTextSearchFilterType>::BroadcastOnChanged);
			}
		}

		/** Builds the widget view for all contained filters */
		TSharedRef<SWidget> BuildFilterWidgets() const
		{
			return SNew(SVerticalBox)

				// Search bar
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					TextSearchFilter->GetFilterWidget()
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2)
				[
					BuildCustomFilterListWidget()
				];
		}
	
		//~ Begin IFilter Interface
		virtual bool PassesFilter(TFilterType InItem) const override
		{
			return Algo::AllOf(
				AllFilters,
				[&InItem](const TSharedRef<FConcertFilter>& AndFilter){ return AndFilter->PassesFilter(InItem); }
				);
		}
		//~ End IFilter Interface

		FORCEINLINE const TSharedRef<FConcertFrontendFilter_TextSearch>& GetTextSearchFilter() const { return TextSearchFilter; }

	private:

		/** The text search filter. Also in AllFilters. Separate variable to build search bar in new line. */
		TSharedRef<FConcertFrontendFilter_TextSearch> TextSearchFilter;

		/** AllFrontendFilters without TextSearchFilter. */
		TArray<TSharedRef<FConcertFrontendFilter>> FrontendFilters;
	
		/** Filters that are combined using logical AND. */
		TArray<TSharedRef<FConcertFilter>> AllFilters;

		void BroadcastOnChanged()
		{
			Super::template OnChanged().Broadcast();
		}

		/** Builds the widgets that go under the text */
		TSharedRef<SWidget> BuildCustomFilterListWidget() const
		{
			TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

			bool bIsFirst = true;
			for (const TSharedRef<FConcertFrontendFilter>& Filter : FrontendFilters)
			{
				const FMargin Margin = bIsFirst ? FMargin() : FMargin(8, 0, 0, 0);
				bIsFirst = false;
				Box->AddSlot()
					.AutoWidth()
					.Padding(Margin)
					.VAlign(VAlign_Center)
					[
						Filter->GetFilterWidget()
					];
			}

			return Box;
		}
	};
}


