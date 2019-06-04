// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SStatsTableRow.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/StatsViewColumnFactory.h"
#include "Insights/Widgets/SStatsViewTooltip.h"
#include "Insights/Widgets/SStatsTableCell.h"

#define LOCTEXT_NAMESPACE "SStatsView"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SStatsTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	OnShouldBeEnabled = InArgs._OnShouldBeEnabled;
	IsColumnVisibleDelegate = InArgs._OnIsColumnVisible;
	SetHoveredTableCellDelegate = InArgs._OnSetHoveredTableCell;
	GetColumnOutlineHAlignmentDelegate = InArgs._OnGetColumnOutlineHAlignmentDelegate;

	HighlightText = InArgs._HighlightText;
	HighlightedStatsName = InArgs._HighlightedStatsName;

	StatsNodePtr = InArgs._StatsNodePtr;

	SetEnabled(TAttribute<bool>(this, &SStatsTableRow::HandleShouldBeEnabled));

	SMultiColumnTableRow<FStatsNodePtr>::Construct(SMultiColumnTableRow<FStatsNodePtr>::FArguments(), InOwnerTableView);

	/*
	const FSlateBrush* const NodeIcon = StatsNodeTypeHelper::GetIconForStatsNodeType(InStatsNode->GetType());
	const TSharedRef<SToolTip> Tooltip = InStatsNode->IsGroup() ? SNew(SToolTip) : SStatsViewTooltip(InStatsNode->GetId()).GetTooltip();

	ChildSlot
		[
			SNew(SHorizontalBox)

			// Expander arrow.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SExpanderArrow, SharedThis(this))
			]

			// Icon to visualize group or timer type.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SImage)
				.Image(NodeIcon)
				.ToolTip(Tooltip)
			]

			// Description text.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(this, &SStatsTableRow::GetText)
				.HighlightText(InArgs._HighlightText)
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
				.ColorAndOpacity(this, &SStatsTableRow::GetColorAndOpacity)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(0.0f, 1.0f, 0.0f, 0.0f)
			[
				SNew(SImage)
				.Visibility(!InStatsNode->IsGroup() ? EVisibility::Visible : EVisibility::Collapsed)
				.Image(FEditorStyle::GetBrush("Profiler.Tooltip.HintIcon10"))
				.ToolTip(Tooltip)
			]
		];

	STableRow<FStatsNodePtr>::ConstructInternal(STableRow::FArguments().ShowSelection(true), InOwnerTableView);
	*/
}

TSharedRef<SWidget> SStatsTableRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	return

	SNew(SOverlay)
	.Visibility(EVisibility::SelfHitTestInvisible)

	+SOverlay::Slot()
	.Padding(0.0f)
	[
		SNew(SImage)
		.Image(FEditorStyle::GetBrush("Profiler.LineGraphArea"))
		.ColorAndOpacity(this, &SStatsTableRow::GetBackgroundColorAndOpacity)
	]

	+SOverlay::Slot()
	.Padding(0.0f)
	[
		SNew(SImage)
		.Image(this, &SStatsTableRow::GetOutlineBrush, ColumnId)
		.ColorAndOpacity(this, &SStatsTableRow::GetOutlineColorAndOpacity)
	]

	+SOverlay::Slot()
	[
		SNew(SStatsTableCell, SharedThis(this))
		.Visibility(this, &SStatsTableRow::IsColumnVisible, ColumnId)
		.StatsNodePtr(StatsNodePtr)
		.ColumnId(ColumnId)
		.HighlightText(HighlightText)
		.IsStatsNameColumn(ColumnId == FStatsViewColumnFactory::Get().Collection[0]->Id) // name column
		.OnSetHoveredTableCell(this, &SStatsTableRow::OnSetHoveredTableCell)
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply SStatsTableRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//im:TODO
	//if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	//{
	//	if (StatsNode->IsGroup())
	//	{
	//		// Add all timer Ids for the group.
	//		TArray<int32> StatsIds;
	//		const TArray<FStatsNodePtr>& FilteredChildren = StatsNode->GetFilteredChildren();
	//		const int32 NumFilteredChildren = FilteredChildren.Num();
	//
	//		StatsIds.Reserve(NumFilteredChildren);
	//		for (int32 Nx = 0; Nx < NumFilteredChildren; ++Nx)
	//		{
	//			StatsIds.Add(FilteredChildren[Nx]->GetId());
	//		}
	//
	//		return FReply::Handled().BeginDragDrop(FStatIDDragDropOp::NewGroup(StatsIds, StatsNode->GetName().GetPlainNameString()));
	//	}
	//	else
	//	{
	//		return FReply::Handled().BeginDragDrop(FStatIDDragDropOp::NewSingle(StatsNode->GetId(), StatsNode->GetName().GetPlainNameString()));
	//	}
	//}

	return SMultiColumnTableRow<FStatsNodePtr>::OnDragDetected(MyGeometry, MouseEvent);
}

FText SStatsTableRow::GetText() const
{
	FText Text = FText::GetEmpty();

	if (StatsNodePtr->IsGroup())
	{
		Text = FText::Format(LOCTEXT("StatsNode_GroupNodeTextFmt", "{0} ({1})"), FText::FromName(StatsNodePtr->GetName()), FText::AsNumber(StatsNodePtr->GetChildren().Num()));
	}
	else
	{
		Text = FText::FromName(StatsNodePtr->GetName());
	}

	return Text;
}

FSlateFontInfo SStatsTableRow::GetFont() const
{
	const bool bIsStatTracked = false;//im:TODO: FTimingProfilerManager::Get()->IsStatTracked(StatsNodePtr->GetStatsId());
	const FSlateFontInfo FontInfo = bIsStatTracked ? FEditorStyle::GetFontStyle("BoldFont") : FEditorStyle::GetFontStyle("NormalFont");
	return FontInfo;
}

FSlateColor SStatsTableRow::GetColorAndOpacity() const
{
	//const bool bIsStatTracked = FTimingProfilerManager::Get()->IsStatTracked(StatsNodePtr->GetId());
	//const FSlateColor Color = bIsStatTracked ? FTimingProfilerManager::Get()->GetColorForStatsId(StatsNodePtr->GetId()) : FLinearColor::White;
	//return Color;
	//FTimingProfilerManager::GetSettings().GetColorForStat(StatName)
	return FLinearColor::White;
}

FSlateColor SStatsTableRow::GetBackgroundColorAndOpacity() const
{
	//return GetBackgroundColorAndOpacity(StatsNodePtr->GetAggregatedStats().Sum);
	return FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

FSlateColor SStatsTableRow::GetBackgroundColorAndOpacity(double Time) const
{
	const FLinearColor Color =	Time > TimeUtils::Second      ? FLinearColor(0.3f, 0.0f, 0.0f, 1.0f) :
								Time > TimeUtils::Milisecond  ? FLinearColor(0.3f, 0.1f, 0.0f, 1.0f) :
								Time > TimeUtils::Microsecond ? FLinearColor(0.0f, 0.1f, 0.0f, 1.0f) :
																FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	return Color;
}

FSlateColor SStatsTableRow::GetOutlineColorAndOpacity() const
{
	const FLinearColor NoColor(0.0f, 0.0f, 0.0f, 0.0f);
	const bool bShouldBeHighlighted = StatsNodePtr->GetName() == HighlightedStatsName.Get();
	const FLinearColor OutlineColorAndOpacity = bShouldBeHighlighted ? FLinearColor(FColorList::SlateBlue) : NoColor;
	return OutlineColorAndOpacity;
}

const FSlateBrush* SStatsTableRow::GetOutlineBrush(const FName ColumnId) const
{
	EHorizontalAlignment Result = HAlign_Center;
	if (IsColumnVisibleDelegate.IsBound())
	{
		Result = GetColumnOutlineHAlignmentDelegate.Execute(ColumnId);
	}

	const FSlateBrush* Brush = nullptr;
	if (Result == HAlign_Left)
	{
		Brush = FEditorStyle::GetBrush("Profiler.EventGraph.Border.L");
	}
	else if(Result == HAlign_Right)
	{
		Brush = FEditorStyle::GetBrush("Profiler.EventGraph.Border.R");
	}
	else
	{
		Brush = FEditorStyle::GetBrush("Profiler.EventGraph.Border.TB");
	}
	return Brush;
}

bool SStatsTableRow::HandleShouldBeEnabled() const
{
	bool bResult = false;

	if (StatsNodePtr->IsGroup())
	{
		bResult = true;
	}
	else
	{
		if (OnShouldBeEnabled.IsBound())
		{
			bResult = OnShouldBeEnabled.Execute(StatsNodePtr->GetId());
		}
	}

	return bResult;
}

EVisibility SStatsTableRow::IsColumnVisible(const FName ColumnId) const
{
	EVisibility Result = EVisibility::Collapsed;

	if (IsColumnVisibleDelegate.IsBound())
	{
		Result = IsColumnVisibleDelegate.Execute(ColumnId) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	return Result;
}

void SStatsTableRow::OnSetHoveredTableCell(const FName InColumnId, const FStatsNodePtr InSamplePtr)
{
	SetHoveredTableCellDelegate.ExecuteIfBound(InColumnId, InSamplePtr);
}

#undef LOCTEXT_NAMESPACE
