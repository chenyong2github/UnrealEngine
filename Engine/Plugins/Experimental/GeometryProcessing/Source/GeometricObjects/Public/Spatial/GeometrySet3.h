// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoxTypes.h"
#include "RayTypes.h"
#include "Polyline3.h"

/**
 * FGeometrySet3 stores a set of 3D Points and Polyline curves,
 * and supports spatial queries against these sets. 
 * 
 * Since Points and Curves have no area to hit, hit-tests are done via nearest-point-on-ray.
 */
class GEOMETRICOBJECTS_API FGeometrySet3
{
public:
	/**
	 * @param bPoints if true, discard all points
	 * @param bCurves if true, discard all polycurves
	 */
	void Reset(bool bPoints = true, bool bCurves = true);

	/** Add a point with given PointID at the given Position*/
	void AddPoint(int PointID, const FVector3d& Position);
	/** Add a polycurve with given CurveID and the give Polyline */
	void AddCurve(int CurveID, const FPolyline3d& Polyline);

	/** Update the Position of previously-added PointID */
	void UpdatePoint(int PointID, const FVector3d& Position);
	/** Update the Polyline of previously-added CurveID */
	void UpdateCurve(int CurveID, const FPolyline3d& Polyline);
	
	/**
	 * FNearest is returned by nearest-point queries
	 */
	struct GEOMETRICOBJECTS_API FNearest
	{
		/** ID of point or curve */
		int ID;
		/** true for point, false for polyline curve*/
		bool bIsPoint;

		/** Nearest point on ray */
		FVector3d NearestRayPoint;
		/** Nearest point on geometry (ie the point, or point on curve)*/
		FVector3d NearestGeoPoint;

		/** parameter of nearest point on ray (equivalent to NearestRayPoint) */
		double RayParam;

		/** if bIsPoint=false, index of nearest segment on polyline curve */
		int PolySegmentIdx;
		/** if bIsPoint=false, parameter of NearestGeoPoint along segment defined by PolySegmentIdx */
		double PolySegmentParam;
	};


	/**
	 * @param Ray query ray
	 * @param ResultOut populated with information about successful nearest point result
	 * @param PointWithinToleranceTest should return true if two 3D points are "close enough" to be considered a hit
	 * @return true if the nearest point on Ray to some point in the set passed the PointWithinToleranceTest.
	 * @warning PointWithinToleranceTest is called in parallel and hence must be thread-safe/re-entrant!
	 */
	bool FindNearestPointToRay(const FRay3d& Ray, FNearest& ResultOut,
		TFunction<bool(const FVector3d&, const FVector3d&)> PointWithinToleranceTest) const;

	/**
	 * @param Ray query ray
	 * @param ResultOut populated with information about successful nearest point result
	 * @param PointWithinToleranceTest should return true if two 3D points are "close enough" to be considered a hit
	 * @return true if the nearest point on Ray to some curve in the set passed the PointWithinToleranceTest.
	 * @warning PointWithinToleranceTest is called in parallel and hence must be thread-safe/re-entrant!
	 */
	bool FindNearestCurveToRay(const FRay3d& Ray, FNearest& ResultOut,
		TFunction<bool(const FVector3d&, const FVector3d&)> PointWithinToleranceTest) const;


protected:

	struct FPoint
	{
		int ID;
		FVector3d Position;
	};
	TArray<FPoint> Points;
	TMap<int, int> PointIDToIndex;

	struct FCurve
	{
		int ID;
		FPolyline3d Geometry;
		
		FAxisAlignedBox3d Bounds;
	};
	TArray<FCurve> Curves;
	TMap<int, int> CurveIDToIndex;

};