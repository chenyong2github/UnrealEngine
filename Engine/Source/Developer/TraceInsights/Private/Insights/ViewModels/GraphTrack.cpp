// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GraphTrack.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateBorderBrush.h"
#include "EditorStyleSet.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "GraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrackSeries
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrackSeries::FGraphTrackSeries()
	: Color(0.0f, 0.5f, 1.0f, 1.0f)
	, BorderColor(0.3f, 0.8f, 1.0f, 1.0f)
	//, Points()
	//, LinePoints()
	//, Boxes()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrackSeries::~FGraphTrackSeries()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrack::FGraphTrack(uint64 InTrackId)
	: FBaseTimingTrack(InTrackId)
	//, AllSeries()
	, WhiteBrush(FCoreStyle::Get().GetBrush("WhiteBrush"))
	, PointBrush(FEditorStyle::GetBrush("Graph.ExecutionBubble"))
	, BorderBrush(new FSlateBorderBrush(NAME_None, FMargin(1.0f)))
	//, BorderBrush(FEditorStyle::GetBrush("PlainBorder"))
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	, bDrawPoints(true)
	, bDrawPointsWithBorder(true)
	, bDrawLines(true)
	, bDrawLinesWithDuration(false)
	, bDrawBoxes(true)
	, BaselineY(0.0f)
	, ScaleY(1.0)
	, NumAddedEvents(0)
	, NumDrawPoints(0)
	, NumDrawLines(0)
	, NumDrawBoxes(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrack::~FGraphTrack()
{
	delete BorderBrush;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::UpdateHoveredState(float MouseX, float MouseY, const FTimingTrackViewport& Viewport)
{
	constexpr float HeaderWidth = 100.0f;
	constexpr float HeaderHeight = 14.0f;

	if (MouseY >= GetPosY() && MouseY < GetPosY() + GetHeight())
	{
		SetHoveredState(true);
		SetHeaderHoveredState(MouseX < HeaderWidth && MouseY < GetPosY() + HeaderHeight);
	}
	else
	{
		SetHoveredState(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::UpdateStats()
{
	NumDrawPoints = 0;
	NumDrawLines = 0;
	NumDrawBoxes = 0;

	for (const FGraphTrackSeries& Series : AllSeries)
	{
		NumDrawPoints += Series.Points.Num();
		NumDrawLines += Series.LinePoints.Num() / 2;
		NumDrawBoxes += Series.Boxes.Num();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::Draw(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const
{
	DrawContext.DrawBox(0.0f, GetPosY(), Viewport.Width, GetHeight(), WhiteBrush, FLinearColor(0.1f, 0.1f, 0.1f, 1.0f));
	DrawContext.LayerId++;

	//TODO: Set clipping to (0, GetPosY(), Viewport.Width, GetHeight())!

	for (const FGraphTrackSeries& Series : AllSeries)
	{
		DrawSeries(Series, DrawContext, Viewport);
	}

	// Draw baseline (Value == 0).
	DrawContext.DrawBox(0.0f, GetPosY() + BaselineY - 1.0f, Viewport.Width, 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::DrawSeries(const FGraphTrackSeries& Series, FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const
{
	if (bDrawBoxes)
	{
		int32 NumBoxes = Series.Boxes.Num();
		for (int32 Index = 0; Index < NumBoxes; ++Index)
		{
			const FGraphBox& Box = Series.Boxes[Index];
			DrawContext.DrawBox(Box.X, GetPosY() + Box.Y, Box.W, BaselineY - Box.Y, WhiteBrush, Series.Color);
		}
		DrawContext.LayerId++;
	}

	if (bDrawLines)
	{
		FPaintGeometry Geo = DrawContext.Geometry.ToPaintGeometry();
		Geo.AppendTransform(FSlateLayoutTransform(FVector2D(0, GetPosY())));
		FSlateDrawElement::MakeLines(DrawContext.ElementList, DrawContext.LayerId, Geo, Series.LinePoints, DrawContext.DrawEffects, Series.Color, false, 1.0f);
		DrawContext.LayerId++;
	}

	if (bDrawPoints)
	{
		int32 NumPoints = Series.Points.Num();

#define INSIGHTS_GRAPH_TRACK_DRAW_POINTS_AS_RECTANGLES 0
#if !INSIGHTS_GRAPH_TRACK_DRAW_POINTS_AS_RECTANGLES

		if (bDrawPointsWithBorder)
		{
			// Draw points (border).
			for (int32 Index = 0; Index < NumPoints; ++Index)
			{
				const FVector2D& Pt = Series.Points[Index];
				const float PtX = Pt.X - PointVisualSize / 2.0f - 1.0f;
				const float PtY = GetPosY() + Pt.Y - PointVisualSize / 2.0f - 1.0f;
				DrawContext.DrawBox(PtX, PtY, PointVisualSize + 2.0f, PointVisualSize + 2.0f, PointBrush, Series.BorderColor);
			}
			DrawContext.LayerId++;
		}

		// Draw points (interior).
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const FVector2D& Pt = Series.Points[Index];
			const float PtX = Pt.X - PointVisualSize / 2.0f;
			const float PtY = GetPosY() + Pt.Y - PointVisualSize / 2.0f;
			DrawContext.DrawBox(PtX, PtY, PointVisualSize, PointVisualSize, PointBrush, Series.Color);
		}
		DrawContext.LayerId++;

#else // Alternative way of drawing points; kept here for debugging purposes.

		//const float Angle = FMath::DegreesToRadians(45.0f);

		if (bDrawPointsWithBorder)
		{
			// Draw borders.
			const float BorderPtSize = PointVisualSize;
			//FVector2D BorderRotationPoint(BorderPtSize / 2.0f, BorderPtSize / 2.0f);
			for (int32 Index = 0; Index < NumPoints; ++Index)
			{
				const FVector2D& Pt = Series.Points[Index];
				const float PtX = Pt.X - BorderPtSize / 2.0f;
				const float PtY = GetPosY() + Pt.Y - BorderPtSize / 2.0f;
				DrawContext.DrawBox(PtX, PtY, BorderPtSize, BorderPtSize, BorderBrush, Series.BorderColor);
				//DrawContext.DrawRotatedBox(PtX, PtY, BorderPtSize, BorderPtSize, BorderBrush, Series.BorderColor, Angle, BorderRotationPoint);
			}
			DrawContext.LayerId++;
		}

		// Draw points as rectangles.
		const float PtSize = PointVisualSize - 2.0f;
		//FVector2D RotationPoint(PtSize / 2.0f, PtSize / 2.0f);
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const FVector2D& Pt = Series.Points[Index];
			const float PtX = Pt.X - PtSize / 2.0f;
			const float PtY = GetPosY() + Pt.Y - PtSize / 2.0f;
			DrawContext.DrawBox(PtX, PtY, PtSize, PtSize, PointBrush, Series.Color);
			//DrawContext.DrawRotatedBox(PtX, PtY, PtSize, PtSize, WhiteBrush, Series.Color, Angle, RotationPoint);
		}
		DrawContext.LayerId++;

#endif
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FRandomGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FRandomGraphTrack::FRandomGraphTrack(uint64 InTrackId)
	: FGraphTrack(InTrackId)
{
	bDrawPoints = true;
	bDrawPointsWithBorder = true;
	bDrawLines = true;
	bDrawLinesWithDuration = false;
	bDrawBoxes = false;

	AllSeries.AddDefaulted();
	FGraphTrackSeries& Series = AllSeries[0];
	Series.SetColor(FLinearColor(0.0f, 0.5f, 1.0f, 1.0f), FLinearColor(0.3f, 0.8f, 1.0f, 1.0f));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FRandomGraphTrack::~FRandomGraphTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FRandomGraphTrack::Update(const FTimingTrackViewport& Viewport)
{
	NumAddedEvents = 0;

	// TODO: vertical panning and zooming
	BaselineY = GetHeight();
	ScaleY = 1.0;

	ensure(AllSeries.Num() == 1);
	GenerateSeries(AllSeries[0], Viewport, 1000000);

	UpdateStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FRandomGraphTrack::GenerateSeries(FGraphTrackSeries& Series, const FTimingTrackViewport& Viewport, const int32 EventCount)
{
	//////////////////////////////////////////////////
	// Generate random events.

	constexpr double MinDeltaTime = 0.0000001; // 100ns
	constexpr double MaxDeltaTime = 0.01; // 100ms
	const float MinValue = 0;
	const float MaxValue = GetHeight();

	struct FGraphEvent
	{
		double Time;
		double Duration;
		double Value;
	};

	TArray<FGraphEvent> Events;
	Events.Reserve(EventCount);

	FRandomStream RandomStream(0);
	double NextT = 0.0;
	for (int32 Index = 0; Index < EventCount; ++Index)
	{
		FGraphEvent Ev;
		Ev.Time = NextT;
		const double TimeAdvance = RandomStream.GetFraction() * (MaxDeltaTime - MinDeltaTime);
		NextT += MinDeltaTime + TimeAdvance;
		Ev.Duration = MinDeltaTime + RandomStream.GetFraction() * TimeAdvance;
		Ev.Value = MinValue + RandomStream.GetFraction() * (MaxValue - MinValue);
		Events.Add(Ev);
	}

	//////////////////////////////////////////////////
	// Optimize and build draw lists.
	{
		FGraphTrackBuilder Builder(*this, Series, Viewport);

		int32 Index = 0;
		while (Index < EventCount && Events[Index].Time < Viewport.StartTime)
		{
			++Index;
		}
		if (Index > 0)
		{
			Index--; // one point outside screen (left side)
		}
		while (Index < EventCount)
		{
			const FGraphEvent& Ev = Events[Index];
			Builder.AddEvent(Ev.Time, Ev.Duration, Ev.Value);

			if (Ev.Time > Viewport.EndTime)
			{
				// one point outside screen (right side)
				break;
			}

			++Index;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FFramesGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FFramesGraphTrack::FFramesGraphTrack(uint64 InTrackId)
	: FGraphTrack(InTrackId)
{
	bDrawPoints = true;
	bDrawPointsWithBorder = true;
	bDrawLines = true;
	bDrawLinesWithDuration = true;
	bDrawBoxes = false;

	AllSeries.AddDefaulted(2);

	FGraphTrackSeries& GameFramesSeries = AllSeries[0];
	GameFramesSeries.SetColor(FLinearColor(0.3f, 0.3f, 1.0f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

	FGraphTrackSeries& RenderFramesSeries = AllSeries[1];
	RenderFramesSeries.SetColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FFramesGraphTrack::~FFramesGraphTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFramesGraphTrack::Update(const FTimingTrackViewport& Viewport)
{
	NumAddedEvents = 0;

	// TODO: Vertical panning and zooming needs to be moved out in a Viewport like controller.
	BaselineY = GetHeight();
	ScaleY = 200.0 / 0.1; // 200px = 100ms

	ensure(AllSeries.Num() == 2);
	UpdateSeries(AllSeries[0], Viewport, TraceFrameType_Game);
	UpdateSeries(AllSeries[1], Viewport, TraceFrameType_Rendering);

	UpdateStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFramesGraphTrack::UpdateSeries(FGraphTrackSeries& Series, const FTimingTrackViewport& Viewport, ETraceFrameType FrameType)
{
	FGraphTrackBuilder Builder(*this, Series, Viewport);

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::IFrameProvider& FramesProvider = ReadFrameProvider(*Session.Get());
		uint64 FrameCount = FramesProvider.GetFrameCount(FrameType);
		FramesProvider.EnumerateFrames(FrameType, 0, FrameCount - 1, [&Builder](const Trace::FFrame& Frame)
		{
			const double Duration = Frame.EndTime - Frame.StartTime;
			Builder.AddEvent(Frame.StartTime, Duration, Duration);
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrackBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrackBuilder::FGraphTrackBuilder(FGraphTrack& InTrack, FGraphTrackSeries& InSeries, const FTimingTrackViewport& InViewport)
	: Track(InTrack)
	, Series(InSeries)
	, Viewport(InViewport)
{
	BeginPoints();
	BeginConnectedLines();
	BeginBoxes();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrackBuilder::~FGraphTrackBuilder()
{
	EndPoints();
	EndConnectedLines();
	EndBoxes();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::AddEvent(double Time, double Duration, double Value)
{
	Track.NumAddedEvents++;

	if (Track.bDrawPoints)
	{
		AddPoint(Time, Value);
	}

	if (Track.bDrawLines)
	{
		AddConnectedLine(Time, Value);

		if (Track.bDrawLinesWithDuration)
		{
			AddConnectedLine(Time + Duration, Value);
		}
	}

	if (Track.bDrawBoxes)
	{
		AddBox(Time, Duration, Value);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrackBuilder - Points
////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::BeginPoints()
{
	Series.Points.Reset();

	PointsCurrentX = -DBL_MAX;

	PointsAtCurrentX.Reset();
	int32 MaxPointsPerLineScan = FMath::CeilToInt(Track.GetHeight() / FGraphTrack::PointSizeY);
	if (MaxPointsPerLineScan > 0)
	{
		PointsAtCurrentX.AddDefaulted(MaxPointsPerLineScan);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::AddPoint(double Time, double Value)
{
	const float X = Viewport.TimeToSlateUnitsRounded(Time);
	if (X < -FGraphTrack::PointVisualSize / 2.0f || X >= Viewport.Width + FGraphTrack::PointVisualSize / 2.0f)
	{
		return;
	}

	// Align the X with a grid of GraphTrackPointDX pixels in size, in the global space (i.e. scroll independent).
	const double AlignedX = FMath::RoundToDouble(Time * Viewport.ScaleX / FGraphTrack::PointSizeX) * FGraphTrack::PointSizeX;

	if (AlignedX > PointsCurrentX + FGraphTrack::PointSizeX - 0.5)
	{
		FlushPoints();
		PointsCurrentX = AlignedX;
	}

	const float Y = Track.GetYForValue(Value);

	int32 Index = FMath::RoundToInt(Y / FGraphTrack::PointSizeY);
	if (Index >= 0 && Index < PointsAtCurrentX.Num())
	{
		FPointInfo& Pt = PointsAtCurrentX[Index];
		Pt.bValid = true;
		Pt.X = X;
		Pt.Y = Y;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::FlushPoints()
{
	for (int32 Index = 0; Index < PointsAtCurrentX.Num(); ++Index)
	{
		FPointInfo& Pt = PointsAtCurrentX[Index];
		if (Pt.bValid)
		{
			Pt.bValid = false;
			Series.Points.Add(FVector2D(Pt.X, Pt.Y));
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::EndPoints()
{
	FlushPoints();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrackBuilder - Connected Lines
////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::BeginConnectedLines()
{
	Series.LinePoints.Reset();

	LinesCurrentX = -FLT_MAX;
	LinesMinY = FLT_MAX;
	LinesMaxY = -FLT_MAX;
	LinesFirstY = FLT_MAX;
	LinesLastY = FLT_MAX;
	bIsLastLineAdded = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::AddConnectedLine(double Time, double Value)
{
	if (bIsLastLineAdded)
	{
		return;
	}

	const float Y = Track.GetYForValue(Value);

	const float X = Viewport.TimeToSlateUnitsRounded(Viewport.RestrictEndTime(Time));

	ensure(X >= LinesCurrentX); // we are assuming events are already sorted by Time

	if (X < 0)
	{
		LinesCurrentX = X;
		LinesLastY = Y;
		return;
	}

	if (X >= Viewport.Width)
	{
		if (!bIsLastLineAdded)
		{
			bIsLastLineAdded = true;

			if (LinesLastY != FLT_MAX)
			{
				FlushConnectedLine();

				Series.LinePoints.Add(FVector2D(LinesCurrentX, LinesLastY));
				Series.LinePoints.Add(FVector2D(X, Y));
			}

			// Reset the "reduction line" so last FlushConnectedLine() call will do nothing.
			LinesMinY = Y;
			LinesMaxY = Y;
		}
		return;
	}

	if (X > LinesCurrentX)
	{
		if (LinesLastY != FLT_MAX)
		{
			FlushConnectedLine();

			Series.LinePoints.Add(FVector2D(LinesCurrentX, LinesLastY));
			Series.LinePoints.Add(FVector2D(X, Y));
		}

		// Advance the "reduction line".
		LinesCurrentX = X;
		LinesMinY = Y;
		LinesMaxY = Y;
		LinesFirstY = Y;
		LinesLastY = Y;
	}
	else
	{
		// Merge current line with the "reduction line".
		if (Y < LinesMinY)
		{
			LinesMinY = Y;
		}
		if (Y > LinesMaxY)
		{
			LinesMaxY = Y;
		}
		LinesLastY = Y;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::FlushConnectedLine()
{
	if (LinesCurrentX >= 0.0f && LinesMinY != LinesMaxY)
	{
		Series.LinePoints.Add(FVector2D(LinesCurrentX, LinesMaxY));
		Series.LinePoints.Add(FVector2D(LinesCurrentX, LinesMinY));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::EndConnectedLines()
{
	FlushConnectedLine();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrackBuilder - Boxes
////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::BeginBoxes()
{
	Series.Boxes.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::AddBox(double Time, double Duration, double Value)
{
	float X1 = Viewport.TimeToSlateUnitsRounded(Time);
	if (X1 > Viewport.Width)
	{
		return;
	}

	double EndTime = Viewport.RestrictEndTime(Time + Duration);
	float X2 = Viewport.TimeToSlateUnitsRounded(EndTime);
	if (X2 < 0)
	{
		return;
	}

	float W = X2 - X1;
	ensure(W >= 0); // we expect events to be sorted

	// Timing events are displayed with minimum 1px (including empty ones).
	if (W == 0)
	{
		W = 1.0f;
	}

	// TODO: reduction algorithm
	FGraphBox Box;
	Box.X = X1;
	Box.W = W;
	Box.Y = Track.GetYForValue(Value);
	Series.Boxes.Add(Box);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::FlushBox()
{
	// TODO: reduction algorithm
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::EndBoxes()
{
	FlushBox();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
