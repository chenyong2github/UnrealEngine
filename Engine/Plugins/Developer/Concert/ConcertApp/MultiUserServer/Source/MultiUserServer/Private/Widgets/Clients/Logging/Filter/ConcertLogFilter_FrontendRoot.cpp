// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertLogFilter_FrontendRoot.h"

#include "ConcertFrontendLogFilter.h"
#include "ConcertFrontendLogFilter_MessageAction.h"
#include "ConcertFrontendLogFilter_MessageType.h"
#include "ConcertFrontendLogFilter_TextSearch.h"
#include "ConcertFrontendLogFilter_Time.h"
#include "Algo/AllOf.h"

#include "Algo/AnyOf.h"
#include "Widgets/SBoxPanel.h"

FConcertLogFilter_FrontendRoot::FConcertLogFilter_FrontendRoot(TSharedRef<FConcertLogTokenizer> Tokenizer, TArray<TSharedRef<FConcertFrontendLogFilter>> InCustomFilters)
	: TextSearchFilter(MakeShared<FConcertFrontendLogFilter_TextSearch>(MoveTemp(Tokenizer)))
	, CustomFilters(MoveTemp(InCustomFilters))
	, AllFrontendFilters(CustomFilters)
{
	AllFrontendFilters.Add(TextSearchFilter);
	
	for (const TSharedRef<FConcertFrontendLogFilter>& Filter : AllFrontendFilters)
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
		[&InItem](const TSharedRef<FConcertFrontendLogFilter>& AndFilter){ return AndFilter->PassesFilter(InItem); }
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

namespace UE::MultiUserServer::Private
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

TSharedRef<FConcertLogFilter_FrontendRoot> UE::MultiUserServer::MakeGlobalLogFilter(TSharedRef<FConcertLogTokenizer> Tokenizer)
{
	return MakeShared<FConcertLogFilter_FrontendRoot>(
		MoveTemp(Tokenizer),
		Private::CreateCommonFilters()
		);
}
