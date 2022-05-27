// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Filter/FilteredConcertLogList.h"
#include "TransportLogDelegates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SOverlay;
class SPromptConcertLoggingEnabled;
class FConcertLogTokenizer;
class FConcertLogFilter_FrontendRoot;
class FMenuBuilder;
class FPagedFilteredConcertLogList;
class IConcertLogSource;
class ITableRow;
class SHeaderRow;
class STableViewBase;
template <typename ItemType> class SListView;

struct FColumnVisibilitySnapshot;
struct FConcertLogEntry;

/**
 * Displays the contents of a IConcertLogSource and has UI for filtering.
 */
class SConcertTransportLog : public SCompoundWidget
{
public:

	static const FName FirstColumnId;

	SLATE_BEGIN_ARGS(SConcertTransportLog)
	{}
		/** Optional filters to display in UI */
		SLATE_ARGUMENT(TSharedPtr<FConcertLogFilter_FrontendRoot>, Filter)
		/** Used to better display client info, e.g. client name and colour */
		SLATE_EVENT(FGetClientInfo, GetClientInfo)
	SLATE_END_ARGS()
	virtual ~SConcertTransportLog() override;

	void Construct(const FArguments& InArgs, TSharedRef<IConcertLogSource> LogSource);

private:
	
	/** Used to overlay EnableLoggingPrompt over the tabs */
	TSharedPtr<SOverlay> EnableLoggingPromptOverlay;
	/** Reminds the user to enable logging */
	TSharedPtr<SPromptConcertLoggingEnabled> EnableLoggingPrompt;
	
	/** Sorts the log into pages whilst applying filters */
	TSharedPtr<FPagedFilteredConcertLogList> PagedLogList;
	/** Used by various systems to convert logs to text */
	TSharedPtr<FConcertLogTokenizer> LogTokenizer;

	/** Updates to be the content of the search text. Shared with all rows. */
	TSharedPtr<FText> HighlightText;
	
	/** Lists the logs */
	TSharedPtr<SListView<TSharedPtr<FConcertLogEntry>>> LogView;
	/** Header row of LogView */
	TSharedPtr<SHeaderRow> HeaderRow;

	FGetClientInfo GetClientInfoFunc;

	/** Whether to automatically scroll to new logs as they come in */
	bool bAutoScroll = true;
	/** Whether we currently loading the column visibility - prevents infinite event recursion */
	bool bIsUpdatingColumnVisibility = false;

	// Table view creation
	TSharedRef<SWidget> CreateTableView();
	TSharedRef<SHeaderRow> CreateHeaderRow();
	TSharedRef<ITableRow> OnGenerateActivityRowWidget(TSharedPtr<FConcertLogEntry> Item, const TSharedRef<STableViewBase>& OwnerTable) const;

	void ExtendViewOptions(FMenuBuilder& MenuBuilder);
	void OnFilterMenuChecked();
	void OnPageViewChanged(const TArray<TSharedPtr<FConcertLogEntry>>&);
	void OnSearchTextChanged(const FText& NewSearchText);
	void OnColumnVisibilitySettingsChanged(const FColumnVisibilitySnapshot& ColumnSnapshot);
	
	void OnConcertLoggingEnabledChanged(bool bNewEnabled);
};

