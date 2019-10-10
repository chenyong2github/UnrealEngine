// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNetStatsTableCell.h"

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
#include "Insights/NetworkingProfiler/Widgets/SNetStatsViewTooltip.h"

#define LOCTEXT_NAMESPACE "SNetStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SNetStatsTableCell::Construct(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	TablePtr = InArgs._TablePtr;
	ColumnPtr = InArgs._ColumnPtr;
	NetEventNodePtr = InArgs._NetEventNodePtr;

	ensure(TablePtr.IsValid());
	ensure(ColumnPtr.IsValid());
	ensure(NetEventNodePtr.IsValid());

	SetHoveredCellDelegate = InArgs._OnSetHoveredCell;

	ChildSlot
		[
			GenerateWidgetForColumn(InArgs, TableRow)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SNetStatsTableCell::GenerateWidgetForColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
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

TSharedRef<SWidget> SNetStatsTableCell::GenerateWidgetForNameColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
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

		// Info icon + tooltip
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Visibility(this, &SNetStatsTableCell::GetHintIconVisibility)
			.Image(FEditorStyle::GetBrush("Profiler.Tooltip.HintIcon10"))
			.ToolTip(SNetStatsViewTooltip::GetCellTooltip(NetEventNodePtr, ColumnPtr))
		]

		// Name
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(ColumnPtr->GetHorizontalAlignment())
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(this, &SNetStatsTableCell::GetDisplayName)
			.HighlightText(InArgs._HighlightText)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.ColorAndOpacity(this, &SNetStatsTableCell::GetColorAndOpacity)
			.ShadowColorAndOpacity(this, &SNetStatsTableCell::GetShadowColorAndOpacity)
		]
	;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SNetStatsTableCell::GenerateWidgetForStatsColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow)
{
	const FText CellText = NetEventNodePtr->IsGroup() ? FText::GetEmpty() : ColumnPtr->GetValueAsText(*NetEventNodePtr);

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
			.Text(CellText)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.ColorAndOpacity(this, &SNetStatsTableCell::GetStatsColorAndOpacity)
			.ShadowColorAndOpacity(this, &SNetStatsTableCell::GetShadowColorAndOpacity)
		]
	;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
