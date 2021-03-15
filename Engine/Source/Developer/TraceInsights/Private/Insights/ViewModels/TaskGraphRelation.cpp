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

	FLinearColor RelationColors[(int32)ETaskGraphRelationType::Count] = {
		FLinearColor::Yellow, // Created
		FLinearColor::Green, // Launched
		FLinearColor::Red, // Prerequisite
		FLinearColor::Blue, // Scheduled
		FLinearColor::Blue, // AddedNested
		FLinearColor::Red, // NestedCompleted
		FLinearColor::Red, // Subsequent
		FLinearColor::Yellow, // Completed
	};

	FLinearColor Color = RelationColors[(int32)Type];
	DrawContext.DrawLines(Helper.GetRelationLayerId(), X1, Y1, Points, ESlateDrawEffect::None, Color, /*bAntialias=*/ true, /*Thickness=*/ 2.0);
}

} // namespace Insights