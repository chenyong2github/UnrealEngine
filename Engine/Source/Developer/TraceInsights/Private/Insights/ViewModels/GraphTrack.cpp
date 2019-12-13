// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Insights/ViewModels/GraphTrack.h"

#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ViewModels/DrawHelpers.h"
#include "Insights/ViewModels/GraphSeries.h"
#include "Insights/ViewModels/GraphTrackBuilder.h"
#include "Insights/ViewModels/GraphTrackEvent.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Insights/Widgets/SGraphSeriesList.h"

#define LOCTEXT_NAMESPACE "GraphTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FGraphTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrack::FGraphTrack(const FName& InSubType)
	: FBaseTimingTrack(FName(TEXT("Graph")), InSubType)
	//, AllSeries()
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, PointBrush(FEditorStyle::GetBrush("Graph.ExecutionBubble"))
	, BorderBrush(FInsightsStyle::Get().GetBrush("SingleBorder"))
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	, bDrawDebugInfo(false)
	, bDrawPoints(true)
	, bDrawPointsWithBorder(true)
	, bDrawLines(true)
	, bDrawPolygon(true)
	, bUseEventDuration(true)
	, bDrawBoxes(true)
	, bDrawBaseline(true)
	, NumAddedEvents(0)
	, NumDrawPoints(0)
	, NumDrawLines(0)
	, NumDrawBoxes(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FGraphTrack::FGraphTrack(const FName& InSubType, const FString& InName)
	: FBaseTimingTrack(FName(TEXT("Graph")), InSubType, InName)
	//, AllSeries()
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, PointBrush(FEditorStyle::GetBrush("Graph.ExecutionBubble"))
	, BorderBrush(FInsightsStyle::Get().GetBrush("SingleBorder"))
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	, bDrawDebugInfo(false)
	, bDrawPoints(true)
	, bDrawPointsWithBorder(true)
	, bDrawLines(true)
	, bDrawPolygon(true)
	, bUseEventDuration(true)
	, bDrawBoxes(true)
	, bDrawBaseline(true)
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
	AllSeries.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::PostUpdate(const ITimingTrackUpdateContext& Context)
{
	constexpr float HeaderWidth = 100.0f;
	constexpr float HeaderHeight = 14.0f;

	const float MouseX = Context.GetMousePosition().X;
	const float MouseY = Context.GetMousePosition().Y;

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

	for (const TSharedPtr<FGraphSeries>& Series : AllSeries)
	{
		if (Series->IsVisible())
		{
			NumDrawPoints += Series->Points.Num();
			for (int32 BatchIndex = 0; BatchIndex < Series->LinePoints.Num(); ++BatchIndex)
			{
				NumDrawLines += Series->LinePoints[BatchIndex].Num() / 2;
			}
			NumDrawBoxes += Series->Boxes.Num();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::PreDraw(const ITimingTrackDrawContext& Context) const
{
	FDrawContext& DrawContext = Context.GetDrawContext();
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	FDrawHelpers::DrawBackground(DrawContext, WhiteBrush, Viewport, GetPosY(), GetHeight());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	FDrawContext& DrawContext = Context.GetDrawContext();
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	// Set clipping area.
	{
		const FVector2D AbsPos = DrawContext.Geometry.GetAbsolutePosition();
		const float Scale = DrawContext.Geometry.GetAccumulatedLayoutTransform().GetScale();
		const float L = AbsPos.X;
		const float R = AbsPos.X + (Viewport.GetWidth() * Scale);
		const float T = AbsPos.Y + (GetPosY() * Scale);
		const float B = AbsPos.Y + ((GetPosY() + GetHeight()) * Scale);
		const FSlateClippingZone ClipZone(FVector2D(L, T), FVector2D(R, T), FVector2D(L, B), FVector2D(R, B));
		DrawContext.ElementList.PushClip(ClipZone);
	}

	// Draw top line.
	//DrawContext.DrawBox(0.0f, GetPosY(), Viewport.Width, 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));

	bool bDrawnBaseline = false;
	for (const TSharedPtr<FGraphSeries>& Series : AllSeries)
	{
		if (Series->IsVisible())
		{
			// Draw baseline (Value == 0), for the first visible series only.
			if (bDrawBaseline && !bDrawnBaseline)
			{
				const float BaselineY = FMath::RoundToFloat(static_cast<float>(Series->GetBaselineY()));
				if (BaselineY >= 0.0f && BaselineY < GetHeight())
				{
					DrawContext.DrawBox(0.0f, GetPosY() + BaselineY, Viewport.GetWidth(), 1.0f, WhiteBrush, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
					DrawContext.LayerId++;
				}

				bDrawnBaseline = true;
			}

			DrawSeries(*Series, DrawContext, Viewport);
		}
	}

	// Restore clipping.
	DrawContext.ElementList.PopClip();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::DrawSeries(const FGraphSeries& Series, FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const
{
	// Set clipping area. This area tries to take into account the optional border area that allows
	// graph tracks to optionally act like 'event' tracks if required (with respect to layout, anyway).
	// The GetBorderY() - 1.0f calculation is desgiend to avoid clipping the line rasterization, as the custom verts
	// of the fill and the outer lines appear to get rasterized differently, the latter missing one pixel
	// on its upper side.
	{
		const FVector2D AbsPos = DrawContext.Geometry.GetAbsolutePosition();
		const float Scale = DrawContext.Geometry.GetAccumulatedLayoutTransform().GetScale();
		const float L = AbsPos.X;
		const float R = AbsPos.X + (Viewport.GetWidth() * Scale);
		const float T = AbsPos.Y + ((GetPosY() + (GetBorderY() - 1.0f)) * Scale);
		const float B = AbsPos.Y + ((GetPosY() + (GetHeight() - (GetBorderY() - 1.0f))) * Scale);
		const FSlateClippingZone ClipZone(FVector2D(L, T), FVector2D(R, T), FVector2D(L, B), FVector2D(R, B));
		DrawContext.ElementList.PushClip(ClipZone);
	}

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

	const float LocalPosY = FMath::RoundToFloat(GetPosY());

	FPaintGeometry Geo = DrawContext.Geometry.ToPaintGeometry();
	Geo.AppendTransform(FSlateLayoutTransform(FVector2D(0.0f, LocalPosY * DrawContext.Geometry.GetAccumulatedLayoutTransform().GetScale())));

	if (bDrawPolygon)
	{
		FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
		const FSlateShaderResourceProxy* ResourceProxy = ResourceHandle.GetResourceProxy();

		FVector2D AtlasOffset = ResourceProxy ? ResourceProxy->StartUV : FVector2D(0.f, 0.f);
		FVector2D AtlasUVSize = ResourceProxy ? ResourceProxy->SizeUV : FVector2D(1.f, 1.f);

		const FVector2D Size = DrawContext.Geometry.GetLocalSize();

		const FSlateRenderTransform& RenderTransform = Geo.GetAccumulatedRenderTransform();

		FColor FillColor(Series.FillColor.R * 255, Series.FillColor.G * 255, Series.FillColor.B * 255, 42);

		for (int32 BatchIndex = 0; BatchIndex < Series.LinePoints.Num(); ++BatchIndex)
		{
			const TArray<FVector2D>& LinePoints = Series.LinePoints[BatchIndex];

			TArray<SlateIndex> Indices;
			TArray<FSlateVertex> Verts;

			Indices.Reserve(LinePoints.Num() * 6);
			Verts.Reserve(LinePoints.Num() * 2);

			const float TopV = 0.0f;
			const float BottomV = GetHeight() / Size.Y;
			const float BaselineY = static_cast<float>(Series.GetBaselineY());
			const float BaselineV = FMath::Clamp<float>(BaselineY / Size.Y, TopV, BottomV);

			int32 PrevSide = 0;

			for (int32 PointIndex = 0; PointIndex < LinePoints.Num(); ++PointIndex)
			{
				const FVector2D& LinePoint = LinePoints[PointIndex];

				// When crossing baseline the polygon needs to be intersected.
				int32 CrtSide = (LinePoint.Y > BaselineY) ? 1 : (LinePoint.Y < BaselineY) ? -1 : 0;
				if (PrevSide != 0 && CrtSide + PrevSide == 0) // alternating sides?
				{
					// Compute intersection point.
					const FVector2D& PrevLinePoint = LinePoints[PointIndex - 1];
					const float X = PrevLinePoint.X + (LinePoint.X - PrevLinePoint.X) / ((BaselineY - LinePoint.Y) / (PrevLinePoint.Y - BaselineY) + 1.0f);

					// Add an intersection point vertex.
					FVector2D UV(X / Size.X, BaselineV);
					Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, UV * Size, AtlasOffset + UV * AtlasUVSize, FillColor));

					// Add a value point vertex.
					UV.X = LinePoint.X / Size.X;
					UV.Y = TopV + LinePoint.Y / Size.Y;
					Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, UV * Size, AtlasOffset + UV * AtlasUVSize, FillColor));

					// Add a baseline vertex.
					UV.Y = BaselineV;
					Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, UV * Size, AtlasOffset + FVector2D(UV.X, 0.5f) * AtlasUVSize, FillColor));

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
					Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, UV * Size, AtlasOffset + UV * AtlasUVSize, FillColor));

					// Add a baseline vertex.
					UV.Y = BaselineV;
					Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, UV * Size, AtlasOffset + FVector2D(UV.X, 0.5f) * AtlasUVSize, FillColor));

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
	}

	if (bDrawLines)
	{
		// Find scale to get the size of a pixel
		const float InvScale = 1.0f / DrawContext.Geometry.GetAccumulatedLayoutTransform().GetScale();
		FPaintGeometry LineGeo = Geo;
		LineGeo.AppendTransform(FSlateLayoutTransform(FVector2D(0.5f * InvScale, 0.5f * InvScale)));

		// Disable pixel snapping here so lines line up with boxes/polys correctly.
		const ESlateDrawEffect LineDrawEffects = DrawContext.DrawEffects | ESlateDrawEffect::NoPixelSnapping;

		for (int32 BatchIndex = 0; BatchIndex < Series.LinePoints.Num(); ++BatchIndex)
		{
			const TArray<FVector2D>& LinePoints = Series.LinePoints[BatchIndex];
			if (LinePoints.Num() > 0)
			{
				FSlateDrawElement::MakeLines(DrawContext.ElementList, DrawContext.LayerId, LineGeo, LinePoints, LineDrawEffects, Series.Color, false, 1.0f);
			}
		}
		DrawContext.LayerId++;
	}

	// Restore clipping.
	DrawContext.ElementList.PopClip();

	if (bDrawPoints)
	{
		const int32 NumPoints = Series.Points.Num();

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
	}

	if (bDrawDebugInfo) // for debugging only
	{
		// Find scale to get the size of a pixel
		const float PixelUnit = 1.0f / DrawContext.Geometry.GetAccumulatedLayoutTransform().GetScale();
		FPaintGeometry LineGeo = Geo;
		LineGeo.AppendTransform(FSlateLayoutTransform(FVector2D(0.5f * PixelUnit, 0.5f * PixelUnit)));

		// Disable pixel snapping here so lines line up with boxes/polys correctly.
		const ESlateDrawEffect LineDrawEffects = DrawContext.DrawEffects | ESlateDrawEffect::NoPixelSnapping;

		// Draw white corner at (0, 0) using MakeLines.
		{
			TArray<FVector2D> HLine;
			HLine.Add(FVector2D(0.0f, 0.0f));
			HLine.Add(FVector2D(10.0f, 0.0f));
			FSlateDrawElement::MakeLines(DrawContext.ElementList, DrawContext.LayerId, LineGeo, HLine, LineDrawEffects, FLinearColor::White, false, 1.0f);

			TArray<FVector2D> VLine;
			VLine.Add(FVector2D(0.0f, 0.0f));
			VLine.Add(FVector2D(0.0f, 10.0f));
			FSlateDrawElement::MakeLines(DrawContext.ElementList, DrawContext.LayerId, LineGeo, VLine, LineDrawEffects, FLinearColor::White, false, 1.0f);
		}
		DrawContext.LayerId++;

		// Draw corner at (0, 0) using DrawBox.
		DrawContext.DrawBox(2.0f, LocalPosY, 6.0f, PixelUnit, WhiteBrush, Series.Color);
		DrawContext.DrawBox(0.0f, LocalPosY + 2.0f, PixelUnit, 6.0f, WhiteBrush, Series.Color);
		DrawContext.LayerId++;

		const int32 NumDbgPoints = Series.Points.Num();

		// Draw white cross lines (15x15) at each point.
		for (int32 Index = 0; Index < NumDbgPoints; ++Index)
		{
			const FVector2D& Pt = Series.Points[Index];
			const float PtX = Pt.X;
			const float PtY = Pt.Y;

			TArray<FVector2D> HLine;
			HLine.Add(FVector2D(PtX - 7.0f, PtY));
			HLine.Add(FVector2D(PtX + 8.0f, PtY));
			FSlateDrawElement::MakeLines(DrawContext.ElementList, DrawContext.LayerId, LineGeo, HLine, LineDrawEffects, FLinearColor::White, false, 1.0f);

			TArray<FVector2D> VLine;
			VLine.Add(FVector2D(PtX, PtY - 7.0f));
			VLine.Add(FVector2D(PtX, PtY + 8.0f));
			FSlateDrawElement::MakeLines(DrawContext.ElementList, DrawContext.LayerId, LineGeo, VLine, LineDrawEffects, FLinearColor::White, false, 1.0f);
		}
		DrawContext.LayerId++;

		// Draw cross lines (11x11) at each point.
		for (int32 Index = 0; Index < NumDbgPoints; ++Index)
		{
			const FVector2D& Pt = Series.Points[Index];
			const float PtX = Pt.X;
			const float PtY = LocalPosY + Pt.Y;
			DrawContext.DrawBox(PtX - 5.0f, PtY, 11.0f, PixelUnit, WhiteBrush, Series.Color);
			DrawContext.DrawBox(PtX, PtY - 5.0f, PixelUnit, 11.0f, WhiteBrush, Series.Color);
		}
		DrawContext.LayerId++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::DrawEvent(const ITimingTrackDrawContext& Context, const ITimingEvent& InTimingEvent, EDrawEventMode InDrawMode) const
{
	ensure(InTimingEvent.CheckTrack(this));
	ensure(FGraphTrackEvent::CheckTypeName(InTimingEvent));

	const FGraphTrackEvent& GraphEvent = static_cast<const FGraphTrackEvent&>(InTimingEvent);
	const TSharedPtr<const FGraphSeries> Series = GraphEvent.GetSeries();

	const FTimingTrackViewport& Viewport = Context.GetViewport();
	FDrawContext& DrawContext = Context.GetDrawContext();

	const float EventX1 = Viewport.TimeToSlateUnitsRounded(GraphEvent.GetStartTime());
	const float EventX2 = Viewport.TimeToSlateUnitsRounded(GraphEvent.GetEndTime());

	const float EventY1 = GetPosY() + Series->GetRoundedYForValue(GraphEvent.GetValue());

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

		const float EventY2 = GetPosY() + static_cast<float>(Series->GetBaselineY());

		const float Y1 = FMath::Min(EventY1, EventY2);
		const float DY = FMath::Abs(EventY2 - EventY1);

		DrawContext.DrawBox(EventX1 - 1.0f, Y1 - 1.0f, W + 2.0f, DY + 3.0f, WhiteBrush, HighlightColor);
		DrawContext.LayerId++;
		DrawContext.DrawBox(EventX1, Y1, W, DY + 1.0f, WhiteBrush, Series->Color);
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
		DrawContext.DrawBox(EventX1, EventY1, W, 1.0f, WhiteBrush, Series->Color);
		DrawContext.LayerId++;
	}

	// Draw highlighted point.
	DrawContext.DrawBox(EventX1 - PointVisualSize / 2.0f - 1.5f, EventY1 - PointVisualSize / 2.0f - 1.5f, PointVisualSize + 4.0f, PointVisualSize + 4.0f, PointBrush, HighlightColor);
	DrawContext.LayerId++;
	DrawContext.DrawBox(EventX1 - PointVisualSize / 2.0f + 0.5f, EventY1 - PointVisualSize / 2.0f + 0.5f, PointVisualSize, PointVisualSize, PointBrush, Series->Color);
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	if (InTooltipEvent.CheckTrack(this) && FGraphTrackEvent::CheckTypeName(InTooltipEvent))
	{
		const FGraphTrackEvent& TooltipEvent = static_cast<const FGraphTrackEvent&>(InTooltipEvent);
		const TSharedRef<const FGraphSeries> Series = TooltipEvent.GetSeries();

		InOutTooltip.ResetContent();
		InOutTooltip.AddTitle(Series->GetName().ToString(), Series->GetColor());
		InOutTooltip.AddNameValueTextLine(TEXT("Time:"), TimeUtils::FormatTimeAuto(TooltipEvent.GetStartTime()));
		InOutTooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(TooltipEvent.GetDuration()));
		InOutTooltip.AddNameValueTextLine(TEXT("Value:"), Series->FormatValue(TooltipEvent.GetValue()));
		InOutTooltip.UpdateLayout();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FGraphTrack::GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const
{
	const float LocalPosX = InPosX;
	const float LocalPosY = InPosY - GetPosY();

	const bool bCheckLine = bUseEventDuration && (bDrawLines || bDrawPolygon);

	// Search series in reverse order.
	for (int32 SeriesIndex = AllSeries.Num() - 1; SeriesIndex >= 0; --SeriesIndex)
	{
		const TSharedPtr<FGraphSeries>& Series = AllSeries[SeriesIndex];
		if (Series->IsVisible())
		{
			const FGraphSeriesEvent* Event = Series->GetEvent(LocalPosX, LocalPosY, Viewport, bCheckLine, bDrawBoxes);
			if (Event != nullptr)
			{
				return MakeShared<FGraphTrackEvent>(SharedThis(this), Series.ToSharedRef(), *Event);
			}
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("Misc");
	{
		if (false) // debug functionality
		{
			FUIAction Action_ShowDebugInfo
			(
				FExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowDebugInfo_Execute),
				FCanExecuteAction::CreateSP(this, &FGraphTrack::ContextMenu_ShowDebugInfo_CanExecute),
				FIsActionChecked::CreateSP(this, &FGraphTrack::ContextMenu_ShowDebugInfo_IsChecked)
			);
			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("ContextMenu_ShowDebugInfo", "Show Debug Info"),
				LOCTEXT("ContextMenu_ShowDebugInfo_Desc", "Show Debug Info."),
				FSlateIcon(),
				Action_ShowDebugInfo,
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}

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
		MenuBuilder.AddWidget(
			SNew(SBox)
			.MaxDesiredHeight(150.0f)
			[
				SNew(SGraphSeriesList, SharedThis(this))
			], 
			FText::GetEmpty(), true);
	}
	MenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FGraphTrack::ContextMenu_ShowDebugInfo_Execute()
{
	bDrawDebugInfo = !bDrawDebugInfo;
	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowDebugInfo_CanExecute()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FGraphTrack::ContextMenu_ShowDebugInfo_IsChecked()
{
	return bDrawDebugInfo;
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

FRandomGraphTrack::FRandomGraphTrack()
	: FGraphTrack(FName(TEXT("Random")))
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
	TSharedRef<FGraphSeries> Series0 = MakeShared<FGraphSeries>();
	Series0->SetName(TEXT("Random Blue"));
	Series0->SetDescription(TEXT("Random series; for debuging purposes"));
	Series0->SetColor(FLinearColor(0.1f, 0.5f, 1.0f, 1.0f), FLinearColor(0.4f, 0.8f, 1.0f, 1.0f));
	Series0->SetVisibility(true);
	AllSeries.Add(Series0);

	TSharedRef<FGraphSeries> Series1 = MakeShared<FGraphSeries>();
	Series1->SetName(TEXT("Random Yellow"));
	Series1->SetDescription(TEXT("Random series; for debuging purposes"));
	Series1->SetColor(FLinearColor(0.9f, 0.9f, 0.1f, 1.0f), FLinearColor(1.0f, 1.0f, 0.4f, 1.0f));
	Series1->SetVisibility(false);
	AllSeries.Add(Series1);

	TSharedRef<FGraphSeries> Series2 = MakeShared<FGraphSeries>();
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

void FRandomGraphTrack::Update(const ITimingTrackUpdateContext& Context)
{
	FGraphTrack::Update(Context);

	if (IsDirty())
	{
		ClearDirtyFlag();

		NumAddedEvents = 0;

		const FTimingTrackViewport& Viewport = Context.GetViewport();

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

#undef LOCTEXT_NAMESPACE
