// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STimingView.h"

#include "Containers/ArrayBuilder.h"
#include "Containers/MapBuilder.h"
#include "EditorStyleSet.h"
#include "Features/IModularFeatures.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformTime.h"
#include "Layout/WidgetPath.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "SlateOptMacros.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/ITimingViewExtender.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/LoadingProfiler/LoadingProfilerManager.h"
#include "Insights/LoadingProfiler/Widgets/SLoadingProfilerWindow.h"
#include "Insights/Table/Widgets/STableTreeView.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/BaseTimingTrack.h"
#include "Insights/ViewModels/DrawHelpers.h"
#include "Insights/ViewModels/FileActivityTimingTrack.h"
#include "Insights/ViewModels/GraphSeries.h"
#include "Insights/ViewModels/GraphTrack.h"
#include "Insights/ViewModels/LoadingTimingTrack.h"
#include "Insights/ViewModels/MarkersTimingTrack.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/ViewModels/TimeRulerTrack.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/TimingGraphTrack.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/Widgets/SStatsView.h"
#include "Insights/Widgets/STimersView.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "STimingView"

// start auto generated ids from a big number (MSB set to 1) to avoid collisions with ids for gpu/cpu tracks based on 32bit timeline index
uint64 FBaseTimingTrack::IdGenerator = (1ULL << 63);

const TCHAR* GetFileActivityTypeName(Trace::EFileActivityType Type);
uint32 GetFileActivityTypeColor(Trace::EFileActivityType Type);

namespace Insights { const FName TimingViewExtenderFeatureName(TEXT("TimingViewExtender")); }

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingView::STimingView()
	: bScrollableTracksOrderIsDirty(false)
	, ThreadTimingSharedState(MakeShared<FThreadTimingSharedState>(this))
	, LoadingSharedState(MakeShared<FLoadingSharedState>(this))
	, bAssetLoadingMode(false)
	, FileActivitySharedState(MakeShared<FFileActivitySharedState>(this))
	, TimeRulerTrack(MakeShared<FTimeRulerTrack>())
	, MarkersTrack(MakeShared<FMarkersTimingTrack>())
	, GraphTrack(MakeShared<FTimingGraphTrack>())
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, MainFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
	IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, ThreadTimingSharedState.Get());
	IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, LoadingSharedState.Get());
	IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, FileActivitySharedState.Get());

	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingView::~STimingView()
{
	AllTracks.Reset();
	TopDockedTracks.Reset();
	BottomDockedTracks.Reset();
	ScrollableTracks.Reset();
	ForegroundTracks.Reset();

	for (Insights::ITimingViewExtender* Extender : GetExtenders())
	{
		Extender->OnEndSession(*this);
	}

	IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, ThreadTimingSharedState.Get());
	IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, LoadingSharedState.Get());
	IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, FileActivitySharedState.Get());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)

		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0, 0, 12, 0))
		[
			SAssignNew(HorizontalScrollBar, SScrollBar)
			.Orientation(Orient_Horizontal)
			.AlwaysShowScrollbar(false)
			.Visibility(EVisibility::Visible)
			.Thickness(FVector2D(5.0f, 5.0f))
			//.RenderOpacity(0.75)
			.OnUserScrolled(this, &STimingView::HorizontalScrollBar_OnUserScrolled)
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.Padding(FMargin(0, 22, 0, 12))
		[
			SAssignNew(VerticalScrollBar, SScrollBar)
			.Orientation(Orient_Vertical)
			.AlwaysShowScrollbar(false)
			.Visibility(EVisibility::Visible)
			.Thickness(FVector2D(5.0f, 5.0f))
			//.RenderOpacity(0.75)
			.OnUserScrolled(this, &STimingView::VerticalScrollBar_OnUserScrolled)
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 0.0f))
		[
			// Tracks Filter
			SNew(SComboButton)
			.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
			.ForegroundColor(FLinearColor::White)
			.ToolTipText(LOCTEXT("TracksFilterToolTip", "Filter timing tracks."))
			.OnGetMenuContent(this, &STimingView::MakeTracksFilterMenu)
			.HasDownArrow(true)
			.ContentPadding(FMargin(1.0f, 1.0f, 1.0f, 1.0f))
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.Padding(0.0f)
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
					.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
				]

				+SHorizontalBox::Slot()
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
					.Text(LOCTEXT("TracksFilter", "Tracks"))
				]
			]
		]
	];

	UpdateHorizontalScrollBar();
	UpdateVerticalScrollBar();

	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::Reset()
{
	for (Insights::ITimingViewExtender* Extender : GetExtenders())
	{
		Extender->OnEndSession(*this);
	}

	//////////////////////////////////////////////////

	Viewport.Reset();

	//////////////////////////////////////////////////

	AllTracks.Reset();
	TopDockedTracks.Reset();
	BottomDockedTracks.Reset();
	ScrollableTracks.Reset();
	ForegroundTracks.Reset();

	bScrollableTracksOrderIsDirty = false;

	FTimingEventsTrack::bUseDownSampling = true;

	//////////////////////////////////////////////////

	TimeRulerTrack->Reset();
	TimeRulerTrack->SetOrder(-999);
	AddTopDockedTrack(TimeRulerTrack);

	MarkersTrack->Reset();
	MarkersTrack->SetOrder(-100);
	AddTopDockedTrack(MarkersTrack);

	GraphTrack->Reset();
	GraphTrack->SetOrder(0);
	GraphTrack->SetHeight(200.0f);
	GraphTrack->AddDefaultFrameSeries();
	GraphTrack->SetVisibilityFlag(false);
	AddTopDockedTrack(GraphTrack);

	//////////////////////////////////////////////////

	MousePosition = FVector2D::ZeroVector;

	MousePositionOnButtonDown = FVector2D::ZeroVector;
	ViewportStartTimeOnButtonDown = 0.0;
	ViewportScrollPosYOnButtonDown = 0.0f;

	MousePositionOnButtonUp = FVector2D::ZeroVector;

	LastScrollPosY = 0.0f;

	bIsLMB_Pressed = false;
	bIsRMB_Pressed = false;

	bIsSpaceBarKeyPressed = false;
	bIsDragging = false;

	bIsPanning = false;
	PanningMode = EPanningMode::None;

	OverscrollLeft = 0.0f;
	OverscrollRight = 0.0f;
	OverscrollTop = 0.0f;
	OverscrollBottom = 0.0f;

	bIsSelecting = false;
	SelectionStartTime = 0.0;
	SelectionEndTime = 0.0;

	if (HoveredTrack.IsValid())
	{
		HoveredTrack.Reset();
		OnHoveredTrackChangedDelegate.Broadcast(HoveredTrack);
	}
	if (HoveredEvent.IsValid())
	{
		HoveredEvent.Reset();
		OnHoveredEventChangedDelegate.Broadcast(HoveredEvent);
	}

	if (SelectedTrack.IsValid())
	{
		SelectedTrack.Reset();
		OnSelectedTrackChangedDelegate.Broadcast(SelectedTrack);
	}
	if (SelectedEvent.IsValid())
	{
		SelectedEvent.Reset();
		OnSelectedEventChangedDelegate.Broadcast(SelectedEvent);
	}

	if (TimingEventFilter.IsValid())
	{
		TimingEventFilter.Reset();
	}

	bPreventThrottling = false;

	Tooltip.Reset();

	LastSelectionType = ESelectionType::None;

	TimeMarker = std::numeric_limits<double>::infinity();
	bIsScrubbing = false;

	//ThisGeometry

	//////////////////////////////////////////////////

	NumUpdatedEvents = 0;
	PreUpdateTracksDurationHistory.Reset();
	PreUpdateTracksDurationHistory.AddValue(0);
	UpdateTracksDurationHistory.Reset();
	UpdateTracksDurationHistory.AddValue(0);
	PostUpdateTracksDurationHistory.Reset();
	PostUpdateTracksDurationHistory.AddValue(0);
	TickDurationHistory.Reset();
	TickDurationHistory.AddValue(0);
	PreDrawTracksDurationHistory.Reset();
	PreDrawTracksDurationHistory.AddValue(0);
	DrawTracksDurationHistory.Reset();
	DrawTracksDurationHistory.AddValue(0);
	PostDrawTracksDurationHistory.Reset();
	PostDrawTracksDurationHistory.AddValue(0);
	OnPaintDeltaTimeHistory.Reset();
	OnPaintDeltaTimeHistory.AddValue(0);
	LastOnPaintTime = FPlatformTime::Cycles64();

	//////////////////////////////////////////////////

	for (Insights::ITimingViewExtender* Extender : GetExtenders())
	{
		Extender->OnBeginSession(*this);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::IsGpuTrackVisible() const
{
	return ThreadTimingSharedState && ThreadTimingSharedState->IsGpuTrackVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::IsCpuTrackVisible(uint32 InThreadId) const
{
	return ThreadTimingSharedState && ThreadTimingSharedState->IsCpuTrackVisible(InThreadId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	//SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	FStopwatch TickStopwatch;
	TickStopwatch.Start();

	ThisGeometry = AllottedGeometry;

	bPreventThrottling = false;

	constexpr float OverscrollFadeSpeed = 2.0f;
	if (OverscrollLeft > 0.0f)
	{
		OverscrollLeft = FMath::Max(0.0f, OverscrollLeft - InDeltaTime * OverscrollFadeSpeed);
	}
	if (OverscrollRight > 0.0f)
	{
		OverscrollRight = FMath::Max(0.0f, OverscrollRight - InDeltaTime * OverscrollFadeSpeed);
	}
	if (OverscrollTop > 0.0f)
	{
		OverscrollTop = FMath::Max(0.0f, OverscrollTop - InDeltaTime * OverscrollFadeSpeed);
	}
	if (OverscrollBottom > 0.0f)
	{
		OverscrollBottom = FMath::Max(0.0f, OverscrollBottom - InDeltaTime * OverscrollFadeSpeed);
	}

	const float ViewWidth = AllottedGeometry.GetLocalSize().X;
	const float ViewHeight = AllottedGeometry.GetLocalSize().Y;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Update viewport.

	Viewport.UpdateSize(ViewWidth, ViewHeight);

	if (!bIsPanning)
	{
		// Elastic snap to horizontal time limits.
		if (Viewport.EnforceHorizontalScrollLimits(0.5)) // 0.5 is the interpolation factor
		{
			UpdateHorizontalScrollBar();
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Check the analysis session time.

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session)
	{
		double SessionTime = 0.0;
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			SessionTime = Session->GetDurationSeconds();
		}

		// Check if horizontal scroll area has changed.
		if (SessionTime > Viewport.GetMaxValidTime() &&
			SessionTime != DBL_MAX &&
			SessionTime != std::numeric_limits<double>::infinity())
		{
			const double PreviousSessionTime = Viewport.GetMaxValidTime();
			if ((PreviousSessionTime >= Viewport.GetStartTime() && PreviousSessionTime <= Viewport.GetEndTime()) ||
				(SessionTime >= Viewport.GetStartTime() && SessionTime <= Viewport.GetEndTime()))
			{
				Viewport.AddDirtyFlags(ETimingTrackViewportDirtyFlags::HClippedSessionTimeChanged);
			}

			//UE_LOG(TimingProfiler, Log, TEXT("Session Duration: %g"), DT);
			Viewport.SetMaxValidTime(SessionTime);
			UpdateHorizontalScrollBar();
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////

	if (Session)
	{
		// Tick plugin extenders.
		// Each extender can add/remove tracks and/or change order of tracks.
		for (Insights::ITimingViewExtender* Extender : GetExtenders())
		{
			Extender->Tick(*this, *Session.Get());
		}

		// Re-sort now if we need to
		UpdateScrollableTracksOrder();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////

	// Animate the (vertical) layout transition (i.e. compact mode <-> normal mode).
	Viewport.UpdateLayout();

	TimeRulerTrack->SetSelection(bIsSelecting, SelectionStartTime, SelectionEndTime);
	TimeRulerTrack->SetTimeMarker(bIsDragging, TimeMarker);

	////////////////////////////////////////////////////////////////////////////////////////////////////

	class FTimingTrackUpdateContext : public ITimingTrackUpdateContext
	{
	public:
		explicit FTimingTrackUpdateContext(STimingView* InTimingView, double InCurrentTime, float InDeltaTime) : TimingView(InTimingView), CurrentTime(InCurrentTime), DeltaTime(InDeltaTime) {}

		virtual const FTimingTrackViewport& GetViewport() const override { return TimingView->GetViewport(); }
		virtual const FVector2D& GetMousePosition() const override { return TimingView->GetMousePosition(); }
		virtual const TSharedPtr<const ITimingEvent> GetHoveredEvent() const override { return TimingView->GetHoveredEvent(); }
		virtual const TSharedPtr<const ITimingEvent> GetSelectedEvent() const override { return TimingView->GetSelectedEvent(); }
		virtual const TSharedPtr<ITimingEventFilter> GetEventFilter() const override { return TimingView->GetEventFilter(); }
		virtual double GetCurrentTime() const override { return CurrentTime; }
		virtual float GetDeltaTime() const override { return DeltaTime; }

	public:
		STimingView* TimingView;
		double CurrentTime;
		float DeltaTime;
	};

	FTimingTrackUpdateContext UpdateContext(this, InCurrentTime, InDeltaTime);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Pre-Update.
	// The tracks needs to update their size.

	{
		FStopwatch PreUpdateTracksStopwatch;
		PreUpdateTracksStopwatch.Start();

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : TopDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PreUpdate(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PreUpdate(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PreUpdate(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ForegroundTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PreUpdate(UpdateContext);
			}
		}

		PreUpdateTracksStopwatch.Stop();
		PreUpdateTracksDurationHistory.AddValue(PreUpdateTracksStopwatch.AccumulatedTime);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Update Y postion for the visible top/bottom docked tracks.
	// Compute the total height of top/bottom docked areas.

	float TopOffset = 0.0f;
	for (TSharedPtr<FBaseTimingTrack>& TrackPtr : TopDockedTracks)
	{
		TrackPtr->SetPosY(TopOffset);
		if (TrackPtr->IsVisible())
		{
			TopOffset += TrackPtr->GetHeight();
		}
	}
	Viewport.SetTopOffset(TopOffset);

	float BottomOffset = 0.0f;
	for (TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
	{
		if (TrackPtr->IsVisible())
		{
			BottomOffset += TrackPtr->GetHeight();
		}
	}
	float BottomOffsetY = Viewport.GetHeight() - BottomOffset;
	for (TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
	{
		TrackPtr->SetPosY(BottomOffsetY);
		if (TrackPtr->IsVisible())
		{
			BottomOffsetY += TrackPtr->GetHeight();
		}
	}
	Viewport.SetBottomOffset(BottomOffset);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Compute the total height of visible scrollable tracks.

	float ScrollHeight = 0.0f;
	for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
	{
		if (TrackPtr->IsVisible())
		{
			ScrollHeight += TrackPtr->GetHeight();
		}
	}
	ScrollHeight += 1.0f; // allow 1 pixel at the bottom (for last horizontal line)

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Check if vertical scroll area has changed.

	bool bScrollHeightChanged = false;
	if (ScrollHeight != Viewport.GetScrollHeight())
	{
		bScrollHeightChanged = true;
		Viewport.SetScrollHeight(ScrollHeight);
		UpdateVerticalScrollBar();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////

	const float InitialScrollPosY = Viewport.GetScrollPosY();

	TSharedPtr<FBaseTimingTrack> SelectedScrollableTrack;
	if (SelectedTrack.IsValid() && SelectedTrack->IsVisible())
	{
		if (ScrollableTracks.Contains(SelectedTrack))
		{
			SelectedScrollableTrack = SelectedTrack;
		}
	}

	const float InitialPinnedTrackPosY = SelectedScrollableTrack.IsValid() ? SelectedScrollableTrack->GetPosY() : 0.0f;

	// Update the Y position for visible scrollable tracks.
	UpdatePositionForScrollableTracks();

	// The selected track will be pinned (keeps Y pos fixed unless user scrolls vertically).
	if (SelectedScrollableTrack.IsValid())
	{
		const float ScrollingDY = LastScrollPosY - InitialScrollPosY;
		const float PinnedTrackPosY = SelectedScrollableTrack->GetPosY();
		const float AdjustmentDY = InitialPinnedTrackPosY - PinnedTrackPosY + ScrollingDY;

		if (!FMath::IsNearlyZero(AdjustmentDY, 0.5f))
		{
			ViewportScrollPosYOnButtonDown -= AdjustmentDY;
			ScrollAtPosY(InitialScrollPosY - AdjustmentDY);
			UpdatePositionForScrollableTracks();
		}
	}

	// Elastic snap to vertical scroll limits.
	if (!bIsPanning)
	{
		const float DY = Viewport.GetScrollHeight() - Viewport.GetScrollableAreaHeight() + 7.0f; // +7 is to allow some space for the horizontal scrollbar
		const float MinY = FMath::Min(DY, 0.0f);
		const float MaxY = DY - MinY;

		float ScrollPosY = Viewport.GetScrollPosY();

		if (ScrollPosY < MinY)
		{
			if (bScrollHeightChanged || Viewport.IsDirty(ETimingTrackViewportDirtyFlags::VLayoutChanged))
			{
				ScrollPosY = MinY;
			}
			else
			{
				constexpr float U = 0.5f;
				ScrollPosY = ScrollPosY * U + (1.0f - U) * MinY;
				if (FMath::IsNearlyEqual(ScrollPosY, MinY, 0.5f))
				{
					ScrollPosY = MinY;
				}
			}
		}
		else if (ScrollPosY > MaxY)
		{
			if (bScrollHeightChanged || Viewport.IsDirty(ETimingTrackViewportDirtyFlags::VLayoutChanged))
			{
				ScrollPosY = MaxY;
			}
			else
			{
				constexpr float U = 0.5f;
				ScrollPosY = ScrollPosY * U + (1.0f - U) * MaxY;
				if (FMath::IsNearlyEqual(ScrollPosY, MaxY, 0.5f))
				{
					ScrollPosY = MaxY;
				}
			}
			if (ScrollPosY < MinY)
			{
				ScrollPosY = MinY;
			}
		}

		if (ScrollPosY != Viewport.GetScrollPosY())
		{
			ScrollAtPosY(ScrollPosY);
			UpdatePositionForScrollableTracks();
		}
	}

	LastScrollPosY = Viewport.GetScrollPosY();

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// At this point it is assumed all tracks have proper position and size.
	// Update.
	{
		FStopwatch UpdateTracksStopwatch;
		UpdateTracksStopwatch.Start();

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : TopDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->Update(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->Update(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->Update(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ForegroundTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->Update(UpdateContext);
			}
		}

		UpdateTracksStopwatch.Stop();
		UpdateTracksDurationHistory.AddValue(UpdateTracksStopwatch.AccumulatedTime);
	}
	//////////////////////////////////////////////////
	// Post-Update.
	{
		FStopwatch PostUpdateTracksStopwatch;
		PostUpdateTracksStopwatch.Start();

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : TopDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PostUpdate(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PostUpdate(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PostUpdate(UpdateContext);
			}
		}

		for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ForegroundTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PostUpdate(UpdateContext);
			}
		}

		PostUpdateTracksStopwatch.Stop();
		PostUpdateTracksDurationHistory.AddValue(PostUpdateTracksStopwatch.AccumulatedTime);
	}
	//////////////////////////////////////////////////

	Tooltip.Update();
	if (!MousePosition.IsZero())
	{
		Tooltip.SetPosition(MousePosition, 0.0f, Viewport.GetWidth() - 12.0f, 0.0f, Viewport.GetHeight() - 12.0f); // -12.0f is to avoid overlaping the scrollbars
	}

	//////////////////////////////////////////////////

	// Reset hovered/selected flags for all tracks.
	for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
	{
		if (TrackPtr->IsVisible())
		{
			TrackPtr->SetHoveredState(false);
			TrackPtr->SetSelectedFlag(false);
		}
	}

	// Set the hovered flag for the actual hovered track, if any.
	if (HoveredTrack.IsValid())
	{
		HoveredTrack->SetHoveredState(true);
	}

	// Set the selected flag for the actual selected track, if any.
	if (SelectedTrack.IsValid())
	{
		SelectedTrack->SetSelectedFlag(true);
	}

	//////////////////////////////////////////////////

	Viewport.ResetDirtyFlags();

	UpdateOtherViews();

	TickStopwatch.Stop();
	TickDurationHistory.AddValue(TickStopwatch.AccumulatedTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

//TODO: Move this check into STimersView/SStatsView or in STimingProfilerWindow or in FTimingProfilerManager.
void STimingView::UpdateOtherViews()
{
	// We need to check if TimersView or StatsView needs to update their lists of timers / counters.
	// But, ensure we do not check too often.
	static uint64 NextTimestamp = 0;
	uint64 Time = FPlatformTime::Cycles64();
	if (Time > NextTimestamp)
	{
		const uint64 WaitTime = static_cast<uint64>(0.2 / FPlatformTime::GetSecondsPerCycle64()); // 200ms
		NextTimestamp = Time + WaitTime;

		TSharedPtr<STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Wnd)
		{
			TSharedPtr<STimersView> TimersView = Wnd->GetTimersView();
			if (TimersView)
			{
				TimersView->RebuildTree(false);
			}
			TSharedPtr<SStatsView> StatsView = Wnd->GetStatsView();
			if (StatsView)
			{
				StatsView->RebuildTree(false);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdatePositionForScrollableTracks()
{
	// Update the Y postion for visible scrollable tracks.
	float ScrollableTrackPosY = Viewport.GetTopOffset() - Viewport.GetScrollPosY();
	for (TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
	{
		TrackPtr->SetPosY(ScrollableTrackPosY); // set pos y also for the hidden tracks
		if (TrackPtr->IsVisible())
		{
			ScrollableTrackPosY += TrackPtr->GetHeight();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 STimingView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FStopwatch Stopwatch;
	Stopwatch.Start();

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DrawContext(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

#if 0 // Enabling this may further increase UI performance (TODO: profile if this is really needed again).
	// Avoids multiple resizes of Slate's draw elements buffers.
	OutDrawElements.GetRootDrawLayer().DrawElements.Reserve(50000);
#endif

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const float ViewWidth = AllottedGeometry.GetLocalSize().X;
	const float ViewHeight = AllottedGeometry.GetLocalSize().Y;

#if 0 // Enabling this may further increase UI performance (TODO: profile if this is really needed again).
	// Warm up Slate vertex/index buffers to avoid initial freezes due to multiple resizes of those buffers.
	static bool bWarmingUp = false;
	if (!bWarmingUp)
	{
		bWarmingUp = true;

		FRandomStream RandomStream(0);
		const int32 Count = 1'000'000;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			float X = ViewWidth * RandomStream.GetFraction();
			float Y = ViewHeight * RandomStream.GetFraction();
			FLinearColor Color(RandomStream.GetFraction(), RandomStream.GetFraction(), RandomStream.GetFraction(), 1.0f);
			DrawContext.DrawBox(X, Y, 1.0f, 1.0f, WhiteBrush, Color);
		}
		LayerId++;
		LayerId++;
	}
#endif

	////////////////////////////////////////////////////////////////////////////////////////////////////

	class FTimingTrackDrawContext : public ITimingTrackDrawContext
	{
	public:
		explicit FTimingTrackDrawContext(const STimingView* InTimingView, FDrawContext& InDrawContext, const FTimingViewDrawHelper& InHelper)
			: TimingView(InTimingView)
			, DrawContext(InDrawContext)
			, Helper(InHelper)
		{}

		virtual const FTimingTrackViewport& GetViewport() const override { return TimingView->GetViewport(); }
		virtual const FVector2D& GetMousePosition() const override { return TimingView->GetMousePosition(); }
		virtual const TSharedPtr<const ITimingEvent> GetHoveredEvent() const override { return TimingView->GetHoveredEvent(); }
		virtual const TSharedPtr<const ITimingEvent> GetSelectedEvent() const override { return TimingView->GetSelectedEvent(); }
		virtual const TSharedPtr<ITimingEventFilter> GetEventFilter() const override { return TimingView->GetEventFilter(); }
		virtual FDrawContext& GetDrawContext() const override { return DrawContext; }
		virtual const ITimingViewDrawHelper& GetHelper() const override { return Helper; }

	public:
		const STimingView* TimingView;
		FDrawContext& DrawContext;
		const FTimingViewDrawHelper& Helper;
	};

	FTimingViewDrawHelper Helper(DrawContext, Viewport);
	FTimingTrackDrawContext TimingDrawContext(this, DrawContext, Helper);

	////////////////////////////////////////////////////////////////////////////////////////////////////

	// Draw background.
	Helper.DrawBackground();

	//////////////////////////////////////////////////
	// Pre-Draw
	{
		FStopwatch PreDrawTracksStopwatch;
		PreDrawTracksStopwatch.Start();

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PreDraw(TimingDrawContext);
			}
		}

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : TopDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PreDraw(TimingDrawContext);
			}
		}

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PreDraw(TimingDrawContext);
			}
		}

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ForegroundTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PreDraw(TimingDrawContext);
			}
		}

		PreDrawTracksStopwatch.Stop();
		PreDrawTracksDurationHistory.AddValue(PreDrawTracksStopwatch.AccumulatedTime);
	}

	//////////////////////////////////////////////////
	// Draw
	{
		FStopwatch DrawTracksStopwatch;
		DrawTracksStopwatch.Start();

		Helper.BeginDrawTracks();

		const FVector2D Position = AllottedGeometry.GetAbsolutePosition();
		const float Scale = AllottedGeometry.GetAccumulatedLayoutTransform().GetScale();

		{
			const float L = Position.X;
			const float R = Position.X + (Viewport.GetWidth() * Scale);
			const float T = Position.Y + (Viewport.GetTopOffset() * Scale);
			const float B = Position.Y + ((Viewport.GetHeight() - Viewport.GetBottomOffset()) * Scale);
			const FSlateClippingZone ClipZone(FVector2D(L, T), FVector2D(R, T), FVector2D(L, B), FVector2D(R, B));
			DrawContext.ElementList.PushClip(ClipZone);

			for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
			{
				if (TrackPtr->IsVisible())
				{
					TrackPtr->Draw(TimingDrawContext);
				}
			}

			DrawContext.ElementList.PopClip();
		}

		{
			const float L = Position.X;
			const float R = Position.X + (Viewport.GetWidth() * Scale);
			const float T = Position.Y;
			const float B = Position.Y + (Viewport.GetTopOffset() * Scale);
			const FSlateClippingZone ClipZone(FVector2D(L, T), FVector2D(R, T), FVector2D(L, B), FVector2D(R, B));
			DrawContext.ElementList.PushClip(ClipZone);

			for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : TopDockedTracks)
			{
				if (TrackPtr->IsVisible())
				{
					TrackPtr->Draw(TimingDrawContext);
				}
			}

			DrawContext.ElementList.PopClip();
		}

		{
			const float L = Position.X;
			const float R = Position.X + (Viewport.GetWidth() * Scale);
			const float T = Position.Y + ((Viewport.GetHeight() - Viewport.GetBottomOffset()) * Scale);
			const float B = Position.Y + (Viewport.GetHeight() * Scale);
			const FSlateClippingZone ClipZone(FVector2D(L, T), FVector2D(R, T), FVector2D(L, B), FVector2D(R, B));
			DrawContext.ElementList.PushClip(ClipZone);

			for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
			{
				if (TrackPtr->IsVisible())
				{
					TrackPtr->Draw(TimingDrawContext);
				}
			}

			DrawContext.ElementList.PopClip();
		}

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ForegroundTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->Draw(TimingDrawContext);
			}
		}

		Helper.EndDrawTracks();

		DrawTracksStopwatch.Stop();
		DrawTracksDurationHistory.AddValue(DrawTracksStopwatch.AccumulatedTime);
	}

	//////////////////////////////////////////////////
	// Draw the selected and/or hovered event.

	if (ITimingEvent::AreValidAndEquals(SelectedEvent, HoveredEvent))
	{
		const TSharedRef<const FBaseTimingTrack> TrackPtr = SelectedEvent->GetTrack();

		// Highlight the selected and hovered timing event (if any).
		if (TrackPtr->IsVisible())
		{
			SelectedEvent->GetTrack()->DrawEvent(TimingDrawContext, *SelectedEvent, EDrawEventMode::SelectedAndHovered);
		}
	}
	else
	{
		// Highlight the selected timing event (if any).
		if (SelectedEvent.IsValid())
		{
			const TSharedRef<const FBaseTimingTrack> TrackPtr = SelectedEvent->GetTrack();
			if (TrackPtr->IsVisible())
			{
				SelectedEvent->GetTrack()->DrawEvent(TimingDrawContext, *SelectedEvent, EDrawEventMode::Selected);
			}
		}

		// Highlight the hovered timing event (if any).
		if (HoveredEvent.IsValid())
		{
			const TSharedRef<const FBaseTimingTrack> TrackPtr = HoveredEvent->GetTrack();
			if (TrackPtr->IsVisible())
			{
				HoveredEvent->GetTrack()->DrawEvent(TimingDrawContext, *HoveredEvent, EDrawEventMode::Hovered);
			}
		}
	}

	// Draw the time range selection.
	FDrawHelpers::DrawTimeRangeSelection(DrawContext, Viewport, SelectionStartTime, SelectionEndTime, WhiteBrush, MainFont);

	//////////////////////////////////////////////////
	// Post-Draw
	{
		FStopwatch PostDrawTracksStopwatch;
		PostDrawTracksStopwatch.Start();

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PostDraw(TimingDrawContext);
			}
		}

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : TopDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PostDraw(TimingDrawContext);
			}
		}

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PostDraw(TimingDrawContext);
			}
		}

		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ForegroundTracks)
		{
			if (TrackPtr->IsVisible())
			{
				TrackPtr->PostDraw(TimingDrawContext);
			}
		}

		PostDrawTracksStopwatch.Stop();
		PostDrawTracksDurationHistory.AddValue(PostDrawTracksStopwatch.AccumulatedTime);
	}

	//////////////////////////////////////////////////

	// Draw tooltip with info about hovered event.
	Tooltip.Draw(DrawContext);

	// Fill background for the "Tracks" filter combobox.
	DrawContext.DrawBox(0.0f, 0.0f, 66.0f, 24.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));

	//////////////////////////////////////////////////

	constexpr float OverscrollLineSize = 1.0f;
	constexpr int32 OverscrollLineCount = 8;

	if (OverscrollLeft > 0.0f)
	{
		// TODO: single box with gradient opacity
		const float OverscrollLineY = Viewport.GetTopOffset();
		const float OverscrollLineH = Viewport.GetScrollableAreaHeight();
		for (int32 LineIndex = 0; LineIndex < OverscrollLineCount; ++LineIndex)
		{
			const float Opacity = OverscrollLeft * static_cast<float>(OverscrollLineCount - LineIndex) / static_cast<float>(OverscrollLineCount);
			DrawContext.DrawBox(LineIndex * OverscrollLineSize, OverscrollLineY, OverscrollLineSize, OverscrollLineH, WhiteBrush, FLinearColor(1.0f, 0.1f, 0.1f, Opacity));
		}
	}
	if (OverscrollRight > 0.0f)
	{
		const float OverscrollLineY = Viewport.GetTopOffset();
		const float OverscrollLineH = Viewport.GetScrollableAreaHeight();
		for (int32 LineIndex = 0; LineIndex < OverscrollLineCount; ++LineIndex)
		{
			const float Opacity = OverscrollRight * static_cast<float>(OverscrollLineCount - LineIndex) / static_cast<float>(OverscrollLineCount);
			DrawContext.DrawBox(ViewWidth - (1 + LineIndex) * OverscrollLineSize, OverscrollLineY, OverscrollLineSize, OverscrollLineH, WhiteBrush, FLinearColor(1.0f, 0.1f, 0.1f, Opacity));
		}
	}
	if (OverscrollTop > 0.0f)
	{
		const float OverscrollLineY = Viewport.GetTopOffset();
		for (int32 LineIndex = 0; LineIndex < OverscrollLineCount; ++LineIndex)
		{
			const float Opacity = OverscrollTop * static_cast<float>(OverscrollLineCount - LineIndex) / static_cast<float>(OverscrollLineCount);
			DrawContext.DrawBox(0.0f, OverscrollLineY + LineIndex * OverscrollLineSize, ViewWidth, OverscrollLineSize, WhiteBrush, FLinearColor(1.0f, 0.1f, 0.1f, Opacity));
		}
	}
	if (OverscrollBottom > 0.0f)
	{
		const float OverscrollLineY = ViewHeight - Viewport.GetBottomOffset();
		for (int32 LineIndex = 0; LineIndex < OverscrollLineCount; ++LineIndex)
		{
			const float Opacity = OverscrollBottom * static_cast<float>(OverscrollLineCount - LineIndex) / static_cast<float>(OverscrollLineCount);
			DrawContext.DrawBox(0.0f, OverscrollLineY - (1 + LineIndex) * OverscrollLineSize, ViewWidth, OverscrollLineSize, WhiteBrush, FLinearColor(1.0f, 0.1f, 0.1f, Opacity));
		}
	}

	//////////////////////////////////////////////////

	const bool bShouldDisplayDebugInfo = FInsightsManager::Get()->IsDebugInfoEnabled();
	if (bShouldDisplayDebugInfo)
	{
		const FSlateFontInfo& SummaryFont = MainFont;

		const float MaxFontCharHeight = FontMeasureService->Measure(TEXT("!"), SummaryFont).Y;
		const float DbgDY = MaxFontCharHeight;

		const float DbgW = 320.0f;
		const float DbgH = DbgDY * 9 + 3.0f;
		const float DbgX = ViewWidth - DbgW - 20.0f;
		float DbgY = Viewport.GetTopOffset() + 10.0f;

		DrawContext.LayerId++;

		DrawContext.DrawBox(DbgX - 2.0f, DbgY - 2.0f, DbgW, DbgH, WhiteBrush, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f));
		DrawContext.LayerId++;

		FLinearColor DbgTextColor(0.0f, 0.0f, 0.0f, 0.9f);

		//////////////////////////////////////////////////
		// Display the "Draw" performance info.

		// Time interval since last OnPaint call.
		const uint64 CurrentTime = FPlatformTime::Cycles64();
		const uint64 OnPaintDeltaTime = CurrentTime - LastOnPaintTime;
		LastOnPaintTime = CurrentTime;
		OnPaintDeltaTimeHistory.AddValue(OnPaintDeltaTime); // saved for last 32 OnPaint calls
		const uint64 AvgOnPaintDeltaTime = OnPaintDeltaTimeHistory.ComputeAverage();
		const uint64 AvgOnPaintDeltaTimeMs = FStopwatch::Cycles64ToMilliseconds(AvgOnPaintDeltaTime);
		const double AvgOnPaintFps = AvgOnPaintDeltaTimeMs != 0 ? 1.0 / FStopwatch::Cycles64ToSeconds(AvgOnPaintDeltaTime) : 0.0;

		const uint64 AvgPreDrawTracksDurationMs = FStopwatch::Cycles64ToMilliseconds(PreDrawTracksDurationHistory.ComputeAverage());
		const uint64 AvgDrawTracksDurationMs = FStopwatch::Cycles64ToMilliseconds(DrawTracksDurationHistory.ComputeAverage());
		const uint64 AvgPostDrawTracksDurationMs = FStopwatch::Cycles64ToMilliseconds(PostDrawTracksDurationHistory.ComputeAverage());
		const uint64 AvgTotalDrawDurationMs = FStopwatch::Cycles64ToMilliseconds(TotalDrawDurationHistory.ComputeAverage());

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("D: %llu ms + %llu ms + %llu ms + %llu ms = %llu ms | + %llu ms = %llu ms (%d fps)"),
				AvgPreDrawTracksDurationMs, // pre-draw tracks time
				AvgDrawTracksDurationMs, // draw tracks time
				AvgPostDrawTracksDurationMs, // post-draw tracks time
				AvgTotalDrawDurationMs - AvgPreDrawTracksDurationMs - AvgDrawTracksDurationMs - AvgPostDrawTracksDurationMs, // other draw code
				AvgTotalDrawDurationMs,
				AvgOnPaintDeltaTimeMs - AvgTotalDrawDurationMs, // other overhead to OnPaint calls
				AvgOnPaintDeltaTimeMs, // average time between two OnPaint calls
				FMath::RoundToInt(AvgOnPaintFps)), // framerate of OnPaint calls
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display the "update" performance info.

		const uint64 AvgPreUpdateTracksDurationMs = FStopwatch::Cycles64ToMilliseconds(PreUpdateTracksDurationHistory.ComputeAverage());
		const uint64 AvgUpdateTracksDurationMs = FStopwatch::Cycles64ToMilliseconds(UpdateTracksDurationHistory.ComputeAverage());
		const uint64 AvgPostUpdateTracksDurationMs = FStopwatch::Cycles64ToMilliseconds(PostUpdateTracksDurationHistory.ComputeAverage());
		const uint64 AvgTickDurationMs = FStopwatch::Cycles64ToMilliseconds(TickDurationHistory.ComputeAverage());

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U avg: %llu ms + %llu ms + %llu ms + %llu ms = %llu ms"),
				AvgPreUpdateTracksDurationMs,
				AvgUpdateTracksDurationMs,
				AvgPostUpdateTracksDurationMs,
				AvgTickDurationMs - AvgPreUpdateTracksDurationMs - AvgUpdateTracksDurationMs - AvgPostUpdateTracksDurationMs,
				AvgTickDurationMs),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display timing events stats.

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Format(TEXT("{0} events : {1} ({2}) boxes, {3} borders, {4} texts"),
			{
				FText::AsNumber(Helper.GetNumEvents()).ToString(),
				FText::AsNumber(Helper.GetNumDrawBoxes()).ToString(),
				FText::AsPercent((double)Helper.GetNumDrawBoxes() / (Helper.GetNumDrawBoxes() + Helper.GetNumMergedBoxes())).ToString(),
				FText::AsNumber(Helper.GetNumDrawBorders()).ToString(),
				FText::AsNumber(Helper.GetNumDrawTexts()).ToString(),
				//OutDrawElements.GetRootDrawLayer().GetElementCount(),
			}),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display time markers stats.

		if (MarkersTrack)
		{
			DrawContext.DrawText
			(
				DbgX, DbgY,
				FString::Format(TEXT("{0}{1} logs : {2} boxes, {3} texts"),
				{
					MarkersTrack->IsVisible() ? TEXT("") : TEXT("*"),
					FText::AsNumber(MarkersTrack->GetNumLogMessages()).ToString(),
					FText::AsNumber(MarkersTrack->GetNumBoxes()).ToString(),
					FText::AsNumber(MarkersTrack->GetNumTexts()).ToString(),
				}),
				SummaryFont, DbgTextColor
			);
			DbgY += DbgDY;
		}

		//////////////////////////////////////////////////
		// Display Graph track stats.

		if (GraphTrack)
		{
			DrawContext.DrawText
			(
				DbgX, DbgY,
				FString::Format(TEXT("{0} events : {1} points, {2} lines, {3} boxes"),
					{
						FText::AsNumber(GraphTrack->GetNumAddedEvents()).ToString(),
						FText::AsNumber(GraphTrack->GetNumDrawPoints()).ToString(),
						FText::AsNumber(GraphTrack->GetNumDrawLines()).ToString(),
						FText::AsNumber(GraphTrack->GetNumDrawBoxes()).ToString(),
					}),
					SummaryFont, DbgTextColor
			);
			DbgY += DbgDY;
		}

		//////////////////////////////////////////////////
		// Display viewport's horizontal info.

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("SX: %g, ST: %g, ET: %s"),
				Viewport.GetScaleX(),
				Viewport.GetStartTime(),
				*TimeUtils::FormatTimeAuto(Viewport.GetMaxValidTime())),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display viewport's vertical info.

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("Y: %.2f, H: %g, VH: %g"),
				Viewport.GetScrollPosY(),
				Viewport.GetScrollHeight(),
				Viewport.GetHeight()),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display input related debug info.

		FString InputStr = FString::Printf(TEXT("(%.0f, %.0f)"), MousePosition.X, MousePosition.Y);
		if (bIsSpaceBarKeyPressed) InputStr += " Space";
		if (bIsLMB_Pressed) InputStr += " LMB";
		if (bIsRMB_Pressed) InputStr += " RMB";
		if (bIsPanning) InputStr += " Panning";
		if (bIsSelecting) InputStr += " Selecting";
		if (bIsDragging) InputStr += " Dragging";
		if (bIsScrubbing) InputStr += " Scrubbing";
		DrawContext.DrawText(DbgX, DbgY, InputStr, SummaryFont, DbgTextColor);
		DbgY += DbgDY;
	}

	//////////////////////////////////////////////////

	Stopwatch.Stop();
	TotalDrawDurationHistory.AddValue(Stopwatch.AccumulatedTime);

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::AddTopDockedTrack(TSharedPtr<FBaseTimingTrack> Track)
{
	check(Track.IsValid());
	UE_LOG(TimingProfiler, Log, TEXT("New Top Docked Track (%d) : %s - %s (\"%s\")"),
		TopDockedTracks.Num() + 1,
		*Track->GetType().ToString(),
		*Track->GetSubType().ToString(),
		*Track->GetName());
	check(!AllTracks.Contains(Track->GetId()));
	AllTracks.Add(Track->GetId(), Track);
	TopDockedTracks.Add(Track);
	Algo::SortBy(TopDockedTracks, &FBaseTimingTrack::GetOrder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::AddBottomDockedTrack(TSharedPtr<FBaseTimingTrack> Track)
{
	check(Track.IsValid());
	UE_LOG(TimingProfiler, Log, TEXT("New Bottom Docked Track (%d) : %s - %s (\"%s\")"),
		BottomDockedTracks.Num() + 1,
		*Track->GetType().ToString(),
		*Track->GetSubType().ToString(),
		*Track->GetName());
	check(!AllTracks.Contains(Track->GetId()));
	AllTracks.Add(Track->GetId(), Track);
	BottomDockedTracks.Add(Track);
	Algo::SortBy(BottomDockedTracks, &FBaseTimingTrack::GetOrder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::AddScrollableTrack(TSharedPtr<FBaseTimingTrack> Track)
{
	check(Track.IsValid());
	UE_LOG(TimingProfiler, Log, TEXT("New Scrollable Track (%d) : %s - %s (\"%s\")"),
		ScrollableTracks.Num() + 1,
		*Track->GetType().ToString(),
		*Track->GetSubType().ToString(),
		*Track->GetName());
	check(!AllTracks.Contains(Track->GetId()));
	AllTracks.Add(Track->GetId(), Track);
	ScrollableTracks.Add(Track);
	InvalidateScrollableTracksOrder();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::InvalidateScrollableTracksOrder()
{
	bScrollableTracksOrderIsDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdateScrollableTracksOrder()
{
	if (bScrollableTracksOrderIsDirty)
	{
		Algo::SortBy(ScrollableTracks, &FBaseTimingTrack::GetOrder);
		bScrollableTracksOrderIsDirty = false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::AddForegroundTrack(TSharedPtr<FBaseTimingTrack> Track)
{
	check(Track.IsValid());
	UE_LOG(TimingProfiler, Log, TEXT("New Foreground Track (%d) : %s - %s (\"%s\")"),
		ForegroundTracks.Num() + 1,
		*Track->GetType().ToString(),
		*Track->GetSubType().ToString(),
		*Track->GetName());
	check(!AllTracks.Contains(Track->GetId()));
	AllTracks.Add(Track->GetId(), Track);
	ForegroundTracks.Add(Track);
	Algo::SortBy(ForegroundTracks, &FBaseTimingTrack::GetOrder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonDown = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MousePosition = MousePositionOnButtonDown;

	bool bStartPanningSelectingOrScrubbing = false;
	bool bStartPanning = false;
	bool bStartSelecting = false;
	bool bStartScrubbing = false;

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (MarkersTrack->IsVisible() && MarkersTrack->IsHeaderHovered())
		{
			MarkersTrack->ToggleCollapsed();
		}

		if (!bIsRMB_Pressed)
		{
			bIsLMB_Pressed = true;
			bStartPanningSelectingOrScrubbing = true;
			SelectHoveredTimingTrack();
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (!bIsLMB_Pressed)
		{
			bIsRMB_Pressed = true;
			bStartPanningSelectingOrScrubbing = true;
			SelectHoveredTimingTrack();
		}
	}

	if (bStartPanningSelectingOrScrubbing)
	{
		if (bIsSpaceBarKeyPressed)
		{
			bStartPanning = true;
		}
		else if (MousePositionOnButtonDown.Y < TimeRulerTrack->GetHeight() &&
			(MouseEvent.GetModifierKeys().IsControlDown() || MouseEvent.GetModifierKeys().IsShiftDown()))
		{
			bStartScrubbing = true;
		}
		else if (MousePositionOnButtonDown.Y < TimeRulerTrack->GetHeight() ||
			(MouseEvent.GetModifierKeys().IsControlDown() && MouseEvent.GetModifierKeys().IsShiftDown()))
		{
			bStartSelecting = true;
		}
		else
		{
			bStartPanning = true;
		}

		// Capture mouse, so we can drag outside this widget.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}

	if (bPreventThrottling)
	{
		Reply.PreventThrottling();
	}

	if (bStartScrubbing)
	{
		bIsPanning = false;
		bIsDragging = false;
		bIsScrubbing = true;
	}
	else if (bStartPanning)
	{
		bIsPanning = true;
		bIsDragging = false;
		bIsScrubbing = false;

		ViewportStartTimeOnButtonDown = Viewport.GetStartTime();
		ViewportScrollPosYOnButtonDown = Viewport.GetScrollPosY();

		if (MouseEvent.GetModifierKeys().IsControlDown())
		{
			// Allow panning only horizontally.
			PanningMode = EPanningMode::Horizontal;
		}
		else if (MouseEvent.GetModifierKeys().IsShiftDown())
		{
			// Allow panning only vertically.
			PanningMode = EPanningMode::Vertical;
		}
		else
		{
			// Allow panning both horizontally and vertically.
			PanningMode = EPanningMode::HorizontalAndVertical;
		}
	}
	else if (bStartSelecting)
	{
		bIsSelecting = true;
		bIsDragging = false;
		bIsScrubbing = false;

		SelectionStartTime = Viewport.SlateUnitsToTime(MousePositionOnButtonDown.X);
		SelectionEndTime = SelectionStartTime;
		LastSelectionType = ESelectionType::None;
		RaiseSelectionChanging();
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonUp = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MousePosition = MousePositionOnButtonUp;

	const bool bIsValidForMouseClick = MousePositionOnButtonUp.Equals(MousePositionOnButtonDown, 2.0f);

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bIsLMB_Pressed)
		{
			if (bIsPanning)
			{
				PanningMode = EPanningMode::None;
				bIsPanning = false;
			}
			else if (bIsSelecting)
			{
				RaiseSelectionChanged();
				bIsSelecting = false;
			}
			else if (bIsScrubbing)
			{
				RaiseTimeMarkerChanged();
				bIsScrubbing = false;
			}

			if (bIsValidForMouseClick)
			{
				// Select the hovered timing event (if any).
				UpdateHoveredTimingEvent(MousePositionOnButtonUp.X, MousePositionOnButtonUp.Y);
				SelectHoveredTimingTrack();
				SelectHoveredTimingEvent();

				// When clicking on an empty space...
				if (!SelectedEvent.IsValid())
				{
					// ...reset selection.
					SelectionEndTime = SelectionStartTime = 0.0;
					LastSelectionType = ESelectionType::None;
					RaiseSelectionChanged();
				}
			}

			bIsDragging = false;

			// Release mouse as we no longer drag.
			Reply = FReply::Handled().ReleaseMouseCapture();

			bIsLMB_Pressed = false;
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (bIsRMB_Pressed)
		{
			if (bIsPanning)
			{
				PanningMode = EPanningMode::None;
				bIsPanning = false;
			}
			else if (bIsSelecting)
			{
				RaiseSelectionChanged();
				bIsSelecting = false;
			}
			else if (bIsScrubbing)
			{
				RaiseTimeMarkerChanged();
				bIsScrubbing = false;
			}

			if (bIsValidForMouseClick)
			{
				SelectHoveredTimingTrack();
				ShowContextMenu(MouseEvent);
			}

			bIsDragging = false;

			// Release mouse as we no longer drag.
			Reply = FReply::Handled().ReleaseMouseCapture();

			bIsRMB_Pressed = false;
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (MarkersTrack->IsVisible() && MarkersTrack->IsHovered())
		{
			MarkersTrack->ToggleCollapsed();
		}
		else if (HoveredEvent.IsValid())
		{
			if (MouseEvent.GetModifierKeys().IsControlDown())
			{
				const double EndTime = Viewport.RestrictEndTime(HoveredEvent->GetEndTime());
				SelectTimeInterval(HoveredEvent->GetStartTime(), EndTime - HoveredEvent->GetStartTime());
			}
			else
			{
				SetEventFilter(HoveredEvent->GetTrack()->GetFilterByEvent(HoveredEvent));
			}
		}
		else
		{
			if (TimingEventFilter.IsValid())
			{
				TimingEventFilter.Reset();
				Viewport.AddDirtyFlags(ETimingTrackViewportDirtyFlags::HInvalidated);
			}
		}

		Reply = FReply::Handled();
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (!MouseEvent.GetCursorDelta().IsZero())
	{
		if (bIsPanning)
		{
			if (HasMouseCapture())
			{
				bIsDragging = true;

				if ((int32)PanningMode & (int32)EPanningMode::Horizontal)
				{
					const double StartTime = ViewportStartTimeOnButtonDown + static_cast<double>(MousePositionOnButtonDown.X - MousePosition.X) / Viewport.GetScaleX();
					ScrollAtTime(StartTime);
				}

				if ((int32)PanningMode & (int32)EPanningMode::Vertical)
				{
					const float ScrollPosY = ViewportScrollPosYOnButtonDown + (MousePositionOnButtonDown.Y - MousePosition.Y);
					ScrollAtPosY(ScrollPosY);
				}
			}
		}
		else if (bIsSelecting)
		{
			if (HasMouseCapture())
			{
				bIsDragging = true;

				SelectionStartTime = Viewport.SlateUnitsToTime(MousePositionOnButtonDown.X);
				SelectionEndTime = Viewport.SlateUnitsToTime(MousePosition.X);
				if (SelectionStartTime > SelectionEndTime)
				{
					double Temp = SelectionStartTime;
					SelectionStartTime = SelectionEndTime;
					SelectionEndTime = Temp;
				}
				LastSelectionType = ESelectionType::TimeRange;
				RaiseSelectionChanging();
			}
		}
		else if (bIsScrubbing)
		{
			if (HasMouseCapture())
			{
				bIsDragging = true;

				TimeMarker = Viewport.SlateUnitsToTime(MousePosition.X);
				RaiseTimeMarkerChanging();
			}
		}
		else
		{
			UpdateHoveredTimingEvent(MousePosition.X, MousePosition.Y);
		}

		Reply = FReply::Handled();
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		// No longer dragging (unless we have mouse capture).
		bIsDragging = false;
		bIsPanning = false;
		bIsSelecting = false;

		bIsLMB_Pressed = false;
		bIsRMB_Pressed = false;

		MousePosition = FVector2D::ZeroVector;

		if (HoveredTrack.IsValid())
		{
			HoveredTrack.Reset();
			OnHoveredTrackChangedDelegate.Broadcast(HoveredTrack);
		}
		if (HoveredEvent.IsValid())
		{
			HoveredEvent.Reset();
			OnHoveredEventChangedDelegate.Broadcast(HoveredEvent);
		}
		Tooltip.SetDesiredOpacity(0.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetModifierKeys().IsShiftDown())
	{
		//MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		if (GraphTrack->IsVisible() &&
			MousePosition.Y >= GraphTrack->GetPosY() &&
			MousePosition.Y < GraphTrack->GetPosY() + GraphTrack->GetHeight())
		{
			for (const TSharedPtr<FGraphSeries>& Series : GraphTrack->GetSeries())
			{
				if (Series->IsVisible() && !Series->IsAutoZoomEnabled())
				{
					// Zoom in/out vertically.
					const double Delta = MouseEvent.GetWheelDelta();
					constexpr double ZoomStep = 0.25; // as percent
					double ScaleY;

					if (Delta > 0)
					{
						ScaleY = Series->GetScaleY() * FMath::Pow(1.0 + ZoomStep, Delta);
					}
					else
					{
						ScaleY = Series->GetScaleY() * FMath::Pow(1.0 / (1.0 + ZoomStep), -Delta);
					}

					Series->SetScaleY(ScaleY);
					Series->SetDirtyFlag();
				}
			}
		}
		else
		{
			// Scroll vertically.
			constexpr float ScrollSpeedY = 16.0f * 3;
			const float NewScrollPosY = Viewport.GetScrollPosY() - ScrollSpeedY * MouseEvent.GetWheelDelta();
			ScrollAtPosY(EnforceVerticalScrollLimits(NewScrollPosY));
		}
	}
	else if (MouseEvent.GetModifierKeys().IsControlDown())
	{
		// Scroll horizontally.
		const double ScrollSpeedX = Viewport.GetDurationForViewportDX(16.0 * 3);
		const double NewStartTime = Viewport.GetStartTime() - ScrollSpeedX * MouseEvent.GetWheelDelta();
		ScrollAtTime(EnforceHorizontalScrollLimits(NewStartTime));
	}
	else
	{
		// Zoom in/out horizontally.
		const double Delta = MouseEvent.GetWheelDelta();
		//MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		if (Viewport.RelativeZoomWithFixedX(Delta, MousePosition.X))
		{
			UpdateHorizontalScrollBar();
		}
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);

	//TSharedPtr<FStatIDDragDropOp> Operation = DragDropEvent.GetOperationAs<FStatIDDragDropOp>();
	//if (Operation.IsValid())
	//{
	//	Operation->ShowOK();
	//}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragLeave(DragDropEvent);

	//TSharedPtr<FStatIDDragDropOp> Operation = DragDropEvent.GetOperationAs<FStatIDDragDropOp>();
	//if (Operation.IsValid())
	//{
	//	Operation->ShowError();
	//}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return SCompoundWidget::OnDragOver(MyGeometry,DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	//TSharedPtr<FStatIDDragDropOp> Operation = DragDropEvent.GetOperationAs<FStatIDDragDropOp>();
	//if (Operation.IsValid())
	//{
	//	return FReply::Handled();
	//}

	return SCompoundWidget::OnDrop(MyGeometry,DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FCursorReply STimingView::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (bIsPanning)
	{
		if (bIsDragging)
		{
			//return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
			return FCursorReply::Cursor(EMouseCursor::GrabHand);
		}
	}
	else if (bIsSelecting)
	{
		if (bIsDragging)
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		}
	}
	else if (bIsSpaceBarKeyPressed)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	return FCursorReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::B)
	{
		// Toggle Bookmarks.
		if (MarkersTrack->IsVisible())
		{
			if (!MarkersTrack->IsBookmarksTrack())
			{
				SetDrawOnlyBookmarks(true);
			}
			else
			{
				SetTimeMarkersVisible(false);
			}
		}
		else
		{
			SetTimeMarkersVisible(true);
			SetDrawOnlyBookmarks(true);
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::M)
	{
		// Toggle Time Markers.
		if (MarkersTrack->IsVisible())
		{
			if (MarkersTrack->IsBookmarksTrack())
			{
				SetDrawOnlyBookmarks(false);
			}
			else
			{
				SetTimeMarkersVisible(false);
			}
		}
		else
		{
			SetTimeMarkersVisible(true);
			SetDrawOnlyBookmarks(false);
		}

		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::F)
	{
		FrameSelection();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::C)
	{
		if (InKeyEvent.GetModifierKeys().IsControlDown())
		{
			if (SelectedEvent.IsValid())
			{
				SelectedEvent->GetTrack()->OnClipboardCopyEvent(*SelectedEvent);
			}
		}
		else
		{
			Viewport.SwitchLayoutCompactMode();
			return FReply::Handled();
		}
	}
	else if (InKeyEvent.GetKey() == EKeys::V)
	{
		ToggleAutoHideEmptyTracks();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Equals ||
			 InKeyEvent.GetKey() == EKeys::Add)
	{
		// Zoom In
		const double ScaleX = Viewport.GetScaleX() * 1.25;
		if (Viewport.ZoomWithFixedX(ScaleX, Viewport.GetWidth() / 2))
		{
			UpdateHorizontalScrollBar();
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Hyphen ||
			 InKeyEvent.GetKey() == EKeys::Subtract)
	{
		// Zoom Out
		const double ScaleX = Viewport.GetScaleX() / 1.25;
		if (Viewport.ZoomWithFixedX(ScaleX, Viewport.GetWidth() / 2))
		{
			UpdateHorizontalScrollBar();
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Left)
	{
		if (InKeyEvent.GetModifierKeys().IsControlDown())
		{
			// Scroll Left
			const double NewStartTime = Viewport.GetStartTime() - Viewport.GetDuration() * 0.05;
			ScrollAtTime(EnforceHorizontalScrollLimits(NewStartTime));
		}
		else
		{
			SelectLeftTimingEvent();
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Right)
	{
		if (InKeyEvent.GetModifierKeys().IsControlDown())
		{
			// Scroll Right
			const double NewStartTime = Viewport.GetStartTime() + Viewport.GetDuration() * 0.05;
			ScrollAtTime(EnforceHorizontalScrollLimits(NewStartTime));
		}
		else
		{
			SelectRightTimingEvent();
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Up)
	{
		if (InKeyEvent.GetModifierKeys().IsControlDown())
		{
			// Scroll Up
			const float NewScrollPosY = Viewport.GetScrollPosY() - 16.0f * 3;
			ScrollAtPosY(EnforceVerticalScrollLimits(NewScrollPosY));
		}
		else
		{
			SelectUpTimingEvent();
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Down)
	{
		if (InKeyEvent.GetModifierKeys().IsControlDown())
		{
			// Scroll Down
			const float NewScrollPosY = Viewport.GetScrollPosY() + 16.0f * 3;
			ScrollAtPosY(EnforceVerticalScrollLimits(NewScrollPosY));
		}
		else
		{
			SelectDownTimingEvent();
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		if (SelectedEvent.IsValid())
		{
			const double Duration = Viewport.RestrictDuration(SelectedEvent->GetStartTime(), SelectedEvent->GetEndTime());
			SelectTimeInterval(SelectedEvent->GetStartTime(), Duration);
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		bIsSpaceBarKeyPressed = true;
		FSlateApplication::Get().QueryCursor();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::D) // debug: toggles down-sampling on/off
	{
		FTimingEventsTrack::bUseDownSampling = !FTimingEventsTrack::bUseDownSampling;
		Viewport.AddDirtyFlags(ETimingTrackViewportDirtyFlags::HInvalidated);
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::G) // debug: toggles Graph track on/off
	{
		ShowHideGraphTrack_Execute();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Y) // debug: toggles GPU track on/off
	{
		ThreadTimingSharedState->ShowHideAllGpuTracks();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::U) // debug: toggles CPU tracks on/off
	{
		ThreadTimingSharedState->ShowHideAllCpuTracks();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::L)  // debug: toggles Loading tracks on/off
	{
		LoadingSharedState->ShowHideAllLoadingTracks();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::I)  // debug: toggles IO tracks on/off
	{
		FileActivitySharedState->ShowHideAllIoTracks();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::O)  // debug: toggles IO merge lanes algorithm
	{
		//FileActivitySharedState->ToggleMergeLanes();
		//FileActivitySharedState->RequestUpdate();
		FileActivitySharedState->ToggleBackgroundEvents();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::One)
	{
		LoadingSharedState->SetColorSchema(0);
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Two)
	{
		LoadingSharedState->SetColorSchema(1);
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Three)
	{
		LoadingSharedState->SetColorSchema(2);
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Four)
	{
		LoadingSharedState->SetColorSchema(3);
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		bIsSpaceBarKeyPressed = false;
		FSlateApplication::Get().QueryCursor();
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyUp(MyGeometry, InKeyEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ShowContextMenu(const FPointerEvent& MouseEvent)
{
	//TSharedPtr<FUICommandList> ProfilerCommandList = FTimingProfilerManager::Get()->GetCommandList();
	//const FTimingProfilerCommands& ProfilerCommands = FTimingProfilerManager::GetCommands();
	//const FTimingViewCommands& Commands = FTimingViewCommands::Get();

	const bool bShouldCloseWindowAfterMenuSelection = true;
	//FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ProfilerCommandList);
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);

	if (GraphTrack->IsVisible() &&
		MousePosition.Y >= GraphTrack->GetPosY() &&
		MousePosition.Y < GraphTrack->GetPosY() + GraphTrack->GetHeight())
	{
		//GraphTrack->BuildContextMenu(MenuBuilder);
		MenuBuilder.AddSubMenu
		(
			LOCTEXT("ContextMenu_Header_GraphTrack", "Graph Track"),
			TAttribute<FText>(), // no tooltip
			FNewMenuDelegate::CreateSP(GraphTrack.Get(), &FBaseTimingTrack::BuildContextMenu),
			false,
			FSlateIcon()
		);
	}
	else if (HoveredTrack.IsValid())
	{
		HoveredTrack->BuildContextMenu(MenuBuilder);
	}
	else
	{
		MenuBuilder.BeginSection(TEXT("Empty"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ContextMenu_NA", "N/A"),
				LOCTEXT("ContextMenu_NA_Desc", "No actions available."),
				FSlateIcon(),
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([](){ return false; })),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		}
		MenuBuilder.EndSection();
	}

	//MenuBuilder.BeginSection(TEXT("Misc"), LOCTEXT("Miscellaneous", "Miscellaneous"));
	//{
	//	MenuBuilder.AddMenuEntry(Commands.ShowAllGpuTracks);
	//	MenuBuilder.AddMenuEntry(Commands.ShowAllCpuTracks);
	//	MenuBuilder.AddMenuEntry(ProfilerCommands.ToggleTimersViewVisibility);
	//	MenuBuilder.AddMenuEntry(ProfilerCommands.ToggleStatsCountersViewVisibility);
	//}
	//MenuBuilder.EndSection();

	TSharedRef<SWidget> MenuWidget = MenuBuilder.MakeWidget();

	FWidgetPath EventPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	FSlateApplication::Get().PushMenu(SharedThis(this), EventPath, MenuWidget, ScreenSpacePosition, FPopupTransitionEffect::ContextMenu);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::BindCommands()
{
	//FTimingViewCommands::Register();
	//const FTimingViewCommands& Commands = FTimingViewCommands::Get();
	//
	//TSharedPtr<FUICommandList> CommandList = FTimingProfilerManager::Get()->GetCommandList();

	//CommandList->MapAction(
	//	Commands.ShowAllGpuTracks,
	//	FExecuteAction::CreateSP(this, &STimingView::ShowHideAllGpuTracks_Execute),
	//	FCanExecuteAction(), //FCanExecuteAction::CreateLambda([] { return true; }),
	//	FIsActionChecked::CreateSP(this, &STimingView::ShowHideAllGpuTracks_IsChecked));

	//CommandList->MapAction(
	//	Commands.ShowAllCpuTracks,
	//	FExecuteAction::CreateSP(this, &STimingView::ShowHideAllCpuTracks_Execute),
	//	FCanExecuteAction(), //FCanExecuteAction::CreateLambda([] { return true; }),
	//	FIsActionChecked::CreateSP(this, &STimingView::ShowHideAllCpuTracks_IsChecked));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double STimingView::EnforceHorizontalScrollLimits(const double InStartTime)
{
	double NewStartTime = InStartTime;

	double MinT, MaxT;
	Viewport.GetHorizontalScrollLimits(MinT, MaxT);

	if (NewStartTime > MaxT)
	{
		NewStartTime = MaxT;
		OverscrollRight = 1.0f;
	}

	if (NewStartTime < MinT)
	{
		NewStartTime = MinT;
		OverscrollLeft = 1.0f;
	}

	return NewStartTime;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

float STimingView::EnforceVerticalScrollLimits(const float InScrollPosY)
{
	float NewScrollPosY = InScrollPosY;

	const float DY = Viewport.GetScrollHeight() - Viewport.GetScrollableAreaHeight() + 7.0f; // +7 is to allow some space for the horizontal scrollbar
	const float MinY = FMath::Min(DY, 0.0f);
	const float MaxY = DY - MinY;

	if (NewScrollPosY > MaxY)
	{
		NewScrollPosY = MaxY;
		OverscrollBottom = 1.0f;
	}

	if (NewScrollPosY < MinY)
	{
		NewScrollPosY = MinY;
		OverscrollTop = 1.0f;
	}

	return NewScrollPosY;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::HorizontalScrollBar_OnUserScrolled(float ScrollOffset)
{
	Viewport.OnUserScrolled(HorizontalScrollBar, ScrollOffset);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdateHorizontalScrollBar()
{
	Viewport.UpdateScrollBar(HorizontalScrollBar);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::VerticalScrollBar_OnUserScrolled(float ScrollOffset)
{
	Viewport.OnUserScrolledY(VerticalScrollBar, ScrollOffset);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdateVerticalScrollBar()
{
	Viewport.UpdateScrollBarY(VerticalScrollBar);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ScrollAtPosY(float ScrollPosY)
{
	if (ScrollPosY != Viewport.GetScrollPosY())
	{
		Viewport.SetScrollPosY(ScrollPosY);
		UpdateVerticalScrollBar();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ScrollAtTime(double StartTime)
{
	if (Viewport.ScrollAtTime(StartTime))
	{
		UpdateHorizontalScrollBar();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::CenterOnTimeInterval(double IntervalStartTime, double IntervalDuration)
{
	if (Viewport.CenterOnTimeInterval(IntervalStartTime, IntervalDuration))
	{
		Viewport.EnforceHorizontalScrollLimits(1.0); // 1.0 is to disable interpolation
		UpdateHorizontalScrollBar();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::BringIntoView(double StartTime, double EndTime)
{
	EndTime = Viewport.RestrictEndTime(EndTime);

	// Increase interval with 8% (of view size) on each side.
	const double DT = Viewport.GetDuration() * 0.08;
	StartTime -= DT;
	EndTime += DT;

	double NewStartTime = Viewport.GetStartTime();

	if (EndTime > Viewport.GetEndTime())
	{
		NewStartTime += EndTime - Viewport.GetEndTime();
	}

	if (StartTime < NewStartTime)
	{
		NewStartTime = StartTime;
	}

	ScrollAtTime(NewStartTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectTimeInterval(double IntervalStartTime, double IntervalDuration)
{
	SelectionStartTime = IntervalStartTime;
	SelectionEndTime = IntervalStartTime + IntervalDuration;
	LastSelectionType = ESelectionType::TimeRange;
	RaiseSelectionChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::RaiseSelectionChanging()
{
	OnSelectionChangedDelegate.Broadcast(Insights::ETimeChangedFlags::Interactive, SelectionStartTime, SelectionEndTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::RaiseSelectionChanged()
{
	OnSelectionChangedDelegate.Broadcast(Insights::ETimeChangedFlags::None, SelectionStartTime, SelectionEndTime);

	FTimingProfilerManager::Get()->SetSelectedTimeRange(SelectionStartTime, SelectionEndTime);

	if (SelectionStartTime < SelectionEndTime)
	{
		UpdateAggregatedStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::RaiseTimeMarkerChanging()
{
	OnTimeMarkerChangedDelegate.Broadcast(Insights::ETimeChangedFlags::Interactive, TimeMarker);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::RaiseTimeMarkerChanged()
{
	OnTimeMarkerChangedDelegate.Broadcast(Insights::ETimeChangedFlags::None, TimeMarker);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdateAggregatedStats()
{
	if (bAssetLoadingMode)
	{
		TSharedPtr<SLoadingProfilerWindow> LoadingProfilerWnd = FLoadingProfilerManager::Get()->GetProfilerWindow();
		if (LoadingProfilerWnd.IsValid())
		{
			LoadingProfilerWnd->UpdateTableTreeViews();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SetAndCenterOnTimeMarker(double Time)
{
	TimeMarker = Time;

	double MinT, MaxT;
	Viewport.GetHorizontalScrollLimits(MinT, MaxT);
	const double ViewportDuration = Viewport.GetDuration();
	MinT += ViewportDuration / 2;
	MaxT += ViewportDuration / 2;
	Time = FMath::Clamp<double>(Time, MinT, MaxT);

	Time = Viewport.AlignTimeToPixel(Time);
	CenterOnTimeInterval(Time, 0.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectToTimeMarker(double Time)
{
	if (TimeMarker < Time)
	{
		SelectTimeInterval(TimeMarker, Time - TimeMarker);
	}
	else
	{
		SelectTimeInterval(Time, TimeMarker - Time);
	}

	TimeMarker = Time;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SetTimeMarkersVisible(bool bIsMarkersTrackVisible)
{
	if (MarkersTrack->IsVisible() != bIsMarkersTrackVisible)
	{
		MarkersTrack->SetVisibilityFlag(bIsMarkersTrackVisible);

		if (MarkersTrack->IsVisible())
		{
			if (Viewport.GetScrollPosY() != 0.0f)
			{
				UE_LOG(TimingProfiler, Log, TEXT("SetTimeMarkersVisible!!!"));
				Viewport.SetScrollPosY(Viewport.GetScrollPosY() + MarkersTrack->GetHeight());
			}

			MarkersTrack->SetDirtyFlag();
		}
		else
		{
			UE_LOG(TimingProfiler, Log, TEXT("SetTimeMarkersVisible!!!"));
			Viewport.SetScrollPosY(Viewport.GetScrollPosY() - MarkersTrack->GetHeight());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SetDrawOnlyBookmarks(bool bIsBookmarksTrack)
{
	if (MarkersTrack->IsBookmarksTrack() != bIsBookmarksTrack)
	{
		const float PrevHeight = MarkersTrack->GetHeight();
		MarkersTrack->SetBookmarksTrackFlag(bIsBookmarksTrack);

		if (MarkersTrack->IsVisible())
		{
			if (Viewport.GetScrollPosY() != 0.0f)
			{
				UE_LOG(TimingProfiler, Log, TEXT("SetDrawOnlyBookmarks!!!"));
				Viewport.SetScrollPosY(Viewport.GetScrollPosY() + MarkersTrack->GetHeight() - PrevHeight);
			}

			MarkersTrack->SetDirtyFlag();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<FBaseTimingTrack> STimingView::GetTrackAt(float InPosX, float InPosY) const
{
	TSharedPtr<FBaseTimingTrack> FoundTrack;

	if (InPosY < 0.0f)
	{
		// above viewport
	}
	else if (InPosY < Viewport.GetTopOffset())
	{
		// Top Docked Tracks
		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : TopDockedTracks)
		{
			const FBaseTimingTrack& Track = *TrackPtr;
			if (TrackPtr->IsVisible())
			{
				if (InPosY >= Track.GetPosY() && InPosY < Track.GetPosY() + Track.GetHeight())
				{
					FoundTrack = TrackPtr;
					break;
				}
			}
		}
	}
	else if (InPosY < Viewport.GetHeight() - Viewport.GetBottomOffset())
	{
		// Scrollable Tracks
		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
		{
			const FBaseTimingTrack& Track = *TrackPtr;
			if (Track.IsVisible())
			{
				if (InPosY >= Track.GetPosY() && InPosY < Track.GetPosY() + Track.GetHeight())
				{
					FoundTrack = TrackPtr;
					break;
				}
			}
		}
	}
	else if (InPosY < Viewport.GetHeight())
	{
		// Bottom Docked Tracks
		for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : BottomDockedTracks)
		{
			const FBaseTimingTrack& Track = *TrackPtr;
			if (TrackPtr->IsVisible())
			{
				if (InPosY >= Track.GetPosY() && InPosY < Track.GetPosY() + Track.GetHeight())
				{
					FoundTrack = TrackPtr;
					break;
				}
			}
		}
	}
	else
	{
		// below viewport
	}

	return FoundTrack;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdateHoveredTimingEvent(float InMousePosX, float InMousePosY)
{
	TSharedPtr<FBaseTimingTrack> NewHoveredTrack = GetTrackAt(InMousePosX, InMousePosY);
	if (NewHoveredTrack != HoveredTrack)
	{
		HoveredTrack = NewHoveredTrack;
		OnHoveredTrackChangedDelegate.Broadcast(HoveredTrack);
	}

	TSharedPtr<const ITimingEvent> NewHoveredEvent;
	if (HoveredTrack.IsValid())
	{
		NewHoveredEvent = HoveredTrack->GetEvent(InMousePosX, InMousePosY, Viewport);
	}

	if (NewHoveredEvent.IsValid())
	{
		if (!HoveredEvent.IsValid() || !NewHoveredEvent->Equals(*HoveredEvent))
		{
			HoveredEvent = NewHoveredEvent;
			ensure(HoveredTrack == HoveredEvent->GetTrack());
			HoveredTrack->UpdateEventStats(const_cast<ITimingEvent&>(*HoveredEvent));
			HoveredTrack->InitTooltip(Tooltip, *HoveredEvent);
			OnHoveredEventChangedDelegate.Broadcast(HoveredEvent);
		}
		Tooltip.SetDesiredOpacity(1.0f);
	}
	else
	{
		if (HoveredEvent.IsValid())
		{
			HoveredEvent.Reset();
			OnHoveredEventChangedDelegate.Broadcast(HoveredEvent);
		}
		Tooltip.SetDesiredOpacity(0.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::OnSelectedTimingEventChanged()
{
	if (!bAssetLoadingMode)
	{
		if (SelectedEvent.IsValid())
		{
			SelectedEvent->GetTrack()->UpdateEventStats(const_cast<ITimingEvent&>(*SelectedEvent));
			SelectedEvent->GetTrack()->OnEventSelected(*SelectedEvent);
		}
	}

	OnSelectedEventChangedDelegate.Broadcast(SelectedEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectHoveredTimingTrack()
{
	if (SelectedTrack != HoveredTrack)
	{
		SelectedTrack = HoveredTrack;
		OnSelectedTrackChangedDelegate.Broadcast(SelectedTrack);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectHoveredTimingEvent()
{
	if (SelectedEvent != HoveredEvent)
	{
		SelectedEvent = HoveredEvent;

		if (SelectedEvent.IsValid())
		{
			LastSelectionType = ESelectionType::TimingEvent;
			BringIntoView(SelectedEvent->GetStartTime(), SelectedEvent->GetEndTime());
		}

		OnSelectedTimingEventChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectLeftTimingEvent()
{
	if (SelectedEvent.IsValid())
	{
		const uint32 Depth = SelectedEvent->GetDepth();
		const double StartTime = SelectedEvent->GetStartTime();
		const double EndTime = SelectedEvent->GetEndTime();

		auto EventFilter = [Depth, StartTime, EndTime](double EventStartTime, double EventEndTime, uint32 EventDepth)
		{
			return EventDepth == Depth
				&& (EventStartTime < StartTime || EventEndTime < EndTime);
		};

		const TSharedPtr<const ITimingEvent> LeftEvent = SelectedEvent->GetTrack()->SearchEvent(
			FTimingEventSearchParameters(0.0, StartTime, ETimingEventSearchFlags::SearchAll, EventFilter));

		if (LeftEvent.IsValid())
		{
			SelectedEvent = LeftEvent;
			BringIntoView(SelectedEvent->GetStartTime(), SelectedEvent->GetEndTime());
			OnSelectedTimingEventChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectRightTimingEvent()
{
	if (SelectedEvent.IsValid())
	{
		const uint32 Depth = SelectedEvent->GetDepth();
		const double StartTime = SelectedEvent->GetStartTime();
		const double EndTime = SelectedEvent->GetEndTime();

		auto EventFilter = [Depth, StartTime, EndTime](double EventStartTime, double EventEndTime, uint32 EventDepth)
		{
			return EventDepth == Depth
				&& (EventStartTime > StartTime || EventEndTime > EndTime);
		};

		const TSharedPtr<const ITimingEvent> RightEvent = SelectedEvent->GetTrack()->SearchEvent(
			FTimingEventSearchParameters(EndTime, Viewport.GetMaxValidTime(), ETimingEventSearchFlags::StopAtFirstMatch, EventFilter));

		if (RightEvent.IsValid())
		{
			SelectedEvent = RightEvent;
			BringIntoView(SelectedEvent->GetStartTime(), SelectedEvent->GetEndTime());
			OnSelectedTimingEventChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectUpTimingEvent()
{
	if (SelectedEvent.IsValid() &&
		SelectedEvent->GetDepth() > 0)
	{
		const uint32 Depth = SelectedEvent->GetDepth() - 1;
		const double StartTime = SelectedEvent->GetStartTime();
		const double EndTime = SelectedEvent->GetEndTime();

		auto EventFilter = [Depth, StartTime, EndTime](double EventStartTime, double EventEndTime, uint32 EventDepth)
		{
			return EventDepth == Depth
				&& EventStartTime <= EndTime
				&& EventEndTime >= StartTime;
		};

		const TSharedPtr<const ITimingEvent> UpEvent = SelectedEvent->GetTrack()->SearchEvent(
			FTimingEventSearchParameters(StartTime, EndTime, ETimingEventSearchFlags::StopAtFirstMatch, EventFilter));

		if (UpEvent.IsValid())
		{
			SelectedEvent = UpEvent;
			BringIntoView(SelectedEvent->GetStartTime(), SelectedEvent->GetEndTime());
			OnSelectedTimingEventChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectDownTimingEvent()
{
	if (SelectedEvent.IsValid())
	{
		const uint32 Depth = SelectedEvent->GetDepth() + 1;
		const double StartTime = SelectedEvent->GetStartTime();
		const double EndTime = SelectedEvent->GetEndTime();
		double LargestDuration = 0.0;

		auto EventFilter = [Depth, StartTime, EndTime, &LargestDuration](double EventStartTime, double EventEndTime, uint32 EventDepth)
		{
			const double Duration = EventEndTime - EventStartTime;
			return Duration > LargestDuration
				&& EventDepth == Depth
				&& EventStartTime <= EndTime
				&& EventEndTime >= StartTime;
		};

		auto EventMatched = [&LargestDuration](double EventStartTime, double EventEndTime, uint32 EventDepth)
		{
			const double Duration = EventEndTime - EventStartTime;
			LargestDuration = Duration;
		};

		const TSharedPtr<const ITimingEvent> DownEvent = SelectedEvent->GetTrack()->SearchEvent(
			FTimingEventSearchParameters(StartTime, EndTime, ETimingEventSearchFlags::SearchAll, EventFilter, EventMatched));

		if (DownEvent.IsValid())
		{
			SelectedEvent = DownEvent;
			BringIntoView(SelectedEvent->GetStartTime(), SelectedEvent->GetEndTime());
			OnSelectedTimingEventChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::FrameSelection()
{
	double StartTime, EndTime;

	ESelectionType Type = ESelectionType::None;

	if (LastSelectionType == ESelectionType::TimingEvent)
	{
		// Try framing the selected timing event.
		if (SelectedEvent.IsValid())
		{
			Type = ESelectionType::TimingEvent;
		}

		// Next time, try framing the selected time range.
		LastSelectionType = ESelectionType::TimeRange;
	}
	else if (LastSelectionType == ESelectionType::TimeRange)
	{
		// Try framing the selected time range.
		if (SelectionEndTime > SelectionStartTime)
		{
			Type = ESelectionType::TimeRange;
		}

		// Next time, try framing the selected timing event.
		LastSelectionType = ESelectionType::TimingEvent;
	}

	// If no last selection or last selection is empty...
	if (LastSelectionType == ESelectionType::None || Type == ESelectionType::None)
	{
		// First, try framing the selected timing event...
		if (SelectedEvent.IsValid())
		{
			Type = ESelectionType::TimingEvent;
		}
		else // ...otherwise, try framing the selected time range
		{
			Type = ESelectionType::TimeRange;
		}
	}

	if (Type == ESelectionType::TimingEvent)
	{
		// Frame the selected event.
		StartTime = SelectedEvent->GetStartTime();
		EndTime = Viewport.RestrictEndTime(SelectedEvent->GetEndTime());
		if (EndTime == StartTime)
		{
			EndTime += 1.0 / Viewport.GetScaleX(); // +1px
		}
	}
	else
	{
		// Frame the selected time range.
		StartTime = SelectionStartTime;
		EndTime = Viewport.RestrictEndTime(SelectionEndTime);
	}

	if (EndTime > StartTime)
	{
		const double Duration = EndTime - StartTime;
		if (Viewport.ZoomOnTimeInterval(StartTime - Duration * 0.1, Duration * 1.2))
		{
			UpdateHorizontalScrollBar();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SetEventFilter(const TSharedPtr<ITimingEventFilter> InEventFilter)
{
	TimingEventFilter = InEventFilter;
	Viewport.AddDirtyFlags(ETimingTrackViewportDirtyFlags::HInvalidated);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STimingView::MakeTracksFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.BeginSection("QuickFilter", LOCTEXT("TracksFilterHeading", "Quick Filter"));
	{
		//const FTimingViewCommands& Commands = FTimingViewCommands::Get();

		//TODO: MenuBuilder.AddMenuEntry(Commands.AutoHideGraphTrack);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowGraphTrack", "Graph Track - G"),
			LOCTEXT("ShowGraphTrack_Tooltip", "Show/hide the Graph track"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &STimingView::ShowHideGraphTrack_Execute),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &STimingView::ShowHideGraphTrack_IsChecked)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		MenuBuilder.AddMenuSeparator("QuickFilterSeparator");

		//TODO: MenuBuilder.AddMenuEntry(Commands.AutoHideEmptyTracks);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("AutoHideEmptyTracks", "Auto Hide Empty Tracks - V"),
			LOCTEXT("AutoHideEmptyTracks_Tooltip", "Auto hide empty tracks (ones without timing events in current viewport)"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &STimingView::ToggleAutoHideEmptyTracks),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &STimingView::IsAutoHideEmptyTracksEnabled)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	//MenuBuilder.BeginSection("Tracks", LOCTEXT("TracksHeading", "Tracks"));
	//CreateTracksMenu(MenuBuilder);
	//MenuBuilder.EndSection();

	// Let any plugin extend the filter menu.
	for (Insights::ITimingViewExtender* Extender : GetExtenders())
	{
		Extender->ExtendFilterMenu(*this, MenuBuilder);
	}

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::CreateTracksMenu(FMenuBuilder& MenuBuilder)
{
	for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
	{
		const FBaseTimingTrack& Track = *TrackPtr;
		if (Track.GetHeight() > 0.0f || Viewport.GetLayout().TargetMinTimelineH > 0.0f)
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(Track.GetName()),
				TAttribute<FText>(), // no tooltip
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &STimingView::ToggleTrackVisibility_Execute, Track.GetId()),
						  FCanExecuteAction::CreateLambda([] { return true; }),
						  FIsActionChecked::CreateSP(this, &STimingView::ToggleTrackVisibility_IsChecked, Track.GetId())),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::ShowHideGraphTrack_IsChecked() const
{
	return GraphTrack->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ShowHideGraphTrack_Execute()
{
	GraphTrack->ToggleVisibility();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::IsAutoHideEmptyTracksEnabled() const
{
	return (Viewport.GetLayout().TargetMinTimelineH == 0.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ToggleAutoHideEmptyTracks()
{
	Viewport.ToggleLayoutMinTrackHeight();

	for (const TSharedPtr<FBaseTimingTrack>& TrackPtr : ScrollableTracks)
	{
		TrackPtr->SetHeight(0.0f);
	}

	ScrollAtPosY(0.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::ToggleTrackVisibility_IsChecked(uint64 InTrackId) const
{
	const TSharedPtr<FBaseTimingTrack>* const TrackPtrPtr = AllTracks.Find(InTrackId);
	if (TrackPtrPtr)
	{
		return (*TrackPtrPtr)->IsVisible();
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ToggleTrackVisibility_Execute(uint64 InTrackId)
{
	const TSharedPtr<FBaseTimingTrack>* TrackPtrPtr = AllTracks.Find(InTrackId);
	if (TrackPtrPtr)
	{
		(*TrackPtrPtr)->ToggleVisibility();
		OnTrackVisibilityChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::OnTrackVisibilityChanged()
{
	if (HoveredTrack.IsValid())
	{
		HoveredTrack.Reset();
		OnHoveredTrackChangedDelegate.Broadcast(HoveredTrack);
	}
	if (HoveredEvent.IsValid())
	{
		HoveredEvent.Reset();
		OnHoveredEventChangedDelegate.Broadcast(HoveredEvent);
	}
	if (SelectedTrack.IsValid())
	{
		SelectedTrack.Reset();
		OnSelectedTrackChangedDelegate.Broadcast(SelectedTrack);
	}
	if (SelectedEvent.IsValid())
	{
		SelectedEvent.Reset();
		OnSelectedEventChangedDelegate.Broadcast(SelectedEvent);
	}
	Tooltip.SetDesiredOpacity(0.0f);

	//TODO: ThreadFilterChangedEvent.Broadcast();
	FTimingProfilerManager::Get()->OnThreadFilterChanged();

	if (SelectionStartTime < SelectionEndTime)
	{
		UpdateAggregatedStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::PreventThrottling()
{
	bPreventThrottling = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FBaseTimingTrack> STimingView::FindTrack(uint64 InTrackId)
{
	TSharedPtr<FBaseTimingTrack>* TrackPtrPtr = AllTracks.Find(InTrackId);
	return TrackPtrPtr ? *TrackPtrPtr : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TArray<Insights::ITimingViewExtender*> STimingView::GetExtenders() const
{
	return IModularFeatures::Get().GetModularFeatureImplementations<Insights::ITimingViewExtender>(Insights::TimingViewExtenderFeatureName);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
