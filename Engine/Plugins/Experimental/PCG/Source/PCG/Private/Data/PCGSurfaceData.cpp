// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGSurfaceData.h"

FVector UPCGSurfaceData::TransformPosition(const FVector& InPosition) const
{
	return Transform.InverseTransformPosition(InPosition);
}

FPCGPoint UPCGSurfaceData::TransformPoint(const FPCGPoint& InPoint) const
{
	FPCGPoint Point = InPoint;

	// Update point location: put it on the surface plane
	FVector PointPositionInLocalSpace = TransformPosition(InPoint.Transform.GetLocation());
	PointPositionInLocalSpace.Z = 0;
	Point.Transform.SetLocation(Transform.TransformPosition(PointPositionInLocalSpace));

	// Update density
	Point.Density *= GetDensityAtPosition(InPoint.Transform.GetLocation());

	return Point;
}