// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertFrontendLogFilter_TextSearch.h"

#include "ConcertTransportEvents.h"
#include "Widgets/Clients/Logging/Util/ConcertLogTokenizer.h"

FConcertLogFilter_TextSearch::FConcertLogFilter_TextSearch(TSharedRef<FConcertLogTokenizer> Tokenizer)
	: TextFilter(TTextFilter<const FConcertLog&>::FItemToStringArray::CreateRaw(this, &FConcertLogFilter_TextSearch::GenerateSearchTerms))
	, Tokenizer(MoveTemp(Tokenizer))
{
	TextFilter.OnChanged().AddLambda([this]
	{
		OnChanged().Broadcast();
	});
}

void FConcertLogFilter_TextSearch::GenerateSearchTerms(const FConcertLog& InItem, TArray<FString>& OutTerms) const
{
	for (TFieldIterator<const FProperty> PropertyIt(FConcertLog::StaticStruct()); PropertyIt; ++PropertyIt)
	{
		OutTerms.Add(Tokenizer->Tokenize(InItem, **PropertyIt));
	}
}

FConcertFrontendLogFilter_TextSearch::FConcertFrontendLogFilter_TextSearch(TSharedRef<FConcertLogTokenizer> Tokenizer)
	: Super(MoveTemp(Tokenizer))
{
	ChildSlot = SNew(SSearchBox)
		.OnTextChanged_Lambda([this](const FText& NewSearchText)
		{
			OnSearchTextChanged().Broadcast(NewSearchText);
			Implementation.SetRawFilterText(NewSearchText);
		})
		.DelayChangeNotificationsWhileTyping(true);
}
