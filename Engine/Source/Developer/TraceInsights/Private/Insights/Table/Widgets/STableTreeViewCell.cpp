// Copyright Epic Games, Inc. All Rights Reserved.

#include "STableTreeViewCell.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"

// Insights
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/Widgets/STableTreeViewRow.h"

#define LOCTEXT_NAMESPACE "STableTreeView"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void STableTreeViewCell::Construct(const FArguments& InArgs, const TSharedRef<ITableRow>& TableRow)
{
	TablePtr = InArgs._TablePtr;
	ColumnPtr = InArgs._ColumnPtr;
	TableTreeNodePtr = InArgs._TableTreeNodePtr;

	ensure(TablePtr.IsValid());
	ensure(ColumnPtr.IsValid());
	ensure(TableTreeNodePtr.IsValid());

	SetHoveredCellDelegate = InArgs._OnSetHoveredCell;

	ChildSlot
		[
			GenerateWidgetForColumn(InArgs, TableRow)
		];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STableTreeViewCell::GenerateWidgetForColumn(const FArguments& InArgs, const TSharedRef<ITableRow>& TableRow)
{
	if (InArgs._IsNameColumn)
	{
		return GenerateWidgetForNameColumn(InArgs, TableRow);
	}
	else
	{
		return GenerateWidgetForTableColumn(InArgs, TableRow);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STableTreeViewCell::GenerateWidgetForNameColumn(const FArguments& InArgs, const TSharedRef<ITableRow>& TableRow)
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
			.Visibility(this, &STableTreeViewCell::GetHintIconVisibility)
			.Image(FEditorStyle::GetBrush("Profiler.Tooltip.HintIcon10"))
			.ToolTip(GetRowToolTip(TableRow))
		]

		// Name
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(ColumnPtr->GetHorizontalAlignment())
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(this, &STableTreeViewCell::GetDisplayName)
			.HighlightText(InArgs._HighlightText)
			.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
			.ColorAndOpacity(this, &STableTreeViewCell::GetColorAndOpacity)
			.ShadowColorAndOpacity(this, &STableTreeViewCell::GetShadowColorAndOpacity)
		]
	;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<IToolTip> STableTreeViewCell::GetRowToolTip(const TSharedRef<ITableRow>& TableRow) const
{
	TSharedRef<STableTreeViewRow> Row = StaticCastSharedRef<STableTreeViewRow, ITableRow>(TableRow);
	return Row->GetRowToolTip();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText STableTreeViewCell::GetValueAsText() const
{
	return ColumnPtr->GetValueAsText(*TableTreeNodePtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STableTreeViewCell::GenerateWidgetForTableColumn(const FArguments& InArgs, const TSharedRef<ITableRow>& TableRow)
{
	// Note: For performance reason, init the cell text (value) only once.
	//       If we'll need to update values without recreating the table row/cell widgets, bind .Text to STableTreeViewCell::GetValueAsText
	//       or add API to explicitly update the text block.
	const FText CellText = ColumnPtr->GetValueAsText(*TableTreeNodePtr);

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
			.ColorAndOpacity(this, &STableTreeViewCell::GetStatsColorAndOpacity)
			.ShadowColorAndOpacity(this, &STableTreeViewCell::GetShadowColorAndOpacity)
		]
	;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
