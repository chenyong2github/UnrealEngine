// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrajectoryDrawInfo.h"

#include "MotionTrailEditorMode.h"
#include "TrailHierarchy.h"
#include "TrajectoryCache.h"

namespace UE
{
namespace MotionTrailEditor
{

TOptional<FVector2D> FTrailScreenSpaceTransform::ProjectPoint(const FVector& Point) const
{
	const FPlane Projection = View->Project(Point);
	if (Projection.W > 0.0f)
	{
		const float XPos = HalfScreenSize.X + (HalfScreenSize.X * Projection.X * 1.0f);
		const float YPos = HalfScreenSize.Y + (HalfScreenSize.Y * Projection.Y * -1.0f);
		return FVector2D(XPos, YPos);
	}

	return TOptional<FVector2D>();
}

TArray<FVector> FTrajectoryDrawInfo::GetTrajectoryPointsForDisplay(const FDisplayContext& InDisplayContext)
{
	// TODO: return all points in range
	TArray<FVector> TrajectoryPoints;
	CachedViewRange = InDisplayContext.TimeRange;

	TArray<double> TrajectoryTimes = TrajectoryCache->GetAllTimesInRange(InDisplayContext.TimeRange);
	TrajectoryPoints.Reserve(TrajectoryTimes.Num());
	
	for (const double Time : TrajectoryTimes)
	{
		TrajectoryPoints.Add(TrajectoryCache->Get(Time).GetTranslation());
	}

	return TrajectoryPoints;
}

void FTrajectoryDrawInfo::GetTickPointsForDisplay(const FDisplayContext& InDisplayContext, TArray<FVector2D>& Ticks, TArray<FVector2D>& TickNormals)
{
	const double FirstTick = FMath::FloorToDouble(InDisplayContext.TimeRange.GetLowerBoundValue() / InDisplayContext.SecondsPerTick) * InDisplayContext.SecondsPerTick;
	const double Spacing = InDisplayContext.TrailHierarchy->GetSecondsPerSegment();

	for (double TickItr = FirstTick + InDisplayContext.SecondsPerTick; TickItr < InDisplayContext.TimeRange.GetUpperBoundValue(); TickItr += InDisplayContext.SecondsPerTick)
	{
		FVector Interpolated = TrajectoryCache->GetInterp(TickItr).GetTranslation();
		TOptional<FVector2D> Projected = InDisplayContext.ScreenSpaceTransform.ProjectPoint(Interpolated);
		const double PrevSampleTime = TickItr + Spacing < InDisplayContext.TimeRange.GetUpperBoundValue() ? TickItr + Spacing : TickItr - Spacing;
		TOptional<FVector2D> PrevProjected = InDisplayContext.ScreenSpaceTransform.ProjectPoint(TrajectoryCache->GetInterp(PrevSampleTime).GetTranslation());

		if (Projected && PrevProjected)
		{
			Ticks.Add(Projected.GetValue());

			FVector2D Diff = Projected.GetValue() - PrevProjected.GetValue();
			Diff.Normalize();

			TickNormals.Add(FVector2D(-Diff.Y, Diff.X));
		}
	}
}

} // namespace MovieScene
} // namespace UE
