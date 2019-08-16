// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SStatsTableCell.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"

// Insights
#include "Insights/ViewModels/StatsViewColumn.h"
#include "Insights/ViewModels/StatsViewColumnFactory.h"
#include "Insights/Widgets/SStatsViewTooltip.h"

#define LOCTEXT_NAMESPACE "SStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SStatsTableCell::Construct(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	SetHoveredTableCellDelegate = InArgs._OnSetHoveredTableCell;
	StatsNodePtr = InArgs._StatsNodePtr;
	ColumnId = InArgs._ColumnId;
	//HighlightText = InArgs._HighlightText;

	ChildSlot
		[
			GenerateWidgetForColumn(InArgs, TableRow)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsTableCell::GenerateWidgetForColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	if (InArgs._IsStatsNameColumn)
	{
		return GenerateWidgetForNameColumn(InArgs, TableRow);
	}
	else
	{
		return GenerateWidgetForStatsColumn(InArgs, TableRow);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsTableCell::GenerateWidgetForNameColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	const FStatsViewColumn& Column = *FStatsViewColumnFactory::Get().ColumnIdToPtrMapping.FindChecked(ColumnId);

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
			.Visibility(this, &SStatsTableCell::GetHintIconVisibility)
			.Image(FEditorStyle::GetBrush("Profiler.Tooltip.HintIcon10"))
			.ToolTip(SStatsViewTooltip::GetTableCellTooltip(StatsNodePtr))
		]

		// Name
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(Column.HorizontalAlignment)
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(STextBlock)
			//.Text(StatsNodePtr->GetNameEx())
			.Text(this, &SStatsTableCell::GetNameEx)
			.HighlightText(InArgs._HighlightText)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.ColorAndOpacity(this, &SStatsTableCell::GetColorAndOpacity)
			.ShadowColorAndOpacity(this, &SStatsTableCell::GetShadowColorAndOpacity)
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
			//.OnClicked(this, &SStatsTableCell::ExpandCulledEvents_OnClicked)
			[
				SNew(SImage)
				.Visibility(this, &SStatsTableCell::GetCulledEventsIconVisibility)
				.Image(FEditorStyle::GetBrush("Profiler.EventGraph.HasCulledEventsSmall"))
				.ToolTipText(LOCTEXT("HasCulledEvents_TT", "This event contains culled children, if you want to see all children, please disable culling or use function details, or press this icon"))
			]
		]*/
		;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsTableCell::GenerateWidgetForStatsColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	const FStatsViewColumn& Column = *FStatsViewColumnFactory::Get().ColumnIdToPtrMapping.FindChecked(ColumnId);
	const FText FormattedValue = Column.GetFormattedValue(*StatsNodePtr);

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
			.ColorAndOpacity(this, &SStatsTableCell::GetStatsColorAndOpacity)
			.ShadowColorAndOpacity(this, &SStatsTableCell::GetShadowColorAndOpacity)
		];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
