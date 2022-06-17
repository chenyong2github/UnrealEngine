// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertFrontendLogFilter.h"
#include "Misc/TextFilter.h"
#include "Widgets/Input/SSearchBox.h"

class FConcertLogTokenizer;

/** Allows advanced search by text. Implements Adapter pattern to wrap TTextFilter. */
class FConcertLogFilter_TextSearch : public FConcertLogFilter
{
public:

	FConcertLogFilter_TextSearch(TSharedRef<FConcertLogTokenizer> Tokenizer);
	
	//~ Begin FConcertLogFilter Interface
	virtual bool PassesFilter(const FConcertLogEntry& InItem) const override { return TextFilter.PassesFilter(InItem); }
	//~ End FConcertLogFilter Interface
	
	void SetRawFilterText(const FText& InFilterText) { TextFilter.SetRawFilterText(InFilterText); }
	
private:

	/** Does the actual string search */
	TTextFilter<const FConcertLogEntry&> TextFilter;
	/** Helps in converting FConcertLog members into search terms */
	TSharedRef<FConcertLogTokenizer> Tokenizer;

	/** Parses InItem into a bunch of strings that can be searched */
	void GenerateSearchTerms(const FConcertLogEntry& InItem, TArray<FString>& OutTerms) const;
};

/** Creates a search bar */
class FConcertFrontendLogFilter_TextSearch : public TConcertFrontendLogFilterAggregate<FConcertLogFilter_TextSearch, SSearchBox>
{
	using Super = TConcertFrontendLogFilterAggregate<FConcertLogFilter_TextSearch, SSearchBox>;
public:

	FConcertFrontendLogFilter_TextSearch(TSharedRef<FConcertLogTokenizer> Tokenizer);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSearchTextChanged, const FText& /*NewSearchText*/);
	FOnSearchTextChanged& OnSearchTextChanged() { return OnSearchTextChangedEvent; }

private:

	/** Useful to let external subscriber handle text highlighting */
	FOnSearchTextChanged OnSearchTextChangedEvent;
};
