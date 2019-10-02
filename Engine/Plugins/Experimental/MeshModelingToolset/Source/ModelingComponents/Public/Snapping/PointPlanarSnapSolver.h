// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Snapping/BasePositionSnapSolver3.h"

/**
 * FPointPlanarSnapSolver solves for a Point snap location on a plane, based
 * on an input Point and a set of target points and lines in the plane.
 *
 * This implementation has the notion of a "history" of previous points,
 * from which line and distance constraints can be inferred.
 * This is useful for snapping in 2D polygon drawing.
 *
 * See FBasePositionSnapSolver3 for details on how to set up the snap problem
 * and get results.
 */
class MODELINGCOMPONENTS_API FPointPlanarSnapSolver : public FBasePositionSnapSolver3
{
public:
	// configuration variables
	FFrame3d Plane;

	bool bEnableSnapToKnownLengths = true;

	static constexpr int CardinalAxisTargetID = 10;
	static constexpr int LastSegmentTargetID = 11;

	int CardinalAxisPriority = 150;
	int LastSegmentPriority = 140;
	int KnownLengthPriorityDelta = 10;
	int MinInternalPriority() const { return LastSegmentPriority - KnownLengthPriorityDelta; }

public:
	FPointPlanarSnapSolver();

	void RegenerateTargetLines(bool bCardinalAxes, bool bLastHistorySegment);

	virtual void Reset() override;
	virtual void ResetActiveSnap() override;

	void UpdatePointHistory(const TArray<FVector3d>& Points);
	void UpdatePointHistory(const TArray<FVector>& Points);

	void UpdateSnappedPoint(const FVector3d& PointIn);

protected:

	TArray<FSnapTargetLine> GeneratedLines;

	TSet<int> IgnoreTargets;

	TArray<FVector3d> PointHistory;

	TArray<FSnapTargetPoint> GeneratedTargets;
	void GenerateTargets(const FVector3d& PointIn);
};
