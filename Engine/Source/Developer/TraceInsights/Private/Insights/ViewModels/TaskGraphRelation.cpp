// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskGraphRelation.h"

#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/Common/PaintUtils.h"

namespace Insights
{

FTaskGraphRelation::FTaskGraphRelation(double InSourceTime, int32 InSourceThreadId, double InTargetTime, int32 InTargetThreadId, ETaskEventType InType)
{
	SourceTime = InSourceTime;
	SourceThreadId = InSourceThreadId;
	TargetTime = InTargetTime;
	TargetThreadId = InTargetThreadId;
	Type = InType;
}

void FTaskGraphRelation::Draw(const FDrawContext& DrawContext, const FTimingTrackViewport& Viewport, const ITimingViewDrawHelper& Helper, const ITimingEventRelation::EDrawFilter Filter)
{
	int32 LayerId = Helper.GetRelationLayerId();

	if (Filter == ITimingEventRelation::EDrawFilter::BetweenScrollableTracks)
	{
		if (SourceTrack.Pin()->GetLocation() > ETimingTrackLocation::Scrollable || TargetTrack.Pin()->GetLocation() > ETimingTrackLocation::Scrollable)
		{
			return;
		}
	}

	if (Filter == ITimingEventRelation::EDrawFilter::BetweenDockedTracks)
	{
		if (SourceTrack.Pin()->GetLocation() <= ETimingTrackLocation::Scrollable && TargetTrack.Pin()->GetLocation() <= ETimingTrackLocation::Scrollable)
		{
			return;
		}

		LayerId = DrawContext.LayerId;
	}

	float X1 = Viewport.TimeToSlateUnitsRounded(SourceTime);
	float Y1 = SourceTrack.Pin()->GetPosY();
	Y1 += Viewport.GetLayout().GetLaneY(SourceDepth) + Viewport.GetLayout().EventH / 2.0f;

	float X2 = Viewport.TimeToSlateUnitsRounded(TargetTime);
	float Y2 = TargetTrack.Pin()->GetPosY();
	Y2 += Viewport.GetLayout().GetLaneY(TargetDepth) + Viewport.GetLayout().EventH / 2.0f;

	FVector2D StartPoint = FVector2D(X1, Y1);
	FVector2D EndPoint = FVector2D(X2, Y2);
	float Distance = FVector2D::Distance(StartPoint, EndPoint);

	constexpr float DirectionFactor = 2.0f;
	constexpr float LineLenghtAtEnds = 20.0f;
	FVector2D StartDir((X2 - X1) / DirectionFactor, 0.0f);

	FLinearColor Color = FTaskGraphProfilerManager::Get()->GetColorForTaskEvent(Type);
	TArray<FVector2D> ArrowPoints;

	if (Distance > LineLenghtAtEnds)
	{
		FVector2D SplineStart(StartPoint.X + LineLenghtAtEnds, StartPoint.Y);
		FVector2D SplineEnd(EndPoint.X - LineLenghtAtEnds, EndPoint.Y);

		DrawContext.DrawSpline(LayerId, 0.0f, 0.0f, SplineStart, StartDir, SplineEnd, StartDir, /*Thickness=*/ 2.0f, Color);

		ArrowPoints.Add(StartPoint);
		ArrowPoints.Add(SplineStart);

		DrawContext.DrawLines(LayerId, 0.0f, 0.0f, ArrowPoints, ESlateDrawEffect::None, Color, /*bAntialias=*/ true, /*Thickness=*/ 2.0f);

		ArrowPoints.Empty();
		ArrowPoints.Add(SplineEnd);
		ArrowPoints.Add(EndPoint);
		DrawContext.DrawLines(LayerId, 0.0f, 0.0f, ArrowPoints, ESlateDrawEffect::None, Color, /*bAntialias=*/ true, /*Thickness=*/ 2.0f);
	}
	else
	{
		ArrowPoints.Empty();
		ArrowPoints.Add(StartPoint);
		ArrowPoints.Add(EndPoint);
		DrawContext.DrawLines(LayerId, 0.0f, 0.0f, ArrowPoints, ESlateDrawEffect::None, Color, /*bAntialias=*/ true, /*Thickness=*/ 2.0f);
	}

	FVector2D ArrowOrigin = EndPoint;

	constexpr float ArrowDirectionLen = -15.0f;
	constexpr float ArrowRotationAngle = 20.0f;
	FVector2D ArrowDirection(ArrowDirectionLen, 0.0f);

	ArrowPoints.Empty();
	ArrowPoints.Add(ArrowOrigin + ArrowDirection.GetRotated(ArrowRotationAngle));
	ArrowPoints.Add(ArrowOrigin);
	ArrowPoints.Add(ArrowOrigin + ArrowDirection.GetRotated(-ArrowRotationAngle));

	constexpr float YOffset = 1.0f; // Needed to align the arrow with the line. 
	DrawContext.DrawLines(LayerId, 0.0f, YOffset, ArrowPoints, ESlateDrawEffect::None, Color, /*bAntialias=*/ true, /*Thickness=*/ 2.0f);
}

} // namespace Insights