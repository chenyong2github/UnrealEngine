// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimingGraphTrack.h"

#include <limits>

#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "GraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingGraphSeries
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
		if (bIsMemory)
		{
			const int64 MemValue = static_cast<int64>(Value);
			return FString::Printf(TEXT("%s (%s bytes)"), *FText::AsMemory(MemValue).ToString(), *FText::AsNumber(MemValue).ToString());
		}
		if (bIsFloatingPoint)
		{
			return FString::Printf(TEXT("%g"), Value);
		}
		else
		{
			const int64 Int64Value = static_cast<int64>(Value);
			return FText::AsNumber(Int64Value).ToString();
		}
		break;
	}

	return FGraphSeries::FormatValue(Value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingGraphTrack::FTimingGraphTrack(uint64 InTrackId)
	: FGraphTrack(InTrackId)
{
	bDrawPoints = true;
	bDrawPointsWithBorder = true;
	bDrawLines = true;
	bDrawPolygon = true;
	bUseEventDuration = true;
	bDrawBoxes = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::AddDefaultFrameSeries()
{
	const float BaselineY = GetHeight() - 1.0f;
	const double ScaleY = GetHeight() / 0.1; // 100ms

	TSharedPtr<FTimingGraphSeries> GameFramesSeries = MakeShareable(new FTimingGraphSeries());
	GameFramesSeries->SetName(TEXT("Game Frames"));
	GameFramesSeries->SetDescription(TEXT("Duration of Game frames"));
	GameFramesSeries->SetColor(FLinearColor(0.3f, 0.3f, 1.0f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	GameFramesSeries->Type = FTimingGraphSeries::ESeriesType::Frame;
	GameFramesSeries->Id = static_cast<uint32>(TraceFrameType_Game);
	GameFramesSeries->SetBaselineY(BaselineY);
	GameFramesSeries->SetScaleY(ScaleY);
	AllSeries.Add(GameFramesSeries);

	TSharedPtr<FTimingGraphSeries> RenderingFramesSeries = MakeShareable(new FTimingGraphSeries());
	RenderingFramesSeries->SetName(TEXT("Rendering Frames"));
	RenderingFramesSeries->SetDescription(TEXT("Duration of Rendering frames"));
	RenderingFramesSeries->SetColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	RenderingFramesSeries->Type = FTimingGraphSeries::ESeriesType::Frame;
	RenderingFramesSeries->Id = static_cast<uint32>(TraceFrameType_Rendering);
	RenderingFramesSeries->SetBaselineY(BaselineY);
	RenderingFramesSeries->SetScaleY(ScaleY);
	AllSeries.Add(RenderingFramesSeries);
}

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
	TSharedPtr<FTimingGraphSeries> Series = MakeShareable(new FTimingGraphSeries());

	const TCHAR* CounterName = nullptr;
	bool bIsFloatingPoint = false;
	bool bIsMemory = false;

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
				bIsFloatingPoint = Counter.IsFloatingPoint();
				bIsMemory = (Counter.GetDisplayHint() == Trace::CounterDisplayHint_Memory);
			});
		}
	}

	Series->SetName(CounterName != nullptr ? CounterName : TEXT("<StatsCounter>"));
	Series->SetDescription(TEXT("Stats counter series"));

	FLinearColor BorderColor(Color.R + 0.4f, Color.G + 0.4f, Color.B + 0.4f, 1.0f);
	Series->SetColor(Color, BorderColor);

	Series->Type = FTimingGraphSeries::ESeriesType::StatsCounter;
	Series->Id = CounterId;
	Series->bIsFloatingPoint = bIsFloatingPoint;
	Series->bIsMemory = bIsMemory;

	Series->SetBaselineY(GetHeight() - 1.0f);
	Series->SetScaleY(1.0);

	Series->EnableAutoZoom();
	Series->SetTargetAutoZoomRange(0.0, 1.0);
	Series->SetAutoZoomRange(0.0, 1.0);

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

FTimingGraphTrack::~FTimingGraphTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::Update(const FTimingTrackViewport& Viewport)
{
	FGraphTrack::Update(Viewport);

	const bool bIsEntireGraphTrackDirty = IsDirty();
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

		for (TSharedPtr<FGraphSeries>& Series : AllSeries)
		{
			if (Series->IsVisible() && (bIsEntireGraphTrackDirty || Series->IsDirty()))
			{
				// Clear the flag before updating, becasue the update itself may furter need to set the series as dirty.
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

void FTimingGraphTrack::UpdateTimerSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport)
{
	//TODO
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::UpdateStatsCounterSeries(FTimingGraphSeries& Series, const FTimingTrackViewport& Viewport)
{
	FGraphTrackBuilder Builder(*this, Series, Viewport);
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::ICounterProvider& CountersProvider = Trace::ReadCounterProvider(*Session.Get());
		CountersProvider.ReadCounter(Series.Id, [this, &Viewport, &Builder, &Series](const Trace::ICounter& Counter)
		{
			if (Series.IsAutoZoomEnabled())
			{
				double MinValue =  std::numeric_limits<double>::infinity();
				double MaxValue = -std::numeric_limits<double>::infinity();

				if (Counter.IsFloatingPoint())
				{
					Counter.EnumerateFloatValues(Viewport.GetStartTime(), Viewport.GetEndTime(), [&Builder, &MinValue, &MaxValue](double Time, double Value)
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
					Counter.EnumerateValues(Viewport.GetStartTime(), Viewport.GetEndTime(), [&Builder, &MinValue, &MaxValue](double Time, int64 IntValue)
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

				const double LowValue = Series.GetValueForY(BottomY);
				const double HighValue = Series.GetValueForY(TopY);

				// If MinValue == MaxValue, we keep the previous baseline and scale, but only if the min/max value is already visible.
				if (MinValue == MaxValue && (MinValue < LowValue || MaxValue > HighValue))
				{
					MinValue = FMath::Min(MinValue, LowValue);
					MaxValue = FMath::Max(MaxValue, HighValue);
				}

				if (MinValue < MaxValue)
				{
					constexpr bool bIsAutoZoomAnimated = true;
					if (bIsAutoZoomAnimated)
					{
						// Interpolate the min-max interval (animating the vertical position and scale of the graph series).
						constexpr double InterpolationSpeed = 0.5;
						const double NewMinValue = InterpolationSpeed * MinValue + (1.0 - InterpolationSpeed) * LowValue;
						const double NewMaxValue = InterpolationSpeed * MaxValue + (1.0 - InterpolationSpeed) * HighValue;

						// Check if we reach the target min-max interval.
						const double ErrorTolerance = 0.5 / Series.GetScaleY(); // delta value for dy ~= 0.5 pixels
						if (!FMath::IsNearlyEqual(NewMinValue, MinValue, ErrorTolerance) ||
							!FMath::IsNearlyEqual(NewMaxValue, MaxValue, ErrorTolerance))
						{
							MinValue = NewMinValue;
							MaxValue = NewMaxValue;

							// Request a new update so we can further interpolate the min-max interval.
							Series.SetDirtyFlag();
						}
					}

					double BaselineY;
					double ScaleY;
					Series.ComputeBaselineAndScale(MinValue, MaxValue, TopY, BottomY, BaselineY, ScaleY);
					Series.SetBaselineY(BaselineY);
					Series.SetScaleY(ScaleY);
				}
				else
				{
					// If MinValue == MaxValue, we keep the previous baseline and scale.
				}
			}

			if (Counter.IsFloatingPoint())
			{
				Counter.EnumerateFloatValues(Viewport.GetStartTime(), Viewport.GetEndTime(), [&Builder](double Time, double Value)
				{
					//TODO: add a "value unit converter"
					Builder.AddEvent(Time, 0.0, Value);
				});
			}
			else
			{
				Counter.EnumerateValues(Viewport.GetStartTime(), Viewport.GetEndTime(), [&Builder](double Time, int64 IntValue)
				{
					//TODO: add a "value unit converter"
					Builder.AddEvent(Time, 0.0, static_cast<double>(IntValue));
				});
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
