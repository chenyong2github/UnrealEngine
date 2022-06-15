// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertLogEntry.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STableRow.h"

class FConcertLogTokenizer;

class SConcertTransportLogRow : public SMultiColumnTableRow<TSharedPtr<FConcertLogEntry>>
{
public:

	SLATE_BEGIN_ARGS(SConcertTransportLogRow)
	{}
		SLATE_ARGUMENT(FLinearColor, AvatarColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FConcertLogEntry> InLogEntry, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedRef<FConcertLogTokenizer> InTokenizer, TSharedRef<FText> InHighlightText);

	//~ Begin SMultiColumnTableRow Interface
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
	//~ End SMultiColumnTableRow Interface
	
private:

	TSharedPtr<FConcertLogEntry> LogEntry;
	/** Used to convert some members into display strings */
	TSharedPtr<FConcertLogTokenizer> Tokenizer;

	/** Owned by SConcertTransportLog. Updated with the search text. */
	TSharedPtr<FText> HighlightText;

	FLinearColor AvatarColor;

	TSharedRef<SWidget> CreateDefaultColumn(const FName& PropertyName);
};
