// Copyright Epic Games, Inc. All Rights Reserved.

#include "CpuCoreTimingTrack.h"

#include "TraceServices/Model/ContextSwitches.h"
#include "TraceServices/Model/Threads.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ContextSwitches/ViewModels/ContextSwitchesSharedState.h"
#include "Insights/ContextSwitches/ViewModels/ContextSwitchTimingEvent.h"
#include "Insights/InsightsManager.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"

#define LOCTEXT_NAMESPACE "FCpuCoreTimingTrack"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FCpuCoreTimingTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

void FCpuCoreTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::IContextSwitchesProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchesProvider(*Session.Get());
	if (ContextSwitchesProvider == nullptr)
	{
		return;
	}

	const FTimingTrackViewport& Viewport = Context.GetViewport();

	ContextSwitchesProvider->EnumerateCpuCoreEvents(CoreNumber, Viewport.GetStartTime(), Viewport.GetEndTime(), [&Builder, ContextSwitchesProvider](const TraceServices::FCpuCoreEvent& CpuCoreEvent)
		{
			constexpr uint32 EventDepth = 0;
			const uint32 EventColor = FTimingEvent::ComputeEventColor(CpuCoreEvent.SystemThreadId);
			const uint32 SystemThreadId = CpuCoreEvent.SystemThreadId;

			Builder.AddEvent(CpuCoreEvent.Start, CpuCoreEvent.End, EventDepth, EventColor,
				[SystemThreadId](float Width) -> const FString
				{
					const TCHAR* ThreadName = nullptr;

					TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
					if (Session.IsValid())
					{
						TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
						const TraceServices::IContextSwitchesProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchesProvider(*Session.Get());
						if (ContextSwitchesProvider)
						{
							uint32 ThreadId;
							if (ContextSwitchesProvider->GetThreadId(SystemThreadId, ThreadId))
							{
								const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session.Get());
								ThreadName = ThreadProvider.GetThreadName(ThreadId);
							}
						}
					}

					if (ThreadName)
					{
						return FString(ThreadName);
					}
					else
					{
						return FString::Printf(TEXT("Unknown %d"), SystemThreadId);
					}
				});

			return TraceServices::EContextSwitchEnumerationResult::Continue;
		});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FCpuCoreTimingTrack::BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	const TSharedPtr<ITimingEventFilter> EventFilterPtr = Context.GetEventFilter();
	if (EventFilterPtr.IsValid() && EventFilterPtr->FilterTrack(*this))
	{
		bool bFilterOnlyByEventType = false; // this is the most often use case, so the below code tries to optimize it
		uint64 FilterEventType = 0;
		if (EventFilterPtr->Is<FTimingEventFilterByEventType>())
		{
			bFilterOnlyByEventType = true;
			const FTimingEventFilterByEventType& EventFilter = EventFilterPtr->As<FTimingEventFilterByEventType>();
			FilterEventType = EventFilter.GetEventType();
		}

		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && TraceServices::ReadContextSwitchesProvider(*Session.Get()))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const TraceServices::IContextSwitchesProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchesProvider(*Session.Get());

			const FTimingTrackViewport& Viewport = Context.GetViewport();

			if (bFilterOnlyByEventType)
			{
				ContextSwitchesProvider->EnumerateCpuCoreEvents(CoreNumber, Viewport.GetStartTime(), Viewport.GetEndTime(), [&Builder, ContextSwitchesProvider, FilterEventType](const TraceServices::FCpuCoreEvent& CpuCoreEvent)
					{
						if (CpuCoreEvent.SystemThreadId != FilterEventType)
						{
							return TraceServices::EContextSwitchEnumerationResult::Continue;
						}

						constexpr uint32 EventDepth = 0;
						const uint32 EventColor = FTimingEvent::ComputeEventColor(CpuCoreEvent.SystemThreadId);
						const uint32 SystemThreadId = CpuCoreEvent.SystemThreadId;

						Builder.AddEvent(CpuCoreEvent.Start, CpuCoreEvent.End, EventDepth, EventColor,
							[SystemThreadId](float Width) -> const FString
							{
								const TCHAR* ThreadName = nullptr;

								TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
								if (Session.IsValid())
								{
									TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
									const TraceServices::IContextSwitchesProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchesProvider(*Session.Get());
									if (ContextSwitchesProvider)
									{
										uint32 ThreadId;
										if (ContextSwitchesProvider->GetThreadId(SystemThreadId, ThreadId))
										{
											const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session.Get());
											ThreadName = ThreadProvider.GetThreadName(ThreadId);
										}
									}
								}

								if (ThreadName)
								{
									return FString(ThreadName);
								}
								else
								{
									return FString::Printf(TEXT("Unknown %d"), SystemThreadId);
								}
							});

						return TraceServices::EContextSwitchEnumerationResult::Continue;
					});
			}
			else // generic filter
			{
				//TODO: if (EventFilterPtr->FilterEvent(TimingEvent))
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FCpuCoreTimingTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	FTimingEventsTrack::Draw(Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FCpuCoreTimingTrack::GetEvent(double InTime, double SecondsPerPixel, int32 Depth) const
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return nullptr;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::IContextSwitchesProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchesProvider(*Session.Get());
	if (ContextSwitchesProvider == nullptr)
	{
		return nullptr;
	}

	TraceServices::FCpuCoreEvent BestMatchEvent;
	double Delta = 2 * SecondsPerPixel;

	ContextSwitchesProvider->EnumerateCpuCoreEvents(CoreNumber, InTime - Delta, InTime + Delta,
		[InTime, &BestMatchEvent, &Delta](const TraceServices::FCpuCoreEvent& CpuCoreEvent)
		{
			if (CpuCoreEvent.Start <= InTime && CpuCoreEvent.End >= InTime)
			{
				BestMatchEvent = CpuCoreEvent;
				Delta = 0.0f;
				return TraceServices::EContextSwitchEnumerationResult::Stop;
			}

			double DeltaLeft = InTime - CpuCoreEvent.End;
			if (DeltaLeft >= 0 && DeltaLeft < Delta)
			{
				Delta = DeltaLeft;
				BestMatchEvent = CpuCoreEvent;
			}

			double DeltaRight = CpuCoreEvent.Start - InTime;
			if (DeltaRight >= 0 && DeltaRight < Delta)
			{
				Delta = DeltaRight;
				BestMatchEvent = CpuCoreEvent;
			}

			return TraceServices::EContextSwitchEnumerationResult::Continue;
		});

	if (Delta < 2 * SecondsPerPixel)
	{
		TSharedPtr<FCpuCoreTimingEvent> TimingEvent = MakeShared<FCpuCoreTimingEvent>(SharedThis(this), BestMatchEvent.Start, BestMatchEvent.End, 0);
		TimingEvent->SetSystemThreadId(BestMatchEvent.SystemThreadId);
		return TimingEvent;
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FCpuCoreTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	InOutTooltip.ResetContent();

	if (!InTooltipEvent.CheckTrack(this) || !InTooltipEvent.Is<FCpuCoreTimingEvent>())
	{
		return;
	}

	const FCpuCoreTimingEvent& CpuCoreEvent = InTooltipEvent.As<FCpuCoreTimingEvent>();

	const uint32 SystemThreadId = CpuCoreEvent.GetSystemThreadId();
	uint32 ThreadId = ~0;
	const TCHAR* ThreadName = nullptr;
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const TraceServices::IContextSwitchesProvider* ContextSwitchesProvider = TraceServices::ReadContextSwitchesProvider(*Session.Get());
			if (ContextSwitchesProvider)
			{
				if (ContextSwitchesProvider->GetThreadId(SystemThreadId, ThreadId))
				{
					const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session.Get());
					ThreadName = ThreadProvider.GetThreadName(ThreadId);
				}
			}
		}
	}
	if (ThreadName)
	{
		InOutTooltip.AddTitle(ThreadName);
	}
	else
	{
		InOutTooltip.AddTitle(TEXT("Unknown Thread"));
	}

	InOutTooltip.AddNameValueTextLine(TEXT("System Thread Id:"), FString::Printf(TEXT("%d"), SystemThreadId));
	if (ThreadId != ~0)
	{
		InOutTooltip.AddNameValueTextLine(TEXT("Thread Id:"), FString::Printf(TEXT("%d"), ThreadId));
	}

	InOutTooltip.AddNameValueTextLine(TEXT("Start Time:"), TimeUtils::FormatTimeAuto(InTooltipEvent.GetStartTime(), 6));
	InOutTooltip.AddNameValueTextLine(TEXT("End Time:"), TimeUtils::FormatTimeAuto(InTooltipEvent.GetEndTime(), 6));
	InOutTooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(InTooltipEvent.GetDuration()));

	InOutTooltip.UpdateLayout();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
