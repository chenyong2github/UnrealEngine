// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FrameTrackHelper.h"

#include "Brushes/SlateBorderBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "EditorStyleSet.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/ViewModels/FrameTrackViewport.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFrameTrackTimelineBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FFrameTrackTimelineBuilder::FFrameTrackTimelineBuilder(FFrameTrackTimeline& InTimeline, const FFrameTrackViewport& InViewport)
	: Timeline(InTimeline)
	, Viewport(InViewport)
	, NumAddedFrames(0)
{
	SampleW = Viewport.GetSampleWidth();
	FramesPerSample = Viewport.GetNumFramesPerSample();
	NumSamples = FMath::Max(0, FMath::CeilToInt(Viewport.Width / SampleW));
	FirstFrameIndex = Viewport.GetFirstFrameIndex();

	Timeline.NumAggregatedFrames = 0;
	Timeline.Samples.Reset();
	Timeline.Samples.AddDefaulted(NumSamples);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackTimelineBuilder::AddFrame(const Trace::FFrame& Frame)
{
	NumAddedFrames++;

	const int32 FrameIndex = Frame.Index;

	int32 SampleIndex = (FrameIndex - FirstFrameIndex) / FramesPerSample;
	if (SampleIndex >= 0 && SampleIndex < NumSamples)
	{
		FFrameTrackSample& Sample = Timeline.Samples[SampleIndex];
		Sample.NumFrames++;

		double Duration = Frame.EndTime - Frame.StartTime;
		Sample.TotalDuration += Duration;
		if (Frame.StartTime < Sample.StartTime)
		{
			Sample.StartTime = Frame.StartTime;
		}
		if (Frame.EndTime > Sample.EndTime)
		{
			Sample.EndTime = Frame.EndTime;
		}
		if (Duration > Sample.LargestFrameDuration)
		{
			Sample.LargestFrameIndex = FrameIndex;
			Sample.LargestFrameStartTime = Frame.StartTime;
			Sample.LargestFrameDuration = Duration;
		}

		Timeline.NumAggregatedFrames++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFrameTrackDrawHelper
////////////////////////////////////////////////////////////////////////////////////////////////////

FFrameTrackDrawHelper::FFrameTrackDrawHelper(const FDrawContext& InDrawContext, const FFrameTrackViewport& InViewport)
	: DrawContext(InDrawContext)
	, Viewport(InViewport)
	, WhiteBrush(FCoreStyle::Get().GetBrush("WhiteBrush"))
	, BorderBrush(FEditorStyle::GetBrush("PlainBorder"))
	, NumFrames(0)
	, NumDrawSamples(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackDrawHelper::DrawBackground() const
{
	const FSlateBrush* AreaBrush = WhiteBrush;
	const FLinearColor ValidAreaColor(0.07f, 0.07f, 0.07f, 1.0f);
	const FLinearColor InvalidAreaColor(0.1f, 0.07f, 0.07f, 1.0f);

	float X0 = Viewport.MinX - Viewport.PosX;
	float X1 = Viewport.MaxX - Viewport.PosX;
	const float W = FMath::CeilToFloat(Viewport.Width);
	const float H = FMath::CeilToFloat(Viewport.Height);

	if (X0 >= W || X1 <= 0.0f)
	{
		// Draw invalid area (entire view).
		DrawContext.DrawBox(0.0f, 0.0f, W, H, AreaBrush, InvalidAreaColor);
	}
	else // X0 < W && X1 > 0
	{
		if (X0 > 0.0f)
		{
			// Draw invalid area (left).
			DrawContext.DrawBox(0.0f, 0.0f, X0, H, AreaBrush, InvalidAreaColor);
		}

		if (X1 < W)
		{
			// Draw invalid area (right).
			DrawContext.DrawBox(X1, 0.0f, W - X1, H, AreaBrush, InvalidAreaColor);
		}

		X0 = FMath::Max(X0, 0.0f);
		X1 = FMath::Min(X1, W);

		if (X1 > X0)
		{
			// Draw valid area.
			DrawContext.DrawBox(X0, 0.0f, X1 - X0, H, AreaBrush, ValidAreaColor);
		}
	}

	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FFrameTrackDrawHelper::GetColorById(int32 Id) const
{
	const float Alpha = 0.9;
	switch (Id)
	{
	case 0: // Game Frames
		return FLinearColor(0.75, 1.0, 1.0, Alpha);

	case 1: // Rendering Frames
		return FLinearColor(1.0, 0.75, 0.75, Alpha);

	case 2:
		return FLinearColor(0.75, 0.75, 1.0, Alpha);

	default:
		return FLinearColor(1.0, 1.0, 1.0, Alpha);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackDrawHelper::DrawCached(const FFrameTrackTimeline& Timeline) const
{
	if (Timeline.NumAggregatedFrames == 0)
	{
		return;
	}

	NumFrames += Timeline.NumAggregatedFrames;

	FLinearColor TimelineColor = GetColorById(static_cast<int32>(Timeline.Id));

	const float SampleW = Viewport.GetSampleWidth();
	const int32 NumSamples = Timeline.Samples.Num();

	const float Y1 = FMath::RoundToFloat(Viewport.GetViewportYForValue(0.0));

	for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFrameTrackSample& Sample = Timeline.Samples[SampleIndex];
		if (Sample.NumFrames == 0)
		{
			continue;
		}

		NumDrawSamples++;

		const float X = SampleIndex * SampleW;
		float Y2;

		FLinearColor ColorFill = TimelineColor;

		if (Sample.LargestFrameDuration == std::numeric_limits<double>::infinity())
		{
			Y2 = Viewport.Height;
			ColorFill.R = 0.0f;
			ColorFill.G = 0.0f;
			ColorFill.B = 0.0f;
		}
		else
		{
			Y2 = FMath::RoundToFloat(Viewport.GetViewportYForValue(Sample.LargestFrameDuration));
			if (Sample.LargestFrameDuration > 1.0 / 30.0)
			{
				ColorFill.G *= 0.5f;
				ColorFill.B *= 0.5f;
			}
			else if (Sample.LargestFrameDuration > 1.0 / 60.0)
			{
				ColorFill.B *= 0.5f;
			}
		}

		const float Y = FMath::RoundToFloat(Viewport.Height) - Y2;
		const float H = Y2 - Y1;

		const FLinearColor ColorBorder(ColorFill.R * 0.75f, ColorFill.G * 0.75f, ColorFill.B * 0.75f, 1.0);

		if (SampleW > 2.0f)
		{
			DrawContext.DrawBox(X + 1.0f, Y + 1.0f, SampleW - 2.0f, H - 2.0f, WhiteBrush, ColorFill);

			// Draw border.
			DrawContext.DrawBox(X, Y, 1.0, H, WhiteBrush, ColorBorder);
			DrawContext.DrawBox(X + SampleW - 1.0f, Y, 1.0, H, WhiteBrush, ColorBorder);
			DrawContext.DrawBox(X + 1.0f, Y, SampleW - 2.0f, 1.0f, WhiteBrush, ColorBorder);
			DrawContext.DrawBox(X + 1.0f, Y + H - 1.0f, SampleW - 2.0f, 1.0f, WhiteBrush, ColorBorder);
		}
		else
		{
			DrawContext.DrawBox(X, Y, SampleW, H, WhiteBrush, ColorBorder);
		}
	}

	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackDrawHelper::DrawHoveredSample(const FFrameTrackSample& Sample) const
{
	const float SampleW = Viewport.GetSampleWidth();
	const int32 FramesPerSample = Viewport.GetNumFramesPerSample();
	const int32 FirstFrameIndex = Viewport.GetFirstFrameIndex();
	const int32 SampleIndex = (Sample.LargestFrameIndex - FirstFrameIndex) / FramesPerSample;
	const float X = SampleIndex * SampleW;

	const float Y1 = FMath::RoundToFloat(Viewport.GetViewportYForValue(0.0));
	float Y2;
	if (Sample.LargestFrameDuration == std::numeric_limits<double>::infinity())
	{
		Y2 = Viewport.Height;
	}
	else
	{
		Y2 = FMath::RoundToFloat(Viewport.GetViewportYForValue(Sample.LargestFrameDuration));
	}
	const float Y = FMath::RoundToFloat(Viewport.Height) - Y2;
	const float H = Y2 - Y1;

	const FLinearColor ColorBorder(1.0f, 1.0f, 0.0f, 1.0);
	DrawContext.DrawBox(X - 1.0f, Y - 1.0f, SampleW + 2.0f, H + 2.0f, BorderBrush, ColorBorder);
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackDrawHelper::DrawHighlightedInterval(const FFrameTrackTimeline& Timeline, const double StartTime, const double EndTime) const
{
	const int32 NumSamples = Timeline.Samples.Num();

	//TODO: binary search
	int32 Index1 = 0;
	int32 Index2 = NumSamples - 1;
	while (Index1 < NumSamples && Timeline.Samples[Index1].EndTime < StartTime)
	{
		Index1++;
	}
	while (Index2 >= Index1 && Timeline.Samples[Index2].StartTime > EndTime)
	{
		Index2--;
	}

	if (Index1 <= Index2)
	{
		const float SampleW = Viewport.GetSampleWidth();
		float X1 = Index1 * SampleW;
		float X2 = Index2 * SampleW;

		constexpr float Y1 = 12.0f; // allows 12px for the horizontal scrollbar (one displayed on top of the track)
		const float Y2 = Viewport.Height;
		constexpr float D = 2.0f; // line thickness (for both horizontal and vertical lines)
		constexpr float H = 10.0f; // height of corner lines

		const FLinearColor Color(1.0f, 1.0f, 1.0f, 1.0f);

		if (X1 >= 0.0f && X1 < Viewport.Width - 2.0f)
		{
			// Draw left side vertical lines.
			DrawContext.DrawBox(X1 - D, Y1, D, H, WhiteBrush, Color);
			DrawContext.DrawBox(X1 - D, Y2 - H, D, H, WhiteBrush, Color);
		}

		if (X2 >= -2.0f && X2 < Viewport.Width)
		{
			// Draw right side vertical lines.
			DrawContext.DrawBox(X2, Y1, D, H, WhiteBrush, Color);
			DrawContext.DrawBox(X2, Y2 - H, D, H, WhiteBrush, Color);
		}

		if (X1 < 0)
		{
			X1 = 0.0f;
		}
		if (X2 > Viewport.Width)
		{
			X2 = Viewport.Width;
		}
		if (X1 < X2)
		{
			// Draw horizontal lines.
			DrawContext.DrawBox(X1, Y1, X2 - X1, D, WhiteBrush, Color);
			DrawContext.DrawBox(X1, Y2 - D, X2 - X1, D, WhiteBrush, Color);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
