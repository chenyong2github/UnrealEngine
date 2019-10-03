// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Snapping/PointPlanarSnapSolver.h"
#include "VectorUtil.h"
#include "LineTypes.h"
#include "Quaternion.h"


FPointPlanarSnapSolver::FPointPlanarSnapSolver()
{
	Plane = FFrame3d();
}

void FPointPlanarSnapSolver::Reset()
{
	FBasePositionSnapSolver3::Reset();
	PointHistory.Reset();
}

void FPointPlanarSnapSolver::ResetActiveSnap()
{
	FBasePositionSnapSolver3::ResetActiveSnap();
	GeneratedLines.Reset();
	GeneratedTargets.Reset();
}


void FPointPlanarSnapSolver::UpdatePointHistory(const TArray<FVector3d>& Points)
{
	PointHistory = Points;
	GeneratedLines.Reset();
	GeneratedTargets.Reset();
}

void FPointPlanarSnapSolver::UpdatePointHistory(const TArray<FVector>& Points)
{
	PointHistory.Reset();
	int Num = Points.Num();
	for (int k = 0; k < Num; ++k)
	{
		PointHistory.Add(Points[k]);
	}
	GeneratedLines.Reset();
	GeneratedTargets.Reset();
}



void FPointPlanarSnapSolver::RegenerateTargetLines(bool bCardinalAxes, bool bLastHistorySegment)
{
	GeneratedLines.Reset();

	int NumHistoryPts = PointHistory.Num();
	if (NumHistoryPts == 0)
	{
		return;
	}

	FVector3d LastPt = PointHistory[NumHistoryPts - 1];

	if (bCardinalAxes)
	{
		FSnapTargetLine AxisLine;
		AxisLine.TargetID = CardinalAxisTargetID;
		AxisLine.Priority = CardinalAxisPriority;
		AxisLine.Line = FLine3d(LastPt, Plane.X());
		GeneratedLines.Add(AxisLine);
		AxisLine.Line = FLine3d(LastPt, Plane.Y());
		GeneratedLines.Add(AxisLine);
	}

	if (bLastHistorySegment && NumHistoryPts > 1)
	{
		FSnapTargetLine SegLine;
		SegLine.TargetID = LastSegmentTargetID;
		SegLine.Priority = LastSegmentPriority;
		SegLine.Line = FLine3d::FromPoints(LastPt, PointHistory[NumHistoryPts-2]);
		SegLine.Line.Direction = FQuaterniond(Plane.Z(), 90, true) * SegLine.Line.Direction;
		GeneratedLines.Add(SegLine);
	}
}


void FPointPlanarSnapSolver::GenerateTargets(const FVector3d& PointIn)
{
	GeneratedTargets.Reset();

	int NumSnapLines = GeneratedLines.Num();

	// nearest-point-on-line snaps
	for (int j = 0; j < NumSnapLines; ++j)
	{
		FSnapTargetPoint Target;
		Target.Position = GeneratedLines[j].Line.NearestPoint(PointIn);
		Target.TargetID = GeneratedLines[j].TargetID;
		Target.Priority = GeneratedLines[j].Priority;
		Target.bIsSnapLine = true;
		Target.SnapLine = GeneratedLines[j].Line;
		GeneratedTargets.Add(Target);
	}

	// length-along-line snaps
	if (bEnableSnapToKnownLengths)
	{
		int NumHistoryPts = PointHistory.Num();
		for (int j = 0; j < NumHistoryPts - 1; ++j)
		{
			double SegLength = PointHistory[j].Distance(PointHistory[j + 1]);
			for (int k = 0; k < NumSnapLines; ++k)
			{
				FSnapTargetPoint Target;
				Target.TargetID = GeneratedLines[k].TargetID;
				Target.Priority = GeneratedLines[k].Priority - KnownLengthPriorityDelta;
				Target.bIsSnapLine = true;
				Target.SnapLine = GeneratedLines[k].Line;
				Target.bIsSnapDistance = true;
				Target.SnapDistanceID = j;

				Target.Position = GeneratedLines[k].Line.PointAt(SegLength);
				GeneratedTargets.Add(Target);
				Target.Position = GeneratedLines[k].Line.PointAt(-SegLength);
				GeneratedTargets.Add(Target);
			}
		}
	}
}



void FPointPlanarSnapSolver::UpdateSnappedPoint(const FVector3d& PointIn)
{
	double MinMetric = TNumericLimits<double>::Max();
	static constexpr int MIN_PRIORITY = TNumericLimits<int>::Max();
	int BestPriority = MIN_PRIORITY;
	const FSnapTargetPoint* BestSnapTarget = nullptr;

	GenerateTargets(PointIn);

	TFunction<FVector3d(const FVector3d&)> GetSnapFromPointFunc = [&PointIn](const FVector3d& Point)
	{
		return PointIn;
	};


	const FSnapTargetPoint* BestPointTarget = FindBestSnapInSet(TargetPoints, MinMetric, BestPriority, GetSnapFromPointFunc);
	if (BestPointTarget != nullptr)
	{
		BestSnapTarget = BestPointTarget;
	}

	const FSnapTargetPoint* BestLineTarget = FindBestSnapInSet(GeneratedTargets, MinMetric, BestPriority, GetSnapFromPointFunc);
	if (BestLineTarget != nullptr)
	{
		BestSnapTarget = BestLineTarget;
	}

	if (bHaveActiveSnap && bEnableStableSnap && ActiveSnapTarget.bIsSnapLine == false)
	{
		if (TestSnapTarget(ActiveSnapTarget, MinMetric*StableSnapImproveThresh, BestPriority, GetSnapFromPointFunc))
		{
			return;
		}
	}


	if (BestSnapTarget != nullptr)
	{
		SetActiveSnapData(*BestSnapTarget, BestSnapTarget->Position, Plane.ToPlane(BestSnapTarget->Position, 2), MinMetric);
	}
	else
	{
		ClearActiveSnapData();
	}

}
