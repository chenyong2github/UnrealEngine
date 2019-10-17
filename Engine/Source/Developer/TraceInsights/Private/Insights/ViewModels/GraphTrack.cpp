// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GraphTrack.h"

#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ViewModels/DrawHelpers.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "GraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphSeries
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphSeries::FGraphSeries()
	: Name()
	, Description()
	, bIsVisible(true)
	, bIsDirty(false)
	, bAutoZoom(false)
	, TargetAutoZoomLowValue(0.0)
	, TargetAutoZoomHighValue(1.0)
	, AutoZoomLowValue(0.0)
	, AutoZoomHighValue(1.0)
	, BaselineY(0.0)
	, ScaleY(1.0)
	, Color(0.0f, 0.5f, 1.0f, 1.0f)
	, BorderColor(0.3f, 0.8f, 1.0f, 1.0f)
	//, Events()
	//, Points()
	//, LinePoints()
	//, Boxes()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphSeries::~FGraphSeries()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FGraphSeries::FEvent* FGraphSeries::GetEvent(const float X, const float Y, const FTimingTrackViewport& Viewport, bool bCheckLine, bool bCheckBox) const
{
	const float LocalBaselineY = static_cast<float>(GetBaselineY());

	for (const FGraphSeries::FEvent& Event : Events)
	{
		const float EventX1 = Viewport.TimeToSlateUnitsRounded(Event.Time);
		const float EventX2 = Viewport.TimeToSlateUnitsRounded(Event.Time + Event.Duration);

		const float EventY = GetRoundedYForValue(Event.Value);

		// Check bounding box of the visual point.
		constexpr float PointTolerance = 5.0f;
		if (X >= EventX1 - PointTolerance && X <= EventX1 + PointTolerance &&
			Y >= EventY - PointTolerance && Y <= EventY + PointTolerance)
		{
			return &Event;
		}

		if (bCheckLine)
		{
			// Check bounding box of the horizontal line.
			constexpr float LineTolerance = 2.0f;
			if (X >= EventX1 - LineTolerance && X <= EventX2 + LineTolerance &&
				Y >= EventY - LineTolerance && Y <= EventY + LineTolerance)
			{
				return &Event;
			}
		}

		if (bCheckBox)
		{
			// Check bounding box of the visual box.
			constexpr float BoxTolerance = 1.0f;
			if (X >= EventX1 - BoxTolerance && X <= EventX2 + BoxTolerance)
			{
				if (EventY < LocalBaselineY)
				{
					if (Y >= EventY - BoxTolerance && Y <= LocalBaselineY + BoxTolerance)
					{
						return &Event;
					}
				}
				else
				{
					if (Y >= LocalBaselineY - BoxTolerance && Y <= EventY + BoxTolerance)
					{
						return &Event;
					}
				}
			}
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString FGraphSeries::FormatValue(double Value) const
{
	return FString::Printf(TEXT("%g"), Value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrack::FGraphTrack(uint64 InTrackId)
	: FBaseTimingTrack(InTrackId)
	//, AllSeries()
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, PointBrush(FEditorStyle::GetBrush("Graph.ExecutionBubble"))
	, BorderBrush(FInsightsStyle::Get().GetBrush("SingleBorder"))
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	, bDrawPoints(true)
	, bDrawPointsWithBorder(true)
	, bDrawLines(true)
	, bDrawPolygon(true)
	, bUseEventDuration(true)
	, bDrawBoxes(true)
	, HoveredGraphEvent()
	, Tooltip()
	, TooltipMousePosition(0.0f, 0.0f)
	, NumAddedEvents(0)
	, NumDrawPoints(0)
	, NumDrawLines(0)
	, NumDrawBoxes(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrack::~FGraphTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::Reset()
{
	HoveredGraphEvent.Reset();
	Tooltip.Reset();
	TooltipMousePosition.Set(0.0f, 0.0f);

	AllSeries.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::UpdateHoveredState(float MouseX, float MouseY, const FTimingTrackViewport& Viewport)
{
	constexpr float HeaderWidth = 100.0f;
	constexpr float HeaderHeight = 14.0f;

	HoveredGraphEvent.Reset();

	if (MouseY >= GetPosY() && MouseY < GetPosY() + GetHeight())
	{
		SetHoveredState(true);
		SetHeaderHoveredState(MouseX < HeaderWidth && MouseY < GetPosY() + HeaderHeight);

		GetEvent(MouseX, MouseY, Viewport, HoveredGraphEvent);
	}
	else
	{
		SetHoveredState(false);
	}

	if (HoveredGraphEvent.IsValid())
	{
		InitTooltip();
		Tooltip.SetDesiredOpacity(1.0f);
	}
	else
	{
		Tooltip.SetDesiredOpacity(0.0f);
	}
	TooltipMousePosition.Set(MouseX, MouseY);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::Update(const FTimingTrackViewport& Viewport)
{
	Tooltip.Update();

	if (!TooltipMousePosition.IsZero())
	{
		Tooltip.SetPosition(TooltipMousePosition, 0.0f, Viewport.GetWidth(), 0.0f, Viewport.GetHeight());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::UpdateStats()
{
	NumDrawPoints = 0;
	NumDrawLines = 0;
	NumDrawBoxes = 0;

	for (const TSharedPtr<FGraphSeries>& Series : AllSeries)
	{
		if (Series->IsVisible())
		{
			NumDrawPoints += Series->Points.Num();
			NumDrawLines += Series->LinePoints.Num() / 2;
			NumDrawBoxes += Series->Boxes.Num();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::DrawBackground(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const
{
	FDrawHelpers::DrawBackground(DrawContext, WhiteBrush, Viewport, GetPosY(), GetHeight());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::Draw(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport, const FVector2D& MousePosition) const
{
	DrawBackground(DrawContext, Viewport);

	{
		FVector2D AbsPos = DrawContext.Geometry.GetAbsolutePosition();
		const float L = AbsPos.X;
		const float R = AbsPos.X + Viewport.GetWidth();
		const float T = AbsPos.Y + GetPosY();
		const float B = AbsPos.Y + GetPosY() + GetHeight();
		FSlateClippingZone ClipZone(FVector2D(L, T), FVector2D(R, T), FVector2D(L, B), FVector2D(R, B));
		DrawContext.ElementList.PushClip(ClipZone);
	}

	// Draw top line.
	//DrawContext.DrawBox(0.0f, GetPosY(), Viewport.Width, 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));

	// Draw baseline (Value == 0), for the first series only.
	if (AllSeries.Num() > 0)
	{
		const TSharedPtr<FGraphSeries>& Series = AllSeries[0];
		const float BaselineY = FMath::RoundToFloat(static_cast<float>(Series->GetBaselineY()));
		if (BaselineY >= 0.0f && BaselineY < GetHeight())
		{
			DrawContext.DrawBox(0.0f, GetPosY() + BaselineY, Viewport.GetWidth(), 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
			DrawContext.LayerId++;
		}
	}

	for (const TSharedPtr<FGraphSeries>& Series : AllSeries)
	{
		if (Series->IsVisible())
		{
			DrawSeries(*Series, DrawContext, Viewport);
		}
	}

	if (HoveredGraphEvent.IsValid())
	{
		DrawHighlightedEvent(DrawContext, Viewport, HoveredGraphEvent);
	}

	DrawContext.ElementList.PopClip();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::PostDraw(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport, const FVector2D& MousePosition) const
{
	Tooltip.Draw(DrawContext);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::DrawSeries(const FGraphSeries& Series, FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const
{
	if (bDrawBoxes)
	{
		int32 NumBoxes = Series.Boxes.Num();
		const float TrackPosY = GetPosY();
		const float BaselineY = FMath::RoundToFloat(static_cast<float>(Series.GetBaselineY()));
		for (int32 Index = 0; Index < NumBoxes; ++Index)
		{
			const FGraphSeries::FBox& Box = Series.Boxes[Index];
			if (BaselineY >= Box.Y)
			{
				DrawContext.DrawBox(Box.X, TrackPosY + Box.Y, Box.W, BaselineY - Box.Y + 1.0f, WhiteBrush, Series.Color);
			}
			else
			{
				DrawContext.DrawBox(Box.X, TrackPosY + BaselineY, Box.W, Box.Y - BaselineY + 1.0f, WhiteBrush, Series.Color);
			}
		}
		DrawContext.LayerId++;
	}

	if (bDrawPolygon)
	{
		FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
		const FSlateShaderResourceProxy* ResourceProxy = ResourceHandle.GetResourceProxy();

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

		const float TopV = GetPosY() / Size.Y;
		const float BottomV = (GetPosY() + GetHeight()) / Size.Y;
		const float BaselineY = static_cast<float>(Series.GetBaselineY());
		const float BaselineV = FMath::Clamp<float>((GetPosY() + BaselineY) / Size.Y, TopV, BottomV);

		int32 PrevSide = 0;

		for (int32 PointIndex = 0; PointIndex < Series.LinePoints.Num(); ++PointIndex)
		{
			const FVector2D& LinePoint = Series.LinePoints[PointIndex];

			// When crossing baseline the polygon needs to be intersected.
			int32 CrtSide = (LinePoint.Y > BaselineY) ? 1 : (LinePoint.Y < BaselineY) ? -1 : 0;
			if (PrevSide != 0 && CrtSide + PrevSide == 0) // alternating sides?
			{
				// Compute intersection point.
				const FVector2D& PrevLinePoint = Series.LinePoints[PointIndex - 1];
				float X = PrevLinePoint.X + (LinePoint.X - PrevLinePoint.X) / ((BaselineY - LinePoint.Y) / (PrevLinePoint.Y - BaselineY) + 1.0f);

				// Add an interesection point vertex.
				FVector2D UV(X / Size.X, BaselineV);
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, Pos + UV * Size * Scale, AtlasOffset + UV * AtlasUVSize, FillColor));

				// Add a value point vertex.
				UV.X = LinePoint.X / Size.X;
				UV.Y = TopV + LinePoint.Y / Size.Y;
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, Pos + UV * Size * Scale, AtlasOffset + UV * AtlasUVSize, FillColor));

				// Add a baseline vertex.
				UV.Y = BaselineV;
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, Pos + UV * Size * Scale, AtlasOffset + FVector2D(UV.X, 0.5f) * AtlasUVSize, FillColor));

				int32 Index0 = Verts.Num() - 5;
				int32 Index1 = Verts.Num() - 4;
				int32 Index2 = Verts.Num() - 3;
				int32 Index3 = Verts.Num() - 2;
				int32 Index4 = Verts.Num() - 1;

				Indices.Add(Index0);
				Indices.Add(Index1);
				Indices.Add(Index2);

				Indices.Add(Index2);
				Indices.Add(Index3);
				Indices.Add(Index4);
			}
			else
			{
				// Add a value point vertex.
				FVector2D UV(LinePoint.X / Size.X, TopV + LinePoint.Y / Size.Y);
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, Pos + UV * Size * Scale, AtlasOffset + UV * AtlasUVSize, FillColor));

				// Add a baseline vertex.
				UV.Y = BaselineV;
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, Pos + UV * Size * Scale, AtlasOffset + FVector2D(UV.X, 0.5f) * AtlasUVSize, FillColor));

				if (Verts.Num() >= 4)
				{
					int32 Index0 = Verts.Num() - 4;
					int32 Index1 = Verts.Num() - 3;
					int32 Index2 = Verts.Num() - 2;
					int32 Index3 = Verts.Num() - 1;

					Indices.Add(Index0);
					Indices.Add(Index1);
					Indices.Add(Index2);

					Indices.Add(Index2);
					Indices.Add(Index1);
					Indices.Add(Index3);
				}
			}
			PrevSide = CrtSide;
		}

		if (Indices.Num() > 0)
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

	const float LocalPosY = FMath::RoundToFloat(GetPosY());

	if (bDrawLines)
	{
		FPaintGeometry Geo = DrawContext.Geometry.ToPaintGeometry();
		Geo.AppendTransform(FSlateLayoutTransform(FVector2D(1.0f, LocalPosY + 1.0f)));
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
				const float PtX = Pt.X - PointVisualSize / 2.0f - 0.5f;
				const float PtY = LocalPosY + Pt.Y - PointVisualSize / 2.0f - 0.5f;
				DrawContext.DrawBox(PtX, PtY, PointVisualSize + 2.0f, PointVisualSize + 2.0f, PointBrush, Series.BorderColor);
			}
			DrawContext.LayerId++;
		}

		// Draw points (interior).
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const FVector2D& Pt = Series.Points[Index];
			const float PtX = Pt.X - PointVisualSize / 2.0f + 0.5f;
			const float PtY = LocalPosY + Pt.Y - PointVisualSize / 2.0f + 0.5f;
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
				const float PtX = Pt.X - BorderPtSize / 2.0f + 0.5f;
				const float PtY = LocalPosY + Pt.Y - BorderPtSize / 2.0f + 0.5f;
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
			const float PtX = Pt.X - PtSize / 2.0f + 0.5f;
			const float PtY = LocalPosY + Pt.Y - PtSize / 2.0f + 0.5f;
			DrawContext.DrawBox(PtX, PtY, PtSize, PtSize, WhiteBrush, Series.Color);
			//DrawContext.DrawRotatedBox(PtX, PtY, PtSize, PtSize, WhiteBrush, Series.Color, Angle, RotationPoint);
		}
		DrawContext.LayerId++;

#endif

		if (false) // for debugging only
		{
			// Draw points (cross).
			for (int32 Index = 0; Index < NumPoints; ++Index)
			{
				const FVector2D& Pt = Series.Points[Index];
				const float PtX = Pt.X;
				const float PtY = LocalPosY + Pt.Y;
				DrawContext.DrawBox(PtX - 5.0f, PtY, 11.0f, 1.0f, WhiteBrush, Series.Color);
				DrawContext.DrawBox(PtX, PtY - 5.0f, 1.0f, 11.0f, WhiteBrush, Series.Color);
			}
			DrawContext.LayerId++;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::DrawHighlightedEvent(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport, const FGraphTrack::FEvent& GraphEvent) const
{
	ensure(GraphEvent.IsValid());

	const float EventX1 = Viewport.TimeToSlateUnitsRounded(GraphEvent.SeriesEvent.Time);
	const float EventX2 = Viewport.TimeToSlateUnitsRounded(GraphEvent.SeriesEvent.Time + GraphEvent.SeriesEvent.Duration);

	const float EventY1 = GetPosY() + GraphEvent.Series->GetRoundedYForValue(GraphEvent.SeriesEvent.Value);

	const FLinearColor HighlightColor(1.0f, 1.0f, 0.0f, 1.0f);

	// Draw highlighted box.
	if (bDrawBoxes)
	{
		float W = EventX2 - EventX1;
		ensure(W >= 0); // we expect events to be sorted

		// Timing events are displayed with minimum 1px (including empty ones).
		if (W == 0)
		{
			W = 1.0f;
		}

		const float EventY2 = GetPosY() + static_cast<float>(GraphEvent.Series->GetBaselineY());

		const float Y1 = FMath::Min(EventY1, EventY2);
		const float DY = FMath::Abs(EventY2 - EventY1);

		DrawContext.DrawBox(EventX1 - 1.0f, Y1 - 1.0f, W + 2.0f, DY + 3.0f, WhiteBrush, HighlightColor);
		DrawContext.LayerId++;
		DrawContext.DrawBox(EventX1, Y1, W, DY + 1.0f, WhiteBrush, GraphEvent.Series->Color);
		DrawContext.LayerId++;
	}

	// Draw highlighted line.
	if ((EventX2 > EventX1) && ((bUseEventDuration && (bDrawLines || bDrawPolygon)) || bDrawBoxes))
	{
		float W = EventX2 - EventX1;
		ensure(W >= 0); // we expect events to be sorted

		// Timing events are displayed with minimum 1px (including empty ones).
		if (W == 0)
		{
			W = 1.0f;
		}

		DrawContext.DrawBox(EventX1 - 1.0f, EventY1 - 1.0f, W + 2.0f, 3.0f, WhiteBrush, HighlightColor);
		DrawContext.LayerId++;
		DrawContext.DrawBox(EventX1, EventY1, W, 1.0f, WhiteBrush, GraphEvent.Series->Color);
		DrawContext.LayerId++;
	}

	// Draw highlighted point.
	DrawContext.DrawBox(EventX1 - PointVisualSize / 2.0f - 1.5f, EventY1 - PointVisualSize / 2.0f - 1.5f, PointVisualSize + 4.0f, PointVisualSize + 4.0f, PointBrush, HighlightColor);
	DrawContext.LayerId++;
	DrawContext.DrawBox(EventX1 - PointVisualSize / 2.0f + 0.5f, EventY1 - PointVisualSize / 2.0f + 0.5f, PointVisualSize, PointVisualSize, PointBrush, GraphEvent.Series->Color);
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::InitTooltip()
{
	Tooltip.ResetContent();
	Tooltip.AddTitle(HoveredGraphEvent.Series->GetName().ToString(), HoveredGraphEvent.Series->GetColor());
	Tooltip.AddNameValueTextLine(TEXT("Time:"), TimeUtils::FormatTimeAuto(HoveredGraphEvent.SeriesEvent.Time));
	Tooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(HoveredGraphEvent.SeriesEvent.Duration));
	Tooltip.AddNameValueTextLine(TEXT("Value:"), HoveredGraphEvent.Series->FormatValue(HoveredGraphEvent.SeriesEvent.Value));
	Tooltip.UpdateLayout();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const bool FGraphTrack::GetEvent(const float MouseX, const float MouseY, const FTimingTrackViewport& Viewport, FGraphTrack::FEvent& OutEvent) const
{
	const float LocalPosX = MouseX;
	const float LocalPosY = MouseY - GetPosY();

	const bool bCheckLine = bUseEventDuration && (bDrawLines || bDrawPolygon);

	// Search series in reverse order.
	for (int32 SeriesIndex = AllSeries.Num() - 1; SeriesIndex >= 0; --SeriesIndex)
	{
		const TSharedPtr<FGraphSeries>& Series = AllSeries[SeriesIndex];
		if (Series->IsVisible())
		{
			const FGraphSeries::FEvent* Event = Series->GetEvent(LocalPosX, LocalPosY, Viewport, bCheckLine, bDrawBoxes);
			if (Event != nullptr)
			{
				OutEvent.Series = Series;
				OutEvent.SeriesEvent = *Event;
				return true;
			}
		}
	}

	return false;
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
		for (const TSharedPtr<FGraphSeries>& Series : AllSeries)
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

void FGraphTrack::ContextMenu_ShowSeries_Execute(FGraphSeries* Series)
{
	Series->SetVisibility(!Series->IsVisible());
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowSeries_CanExecute(FGraphSeries* Series)
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowSeries_IsChecked(FGraphSeries* Series)
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
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FRandomGraphTrack::AddDefaultSeries()
{
	TSharedPtr<FGraphSeries> Series0 = MakeShareable(new FGraphSeries());
	Series0->SetName(TEXT("Random Blue"));
	Series0->SetDescription(TEXT("Random series; for debuging purposes"));
	Series0->SetColor(FLinearColor(0.1f, 0.5f, 1.0f, 1.0f), FLinearColor(0.4f, 0.8f, 1.0f, 1.0f));
	Series0->SetVisibility(true);
	AllSeries.Add(Series0);

	TSharedPtr<FGraphSeries> Series1 = MakeShareable(new FGraphSeries());
	Series1->SetName(TEXT("Random Yellow"));
	Series1->SetDescription(TEXT("Random series; for debuging purposes"));
	Series1->SetColor(FLinearColor(0.9f, 0.9f, 0.1f, 1.0f), FLinearColor(1.0f, 1.0f, 0.4f, 1.0f));
	Series1->SetVisibility(false);
	AllSeries.Add(Series1);

	TSharedPtr<FGraphSeries> Series2 = MakeShareable(new FGraphSeries());
	Series2->SetName(TEXT("Random Red"));
	Series2->SetDescription(TEXT("Random series; for debuging purposes"));
	Series2->SetColor(FLinearColor(1.0f, 0.1f, 0.2f, 1.0f), FLinearColor(1.0f, 0.4f, 0.5f, 1.0f));
	Series2->SetVisibility(true);
	AllSeries.Add(Series2);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FRandomGraphTrack::~FRandomGraphTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FRandomGraphTrack::Update(const FTimingTrackViewport& Viewport)
{
	FGraphTrack::Update(Viewport);

	if (IsDirty())
	{
		ClearDirtyFlag();

		NumAddedEvents = 0;

		int32 Seed = 0;
		for (TSharedPtr<FGraphSeries>& Series : AllSeries)
		{
			Series->SetBaselineY(GetHeight() / 2.0);
			Series->SetScaleY(GetHeight());
			GenerateSeries(*Series, Viewport, 1000000, Seed++);
		}

		UpdateStats();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FRandomGraphTrack::GenerateSeries(FGraphSeries& Series, const FTimingTrackViewport& Viewport, const int32 EventCount, int32 Seed)
{
	//////////////////////////////////////////////////
	// Generate random events.

	constexpr double MinDeltaTime = 0.0000001; // 100ns
	constexpr double MaxDeltaTime = 0.01; // 100ms

	float MinValue = (Seed == 0) ? 0.0f : (Seed == 1) ? -0.25f : -0.5f;
	float MaxValue = (Seed == 0) ? 0.5f : (Seed == 1) ? +0.25f :  0.0f;

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
		while (Index < EventCount && Events[Index].Time < Viewport.GetStartTime())
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

			if (Ev.Time > Viewport.GetEndTime())
			{
				// one point outside screen (right side)
				break;
			}

			++Index;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrackBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrackBuilder::FGraphTrackBuilder(FGraphTrack& InTrack, FGraphSeries& InSeries, const FTimingTrackViewport& InViewport)
	: Track(InTrack)
	, Series(InSeries)
	, Viewport(InViewport)
{
	Series.Events.Reset();
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

	bool bKeepEvent = (Viewport.GetViewportDXForDuration(Duration) > 1.0f); // always keep the events wider than 1px

	//if (Track.bDrawPoints)
	{
		bKeepEvent |= AddPoint(Time, Value);
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

	if (bKeepEvent)
	{
		Series.Events.Add({ Time, Duration, Value });
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

bool FGraphTrackBuilder::AddPoint(double Time, double Value)
{
	const float X = Viewport.TimeToSlateUnitsRounded(Time);
	if (X < -FGraphTrack::PointVisualSize / 2.0f || X >= Viewport.GetWidth() + FGraphTrack::PointVisualSize / 2.0f)
	{
		return false;
	}

	// Align the X with a grid of GraphTrackPointDX pixels in size, in the global space (i.e. scroll independent).
	const double AlignedX = FMath::RoundToDouble(Time * Viewport.GetScaleX() / FGraphTrack::PointSizeX) * FGraphTrack::PointSizeX;

	if (AlignedX > PointsCurrentX + FGraphTrack::PointSizeX - 0.5)
	{
		FlushPoints();
		PointsCurrentX = AlignedX;
	}

	const float Y = Series.GetRoundedYForValue(Value);

	bool bIsNewVisiblePoint = false;

	int32 Index = FMath::RoundToInt(Y / FGraphTrack::PointSizeY);
	if (Index >= 0 && Index < PointsAtCurrentX.Num())
	{
		FPointInfo& Pt = PointsAtCurrentX[Index];
		bIsNewVisiblePoint = !Pt.bValid;
		Pt.bValid = true;
		Pt.X = X;
		Pt.Y = Y;
	}

	return bIsNewVisiblePoint;
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

	const float X = Viewport.TimeToSlateUnitsRounded(Viewport.RestrictEndTime(Time));
	const float Y = Series.GetRoundedYForValue(Value);

	ensure(X >= LinesCurrentX); // we are assuming events are already sorted by Time

	if (X < 0)
	{
		LinesCurrentX = X;
		LinesLastY = Y;
		return;
	}

	if (X >= Viewport.GetWidth())
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
	if (X1 > Viewport.GetWidth())
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

	const float Y = Series.GetRoundedYForValue(Value);

	// simple reduction
	if (W == 1.0f && Series.Boxes.Num() > 0)
	{
		FGraphSeries::FBox& LastBox = Series.Boxes.Last();
		if (LastBox.W == 1.0f && LastBox.X == X1)
		{
			const float RoundedBaselineY = FMath::RoundToFloat(Series.GetBaselineY());

			if (LastBox.Y < RoundedBaselineY)
			{
				if (Y < RoundedBaselineY)
				{
					// Merge current box with previous one.
					if (Y < LastBox.Y)
					{
						LastBox.Y = Y;
					}
					return;
				}
			}
			else
			{
				if (Y >= RoundedBaselineY)
				{
					// Merge current box with previous one.
					if (Y > LastBox.Y)
					{
						LastBox.Y = Y;
					}
					return;
				}
			}
		}
	}

	FGraphSeries::FBox Box;
	Box.X = X1;
	Box.W = W;
	Box.Y = Y;
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
