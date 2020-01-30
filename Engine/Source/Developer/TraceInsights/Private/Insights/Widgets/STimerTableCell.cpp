// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimerTableCell.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"

// Insights
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Widgets/STimerTableRow.h"

#define LOCTEXT_NAMESPACE "STimersView"

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void STimerTableCell::Construct(const FArguments& InArgs, const TSharedRef<ITableRow>& TableRow)
{
	TablePtr = InArgs._TablePtr;
	ColumnPtr = InArgs._ColumnPtr;
	TimerNodePtr = InArgs._TimerNodePtr;

	ensure(TablePtr.IsValid());
	ensure(ColumnPtr.IsValid());
	ensure(TimerNodePtr.IsValid());

	SetHoveredCellDelegate = InArgs._OnSetHoveredCell;

	ChildSlot
		[
			GenerateWidgetForColumn(InArgs, TableRow)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STimerTableCell::GenerateWidgetForColumn(const FArguments& InArgs, const TSharedRef<ITableRow>& TableRow)
{
	if (InArgs._IsNameColumn)
	{
		return GenerateWidgetForNameColumn(InArgs, TableRow);
	}
	else
	{
		return GenerateWidgetForStatsColumn(InArgs, TableRow);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STimerTableCell::GenerateWidgetForNameColumn(const FArguments& InArgs, const TSharedRef<ITableRow>& TableRow)
{
	return
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SExpanderArrow, TableRow)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SOverlay)

			// Hot path icon
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Visibility(this, &STimerTableCell::GetHotPathIconVisibility)
				.Image(FEditorStyle::GetBrush(TEXT("Profiler.EventGraph.HotPathSmall")))
			]

			// Info icon + tooltip
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Visibility(this, &STimerTableCell::GetHintIconVisibility)
				.Image(FEditorStyle::GetBrush("Profiler.Tooltip.HintIcon10"))
				.ToolTip(GetRowToolTip(TableRow))
			]
		]

		// Name
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(ColumnPtr->GetHorizontalAlignment())
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(this, &STimerTableCell::GetDisplayName)
			.HighlightText(InArgs._HighlightText)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.ColorAndOpacity(this, &STimerTableCell::GetColorAndOpacity)
			.ShadowColorAndOpacity(this, &STimerTableCell::GetShadowColorAndOpacity)
		]
	;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<IToolTip> STimerTableCell::GetRowToolTip(const TSharedRef<ITableRow>& TableRow) const
{
	TSharedRef<STimerTableRow> Row = StaticCastSharedRef<STimerTableRow, ITableRow>(TableRow);
	return Row->GetRowToolTip();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STimerTableCell::GetValueAsText() const
{
	return ColumnPtr->GetValueAsText(*TimerNodePtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STimerTableCell::GenerateWidgetForStatsColumn(const FArguments& InArgs, const TSharedRef<ITableRow>& TableRow)
{
	return
		SNew(SHorizontalBox)

		// Value
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.HAlign(ColumnPtr->GetHorizontalAlignment())
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(this, &STimerTableCell::GetValueAsText)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.ColorAndOpacity(this, &STimerTableCell::GetStatsColorAndOpacity)
			.ShadowColorAndOpacity(this, &STimerTableCell::GetShadowColorAndOpacity)
		]
	;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
