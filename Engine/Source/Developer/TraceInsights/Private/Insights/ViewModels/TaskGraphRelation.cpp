// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskGraphRelation.h"

#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/Common/PaintUtils.h"

namespace Insights
{

FTaskGraphRelation::FTaskGraphRelation(double InSourceTime, int32 InSourceThreadId, double InTargetTime, int32 InTargetThreadId, ETaskGraphRelationType InType)
{
	SourceTime = InSourceTime;
	SourceThreadId = InSourceThreadId;
	TargetTime = InTargetTime;
	TargetThreadId = InTargetThreadId;
	Type = InType;
}

void FTaskGraphRelation::Draw(const FDrawContext& DrawContext, const FTimingTrackViewport& Viewport, const ITimingViewDrawHelper& Helper)
{
	float X1 = Viewport.TimeToSlateUnitsRounded(SourceTime);
	float Y1 = SourceTrack.Pin()->GetPosY();
	Y1 += Viewport.GetLayout().GetLaneY(SourceDepth) + Viewport.GetLayout().EventH / 2;

	float X2 = Viewport.TimeToSlateUnitsRounded(TargetTime);
	float Y2 = TargetTrack.Pin()->GetPosY();
	Y2 += Viewport.GetLayout().GetLaneY(TargetDepth) + Viewport.GetLayout().EventH / 2;

	TArray<FVector2D> Points;
	Points.Emplace(0, 0);
	Points.Emplace(X2 - X1, Y2 - Y1);

	FLinearColor Color = Type == ETaskGraphRelationType::Subsequent ? FLinearColor::Red : FLinearColor::Blue;
	DrawContext.DrawLines(Helper.GetRelationLayerId(), X1, Y1, Points, ESlateDrawEffect::None, Color);
}

} // namespace Insights