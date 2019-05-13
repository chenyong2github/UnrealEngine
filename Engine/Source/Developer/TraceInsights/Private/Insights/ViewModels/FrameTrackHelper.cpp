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
// FFrameTrackCacheContext
////////////////////////////////////////////////////////////////////////////////////////////////////

FFrameTrackCacheContext::FFrameTrackCacheContext(const FFrameTrackViewport& InViewport)
	: Viewport(InViewport)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackCacheContext::Begin()
{
	SampleW = Viewport.GetSampleWidth();
	FramesPerSample = Viewport.GetNumFramesPerSample();
	NumSamples = FMath::CeilToInt(Viewport.Width / SampleW);
	FirstFrameIndex = Viewport.GetFirstFrameIndex();

	NumFrames = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FFrameTrackCacheContext::BeginTimeline(FFrameTrackTimeline& CachedTimeline)
{
	CurrentCachedTimeline = &CachedTimeline;

	CurrentCachedTimeline->NumAggregatedFrames = 0;
	CurrentCachedTimeline->Samples.Reset(NumSamples);
	CurrentCachedTimeline->Samples.AddDefaulted(NumSamples);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackCacheContext::AddEvent(const Trace::FFrame& Event)
{
	NumFrames++;

	const int32 FrameIndex = Event.Index;

	int SampleIndex = (FrameIndex - FirstFrameIndex) / FramesPerSample;
	if (SampleIndex >= 0 && SampleIndex < NumSamples)
	{
		FFrameTrackSample& Sample = CurrentCachedTimeline->Samples[SampleIndex];
		Sample.NumFrames++;

		double Duration = Event.EndTime - Event.StartTime;
		Sample.TotalDuration += Duration;
		if (Event.StartTime < Sample.StartTime)
		{
			Sample.StartTime = Event.StartTime;
		}
		if (Event.EndTime > Sample.EndTime)
		{
			Sample.EndTime = Event.EndTime;
		}
		if (Duration > Sample.LargestFrameDuration)
		{
			Sample.LargestFrameIndex = FrameIndex;
			Sample.LargestFrameStartTime = Event.StartTime;
			Sample.LargestFrameDuration = Duration;
		}

		CurrentCachedTimeline->NumAggregatedFrames++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackCacheContext::EndTimeline()
{
	//CurrentCachedTimeline = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackCacheContext::End()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFrameTrackDrawContext
////////////////////////////////////////////////////////////////////////////////////////////////////

FFrameTrackDrawContext::FFrameTrackDrawContext(const FFrameTrackViewport& InViewport, const FGeometry& InAllottedGeometry, int32& InLayerId, FSlateWindowElementList& InElementList)
	: Viewport(InViewport)
	, AllottedGeometry(InAllottedGeometry)
	, LayerId(InLayerId)
	, ElementList(InElementList)
	, DrawEffects(ESlateDrawEffect::None)
	, WhiteBrush(FCoreStyle::Get().GetBrush("WhiteBrush"))
	, BorderBrush(FEditorStyle::GetBrush("PlainBorder"))
	, NumFrames(0)
	, NumDrawSamples(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackDrawContext::DrawBackground(const FWidgetStyle& InWidgetStyle) const
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
		FSlateDrawElement::MakeBox
		(
			ElementList,
			LayerId,
			MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, 0.0f, 0.0f, W, H),
			AreaBrush,
			DrawEffects,
			InvalidAreaColor
		);
	}
	else // X0 < W && X1 > 0
	{
		if (X0 > 0.0f)
		{
			// Draw invalid area (left).
			FSlateDrawElement::MakeBox
			(
				ElementList,
				LayerId,
				MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, 0.0f, 0.0f, X0, H),
				AreaBrush,
				DrawEffects,
				InvalidAreaColor
			);
		}

		if (X1 < W)
		{
			// Draw invalid area (right).
			FSlateDrawElement::MakeBox
			(
				ElementList,
				LayerId,
				MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X1, 0.0f, W - X1, H),
				AreaBrush,
				DrawEffects,
				InvalidAreaColor
			);
		}

		X0 = FMath::Max(X0, 0.0f);
		X1 = FMath::Min(X1, W);

		if (X1 > X0)
		{
			// Draw valid area.
			FSlateDrawElement::MakeBox
			(
				ElementList,
				LayerId,
				MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X0, 0.0f, X1 - X0, H),
				AreaBrush,
				DrawEffects,
				ValidAreaColor
			);
		}
	}

	LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FFrameTrackDrawContext::GetColorById(int Id) const
{
	const float Alpha = 0.9;
	switch (Id)
	{
	case 0: // Game Frames
		return FLinearColor(0.75, 1.0, 1.0, Alpha);

	case 1: // Render Frames
		return FLinearColor(1.0, 0.75, 0.75, Alpha);

	case 2:
		return FLinearColor(0.75, 0.75, 1.0, Alpha);

	default:
		return FLinearColor(1.0, 1.0, 1.0, Alpha);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackDrawContext::DrawCached(const FFrameTrackTimeline& CachedTimeline) const
{
	if (CachedTimeline.NumAggregatedFrames == 0)
	{
		return;
	}

	NumFrames += CachedTimeline.NumAggregatedFrames;

	FLinearColor TimelineColor = GetColorById(CachedTimeline.Id);

	const float SampleW = Viewport.GetSampleWidth();
	const int NumSamples = CachedTimeline.Samples.Num();

	const float VY1 = FMath::RoundToFloat(Viewport.GetViewportYForValue(0.0));

	for (int SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
	{
		const FFrameTrackSample& Sample = CachedTimeline.Samples[SampleIndex];
		if (Sample.NumFrames == 0)
		{
			continue;
		}

		NumDrawSamples++;

		const float X = SampleIndex * SampleW;
		float VY2;

		FLinearColor ColorFill = TimelineColor;

		if (Sample.LargestFrameDuration == std::numeric_limits<double>::infinity())
		{
			VY2 = Viewport.Height;
			ColorFill.R = 0.0f;
			ColorFill.G = 0.0f;
			ColorFill.B = 0.0f;
		}
		else
		{
			VY2 = FMath::RoundToFloat(Viewport.GetViewportYForValue(Sample.LargestFrameDuration));
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

		const float Y = FMath::RoundToFloat(Viewport.Height) - VY2;
		const float H = VY2 - VY1;

		const FLinearColor ColorBorder(ColorFill.R * 0.75f, ColorFill.G * 0.75f, ColorFill.B * 0.75f, 1.0);

		if (SampleW > 2.0f)
		{
			FPaintGeometry GeoFill = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X + 1.0f, Y + 1.0f, SampleW - 2.0f, H - 2.0f);
			FSlateDrawElement::MakeBox(ElementList, LayerId, GeoFill, WhiteBrush, DrawEffects, ColorFill);

			//FPaintGeometry GeoBorder = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X, Y, SampleW, H);
			//FSlateDrawElement::MakeBox(ElementList, LayerId+1, GeoBorder, BorderBrush, DrawEffects, ColorBorder);

			#define LOCAL_DRAW_RC(x, y, w, h) FSlateDrawElement::MakeBox(ElementList, LayerId, MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, x, y, w, h), WhiteBrush, DrawEffects, ColorBorder)
			LOCAL_DRAW_RC(X, Y, 1.0, H);
			LOCAL_DRAW_RC(X + SampleW - 1.0f, Y, 1.0, H);
			LOCAL_DRAW_RC(X + 1.0f, Y, SampleW - 2.0f, 1.0f);
			LOCAL_DRAW_RC(X + 1.0f, Y + H - 1.0f, SampleW - 2.0f, 1.0f);
			#undef DRAW_RC
		}
		else
		{
			FPaintGeometry GeoBorder = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X, Y, SampleW, H);
			FSlateDrawElement::MakeBox(ElementList, LayerId, GeoBorder, WhiteBrush, DrawEffects, ColorBorder);
		}
	}

	LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackDrawContext::DrawHoveredSample(const FFrameTrackSample& Sample) const
{
	const float SampleW = Viewport.GetSampleWidth();
	const int32 FramesPerSample = Viewport.GetNumFramesPerSample();
	const int32 FirstFrameIndex = Viewport.GetFirstFrameIndex();
	const int SampleIndex = (Sample.LargestFrameIndex - FirstFrameIndex) / FramesPerSample;
	const float X = SampleIndex * SampleW;

	const float VY1 = FMath::RoundToFloat(Viewport.GetViewportYForValue(0.0));
	float VY2;
	if (Sample.LargestFrameDuration == std::numeric_limits<double>::infinity())
	{
		VY2 = Viewport.Height;
	}
	else
	{
		VY2 = FMath::RoundToFloat(Viewport.GetViewportYForValue(Sample.LargestFrameDuration));
	}
	const float Y = FMath::RoundToFloat(Viewport.Height) - VY2;
	const float H = VY2 - VY1;

	const FLinearColor ColorBorder(1.0f, 1.0f, 0.0f, 1.0);
	FPaintGeometry GeoBorder = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X - 1.0f, Y - 1.0f, SampleW + 2.0f, H + 2.0f);
	FSlateDrawElement::MakeBox(ElementList, LayerId, GeoBorder, BorderBrush, DrawEffects, ColorBorder);

	LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFrameTrackDrawContext::DrawHighlightedInterval(const FFrameTrackTimeline& CachedTimeline, const double StartTime, const double EndTime) const
{
	const int NumSamples = CachedTimeline.Samples.Num();

	//TODO: binary search
	int Index1 = 0;
	int Index2 = NumSamples - 1;
	while (Index1 < NumSamples && CachedTimeline.Samples[Index1].EndTime < StartTime)
		Index1++;
	while (Index2 >= Index1 && CachedTimeline.Samples[Index2].StartTime > EndTime)
		Index2--;

	if (Index1 <= Index2)
	{
		const float SampleW = Viewport.GetSampleWidth();
		float X1 = Index1 * SampleW;
		float X2 = Index2 * SampleW;

		const float Y1 = 10.0f;
		const float Y2 = Viewport.Height;
		const float D = 2.0f;
		const float H = 10.0f;

		const FLinearColor Color(1.0f, 1.0f, 1.0f, 1.0f);

		if (X1 >= 0.0f && X1 < Viewport.Width - 2.0f)
		{
			FPaintGeometry GeoL1 = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X1 - D, Y1, D, H);
			FSlateDrawElement::MakeBox(ElementList, LayerId, GeoL1, WhiteBrush, DrawEffects, Color);

			FPaintGeometry GeoL2 = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X1 - D, Y2 - H, D, H);
			FSlateDrawElement::MakeBox(ElementList, LayerId, GeoL2, WhiteBrush, DrawEffects, Color);
		}

		if (X2 >= -2.0f && X2 < Viewport.Width)
		{
			FPaintGeometry GeoR1 = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X2, Y1, D, H);
			FSlateDrawElement::MakeBox(ElementList, LayerId, GeoR1, WhiteBrush, DrawEffects, Color);

			FPaintGeometry GeoR2 = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X2, Y2 - H, D, H);
			FSlateDrawElement::MakeBox(ElementList, LayerId, GeoR2, WhiteBrush, DrawEffects, Color);
		}

		if (X1 < 0)
			X1 = 0.0f;
		if (X2 > Viewport.Width)
			X2 = Viewport.Width;
		if (X1 < X2)
		{
			FPaintGeometry GeoT = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X1, Y1, X2 - X1, D);
			FSlateDrawElement::MakeBox(ElementList, LayerId, GeoT, WhiteBrush, DrawEffects, Color);

			FPaintGeometry GeoB = MAKE_PAINT_GEOMETRY_RC(AllottedGeometry, X1, Y2 - D, X2 - X1, D);
			FSlateDrawElement::MakeBox(ElementList, LayerId, GeoB, WhiteBrush, DrawEffects, Color);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
