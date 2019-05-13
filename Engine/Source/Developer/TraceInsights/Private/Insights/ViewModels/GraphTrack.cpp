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
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrack::~FGraphTrack()
{
	delete BorderBrush;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport)
{
	if (MY >= Y && MY < Y + H)
	{
		bIsHovered = true;

		if (MX < 100.0f && MY < Y + 14.0f)
		{
			bIsHeaderHovered = true;
		}
		else
		{
			bIsHeaderHovered = false;
		}
	}
	else
	{
		bIsHovered = false;
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

void FGraphTrack::Draw(FDrawContext& DC, const FTimingTrackViewport& Viewport) const
{
	DC.DrawBox(0.0f, Y, Viewport.Width, H, WhiteBrush, FLinearColor(0.1f, 0.1f, 0.1f, 1.0f));
	DC.LayerId++;

	//TODO: Set clipping to (0, Y, Viewport.Width, H)!

	for (const FGraphTrackSeries& Series : AllSeries)
	{
		DrawSeries(Series, DC, Viewport);
	}

	// Draw bottom of the track (debug).
	//DC.DrawBox(0.0f, Viewport.Height - 1.0f, Viewport.Width, 1.0f, WhiteBrush, FLinearColor(0.5f, 0.0f, 0.0f, 1.0f));
	//DC.LayerId++;

	// Draw baseline (Value == 0).
	DC.DrawBox(0.0f, Y + BaselineY - 1.0f, Viewport.Width, 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
	DC.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::DrawSeries(const FGraphTrackSeries& Series, FDrawContext& DC, const FTimingTrackViewport& Viewport) const
{
	if (bDrawBoxes)
	{
		int NumBoxes = Series.Boxes.Num();
		for (int Index = 0; Index < NumBoxes; ++Index)
		{
			const FGraphBox& Box = Series.Boxes[Index];
			DC.DrawBox(Box.X, Y + Box.Y, Box.W, BaselineY - Box.Y, WhiteBrush, Series.Color);
		}
		DC.LayerId++;
	}

	if (bDrawLines)
	{
		FPaintGeometry Geo = DC.Geometry.ToPaintGeometry();
		Geo.AppendTransform(FSlateLayoutTransform(FVector2D(0, Y)));
		FSlateDrawElement::MakeLines(DC.ElementList, DC.LayerId, Geo, Series.LinePoints, DC.DrawEffects, Series.Color, false, 1.0f);
		DC.LayerId++;
	}

	if (bDrawPoints)
	{
		int NumPoints = Series.Points.Num();

		if (bDrawPointsWithBorder)
		{
			// Draw points (border).
			for (int Index = 0; Index < NumPoints; ++Index)
			{
				const FVector2D& Pt = Series.Points[Index];
				const float PtX = Pt.X - GraphTrackPointVisualSize / 2.0f - 1.0f;
				const float PtY = Y + Pt.Y - GraphTrackPointVisualSize / 2.0f - 1.0f;
				DC.DrawBox(PtX, PtY, GraphTrackPointVisualSize + 2.0f, GraphTrackPointVisualSize + 2.0f, PointBrush, Series.BorderColor);
			}
			DC.LayerId++;
		}

		// Draw points (interior).
		for (int Index = 0; Index < NumPoints; ++Index)
		{
			const FVector2D& Pt = Series.Points[Index];
			const float PtX = Pt.X - GraphTrackPointVisualSize / 2.0f;
			const float PtY = Y + Pt.Y - GraphTrackPointVisualSize / 2.0f;
			DC.DrawBox(PtX, PtY, GraphTrackPointVisualSize, GraphTrackPointVisualSize, PointBrush, Series.Color);
		}
		DC.LayerId++;

		/*
		//const float Angle = FMath::DegreesToRadians(45.0f);

		// Draw points as rectangles.
		const float PtSize = GraphTrackPointVisualSize - 2.0f;
		//FVector2D RotationPoint(PtSize / 2.0f, PtSize / 2.0f);
		for (int Index = 0; Index < NumPoints; ++Index)
		{
			const FVector2D& Pt = Series.Points[Index];
			const float PtX = Pt.X - PtSize / 2.0f;
			const float PtY = Y + Pt.Y - PtSize / 2.0f;
			DC.DrawBox(PtX, PtY, PtSize, PtSize, PointBrush, Series.Color);
			//DC.DrawRotatedBox(PtX, PtY, PtSize, PtSize, WhiteBrush, Series.Color, Angle, RotationPoint);
		}
		DC.LayerId++;

		// Draw borders.
		const float BorderPtSize = GraphTrackPointVisualSize;
		//FVector2D BorderRotationPoint(BorderPtSize / 2.0f, BorderPtSize / 2.0f);
		for (int Index = 0; Index < NumPoints; ++Index)
		{
			const FVector2D& Pt = Series.Points[Index];
			const float PtX = Pt.X - BorderPtSize / 2.0f;
			const float PtY = Y + Pt.Y - BorderPtSize / 2.0f;
			DC.DrawBox(PtX, PtY, BorderPtSize, BorderPtSize, BorderBrush, Series.BorderColor);
			//DC.DrawRotatedBox(PtX, PtY, BorderPtSize, BorderPtSize, BorderBrush, Series.BorderColor, Angle, BorderRotationPoint);
		}
		DC.LayerId++;
		*/
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
	BaselineY = H;
	ScaleY = 1.0;

	GenerateSeries(AllSeries[0], Viewport, 1000000);

	UpdateStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FRandomGraphTrack::GenerateSeries(FGraphTrackSeries& Series, const FTimingTrackViewport& Viewport, const int EventCount)
{
	//////////////////////////////////////////////////
	// Generate random events.

	static const double DT1 = 0.0000001; // 100ns
	static const double DT2 = 0.01; // 100ms
	const float V1 = 0;
	const float V2 = H;

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
	for (int Index = 0; Index < EventCount; ++Index)
	{
		FGraphEvent Ev;
		Ev.Time = NextT;
		NextT += DT1 + RandomStream.GetFraction() * (DT2 - DT1);
		Ev.Duration = DT1 + RandomStream.GetFraction() * (NextT - Ev.Time - DT1);
		Ev.Value = V1 + RandomStream.GetFraction() * (V2 - V1);
		Events.Add(Ev);
	}

	// Sort events by time.
	//Events.Sort([](const FGraphEvent& A, const FGraphEvent& B) { return A.Time < B.Time; });

	//////////////////////////////////////////////////
	// Optimize and build draw lists.

	FGraphTrackBuilder Builder(*this, Series, Viewport);

	Builder.Begin();

	int Index = 0;
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

	Builder.End();
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
	//GameFramesSeries.SetColor(FLinearColor(1.0f, 0.5f, 0.0f, 1.0f), FLinearColor(1.0f, 0.8f, 0.3f, 1.0f));
	GameFramesSeries.SetColor(FLinearColor(0.3f, 0.3f, 1.0f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

	FGraphTrackSeries& RenderFramesSeries = AllSeries[1];
	//RenderFramesSeries.SetColor(FLinearColor(1.0f, 0.0f, 0.5f, 1.0f), FLinearColor(1.0f, 0.3f, 0.8f, 1.0f));
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
	BaselineY = H;
	ScaleY = 200.0 / 0.1; // 200px = 100ms

	UpdateSeries(AllSeries[0], Viewport, TraceFrameType_Game);
	UpdateSeries(AllSeries[1], Viewport, TraceFrameType_Rendering);

	UpdateStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FFramesGraphTrack::UpdateSeries(FGraphTrackSeries& Series, const FTimingTrackViewport& Viewport, ETraceFrameType FrameType)
{
	FGraphTrackBuilder Builder(*this, Series, Viewport);

	Builder.Begin();

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope _(*Session.Get());
		Session->ReadFramesProvider([this, &Builder, FrameType](const Trace::IFrameProvider& FramesProvider)
		{
			uint64 FrameCount = FramesProvider.GetFrameCount(FrameType);
			FramesProvider.EnumerateFrames(FrameType, 0, FrameCount - 1, [&Builder](const Trace::FFrame& Frame)
			{
				const double Duration = Frame.EndTime - Frame.StartTime;
				Builder.AddEvent(Frame.StartTime, Duration, Duration);
			});
		});
	}

	Builder.End();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrackBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrackBuilder::FGraphTrackBuilder(FGraphTrack& InTrack, FGraphTrackSeries& InSeries, const FTimingTrackViewport& InViewport)
	: Track(InTrack)
	, Series(InSeries)
	, Viewport(InViewport)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::Begin()
{
	BeginPoints();
	BeginConnectedLines();
	BeginBoxes();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::End()
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

	int MaxPointsPerLineScan = FMath::CeilToInt(Track.H / GraphTrackPointDY);
	PointsAtCurrentX.Reset();
	PointsAtCurrentX.AddDefaulted(MaxPointsPerLineScan);
	//PointsAtCurrentX.SetNum(MaxPointsPerLineScan);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrackBuilder::AddPoint(double Time, double Value)
{
	const float X = Viewport.TimeToSlateUnitsRounded(Time);
	if (X < -GraphTrackPointVisualSize / 2.0f || X >= Viewport.Width + GraphTrackPointVisualSize / 2.0f)
	{
		return;
	}

	// Align the X with a grid of GraphTrackPointDX pixels in size, in the global space (i.e. scroll independent).
	const double AlignedX = FMath::RoundToDouble(Time * Viewport.ScaleX / GraphTrackPointDX) * GraphTrackPointDX;

	if (AlignedX > PointsCurrentX + GraphTrackPointDX - 0.5)
	{
		FlushPoints();
		PointsCurrentX = AlignedX;
	}

	const float Y = Track.GetYForValue(Value);

	int Index = FMath::RoundToInt(Y / GraphTrackPointDY);
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
	for (int Index = 0; Index < PointsAtCurrentX.Num(); ++Index)
	{
		FPointInfo& Pt = PointsAtCurrentX[Index];
		if (Pt.bValid)
		{
			Pt.bValid = false;
			//const float Y = Index * GraphTrackPointDY + GraphTrackPointDY / 2.0f;
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

void FGraphTrackBuilder::EndBoxes()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
