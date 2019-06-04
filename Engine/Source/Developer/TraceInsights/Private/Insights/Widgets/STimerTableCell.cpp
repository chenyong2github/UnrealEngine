// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STimerTableCell.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"

// Insights
#include "Insights/ViewModels/TimersViewColumn.h"
#include "Insights/ViewModels/TimersViewColumnFactory.h"
#include "Insights/Widgets/STimersViewTooltip.h"

#define LOCTEXT_NAMESPACE "STimersView"

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void STimerTableCell::Construct(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	SetHoveredTableCellDelegate = InArgs._OnSetHoveredTableCell;
	TimerNodePtr = InArgs._TimerNodePtr;
	ColumnId = InArgs._ColumnId;
	//HighlightText = InArgs._HighlightText;

	ChildSlot
		[
			GenerateWidgetForColumn(InArgs, TableRow)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STimerTableCell::GenerateWidgetForColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	if (InArgs._IsTimerNameColumn)
	{
		return GenerateWidgetForNameColumn(InArgs, TableRow);
	}
	else
	{
		return GenerateWidgetForStatsColumn(InArgs, TableRow);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STimerTableCell::GenerateWidgetForNameColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	const FTimersViewColumn& Column = *FTimersViewColumnFactory::Get().ColumnIdToPtrMapping.FindChecked(ColumnId);

	return
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SExpanderArrow, TableRow)
		]

		// Event info icon + tooltip.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Visibility(this, &STimerTableCell::GetHintIconVisibility)
			.Image(FEditorStyle::GetBrush("Profiler.Tooltip.HintIcon10"))
			.ToolTip(STimersViewTooltip::GetTableCellTooltip(TimerNodePtr))
		]

		// Name
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(Column.HorizontalAlignment)
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(STextBlock)
			//.Text(TimerNodePtr->GetNameEx())
			.Text(this, &STimerTableCell::GetNameEx)
			.HighlightText(InArgs._HighlightText)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.ColorAndOpacity(this, &STimerTableCell::GetColorAndOpacity)
			.ShadowColorAndOpacity(this, &STimerTableCell::GetShadowColorAndOpacity)
		]

		/*
		// Culled children warning icon + tooltip.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), TEXT("HoverHintOnly"))
			.ContentPadding(0.0f)
			.IsFocusable(false)
			//.OnClicked(this, &STimerTableCell::ExpandCulledEvents_OnClicked)
			[
				SNew(SImage)
				.Visibility(this, &STimerTableCell::GetCulledEventsIconVisibility)
				.Image(FEditorStyle::GetBrush("Profiler.EventGraph.HasCulledEventsSmall"))
				.ToolTipText(LOCTEXT("HasCulledEvents_TT", "This event contains culled children, if you want to see all children, please disable culling or use function details, or press this icon"))
			]
		]*/
		;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STimerTableCell::GenerateWidgetForStatsColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	const FTimersViewColumn& Column = *FTimersViewColumnFactory::Get().ColumnIdToPtrMapping.FindChecked(ColumnId);
	const FText FormattedValue = Column.GetFormattedValue(*TimerNodePtr);

	return
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.HAlign(Column.HorizontalAlignment)
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(FormattedValue)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.ColorAndOpacity(this, &STimerTableCell::GetStatsColorAndOpacity)
			.ShadowColorAndOpacity(this, &STimerTableCell::GetShadowColorAndOpacity)
		];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
