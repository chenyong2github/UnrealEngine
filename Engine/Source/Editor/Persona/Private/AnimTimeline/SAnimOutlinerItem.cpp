// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAnimOutlinerItem.h"
#include "AnimTimelineTrack.h"
#include "Widgets/Text/STextBlock.h"
#include "SAnimOutliner.h"
#include "Widgets/SOverlay.h"
#include "SAnimTrackResizeArea.h"

SAnimOutlinerItem::~SAnimOutlinerItem()
{
	TSharedPtr<SAnimOutliner> Outliner = StaticCastSharedPtr<SAnimOutliner>(OwnerTablePtr.Pin());
	TSharedPtr<FAnimTimelineTrack> PinnedTrack = Track.Pin();
	if (Outliner.IsValid() && PinnedTrack.IsValid())
	{
		Outliner->OnChildRowRemoved(PinnedTrack.ToSharedRef());
	}
}

void SAnimOutlinerItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedRef<FAnimTimelineTrack>& InTrack)
{
	Track = InTrack;
	OnGenerateWidgetForColumn = InArgs._OnGenerateWidgetForColumn;
	HighlightText = InArgs._HighlightText;

	SMultiColumnTableRow::Construct(
		SMultiColumnTableRow::FArguments()
			.ShowSelection(true),
		InOwnerTableView);
}

TSharedRef<SWidget> SAnimOutlinerItem::GenerateWidgetForColumn(const FName& ColumnId)
{
	TSharedPtr<FAnimTimelineTrack> PinnedTrack = Track.Pin();
	if (PinnedTrack.IsValid())
	{
		TSharedPtr<SWidget> ColumnWidget = SNullWidget::NullWidget;
		if(OnGenerateWidgetForColumn.IsBound())
		{
			ColumnWidget = OnGenerateWidgetForColumn.Execute(PinnedTrack.ToSharedRef(), ColumnId, SharedThis(this));
		}

		return SNew(SOverlay)
		+SOverlay::Slot()
		[
			ColumnWidget.ToSharedRef()
		];
	}

	return SNullWidget::NullWidget;
}

void SAnimOutlinerItem::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	StaticCastSharedPtr<SAnimOutliner>(OwnerTablePtr.Pin())->ReportChildRowGeometry(Track.Pin().ToSharedRef(), AllottedGeometry);
}

FVector2D SAnimOutlinerItem::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	TSharedPtr<FAnimTimelineTrack> PinnedTrack = Track.Pin();
	if(PinnedTrack.IsValid())
	{
		return FVector2D(100.0f, PinnedTrack->GetHeight() + PinnedTrack->GetPadding().Combined());
	}

	return FVector2D(100.0f, 16.0f);
}

void SAnimOutlinerItem::AddTrackAreaReference(const TSharedPtr<SAnimTrack>& InTrackWidget)
{
	TrackWidget = InTrackWidget;
}

void SAnimOutlinerItem::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SMultiColumnTableRow<TSharedRef<FAnimTimelineTrack>>::OnMouseEnter(MyGeometry, MouseEvent);

	TSharedPtr<FAnimTimelineTrack> PinnedTrack = Track.Pin();
	if(PinnedTrack.IsValid())
	{
		PinnedTrack->SetHovered(true);
	}
}

void SAnimOutlinerItem::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SMultiColumnTableRow<TSharedRef<FAnimTimelineTrack>>::OnMouseLeave(MouseEvent);

	TSharedPtr<FAnimTimelineTrack> PinnedTrack = Track.Pin();
	if(PinnedTrack.IsValid())
	{
		PinnedTrack->SetHovered(false);
	}
}

bool SAnimOutlinerItem::IsHovered() const
{
	TSharedPtr<FAnimTimelineTrack> PinnedTrack = Track.Pin();
	if(PinnedTrack.IsValid())
	{
		return SMultiColumnTableRow<TSharedRef<FAnimTimelineTrack>>::IsHovered() || PinnedTrack->IsHovered();
	}

	return SMultiColumnTableRow<TSharedRef<FAnimTimelineTrack>>::IsHovered();
}