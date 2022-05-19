// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Filter/FilteredConcertLogList.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FMenuBuilder;
class STextBlock;
template<typename T> class SSpinBox;
struct FConcertLogEntry;

/** Displays the number of pages and items. Displayed under the table view. */
class SConcertTransportLogFooter : public SCompoundWidget
{
public:
	
	DECLARE_DELEGATE_OneParam(FExtendContextMenu, FMenuBuilder&)

	SLATE_BEGIN_ARGS(SConcertTransportLogFooter) {}
		SLATE_EVENT(FExtendContextMenu, ExtendViewOptions)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FPagedFilteredConcertLogList> InPagedLogList);

private:

	/** The model we'll be updating */
	TSharedPtr<FPagedFilteredConcertLogList> PagedLogList;

	/** Selects the current page */
	TSharedPtr<SSpinBox<FPagedFilteredConcertLogList::FPageCount>> CurrentPage;
	/** Displays the number of pages */
	TSharedPtr<STextBlock> PageCounterText;

	TSharedRef<SWidget> MakeViewOptionsMenuWidget(FExtendContextMenu ExtendViewOptions);
};
