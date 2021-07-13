// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sampling/MeshBakerCommon.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

/**
 * Find point on Detail mesh that corresponds to point on Base mesh.
 * Strategy is:
 *    1) cast a ray inwards along -Normal from BasePoint + Thickness*Normal
 *    2) cast a ray outwards along Normal from BasePoint
 *    3) cast a ray inwards along -Normal from BasePoint
 * We take (1) preferentially, and then (2), and then (3)
 *
 * If all of those fail, if bFailToNearestPoint is true we fall back to nearest-point,
 *
 * If all the above fail, return false
 */
bool UE::Geometry::GetDetailMeshTrianglePoint_Raycast(
	const FDynamicMesh3& DetailMesh,
	const FDynamicMeshAABBTree3& DetailSpatial,
	const FVector3d& BasePoint,
	const FVector3d& BaseNormal,
	int32& DetailTriangleOut,
	FVector3d& DetailTriBaryCoords,
	double Thickness,
	bool bFailToNearestPoint
)
{
	// TODO: should we check normals here? inverse normal should probably not be considered valid

	// shoot rays forwards and backwards
	FRay3d InwardRay = FRay3d(BasePoint + Thickness * BaseNormal, -BaseNormal);
	FRay3d ForwardRay(BasePoint, BaseNormal);
	FRay3d BackwardRay(BasePoint, -BaseNormal);
	int32 ForwardHitTID = IndexConstants::InvalidID, InwardHitTID = IndexConstants::InvalidID, BackwardHitTID = IndexConstants::InvalidID;
	double ForwardHitDist, InwardHitDist, BackwardHitDist;

	IMeshSpatial::FQueryOptions Options;
	Options.MaxDistance = Thickness;
	bool bHitInward = DetailSpatial.FindNearestHitTriangle(InwardRay, InwardHitDist, InwardHitTID, Options);
	bool bHitForward = DetailSpatial.FindNearestHitTriangle(ForwardRay, ForwardHitDist, ForwardHitTID, Options);
	bool bHitBackward = DetailSpatial.FindNearestHitTriangle(BackwardRay, BackwardHitDist, BackwardHitTID, Options);

	FRay3d HitRay;
	int32 HitTID = IndexConstants::InvalidID;
	double HitDist = TNumericLimits<double>::Max();

	if (bHitInward)
	{
		HitRay = InwardRay;
		HitTID = InwardHitTID;
		HitDist = InwardHitDist;
	}
	else if (bHitForward)
	{
		HitRay = ForwardRay;
		HitTID = ForwardHitTID;
		HitDist = ForwardHitDist;
	}
	else if (bHitBackward)
	{
		HitRay = BackwardRay;
		HitTID = BackwardHitTID;
		HitDist = BackwardHitDist;
	}

	// if we got a valid ray hit, use it
	if (DetailMesh.IsTriangle(HitTID))
	{
		DetailTriangleOut = HitTID;
		FIntrRay3Triangle3d IntrQuery = TMeshQueries<FDynamicMesh3>::TriangleIntersection(DetailMesh, HitTID, HitRay);
		DetailTriBaryCoords = IntrQuery.TriangleBaryCoords;
		return true;
	}
	else
	{
		// if we did not find any hits, try nearest-point
		IMeshSpatial::FQueryOptions OnSurfQueryOptions;
		OnSurfQueryOptions.MaxDistance = Thickness;
		double NearDistSqr = 0;
		int32 NearestTriID = -1;
		// if we are using absolute nearest point as a fallback, then ignore max distance
		if (bFailToNearestPoint)
		{
			NearestTriID = DetailSpatial.FindNearestTriangle(BasePoint, NearDistSqr);
		}
		else
		{
			NearestTriID = DetailSpatial.FindNearestTriangle(BasePoint, NearDistSqr, OnSurfQueryOptions);
		}
		if (DetailMesh.IsTriangle(NearestTriID))
		{
			DetailTriangleOut = NearestTriID;
			FDistPoint3Triangle3d DistQuery = TMeshQueries<FDynamicMesh3>::TriangleDistance(DetailMesh, NearestTriID, BasePoint);
			DetailTriBaryCoords = DistQuery.TriangleBaryCoords;
			return true;
		}
	}

	return false;
}

/**
 * Find point on Detail mesh that corresponds to point on Base mesh using minimum distance
 */
bool UE::Geometry::GetDetailMeshTrianglePoint_Nearest(
	const FDynamicMesh3& DetailMesh,
	const FDynamicMeshAABBTree3& DetailSpatial,
	const FVector3d& BasePoint,
	int32& DetailTriangleOut,
	FVector3d& DetailTriBaryCoords)
{
	double NearDistSqr = 0;
	int32 NearestTriID = DetailSpatial.FindNearestTriangle(BasePoint, NearDistSqr);
	if (DetailMesh.IsTriangle(NearestTriID))
	{
		DetailTriangleOut = NearestTriID;
		FDistPoint3Triangle3d DistQuery = TMeshQueries<FDynamicMesh3>::TriangleDistance(DetailMesh, NearestTriID, BasePoint);
		DetailTriBaryCoords = DistQuery.TriangleBaryCoords;
		return true;
	}

	return false;
}

