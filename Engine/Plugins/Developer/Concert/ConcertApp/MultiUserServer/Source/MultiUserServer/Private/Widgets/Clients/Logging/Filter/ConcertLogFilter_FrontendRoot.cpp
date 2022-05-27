// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertLogFilter_FrontendRoot.h"

#include "ConcertFrontendLogFilter.h"
#include "ConcertFrontendLogFilter_Client.h"
#include "ConcertFrontendLogFilter_MessageAction.h"
#include "ConcertFrontendLogFilter_MessageType.h"
#include "ConcertFrontendLogFilter_TextSearch.h"
#include "ConcertFrontendLogFilter_Time.h"
#include "Algo/AllOf.h"

#include "Algo/AnyOf.h"
#include "Widgets/SBoxPanel.h"

FConcertLogFilter_FrontendRoot::FConcertLogFilter_FrontendRoot(TSharedRef<FConcertLogTokenizer> Tokenizer, TArray<TSharedRef<FConcertFrontendLogFilter>> InCustomFilters, const TArray<TSharedRef<FConcertLogFilter>>& NonVisualFilters)
	: TextSearchFilter(MakeShared<FConcertFrontendLogFilter_TextSearch>(MoveTemp(Tokenizer)))
	, CustomFilters(MoveTemp(InCustomFilters))
	, AllFrontendFilters(CustomFilters)
{
	AllFrontendFilters.Reserve(CustomFilters.Num() + 1 + NonVisualFilters.Num());
	
	AllFrontendFilters.Add(TextSearchFilter);
	for (const TSharedRef<FConcertLogFilter>& NonVisualFilter : NonVisualFilters)
	{
		AllFrontendFilters.Add(NonVisualFilter);
	}
	
	for (const TSharedRef<FConcertLogFilter>& Filter : AllFrontendFilters)
	{
		Filter->OnChanged().AddLambda([this]()
		{
			OnChanged().Broadcast();
		});
	}
}

TSharedRef<SWidget> FConcertLogFilter_FrontendRoot::BuildFilterWidgets() const
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

bool FConcertLogFilter_FrontendRoot::PassesFilter(const FConcertLog& InItem) const
{
	return Algo::AllOf(
		AllFrontendFilters,
		[&InItem](const TSharedRef<FConcertLogFilter>& AndFilter){ return AndFilter->PassesFilter(InItem); }
		);
}

TSharedRef<SWidget> FConcertLogFilter_FrontendRoot::BuildCustomFilterListWidget() const
{
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

	bool bIsFirst = true;
	for (const TSharedRef<FConcertFrontendLogFilter>& Filter : CustomFilters)
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

namespace UE::MultiUserServer
{
	namespace Private
	{
		static TArray<TSharedRef<FConcertFrontendLogFilter>> CreateCommonFilters()
		{
			return {
				MakeShared<FConcertFrontendLogFilter_MessageAction>(),
				MakeShared<FConcertFrontendLogFilter_MessageType>(),
				MakeShared<FConcertFrontendLogFilter_Time>(ETimeFilter::AllowAfter),
				MakeShared<FConcertFrontendLogFilter_Time>(ETimeFilter::AllowBefore)
			};
		}
	}
	
	TSharedRef<FConcertLogFilter_FrontendRoot> MakeGlobalLogFilter(TSharedRef<FConcertLogTokenizer> Tokenizer)
	{
		return MakeShared<FConcertLogFilter_FrontendRoot>(
			MoveTemp(Tokenizer),
			Private::CreateCommonFilters()
			);
	}

	TSharedRef<FConcertLogFilter_FrontendRoot> MakeClientLogFilter(TSharedRef<FConcertLogTokenizer> Tokenizer, const FGuid& ClientEndpointId)
	{
		const TArray<TSharedRef<FConcertFrontendLogFilter>> CommonFilters = Private::CreateCommonFilters();
		const TArray<TSharedRef<FConcertLogFilter>> NonVisuals = {
			MakeShared<FConcertLogFilter_Client>(ClientEndpointId)
		};
		return MakeShared<FConcertLogFilter_FrontendRoot>(
			MoveTemp(Tokenizer),
			CommonFilters,
			NonVisuals
			);
	}
}
