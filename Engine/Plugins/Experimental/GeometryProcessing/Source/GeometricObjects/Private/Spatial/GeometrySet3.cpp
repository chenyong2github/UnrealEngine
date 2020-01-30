// Copyright Epic Games, Inc. All Rights Reserved.

#include "Spatial/GeometrySet3.h"
#include "Distance/DistRay3Segment3.h"
#include "Async/ParallelFor.h"


void FGeometrySet3::Reset(bool bPoints, bool bCurves)
{
	if (bPoints)
	{
		Points.Reset();
		PointIDToIndex.Reset();
	}
	if (bCurves)
	{
		Curves.Reset();
		CurveIDToIndex.Reset();
	}
}



void FGeometrySet3::AddPoint(int PointID, const FVector3d& Point)
{
	check(PointIDToIndex.Contains(PointID) == false);
	FPoint NewPoint = { PointID, Point };
	int NewIndex = Points.Add(NewPoint);
	PointIDToIndex.Add(PointID, NewIndex);
}

void FGeometrySet3::AddCurve(int CurveID, const FPolyline3d& Polyline)
{
	check(CurveIDToIndex.Contains(CurveID) == false);
	int NewIndex = Curves.Add(FCurve());
	FCurve& NewCurve = Curves[NewIndex];
	NewCurve.ID = CurveID;
	NewCurve.Geometry = Polyline;
	NewCurve.Bounds = Polyline.GetBounds();
	CurveIDToIndex.Add(CurveID, NewIndex);
}


void FGeometrySet3::UpdatePoint(int PointID, const FVector3d& Point)
{
	const int* Index = PointIDToIndex.Find(PointID);
	check(Index != nullptr);
	Points[*Index].Position = Point;
}

void FGeometrySet3::UpdateCurve(int CurveID, const FPolyline3d& Polyline)
{
	const int* Index = CurveIDToIndex.Find(CurveID);
	check(Index != nullptr);
	Curves[*Index].Geometry.SetVertices(Polyline.GetVertices());
	Curves[*Index].Bounds = Polyline.GetBounds();
}



bool FGeometrySet3::FindNearestPointToRay(const FRay3d& Ray, FNearest& ResultOut,
	TFunction<bool(const FVector3d&, const FVector3d&)> PointWithinToleranceTest) const
{
	double MinRayParamT = TNumericLimits<double>::Max();
	int NearestID = -1;
	int NearestIndex = -1;

	FCriticalSection Critical;

	int NumPoints = Points.Num();
	ParallelFor(NumPoints, [&](int pi)
	{
		const FPoint& Point = Points[pi];
		double RayT = Ray.Project(Point.Position);
		FVector3d RayPt = Ray.NearestPoint(Point.Position);
		if (PointWithinToleranceTest(RayPt, Point.Position))
		{
			Critical.Lock();
			if (RayT < MinRayParamT)
			{
				MinRayParamT = RayT;
				NearestIndex = pi;
				NearestID = Points[pi].ID;
			}
			Critical.Unlock();
		}
	});

	if (NearestID != -1)
	{
		ResultOut.ID = NearestID;
		ResultOut.bIsPoint = true;
		ResultOut.NearestRayPoint = Ray.PointAt(MinRayParamT);
		ResultOut.NearestGeoPoint = Points[NearestIndex].Position;
		ResultOut.RayParam = MinRayParamT;
	}

	return NearestID != -1;
}





bool FGeometrySet3::FindNearestCurveToRay(const FRay3d& Ray, FNearest& ResultOut,
	TFunction<bool(const FVector3d&, const FVector3d&)> PointWithinToleranceTest) const
{
	double MinRayParamT = TNumericLimits<double>::Max();
	int NearestID = -1;
	int NearestIndex = -1;
	int NearestSegmentIdx = -1;
	double NearestSegmentParam = 0;

	FCriticalSection Critical;

	int NumCurves = Curves.Num();
	ParallelFor(NumCurves, [&](int ci)
	{
		const FCurve& Curve = Curves[ci];
		const FPolyline3d& Polyline = Curve.Geometry;
		int NumSegments = Polyline.SegmentCount();

		double CurveMinRayParamT = TNumericLimits<double>::Max();
		int CurveNearestSegmentIdx = -1;
		double CurveNearestSegmentParam = 0;
		for (int si = 0; si < NumSegments; ++si)
		{
			double SegRayParam; double SegSegParam;
			double SegDistSqr = FDistRay3Segment3d::SquaredDistance(Ray, Polyline.GetSegment(si), SegRayParam, SegSegParam);
			if (SegRayParam < CurveMinRayParamT)
			{
				FVector3d RayPosition = Ray.PointAt(SegRayParam);
				FVector3d CurvePosition = Polyline.GetSegmentPoint(si, SegSegParam);
				if (PointWithinToleranceTest(RayPosition, CurvePosition))
				{
					CurveMinRayParamT = SegRayParam;
					CurveNearestSegmentIdx = si;
					CurveNearestSegmentParam = SegSegParam;
				}
			}
		}

		// if we found a possible point, lock outer structures and merge in
		if (CurveMinRayParamT < TNumericLimits<double>::Max())
		{
			Critical.Lock();
			if (CurveMinRayParamT < MinRayParamT)
			{
				MinRayParamT = CurveMinRayParamT;
				NearestIndex = ci;
				NearestID = Curve.ID;
				NearestSegmentIdx = CurveNearestSegmentIdx;
				NearestSegmentParam = CurveNearestSegmentParam;
			}
			Critical.Unlock();
		}
	});   // end ParallelFor

	if (NearestID != -1)
	{
		ResultOut.ID = NearestID;
		ResultOut.bIsPoint = false;
		ResultOut.NearestRayPoint = Ray.PointAt(MinRayParamT);
		ResultOut.NearestGeoPoint = Curves[NearestIndex].Geometry.GetSegmentPoint(NearestSegmentIdx, NearestSegmentParam);
		ResultOut.RayParam = MinRayParamT;
		ResultOut.PolySegmentIdx = NearestSegmentIdx;
		ResultOut.PolySegmentParam = NearestSegmentParam;
	}

	return NearestID != -1;
}
