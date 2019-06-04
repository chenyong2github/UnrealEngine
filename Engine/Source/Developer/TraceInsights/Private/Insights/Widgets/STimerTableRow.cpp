// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STimerTableRow.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/TimersViewColumnFactory.h"
#include "Insights/Widgets/STimersViewTooltip.h"
#include "Insights/Widgets/STimerTableCell.h"

#define LOCTEXT_NAMESPACE "STimersView"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void STimerTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	OnShouldBeEnabled = InArgs._OnShouldBeEnabled;
	IsColumnVisibleDelegate = InArgs._OnIsColumnVisible;
	SetHoveredTableCellDelegate = InArgs._OnSetHoveredTableCell;
	GetColumnOutlineHAlignmentDelegate = InArgs._OnGetColumnOutlineHAlignmentDelegate;

	HighlightText = InArgs._HighlightText;
	HighlightedTimerName = InArgs._HighlightedTimerName;

	TimerNodePtr = InArgs._TimerNodePtr;

	SetEnabled(TAttribute<bool>(this, &STimerTableRow::HandleShouldBeEnabled));

	SMultiColumnTableRow<FTimerNodePtr>::Construct(SMultiColumnTableRow<FTimerNodePtr>::FArguments(), InOwnerTableView);

	/*
	const FSlateBrush* const NodeIcon = TimerNodeTypeHelper::GetIconForTimerNodeType(InTimerNode->GetType());
	const TSharedRef<SToolTip> Tooltip = InTimerNode->IsGroup() ? SNew(SToolTip) : STimersViewTooltip(InTimerNode->GetId()).GetTooltip();

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
				.Text(this, &STimerTableRow::GetText)
				.HighlightText(InArgs._HighlightText)
				.TextStyle(FEditorStyle::Get(), TEXT("Profiler.Tooltip"))
				.ColorAndOpacity(this, &STimerTableRow::GetColorAndOpacity)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(0.0f, 1.0f, 0.0f, 0.0f)
			[
				SNew(SImage)
				.Visibility(!InTimerNode->IsGroup() ? EVisibility::Visible : EVisibility::Collapsed)
				.Image(FEditorStyle::GetBrush("Profiler.Tooltip.HintIcon10"))
				.ToolTip(Tooltip)
			]
		];

	STableRow<FTimerNodePtr>::ConstructInternal(STableRow::FArguments().ShowSelection(true), InOwnerTableView);
	*/
}

TSharedRef<SWidget> STimerTableRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	return

	SNew(SOverlay)
	.Visibility(EVisibility::SelfHitTestInvisible)

	+SOverlay::Slot()
	.Padding(0.0f)
	[
		SNew(SImage)
		.Image(FEditorStyle::GetBrush("Profiler.LineGraphArea"))
		.ColorAndOpacity(this, &STimerTableRow::GetBackgroundColorAndOpacity)
	]

	+SOverlay::Slot()
	.Padding(0.0f)
	[
		SNew(SImage)
		.Image(this, &STimerTableRow::GetOutlineBrush, ColumnId)
		.ColorAndOpacity(this, &STimerTableRow::GetOutlineColorAndOpacity)
	]

	+SOverlay::Slot()
	[
		SNew(STimerTableCell, SharedThis(this))
		.Visibility(this, &STimerTableRow::IsColumnVisible, ColumnId)
		.TimerNodePtr(TimerNodePtr)
		.ColumnId(ColumnId)
		.HighlightText(HighlightText)
		.IsTimerNameColumn(ColumnId == FTimersViewColumnFactory::Get().Collection[0]->Id) // name column
		.OnSetHoveredTableCell(this, &STimerTableRow::OnSetHoveredTableCell)
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply STimerTableRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//im:TODO
	//if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	//{
	//	if (TimerNode->IsGroup())
	//	{
	//		// Add all timer Ids for the group.
	//		TArray<int32> TimerIds;
	//		const TArray<FTimerNodePtr>& FilteredChildren = TimerNode->GetFilteredChildren();
	//		const int32 NumFilteredChildren = FilteredChildren.Num();
	//
	//		TimerIds.Reserve(NumFilteredChildren);
	//		for (int32 Nx = 0; Nx < NumFilteredChildren; ++Nx)
	//		{
	//			TimerIds.Add(FilteredChildren[Nx]->GetId());
	//		}
	//
	//		return FReply::Handled().BeginDragDrop(FStatIDDragDropOp::NewGroup(TimerIds, TimerNode->GetName().GetPlainNameString()));
	//	}
	//	else
	//	{
	//		return FReply::Handled().BeginDragDrop(FStatIDDragDropOp::NewSingle(TimerNode->GetId(), TimerNode->GetName().GetPlainNameString()));
	//	}
	//}

	return SMultiColumnTableRow<FTimerNodePtr>::OnDragDetected(MyGeometry, MouseEvent);
}

FText STimerTableRow::GetText() const
{
	FText Text = FText::GetEmpty();

	if (TimerNodePtr->IsGroup())
	{
		Text = FText::Format(LOCTEXT("TimerNode_GroupNodeTextFmt", "{0} ({1})"), FText::FromName(TimerNodePtr->GetName()), FText::AsNumber(TimerNodePtr->GetChildren().Num()));
	}
	else
	{
		Text = FText::FromName(TimerNodePtr->GetName());
	}

	return Text;
}

FSlateFontInfo STimerTableRow::GetFont() const
{
	const bool bIsStatTracked = false;//im:TODO: FTimingProfilerManager::Get()->IsStatTracked(TimerNodePtr->GetTimerId());
	const FSlateFontInfo FontInfo = bIsStatTracked ? FEditorStyle::GetFontStyle("BoldFont") : FEditorStyle::GetFontStyle("NormalFont");
	return FontInfo;
}

FSlateColor STimerTableRow::GetColorAndOpacity() const
{
	//const bool bIsStatTracked = FTimingProfilerManager::Get()->IsStatTracked(TimerNodePtr->GetId());
	//const FSlateColor Color = bIsStatTracked ? FTimingProfilerManager::Get()->GetColorForTimerId(TimerNodePtr->GetId()) : FLinearColor::White;
	//return Color;
	//FTimingProfilerManager::GetSettings().GetColorForStat(StatName)
	return FLinearColor::White;
}

FSlateColor STimerTableRow::GetBackgroundColorAndOpacity() const
{
	return GetBackgroundColorAndOpacity(TimerNodePtr->GetAggregatedStats().TotalInclusiveTime);
	//return FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)
}

FSlateColor STimerTableRow::GetBackgroundColorAndOpacity(double Time) const
{
	const FLinearColor Color =	Time > TimeUtils::Second      ? FLinearColor(0.3f, 0.0f, 0.0f, 1.0f) :
								Time > TimeUtils::Milisecond  ? FLinearColor(0.3f, 0.1f, 0.0f, 1.0f) :
								Time > TimeUtils::Microsecond ? FLinearColor(0.0f, 0.1f, 0.0f, 1.0f) :
																FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	return Color;
}

FSlateColor STimerTableRow::GetOutlineColorAndOpacity() const
{
	const FLinearColor NoColor(0.0f, 0.0f, 0.0f, 0.0f);
	const bool bShouldBeHighlighted = TimerNodePtr->GetName() == HighlightedTimerName.Get();
	const FLinearColor OutlineColorAndOpacity = bShouldBeHighlighted ? FLinearColor(FColorList::SlateBlue) : NoColor;
	return OutlineColorAndOpacity;
}

const FSlateBrush* STimerTableRow::GetOutlineBrush(const FName ColumnId) const
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

bool STimerTableRow::HandleShouldBeEnabled() const
{
	bool bResult = false;

	if (TimerNodePtr->IsGroup())
	{
		bResult = true;
	}
	else
	{
		if (OnShouldBeEnabled.IsBound())
		{
			bResult = OnShouldBeEnabled.Execute(TimerNodePtr->GetId());
		}
	}

	return bResult;
}

EVisibility STimerTableRow::IsColumnVisible(const FName ColumnId) const
{
	EVisibility Result = EVisibility::Collapsed;

	if (IsColumnVisibleDelegate.IsBound())
	{
		Result = IsColumnVisibleDelegate.Execute(ColumnId) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	return Result;
}

void STimerTableRow::OnSetHoveredTableCell(const FName InColumnId, const FTimerNodePtr InSamplePtr)
{
	SetHoveredTableCellDelegate.ExecuteIfBound(InColumnId, InSamplePtr);
}

#undef LOCTEXT_NAMESPACE
