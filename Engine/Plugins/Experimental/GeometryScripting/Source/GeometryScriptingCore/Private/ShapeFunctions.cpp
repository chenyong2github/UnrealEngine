// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/ShapeFunctions.h"
#include "VectorTypes.h"
#include "Intersection/IntersectionUtil.h"
#include "Intersection/IntrRay3AxisAlignedBox3.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ShapeFunctions)

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_BoxFunctions"



FRay UGeometryScriptLibrary_RayFunctions::MakeRayFromPoints(const FVector& A, const FVector& B)
{
	return FRay(A, Normalized(B-A), true);
}

FRay UGeometryScriptLibrary_RayFunctions::MakeRayFromPointDirection(const FVector& Origin, const FVector& Direction, bool bDirectionIsNormalized)
{
	return FRay(Origin, Direction, bDirectionIsNormalized);
}

FRay UGeometryScriptLibrary_RayFunctions::GetTransformedRay(const FRay& Ray, const FTransform& TransformIn, bool bInvert)
{
	FTransformSRT3d Transform(TransformIn);
	return (bInvert) ? Transform.InverseTransformRay(Ray) : Transform.TransformRay(Ray);
}

FVector UGeometryScriptLibrary_RayFunctions::GetRayPoint(const FRay& Ray, double Distance)
{
	return Ray.PointAt(Distance);
}

double UGeometryScriptLibrary_RayFunctions::GetRayParameter(const FRay& Ray, FVector& Point)
{
	return Ray.GetParameter(Point);
}

double UGeometryScriptLibrary_RayFunctions::GetRayPointDistance(const FRay& Ray, FVector& Point)
{
	return Ray.Dist(Point);
}

FVector UGeometryScriptLibrary_RayFunctions::GetRayClosestPoint(const FRay& Ray, FVector& Point)
{
	return Ray.ClosestPoint(Point);
}

bool UGeometryScriptLibrary_RayFunctions::GetRaySphereIntersection(const FRay& Ray, const FVector& SphereCenter, double SphereRadius, double& Distance1, double& Distance2)
{
	Distance1 = Distance2 = (double)TNumericLimits<float>::Max();
	FLinearIntersection Intersection;
	bool bIntersects = IntersectionUtil::RaySphereIntersection(Ray.Origin, Ray.Direction, SphereCenter, SphereRadius, Intersection);
	if (bIntersects)
	{
		Distance1 = Intersection.parameter.Min;
		Distance2 = (Intersection.numIntersections > 1) ? Intersection.parameter.Max : Intersection.parameter.Min;
	}
	return bIntersects;
}

bool UGeometryScriptLibrary_RayFunctions::GetRayBoxIntersection(const FRay& Ray, const FBox& Box, double& HitDistance)
{
	HitDistance = (double)TNumericLimits<float>::Max();
	return FIntrRay3AxisAlignedBox3d::FindIntersection(Ray, FAxisAlignedBox3d(Box), HitDistance);
}





FBox UGeometryScriptLibrary_BoxFunctions::MakeBoxFromCenterSize(const FVector& Center, const FVector& Dimensions)
{
	FVector Extents( FMathd::Max(0, Dimensions.X * 0.5), FMathd::Max(0, Dimensions.Y * 0.5), FMathd::Max(0, Dimensions.Z * 0.5) );
	return FBox(Center - Extents, Center + Extents);
}

void UGeometryScriptLibrary_BoxFunctions::GetBoxCenterSize(const FBox& Box, FVector& Center, FVector& Dimensions)
{
	FVector Extents;
	Box.GetCenterAndExtents(Center, Extents);
	Dimensions = 2.0 * Extents;
}

FVector UGeometryScriptLibrary_BoxFunctions::GetBoxCorner(const FBox& Box, int CornerIndex)
{
	CornerIndex = FMath::Clamp(CornerIndex, 0, 7);
	switch (CornerIndex)		// matches Box.GetVertices()
	{
	default:
	case 0: return Box.Min;
	case 1: return FVector(Box.Min.X, Box.Min.Y, Box.Max.Z);
	case 2: return FVector(Box.Min.X, Box.Max.Y, Box.Min.Z);
	case 3: return FVector(Box.Max.X, Box.Min.Y, Box.Min.Z);
	case 4: return FVector(Box.Max.X, Box.Max.Y, Box.Min.Z);
	case 5: return FVector(Box.Max.X, Box.Min.Y, Box.Max.Z);
	case 6: return FVector(Box.Min.X, Box.Max.Y, Box.Max.Z);
	case 7: return Box.Max;
	}
}

FVector UGeometryScriptLibrary_BoxFunctions::GetBoxFaceCenter(const FBox& Box, int FaceIndex, FVector& FaceNormal)
{
	FaceIndex = FMath::Clamp(FaceIndex, 0, 5);
	FVector Center = 0.5 * (Box.Min + Box.Max);
	switch (FaceIndex)
	{
	default:
	case 0: FaceNormal = FVector(0, 0, -1); return FVector(Center.X, Center.Y, Box.Min.Z);
	case 1: FaceNormal = FVector(0, 0, 1); return FVector(Center.X, Center.Y, Box.Max.Z);
	case 2: FaceNormal = FVector(0, -1, 0); return FVector(Center.X, Box.Min.Y, Center.Z);
	case 3: FaceNormal = FVector(0, 1, 0); return FVector(Center.X, Box.Max.Y, Center.Z);
	case 4: FaceNormal = FVector(-1, 0, 0); return FVector(Box.Min.X, Center.Y, Center.Z);
	case 5: FaceNormal = FVector(1, 0, 0); return FVector(Box.Max.X, Center.Y, Center.Z);
	}
}

void UGeometryScriptLibrary_BoxFunctions::GetBoxVolumeArea(const FBox& Box, double& Volume, double& SurfaceArea)
{
	FVector Dimensions = Box.GetSize();
	double AreaXY = Dimensions.X * Dimensions.Y;
	double AreaXZ = Dimensions.X * Dimensions.Z;
	double AreaYZ = Dimensions.Y * Dimensions.Z;
	SurfaceArea = 2.0*AreaXY + 2.0*AreaXZ + 2.0*AreaYZ;
	Volume = Dimensions.X * Dimensions.Y * Dimensions.Z;
}

FBox UGeometryScriptLibrary_BoxFunctions::GetExpandedBox(const FBox& Box, const FVector& ExpandBy)
{
	FBox Result = Box.ExpandBy(ExpandBy);
	// ExpandBy with negative expansion factor does not clamp to original box center
	for (int32 j = 0; j < 3; ++j)
	{
		if (Result.Min[j] > Result.Max[j])
		{
			Result.Min[j] = Result.Max[j] = 0.5*(Box.Min[j] + Box.Max[j]);
		}
	}
	return Result;
}

FBox UGeometryScriptLibrary_BoxFunctions::GetTransformedBox(const FBox& Box, const FTransform& Transform)
{
	return Box.TransformBy(Transform);
}


bool UGeometryScriptLibrary_BoxFunctions::TestBoxBoxIntersection(const FBox& Box1, const FBox& Box2)
{
	return Box1.Intersect(Box2);
}


FBox UGeometryScriptLibrary_BoxFunctions::FindBoxBoxIntersection(const FBox& Box1, const FBox& Box2, bool& bIsIntersecting)
{
	bIsIntersecting = Box1.Intersect(Box2);
	return Box1.Overlap(Box2);
}

double UGeometryScriptLibrary_BoxFunctions::GetBoxBoxDistance(const FBox& Box1, const FBox& Box2)
{
	double DistSqr = FMathd::Max(0.0, Box1.ComputeSquaredDistanceToBox(Box2));
	return FMathd::Sqrt(DistSqr);
}

bool UGeometryScriptLibrary_BoxFunctions::TestPointInsideBox(const FBox& Box, const FVector& Point, bool bConsiderOnBoxAsInside)
{
	return (bConsiderOnBoxAsInside) ? Box.IsInsideOrOn(Point) : Box.IsInside(Point);
}

FVector UGeometryScriptLibrary_BoxFunctions::FindClosestPointOnBox(const FBox& Box, const FVector& Point, bool& bIsInside)
{
	bIsInside = Box.IsInside(Point);
	return Box.GetClosestPointTo(Point);
}

double UGeometryScriptLibrary_BoxFunctions::GetBoxPointDistance(const FBox& Box, const FVector& Point)
{
	double DistSqr = FMathd::Max(0.0, Box.ComputeSquaredDistanceToPoint(Point));
	return FMathd::Sqrt(DistSqr);
}

bool UGeometryScriptLibrary_BoxFunctions::TestBoxSphereIntersection(const FBox& Box, const FVector& SphereCenter, double SphereRadius)
{
	return FMath::SphereAABBIntersection(SphereCenter, SphereRadius*SphereRadius, Box);
}



#undef LOCTEXT_NAMESPACE
