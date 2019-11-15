// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ThreadTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/SlateBrush.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TimerNode.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "ThreadTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FThreadTimingSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FCpuTimingTrack> FThreadTimingSharedState::GetCpuTrack(uint32 InThreadId)
{
	TSharedPtr<FCpuTimingTrack>*const TrackPtrPtr = CpuTracks.Find(InThreadId);
	return TrackPtrPtr ? *TrackPtrPtr : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::IsGpuTrackVisible() const
{
	return GpuTrack != nullptr && GpuTrack->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::IsCpuTrackVisible(uint32 InThreadId) const
{
	const TSharedPtr<FCpuTimingTrack>*const TrackPtrPtr = CpuTracks.Find(InThreadId);
	return TrackPtrPtr && (*TrackPtrPtr)->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (TimingView && TimingView->IsAssetLoadingModeEnabled())
	{
		bShowHideAllGpuTracks = false;
		bShowHideAllCpuTracks = false;
	}
	else
	{
		bShowHideAllGpuTracks = true;
		bShowHideAllCpuTracks = true;
	}

	GpuTrack = nullptr;
	CpuTracks.Reset();
	ThreadGroups.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	bShowHideAllGpuTracks = false;
	bShowHideAllCpuTracks = false;

	GpuTrack = nullptr;
	CpuTracks.Reset();
	ThreadGroups.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	const Trace::ITimingProfilerProvider* TimingProfilerProvider = Trace::ReadTimingProfilerProvider(InAnalysisSession);
	if (TimingProfilerProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);

		const Trace::ILoadTimeProfilerProvider* LoadTimeProfilerProvider = Trace::ReadLoadTimeProfilerProvider(InAnalysisSession);

		// Check if we have a GPU track.
		if (!GpuTrack.IsValid())
		{
			uint32 GpuTimelineIndex;
			if (TimingProfilerProvider->GetGpuTimelineIndex(GpuTimelineIndex))
			{
				GpuTrack = MakeShared<FGpuTimingTrack>(*this, TEXT("GPU"), nullptr, GpuTimelineIndex, 0);
				GpuTrack->SetOrder(1000);
				GpuTrack->SetVisibilityFlag(bShowHideAllGpuTracks);
				InSession.AddScrollableTrack(GpuTrack);
			}
		}

		bool bTracksOrderChanged = false;
		int32 Order = 2000;

		// Iterate through threads.
		const Trace::IThreadProvider& ThreadProvider = Trace::ReadThreadProvider(InAnalysisSession);
		ThreadProvider.EnumerateThreads([this, &InSession, &bTracksOrderChanged, &Order, TimingProfilerProvider, LoadTimeProfilerProvider](const Trace::FThreadInfo& ThreadInfo)
		{
			// Check if this thread is part of a group?
			bool bIsGroupVisible = bShowHideAllCpuTracks;
			const TCHAR* const GroupName = ThreadInfo.GroupName ? ThreadInfo.GroupName : ThreadInfo.Name;
			if (GroupName != nullptr)
			{
				if (!ThreadGroups.Contains(GroupName))
				{
					//UE_LOG(TimingProfiler, Log, TEXT("New CPU Thread Group (%d) : \"%s\""), ThreadGroups.Num() + 1, GroupName);
					ThreadGroups.Add(GroupName, { GroupName, bIsGroupVisible, 0, Order });
				}
				else
				{
					FThreadGroup& ThreadGroup = ThreadGroups[GroupName];
					bIsGroupVisible = ThreadGroup.bIsVisible;
					ThreadGroup.Order = Order;
				}
			}

			// Check if there is an available Asset Loading track for this thread.
			bool bIsLoadingThread = false;
			uint32 LoadingTimelineIndex;
			if (LoadTimeProfilerProvider && LoadTimeProfilerProvider->GetCpuThreadTimelineIndex(ThreadInfo.Id, LoadingTimelineIndex))
			{
				bIsLoadingThread = true;
			}

			// Check if there is an available CPU track for this thread.
			uint32 CpuTimelineIndex;
			if (TimingProfilerProvider->GetCpuThreadTimelineIndex(ThreadInfo.Id, CpuTimelineIndex))
			{
				TSharedPtr<FCpuTimingTrack> Track;

				if (!CpuTracks.Contains(ThreadInfo.Id))
				{
					FString TrackName(ThreadInfo.Name && *ThreadInfo.Name ? ThreadInfo.Name : FString::Printf(TEXT("Thread %u"), ThreadInfo.Id));

					// Create new Timing Events track for the CPU thread.
					Track = MakeShared<FCpuTimingTrack>(*this, TrackName, GroupName, CpuTimelineIndex, ThreadInfo.Id);
					Track->SetOrder(Order);
					CpuTracks.Add(ThreadInfo.Id, Track);

					FThreadGroup& ThreadGroup = ThreadGroups[GroupName];
					ThreadGroup.NumTimelines++;

					if (TimingView && TimingView->IsAssetLoadingModeEnabled() && bIsLoadingThread)
					{
						Track->SetVisibilityFlag(true);
						ThreadGroup.bIsVisible = true;
					}
					else
					{
						Track->SetVisibilityFlag(bIsGroupVisible);
					}

					InSession.AddScrollableTrack(Track);
				}
				else
				{
					Track = CpuTracks[ThreadInfo.Id];

					if (Track->GetOrder() != Order)
					{
						Track->SetOrder(Order);
						bTracksOrderChanged = true;
					}
				}
			}

			Order += 100;
		});

		if (bTracksOrderChanged)
		{
			InSession.InvalidateScrollableTracksOrder();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}

	InOutMenuBuilder.BeginSection("ThreadProfiler", LOCTEXT("ThreadProfilerHeading", "Threads"));
	{
		//TODO: MenuBuilder.AddMenuEntry(Commands.ShowAllGpuTracks);
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllGpuTracks", "GPU Track - Y"),
			LOCTEXT("ShowAllGpuTracks_Tooltip", "Show/hide the GPU track"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ShowHideAllGpuTracks_Execute),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::ShowHideAllGpuTracks_IsChecked)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		//TODO: MenuBuilder.AddMenuEntry(Commands.ShowAllCpuTracks);
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllCpuTracks", "CPU Thread Tracks - U"),
			LOCTEXT("ShowAllCpuTracks_Tooltip", "Show/hide all CPU tracks (and all CPU thread groups)"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ShowHideAllCpuTracks_Execute),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::ShowHideAllCpuTracks_IsChecked)),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	InOutMenuBuilder.EndSection();

	InOutMenuBuilder.BeginSection("ThreadGroups", LOCTEXT("ThreadGroupsHeading", "CPU Thread Groups"));
	CreateThreadGroupsMenu(InOutMenuBuilder);
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::CreateThreadGroupsMenu(FMenuBuilder& InOutMenuBuilder)
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
			InOutMenuBuilder.AddMenuEntry(
				//FText::FromString(ThreadGroup.Name),
				FText::Format(LOCTEXT("ThreadGroupFmt", "{0} ({1})"), FText::FromString(ThreadGroup.Name), ThreadGroup.NumTimelines),
				TAttribute<FText>(), // no tooltip
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ToggleTrackVisibilityByGroup_Execute, ThreadGroup.Name),
						  FCanExecuteAction::CreateLambda([] { return true; }),
						  FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::ToggleTrackVisibilityByGroup_IsChecked, ThreadGroup.Name)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::ShowHideAllCpuTracks_IsChecked() const
{
	return bShowHideAllCpuTracks;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ShowHideAllCpuTracks_Execute()
{
	bShowHideAllCpuTracks = !bShowHideAllCpuTracks;

	for (const auto& KV : CpuTracks)
	{
		FCpuTimingTrack& Track = *KV.Value;
		Track.SetVisibilityFlag(bShowHideAllCpuTracks);
	}

	for (auto& KV : ThreadGroups)
	{
		KV.Value.bIsVisible = bShowHideAllCpuTracks;
	}

	if (TimingView)
	{
		TimingView->OnTrackVisibilityChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::ShowHideAllGpuTracks_IsChecked() const
{
	return bShowHideAllGpuTracks;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ShowHideAllGpuTracks_Execute()
{
	bShowHideAllGpuTracks = !bShowHideAllGpuTracks;

	if (GpuTrack.IsValid())
	{
		GpuTrack->SetVisibilityFlag(bShowHideAllGpuTracks);

		if (TimingView)
		{
			TimingView->OnTrackVisibilityChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::ToggleTrackVisibilityByGroup_IsChecked(const TCHAR* InGroupName) const
{
	if (ThreadGroups.Contains(InGroupName))
	{
		const FThreadGroup& ThreadGroup = ThreadGroups[InGroupName];
		return ThreadGroup.bIsVisible;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ToggleTrackVisibilityByGroup_Execute(const TCHAR* InGroupName)
{
	if (ThreadGroups.Contains(InGroupName))
	{
		FThreadGroup& ThreadGroup = ThreadGroups[InGroupName];
		ThreadGroup.bIsVisible = !ThreadGroup.bIsVisible;

		for (const auto& KV : CpuTracks)
		{
			FCpuTimingTrack& Track = *KV.Value;
			if (Track.GetGroupName() == InGroupName)
			{
				Track.SetVisibilityFlag(ThreadGroup.bIsVisible);
			}
		}

		if (TimingView)
		{
			TimingView->OnTrackVisibilityChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FThreadTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		const FTimingTrackViewport& Viewport = Context.GetViewport();

		TimingProfilerProvider.ReadTimers([this, &Builder, &Viewport, &TimingProfilerProvider](const Trace::FTimingProfilerTimer* Timers, uint64 TimersCount)
		{
			TimingProfilerProvider.ReadTimeline(TimelineIndex, [this, &Builder, &Viewport, Timers](const Trace::ITimingProfilerProvider::Timeline& Timeline)
			{
				if (FTimingEventsTrack::bUseDownSampling)
				{
					const double SecondsPerPixel = 1.0 / Viewport.GetScaleX();
					Timeline.EnumerateEventsDownSampled(Viewport.GetStartTime(), Viewport.GetEndTime(), SecondsPerPixel,
						[this, &Builder, Timers](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
						{
							const uint64 Type = Timers[Event.TimerIndex].Id;
							const uint32 Color = 0;
							Builder.AddEvent(StartTime, EndTime, Depth, Timers[Event.TimerIndex].Name, Type, Color);
						});
				}
				else
				{
					Timeline.EnumerateEvents(Viewport.GetStartTime(), Viewport.GetEndTime(),
						[this, &Builder, Timers](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
						{
							const uint64 Type = Timers[Event.TimerIndex].Id;
							const uint32 Color = 0;
							Builder.AddEvent(StartTime, EndTime, Depth, Timers[Event.TimerIndex].Name, Type, Color);
						});
				}
			});
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::PostDraw(const ITimingTrackDrawContext& Context) const
{
	const TSharedPtr<const ITimingEvent> SelectedEventPtr = Context.GetSelectedEvent();
	if (SelectedEventPtr.IsValid() &&
		SelectedEventPtr->CheckTrack(this) &&
		FTimingEvent::CheckTypeName(*SelectedEventPtr))
	{
		const FTimingEvent& SelectedEvent = static_cast<const FTimingEvent&>(*SelectedEventPtr);
		const ITimingViewDrawHelper& Helper = Context.GetHelper();
		DrawSelectedEventInfo(SelectedEvent, Context.GetViewport(), Context.GetDrawContext(), Helper.GetWhiteBrush(), Helper.GetEventFont());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::DrawSelectedEventInfo(const FTimingEvent& SelectedEvent, const FTimingTrackViewport& Viewport, const FDrawContext& DrawContext, const FSlateBrush* WhiteBrush, const FSlateFontInfo& Font) const
{
	FindTimingProfilerEvent(SelectedEvent, [&SelectedEvent, &Font, &Viewport, &DrawContext, &WhiteBrush](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{
		const FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(InFoundEvent.TimerIndex);
		if (TimerNodePtr.IsValid())
		{
			FString Str = FString::Printf(TEXT("%s (Incl.: %s, Excl.: %s)"),
				TimerNodePtr ? *(TimerNodePtr->GetName().ToString()) : TEXT("N/A"),
				*TimeUtils::FormatTimeAuto(SelectedEvent.GetDuration()),
				*TimeUtils::FormatTimeAuto(SelectedEvent.GetExclusiveTime()));

			const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FVector2D Size = FontMeasureService->Measure(Str, Font);
			const float X = Viewport.GetWidth() - Size.X - 23.0f;
			const float Y = Viewport.GetHeight() - Size.Y - 18.0f;

			const FLinearColor BackgroundColor(0.05f, 0.05f, 0.05f, 1.0f);
			const FLinearColor TextColor(0.7f, 0.7f, 0.7f, 1.0f);

			DrawContext.DrawBox(X - 8.0f, Y - 2.0f, Size.X + 16.0f, Size.Y + 4.0f, WhiteBrush, BackgroundColor);
			DrawContext.LayerId++;

			DrawContext.DrawText(X, Y, Str, Font, TextColor);
			DrawContext.LayerId++;
		}
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	if (InTooltipEvent.CheckTrack(this) && FTimingEvent::CheckTypeName(InTooltipEvent))
	{
		const FTimingEvent& TooltipEvent = static_cast<const FTimingEvent&>(InTooltipEvent);

		FindTimingProfilerEvent(TooltipEvent, [this, &InOutTooltip, &TooltipEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
		{
			TSharedPtr<FTimingEvent> ParentTimingEvent;
			Trace::FTimingProfilerEvent ParentEvent;
			TSharedPtr<FTimingEvent> RootTimingEvent;
			Trace::FTimingProfilerEvent RootEvent;
			GetParentAndRoot(TooltipEvent, ParentTimingEvent, ParentEvent, RootTimingEvent, RootEvent);

			InOutTooltip.ResetContent();

			const FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(InFoundEvent.TimerIndex);
			FString TimerName = TimerNodePtr ? TimerNodePtr->GetName().ToString() : TEXT("N/A");
			InOutTooltip.AddTitle(TimerName);

			if (ParentTimingEvent.IsValid() && TooltipEvent.GetDepth() > 0)
			{
				const FTimerNodePtr ParentTimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(ParentEvent.TimerIndex);
				const FString ParentTimerName = ParentTimerNodePtr.IsValid() ? ParentTimerNodePtr->GetName().ToString() : TEXT("N/A");
				FNumberFormattingOptions FormattingOptions;
				FormattingOptions.MaximumFractionalDigits = 2;
				const FString ValueStr = FString::Printf(TEXT("%s %s"), *FText::AsPercent(TooltipEvent.GetDuration() / ParentTimingEvent->GetDuration(), &FormattingOptions).ToString(), *ParentTimerName);
				InOutTooltip.AddNameValueTextLine(TEXT("% of Parent:"), ValueStr);
			}

			if (RootTimingEvent.IsValid() && TooltipEvent.GetDepth() > 1)
			{
				const FTimerNodePtr RootTimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(RootEvent.TimerIndex);
				const FString RootTimerName = RootTimerNodePtr.IsValid() ? RootTimerNodePtr->GetName().ToString() : TEXT("N/A");
				FNumberFormattingOptions FormattingOptions;
				FormattingOptions.MaximumFractionalDigits = 2;
				const FString ValueStr = FString::Printf(TEXT("%s %s"), *FText::AsPercent(TooltipEvent.GetDuration() / RootTimingEvent->GetDuration(), &FormattingOptions).ToString(), *RootTimerName);
				InOutTooltip.AddNameValueTextLine(TEXT("% of Root:"), ValueStr);
			}

			InOutTooltip.AddNameValueTextLine(TEXT("Inclusive Time:"), TimeUtils::FormatTimeAuto(TooltipEvent.GetDuration()));

			{
				FNumberFormattingOptions FormattingOptions;
				FormattingOptions.MaximumFractionalDigits = 2;
				const FString ExclStr = FString::Printf(TEXT("%s (%s)"), *TimeUtils::FormatTimeAuto(TooltipEvent.GetExclusiveTime()), *FText::AsPercent(TooltipEvent.GetExclusiveTime() / TooltipEvent.GetDuration(), &FormattingOptions).ToString());
				InOutTooltip.AddNameValueTextLine(TEXT("Exclusive Time:"), ExclStr);
			}

			InOutTooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), TooltipEvent.GetDepth()));

			InOutTooltip.UpdateLayout();
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::GetParentAndRoot(const FTimingEvent& TimingEvent, TSharedPtr<FTimingEvent>& OutParentTimingEvent, Trace::FTimingProfilerEvent& OutParentEvent, TSharedPtr<FTimingEvent>& OutRootTimingEvent, Trace::FTimingProfilerEvent& OutRootEvent) const
{
	// Note: This function does not compute Exclusive Time for parent and root events.

	if (TimingEvent.GetDepth() > 0)
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			if (Trace::ReadTimingProfilerProvider(*Session.Get()))
			{
				const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

				TimingProfilerProvider.ReadTimeline(GetTimelineIndex(), [&TimingEvent, &OutParentTimingEvent, &OutParentEvent, &OutRootTimingEvent, &OutRootEvent](const Trace::ITimingProfilerProvider::Timeline& Timeline)
				{
					Timeline.EnumerateEvents(TimingEvent.GetStartTime(), TimingEvent.GetEndTime(), [&TimingEvent, &OutParentTimingEvent, &OutParentEvent, &OutRootTimingEvent, &OutRootEvent](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
					{
						if (EventDepth == 0)
						{
							OutRootEvent = Event;
							OutRootTimingEvent = MakeShared<FTimingEvent>(TimingEvent.GetTrack(), EventStartTime, EventEndTime, EventDepth);
						}
						if (EventDepth == TimingEvent.GetDepth() - 1)
						{
							OutParentEvent = Event;
							OutParentTimingEvent = MakeShared<FTimingEvent>(TimingEvent.GetTrack(), EventStartTime, EventEndTime, EventDepth);
						}
					});
				});
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FThreadTimingTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindTimingProfilerEvent(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{
		FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::UpdateEventStats(ITimingEvent& InOutEvent) const
{
	if (InOutEvent.CheckTrack(this) && FTimingEvent::CheckTypeName(InOutEvent))
	{
		FTimingEvent& TrackEvent = static_cast<FTimingEvent&>(InOutEvent);

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			if (Trace::ReadTimingProfilerProvider(*Session.Get()))
			{
				const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

				// Compute Exclusive Time.
				TimingProfilerProvider.ReadTimeline(GetTimelineIndex(), [&TrackEvent](const Trace::ITimingProfilerProvider::Timeline& Timeline)
				{
					struct FEnumerationState
					{
						double EventStartTime;
						double EventEndTime;
						uint64 EventDepth;

						uint64 CurrentDepth;
						double LastTime;
						double ExclusiveTime;
						bool IsInEventScope;
					};
					FEnumerationState State;

					State.EventStartTime = TrackEvent.GetStartTime();
					State.EventEndTime = TrackEvent.GetEndTime();
					State.EventDepth = TrackEvent.GetDepth();

					State.CurrentDepth = 0;
					State.LastTime = 0.0;
					State.ExclusiveTime = 0.0;
					State.IsInEventScope = false;

					Timeline.EnumerateEvents(TrackEvent.GetStartTime(), TrackEvent.GetEndTime(), [&State](bool IsEnter, double Time, const Trace::FTimingProfilerEvent& Event)
					{
						if (IsEnter)
						{
							if (State.IsInEventScope && State.CurrentDepth == State.EventDepth + 1)
							{
								State.ExclusiveTime += Time - State.LastTime;
							}
							if (State.CurrentDepth == State.EventDepth && Time == State.EventStartTime)
							{
								State.IsInEventScope = true;
							}
							++State.CurrentDepth;
						}
						else
						{
							--State.CurrentDepth;
							if (State.CurrentDepth == State.EventDepth && Time == State.EventEndTime)
							{
								State.IsInEventScope = false;
								State.ExclusiveTime += Time - State.LastTime;
							}
						}
						State.LastTime = Time;
					});

					TrackEvent.SetExclusiveTime(State.ExclusiveTime);
				});
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::OnEventSelected(const ITimingEvent& InSelectedEvent) const
{
	if (InSelectedEvent.CheckTrack(this) && FTimingEvent::CheckTypeName(InSelectedEvent))
	{
		const FTimingEvent& TrackEvent = static_cast<const FTimingEvent&>(InSelectedEvent);

		FindTimingProfilerEvent(TrackEvent, [](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
		{
			// Select the timer node corresponding to timing event type of selected timing event.
			FTimingProfilerManager::Get()->SetSelectedTimer(InFoundEvent.TimerIndex);
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::OnClipboardCopyEvent(const ITimingEvent& InSelectedEvent) const
{
	if (InSelectedEvent.CheckTrack(this) && FTimingEvent::CheckTypeName(InSelectedEvent))
	{
		const FTimingEvent& TrackEvent = static_cast<const FTimingEvent&>(InSelectedEvent);

		FindTimingProfilerEvent(TrackEvent, [](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
		{
			const FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(InFoundEvent.TimerIndex);
			if (TimerNodePtr.IsValid())
			{
				// Copy name of selected timing event to clipboard.
				FPlatformApplicationMisc::ClipboardCopy(*TimerNodePtr->GetName().ToString());
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	FText SectionTitle;
	if (GetGroupName() != nullptr)
	{
		SectionTitle = FText::Format(LOCTEXT("TrackTitleGroupFmt", "{0} (Group: {1})"), FText::FromString(GetName()), FText::FromString(GetGroupName()));
	}
	else
	{
		SectionTitle = FText::FromString(GetName());
	}

	MenuBuilder.BeginSection(TEXT("Empty"), SectionTitle);
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

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingTrack::FindTimingProfilerEvent(const FTimingEvent& InTimingEvent, TFunctionRef<void(double, double, uint32, const Trace::FTimingProfilerEvent&)> InFoundPredicate) const
{
	auto MatchEvent = [&InTimingEvent](double InStartTime, double InEndTime, uint32 InDepth)
	{ 
		return InDepth == InTimingEvent.GetDepth()
			&& InStartTime == InTimingEvent.GetStartTime()
			&& InEndTime == InTimingEvent.GetEndTime();
	};

	FTimingEventSearchParameters SearchParameters(InTimingEvent.GetStartTime(), InTimingEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch, MatchEvent);
	SearchParameters.SearchHandle = &InTimingEvent.GetSearchHandle();
	return FindTimingProfilerEvent(SearchParameters, InFoundPredicate);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingTrack::FindTimingProfilerEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const Trace::FTimingProfilerEvent&)> InFoundPredicate) const
{
	return TTimingEventSearch<Trace::FTimingProfilerEvent>::Search(
		InParameters,

		[this](TTimingEventSearch<Trace::FTimingProfilerEvent>::FContext& InContext)
		{
			TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			if (Session.IsValid())
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

				if (Trace::ReadTimingProfilerProvider(*Session.Get()))
				{
					const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

					TimingProfilerProvider.ReadTimeline(GetTimelineIndex(), [&InContext](const Trace::ITimingProfilerProvider::Timeline& Timeline)
					{
						Timeline.EnumerateEvents(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
						{
							InContext.Check(EventStartTime, EventEndTime, EventDepth, Event);
						});
					});
				}
			}
		},

		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		},
		
		SearchCache);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
