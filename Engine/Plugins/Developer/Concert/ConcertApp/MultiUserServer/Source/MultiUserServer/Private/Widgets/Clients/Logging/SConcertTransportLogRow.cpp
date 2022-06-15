// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertTransportLogRow.h"

#include "Util/ConcertLogTokenizer.h"
#include "SConcertTransportLog.h"
#include "Session/Activity/PredefinedActivityColumns.h"

#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Text/STextBlock.h"

void SConcertTransportLogRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertLogEntry> InLogEntry, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedRef<FConcertLogTokenizer> InTokenizer, TSharedRef<FText> InHighlightText)
{
	LogEntry = MoveTemp(InLogEntry);
	Tokenizer = MoveTemp(InTokenizer);
	HighlightText = MoveTemp(InHighlightText);
	
	AvatarColor = InArgs._AvatarColor;
	
	SMultiColumnTableRow<TSharedPtr<FConcertLogEntry>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SConcertTransportLogRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == SConcertTransportLog::FirstColumnId)
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(2, 1)
			[
				SNew(SColorBlock)
				.Color(AvatarColor)
				.Size(FVector2D(4.f, 16.f))
			];
	}
	
	using FColumnWidgetFactoryFunc = TSharedRef<SWidget>(SConcertTransportLogRow::*)(const FName& InColumnName);
	const TMap<FName, FColumnWidgetFactoryFunc> OverrideFactories = {};
	const FColumnWidgetFactoryFunc FallbackFactoryFunc = &SConcertTransportLogRow::CreateDefaultColumn;

	const FColumnWidgetFactoryFunc* FactoryFunc = OverrideFactories.Find(ColumnName);
	return FactoryFunc
		? Invoke(*FactoryFunc, this, ColumnName)
		: Invoke(FallbackFactoryFunc, this, ColumnName);
}

TSharedRef<SWidget> SConcertTransportLogRow::CreateDefaultColumn(const FName& PropertyName)
{
	return SNew(STextBlock)
		.Text_Lambda([PropertyName, WeakEntry = TWeakPtr<FConcertLogEntry>(LogEntry), WeakTokenizer = TWeakPtr<FConcertLogTokenizer>(Tokenizer)]()
		{
			TSharedPtr<FConcertLogEntry> PinnedEntry = WeakEntry.Pin();
			TSharedPtr<FConcertLogTokenizer> PinnedTokenizer = WeakTokenizer.Pin();
			const FProperty* Property = FConcertLog::StaticStruct()->FindPropertyByName(PropertyName);
			if (ensure(PinnedEntry && PinnedTokenizer && Property))
			{
				return FText::FromString(PinnedTokenizer->Tokenize(PinnedEntry->Log, *Property));
			}
				
			return FText::GetEmpty();
		})
		.HighlightText_Lambda([this](){ return *HighlightText.Get(); });
}
