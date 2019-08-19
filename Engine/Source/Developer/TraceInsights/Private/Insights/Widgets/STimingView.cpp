// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "STimingView.h"

#include "Containers/ArrayBuilder.h"
#include "Containers/MapBuilder.h"
#include "EditorStyleSet.h"
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
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/LoadingProfiler/LoadingProfilerManager.h"
#include "Insights/LoadingProfiler/Widgets/SLoadingProfilerWindow.h"
#include "Insights/Table/Widgets/STableTreeView.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/BaseTimingTrack.h"
#include "Insights/ViewModels/FileActivityTimingTrack.h"
#include "Insights/ViewModels/LoadingTimingTrack.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/ViewModels/TimingGraphTrack.h"
#include "Insights/ViewModels/DrawHelpers.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/Widgets/SStatsView.h"
#include "Insights/Widgets/STimersView.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////

#define DEBUG_TIMING_TRACK 0
#define LOCTEXT_NAMESPACE "STimingView"

// start auto generated ids from a big number (MSB set to 1) to avoid collisions with ids for gpu/cpu tracks based on 32bit timeline index
uint64 FBaseTimingTrack::IdGenerator = (1ULL << 63);

const TCHAR* GetName(ELoadTimeProfilerPackageEventType Type);
const TCHAR* GetName(ELoadTimeProfilerObjectEventType Type);

const TCHAR* GetFileActivityTypeName(Trace::EFileActivityType Type);
uint32 GetFileActivityTypeColor(Trace::EFileActivityType Type);

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingView::STimingView()
	: LoadingSharedState(MakeShareable(new FLoadingSharedState()))
	, bAssetLoadingMode(false)
	, FileActivitySharedState(MakeShareable(new FFileActivitySharedState()))
	, TimeRulerTrack(FBaseTimingTrack::GenerateId())
	, MarkersTrack(FBaseTimingTrack::GenerateId())
	, GraphTrack(MakeShareable(new FTimingGraphTrack(FBaseTimingTrack::GenerateId())))
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, MainFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

STimingView::~STimingView()
{
	for (const auto& KV : CachedTimelines)
	{
		delete KV.Value;
	}
	CachedTimelines.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::Construct(const FArguments& InArgs)
{
	OnSelectionChanged = InArgs._OnSelectionChanged;
	OnHoveredEventChanged = InArgs._OnHoveredEventChanged;
	OnSelectedEventChanged = InArgs._OnSelectedEventChanged;

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
	bShowHideAllGpuTracks = true;
	bShowHideAllCpuTracks = true;
	bShowHideAllLoadingTracks = false;
	bShowHideAllIoTracks = false;

	//////////////////////////////////////////////////

	Viewport.Reset();

	bIsViewportDirty = true;
	bIsVerticalViewportDirty = true;

	//////////////////////////////////////////////////

	for (const auto& KV : CachedTimelines)
	{
		delete KV.Value;
	}
	CachedTimelines.Reset();

	//TODO: TopTracks.Reset();
	//TODO: ScrollableTracks.Reset();
	//TODO: BottomTracks.Reset();
	//TODO: ForegroundTracks.Reset();

	//////////////////////////////////////////////////

	TimingEventsTracks.Reset();

	bAreTimingEventsTracksDirty = true;

	FTimingEventsTrack::bUseDownSampling = true;

	//////////////////////////////////////////////////

	GpuTrack = nullptr;
	CpuTracks.Reset();

	ThreadGroups.Reset();

	//////////////////////////////////////////////////

	LoadingSharedState->Reset();

	LoadingMainThreadTrack = nullptr;
	LoadingAsyncThreadTrack = nullptr;

	LoadingMainThreadId = 0;
	LoadingAsyncThreadId = 0;

	EventAggregationTotalCount = 0;
	EventAggregation.Reset();
	ObjectTypeAggregationTotalCount = 0;
	ObjectTypeAggregation.Reset();

	//////////////////////////////////////////////////

	FileActivitySharedState->Reset();
	IoOverviewTrack = nullptr;
	IoActivityTrack = nullptr;

	//////////////////////////////////////////////////

	TimeRulerTrack.Reset();
	MarkersTrack.Reset();

	GraphTrack->Reset();
	GraphTrack->SetHeight(200.0f);
	GraphTrack->AddDefaultFrameSeries();
	GraphTrack->SetVisibilityFlag(false);

	//////////////////////////////////////////////////

	MousePosition = FVector2D::ZeroVector;

	MousePositionOnButtonDown = FVector2D::ZeroVector;
	ViewportStartTimeOnButtonDown = 0.0;
	ViewportScrollPosYOnButtonDown = 0.0f;

	MousePositionOnButtonUp = FVector2D::ZeroVector;

	bIsLMB_Pressed = false;
	bIsRMB_Pressed = false;

	bIsSpaceBarKeyPressed = false;
	bIsDragging = false;

	bIsPanning = false;
	PanningMode = EPanningMode::None;

	bIsSelecting = false;
	SelectionStartTime = 0.0;
	SelectionEndTime = 0.0;

	HoveredTimingEvent.Reset();
	SelectedTimingEvent.Reset();
	Tooltip.Reset();

	LastSelectionType = ESelectionType::None;

	TimeMarker = std::numeric_limits<double>::infinity();

	//ThisGeometry

	Layout.ForceNormalMode();

	//////////////////////////////////////////////////

	NumUpdatedEvents = 0;
	TimelineCacheUpdateDurationHistory.Reset();
	TimelineCacheUpdateDurationHistory.AddValue(0);
	TimeMarkerCacheUpdateDurationHistory.Reset();
	TimeMarkerCacheUpdateDurationHistory.AddValue(0);
	DrawDurationHistory.Reset();
	OnPaintDurationHistory.Reset();
	LastOnPaintTime = FPlatformTime::Cycles64();

	if (bAssetLoadingMode)
	{
		EnableAssetLoadingMode();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::EnableAssetLoadingMode()
{
	bAssetLoadingMode = true;

	bShowHideAllGpuTracks = false;
	bShowHideAllCpuTracks = false;
	bShowHideAllLoadingTracks = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	//FStopwatch TickStopwatch;
	//TickStopwatch.Start();

	ThisGeometry = AllottedGeometry;

	const float ViewWidth = AllottedGeometry.GetLocalSize().X;
	const float ViewHeight = AllottedGeometry.GetLocalSize().Y;
	if (Viewport.UpdateSize(ViewWidth, ViewHeight))
	{
		bIsViewportDirty = true;
	}

	//////////////////////////////////////////////////
	// Update Y postion of non-scrollable tracks (docked on top).
	// Also compute total height (TopOffset) of top docked tracks.

	float TopOffset = 0.0f;
	if (TimeRulerTrack.IsVisible())
	{
		TimeRulerTrack.SetPosY(TopOffset);
		TopOffset += TimeRulerTrack.GetHeight();
	}
	if (MarkersTrack.IsVisible())
	{
		MarkersTrack.SetPosY(TopOffset);
		TopOffset += MarkersTrack.GetHeight();
	}
	if (GraphTrack->IsVisible())
	{
		GraphTrack->SetPosY(TopOffset);
		TopOffset += GraphTrack->GetHeight();
	}
	Viewport.SetTopOffset(TopOffset);

	//////////////////////////////////////////////////

	if (!bIsPanning)
	{
		//////////////////////////////////////////////////
		// Elastic snap to vertical scroll limits.

		const float MinY = 0.0f;// -0.5f * Viewport.Height;
		const float MaxY = Viewport.GetScrollHeight() - Viewport.GetHeight() + TopOffset + 7.0f;
		const float U = 0.5f;

		float ScrollPosY = Viewport.GetScrollPosY();
		if (ScrollPosY < MinY)
		{
			ScrollPosY = ScrollPosY * U + (1.0f - U) * MinY;
			if (FMath::IsNearlyEqual(ScrollPosY, MinY, 0.5f))
			{
				ScrollPosY = MinY;
			}
		}
		else if (ScrollPosY > MaxY)
		{
			ScrollPosY = ScrollPosY * U + (1.0f - U) * MaxY;
			if (FMath::IsNearlyEqual(ScrollPosY, MaxY, 0.5f))
			{
				ScrollPosY = MaxY;
			}
			if (ScrollPosY < MinY)
			{
				ScrollPosY = MinY;
			}
		}

		ScrollAtPosY(ScrollPosY);

		//////////////////////////////////////////////////
		// Elastic snap to horizontal time limits.

		if (Viewport.EnforceHorizontalScrollLimits(0.5)) // 0.5 is the interpolation factor
		{
			UpdateHorizontalScrollBar();
			bIsViewportDirty = true;
		}
	}

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

	FileActivitySharedState->Update();

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		// Check if horizontal scroll area has changed.
		double SessionTime = Session->GetDurationSeconds();
		if (SessionTime > Viewport.GetMaxValidTime() &&
			SessionTime != DBL_MAX &&
			SessionTime != std::numeric_limits<double>::infinity())
		{
			//UE_LOG(TimingProfiler, Log, TEXT("Session Duration: %g"), DT);
			Viewport.SetMaxValidTime(SessionTime);
			UpdateHorizontalScrollBar();
			//bIsMaxValidTimeDirty = true;

			if (SessionTime >= Viewport.GetStartTime() && SessionTime <= Viewport.GetEndTime())
			{
				bAreTimingEventsTracksDirty = true;
				MarkersTrack.SetDirtyFlag();
			}
		}

		bool bIsTimingEventsTrackDirty = false;

		if (Trace::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			const Trace::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *Trace::ReadLoadTimeProfilerProvider(*Session.Get());

			LoadingMainThreadId = LoadTimeProfilerProvider.GetMainThreadId();
			LoadingAsyncThreadId = LoadTimeProfilerProvider.GetAsyncLoadingThreadId();

			if (LoadingMainThreadTrack == nullptr)
			{
				uint64 TrackId = FBaseTimingTrack::GenerateId();
				LoadingMainThreadTrack = new FLoadingMainThreadTimingTrack(TrackId, LoadingSharedState);
				LoadingMainThreadTrack->SetOrder(-3);
				LoadingMainThreadTrack->SetVisibilityFlag(bShowHideAllLoadingTracks);
				AddTimingEventsTrack(LoadingMainThreadTrack);
				bIsTimingEventsTrackDirty = true;
			}

			if (LoadingAsyncThreadTrack == nullptr)
			{
				uint64 TrackId = FBaseTimingTrack::GenerateId();
				LoadingAsyncThreadTrack = new FLoadingAsyncThreadTimingTrack(TrackId, LoadingSharedState);
				LoadingMainThreadTrack->SetOrder(-2);
				LoadingAsyncThreadTrack->SetVisibilityFlag(bShowHideAllLoadingTracks);
				AddTimingEventsTrack(LoadingAsyncThreadTrack);
				bIsTimingEventsTrackDirty = true;
			}
		}

		if (Trace::ReadFileActivityProvider(*Session.Get()))
		{
			if (IoOverviewTrack == nullptr)
			{
				// Note: The I/O timelines are just prototypes for now (will be removed once the functionality is moved in analyzer).
				const uint64 TrackId = FBaseTimingTrack::GenerateId();
				IoOverviewTrack = new FOverviewFileActivityTimingTrack(TrackId, FileActivitySharedState);
				IoOverviewTrack->SetOrder(-1);
				IoOverviewTrack->SetVisibilityFlag(bShowHideAllIoTracks);
				AddTimingEventsTrack(IoOverviewTrack);
				bIsTimingEventsTrackDirty = true;
			}
		}

		if (Trace::ReadTimingProfilerProvider(*Session.Get()))
		{
			const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

			// Check if we have a GPU track.
			uint32 GpuTimelineIndex;
			if (TimingProfilerProvider.GetGpuTimelineIndex(GpuTimelineIndex))
			{
				const uint64 TrackId = static_cast<uint64>(GpuTimelineIndex);
				if (!CachedTimelines.Contains(TrackId))
				{
					GpuTrack = new FGpuTimingTrack(TrackId, TEXT("GPU"), nullptr);
					GpuTrack->SetOrder(0);
					GpuTrack->SetVisibilityFlag(bShowHideAllGpuTracks);
					AddTimingEventsTrack(GpuTrack);
					bIsTimingEventsTrackDirty = true;
				}
			}

			// Check available CPU tracks.
			int32 Order = 1;
			const Trace::IThreadProvider& ThreadProvider = Trace::ReadThreadProvider(*Session.Get());
			ThreadProvider.EnumerateThreads([this, &Order, &bIsTimingEventsTrackDirty, &TimingProfilerProvider](const Trace::FThreadInfo& ThreadInfo)
			{
				const TCHAR* GroupName = ThreadInfo.GroupName;
				if (GroupName == nullptr)
				{
					GroupName = ThreadInfo.Name;
				}

				bool bIsGroupVisible = bShowHideAllCpuTracks;
				if (GroupName != nullptr)
				{
					if (!ThreadGroups.Contains(GroupName))
					{
						UE_LOG(TimingProfiler, Log, TEXT("New CPU Thread Group (%d) : \"%s\""), ThreadGroups.Num() + 1, GroupName);
						ThreadGroups.Add(GroupName, { GroupName, bIsGroupVisible, 0, Order });
					}
					else
					{
						FThreadGroup& ThreadGroup = ThreadGroups[GroupName];
						bIsGroupVisible = ThreadGroup.bIsVisible;
						ThreadGroup.Order = Order;
					}
				}

				uint32 CpuTimelineIndex;
				if (TimingProfilerProvider.GetCpuThreadTimelineIndex(ThreadInfo.Id, CpuTimelineIndex))
				{
					FTimingEventsTrack* Track = nullptr;
					const uint64 TrackId = static_cast<uint64>(CpuTimelineIndex);

					if (!CachedTimelines.Contains(TrackId))
					{
						FString TrackName(ThreadInfo.Name && *ThreadInfo.Name ? ThreadInfo.Name : FString::Printf(TEXT("Thread %u"), ThreadInfo.Id));

						// Create new Timing Events track for the CPU thread.
						Track = new FCpuTimingTrack(TrackId, TrackName, GroupName);
						Track->SetOrder(Order);
						((FCpuTimingTrack*)Track)->SetThreadId(ThreadInfo.Id);
						CpuTracks.Add(ThreadInfo.Id, Track);

						FThreadGroup& ThreadGroup = ThreadGroups[GroupName];
						ThreadGroup.NumTimelines++;

						if (bAssetLoadingMode && (ThreadInfo.Id == LoadingMainThreadId || ThreadInfo.Id == LoadingAsyncThreadId))
						{
							Track->SetVisibilityFlag(true);
							ThreadGroup.bIsVisible = true;
						}
						else
						{
							Track->SetVisibilityFlag(bIsGroupVisible);
						}

						AddTimingEventsTrack(Track);
						bIsTimingEventsTrackDirty = true;
					}
					else
					{
						Track = CachedTimelines[TrackId];

						if (Track->GetOrder() != Order)
						{
							Track->SetOrder(Order);
							bIsTimingEventsTrackDirty = true;
						}
					}

					Order++;
				}
			});
		}

		if (Trace::ReadFileActivityProvider(*Session.Get()))
		{
			if (IoActivityTrack == nullptr)
			{
				// Note: The I/O timelines are just prototypes for now (will be removed once the functionality is moved in analyzer).
				uint64 TrackId = FBaseTimingTrack::GenerateId();
				IoActivityTrack = new FDetailedFileActivityTimingTrack(TrackId, FileActivitySharedState);
				IoActivityTrack->SetOrder(999999);
				IoActivityTrack->SetVisibilityFlag(bShowHideAllIoTracks);
				AddTimingEventsTrack(IoActivityTrack);
				bIsTimingEventsTrackDirty = true;
			}
		}

		if (bIsTimingEventsTrackDirty)
		{
			// The list has changed. Sort the list again.
			Algo::SortBy(TimingEventsTracks, &FTimingEventsTrack::GetOrder);
		}
	}

	// Compute total height of visible tracks.
	float TotalScrollHeight = 0.0f;
	for (FTimingEventsTrack* TrackPtr : TimingEventsTracks)
	{
		ensure(TrackPtr != nullptr);
		FTimingEventsTrack& Track = *TrackPtr;

		if (Track.IsVisible())
		{
			Track.SetPosY(TotalScrollHeight);
			TotalScrollHeight += Track.GetHeight();
		}

		// Reset track's hovered/selected state.
		Track.SetHoveredState(false);
		Track.SetSelectedFlag(false);
	}
	TotalScrollHeight += 1.0f; // allow 1 pixel at the bottom (for last horizontal line)

	// Set hovered/selected state for actual hovered/selected tracks, if any.
	if (HoveredTimingEvent.Track != nullptr)
	{
		const_cast<FTimingEventsTrack*>(HoveredTimingEvent.Track)->SetHoveredState(true);
	}
	if (SelectedTimingEvent.Track != nullptr)
	{
		const_cast<FTimingEventsTrack*>(SelectedTimingEvent.Track)->SetSelectedFlag(true);
	}

	//TimeRulerTrack.Tick(InCurrentTime, InDeltaTime);
	MarkersTrack.Tick(InCurrentTime, InDeltaTime);

	// Check if vertical scroll area has changed.
	if (TotalScrollHeight != Viewport.GetScrollHeight())
	{
		Viewport.SetScrollHeight(TotalScrollHeight);
		UpdateVerticalScrollBar();
		bIsVerticalViewportDirty = true;
	}

	// Update layout change animation (i.e. compact mode <-> normal mode).
	Layout.Update();

	//////////////////////////////////////////////////
	// Update dirty tracks.

	if (bIsViewportDirty)
	{
		bIsViewportDirty = false;

		//TimeRuler.SetDirtyFlag();
		MarkersTrack.SetDirtyFlag();
		bAreTimingEventsTracksDirty = true;
	}

	if (bIsVerticalViewportDirty)
	{
		bIsVerticalViewportDirty = false;

		bAreTimingEventsTracksDirty = true;
	}

	//if (TimeRuler.IsVisible() && TimeRuler.IsDirty())
	//{
	//	TimeRuler.ClearDirtyFlag();
	//
	//	TimeRuler.OnViewportChanged(Viewport);
	//}

	if (MarkersTrack.IsVisible() && MarkersTrack.IsDirty())
	{
		MarkersTrack.ClearDirtyFlag();

		FStopwatch Stopwatch;
		Stopwatch.Start();

		MarkersTrack.Update(Viewport);

		Stopwatch.Stop();
		TimeMarkerCacheUpdateDurationHistory.AddValue(Stopwatch.AccumulatedTime);
	}

	if (bAreTimingEventsTracksDirty)
	{
		bAreTimingEventsTracksDirty = false;

		GraphTrack->SetDirtyFlag();

		FStopwatch Stopwatch;
		Stopwatch.Start();

		for (int32 TrackIndex = 0; TrackIndex < TimingEventsTracks.Num(); ++TrackIndex)
		{
			TimingEventsTracks[TrackIndex]->Update(Viewport);
		}

		Stopwatch.Stop();
		TimelineCacheUpdateDurationHistory.AddValue(Stopwatch.AccumulatedTime);
	}

	if (GraphTrack->IsVisible())
	{
		GraphTrack->Update(Viewport);
	}

	Tooltip.Update();
	if (!MousePosition.IsZero())
	{
		Tooltip.SetPosition(MousePosition, 0.0f, Viewport.GetWidth() - 12.0f, 0.0f, Viewport.GetHeight() - 12.0f); // -12.0f is to avoid overlaping the scrollbars
	}

	//TickStopwatch.Stop();
	//TODO: TotalUpdateDurationHistory.AddValue(TickStopwatch.AccumulatedTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 STimingView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
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

	//////////////////////////////////////////////////

	FStopwatch Stopwatch;
	Stopwatch.Start();

	FTimingViewDrawHelper Helper(DrawContext, Viewport, Layout);

	// Draw background.
	Helper.DrawBackground();

	// Draw scrollable tracks.
	{
		Helper.BeginTimelines();

		for (int32 TrackIndex = 0; TrackIndex < TimingEventsTracks.Num(); ++TrackIndex)
		{
			FTimingEventsTrack& Track = *TimingEventsTracks[TrackIndex];
			if (Track.IsVisible())
			{
				Track.Draw(Helper);
			}
		}

		Helper.EndTimelines();
	}

	if (!FTimingEvent::AreEquals(SelectedTimingEvent, HoveredTimingEvent))
	{
		// Highlight the selected timing event (if any).
		if (SelectedTimingEvent.IsValid())
		{
			const float Y = Viewport.GetViewportY(SelectedTimingEvent.Track->GetPosY()) + Layout.GetLaneY(SelectedTimingEvent.Depth);
			Helper.DrawTimingEventHighlight(SelectedTimingEvent.StartTime, SelectedTimingEvent.EndTime, Y, FTimingViewDrawHelper::EHighlightMode::Selected);
		}

		// Highlight the hovered timing event (if any).
		if (HoveredTimingEvent.IsValid())
		{
			const float Y = Viewport.GetViewportY(HoveredTimingEvent.Track->GetPosY()) + Layout.GetLaneY(HoveredTimingEvent.Depth);
			Helper.DrawTimingEventHighlight(HoveredTimingEvent.StartTime, HoveredTimingEvent.EndTime, Y, FTimingViewDrawHelper::EHighlightMode::Hovered);
		}
	}
	else
	{
		// Highlight the selected and hovered timing event (if any).
		if (SelectedTimingEvent.IsValid())
		{
			const float Y = Viewport.GetViewportY(SelectedTimingEvent.Track->GetPosY()) + Layout.GetLaneY(SelectedTimingEvent.Depth);
			Helper.DrawTimingEventHighlight(SelectedTimingEvent.StartTime, SelectedTimingEvent.EndTime, Y, FTimingViewDrawHelper::EHighlightMode::SelectedAndHovered);
		}
	}

	if (GraphTrack->IsVisible())
	{
		GraphTrack->Draw(DrawContext, Viewport, MousePosition);
	}

	if (MarkersTrack.IsVisible())
	{
		// Draw the time markers (vertical lines + category & message texts).
		MarkersTrack.Draw(DrawContext, Viewport);
	}

	//////////////////////////////////////////////////

	// Draw the time ruler.
	if (TimeRulerTrack.IsVisible())
	{
		TimeRulerTrack.Draw(DrawContext, Viewport, MousePosition, bIsSelecting, SelectionStartTime, SelectionEndTime);
	}

	// Fill background for the Tracks filter combobox.
	DrawContext.DrawBox(0, 0, 66, 22, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));

	//////////////////////////////////////////////////

	// Draw the time range selection.
	FDrawHelpers::DrawTimeRangeSelection(DrawContext, Viewport, SelectionStartTime, SelectionEndTime, WhiteBrush, MainFont);

	//////////////////////////////////////////////////

	// Draw the time marker (orange vertical line).
	//DrawTimeMarker(OnPaintState);
	float TimeMarkerX = Viewport.TimeToSlateUnitsRounded(TimeMarker);
	if (TimeMarkerX >= 0.0f && TimeMarkerX < Viewport.GetWidth())
	{
		DrawContext.DrawBox(TimeMarkerX, 0.0f, 1.0f, Viewport.GetHeight(), WhiteBrush, FLinearColor(0.85f, 0.5f, 0.03f, 0.5f));
		DrawContext.LayerId++;
	}

	//////////////////////////////////////////////////

	Stopwatch.Stop();
	DrawDurationHistory.AddValue(Stopwatch.AccumulatedTime);

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
		const uint64 OnPaintDuration = CurrentTime - LastOnPaintTime;
		LastOnPaintTime = CurrentTime;
		OnPaintDurationHistory.AddValue(OnPaintDuration); // saved for last 32 OnPaint calls
		const uint64 AvgOnPaintDuration = OnPaintDurationHistory.ComputeAverage();
		const uint64 AvgOnPaintDurationMs = FStopwatch::Cycles64ToMilliseconds(AvgOnPaintDuration);
		const double AvgOnPaintFps = AvgOnPaintDurationMs != 0 ? 1.0 / FStopwatch::Cycles64ToSeconds(AvgOnPaintDuration) : 0.0;

		const uint64 AvgDrawDurationMs = FStopwatch::Cycles64ToMilliseconds(DrawDurationHistory.ComputeAverage());

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("D: %llu ms + %llu ms = %llu ms (%d fps)"),
				AvgDrawDurationMs, // drawing time
				AvgOnPaintDurationMs - AvgDrawDurationMs, // other overhead to OnPaint calls
				AvgOnPaintDurationMs, // average time between two OnPaint calls
				FMath::RoundToInt(AvgOnPaintFps)), // framerate of OnPaint calls
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display the "timelines cache update" info.

		const uint64 AvgTimelineUpdateDurationMs = FStopwatch::Cycles64ToMilliseconds(TimelineCacheUpdateDurationHistory.ComputeAverage());
		const uint64 LastTimelineUpdateDurationMs = FStopwatch::Cycles64ToMilliseconds(TimelineCacheUpdateDurationHistory.GetValue(0));

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U1 avg: %llu ms, last: %llu ms"),
				AvgTimelineUpdateDurationMs, // time to update the Timeline cache
				LastTimelineUpdateDurationMs), // last time to update the Timeline cache
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display the "time markers cache update" info.

		const uint64 AvgTimeMarkerUpdateDurationMs = FStopwatch::Cycles64ToMilliseconds(TimeMarkerCacheUpdateDurationHistory.ComputeAverage());
		const uint64 LastTimeMarkerUpdateDurationMs = FStopwatch::Cycles64ToMilliseconds(TimeMarkerCacheUpdateDurationHistory.GetValue(0));

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U2 avg: %llu ms, last: %llu ms"),
				AvgTimeMarkerUpdateDurationMs, // time to update the TimeMarker cache
				LastTimeMarkerUpdateDurationMs), // last time to update the Timeline cache
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

		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Format(TEXT("{0}{1} logs : {2} boxes, {3} texts"),
			{
				MarkersTrack.IsVisible() ? TEXT("") : TEXT("*"),
				FText::AsNumber(MarkersTrack.GetNumLogMessages()).ToString(),
				FText::AsNumber(MarkersTrack.GetNumBoxes()).ToString(),
				FText::AsNumber(MarkersTrack.GetNumTexts()).ToString(),
			}),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		//////////////////////////////////////////////////
		// Display Graph track stats.

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
		DrawContext.DrawText(DbgX, DbgY, InputStr, SummaryFont, DbgTextColor);
		DbgY += DbgDY;
	}

	//////////////////////////////////////////////////

	{
		// Draw info about selected event (bottom-right corner).
		if (SelectedTimingEvent.IsValid())
		{
			SelectedTimingEvent.Track->DrawSelectedEventInfo(SelectedTimingEvent, Viewport, DrawContext, WhiteBrush, MainFont);
		}

		// Draw tooltip with info about hovered event.
		Tooltip.Draw(DrawContext);

		if (GraphTrack->IsVisible())
		{
			GraphTrack->PostDraw(DrawContext, Viewport, MousePosition);
		}
	}

	if (bAssetLoadingMode)
	{
		constexpr float MarginX = 8.0f;
		constexpr float MarginY = 8.0f;

		const float X = ViewWidth - MarginX;
		float Y = ViewHeight;

		if (ObjectTypeAggregation.Num() > 0)
		{
			Y -= MarginY;
			Y -= DrawAssetLoadingAggregationTable(DrawContext, X, Y, TEXT("Object Type Aggregation"), ObjectTypeAggregation, ObjectTypeAggregationTotalCount);
		}

		if (EventAggregation.Num() > 0)
		{
			Y -= MarginY;
			Y -= DrawAssetLoadingAggregationTable(DrawContext, X, Y, TEXT("Event Aggregation"), EventAggregation, EventAggregationTotalCount);
		}
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

float STimingView::DrawAssetLoadingAggregationTable(FDrawContext& DrawContext, float RightX, float BottomY, const TCHAR* TableName, const TArray<FAssetLoadingEventAggregationRow>& Aggregation, int32 TotalRowCount) const
{
	const FLinearColor BackgroundColor(0.01f, 0.01f, 0.01f, 0.9f);
	const FLinearColor TextColorHeader(0.5f, 0.5f, 0.5f, 0.9f);
	const FLinearColor TextColor(1.0f, 1.0f, 1.0f, 0.9f);

	constexpr float BorderX = 4.0f;
	constexpr float BorderY = 4.0f;

	constexpr float LineH = 14.0f;

	float TableHeight = (2 + Aggregation.Num()) * LineH;
	if (Aggregation.Num() < TotalRowCount)
	{
		TableHeight += LineH; // for the "[...]" line
	}

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	float MaxNameWidth = 140.0f;
	for (const FAssetLoadingEventAggregationRow& Row : Aggregation)
	{
		float NameWidth = FontMeasureService->Measure(Row.Name, MainFont).X;
		if (NameWidth > MaxNameWidth)
		{
			MaxNameWidth = NameWidth;
		}
	}
	float NameColumnWidth = MaxNameWidth + 5.0f;

	constexpr float RowNumberColumnWidth = 20.0f;
	constexpr float CountColumnWidth = 50.0f;
	constexpr float TotalColumnWidth = 65.0f;
	constexpr float MinColumnWidth = 60.0f;
	constexpr float MaxColumnWidth = 60.0f;
	constexpr float AvgColumnWidth = 60.0f;
	constexpr float MedColumnWidth = 60.0f;

	const float TableWidth = RowNumberColumnWidth
						   + NameColumnWidth
						   + CountColumnWidth
						   + TotalColumnWidth
						   + MaxColumnWidth
						   + AvgColumnWidth
						   + MedColumnWidth
						   + MinColumnWidth;

	float W = TableWidth + 2 * BorderX;
	float H = TableHeight + 2 * BorderY;

	float Y = BottomY - H;

	DrawContext.DrawBox(RightX - W, Y, W, H, WhiteBrush, BackgroundColor);
	DrawContext.LayerId++;
	Y += BorderY;

	const float TableX = RightX - W + BorderX;

	// Table name
	{
		FString TableNameRow = FString::Printf(TEXT("%s (%d records, sorted by Total time)"), TableName, TotalRowCount);
		DrawContext.DrawText(TableX, Y, TableNameRow, MainFont, TextColor);
		Y += LineH;
	}

	// Column names
	{
		float X = TableX;

		X += RowNumberColumnWidth;

		DrawContext.DrawText(X, Y, TEXT("Name"), MainFont, TextColorHeader);
		X += NameColumnWidth;

		X += CountColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TEXT("Count"), MainFont, TextColorHeader);

		X += TotalColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TEXT("Total"), MainFont, TextColorHeader);

		X += MaxColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TEXT("Max [ms]"), MainFont, TextColorHeader);

		X += AvgColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TEXT("Avg [ms]"), MainFont, TextColorHeader);

		X += MedColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TEXT("Med [ms]"), MainFont, TextColorHeader);

		X += MinColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TEXT("Min [ms]"), MainFont, TextColorHeader);

		Y += LineH;
	}

	// Records
	for (int32 Index = 0; Index < Aggregation.Num(); ++Index)
	{
		constexpr int32 NumDigits = 2;
		constexpr bool bAddTimeUnit = false;

		const FAssetLoadingEventAggregationRow& Row = Aggregation[Index];

		float X = TableX;

		X += RowNumberColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X - 4.0f, Y, FString::Printf(TEXT("%d."), Index + 1), MainFont, TextColorHeader);

		DrawContext.DrawText(X, Y, Row.Name, MainFont, TextColor);
		X += NameColumnWidth;

		X += CountColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, FText::AsNumber(Row.Count).ToString(), MainFont, TextColor);

		X += TotalColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TimeUtils::FormatTimeAuto(Row.Total), MainFont, TextColor);

		X += MaxColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TimeUtils::FormatTimeMs(Row.Max, NumDigits, bAddTimeUnit), MainFont, TextColor);

		X += AvgColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TimeUtils::FormatTimeMs(Row.Avg, NumDigits, bAddTimeUnit), MainFont, TextColor);

		X += MedColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TimeUtils::FormatTimeMs(Row.Med, NumDigits, bAddTimeUnit), MainFont, TextColor);

		X += MinColumnWidth;
		DrawContext.DrawTextAligned(HAlign_Right, X, Y, TimeUtils::FormatTimeMs(Row.Min, NumDigits, bAddTimeUnit), MainFont, TextColor);

		Y += LineH;
	}

	if (Aggregation.Num() < TotalRowCount)
	{
		DrawContext.DrawText(TableX, Y, TEXT("[...]"), MainFont, TextColorHeader);
		Y += LineH;
	}

	DrawContext.LayerId++;

	return H;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::AddTimingEventsTrack(FTimingEventsTrack* Track)
{
	UE_LOG(TimingProfiler, Log, TEXT("New Timing Events Track (%d) : %s - %s (\"%s\")"),
		TimingEventsTracks.Num() + 1,
		*Track->GetType().ToString(),
		*Track->GetSubType().ToString(),
		*Track->GetName());

	CachedTimelines.Add(Track->GetId(), Track);
	TimingEventsTracks.Add(Track);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::IsGpuTrackVisible() const
{
	return GpuTrack != nullptr && GpuTrack->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::IsCpuTrackVisible(uint32 ThreadId) const
{
	return CpuTracks.Contains(ThreadId) && CpuTracks[ThreadId]->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonDown = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MousePosition = MousePositionOnButtonDown;

	bool bStartPanning = false;
	bool bStartSelecting = false;

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (MarkersTrack.IsVisible() && MarkersTrack.IsHeaderHovered())
		{
			MarkersTrack.ToggleCollapsed();
		}

		if (!bIsRMB_Pressed)
		{
			bIsLMB_Pressed = true;

			if (!bIsSpaceBarKeyPressed &&
				(MousePositionOnButtonDown.Y < TimeRulerTrack.GetHeight() ||
				 (MouseEvent.GetModifierKeys().IsControlDown() && MouseEvent.GetModifierKeys().IsShiftDown())))
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
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (!bIsLMB_Pressed)
		{
			bIsRMB_Pressed = true;

			if (!bIsSpaceBarKeyPressed &&
				(MousePositionOnButtonDown.Y < TimeRulerTrack.GetHeight() ||
				(MouseEvent.GetModifierKeys().IsControlDown() && MouseEvent.GetModifierKeys().IsShiftDown())))
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
	}

	if (bStartPanning)
	{
		bIsPanning = true;
		bIsDragging = false;

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

		SelectionStartTime = Viewport.SlateUnitsToTime(MousePositionOnButtonDown.X);
		SelectionEndTime = SelectionStartTime;
		LastSelectionType = ESelectionType::None;
		//TODO: SelectionChangingEvent.Broadcast(SelectionStartTime, SelectionEndTime);
		EventAggregation.Reset();
		ObjectTypeAggregation.Reset();
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
				//TODO: SelectionChangedEvent.Broadcast(SelectionStartTime, SelectionEndTime);
				UpdateAggregatedStats();

				bIsSelecting = false;
			}

			if (bIsValidForMouseClick)
			{
				// Select the hovered timing event (if any).
				UpdateHoveredTimingEvent(MousePositionOnButtonUp.X, MousePositionOnButtonUp.Y);
				SelectHoveredTimingEvent();

				// When clicking on an empty space...
				if (!SelectedTimingEvent.IsValid())
				{
					// ...reset selection.
					SelectionEndTime = SelectionStartTime = 0.0;
					LastSelectionType = ESelectionType::None;
					//TODO: SelectionChangedEvent.Broadcast(SelectionStartTime, SelectionEndTime);
					EventAggregation.Reset();
					ObjectTypeAggregation.Reset();
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
				//TODO: SelectionChangedEvent.Broadcast(SelectionStartTime, SelectionEndTime);
				UpdateAggregatedStats();

				bIsSelecting = false;
			}

			if (bIsValidForMouseClick)
			{
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
		if (MarkersTrack.IsVisible() && MarkersTrack.IsHovered())
		{
			MarkersTrack.ToggleCollapsed();
		}
		else if (HoveredTimingEvent.IsValid())
		{
			double EndTime = Viewport.RestrictEndTime(HoveredTimingEvent.EndTime);
			SelectTimeInterval(HoveredTimingEvent.StartTime, EndTime - HoveredTimingEvent.StartTime);
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
				//TODO: SelectionChangingEvent.Broadcast(SelectionStartTime, SelectionEndTime);
				EventAggregation.Reset();
				ObjectTypeAggregation.Reset();
			}
		}
		else
		{
			if (MarkersTrack.IsVisible())
			{
				MarkersTrack.UpdateHoveredState(MousePosition.X, MousePosition.Y, Viewport);
			}

			if (GraphTrack->IsVisible())
			{
				GraphTrack->UpdateHoveredState(MousePosition.X, MousePosition.Y, Viewport);
			}

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

		if (MarkersTrack.IsVisible())
		{
			MarkersTrack.UpdateHoveredState(MousePosition.X, MousePosition.Y, Viewport);
		}

		if (GraphTrack->IsVisible())
		{
			GraphTrack->UpdateHoveredState(MousePosition.X, MousePosition.Y, Viewport);
		}

		HoveredTimingEvent.Reset();
		Tooltip.SetDesiredOpacity(0.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply STimingView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetModifierKeys().IsShiftDown())
	{
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
			const float ScrollPosY = Viewport.GetScrollPosY() - ScrollSpeedY * MouseEvent.GetWheelDelta();
			ScrollAtPosY(ScrollPosY);
		}
	}
	else if (MouseEvent.GetModifierKeys().IsControlDown())
	{
		// Scroll horizontally.
		const double ScrollSpeedX = Viewport.GetDurationForViewportDX(16.0 * 3);
		ScrollAtTime(Viewport.GetStartTime() - ScrollSpeedX * MouseEvent.GetWheelDelta());
	}
	else
	{
		// Zoom in/out horizontally.
		const double Delta = MouseEvent.GetWheelDelta();
		MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		if (Viewport.RelativeZoomWithFixedX(Delta, MousePosition.X))
		{
			//Viewport.EnforceHorizontalScrollLimits(1.0);
			UpdateHorizontalScrollBar();
			bIsViewportDirty = true;
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
		if (MarkersTrack.IsVisible())
		{
			if (!MarkersTrack.IsBookmarksTrack())
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
		if (MarkersTrack.IsVisible())
		{
			if (MarkersTrack.IsBookmarksTrack())
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
		Layout.bIsCompactMode = !Layout.bIsCompactMode;
		bAreTimingEventsTracksDirty = true;
		return FReply::Handled();
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
			//Viewport.EnforceHorizontalScrollLimits(1.0);
			UpdateHorizontalScrollBar();
			bIsViewportDirty = true;
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
			//Viewport.EnforceHorizontalScrollLimits(1.0);
			UpdateHorizontalScrollBar();
			bIsViewportDirty = true;
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Left)
	{
		if (InKeyEvent.GetModifierKeys().IsControlDown())
		{
			// Scroll Left
			const double DT = Viewport.GetDuration();
			//ScrollAtTime(Viewport.GetStartTime() - DT * 0.05);
			if (Viewport.ScrollAtTime(Viewport.GetStartTime() - DT * 0.05))
			{
				//Viewport.EnforceHorizontalScrollLimits(1.0);
				UpdateHorizontalScrollBar();
				bIsViewportDirty = true;
			}
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
			const double DT = Viewport.GetDuration();
			//ScrollAtTime(Viewport.GetStartTime() + DT * 0.05);
			if (Viewport.ScrollAtTime(Viewport.GetStartTime() + DT * 0.05))
			{
				//Viewport.EnforceHorizontalScrollLimits(1.0);
				UpdateHorizontalScrollBar();
				bIsViewportDirty = true;
			}
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
			ScrollAtPosY(Viewport.GetScrollPosY() - 16.0f * 3);
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
			ScrollAtPosY(Viewport.GetScrollPosY() + 16.0f * 3);
		}
		else
		{
			SelectDownTimingEvent();
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		if (SelectedTimingEvent.IsValid())
		{
			const double Duration = Viewport.RestrictDuration(SelectedTimingEvent.StartTime, SelectedTimingEvent.EndTime);
			SelectTimeInterval(SelectedTimingEvent.StartTime, Duration);
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
		bAreTimingEventsTracksDirty = true;
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::G) // debug: toggles Graph track on/off
	{
		ShowHideGraphTrack_Execute();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Y) // debug: toggles GPU track on/off
	{
		ShowHideAllGpuTracks_Execute();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::U) // debug: toggles CPU tracks on/off
	{
		ShowHideAllCpuTracks_Execute();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::L)  // debug: toggles Loading tracks on/off
	{
		ShowHideAllLoadingTracks_Execute();
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::I)  // debug: toggles IO tracks on/off
	{
		ShowHideAllIoTracks_Execute();
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
	else if (InKeyEvent.GetKey() == EKeys::Five)
	{
		LoadingSharedState->SetColorSchema(4);
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
	else
	{
		MenuBuilder.BeginSection(TEXT("Empty"));
		{
			struct FLocal
			{
				static bool ReturnFalse()
				{
					return false;
				}
			};

			FUIAction DummyUIAction;
			DummyUIAction.CanExecuteAction = FCanExecuteAction::CreateStatic(&FLocal::ReturnFalse);

			FText Title;
			if (HoveredTimingEvent.Track != nullptr)
			{
				if (HoveredTimingEvent.Track->GetType() == FName(TEXT("Thread")) &&
					((FThreadTimingTrack*)HoveredTimingEvent.Track)->GetGroupName() != nullptr)
				{
					Title = FText::Format(LOCTEXT("TrackTitleGroupFmt", "{0} (Group: {1})"), FText::FromString(HoveredTimingEvent.Track->GetName()), FText::FromString(((FThreadTimingTrack*)HoveredTimingEvent.Track)->GetGroupName()));
				}
				else
				{
					Title = FText::FromString(HoveredTimingEvent.Track->GetName());
				}
			}
			else
			{
				Title = LOCTEXT("ContextMenu_NA", "N/A");
			}

			MenuBuilder.AddMenuEntry
			(
				Title,
				LOCTEXT("ContextMenu_NA_Desc", "No actions available."),
				FSlateIcon(),
				DummyUIAction,
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
	FTimingViewCommands::Register();
	const FTimingViewCommands& Commands = FTimingViewCommands::Get();

	TSharedPtr<FUICommandList> CommandList = FTimingProfilerManager::Get()->GetCommandList();

	CommandList->MapAction(
		Commands.ShowAllGpuTracks,
		FExecuteAction::CreateSP(this, &STimingView::ShowHideAllGpuTracks_Execute),
		FCanExecuteAction(), //FCanExecuteAction::CreateLambda([] { return true; }),
		FIsActionChecked::CreateSP(this, &STimingView::ShowHideAllGpuTracks_IsChecked));

	CommandList->MapAction(
		Commands.ShowAllCpuTracks,
		FExecuteAction::CreateSP(this, &STimingView::ShowHideAllCpuTracks_Execute),
		FCanExecuteAction(), //FCanExecuteAction::CreateLambda([] { return true; }),
		FIsActionChecked::CreateSP(this, &STimingView::ShowHideAllCpuTracks_IsChecked));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::HorizontalScrollBar_OnUserScrolled(float ScrollOffset)
{
	if (Viewport.OnUserScrolled(HorizontalScrollBar, ScrollOffset))
	{
		bIsViewportDirty = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdateHorizontalScrollBar()
{
	Viewport.UpdateScrollBar(HorizontalScrollBar);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::VerticalScrollBar_OnUserScrolled(float ScrollOffset)
{
	if (Viewport.OnUserScrolledY(VerticalScrollBar, ScrollOffset))
	{
		bIsVerticalViewportDirty = true;
	}
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
		bIsVerticalViewportDirty = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ScrollAtTime(double StartTime)
{
	if (Viewport.ScrollAtTime(StartTime))
	{
		UpdateHorizontalScrollBar();
		bIsViewportDirty = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::CenterOnTimeInterval(double IntervalStartTime, double IntervalDuration)
{
	if (Viewport.CenterOnTimeInterval(IntervalStartTime, IntervalDuration))
	{
		Viewport.EnforceHorizontalScrollLimits(1.0);

		UpdateHorizontalScrollBar();
		bIsViewportDirty = true;
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

	//TODO: SelectionChangedEvent.Broadcast(SelectionStartTime, SelectionEndTime);
	UpdateAggregatedStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdateAggregatedStats()
{
	TSharedPtr<STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
	if (Wnd)
	{
		TSharedPtr<STimersView> TimersView = Wnd->GetTimersView();
		if (TimersView)
		{
			TimersView->UpdateStats(SelectionStartTime, SelectionEndTime);
		}
		TSharedPtr<SStatsView> StatsView = Wnd->GetStatsView();
		if (StatsView)
		{
			StatsView->UpdateStats(SelectionStartTime, SelectionEndTime);
		}
	}

	if (bAssetLoadingMode)
	{
		TSharedPtr<Insights::STableTreeView> EventAggregationTreeView;
		TSharedPtr<Insights::STableTreeView> ObjectTypeAggregationTreeView;
		TSharedPtr<SLoadingProfilerWindow> LoadingProfilerWnd = FLoadingProfilerManager::Get()->GetProfilerWindow();
		if (LoadingProfilerWnd)
		{
			EventAggregationTreeView = LoadingProfilerWnd->GetEventAggregationTreeView();
			ObjectTypeAggregationTreeView = LoadingProfilerWnd->GetObjectTypeAggregationTreeView();
		}

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && Trace::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			auto ReadTable = [](Trace::ITable<Trace::FLoadTimeProfilerAggregatedStats>* Table, TArray<FAssetLoadingEventAggregationRow>& Aggregation, int32& TotalRowCount)
			{
				TotalRowCount = Table->GetRowCount();

				constexpr int32 MaxRowCount = 10; // only get first 10 records
				const int32 RowCount = FMath::Min(TotalRowCount, MaxRowCount);

				const Trace::ITableLayout& TableLayout = Table->GetLayout();
				const int32 ColumnCount = TableLayout.GetColumnCount();

				Trace::ITableReader<Trace::FLoadTimeProfilerAggregatedStats>* Reader = Table->CreateReader();

				//////////////////////////////////////////////////

				struct FSortedIndexEntry
				{
					int32 RowIndex;
					double Value;
				};

				TArray<FSortedIndexEntry> SortedIndex;
				SortedIndex.Reserve(TotalRowCount);

				constexpr int32 TotalTimeColumnIndex = 2;
				ensure(TableLayout.GetColumnType(TotalTimeColumnIndex) == Trace::ETableColumnType::TableColumnType_Double);

				for (int32 RowIndex = 0; RowIndex < TotalRowCount; ++RowIndex)
				{
					Reader->SetRowIndex(RowIndex);
					double Value = Reader->GetValueDouble(TotalTimeColumnIndex);
					SortedIndex.Add({ RowIndex, Value });
				}

				SortedIndex.Sort([](const FSortedIndexEntry& A, const FSortedIndexEntry& B) { return A.Value > B.Value; });

				//////////////////////////////////////////////////

				Aggregation.Reset();
				Aggregation.AddDefaulted(RowCount);

				for (int32 Index = 0; Index < RowCount; ++Index)
				{
					FAssetLoadingEventAggregationRow& Row = Aggregation[Index];

					int32 RowIndex = SortedIndex[Index].RowIndex;
					Reader->SetRowIndex(RowIndex);

					ensure(ColumnCount == 7);
					for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
					{
						ensure(TableLayout.GetColumnType(0) == Trace::ETableColumnType::TableColumnType_CString);
						Row.Name = Reader->GetValueCString(0);

						ensure(TableLayout.GetColumnType(1) == Trace::ETableColumnType::TableColumnType_Int);
						Row.Count = Reader->GetValueInt(1);

						ensure(TableLayout.GetColumnType(2) == Trace::ETableColumnType::TableColumnType_Double);
						Row.Total = Reader->GetValueDouble(2);

						ensure(TableLayout.GetColumnType(3) == Trace::ETableColumnType::TableColumnType_Double);
						Row.Min = Reader->GetValueDouble(3);

						ensure(TableLayout.GetColumnType(4) == Trace::ETableColumnType::TableColumnType_Double);
						Row.Max = Reader->GetValueDouble(4);

						ensure(TableLayout.GetColumnType(5) == Trace::ETableColumnType::TableColumnType_Double);
						Row.Avg = Reader->GetValueDouble(5);

						ensure(TableLayout.GetColumnType(6) == Trace::ETableColumnType::TableColumnType_Double);
						Row.Med = Reader->GetValueDouble(6);
					}
				}

				delete Reader;
			};

			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *Trace::ReadLoadTimeProfilerProvider(*Session.Get());

			{
				//TODO: LoadTimeProfilerProvider.UpdateEventAggregation(EventAggregationTreeView->GetTable(), SelectionStartTime, SelectionEndTime)
				Trace::ITable<Trace::FLoadTimeProfilerAggregatedStats>* EventAggregationTable = LoadTimeProfilerProvider.CreateEventAggregation(SelectionStartTime, SelectionEndTime);
				EventAggregationTreeView->GetTable()->UpdateSourceTable(MakeShareable(EventAggregationTable));
				EventAggregationTreeView->RebuildTree(true);
				ReadTable(EventAggregationTable, EventAggregation, EventAggregationTotalCount);
				//delete EventAggregationTable;
			}

			{
				//TODO: LoadTimeProfilerProvider.UpdateObjectTypeAggregation(EventAggregationTreeView->GetTable(), SelectionStartTime, SelectionEndTime)
				Trace::ITable<Trace::FLoadTimeProfilerAggregatedStats>* ObjectTypeAggregationTable = LoadTimeProfilerProvider.CreateObjectTypeAggregation(SelectionStartTime, SelectionEndTime);
				ObjectTypeAggregationTreeView->GetTable()->UpdateSourceTable(MakeShareable(ObjectTypeAggregationTable));
				ObjectTypeAggregationTreeView->RebuildTree(true);
				ReadTable(ObjectTypeAggregationTable, ObjectTypeAggregation, ObjectTypeAggregationTotalCount);
				//delete ObjectTypeAggregationTable;
			}
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
	if (MarkersTrack.IsVisible() != bIsMarkersTrackVisible)
	{
		MarkersTrack.SetVisibilityFlag(bIsMarkersTrackVisible);

		if (MarkersTrack.IsVisible())
		{
			if (Viewport.GetScrollPosY() != 0.0f)
			{
				Viewport.SetScrollPosY(Viewport.GetScrollPosY() + MarkersTrack.GetHeight());
			}

			MarkersTrack.SetDirtyFlag();
		}
		else
		{
			Viewport.SetScrollPosY(Viewport.GetScrollPosY() - MarkersTrack.GetHeight());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SetDrawOnlyBookmarks(bool bIsBookmarksTrack)
{
	if (MarkersTrack.IsBookmarksTrack() != bIsBookmarksTrack)
	{
		const float PrevHeight = MarkersTrack.GetHeight();
		MarkersTrack.SetBookmarksTrackFlag(bIsBookmarksTrack);

		if (MarkersTrack.IsVisible())
		{
			if (Viewport.GetScrollPosY() != 0.0f)
			{
				Viewport.SetScrollPosY(Viewport.GetScrollPosY() + MarkersTrack.GetHeight() - PrevHeight);
			}

			MarkersTrack.SetDirtyFlag();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::UpdateHoveredTimingEvent(float MX, float MY)
{
	HoveredTimingEvent.Track = nullptr;
	HoveredTimingEvent.TypeId = FTimerNode::InvalidId;

	if (MY >= Viewport.GetTopOffset() && MY < Viewport.GetHeight())
	{
		for (const auto& KV : CachedTimelines)
		{
			const FTimingEventsTrack& Track = *KV.Value;
			if (Track.IsVisible())
			{
				const float Y = Viewport.GetTopOffset() + Track.GetPosY() - Viewport.GetScrollPosY();
				if (MY >= Y && MY < Y + Track.GetHeight())
				{
					HoveredTimingEvent.Track = &Track;
					break;
				}
			}
		}

		if (HoveredTimingEvent.Track)
		{
			const float Y0 = Viewport.GetTopOffset() + HoveredTimingEvent.Track->GetPosY() - Viewport.GetScrollPosY() + 1.0f + Layout.TimelineDY;

			// If mouse is not above first sub-track or below last sub-track...
			if (MY >= Y0 && MY < Y0 + HoveredTimingEvent.Track->GetHeight() + Layout.TimelineDY)
			{
				int32 Depth = (MY - Y0) / (Layout.EventDY + Layout.EventH);
				float EventMY = (MY - Y0) - Depth * (Layout.EventDY + Layout.EventH);

				const double StartTime = Viewport.SlateUnitsToTime(MX);
				const double EndTime = StartTime + 2.0 / Viewport.GetScaleX(); // +2px
				constexpr bool bStopAtFirstMatch = true; // get first one matching
				constexpr bool bSearchForLargestEvent = false;
				HoveredTimingEvent.Track->SearchTimingEvent(StartTime, EndTime,
					[Depth](double, double, uint32 EventDepth)
					{
						return EventDepth == Depth;
					},
					HoveredTimingEvent, bStopAtFirstMatch, bSearchForLargestEvent);

				//TODO: ComputeSingleTimingEventStats(HoveredTimingEvent) --> compute ExclusiveTime
			}
		}
	}

	if (HoveredTimingEvent.IsValid())
	{
		HoveredTimingEvent.Track->InitTooltip(Tooltip, HoveredTimingEvent);
		Tooltip.SetDesiredOpacity(1.0f);
	}
	else
	{
		Tooltip.SetDesiredOpacity(0.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::OnSelectedTimingEventChanged()
{
	// Select the timer node coresponding to timing event type of selected timing event.
	if (!bAssetLoadingMode &&
		SelectedTimingEvent.IsValid() &&
		(SelectedTimingEvent.Track->GetType() == FName(TEXT("Thread"))))
	{
		//TODO: make this more generic
		TSharedPtr<STimingProfilerWindow> Wnd = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Wnd)
		{
			TSharedPtr<STimersView> TimersView = Wnd->GetTimersView();
			if (TimersView)
			{
				TimersView->SelectTimerNode(SelectedTimingEvent.TypeId);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectHoveredTimingEvent()
{
	SelectedTimingEvent = HoveredTimingEvent;
	if (SelectedTimingEvent.IsValid())
	{
		LastSelectionType = ESelectionType::TimingEvent;
		BringIntoView(SelectedTimingEvent.StartTime, SelectedTimingEvent.EndTime);
	}
	OnSelectedTimingEventChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectLeftTimingEvent()
{
	if (SelectedTimingEvent.IsValid())
	{
		const uint32 Depth = SelectedTimingEvent.Depth;
		const double StartTime = SelectedTimingEvent.StartTime;
		const double EndTime = SelectedTimingEvent.EndTime;
		const bool bStopAtFirstMatch = false; // get last one matching
		const bool bSearchForLargestEvent = false;
		if (SelectedTimingEvent.Track->SearchTimingEvent(0.0, StartTime,
			[Depth, StartTime, EndTime](double EventStartTime, double EventEndTime, uint32 EventDepth)
			{
				return EventDepth == Depth &&
					(EventStartTime < StartTime || EventEndTime < EndTime);
			},
			SelectedTimingEvent, bStopAtFirstMatch, bSearchForLargestEvent))
		{
			BringIntoView(SelectedTimingEvent.StartTime, SelectedTimingEvent.EndTime);
			OnSelectedTimingEventChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectRightTimingEvent()
{
	if (SelectedTimingEvent.IsValid())
	{
		const uint32 Depth = SelectedTimingEvent.Depth;
		const double StartTime = SelectedTimingEvent.StartTime;
		const double EndTime = SelectedTimingEvent.EndTime;
		const bool bStopAtFirstMatch = true; // get first one matching
		const bool bSearchForLargestEvent = false;
		if (SelectedTimingEvent.Track->SearchTimingEvent(EndTime, Viewport.GetMaxValidTime(),
			[Depth, StartTime, EndTime](double EventStartTime, double EventEndTime, uint32 EventDepth)
			{
				return EventDepth == Depth &&
					(EventStartTime > StartTime || EventEndTime > EndTime);
			},
			SelectedTimingEvent, bStopAtFirstMatch, bSearchForLargestEvent))
		{
			BringIntoView(SelectedTimingEvent.StartTime, SelectedTimingEvent.EndTime);
			OnSelectedTimingEventChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectUpTimingEvent()
{
	if (SelectedTimingEvent.IsValid() &&
		SelectedTimingEvent.Depth > 0)
	{
		const uint32 Depth = SelectedTimingEvent.Depth - 1;
		double StartTime = SelectedTimingEvent.StartTime;
		double EndTime = SelectedTimingEvent.EndTime;
		const bool bStopAtFirstMatch = true; // get first one matching
		const bool bSearchForLargestEvent = false;
		if (SelectedTimingEvent.Track->SearchTimingEvent(StartTime, EndTime,
			[Depth, StartTime, EndTime](double EventStartTime, double EventEndTime, uint32 EventDepth)
			{
				return EventDepth == Depth
					&& EventStartTime <= EndTime
					&& EventEndTime >= StartTime;
			},
			SelectedTimingEvent, bStopAtFirstMatch, bSearchForLargestEvent))
		{
			BringIntoView(SelectedTimingEvent.StartTime, SelectedTimingEvent.EndTime);
			OnSelectedTimingEventChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::SelectDownTimingEvent()
{
	if (SelectedTimingEvent.IsValid())
	{
		const uint32 Depth = SelectedTimingEvent.Depth + 1;
		double StartTime = SelectedTimingEvent.StartTime;
		double EndTime = SelectedTimingEvent.EndTime;
		const bool bStopAtFirstMatch = false; // check all timing events
		const bool bSearchForLargestEvent = true; // get largest timing event
		if (SelectedTimingEvent.Track->SearchTimingEvent(StartTime, EndTime,
			[Depth, StartTime, EndTime](double EventStartTime, double EventEndTime, uint32 EventDepth)
			{
				return EventDepth == Depth
					&& EventStartTime <= EndTime
					&& EventEndTime >= StartTime;
			},
			SelectedTimingEvent, bStopAtFirstMatch, bSearchForLargestEvent))
		{
			BringIntoView(SelectedTimingEvent.StartTime, SelectedTimingEvent.EndTime);
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
		if (SelectedTimingEvent.IsValid())
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
		if (SelectedTimingEvent.IsValid())
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
		StartTime = SelectedTimingEvent.StartTime;
		EndTime = Viewport.RestrictEndTime(SelectedTimingEvent.EndTime);
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
			bIsViewportDirty = true;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> STimingView::MakeTracksFilterMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.BeginSection("QuickFilter", LOCTEXT("TracksFilterHeading", "Quick Filter"));
	{
		const FTimingViewCommands& Commands = FTimingViewCommands::Get();

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

		//TODO: MenuBuilder.AddMenuEntry(Commands.ShowAllGpuTracks);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllGpuTracks", "GPU Track - Y"),
			LOCTEXT("ShowAllGpuTracks_Tooltip", "Show/hide the GPU track"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &STimingView::ShowHideAllGpuTracks_Execute),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &STimingView::ShowHideAllGpuTracks_IsChecked)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		//TODO: MenuBuilder.AddMenuEntry(Commands.ShowAllCpuTracks);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllCpuTracks", "CPU Thread Tracks - U"),
			LOCTEXT("ShowAllCpuTracks_Tooltip", "Show/hide all CPU tracks (and all CPU thread groups)"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &STimingView::ShowHideAllCpuTracks_Execute),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &STimingView::ShowHideAllCpuTracks_IsChecked)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		//TODO: MenuBuilder.AddMenuEntry(Commands.ShowAllLoadingTracks);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllLoadingTracks", "Asset Loading Tracks - L"),
			LOCTEXT("ShowAllLoadingTracks_Tooltip", "Show/hide the Asset Loading tracks"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &STimingView::ShowHideAllLoadingTracks_Execute),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &STimingView::ShowHideAllLoadingTracks_IsChecked)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		//TODO: MenuBuilder.AddMenuEntry(Commands.ShowAllIoTracks);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllIoTracks", "I/O Tracks - I"),
			LOCTEXT("ShowAllIoTracks_Tooltip", "Show/hide the I/O (File Activity) tracks"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &STimingView::ShowHideAllIoTracks_Execute),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &STimingView::ShowHideAllIoTracks_IsChecked)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

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

	MenuBuilder.BeginSection("ThreadGroups", LOCTEXT("ThreadGroupsHeading", "CPU Thread Groups"));
	CreateThreadGroupsMenu(MenuBuilder);
	MenuBuilder.EndSection();

	//MenuBuilder.BeginSection("Tracks", LOCTEXT("TracksHeading", "Tracks"));
	//CreateTracksMenu(MenuBuilder);
	//MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::CreateThreadGroupsMenu(FMenuBuilder& MenuBuilder)
{
	// Sort the list of thread groups.
	TArray<const FThreadGroup*> SortedThreadGroups;
	SortedThreadGroups.Reserve(ThreadGroups.Num());
	for (const auto& KV : ThreadGroups)
	{
		SortedThreadGroups.Add(&KV.Value);
	}
	Algo::SortBy(SortedThreadGroups, &FThreadGroup::GetOrder);

	for (const FThreadGroup* ThreadGroupPtr : SortedThreadGroups)
	{
		const FThreadGroup& ThreadGroup = *ThreadGroupPtr;
		if (ThreadGroup.NumTimelines > 0)
		{
			MenuBuilder.AddMenuEntry(
				//FText::FromString(ThreadGroup.Name),
				FText::Format(LOCTEXT("ThreadGroupFmt", "{0} ({1})"), FText::FromString(ThreadGroup.Name), ThreadGroup.NumTimelines),
				TAttribute<FText>(), // no tooltip
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &STimingView::ToggleTrackVisibilityByGroup_Execute, ThreadGroup.Name),
					FCanExecuteAction::CreateLambda([] { return true; }),
					FIsActionChecked::CreateSP(this, &STimingView::ToggleTrackVisibilityByGroup_IsChecked, ThreadGroup.Name)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::CreateTracksMenu(FMenuBuilder& MenuBuilder)
{
	for (int32 TrackIndex = 0; TrackIndex < TimingEventsTracks.Num(); ++TrackIndex)
	{
		const FTimingEventsTrack& Track = *TimingEventsTracks[TrackIndex];
		if (Track.GetHeight() > 0.0f || Layout.TargetMinTimelineH > 0.0f)
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
	bAreTimingEventsTracksDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::ShowHideAllCpuTracks_IsChecked() const
{
	return bShowHideAllCpuTracks;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ShowHideAllCpuTracks_Execute()
{
	bShowHideAllCpuTracks = !bShowHideAllCpuTracks;

	for (int32 TrackIndex = 0; TrackIndex < TimingEventsTracks.Num(); ++TrackIndex)
	{
		FTimingEventsTrack& Track = *TimingEventsTracks[TrackIndex];
		if (Track.GetType() == FName(TEXT("Thread")) && Track.GetSubType() == FName(TEXT("CPU")))
		{
			Track.SetVisibilityFlag(bShowHideAllCpuTracks);
		}
	}

	for (auto& KV: ThreadGroups)
	{
		KV.Value.bIsVisible = bShowHideAllCpuTracks;
	}

	HoveredTimingEvent.Reset();
	SelectedTimingEvent.Reset();
	Tooltip.SetDesiredOpacity(0.0f);

	bAreTimingEventsTracksDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::ShowHideAllGpuTracks_IsChecked() const
{
	return bShowHideAllGpuTracks;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ShowHideAllGpuTracks_Execute()
{
	bShowHideAllGpuTracks = !bShowHideAllGpuTracks;

	for (int32 TrackIndex = 0; TrackIndex < TimingEventsTracks.Num(); ++TrackIndex)
	{
		FTimingEventsTrack& Track = *TimingEventsTracks[TrackIndex];
		if (Track.GetType() == FName(TEXT("Thread")) && Track.GetSubType() == FName(TEXT("GPU")))
		{
			Track.SetVisibilityFlag(bShowHideAllGpuTracks);
		}
	}

	HoveredTimingEvent.Reset();
	SelectedTimingEvent.Reset();
	Tooltip.SetDesiredOpacity(0.0f);

	bAreTimingEventsTracksDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::ShowHideAllLoadingTracks_IsChecked() const
{
	return bShowHideAllLoadingTracks;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ShowHideAllLoadingTracks_Execute()
{
	bShowHideAllLoadingTracks = !bShowHideAllLoadingTracks;

	for (int32 TrackIndex = 0; TrackIndex < TimingEventsTracks.Num(); ++TrackIndex)
	{
		FTimingEventsTrack& Track = *TimingEventsTracks[TrackIndex];
		if (Track.GetType() == FName(TEXT("Loading")))
		{
			Track.SetVisibilityFlag(bShowHideAllLoadingTracks);
		}
	}

	bAreTimingEventsTracksDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::ShowHideAllIoTracks_IsChecked() const
{
	return bShowHideAllIoTracks;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ShowHideAllIoTracks_Execute()
{
	bShowHideAllIoTracks = !bShowHideAllIoTracks;

	for (int32 TrackIndex = 0; TrackIndex < TimingEventsTracks.Num(); ++TrackIndex)
	{
		FTimingEventsTrack& Track = *TimingEventsTracks[TrackIndex];
		if (Track.GetType() == FName(TEXT("FileActivity")))
		{
			Track.SetVisibilityFlag(bShowHideAllIoTracks);
		}
	}

	bAreTimingEventsTracksDirty = true;

	if (bShowHideAllIoTracks)
	{
		FileActivitySharedState->RequestUpdate();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::IsAutoHideEmptyTracksEnabled() const
{
	return (Layout.TargetMinTimelineH == 0.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ToggleAutoHideEmptyTracks()
{
	if (Layout.TargetMinTimelineH == 0.0f)
	{
		Layout.TargetMinTimelineH = FTimingEventsTrackLayout::RealMinTimelineH;
	}
	else
	{
		Layout.TargetMinTimelineH = 0.0f;
	}

	Layout.MinTimelineH = Layout.TargetMinTimelineH; // no layout animation

	for (auto& CachedTimelineKV : CachedTimelines)
	{
		CachedTimelineKV.Value->SetHeight(0.0f);
	}

	ScrollAtPosY(0.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::ToggleTrackVisibility_IsChecked(uint64 InTrackId) const
{
	if (CachedTimelines.Contains(InTrackId))
	{
		const FTimingEventsTrack* Track = CachedTimelines[InTrackId];
		return Track->IsVisible();
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ToggleTrackVisibility_Execute(uint64 InTrackId)
{
	if (CachedTimelines.Contains(InTrackId))
	{
		FTimingEventsTrack* Track = CachedTimelines[InTrackId];
		Track->ToggleVisibility();

		HoveredTimingEvent.Reset();
		SelectedTimingEvent.Reset();
		Tooltip.SetDesiredOpacity(0.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool STimingView::ToggleTrackVisibilityByGroup_IsChecked(const TCHAR* InGroupName) const
{
	if (ThreadGroups.Contains(InGroupName))
	{
		const FThreadGroup& ThreadGroup = ThreadGroups[InGroupName];
		return ThreadGroup.bIsVisible;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void STimingView::ToggleTrackVisibilityByGroup_Execute(const TCHAR* InGroupName)
{
	if (ThreadGroups.Contains(InGroupName))
	{
		FThreadGroup& ThreadGroup = ThreadGroups[InGroupName];
		ThreadGroup.bIsVisible = !ThreadGroup.bIsVisible;

		for (auto& KV : CachedTimelines)
		{
			FTimingEventsTrack& Track = *KV.Value;
			if (Track.GetType() == FName(TEXT("Thread")) && Track.GetSubType() == FName(TEXT("CPU")) &&
				((FCpuTimingTrack*)&Track)->GetGroupName() == InGroupName)
			{
				Track.SetVisibilityFlag(ThreadGroup.bIsVisible);
			}
		}

		HoveredTimingEvent.Reset();
		SelectedTimingEvent.Reset();
		Tooltip.SetDesiredOpacity(0.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
