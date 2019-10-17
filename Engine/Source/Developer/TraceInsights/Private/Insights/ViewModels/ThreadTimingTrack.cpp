// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ThreadTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Styling/SlateBrush.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"

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
#include "Insights/ViewModels/TimingEventSearch.h"

#define LOCTEXT_NAMESPACE "ThreadTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::Draw(ITimingViewDrawHelper& Helper) const
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
	FindTimingProfilerEvent(SelectedTimingEvent, [&SelectedTimingEvent, &Font, &Viewport, &DrawContext, &WhiteBrush](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{	
		const FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(InFoundEvent.TimerIndex);
		if(TimerNodePtr.IsValid())
		{
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
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const
{
	FindTimingProfilerEvent(HoveredTimingEvent, [this, &Tooltip, &HoveredTimingEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{	
		FTimingEvent ParentTimingEvent;
		Trace::FTimingProfilerEvent ParentEvent;
		FTimingEvent RootTimingEvent;
		Trace::FTimingProfilerEvent RootEvent;
		if (HoveredTimingEvent.IsValid())
		{
			GetParentAndRoot(HoveredTimingEvent, ParentTimingEvent, ParentEvent, RootTimingEvent, RootEvent);
		}

		Tooltip.ResetContent();

		const FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(InFoundEvent.TimerIndex);
		FString TimerName = TimerNodePtr ? TimerNodePtr->GetName().ToString() : TEXT("N/A");
		Tooltip.AddTitle(TimerName);

		if (ParentTimingEvent.IsValid() && HoveredTimingEvent.Depth > 0)
		{
			const FTimerNodePtr ParentTimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(ParentEvent.TimerIndex);
			const FString ParentTimerName = ParentTimerNodePtr.IsValid() ? ParentTimerNodePtr->GetName().ToString() : TEXT("N/A");
			FNumberFormattingOptions FormattingOptions;
			FormattingOptions.MaximumFractionalDigits = 2;
			const FString ValueStr = FString::Printf(TEXT("%s %s"), *FText::AsPercent(HoveredTimingEvent.Duration() / ParentTimingEvent.Duration(), &FormattingOptions).ToString(), *ParentTimerName);
			Tooltip.AddNameValueTextLine(TEXT("% of Parent:"), ValueStr);
		}

		if (RootTimingEvent.IsValid() && HoveredTimingEvent.Depth > 1)
		{
			const FTimerNodePtr RootTimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(RootEvent.TimerIndex);
			const FString RootTimerName = RootTimerNodePtr.IsValid() ? RootTimerNodePtr->GetName().ToString() : TEXT("N/A");
			FNumberFormattingOptions FormattingOptions;
			FormattingOptions.MaximumFractionalDigits = 2;
			const FString ValueStr = FString::Printf(TEXT("%s %s"), *FText::AsPercent(HoveredTimingEvent.Duration() / RootTimingEvent.Duration(), &FormattingOptions).ToString(), *RootTimerName);
			Tooltip.AddNameValueTextLine(TEXT("% of Root:"), ValueStr);
		}

		Tooltip.AddNameValueTextLine(TEXT("Inclusive Time:"), TimeUtils::FormatTimeAuto(HoveredTimingEvent.Duration()));

		//Tooltip.AddNameValueTextLine(TEXT("Exclusive Time:"), TimeUtils::FormatTimeAuto(HoveredTimingEvent.ExclusiveTime));
		{
			FNumberFormattingOptions FormattingOptions;
			FormattingOptions.MaximumFractionalDigits = 2;
			const FString ExclStr = FString::Printf(TEXT("%s (%s)"), *TimeUtils::FormatTimeAuto(HoveredTimingEvent.ExclusiveTime), *FText::AsPercent(HoveredTimingEvent.ExclusiveTime / HoveredTimingEvent.Duration(), &FormattingOptions).ToString());
			Tooltip.AddNameValueTextLine(TEXT("Exclusive Time:"), ExclStr);
		}

		Tooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), HoveredTimingEvent.Depth));

		Tooltip.UpdateLayout();
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::GetParentAndRoot(const FTimingEvent& TimingEvent, FTimingEvent& OutParentTimingEvent, Trace::FTimingProfilerEvent& OutParentEvent, FTimingEvent& OutRootTimingEvent, Trace::FTimingProfilerEvent& OutRootEvent) const
{
	// Note: This function does not compute Exclusive Time for parent and root events.

	if (TimingEvent.Depth > 0)
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			if (Trace::ReadTimingProfilerProvider(*Session.Get()))
			{
				const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

				TimingProfilerProvider.ReadTimeline(GetId(), [&TimingEvent, &OutParentTimingEvent, &OutParentEvent, &OutRootTimingEvent, &OutRootEvent](const Trace::ITimingProfilerProvider::Timeline& Timeline)
				{
					Timeline.EnumerateEvents(TimingEvent.StartTime, TimingEvent.EndTime, [&TimingEvent, &OutParentTimingEvent, &OutParentEvent, &OutRootTimingEvent, &OutRootEvent](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
					{
						if (EventDepth == 0)
						{
							OutRootEvent = Event;
							OutRootTimingEvent = FTimingEvent(TimingEvent.Track, EventStartTime, EventEndTime, EventDepth);
						}
						if (EventDepth == TimingEvent.Depth - 1)
						{
							OutParentEvent = Event;
							OutParentTimingEvent = FTimingEvent(TimingEvent.Track, EventStartTime, EventEndTime, EventDepth);
						}
					});
				});
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingTrack::SearchTimingEvent(const FTimingEventSearchParameters& InSearchParameters, FTimingEvent& InOutTimingEvent) const
{
	return FindTimingProfilerEvent(InSearchParameters, [this, &InOutTimingEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{
		InOutTimingEvent = FTimingEvent(this, InFoundStartTime, InFoundEndTime, InFoundDepth);
	});
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

void FThreadTimingTrack::OnEventSelected(const FTimingEvent& SelectedTimingEvent) const
{
	FindTimingProfilerEvent(SelectedTimingEvent, [](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{
		// Select the timer node corresponding to timing event type of selected timing event.
		FTimingProfilerManager::Get()->SetSelectedTimer(InFoundEvent.TimerIndex);
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::OnClipboardCopyEvent(const FTimingEvent& SelectedTimingEvent) const
{
	FindTimingProfilerEvent(SelectedTimingEvent, [](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{
		const FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(InFoundEvent.TimerIndex);
		if (TimerNodePtr.IsValid())
		{
			// Copy name of selected timing event to clipboard.
			FPlatformApplicationMisc::ClipboardCopy(*TimerNodePtr->GetName().ToString());
		}
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	FText SectionTitle;
	if(GetGroupName() != nullptr)
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
	auto MatchDepth = [&InTimingEvent](double, double, uint32 InDepth)
	{ 
		return InDepth == InTimingEvent.Depth; 
	};

	FTimingEventSearchParameters SearchParameters(InTimingEvent.StartTime, InTimingEvent.EndTime, ETimingEventSearchFlags::StopAtFirstMatch, MatchDepth);
	SearchParameters.SearchHandle = &InTimingEvent.SearchHandle;
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

					TimingProfilerProvider.ReadTimeline(GetId(), [&InContext](const Trace::ITimingProfilerProvider::Timeline& Timeline)
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
