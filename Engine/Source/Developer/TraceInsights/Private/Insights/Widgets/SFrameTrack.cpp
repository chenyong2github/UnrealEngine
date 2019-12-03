// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SFrameTrack.h"

#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"
#include "Widgets/Layout/SScrollBar.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/TimingProfilerCommon.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/FrameTrackHelper.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SFrameTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

SFrameTrack::SFrameTrack()
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SFrameTrack::~SFrameTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::Reset()
{
	Viewport.Reset();
	FAxisViewportDouble& ViewportY = Viewport.GetVerticalAxisViewport();
	ViewportY.SetScaleLimits(0.01, 1000000.0);
	ViewportY.SetScale(1500.0);
	bIsViewportDirty = true;

	SeriesMap.Reset();
	SeriesOrder.Reset();
	SeriesOrder.Add(TraceFrameType_Game);
	SeriesOrder.Add(TraceFrameType_Rendering);

	bIsStateDirty = true;

	bShowGameFrames = true;
	bShowRenderingFrames = true;
	bIsAutoZoomEnabled = true;

	AnalysisSyncNextTimestamp = 0;

	MousePosition = FVector2D::ZeroVector;

	MousePositionOnButtonDown = FVector2D::ZeroVector;
	ViewportPosXOnButtonDown = 0.0f;

	MousePositionOnButtonUp = FVector2D::ZeroVector;

	bIsLMB_Pressed = false;
	bIsRMB_Pressed = false;

	bIsScrolling = false;

	HoveredSample.Reset();
	TooltipDesiredOpacity = 0.9f;
	TooltipOpacity = 0.0f;

	//ThisGeometry

	CursorType = ECursorType::Default;

	NumUpdatedFrames = 0;
	UpdateDurationHistory.Reset();
	DrawDurationHistory.Reset();
	OnPaintDurationHistory.Reset();
	LastOnPaintTime = FPlatformTime::Cycles64();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)

		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		.Padding(FMargin(0, 0, 0, 0))
		[
			SAssignNew(HorizontalScrollBar, SScrollBar)
			.Orientation(Orient_Horizontal)
			.AlwaysShowScrollbar(false)
			.Visibility(EVisibility::Visible)
			.Thickness(FVector2D(5.0f, 5.0f))
			.RenderOpacity(0.75)
			.OnUserScrolled(this, &SFrameTrack::HorizontalScrollBar_OnUserScrolled)
		]
	];

	UpdateHorizontalScrollBar();

	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (ThisGeometry != AllottedGeometry || bIsViewportDirty)
	{
		bIsViewportDirty = false;
		const float ViewWidth = AllottedGeometry.GetLocalSize().X;
		const float ViewHeight = AllottedGeometry.GetLocalSize().Y;
		Viewport.SetSize(ViewWidth, ViewHeight);
		bIsStateDirty = true;
	}

	ThisGeometry = AllottedGeometry;

	FAxisViewportInt32& ViewportX = Viewport.GetHorizontalAxisViewport();

	if (!bIsScrolling)
	{
		// Elastic snap to horizontal limits.
		if (ViewportX.UpdatePosWithinLimits())
		{
			bIsStateDirty = true;
		}
	}

	uint64 Time = FPlatformTime::Cycles64();
	if (Time > AnalysisSyncNextTimestamp)
	{
		const uint64 WaitTime = static_cast<uint64>(0.1 / FPlatformTime::GetSecondsPerCycle64()); // 100ms
		AnalysisSyncNextTimestamp = Time + WaitTime;

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::IFrameProvider& FramesProvider = Trace::ReadFrameProvider(*Session.Get());

			for (int32 FrameType = 0; FrameType < TraceFrameType_Count; ++FrameType)
			{
				TSharedPtr<FFrameTrackSeries> SeriesPtr = FindOrAddSeries(FrameType);

				int32 NumFrames = FramesProvider.GetFrameCount(static_cast<ETraceFrameType>(FrameType));
				if (NumFrames > ViewportX.GetMaxValue())
				{
					ViewportX.SetMinMaxInterval(0, NumFrames);

					if (bIsAutoZoomEnabled)
					{
						if (ViewportX.GetPos() == 0.0f) // only if the view is unchanged
						{
							// Auto zoom out (until entire session time range fits into view).
							while (ViewportX.GetMaxPos() - ViewportX.GetMinPos() > ViewportX.GetSize())
							{
								ZoomHorizontally(-0.1f, 0.0f);
								ViewportX.ScrollAtPos(0.0f);
							}
						}
						else
						{
							bIsAutoZoomEnabled = false;
						}
					}

					UpdateHorizontalScrollBar();
					bIsStateDirty = true;
				}
			}
		}
	}

	if (bIsStateDirty)
	{
		bIsStateDirty = false;
		UpdateState();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<FFrameTrackSeries> SFrameTrack::FindOrAddSeries(int32 FrameType)
{
	TSharedPtr<FFrameTrackSeries>* SeriesPtrPtr = SeriesMap.Find(FrameType);
	if (SeriesPtrPtr)
	{
		ensure((**SeriesPtrPtr).FrameType == FrameType);
		return (*SeriesPtrPtr).ToSharedRef();
	}
	else
	{
		TSharedRef<FFrameTrackSeries> SeriesRef = MakeShared<FFrameTrackSeries>(FrameType);
		SeriesMap.Add(FrameType, SeriesRef);
		return SeriesRef;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FFrameTrackSeries> SFrameTrack::FindSeries(int32 FrameType) const
{
	const TSharedPtr<FFrameTrackSeries>* SeriesPtrPtr = SeriesMap.Find(FrameType);
	if (SeriesPtrPtr)
	{
		ensure((**SeriesPtrPtr).FrameType == FrameType);
		return *SeriesPtrPtr;
	}
	else
	{
		return nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::UpdateState()
{
	FStopwatch Stopwatch;
	Stopwatch.Start();

	// Reset stats.
	for (TPair<int32, TSharedPtr<FFrameTrackSeries>>& KeyValuePair : SeriesMap)
	{
		TSharedPtr<FFrameTrackSeries>& SeriesPtr = KeyValuePair.Value;
		SeriesPtr->NumAggregatedFrames = 0;
	}
	NumUpdatedFrames = 0;

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::IFrameProvider& FramesProvider = Trace::ReadFrameProvider(*Session.Get());

		const FAxisViewportInt32& ViewportX = Viewport.GetHorizontalAxisViewport();

		const uint64 StartIndex = static_cast<uint64>(FMath::Max(0, ViewportX.GetValueAtOffset(0.0f)));
		const uint64 EndIndex = static_cast<uint64>(ViewportX.GetValueAtOffset(ViewportX.GetSize()));

		for (int32 FrameType = 0; FrameType < TraceFrameType_Count; ++FrameType)
		{
			TSharedPtr<FFrameTrackSeries> SeriesPtr = FindOrAddSeries(FrameType);

			FFrameTrackSeriesBuilder Builder(*SeriesPtr, Viewport);

			FramesProvider.EnumerateFrames(static_cast<ETraceFrameType>(FrameType), StartIndex, EndIndex, [&Builder](const Trace::FFrame& Frame)
			{
				Builder.AddFrame(Frame);
			});

			NumUpdatedFrames += Builder.GetNumAddedFrames();
		}
	}

	Stopwatch.Stop();
	UpdateDurationHistory.AddValue(Stopwatch.AccumulatedTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFrameTrackSampleRef SFrameTrack::GetSampleAtMousePosition(float X, float Y)
{
	if (!bIsStateDirty)
	{
		float SampleW = Viewport.GetSampleWidth();
		int32 SampleIndex = FMath::FloorToInt(X / SampleW);
		if (SampleIndex >= 0)
		{
			// Search in reverse paint order.
			for (int32 SeriesIndex = SeriesOrder.Num() - 1; SeriesIndex >= 0; --SeriesIndex)
			{
				int32 FrameType = SeriesOrder[SeriesIndex];

				if ((FrameType == TraceFrameType_Rendering && !bShowRenderingFrames) ||
					(FrameType == TraceFrameType_Game && !bShowGameFrames))
				{
					continue;
				}

				const TSharedPtr<FFrameTrackSeries> SeriesPtr = FindSeries(FrameType);
				if (SeriesPtr.IsValid())
				{
					if (SeriesPtr->NumAggregatedFrames > 0 &&
						SampleIndex < SeriesPtr->Samples.Num())
					{
						const FFrameTrackSample& Sample = SeriesPtr->Samples[SampleIndex];
						if (Sample.NumFrames > 0)
						{
							const FAxisViewportDouble& ViewportY = Viewport.GetVerticalAxisViewport();

							const float ViewHeight = FMath::RoundToFloat(Viewport.GetHeight());
							const float BaselineY = FMath::RoundToFloat(ViewportY.GetOffsetForValue(0.0));

							float ValueY;
							if (Sample.LargestFrameDuration == std::numeric_limits<double>::infinity())
							{
								ValueY = ViewHeight;
							}
							else
							{
								ValueY = FMath::RoundToFloat(ViewportY.GetOffsetForValue(Sample.LargestFrameDuration));
							}

							constexpr float ToleranceY = 3.0f; // [pixels]

							const float BottomY = FMath::Min(ViewHeight, ViewHeight - BaselineY + ToleranceY);
							const float TopY = FMath::Max(0.0f, ViewHeight - ValueY - ToleranceY);

							if (Y >= TopY && Y < BottomY)
							{
								return FFrameTrackSampleRef(SeriesPtr, MakeShared<FFrameTrackSample>(Sample));
							}
						}
					}
				}
			}
		}
	}
	return FFrameTrackSampleRef(nullptr, nullptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::SelectFrameAtMousePosition(float X, float Y)
{
	FFrameTrackSampleRef SampleRef = GetSampleAtMousePosition(X, Y);
	if (!SampleRef.IsValid())
	{
		SampleRef = GetSampleAtMousePosition(X - 1.0f, Y);
	}
	if (!SampleRef.IsValid())
	{
		SampleRef = GetSampleAtMousePosition(X + 1.0f, Y);
	}

	if (SampleRef.IsValid())
	{
		TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Window.IsValid())
		{
			TSharedPtr<STimingView> TimingView = Window->GetTimingView();
			if (TimingView.IsValid())
			{
				const double StartTime = SampleRef.Sample->LargestFrameStartTime;
				const double Duration = SampleRef.Sample->LargestFrameDuration;
				TimingView->CenterOnTimeInterval(StartTime, Duration);
				TimingView->SelectTimeInterval(StartTime, Duration);
				FSlateApplication::Get().SetKeyboardFocus(TimingView);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FrameTypeToString(int32 FrameType)
{
	switch (FrameType)
	{
		case TraceFrameType_Game:      return TEXT("Game");
		case TraceFrameType_Rendering: return TEXT("Rendering");
		default:                       return TEXT("Misc");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 SFrameTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DrawContext(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FSlateFontInfo SummaryFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	const FSlateBrush* WhiteBrush = FInsightsStyle::Get().GetBrush("WhiteBrush");

	const float ViewWidth = AllottedGeometry.Size.X;
	const float ViewHeight = AllottedGeometry.Size.Y;

	int32 NumDrawSamples = 0;

	//////////////////////////////////////////////////
	{
		FStopwatch Stopwatch;
		Stopwatch.Start();

		FFrameTrackDrawHelper Helper(DrawContext, Viewport);

		Helper.DrawBackground();

		// Draw the horizontal axis grid.
		DrawHorizontalAxisGrid(DrawContext, WhiteBrush, SummaryFont);

		// Draw frames, for each visible Series.
		for (int32 SeriesIndex = 0; SeriesIndex < SeriesOrder.Num(); ++SeriesIndex)
		{
			int32 FrameType = SeriesOrder[SeriesIndex];

			if ((FrameType == TraceFrameType_Rendering && !bShowRenderingFrames) ||
				(FrameType == TraceFrameType_Game && !bShowGameFrames))
			{
				continue;
			}

			const TSharedPtr<FFrameTrackSeries> SeriesPtr = FindSeries(FrameType);
			if (SeriesPtr.IsValid())
			{
				Helper.DrawCached(*SeriesPtr);
			}
		}

		NumDrawSamples = Helper.GetNumDrawSamples();

		// Highlight the mouse hovered sample (frame).
		if (HoveredSample.IsValid())
		{
			Helper.DrawHoveredSample(*HoveredSample.Sample);
		}

		// Draw the vertical axis grid.
		DrawVerticalAxisGrid(DrawContext, WhiteBrush, SummaryFont);

		// Draw tooltip for hovered sample (frame).
		if (HoveredSample.IsValid())
		{
			if (TooltipOpacity < TooltipDesiredOpacity)
			{
				TooltipOpacity = TooltipOpacity * 0.9f + TooltipDesiredOpacity * 0.1f;
			}
			else
			{
				TooltipOpacity = TooltipDesiredOpacity;
			}

			const FString Text = FString::Format(TEXT("{0} frame {1} ({2})"),
				{
					::FrameTypeToString(HoveredSample.Series->FrameType),
					FText::AsNumber(HoveredSample.Sample->LargestFrameIndex).ToString(),
					TimeUtils::FormatTimeAuto(HoveredSample.Sample->LargestFrameDuration)
				});

			FVector2D TextSize = FontMeasureService->Measure(Text, SummaryFont);

			const float DX = 2.0f;
			const float W2 = TextSize.X / 2 + DX;

			const FAxisViewportInt32& ViewportX = Viewport.GetHorizontalAxisViewport();

			float X1 = ViewportX.GetOffsetForValue(HoveredSample.Sample->LargestFrameIndex);
			float CX = X1 + FMath::RoundToFloat(Viewport.GetSampleWidth() / 2);
			if (CX + W2 > ViewportX.GetSize())
			{
				CX = FMath::RoundToFloat(ViewportX.GetSize() - W2);
			}
			if (CX - W2 < 0)
			{
				CX = W2;
			}

			const float Y = 10.0f;
			const float H = 14.0f;
			DrawContext.DrawBox(CX - W2, Y, 2 * W2, H, WhiteBrush, FLinearColor(0.7, 0.7, 0.7, TooltipOpacity));
			DrawContext.LayerId++;
			DrawContext.DrawText(CX - W2 + DX, Y + 1.0f, Text, SummaryFont, FLinearColor(0.0, 0.0, 0.0, TooltipOpacity));
			DrawContext.LayerId++;
		}

		TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
		if (Window)
		{
			TSharedPtr<STimingView> TimingView = Window->GetTimingView();
			if (TimingView)
			{
				TSharedPtr<FFrameTrackSeries> SeriesPtr = nullptr;
				for (const TPair<int32, TSharedPtr<FFrameTrackSeries>>& KeyValuePair : SeriesMap)
				{
					SeriesPtr = KeyValuePair.Value;
					break; // stop at first enumerated Series
				}
				if (SeriesPtr.IsValid())
				{
					// Highlight the area corresponding to viewport of Timing View.
					const double StartTime = TimingView->GetViewport().GetStartTime();
					const double EndTime = TimingView->GetViewport().GetEndTime();
					Helper.DrawHighlightedInterval(*SeriesPtr, StartTime, EndTime);
				}
			}
		}

		Stopwatch.Stop();
		DrawDurationHistory.AddValue(Stopwatch.AccumulatedTime);
	}
	//////////////////////////////////////////////////

	const bool bShouldDisplayDebugInfo = FInsightsManager::Get()->IsDebugInfoEnabled();
	if (bShouldDisplayDebugInfo)
	{
		const float MaxFontCharHeight = FontMeasureService->Measure(TEXT("!"), SummaryFont).Y;
		const float DbgDY = MaxFontCharHeight;

		const float DbgW = 280.0f;
		const float DbgH = DbgDY * 4 + 3.0f;
		const float DbgX = ViewWidth - DbgW - 20.0f;
		float DbgY = 7.0f;

		DrawContext.LayerId++;

		DrawContext.DrawBox(DbgX - 2.0f, DbgY - 2.0f, DbgW, DbgH, WhiteBrush, FLinearColor(1.0, 1.0, 1.0, 0.9));
		DrawContext.LayerId++;

		FLinearColor DbgTextColor(0.0, 0.0, 0.0, 0.9);

		// Time interval since last OnPaint call.
		const uint64 CurrentTime = FPlatformTime::Cycles64();
		const uint64 OnPaintDuration = CurrentTime - LastOnPaintTime;
		LastOnPaintTime = CurrentTime;
		OnPaintDurationHistory.AddValue(OnPaintDuration); // saved for last 32 OnPaint calls
		const uint64 AvgOnPaintDuration = OnPaintDurationHistory.ComputeAverage();
		const uint64 AvgOnPaintDurationMs = FStopwatch::Cycles64ToMilliseconds(AvgOnPaintDuration);
		const double AvgOnPaintFps = AvgOnPaintDurationMs != 0 ? 1.0 / FStopwatch::Cycles64ToSeconds(AvgOnPaintDuration) : 0.0;

		const uint64 AvgUpdateDurationMs = FStopwatch::Cycles64ToMilliseconds(UpdateDurationHistory.ComputeAverage());
		const uint64 AvgDrawDurationMs = FStopwatch::Cycles64ToMilliseconds(DrawDurationHistory.ComputeAverage());

		// Draw performance info.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U: %llu ms, D: %llu ms + %llu ms = %llu ms (%d fps)"),
				AvgUpdateDurationMs, // caching time
				AvgDrawDurationMs, // drawing time
				AvgOnPaintDurationMs - AvgDrawDurationMs, // other overhead to OnPaint calls
				AvgOnPaintDurationMs, // average time between two OnPaint calls
				FMath::RoundToInt(AvgOnPaintFps)), // framerate of OnPaint calls
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw number of draw calls.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U: %s frames, D: %s samples"),
				*FText::AsNumber(NumUpdatedFrames).ToString(),
				*FText::AsNumber(NumDrawSamples).ToString()),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw viewport's horizontal info.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			Viewport.GetHorizontalAxisViewport().ToDebugString(TEXT("X"), TEXT("frame")),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw viewport's vertical info.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			Viewport.GetVerticalAxisViewport().ToDebugString(TEXT("Y")),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::DrawVerticalAxisGrid(FDrawContext& DrawContext, const FSlateBrush* Brush, const FSlateFontInfo& Font) const
{
	const float ViewWidth = Viewport.GetWidth();

	const FAxisViewportDouble& ViewportY = Viewport.GetVerticalAxisViewport();
	const float RoundedViewHeight = FMath::RoundToFloat(ViewportY.GetSize());

	constexpr float TextH = 14.0f;
	constexpr float MinDY = 12.0f; // min vertical distance between horizontal grid lines

	const FLinearColor GridColor(0.0f, 0.0f, 0.0f, 0.1f);
	const FLinearColor TextBgColor(0.05f, 0.05f, 0.05f, 1.0f);
	//const FLinearColor TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const double GridValues[] =
	{
		0.0,
		1.0 / 200.0, //    5 ms (200 fps)
		1.0 / 60.0,  // 16.7 ms (60 fps)
		1.0 / 30.0,  // 33.3 ms (30 fps)
		1.0 / 20.0,  //   50 ms (20 fps)
		1.0 / 15.0,  // 66.7 ms (15 fps)
		1.0 / 10.0,  //  100 ms (10 fps)
		1.0 / 5.0,   //  200 ms (5 fps)
		1.0,   // 1s
		10.0,  // 10s
		60.0,  // 1m
		600.0, // 10m
		3600.0 // 1h
	};
	constexpr int32 NumGridValues = sizeof(GridValues) / sizeof(double);

	double PreviousY = -MinDY;

	for (int32 Index = 0; Index < NumGridValues; ++Index)
	{
		const double Value = GridValues[Index];

		constexpr double Time60fps = 1.0 / 60.0;
		constexpr double Time30fps = 1.0 / 30.0;

		FLinearColor TextColor;
		if (Value <= Time60fps)
		{
			TextColor = FLinearColor(0.5f, 1.0f, 0.5f, 1.0f);
		}
		else if (Value <= Time30fps)
		{
			TextColor = FLinearColor(1.0f, 1.0f, 0.5f, 1.0f);
			//const float U = (Value - Time60fps) * 0.5 / (Time30fps - Time60fps);
			//TextColor = FLinearColor(0.5f + U, 1.0f - U, 0.5f, 1.0f);
		}
		else
		{
			TextColor = FLinearColor(1.0f, 0.5f, 0.5f, 1.0f);
		}

		const float Y = RoundedViewHeight - FMath::RoundToFloat(ViewportY.GetOffsetForValue(Value));
		if (Y < 0)
		{
			break;
		}
		if (Y > RoundedViewHeight + TextH || FMath::Abs(PreviousY - Y) < MinDY)
		{
			continue;
		}
		PreviousY = Y;

		// Draw horizontal grid line.
		DrawContext.DrawBox(0, Y, ViewWidth, 1, Brush, GridColor);

		const FString Text = (Value == 0.0) ? TEXT("0") :
							 (Value <= 1.0) ? FString::Printf(TEXT("%s (%.0f fps)"), *TimeUtils::FormatTimeAuto(Value), 1.0 / Value) :
											  TimeUtils::FormatTimeAuto(Value);
		const FVector2D TextSize = FontMeasureService->Measure(Text, Font);

		// Draw background for value text.
		DrawContext.DrawBox(ViewWidth - TextSize.X - 4.0f, Y - TextH, TextSize.X + 4.0f, TextH, Brush, TextBgColor);

		// Draw value text.
		DrawContext.DrawText(ViewWidth - TextSize.X - 2.0f, Y - TextH + 1.0f, Text, Font, TextColor);
	}
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::DrawHorizontalAxisGrid(FDrawContext& DrawContext, const FSlateBrush* Brush, const FSlateFontInfo& Font) const
{
	const FAxisViewportInt32& ViewportX = Viewport.GetHorizontalAxisViewport();

	const float RoundedViewWidth = FMath::RoundToFloat(ViewportX.GetSize());

	constexpr float MinDX = 125.0f; // min horizontal distance between vertical grid lines

	const int32 LeftIndex = ViewportX.GetValueAtOffset(0.0f);
	const int32 GridIndex = ViewportX.GetValueAtOffset(MinDX);
	const int32 RightIndex = ViewportX.GetValueAtOffset(RoundedViewWidth);
	const int32 Delta = GridIndex - LeftIndex;

	if (Delta > 0)
	{
		// Compute rounding based on magnitude of visible range of samples (Delta).
		int32 Power10 = 1;
		int32 Delta10 = Delta;
		while (Delta10 > 0)
		{
			Delta10 /= 10;
			Power10 *= 10;
		}
		if (Power10 >= 100)
		{
			Power10 /= 100;
		}
		else
		{
			Power10 = 1;
		}

		const int32 Grid = ((Delta + Power10 - 1) / Power10) * Power10; // next value divisible with a multiple of 10

		// Skip grid lines for negative indices.
		double StartIndex = ((LeftIndex + Grid - 1) / Grid) * Grid;
		while (StartIndex < 0)
		{
			StartIndex += Grid;
		}

		const float ViewHeight = Viewport.GetHeight();

		const FLinearColor GridColor(0.0f, 0.0f, 0.0f, 0.1f);
		//const FLinearColor TextBgColor(0.05f, 0.05f, 0.05f, 1.0f);
		//const FLinearColor TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		const FLinearColor TopTextColor(1.0f, 1.0f, 1.0f, 0.7f);

		//const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		for (int32 Index = StartIndex; Index < RightIndex; Index += Grid)
		{
			const float X = FMath::RoundToFloat(ViewportX.GetOffsetForValue(Index));

			// Draw vertical grid line.
			DrawContext.DrawBox(X, 0, 1, ViewHeight, Brush, GridColor);

			const FString Text = FText::AsNumber(Index).ToString();
			//const FVector2D TextSize = FontMeasureService->Measure(Text, Font);
			//constexpr float TextH = 14.0f;

			// Draw background for index text.
			//DrawContext.DrawBox(X, ViewHeight - TextH, TextSize.X + 4.0f, TextH, Brush, TextBgColor);

			// Draw index text.
			//DrawContext.DrawText(X + 2.0f, ViewHeight - TextH + 1.0f, Text, Font, TextColor);

			DrawContext.DrawText(X + 2.0f, 10.0f, Text, Font, TopTextColor);
		}
		DrawContext.LayerId++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonDown = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	ViewportPosXOnButtonDown = Viewport.GetHorizontalAxisViewport().GetPos();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsLMB_Pressed = true;

		// Capture mouse.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bIsRMB_Pressed = true;

		// Capture mouse, so we can scroll outside this widget.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePositionOnButtonUp = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	const bool bIsValidForMouseClick = MousePositionOnButtonUp.Equals(MousePositionOnButtonDown, MOUSE_SNAP_DISTANCE);

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bIsLMB_Pressed)
		{
			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = ECursorType::Default;
			}
			else if (bIsValidForMouseClick)
			{
				SelectFrameAtMousePosition(MousePositionOnButtonUp.X, MousePositionOnButtonUp.Y);
			}

			bIsLMB_Pressed = false;

			// Release the mouse.
			Reply = FReply::Handled().ReleaseMouseCapture();
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (bIsRMB_Pressed)
		{
			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = ECursorType::Default;
			}
			else if (bIsValidForMouseClick)
			{
				ShowContextMenu(MouseEvent);
			}

			bIsRMB_Pressed = false;

			// Release mouse as we no longer scroll.
			Reply = FReply::Handled().ReleaseMouseCapture();
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (!MouseEvent.GetCursorDelta().IsZero())
	{
		if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) ||
			MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
		{
			if (HasMouseCapture())
			{
				if (!bIsScrolling)
				{
					bIsScrolling = true;
					CursorType = ECursorType::Hand;

					HoveredSample.Reset();
				}

				FAxisViewportInt32& ViewportX = Viewport.GetHorizontalAxisViewport();
				const float PosX = ViewportPosXOnButtonDown + (MousePositionOnButtonDown.X - MousePosition.X);
				ViewportX.ScrollAtValue(ViewportX.GetValueAtPos(PosX)); // align viewport position with sample (frame index)
				UpdateHorizontalScrollBar();
				bIsStateDirty = true;
			}
		}
		else
		{
			if (!HoveredSample.IsValid())
			{
				TooltipOpacity = 0.0f;
			}
			HoveredSample = GetSampleAtMousePosition(MousePosition.X, MousePosition.Y);
			if (!HoveredSample.IsValid())
			{
				HoveredSample = GetSampleAtMousePosition(MousePosition.X - 1.0f, MousePosition.Y);
			}
			if (!HoveredSample.IsValid())
			{
				HoveredSample = GetSampleAtMousePosition(MousePosition.X + 1.0f, MousePosition.Y);
			}
		}

		Reply = FReply::Handled();
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		bIsLMB_Pressed = false;
		bIsRMB_Pressed = false;

		HoveredSample.Reset();

		CursorType = ECursorType::Default;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.GetModifierKeys().IsShiftDown())
	{
		FAxisViewportDouble& ViewportY = Viewport.GetVerticalAxisViewport();

		// Zoom in/out vertically.
		const float Delta = MouseEvent.GetWheelDelta();
		constexpr float ZoomStep = 0.25f; // as percent
		float ScaleY;

		if (Delta > 0)
		{
			ScaleY = ViewportY.GetScale() * FMath::Pow(1.0f + ZoomStep, Delta);
		}
		else
		{
			ScaleY = ViewportY.GetScale() * FMath::Pow(1.0f / (1.0f + ZoomStep), -Delta);
		}

		ViewportY.SetScale(ScaleY);
		//UpdateVerticalScrollBar();
	}
	else //if (MouseEvent.GetModifierKeys().IsControlDown())
	{
		// Zoom in/out horizontally.
		const float Delta = MouseEvent.GetWheelDelta();
		ZoomHorizontally(Delta, MousePosition.X);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::ZoomHorizontally(const float Delta, const float X)
{
	FAxisViewportInt32& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.RelativeZoomWithFixedOffset(Delta, X);
	ViewportX.ScrollAtValue(ViewportX.GetValueAtPos(ViewportX.GetPos())); // align viewport position with sample (frame index)
	UpdateHorizontalScrollBar();
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFrameTrack::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FCursorReply SFrameTrack::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FCursorReply CursorReply = FCursorReply::Unhandled();

	if (CursorType == ECursorType::Arrow)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	else if (CursorType == ECursorType::Hand)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	return CursorReply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::ShowContextMenu(const FPointerEvent& MouseEvent)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, NULL);

	MenuBuilder.BeginSection("Series", LOCTEXT("ContextMenu_Header_Series", "Series"));
	{
		struct FLocal
		{
			static bool ReturnFalse()
			{
				return false;
			}
		};

		FUIAction Action_ShowGameFrames
		(
			FExecuteAction::CreateSP(this, &SFrameTrack::ContextMenu_ShowGameFrames_Execute),
			FCanExecuteAction::CreateSP(this, &SFrameTrack::ContextMenu_ShowGameFrames_CanExecute),
			FIsActionChecked::CreateSP(this, &SFrameTrack::ContextMenu_ShowGameFrames_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_ShowGameFrames", "Game Frames"),
			LOCTEXT("ContextMenu_ShowGameFrames_Desc", "Show/hide the Game frames"),
			FSlateIcon(),
			Action_ShowGameFrames,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		FUIAction Action_ShowRenderingFrames
		(
			FExecuteAction::CreateSP(this, &SFrameTrack::ContextMenu_ShowRenderingFrames_Execute),
			FCanExecuteAction::CreateSP(this, &SFrameTrack::ContextMenu_ShowRenderingFrames_CanExecute),
			FIsActionChecked::CreateSP(this, &SFrameTrack::ContextMenu_ShowRenderingFrames_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_ShowRenderingFrames", "Rendering Frames"),
			LOCTEXT("ContextMenu_ShowRenderingFrames_Desc", "Show/hide the Rendering frames"),
			FSlateIcon(),
			Action_ShowRenderingFrames,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Misc");
	{
		FUIAction Action_AutoZoom
		(
			FExecuteAction::CreateSP(this, &SFrameTrack::ContextMenu_AutoZoom_Execute),
			FCanExecuteAction::CreateSP(this, &SFrameTrack::ContextMenu_AutoZoom_CanExecute),
			FIsActionChecked::CreateSP(this, &SFrameTrack::ContextMenu_AutoZoom_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_AutoZoom", "Auto Zoom"),
			LOCTEXT("ContextMenu_AutoZoom_Desc", "Enable auto zoom. Makes entire session time range to fit into view."),
			FSlateIcon(),
			Action_AutoZoom,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	TSharedRef<SWidget> MenuWidget = MenuBuilder.MakeWidget();

	FWidgetPath EventPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	FSlateApplication::Get().PushMenu(SharedThis(this), EventPath, MenuWidget, ScreenSpacePosition, FPopupTransitionEffect::ContextMenu);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::ContextMenu_ShowGameFrames_Execute()
{
	bShowGameFrames = !bShowGameFrames;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SFrameTrack::ContextMenu_ShowGameFrames_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SFrameTrack::ContextMenu_ShowGameFrames_IsChecked()
{
	return bShowGameFrames;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::ContextMenu_ShowRenderingFrames_Execute()
{
	bShowRenderingFrames = !bShowRenderingFrames;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SFrameTrack::ContextMenu_ShowRenderingFrames_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SFrameTrack::ContextMenu_ShowRenderingFrames_IsChecked()
{
	return bShowRenderingFrames;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::ContextMenu_AutoZoom_Execute()
{
	FAxisViewportInt32& ViewportX = Viewport.GetHorizontalAxisViewport();

	bIsAutoZoomEnabled = !(bIsAutoZoomEnabled && ViewportX.GetPos() == 0.0f);
	if (bIsAutoZoomEnabled)
	{
		ViewportX.ScrollAtPos(0.0f);

		// Auto zoom in.
		while (ViewportX.GetMaxPos() - ViewportX.GetMinPos() < ViewportX.GetSize())
		{
			ZoomHorizontally(+0.1f, 0.0f);
			ViewportX.ScrollAtPos(0.0f);
		}

		// Auto zoom out (until entire session time range fits into view).
		while (ViewportX.GetMaxPos() - ViewportX.GetMinPos() > ViewportX.GetSize())
		{
			ZoomHorizontally(-0.1f, 0.0f);
			ViewportX.ScrollAtPos(0.0f);
		}

		UpdateHorizontalScrollBar();
		bIsStateDirty = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SFrameTrack::ContextMenu_AutoZoom_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SFrameTrack::ContextMenu_AutoZoom_IsChecked()
{
	return bIsAutoZoomEnabled && Viewport.GetHorizontalAxisViewport().GetPos() == 0.0f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::BindCommands()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::HorizontalScrollBar_OnUserScrolled(float ScrollOffset)
{
	Viewport.GetHorizontalAxisViewport().OnUserScrolled(HorizontalScrollBar, ScrollOffset);
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFrameTrack::UpdateHorizontalScrollBar()
{
	Viewport.GetHorizontalAxisViewport().UpdateScrollBar(HorizontalScrollBar);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
