// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertLogFilter.h"

class FConcertLogTokenizer;
class FConcertFrontendLogFilter;
class FConcertFrontendLogFilter_TextSearch;

/** A filter that contains multiple UI filters */
class FConcertLogFilter_FrontendRoot : public FConcertLogFilter, public TSharedFromThis<FConcertLogFilter_FrontendRoot>
{
public:

	FConcertLogFilter_FrontendRoot(TSharedRef<FConcertLogTokenizer> Tokenizer, TArray<TSharedRef<FConcertFrontendLogFilter>> InCustomFilters);

	/** Builds the widget view for all contained filters */
	TSharedRef<SWidget> BuildFilterWidgets() const;
	
	//~ Begin IFilter Interface
	virtual bool PassesFilter(const FConcertLog& InItem) const override;
	//~ End IFilter Interface

	FORCEINLINE const TSharedRef<FConcertFrontendLogFilter_TextSearch>& GetTextSearchFilter() const { return TextSearchFilter; }
	FORCEINLINE const TArray<TSharedRef<FConcertFrontendLogFilter>>& GetFrontendFilters() const { return AllFrontendFilters; }

private:

	/** The text search filter. Also in FrontendFilters. Separate variable to build search bar in new line. */
	TSharedRef<FConcertFrontendLogFilter_TextSearch> TextSearchFilter;

	/** AllFrontendFilters without special filters we have as propteries above, such as TextSearchFilter. */
	TArray<TSharedRef<FConcertFrontendLogFilter>> CustomFilters;
	
	/** Filters that are combined using logical OR. */
	TArray<TSharedRef<FConcertFrontendLogFilter>> AllFrontendFilters;

	/** Builds the widgets that go under the text */
	TSharedRef<SWidget> BuildCustomFilterListWidget() const;
};

namespace UE::MultiUserServer
{
	/** Creates a filter for the global filter log window */
	TSharedRef<FConcertLogFilter_FrontendRoot> MakeGlobalLogFilter(TSharedRef<FConcertLogTokenizer> Tokenizer);
}
