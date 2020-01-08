// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "FrameTypes.h"
#include "LineTypes.h"
#include "RayTypes.h"
#include "CircleTypes.h"


/**
 * FBasePositionSnapSolver3 is a base class for 3D position snapping implementations.
 * It is not usable on its own and the split between this class and the 
 * implementations is not incredibly clean. However there is lots of
 * shared functionality that is placed here.
 * 
 * 3D Point and Line targets are supported. Targets can also be "ignored".
 * 
 * The actual snap "solve" must be implemented by subclasses, depending on
 * their input data (ray, point, line, etc)
 * 
 * 
 */
class MODELINGCOMPONENTS_API FBasePositionSnapSolver3
{
public:
	//
	// parameters
	// 

	/** This is the function we use to measure distances between snap points. Defaults to Euclidean distance. */
	TFunction<double(const FVector3d&, const FVector3d&)> SnapMetricFunc;
	/** Tolerance for snapping, in unit relative to SnapMetricFunc */
	double SnapMetricTolerance;

	/** If true, then we will prefer to keep current snap point over a new one */
	bool bEnableStableSnap = true;
	/** How much we have to improve the snap metric to discard the current stable snap */
	double StableSnapImproveThresh = 0.5;

	/** subclasses may have internal TargetID values, so external points should have IDs larger than this */
	static constexpr int BaseExternalPointID = 1000;
	/** subclasses may have internal TargetID values, so external lines should have IDs larger than this */
	static constexpr int BaseExternalLineID = 10000;


public:
	FBasePositionSnapSolver3();
	virtual ~FBasePositionSnapSolver3() {}

	//
	// setup
	// 

	/**
	 * Add a snap target point at the given Position
	 * @param TargetID identifier for this target. Can be shared with other targets
	 * @param Priority importance of this snap point. Lower priority is more important.
	 * @param OverrideMetric if provided, then we only snap to this point if Metric < OverrideMetric, as well as the global tolerance
	 */
	virtual void AddPointTarget(const FVector3d& Position, int TargetID, int Priority = 100, double OverrideMetric = TNumericLimits<double>::Max());

	/** Remove any point targets with this TargetID */
	virtual bool RemovePointTargetsByID(int TargetID);

	/**
	 * Add a snap target line
	 * @param TargetID identifier for this target. Can be shared with other targets
	 * @param Priority importance of this snap line. Lower priority is more important.
	 * @param OverrideMetric if provided, then we only snap to this line if Metric < OverrideMetric, as well as the global tolerance
	 */
	virtual void AddLineTarget(const FLine3d& Line, int TargetID, int Priority = 100);

	/** Remove any line targets with this TargetID */
	virtual bool RemoveLineTargetsByID(int TargetID);



	/**
	 * Add a snap target Circle
	 * @param TargetID identifier for this target. Can be shared with other targets
	 * @param Priority importance of this snap target. Lower priority is more important.
	 * @param OverrideMetric if provided, then we only snap to this target if Metric < OverrideMetric, as well as the global tolerance
	 */
	virtual void AddCircleTarget(const FCircle3d& Circle, int TargetID, int Priority = 100);

	/** Remove any line targets with this TargetID */
	virtual bool RemoveCircleTargetsByID(int TargetID);


	/** Add given TargetID to the ignore list, so any points/lines with that ID will not be snapped-to */
	virtual void AddIgnoreTarget(int TargetID);
	/** Add given TargetID from the ignore list */
	virtual void RemoveIgnoreTarget(int TargetID);
	/** @return true if the given TargetID is in the ignore list */
	virtual bool IsIgnored(int TargetID) const;

	/** Discard the set of snap points and lines and clear the active snap */
	virtual void Reset();
	/** Clear the active snap */
	virtual void ResetActiveSnap();

	//
	// solving
	//

	//void UpdateSnappedPoint(const FRay3d& Ray);

	//
	// output
	//

	/** @return true if after the last snap solve we have an active snap */
	bool HaveActiveSnap() const { return bHaveActiveSnap; }
	/** @return the snapped-to point */
	FVector3d GetActiveSnapToPoint() const { return ActiveSnapToPoint; }
	/** @return the snapped-from point. This may be the original target, a point on a line, after projections, etc. Defined by subclasses. */
	FVector3d GetActiveSnapFromPoint() const { return ActiveSnapFromPoint; }
	/** @return targetID of original point or line that resulted in the snap */
	int GetActiveSnapTargetID() const { return (bHaveActiveSnap) ? ActiveSnapTarget.TargetID : -1; }

	/** @return true if the active snap target is a line */
	bool HaveActiveSnapLine() const { return HaveActiveSnap() && ActiveSnapTarget.bIsSnapLine; }
	/** @return 3D line for active snap target */
	const FLine3d& GetActiveSnapLine() const { return ActiveSnapTarget.SnapLine; }

	/** @return true if the active snap target is based on a distance along a line */
	bool HaveActiveSnapDistance() const { return HaveActiveSnap() && ActiveSnapTarget.bIsSnapDistance; }
	/** @return internal snap distance ID (interpretation defined by subclasses) */
	int GetActiveSnapDistanceID() const { return ActiveSnapTarget.SnapDistanceID; }

protected:
	/**
	 * Target point that might be snapped to
	 */
	struct FSnapTargetPoint
	{
		FVector3d Position;

		int TargetID;
		int Priority;
		double OverrideMetric = TNumericLimits<double>::Max();

		bool bIsSnapLine = false;
		FLine3d SnapLine;
		bool bIsSnapDistance = false;
		int SnapDistanceID = -1;
	};
	TArray<FSnapTargetPoint> TargetPoints;

	/**
	 * Target line that might be snapped to
	 */
	struct FSnapTargetLine
	{
		FLine3d Line;
		int TargetID;
		int Priority;
	};
	TArray<FSnapTargetLine> TargetLines;


	struct FSnapTargetCircle
	{
		FCircle3d Circle;
		int TargetID;
		int Priority;
	};
	TArray<FSnapTargetCircle> TargetCircles;


	/** list of TargetID values to ignore in snap queries */
	TSet<int> IgnoreTargets;

	//
	// information about active snap
	//
	bool bHaveActiveSnap;
	FSnapTargetPoint ActiveSnapTarget;
	FVector3d ActiveSnapFromPoint;
	FVector3d ActiveSnapToPoint;
	double SnappedPointMetric;

	virtual void SetActiveSnapData(const FSnapTargetPoint& TargetPoint, const FVector3d& FromPoint, const FVector3d& ToPoint, double Metric);
	virtual void ClearActiveSnapData();


	//
	// snap measurement functions
	// 

	const FSnapTargetPoint* FindBestSnapInSet(const TArray<FSnapTargetPoint>& TestTargets, double& MinMetric, int& MinPriority,
		const TFunction<FVector3d(const FVector3d&)>& GetSnapPointFromFunc);

	bool TestSnapTarget(const FSnapTargetPoint& Target, double MinMetric, int MinPriority,
		const TFunction<FVector3d(const FVector3d&)>& GetSnapPointFromFunc);
};