// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ThreadTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Styling/SlateBrush.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TimerNode.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"

#define LOCTEXT_NAMESPACE "ThreadTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::Draw(FTimingViewDrawHelper& Helper) const
{
	FTimingEventsTrack& Track = *const_cast<FThreadTimingTrack*>(this);

	if (Helper.BeginTimeline(Track))
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

			TimingProfilerProvider.ReadTimers([this, &Helper, &Track, &TimingProfilerProvider](const Trace::FTimingProfilerTimer* Timers, uint64 TimersCount)
			{
				TimingProfilerProvider.ReadTimeline(Track.GetId(), [this, &Helper, &Track, Timers](const Trace::ITimingProfilerProvider::Timeline& Timeline)
				{
					if (FTimingEventsTrack::bUseDownSampling)
					{
						const double SecondsPerPixel = 1.0 / Helper.GetViewport().GetScaleX();
						Timeline.EnumerateEventsDownSampled(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), SecondsPerPixel, [this, &Helper, Timers](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
						{
							Helper.AddEvent(StartTime, EndTime, Depth, Timers[Event.TimerIndex].Name);
						});
					}
					else
					{
						Timeline.EnumerateEvents(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), [this, &Helper, Timers](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
						{
							Helper.AddEvent(StartTime, EndTime, Depth, Timers[Event.TimerIndex].Name);
						});
					}
				});
			});

			Helper.EndTimeline(Track);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::DrawSelectedEventInfo(const FTimingEvent& SelectedTimingEvent, const FTimingTrackViewport& Viewport, const FDrawContext& DrawContext, const FSlateBrush* WhiteBrush, const FSlateFontInfo& Font) const
{
	const FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(SelectedTimingEvent.TypeId);

	FString Str = FString::Printf(TEXT("%s (Incl.: %s, Excl.: %s)"),
		TimerNodePtr ? *(TimerNodePtr->GetName().ToString()) : TEXT("N/A"),
		*TimeUtils::FormatTimeAuto(SelectedTimingEvent.Duration()),
		*TimeUtils::FormatTimeAuto(SelectedTimingEvent.ExclusiveTime));

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

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const
{
	FTimingEvent ParentTimingEvent;
	FTimingEvent RootTimingEvent;
	if (HoveredTimingEvent.IsValid())
	{
		GetParentAndRoot(HoveredTimingEvent, ParentTimingEvent, RootTimingEvent);
	}

	Tooltip.ResetContent();

	const FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(HoveredTimingEvent.TypeId);
	FString TimerName = TimerNodePtr ? TimerNodePtr->GetName().ToString() : TEXT("N/A");
	Tooltip.AddTitle(TimerName);

	if (ParentTimingEvent.IsValid() && HoveredTimingEvent.Depth > 0)
	{
		const FTimerNodePtr ParentTimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(ParentTimingEvent.TypeId);
		const FString ParentTimerName = ParentTimerNodePtr ? ParentTimerNodePtr->GetName().ToString() : TEXT("N/A");
		FNumberFormattingOptions FormattingOptions;
		FormattingOptions.MaximumFractionalDigits = 2;
		const FString ValueStr = FString::Printf(TEXT("%s %s"), *FText::AsPercent(HoveredTimingEvent.Duration() / ParentTimingEvent.Duration(), &FormattingOptions).ToString(), *ParentTimerName);
		Tooltip.AddNameValueTextLine(TEXT("% of Parent:"), ValueStr);
	}

	if (RootTimingEvent.IsValid() && HoveredTimingEvent.Depth > 1)
	{
		const FTimerNodePtr RootTimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(RootTimingEvent.TypeId);
		const FString RootTimerName = RootTimerNodePtr ? RootTimerNodePtr->GetName().ToString() : TEXT("N/A");
		FNumberFormattingOptions FormattingOptions;
		FormattingOptions.MaximumFractionalDigits = 2;
		const FString ValueStr = FString::Printf(TEXT("%s %s"), *FText::AsPercent(HoveredTimingEvent.Duration() / RootTimingEvent.Duration(), &FormattingOptions).ToString(), *RootTimerName);
		Tooltip.AddNameValueTextLine(TEXT("% of Root:"), ValueStr);
	}

	Tooltip.AddNameValueTextLine(TEXT("Inclusive Time:"), TimeUtils::FormatTimeAuto(HoveredTimingEvent.Duration()));
	Tooltip.AddNameValueTextLine(TEXT("Exclusive Time:"), TimeUtils::FormatTimeAuto(HoveredTimingEvent.ExclusiveTime));
	Tooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), HoveredTimingEvent.Depth));

	Tooltip.UpdateLayout();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::GetParentAndRoot(const FTimingEvent& TimingEvent, FTimingEvent& OutParentTimingEvent, FTimingEvent& OutRootTimingEvent) const
{
	// Note: This function does not compute Exclusive Time for parent and root events.

	OutRootTimingEvent.Track = TimingEvent.Track;
	OutParentTimingEvent.Track = TimingEvent.Track;

	if (TimingEvent.Depth > 0)
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			if (Trace::ReadTimingProfilerProvider(*Session.Get()))
			{
				const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

				TimingProfilerProvider.ReadTimeline(TimingEvent.Track->GetId(), [&](const Trace::ITimingProfilerProvider::Timeline& Timeline)
				{
					Timeline.EnumerateEvents(TimingEvent.StartTime, TimingEvent.EndTime, [&](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
					{
						if (EventDepth == 0)
						{
							OutRootTimingEvent.TypeId = Event.TimerIndex;
							OutRootTimingEvent.Depth = EventDepth;
							OutRootTimingEvent.StartTime = EventStartTime;
							OutRootTimingEvent.EndTime = EventEndTime;
						}
						if (EventDepth == TimingEvent.Depth - 1)
						{
							OutParentTimingEvent.TypeId = Event.TimerIndex;
							OutParentTimingEvent.Depth = EventDepth;
							OutParentTimingEvent.StartTime = EventStartTime;
							OutParentTimingEvent.EndTime = EventEndTime;
						}
					});
				});
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingTrack::SearchTimingEvent(const double InStartTime,
										   const double InEndTime,
										   TFunctionRef<bool(double, double, uint32)> InPredicate,
										   FTimingEvent& InOutTimingEvent,
										   bool bInStopAtFirstMatch,
										   bool bInSearchForLargestEvent) const
{
	struct FSearchTimingEventContext
	{
		const double StartTime;
		const double EndTime;
		TFunctionRef<bool(double, double, uint32)> Predicate;
		FTimingEvent& TimingEvent;
		const bool bStopAtFirstMatch;
		const bool bSearchForLargestEvent;
		mutable bool bFound;
		mutable bool bContinueSearching;
		mutable double LargestDuration;

		FSearchTimingEventContext(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent)
			: StartTime(InStartTime)
			, EndTime(InEndTime)
			, Predicate(InPredicate)
			, TimingEvent(InOutTimingEvent)
			, bStopAtFirstMatch(bInStopAtFirstMatch)
			, bSearchForLargestEvent(bInSearchForLargestEvent)
			, bFound(false)
			, bContinueSearching(true)
			, LargestDuration(-1.0)
		{
		}

		void CheckEvent(double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
		{
			if (bContinueSearching && Predicate(EventStartTime, EventEndTime, EventDepth))
			{
				if (!bSearchForLargestEvent || EventEndTime - EventStartTime > LargestDuration)
				{
					LargestDuration = EventEndTime - EventStartTime;

					TimingEvent.TypeId = Event.TimerIndex;
					TimingEvent.Depth = EventDepth;
					TimingEvent.StartTime = EventStartTime;
					TimingEvent.EndTime = EventEndTime;

					bFound = true;
					bContinueSearching = !bStopAtFirstMatch || bSearchForLargestEvent;
				}
			}
		}
	};

	FSearchTimingEventContext Ctx(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent);

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		if (Trace::ReadTimingProfilerProvider(*Session.Get()))
		{
			const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

			TimingProfilerProvider.ReadTimeline(Ctx.TimingEvent.Track->GetId(), [&Ctx](const Trace::ITimingProfilerProvider::Timeline& Timeline)
			{
				Timeline.EnumerateEvents(Ctx.StartTime, Ctx.EndTime, [&Ctx](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
				{
					Ctx.CheckEvent(EventStartTime, EventEndTime, EventDepth, Event);
				});
			});
		}
	}

	return Ctx.bFound;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::ComputeTimingEventStats(FTimingEvent& InOutTimingEvent) const
{
	if (InOutTimingEvent.IsValid())
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			if (Trace::ReadTimingProfilerProvider(*Session.Get()))
			{
				const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

				// Compute Exclusive Time.
				TimingProfilerProvider.ReadTimeline(InOutTimingEvent.Track->GetId(), [&InOutTimingEvent](const Trace::ITimingProfilerProvider::Timeline& Timeline)
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

					State.EventStartTime = InOutTimingEvent.StartTime;
					State.EventEndTime = InOutTimingEvent.EndTime;
					State.EventDepth = InOutTimingEvent.Depth;

					State.CurrentDepth = 0;
					State.LastTime = 0.0;
					State.ExclusiveTime = 0.0;
					State.IsInEventScope = false;

					Timeline.EnumerateEvents(InOutTimingEvent.StartTime, InOutTimingEvent.EndTime, [&State](bool IsEnter, double Time, const Trace::FTimingProfilerEvent& Event)
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

					InOutTimingEvent.ExclusiveTime = State.ExclusiveTime;
				});
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
