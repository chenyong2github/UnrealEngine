// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GraphTrack.h"

#include <limits>

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateBorderBrush.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/DrawHelpers.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "GraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrackSeries
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrackSeries::FGraphTrackSeries()
	: Name()
	, Description()
	, bIsVisible(true)
	, Color(0.0f, 0.5f, 1.0f, 1.0f)
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
	, bDrawPolygon(true)
	, bUseEventDuration(true)
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

	for (const TSharedPtr<FGraphTrackSeries>& Series : AllSeries)
	{
		NumDrawPoints += Series->Points.Num();
		NumDrawLines += Series->LinePoints.Num() / 2;
		NumDrawBoxes += Series->Boxes.Num();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::DrawBackground(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const
{
	const FLinearColor ValidAreaColor(0.07f, 0.07f, 0.07f, 1.0f);
	const FLinearColor InvalidAreaColor(0.1f, 0.07f, 0.07f, 1.0f);
	const FLinearColor EdgeColor(0.05f, 0.05f, 0.05f, 1.0f);

	const float X = 0.0f;
	const float W = FMath::CeilToFloat(Viewport.Width);
	const float Y = GetPosY();
	const float H = GetHeight();

	float ValidAreaX, ValidAreaW;

	FDrawHelpers::DrawBackground(DrawContext, Viewport, WhiteBrush, ValidAreaColor, InvalidAreaColor, EdgeColor, X, Y, W, H, ValidAreaX, ValidAreaW);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::Draw(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const
{
	DrawBackground(DrawContext, Viewport);

	//TODO: Set clipping to (0, GetPosY(), Viewport.Width, GetHeight())!

	// Draw top line.
	//DrawContext.DrawBox(0.0f, GetPosY(), Viewport.Width, 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));

	// Draw baseline (Value == 0).
	DrawContext.DrawBox(0.0f, GetPosY() + BaselineY - 1.0f, Viewport.Width, 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
	DrawContext.LayerId++;

	for (const TSharedPtr<FGraphTrackSeries>& Series : AllSeries)
	{
		if (Series->IsVisible())
		{
			DrawSeries(*Series, DrawContext, Viewport);
		}
	}
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

	if (bDrawPolygon)
	{
		FSlateShaderResourceProxy* ResourceProxy = FSlateDataPayload::ResourceManager->GetShaderResource(*WhiteBrush);
		FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);

		FVector2D AtlasOffset = ResourceProxy ? ResourceProxy->StartUV : FVector2D(0.f, 0.f);
		FVector2D AtlasUVSize = ResourceProxy ? ResourceProxy->SizeUV : FVector2D(1.f, 1.f);

		const FVector2D Pos = DrawContext.Geometry.GetAbsolutePosition();
		const FVector2D Size = DrawContext.Geometry.GetLocalSize();
		const float Scale = DrawContext.Geometry.Scale;

		FSlateRenderTransform RenderTransform;
		FColor FillColor(Series.Color.R * 255, Series.Color.G * 255, Series.Color.B * 255, 42);

		TArray<SlateIndex> Indices;
		TArray<FSlateVertex> Verts;

		Indices.Reserve(Series.LinePoints.Num() * 6);
		Verts.Reserve(Series.LinePoints.Num() * 2);

		const float TopY = GetPosY();
		const float BottomY = (TopY + GetHeight()) / Size.Y;

		for (const FVector2D& LinePoint : Series.LinePoints)
		{
			// Add verts top->bottom
			FVector2D UV(LinePoint.X / Size.X, (TopY + LinePoint.Y) / Size.Y);
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, (Pos + UV * Size * Scale), AtlasOffset + UV * AtlasUVSize, FillColor));
			UV.Y = BottomY;
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, (Pos + UV * Size * Scale), AtlasOffset + FVector2D(UV.X, 0.5f) * AtlasUVSize, FillColor));

			if (Verts.Num() >= 4)
			{
				int32 Index0 = Verts.Num() - 4;
				int32 Index1 = Verts.Num() - 3;
				int32 Index2 = Verts.Num() - 2;
				int32 Index3 = Verts.Num() - 1;

				Indices.Add(Index0);
				Indices.Add(Index1);
				Indices.Add(Index2);

				Indices.Add(Index1);
				Indices.Add(Index2);
				Indices.Add(Index3);
			}
		}

		if (Indices.Num())
		{
			FSlateDrawElement::MakeCustomVerts(
				DrawContext.ElementList,
				DrawContext.LayerId,
				ResourceHandle,
				Verts,
				Indices,
				nullptr,
				0,
				0, ESlateDrawEffect::PreMultipliedAlpha);
			DrawContext.LayerId++;
		}
	}

	if (bDrawLines)
	{
		FPaintGeometry Geo = DrawContext.Geometry.ToPaintGeometry();
		Geo.AppendTransform(FSlateLayoutTransform(FVector2D(0.5f, 0.5f + GetPosY())));
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
			DrawContext.DrawBox(PtX, PtY, PtSize, PtSize, WhiteBrush, Series.Color);
			//DrawContext.DrawRotatedBox(PtX, PtY, PtSize, PtSize, WhiteBrush, Series.Color, Angle, RotationPoint);
		}
		DrawContext.LayerId++;

#endif
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("Misc");
	{
		FUIAction Action_ShowPoints
		(
			FExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowPoints_Execute),
			FCanExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowPoints_CanExecute),
			FIsActionChecked::CreateSP(this, &FGraphTrack::ContextMenu_ShowPoints_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_ShowPoints", "Show Points"),
			LOCTEXT("ContextMenu_ShowPoints_Desc", "Show points."),
			FSlateIcon(),
			Action_ShowPoints,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		FUIAction Action_ShowPointsWithBorder
		(
			FExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowPointsWithBorder_Execute),
			FCanExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowPointsWithBorder_CanExecute),
			FIsActionChecked::CreateSP(this, &FGraphTrack::ContextMenu_ShowPointsWithBorder_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_ShowPointsWithBorder", "Show Points with Border"),
			LOCTEXT("ContextMenu_ShowPointsWithBorder_Desc", "Show border around points."),
			FSlateIcon(),
			Action_ShowPointsWithBorder,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		FUIAction Action_ShowLines
		(
			FExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowLines_Execute),
			FCanExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowLines_CanExecute),
			FIsActionChecked::CreateSP(this, &FGraphTrack::ContextMenu_ShowLines_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_ShowLines", "Show Connected Lines"),
			LOCTEXT("ContextMenu_ShowLines_Desc", "Show connected lines. Each event is a single point in time."),
			FSlateIcon(),
			Action_ShowLines,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		FUIAction Action_ShowPolygon
		(
			FExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowPolygon_Execute),
			FCanExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowPolygon_CanExecute),
			FIsActionChecked::CreateSP(this, &FGraphTrack::ContextMenu_ShowPolygon_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_ShowPolygon", "Show Polygon"),
			LOCTEXT("ContextMenu_ShowPolygon_Desc", "Show filled polygon under the graph series."),
			FSlateIcon(),
			Action_ShowPolygon,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		FUIAction Action_UseEventDuration
		(
			FExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_UseEventDuration_Execute),
			FCanExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_UseEventDuration_CanExecute),
			FIsActionChecked::CreateSP(this, &FGraphTrack::ContextMenu_UseEventDuration_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_UseEventDuration", "Use Event Duration"),
			LOCTEXT("ContextMenu_UseEventDuration_Desc", "Use duration of timing events (for Connected Lines and Polygon)."),
			FSlateIcon(),
			Action_UseEventDuration,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		FUIAction Action_ShowBars
		(
			FExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowBars_Execute),
			FCanExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowBars_CanExecute),
			FIsActionChecked::CreateSP(this, &FGraphTrack::ContextMenu_ShowBars_IsChecked)
		);
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ContextMenu_ShowBars", "Show Bars"),
			LOCTEXT("ContextMenu_ShowBars_Desc", "Show bars. Width of bars corresponds to duration of timing events."),
			FSlateIcon(),
			Action_ShowBars,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Series", LOCTEXT("ContextMenu_Header_Series", "Series"));
	{
		for (const TSharedPtr<FGraphTrackSeries>& Series : AllSeries)
		{
			FUIAction Action_ShowSeries
			(
				FExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowSeries_Execute, Series.Get()),
				FCanExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowSeries_CanExecute, Series.Get()),
				FIsActionChecked::CreateSP(this, &FGraphTrack::ContextMenu_ShowSeries_IsChecked, Series.Get())
			);
			MenuBuilder.AddMenuEntry
			(
				Series->GetName(),
				Series->GetDescription(),
				FSlateIcon(),
				Action_ShowSeries,
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}
	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::ContextMenu_ShowPoints_Execute()
{
	bDrawPoints = !bDrawPoints;
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowPoints_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowPoints_IsChecked()
{
	return bDrawPoints;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::ContextMenu_ShowPointsWithBorder_Execute()
{
	bDrawPointsWithBorder = !bDrawPointsWithBorder;
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowPointsWithBorder_CanExecute()
{
	return bDrawPoints;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowPointsWithBorder_IsChecked()
{
	return bDrawPointsWithBorder;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::ContextMenu_ShowLines_Execute()
{
	bDrawLines = !bDrawLines;
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowLines_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowLines_IsChecked()
{
	return bDrawLines;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::ContextMenu_ShowPolygon_Execute()
{
	bDrawPolygon = !bDrawPolygon;
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowPolygon_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowPolygon_IsChecked()
{
	return bDrawPolygon;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::ContextMenu_UseEventDuration_Execute()
{
	bUseEventDuration = !bUseEventDuration;
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_UseEventDuration_CanExecute()
{
	return bDrawLines || bDrawPolygon;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_UseEventDuration_IsChecked()
{
	return bUseEventDuration;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::ContextMenu_ShowBars_Execute()
{
	bDrawBoxes = !bDrawBoxes;
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowBars_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowBars_IsChecked()
{
	return bDrawBoxes;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::ContextMenu_ShowSeries_Execute(FGraphTrackSeries* Series)
{
	Series->SetVisibility(!Series->IsVisible());
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowSeries_CanExecute(FGraphTrackSeries* Series)
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowSeries_IsChecked(FGraphTrackSeries* Series)
{
	return Series->IsVisible();
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
	bDrawPolygon = false;
	bUseEventDuration = false;
	bDrawBoxes = false;

	TSharedPtr<FGraphTrackSeries> Series0 = MakeShareable(new FGraphTrackSeries());
	Series0->SetName(TEXT("Random Blue"));
	Series0->SetDescription(TEXT("Random series; for debuging purposes"));
	Series0->SetColor(FLinearColor(0.1f, 0.5f, 1.0f, 1.0f), FLinearColor(0.4f, 0.8f, 1.0f, 1.0f));
	Series0->SetVisibility(true);
	AllSeries.Add(Series0);

	TSharedPtr<FGraphTrackSeries> Series1 = MakeShareable(new FGraphTrackSeries());
	Series1->SetName(TEXT("Random Yellow"));
	Series1->SetDescription(TEXT("Random series; for debuging purposes"));
	Series1->SetColor(FLinearColor(0.9f, 0.9f, 0.1f, 1.0f), FLinearColor(1.0f, 1.0f, 0.4f, 1.0f));
	Series1->SetVisibility(false);
	AllSeries.Add(Series1);

	TSharedPtr<FGraphTrackSeries> Series2 = MakeShareable(new FGraphTrackSeries());
	Series2->SetName(TEXT("Random Red"));
	Series2->SetDescription(TEXT("Random series; for debuging purposes"));
	Series2->SetColor(FLinearColor(1.0f, 0.1f, 0.2f, 1.0f), FLinearColor(1.0f, 0.4f, 0.5f, 1.0f));
	Series2->SetVisibility(false);
	AllSeries.Add(Series2);
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

	ensure(AllSeries.Num() == 3);
	GenerateSeries(*AllSeries[0], Viewport, 1000000, 0);
	GenerateSeries(*AllSeries[1], Viewport, 1000000, 1);
	GenerateSeries(*AllSeries[2], Viewport, 1000000, 2);

	UpdateStats();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FRandomGraphTrack::GenerateSeries(FGraphTrackSeries& Series, const FTimingTrackViewport& Viewport, const int32 EventCount, int32 Seed)
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

	FRandomStream RandomStream(Seed);
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

	TSharedPtr<FTimingGraphSeries> GameFramesSeries = MakeShareable(new FTimingGraphSeries());
	GameFramesSeries->SetName(TEXT("Game Frames"));
	GameFramesSeries->SetDescription(TEXT("Duration of Game frames"));
	GameFramesSeries->SetColor(FLinearColor(0.3f, 0.3f, 1.0f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	GameFramesSeries->Type = FTimingGraphSeries::ESeriesType::Frame;
	GameFramesSeries->Id = static_cast<uint32>(TraceFrameType_Game);
	AllSeries.Add(GameFramesSeries);

	TSharedPtr<FTimingGraphSeries> RenderingFramesSeries = MakeShareable(new FTimingGraphSeries());
	RenderingFramesSeries->SetName(TEXT("Rendering Frames"));
	RenderingFramesSeries->SetDescription(TEXT("Duration of Rendering frames"));
	RenderingFramesSeries->SetColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	RenderingFramesSeries->Type = FTimingGraphSeries::ESeriesType::Frame;
	RenderingFramesSeries->Id = static_cast<uint32>(TraceFrameType_Rendering);
	AllSeries.Add(RenderingFramesSeries);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTimingGraphSeries> FTimingGraphTrack::GetStatsCounterSeries(uint32 CounterId)
{
	TSharedPtr<FGraphTrackSeries>* Ptr = AllSeries.FindByPredicate([=](const TSharedPtr<FGraphTrackSeries>& Series)
	{
		const TSharedPtr<FTimingGraphSeries> TimingSeries = StaticCastSharedPtr<FTimingGraphSeries>(Series);
		return TimingSeries->Type == FTimingGraphSeries::ESeriesType::StatsCounter && TimingSeries->Id == CounterId;
	});
	return (Ptr != nullptr) ? StaticCastSharedPtr<FTimingGraphSeries>(*Ptr) : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::AddStatsCounterSeries(uint32 CounterId, FLinearColor Color, double ValueOffset, double ValueScale)
{
	TSharedPtr<FTimingGraphSeries> Series = MakeShareable(new FTimingGraphSeries());

	const TCHAR* CounterName = nullptr;
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
				bIsFloatingPoint = Counter.IsFloatingPoint();
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
	Series->ValueOffset = ValueOffset;
	Series->ValueScale = ValueScale;

	AllSeries.Add(Series);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingGraphTrack::RemoveStatsCounterSeries(uint32 CounterId)
{
	AllSeries.RemoveAll([=](const TSharedPtr<FGraphTrackSeries>& Series)
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
	NumAddedEvents = 0;

	// TODO: Vertical panning and zooming needs to be moved out in a Viewport like controller.
	BaselineY = GetHeight();
	ScaleY = 200.0 / 0.1; // 200px = 100ms

	for (TSharedPtr<FGraphTrackSeries>& Series : AllSeries)
	{
		if (Series->IsVisible())
		{
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
			//const double ValueOffset = Series.ValueOffset;
			//const double ValueScale = Series.ValueScale;
			double MinValue = std::numeric_limits<double>::infinity();
			double MaxValue = -std::numeric_limits<double>::infinity();

			if (Counter.IsFloatingPoint())
			{
				Counter.EnumerateFloatValues(Viewport.StartTime, Viewport.EndTime, [&Builder, &MinValue, &MaxValue](double Time, double Value)
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
				Counter.EnumerateValues(Viewport.StartTime, Viewport.EndTime, [&Builder, &MinValue, &MaxValue](double Time, int64 IntValue)
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

			const double ValueOffset = (MinValue != std::numeric_limits<double>::infinity()) ? -MinValue : 0.0;

			const double AdjustedHeight = GetHeight() - 3.0f;
			const double ValueScale = (MinValue < MaxValue) ? AdjustedHeight / ScaleY / (MaxValue - MinValue) : 1.0;

			if (Counter.IsFloatingPoint())
			{
				Counter.EnumerateFloatValues(Viewport.StartTime, Viewport.EndTime, [&Builder, ValueOffset, ValueScale](double Time, double Value)
				{
					//TODO: add a "value converter"
					Builder.AddEvent(Time, 0.0, (Value + ValueOffset) * ValueScale);
				});
			}
			else
			{
				Counter.EnumerateValues(Viewport.StartTime, Viewport.EndTime, [&Builder, ValueOffset, ValueScale](double Time, int64 IntValue)
				{
					//TODO: add a "value converter"
					Builder.AddEvent(Time, 0.0, (static_cast<double>(IntValue) + ValueOffset) * ValueScale);
				});
			}
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

	if (Track.bDrawLines || Track.bDrawPolygon)
	{
		AddConnectedLine(Time, Value);

		if (Track.bUseEventDuration && Duration != 0.0)
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
	int32 MaxPointsPerLineScan = FMath::CeilToInt(Track.GetHeight() / FGraphTrack::PointSizeY) + 1;
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

	// Transform Value to Y coordinate (local track space) and ensure Y is not infinite.
	const float Y = FMath::Clamp<float>(Track.GetYForValue(Value), -FLT_MAX, FLT_MAX);

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
