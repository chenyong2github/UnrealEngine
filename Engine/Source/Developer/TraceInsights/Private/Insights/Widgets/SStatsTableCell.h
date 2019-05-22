// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// Insights
#include "Insights/ViewModels/StatsNode.h"

DECLARE_DELEGATE_TwoParams(FSetHoveredStatsTableCell, const FName /*ColumnId*/, const FStatsNodePtr /*SamplePtr*/);
DECLARE_DELEGATE_RetVal_OneParam(bool, FIsColumnVisibleDelegate, const FName /*ColumnId*/);
DECLARE_DELEGATE_RetVal_OneParam(EHorizontalAlignment, FGetColumnOutlineHAlignmentDelegate, const FName /*ColumnId*/);

class SStatsTableCell : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStatsTableCell) {}
		SLATE_EVENT(FSetHoveredStatsTableCell, OnSetHoveredTableCell)
		SLATE_ATTRIBUTE(FText, HighlightText)
		SLATE_ARGUMENT(FStatsNodePtr, StatsNodePtr)
		SLATE_ARGUMENT(FName, ColumnId)
		SLATE_ARGUMENT(bool, IsStatsNameColumn)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow);

protected:
	TSharedRef<SWidget> GenerateWidgetForColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow);
	TSharedRef<SWidget> GenerateWidgetForNameColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow);
	TSharedRef<SWidget> GenerateWidgetForStatsColumn(const FArguments& InArgs, const TSharedRef<class ITableRow>& TableRow);

	/**
	 * The system will use this event to notify a widget that the cursor has entered it. This event is NOT bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 */
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
		SetHoveredTableCellDelegate.ExecuteIfBound(ColumnId, StatsNodePtr);
	}

	/**
	 * The system will use this event to notify a widget that the cursor has left it. This event is NOT bubbled.
	 *
	 * @param MouseEvent Information about the input event
	 */
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		SCompoundWidget::OnMouseLeave(MouseEvent);
		SetHoveredTableCellDelegate.ExecuteIfBound(NAME_None, nullptr);
	}

	/**
	 * Called during drag and drop when the drag enters a widget.
	 *
	 * Enter/Leave events in slate are meant as lightweight notifications.
	 * So we do not want to capture mouse or set focus in response to these.
	 * However, OnDragEnter must also support external APIs (e.g. OLE Drag/Drop)
	 * Those require that we let them know whether we can handle the content
	 * being dragged OnDragEnter.
	 *
	 * The concession is to return a can_handled/cannot_handle
	 * boolean rather than a full FReply.
	 *
	 * @param MyGeometry      The geometry of the widget receiving the event.
	 * @param DragDropEvent   The drag and drop event.
	 *
	 * @return A reply that indicated whether the contents of the DragDropEvent can potentially be processed by this widget.
	 */
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
	{
		SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);
		SetHoveredTableCellDelegate.ExecuteIfBound(ColumnId, StatsNodePtr);
	}

	/**
	 * Called during drag and drop when the drag leaves a widget.
	 *
	 * @param DragDropEvent   The drag and drop event.
	 */
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent)  override
	{
		SCompoundWidget::OnDragLeave(DragDropEvent);
		SetHoveredTableCellDelegate.ExecuteIfBound(NAME_None, nullptr);
	}

	EVisibility GetHintIconVisibility() const
	{
		return IsHovered() ? EVisibility::Visible : EVisibility::Hidden;
	}

	EVisibility GetCulledEventsIconVisibility() const
	{
		//return StatsNodePtr->HasCulledChildren() ? EVisibility::Visible : EVisibility::Collapsed;
		return EVisibility::Collapsed;
	}

	FText GetNameEx() const
	{
		return StatsNodePtr->GetNameEx();
	}

	FSlateColor GetColorAndOpacity() const
	{
		const FLinearColor TextColor =
			StatsNodePtr->IsFiltered() ? FLinearColor(1.0f, 1.0f, 1.0f, 0.5f) :
			FLinearColor::White;
		return TextColor;
	}

	FSlateColor GetStatsColorAndOpacity() const
	{
		const FLinearColor TextColor =
			StatsNodePtr->IsFiltered() ? FLinearColor(1.0f, 1.0f, 1.0f, 0.5f) :
			StatsNodePtr->GetAggregatedStats().Count == 0 ? FLinearColor(1.0f, 1.0f, 1.0f, 0.6f) :
			FLinearColor::White;
		return TextColor;
	}

	FLinearColor GetShadowColorAndOpacity() const
	{
		const FLinearColor ShadowColor =
			StatsNodePtr->IsFiltered() ? FLinearColor(0.f, 0.f, 0.f, 0.25f) :
			FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);
		return ShadowColor;
	}

protected:
	/** A shared pointer to the stats node. */
	FStatsNodePtr StatsNodePtr;

	/** The Id of the column where this stats belongs. */
	FName ColumnId;

	FSetHoveredStatsTableCell SetHoveredTableCellDelegate;
};
