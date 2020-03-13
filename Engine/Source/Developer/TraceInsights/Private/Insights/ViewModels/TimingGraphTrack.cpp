// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimingGraphTrack.h"

#include <limits>

#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/Counters.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/ViewModels/GraphTrackBuilder.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "GraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingGraphSeries
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingGraphSeries::FTimingGraphSeries(FTimingGraphSeries::ESeriesType InType)
	: FGraphSeries()
	, Type(InType)
	, Id(0)
	, CachedSessionDuration(0.0)
	, CachedEvents()
	, bIsTime(InType == FTimingGraphSeries::ESeriesType::Frame || InType == FTimingGraphSeries::ESeriesType::Timer)
	, bIsMemory(false)
	, bIsFloatingPoint(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingGraphSeries::~FTimingGraphSeries()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString FTimingGraphSeries::FormatValue(double Value) const
{
	switch (Type)
	{
	case FTimingGraphSeries::ESeriesType::Frame:
		return FString::Printf(TEXT("%s (%g fps)"), *TimeUtils::FormatTimeAuto(Value), 1.0 / Value);

	case FTimingGraphSeries::ESeriesType::Timer:
		return TimeUtils::FormatTimeAuto(Value);

	case FTimingGraphSeries::ESeriesType::StatsCounter:
		if (bIsTime)
		{
			return TimeUtils::FormatTimeAuto(Value);
		}
		else if (bIsMemory)
		{
			const int64 MemValue = static_cast<int64>(Value);
			return FString::Printf(TEXT("%s (%s bytes)"), *FText::AsMemory(MemValue).ToString(), *FText::AsNumber(MemValue).ToString());
		}
		else if (bIsFloatingPoint)
		{
			return FString::Printf(TEXT("%g"), Value);
		}
		else
		{
			const int64 Int64Value = static_cast<int64>(Value);
			return FText::AsNumber(Int64Value).ToString();
		}
	}

	return FGraphSeries::FormatValue(Value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FTimingGraphTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingGraphTrack::FTimingGraphTrack()
	: FGraphTrack()
	//, SharedValueViewport()
{
	EnabledOptions = //EGraphOptions::ShowDebugInfo |
					 //EGraphOptions::ShowPoints |
					 EGraphOptions::ShowPointsWithBorder |
					 EGraphOptions::ShowLines |
					 EGraphOptions::ShowPolygon |
					 EGraphOptions::UseEventDuration |
					 //EGraphOptions::ShowBars |
					 EGraphOptions::ShowBaseline |
					 EGraphOptions::ShowVerticalAxisGrid |
					 EGraphOptions::ShowHeader |
					 EGraphOptions::None;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingGraphTrack::~FTimingGraphTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::Update(const ITimingTrackUpdateContext& Context)
{
	FGraphTrack::Update(Context);

	const bool bIsEntireGraphTrackDirty = IsDirty() || Context.GetViewport().IsHorizontalViewportDirty();
	bool bNeedsUpdate = bIsEntireGraphTrackDirty;

	if (!bNeedsUpdate)
	{
		for (TSharedPtr<FGraphSeries>& Series : AllSeries)
		{
			if (Series->IsVisible() && Series->IsDirty())
			{
				// At least one series is dirty.
				bNeedsUpdate = true;
				break;
			}
		}
	}

	if (bNeedsUpdate)
	{
		ClearDirtyFlag();

		NumAddedEvents = 0;

		const FTimingTrackViewport& Viewport = Context.GetViewport();

		for (TSharedPtr<FGraphSeries>& Series : AllSeries)
		{
			if (Series->IsVisible() && (bIsEntireGraphTrackDirty || Series->IsDirty()))
			{
				// Clear the flag before updating, because the update itself may further need to set the series as dirty.
				Series->ClearDirtyFlag();

				TSharedPtr<FTimingGraphSeries> TimingSeries = StaticCastSharedPtr<FTimingGraphSeries>(Series);
				switch (TimingSeries->Type)
				{
				case FTimingGraphSeries::ESeriesType::Frame:
					UpdateFrameSeries(*TimingSeries, Viewport);
					break;

				case FTimingGraphSeries::ESeriesType::Timer:
					UpdateTimerSeries(*TimingSeries, Viewport);
					break;

				case FTimingGraphSeries::ESeriesType::StatsCounter:
					UpdateStatsCounterSeries(*TimingSeries, Viewport);
					break;
				}
			}
		}

		UpdateStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame Series
////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::AddDefaultFrameSeries()
{
	TSharedRef<FTimingGraphSeries> GameFramesSeries = MakeShared<FTimingGraphSeries>(FTimingGraphSeries::ESeriesType::Frame);
	GameFramesSeries->SetName(TEXT("Game Frames"));
	GameFramesSeries->SetDescription(TEXT("Duration of Game frames"));
	GameFramesSeries->SetColor(FLinearColor(0.3f, 0.3f, 1.0f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	GameFramesSeries->Id = static_cast<uint32>(TraceFrameType_Game);
	GameFramesSeries->SetBaselineY(SharedValueViewport.GetBaselineY());
	GameFramesSeries->SetScaleY(SharedValueViewport.GetScaleY());
	GameFramesSeries->EnableSharedViewport();
	AllSeries.Add(GameFramesSeries);

	TSharedRef<FTimingGraphSeries> RenderingFramesSeries = MakeShared<FTimingGraphSeries>(FTimingGraphSeries::ESeriesType::Frame);
	RenderingFramesSeries->SetName(TEXT("Rendering Frames"));
	RenderingFramesSeries->SetDescription(TEXT("Duration of Rendering frames"));
	RenderingFramesSeries->SetColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	RenderingFramesSeries->Id = static_cast<uint32>(TraceFrameType_Rendering);
	RenderingFramesSeries->SetBaselineY(SharedValueViewport.GetBaselineY());
	RenderingFramesSeries->SetScaleY(SharedValueViewport.GetScaleY());
	RenderingFramesSeries->EnableSharedViewport();
	AllSeries.Add(RenderingFramesSeries);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::UpdateFrameSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport)
{
	FGraphTrackBuilder Builder(*this, Series, Viewport);
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::IFrameProvider& FramesProvider = ReadFrameProvider(*Session.Get());
		ETraceFrameType FrameType = static_cast<ETraceFrameType>(Series.Id);
		uint64 FrameCount = FramesProvider.GetFrameCount(FrameType);
		FramesProvider.EnumerateFrames(FrameType, 0, FrameCount - 1, [&Builder](const Trace::FFrame& Frame)
		{
			//TODO: add a "frame converter" (i.e. to fps, miliseconds or seconds)
			const double Duration = Frame.EndTime - Frame.StartTime;
			Builder.AddEvent(Frame.StartTime, Duration, Duration);
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer Series
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTimingGraphSeries> FTimingGraphTrack::GetTimerSeries(uint32 TimerId)
{
	TSharedPtr<FGraphSeries>* Ptr = AllSeries.FindByPredicate([TimerId](const TSharedPtr<FGraphSeries>& Series)
	{
		const TSharedPtr<FTimingGraphSeries> TimingSeries = StaticCastSharedPtr<FTimingGraphSeries>(Series);
		return TimingSeries->Type == FTimingGraphSeries::ESeriesType::Timer && TimingSeries->Id == TimerId;
	});
	return (Ptr != nullptr) ? StaticCastSharedPtr<FTimingGraphSeries>(*Ptr) : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTimingGraphSeries> FTimingGraphTrack::AddTimerSeries(uint32 TimerId, FLinearColor Color)
{
	TSharedRef<FTimingGraphSeries> Series = MakeShared<FTimingGraphSeries>(FTimingGraphSeries::ESeriesType::Timer);

	Series->SetName(TEXT("<Timer>"));
	Series->SetDescription(TEXT("Timer series"));

	const FLinearColor BorderColor(Color.R + 0.4f, Color.G + 0.4f, Color.B + 0.4f, 1.0f);
	Series->SetColor(Color, BorderColor);

	Series->Id = TimerId;
	//Series->CpuOrGpu = ;
	//Series->TimelineIndex = ;

	// Use shared viewport.
	Series->SetBaselineY(SharedValueViewport.GetBaselineY());
	Series->SetScaleY(SharedValueViewport.GetScaleY());
	Series->EnableSharedViewport();

	Series->CachedSessionDuration = 0.0;

	AllSeries.Add(Series);
	return Series;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::RemoveTimerSeries(uint32 TimerId)
{
	AllSeries.RemoveAll([TimerId](const TSharedPtr<FGraphSeries>& Series)
	{
		const TSharedPtr<FTimingGraphSeries> TimingSeries = StaticCastSharedPtr<FTimingGraphSeries>(Series);
		return TimingSeries->Type == FTimingGraphSeries::ESeriesType::Timer && TimingSeries->Id == TimerId;
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::UpdateTimerSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport)
{
	FGraphTrackBuilder Builder(*this, Series, Viewport);
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const double SessionDuration = Session->GetDurationSeconds();
		if (Series.CachedSessionDuration != SessionDuration)
		{
			Series.CachedSessionDuration = SessionDuration;

			const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());
			const uint32 TimerId = Series.Id;
			const uint64 TimelineCount = TimingProfilerProvider.GetTimelineCount();
			for (uint64 TimelineIndex = 0; TimelineIndex < TimelineCount; ++TimelineIndex)
			{
				TimingProfilerProvider.ReadTimeline(TimelineIndex, [&Series, SessionDuration, TimerId](const Trace::ITimingProfilerProvider::Timeline& Timeline)
				{
					Timeline.EnumerateEvents(0.0, SessionDuration,
						[&Series, TimerId](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
					{
						if (Event.TimerIndex == TimerId)
						{
							//TODO: add a "frame converter" (i.e. to fps, miliseconds or seconds)
							const double Duration = EndTime - StartTime;
							Series.CachedEvents.Add({ StartTime, Duration });
						}
					});
				});
			}

			Series.CachedEvents.Sort(&FTimingGraphSeries::CompareEventsByStartTime);
		}

		int32 StartIndex = Algo::UpperBoundBy(Series.CachedEvents, Viewport.GetStartTime(), &FTimingGraphSeries::FSimpleTimingEvent::StartTime);
		if (StartIndex > 0)
		{
			StartIndex--;
		}
		int32 EndIndex = Algo::UpperBoundBy(Series.CachedEvents, Viewport.GetEndTime(), &FTimingGraphSeries::FSimpleTimingEvent::StartTime);
		if (EndIndex < Series.CachedEvents.Num())
		{
			EndIndex++;
		}
		for (int32 Index = StartIndex; Index < EndIndex; ++Index)
		{
			const FTimingGraphSeries::FSimpleTimingEvent& Event = Series.CachedEvents[Index];
			Builder.AddEvent(Event.StartTime, Event.Duration, Event.Duration);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Stats Counter Series
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTimingGraphSeries> FTimingGraphTrack::GetStatsCounterSeries(uint32 CounterId)
{
	TSharedPtr<FGraphSeries>* Ptr = AllSeries.FindByPredicate([CounterId](const TSharedPtr<FGraphSeries>& Series)
	{
		const TSharedPtr<FTimingGraphSeries> TimingSeries = StaticCastSharedPtr<FTimingGraphSeries>(Series);
		return TimingSeries->Type == FTimingGraphSeries::ESeriesType::StatsCounter && TimingSeries->Id == CounterId;
	});
	return (Ptr != nullptr) ? StaticCastSharedPtr<FTimingGraphSeries>(*Ptr) : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTimingGraphSeries> FTimingGraphTrack::AddStatsCounterSeries(uint32 CounterId, FLinearColor Color)
{
	TSharedRef<FTimingGraphSeries> Series = MakeShared<FTimingGraphSeries>(FTimingGraphSeries::ESeriesType::StatsCounter);

	const TCHAR* CounterName = nullptr;
	bool bIsTime = false;
	bool bIsMemory = false;
	bool bIsFloatingPoint = false;

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::ICounterProvider& CountersProvider = Trace::ReadCounterProvider(*Session.Get());
		if (CounterId < CountersProvider.GetCounterCount())
		{
			CountersProvider.ReadCounter(CounterId, [&](const Trace::ICounter& Counter)
			{
				CounterName = Counter.GetName();
				//bIsTime = (Counter.GetDisplayHint() == Trace::CounterDisplayHint_Time);
				bIsMemory = (Counter.GetDisplayHint() == Trace::CounterDisplayHint_Memory);
				bIsFloatingPoint = Counter.IsFloatingPoint();
			});
		}
	}

	Series->SetName(CounterName != nullptr ? CounterName : TEXT("<StatsCounter>"));
	Series->SetDescription(TEXT("Stats counter series"));

	FLinearColor BorderColor(Color.R + 0.4f, Color.G + 0.4f, Color.B + 0.4f, 1.0f);
	Series->SetColor(Color, BorderColor);

	Series->Id = CounterId;

	Series->bIsTime = bIsTime;
	Series->bIsMemory = bIsMemory;
	Series->bIsFloatingPoint = bIsFloatingPoint;

	Series->SetBaselineY(GetHeight() - 1.0f);
	Series->SetScaleY(1.0);
	Series->EnableAutoZoom();

	AllSeries.Add(Series);
	return Series;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::RemoveStatsCounterSeries(uint32 CounterId)
{
	AllSeries.RemoveAll([CounterId](const TSharedPtr<FGraphSeries>& Series)
	{
		const TSharedPtr<FTimingGraphSeries> TimingSeries = StaticCastSharedPtr<FTimingGraphSeries>(Series);
		return TimingSeries->Type == FTimingGraphSeries::ESeriesType::StatsCounter && TimingSeries->Id == CounterId;
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::UpdateStatsCounterSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport)
{
	FGraphTrackBuilder Builder(*this, Series, Viewport);

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::ICounterProvider& CounterProvider = Trace::ReadCounterProvider(*Session.Get());
		CounterProvider.ReadCounter(Series.Id, [this, &Viewport, &Builder, &Series](const Trace::ICounter& Counter)
		{
			if (Series.IsAutoZoomEnabled())
			{
				double MinValue =  std::numeric_limits<double>::infinity();
				double MaxValue = -std::numeric_limits<double>::infinity();

				if (Counter.IsFloatingPoint())
				{
					Counter.EnumerateFloatValues(Viewport.GetStartTime(), Viewport.GetEndTime(), true, [&Builder, &MinValue, &MaxValue](double Time, double Value)
					{
						if (Value < MinValue)
						{
							MinValue = Value;
						}
						if (Value > MaxValue)
						{
							MaxValue = Value;
						}
					});
				}
				else
				{
					Counter.EnumerateValues(Viewport.GetStartTime(), Viewport.GetEndTime(), true, [&Builder, &MinValue, &MaxValue](double Time, int64 IntValue)
					{
						const double Value = static_cast<double>(IntValue);
						if (Value < MinValue)
						{
							MinValue = Value;
						}
						if (Value > MaxValue)
						{
							MaxValue = Value;
						}
					});
				}

				const float TopY = 4.0f;
				const float BottomY = GetHeight() - 4.0f;
				Series.UpdateAutoZoom(TopY, BottomY, MinValue, MaxValue);
			}

			if (Counter.IsFloatingPoint())
			{
				Counter.EnumerateFloatValues(Viewport.GetStartTime(), Viewport.GetEndTime(), true, [&Builder](double Time, double Value)
				{
					//TODO: add a "value unit converter"
					Builder.AddEvent(Time, 0.0, Value);
				});
			}
			else
			{
				Counter.EnumerateValues(Viewport.GetStartTime(), Viewport.GetEndTime(), true, [&Builder](double Time, int64 IntValue)
				{
					//TODO: add a "value unit converter"
					Builder.AddEvent(Time, 0.0, static_cast<double>(IntValue));
				});
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
