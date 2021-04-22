// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Capsule.h"
#include "Chaos/GJK.h"
#include "Chaos/Triangle.h"
#include "Chaos/Convex.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/Utilities.h"

namespace Chaos
{

	// Note that if this is re-enabled when previously off, the cooked trimeshes won't have the vertex map serialized, so the change will not take effect until re-cooked.
	bool TriMeshPerPolySupport = 1;
	FAutoConsoleVariableRef CVarPerPolySupport(TEXT("p.Chaos.TriMeshPerPolySupport"), TriMeshPerPolySupport, TEXT("Disabling removes memory cost of vertex map on triangle mesh. Note: Changing at runtime will not work."));


template <typename QueryGeomType, typename T, int d>
static auto MakeScaledHelper(const QueryGeomType& B, const TVector<T, d>& InvScale)
{
	TUniquePtr<QueryGeomType> HackBPtr(const_cast<QueryGeomType*>(&B));	//todo: hack, need scaled object to accept raw ptr similar to transformed implicit
	TImplicitObjectScaled<QueryGeomType> ScaledB(MakeSerializable(HackBPtr), InvScale);
	HackBPtr.Release();
	return ScaledB;
}

template <typename QueryGeomType, typename T, int d>
static auto MakeScaledHelper(const TImplicitObjectScaled<QueryGeomType>& B, const TVector<T, d>& InvScale)
{
	//if scaled of scaled just collapse into one scaled
	TImplicitObjectScaled<QueryGeomType> ScaledB(B.Object(), InvScale * B.GetScale());
	return ScaledB;
}

void ScaleTransformHelper(const FVec3& TriMeshScale, const FRigidTransform3& QueryTM, FRigidTransform3& OutScaledQueryTM)
{
	OutScaledQueryTM = TRigidTransform<FReal, 3>(QueryTM.GetLocation() * TriMeshScale, QueryTM.GetRotation());
}

template <typename IdxType>
void TransformVertsHelper(const FVec3& TriMeshScale, int32 TriIdx, const FParticles& Particles,
	const TArray<TVector<IdxType, 3>>& Elements, FVec3& OutA, FVec3& OutB, FVec3& OutC)
{
	OutA = Particles.X(Elements[TriIdx][0]) * TriMeshScale;
	OutB = Particles.X(Elements[TriIdx][1]) * TriMeshScale;
	OutC = Particles.X(Elements[TriIdx][2]) * TriMeshScale;
}

template <typename QueryGeomType>
const QueryGeomType& ScaleGeomIntoWorldHelper(const QueryGeomType& QueryGeom, const FVec3& TriMeshScale)
{
	return QueryGeom;
}

template <typename QueryGeomType>
TImplicitObjectScaled<QueryGeomType> ScaleGeomIntoWorldHelper(const TImplicitObjectScaled<QueryGeomType>& QueryGeom, const FVec3& TriMeshScale)
{
	// This will apply TriMeshScale to QueryGeom and return a new scaled implicit in world space.
	return MakeScaledHelper(QueryGeom, TriMeshScale);
}

void TransformSweepOutputsHelper(FVec3 TriMeshScale, const FVec3& HitNormal, const FVec3& HitPosition, const FReal LengthScale,
	const FReal Time, FVec3& OutNormal, FVec3& OutPosition, FReal& OutTime)
{
	if (ensure(TriMeshScale != FVec3(0.0f)))
	{
		FVec3 InvTriMeshScale = 1.0f / TriMeshScale;

		OutTime = Time / LengthScale;
		OutNormal = (TriMeshScale * HitNormal).GetSafeNormal();
		OutPosition = InvTriMeshScale * HitPosition;
	}
}

template <typename IdxType>
struct FTriangleMeshRaycastVisitor
{
	FTriangleMeshRaycastVisitor(const FVec3& InStart, const FVec3& InDir, const FReal InThickness, const FParticles& InParticles, const TArray<TVector<IdxType, 3>>& InElements, bool bInCullsBackFaceRaycast)
	: Particles(InParticles)
	, Elements(InElements)
	, StartPoint(InStart)
	, Dir(InDir)
	, Thickness(InThickness)
	, OutTime(TNumericLimits<FReal>::Max())
	, bCullsBackFaceRaycast(bInCullsBackFaceRaycast)
	{
	}

	enum class ERaycastType
	{
		Raycast,
		Sweep
	};

	const void* GetQueryData() const
	{
		return nullptr;
	}

	template <ERaycastType SQType>
	bool Visit(int32 TriIdx, FQueryFastData& CurData)
	{
		constexpr FReal Epsilon = 1e-4;
		constexpr FReal Epsilon2 = Epsilon * Epsilon;
		const FReal Thickness2 = SQType == ERaycastType::Sweep ? Thickness * Thickness : 0;
		FReal MinTime = 0;	//no need to initialize, but fixes warning

		const FReal R = Thickness + Epsilon;
		const FReal R2 = R * R;

		const FVec3& A = Particles.X(Elements[TriIdx][0]);
		const FVec3& B = Particles.X(Elements[TriIdx][1]);
		const FVec3& C = Particles.X(Elements[TriIdx][2]);

		const FVec3 AB = B - A;
		const FVec3 AC = C - A;
		FVec3 TriNormal = FVec3::CrossProduct(AB, AC);
		const FReal NormalLength = TriNormal.SafeNormalize();
		if (!CHAOS_ENSURE(NormalLength > Epsilon))
		{
			//hitting degenerate triangle so keep searching - should be fixed before we get to this stage
			return true;
		}

		const bool bBackFace = (FVec3::DotProduct(Dir, TriNormal) > 0.0f);
		if (bCullsBackFaceRaycast && bBackFace)
		{
			return true;
		}

		const TPlane<FReal, 3> TriPlane{ A, TriNormal };
		FVec3 RaycastPosition;
		FVec3 RaycastNormal;
		FReal Time;

		//Check if we even intersect with triangle plane
		int32 DummyFaceIndex;
		if (TriPlane.Raycast(StartPoint, Dir, CurData.CurrentLength, Thickness, Time, RaycastPosition, RaycastNormal, DummyFaceIndex))
		{
			FVec3 IntersectionPosition = RaycastPosition;
			FVec3 IntersectionNormal = RaycastNormal;
			bool bTriangleIntersects = false;
			if (Time == 0)
			{
				//Initial overlap so no point of intersection, do an explicit sphere triangle test.
				const FVec3 ClosestPtOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, StartPoint);
				const FReal DistToTriangle2 = (StartPoint - ClosestPtOnTri).SizeSquared();
				if (DistToTriangle2 <= R2)
				{
					OutTime = 0;
					OutFaceIndex = TriIdx;
					return false; //no one will beat Time == 0
				}
			}
			else
			{
				const FVec3 ClosestPtOnTri = FindClosestPointOnTriangle(RaycastPosition, A, B, C, RaycastPosition);	//We know Position is on the triangle plane
				const FReal DistToTriangle2 = (RaycastPosition - ClosestPtOnTri).SizeSquared();
				bTriangleIntersects = DistToTriangle2 <= Epsilon2;	//raycast gave us the intersection point so sphere radius is already accounted for
			}

			if (SQType == ERaycastType::Sweep && !bTriangleIntersects)
			{
				//sphere is not immediately touching the triangle, but it could start intersecting the perimeter as it sweeps by
				FVec3 BorderPositions[3];
				FVec3 BorderNormals[3];
				FReal BorderTimes[3];
				bool bBorderIntersections[3];

				{
					FVec3 ABCapsuleAxis = B - A;
					FReal ABHeight = ABCapsuleAxis.SafeNormalize();
					bBorderIntersections[0] = FCapsule::RaycastFast(Thickness, ABHeight, ABCapsuleAxis, A, B, StartPoint, Dir, CurData.CurrentLength, 0, BorderTimes[0], BorderPositions[0], BorderNormals[0], DummyFaceIndex);
				}
				
				{
					FVec3 BCCapsuleAxis = C - B;
					FReal BCHeight = BCCapsuleAxis.SafeNormalize();
					bBorderIntersections[1] = FCapsule::RaycastFast(Thickness, BCHeight, BCCapsuleAxis, B, C, StartPoint, Dir, CurData.CurrentLength, 0, BorderTimes[1], BorderPositions[1], BorderNormals[1], DummyFaceIndex);
				}
				
				{
					FVec3 ACCapsuleAxis = C - A;
					FReal ACHeight = ACCapsuleAxis.SafeNormalize();
					bBorderIntersections[2] = FCapsule::RaycastFast(Thickness, ACHeight, ACCapsuleAxis, A, C, StartPoint, Dir, CurData.CurrentLength, 0, BorderTimes[2], BorderPositions[2], BorderNormals[2], DummyFaceIndex);
				}

				int32 MinBorderIdx = INDEX_NONE;
				FReal MinBorderTime = 0;	//initialization not needed, but fixes warning

				for (int32 BorderIdx = 0; BorderIdx < 3; ++BorderIdx)
				{
					if (bBorderIntersections[BorderIdx])
					{
						if (!bTriangleIntersects || BorderTimes[BorderIdx] < MinBorderTime)
						{
							MinBorderTime = BorderTimes[BorderIdx];
							MinBorderIdx = BorderIdx;
							bTriangleIntersects = true;
						}
					}
				}

				if (MinBorderIdx != INDEX_NONE)
				{
					IntersectionNormal = BorderNormals[MinBorderIdx];
					IntersectionPosition = BorderPositions[MinBorderIdx] - IntersectionNormal * Thickness;

					if (Time == 0)
					{
						//we were initially overlapping with triangle plane so no normal was given. Compute it now
						FVec3 TmpNormal;
						const FReal SignedDistance = TriPlane.PhiWithNormal(StartPoint, TmpNormal);
						RaycastNormal = SignedDistance >= 0 ? TmpNormal : -TmpNormal;
					}

					Time = MinBorderTime;
				}
			}

			if (bTriangleIntersects)
			{
				if (Time < OutTime)
				{
					OutPosition = IntersectionPosition;
					OutNormal = RaycastNormal;	//We use the plane normal even when hitting triangle edges. This is to deal with triangles that approximate a single flat surface.
					OutTime = Time;
					CurData.SetLength(Time);	//prevent future rays from going any farther
					OutFaceIndex = TriIdx;
				}
			}
		}

		return true;
	}

	bool VisitRaycast(TSpatialVisitorData<int32> TriIdx, FQueryFastData& CurData)
	{
		return Visit<ERaycastType::Raycast>(TriIdx.Payload, CurData);
	}

	bool VisitSweep(TSpatialVisitorData<int32> TriIdx, FQueryFastData& CurData)
	{
		return Visit<ERaycastType::Sweep>(TriIdx.Payload, CurData);
	}

	bool VisitOverlap(TSpatialVisitorData<int32> TriIdx)
	{
		check(false);
		return true;
	}

	const FParticles& Particles;
	const TArray<TVector<IdxType, 3>>& Elements;
	const FVec3& StartPoint;
	const FVec3& Dir;
	const FReal Thickness;
	FReal OutTime;
	FVec3 OutPosition;
	FVec3 OutNormal;
	int32 OutFaceIndex;
	bool bCullsBackFaceRaycast;
};

FReal FTriangleMeshImplicitObject::PhiWithNormal(const FVec3& x, FVec3& Normal) const
{
	TSphere<FReal, 3> TestSphere(x, 0.0f);
	FRigidTransform3 TestXf(FVec3(0.0), FRotation3::FromIdentity());
	FVec3 TestLocation = x;
	FReal Depth = TNumericLimits<FReal>::Max();
	GJKContactPointImp(TestSphere, TestXf, 0.0f, TestLocation, Normal, Depth);
	return Depth;
}

template <typename IdxType>
bool FTriangleMeshImplicitObject::RaycastImp(const TArray<TVector<IdxType, 3>>& Elements, const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const
{
	FTriangleMeshRaycastVisitor<IdxType> SQVisitor(StartPoint, Dir, Thickness, MParticles, Elements, bCullsBackFaceRaycast);

	if (Thickness > 0)
	{
		BVH.Sweep(StartPoint, Dir, Length, FVec3(Thickness), SQVisitor);
	}
	else
	{
		BVH.Raycast(StartPoint, Dir, Length, SQVisitor);
	}

	if (SQVisitor.OutTime <= Length)
	{
		OutTime = SQVisitor.OutTime;
		OutPosition = SQVisitor.OutPosition;
		OutNormal = SQVisitor.OutNormal;
		OutFaceIndex = SQVisitor.OutFaceIndex;
		return true;
	}
	else
	{
		return false;
	}
}

bool FTriangleMeshImplicitObject::Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const
{
	if (MElements.RequiresLargeIndices())
	{
		return RaycastImp(MElements.GetLargeIndexBuffer(), StartPoint, Dir, Length, Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
	}
	else
	{
		return RaycastImp(MElements.GetSmallIndexBuffer(), StartPoint, Dir, Length, Thickness, OutTime, OutPosition, OutNormal, OutFaceIndex);
	}
}


template <typename QueryGeomType>
bool FTriangleMeshImplicitObject::GJKContactPointImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& OutContactPhi, FVec3 TriMeshScale) const
{
	ensure(TriMeshScale != FVec3(0.0f));
	bool bResult = false;

	const auto& WorldScaleGeom = ScaleGeomIntoWorldHelper(QueryGeom, TriMeshScale);

	TRigidTransform<FReal, 3> WorldScaleQueryTM;
	ScaleTransformHelper(TriMeshScale, QueryTM, WorldScaleQueryTM);

	auto CalculateTriangleContact = [&](const FVec3& A, const FVec3& B, const FVec3& C,
		FVec3& LocalContactLocation, FVec3& LocalContactNormal, FReal& LocalContactPhi) -> bool
	{
		const FVec3 AB = B - A;
		const FVec3 AC = C - A;
		FTriangle TriangleConvex(A, B, C);

		FReal LambdaPenetration;
		FVec3 ClosestA, ClosestB, LambdaNormal;
		int32 ClosestVertexIndexA, ClosestVertexIndexB;
		bool GJKValidResult = GJKPenetration<true>(TriangleConvex, WorldScaleGeom, WorldScaleQueryTM, LambdaPenetration, ClosestA, ClosestB, LambdaNormal, ClosestVertexIndexA, ClosestVertexIndexB, (FReal)0);
		if (GJKValidResult)
		{
			LocalContactLocation = ClosestB;
			LocalContactNormal = LambdaNormal;
			LocalContactPhi = -LambdaPenetration;
		}

		return GJKValidResult;
	};


	auto LambdaHelper = [&](const auto& Elements)
	{
		FReal LocalContactPhi = FLT_MAX;
		FVec3 LocalContactLocation, LocalContactNormal;

		FAABB3 QueryBounds = QueryGeom.BoundingBox();
		QueryBounds.Thicken(Thickness);
		QueryBounds = QueryBounds.TransformedAABB(QueryTM);
		const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);

		for (int32 TriIdx : PotentialIntersections)
		{
			FVec3 A, B, C;
			TransformVertsHelper(TriMeshScale, TriIdx, MParticles, Elements, A, B, C);

			if (CalculateTriangleContact(A, B, C, LocalContactLocation, LocalContactNormal, LocalContactPhi))
			{
				if (LocalContactPhi < OutContactPhi)
				{
					OutContactPhi = LocalContactPhi;
					Location = LocalContactLocation;
					Normal = LocalContactNormal;
				}
			}

		}

		return OutContactPhi < Thickness;
	};

	if (MElements.RequiresLargeIndices())
	{
		return LambdaHelper(MElements.GetLargeIndexBuffer());
	}
	return LambdaHelper(MElements.GetSmallIndexBuffer());
}

bool FTriangleMeshImplicitObject::GJKContactPoint(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& ContactPhi) const
{
	return GJKContactPointImp(QueryGeom, QueryTM, Thickness, Location, Normal, ContactPhi);
}

bool FTriangleMeshImplicitObject::GJKContactPoint(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& ContactPhi) const
{
	return GJKContactPointImp(QueryGeom, QueryTM, Thickness, Location, Normal, ContactPhi);
}

bool FTriangleMeshImplicitObject::GJKContactPoint(const FCapsule& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& ContactPhi) const
{
	return GJKContactPointImp(QueryGeom, QueryTM, Thickness, Location, Normal, ContactPhi);
}

bool FTriangleMeshImplicitObject::GJKContactPoint(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& ContactPhi) const
{
	return GJKContactPointImp(QueryGeom, QueryTM, Thickness, Location, Normal, ContactPhi);
}

bool FTriangleMeshImplicitObject::GJKContactPoint(const TImplicitObjectScaled< TSphere<FReal, 3> >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& ContactPhi, FVec3 TriMeshScale) const
{
	return GJKContactPointImp(QueryGeom, QueryTM, Thickness, Location, Normal, ContactPhi, TriMeshScale);
}

bool FTriangleMeshImplicitObject::GJKContactPoint(const TImplicitObjectScaled< TBox<FReal, 3> >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& ContactPhi, FVec3 TriMeshScale) const
{
	return GJKContactPointImp(QueryGeom, QueryTM, Thickness, Location, Normal, ContactPhi, TriMeshScale);
}

bool FTriangleMeshImplicitObject::GJKContactPoint(const TImplicitObjectScaled< FCapsule >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& ContactPhi, FVec3 TriMeshScale) const
{
	return GJKContactPointImp(QueryGeom, QueryTM, Thickness, Location, Normal, ContactPhi, TriMeshScale);
}

bool FTriangleMeshImplicitObject::GJKContactPoint(const TImplicitObjectScaled< FConvex >& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FVec3& Location, FVec3& Normal, FReal& ContactPhi, FVec3 TriMeshScale) const
{
	return GJKContactPointImp(QueryGeom, QueryTM, Thickness, Location, Normal, ContactPhi, TriMeshScale);
}

int32 FTriangleMeshImplicitObject::GetExternalFaceIndexFromInternal(int32 InternalFaceIndex) const
{
	if (InternalFaceIndex > -1 && ExternalFaceIndexMap.Get())
	{
		if (ensure(InternalFaceIndex >= 0 && InternalFaceIndex < ExternalFaceIndexMap->Num()))
		{
			return (*ExternalFaceIndexMap)[InternalFaceIndex];
		}
	}

	return -1;
}

bool FTriangleMeshImplicitObject::GetCullsBackFaceRaycast() const
{
	return bCullsBackFaceRaycast;
}

void FTriangleMeshImplicitObject::SetCullsBackFaceRaycast(const bool bInCullsBackFace)
{
	bCullsBackFaceRaycast = bInCullsBackFace;
}

template <typename IdxType>
bool FTriangleMeshImplicitObject::OverlapImp(const TArray<TVec3<IdxType>>& Elements, const FVec3& Point, const FReal Thickness) const
{
	FAABB3 QueryBounds(Point, Point);
	QueryBounds.Thicken(Thickness);
	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);

	const FReal Epsilon = 1e-4;
	//ensure(Thickness > Epsilon);	//There's no hope for this to work unless thickness is large (really a sphere overlap test)
	//todo: turn ensure back on, off until some other bug is fixed

	for (int32 TriIdx : PotentialIntersections)
	{
		const FVec3& A = MParticles.X(Elements[TriIdx][0]);
		const FVec3& B = MParticles.X(Elements[TriIdx][1]);
		const FVec3& C = MParticles.X(Elements[TriIdx][2]);

		const FVec3 AB = B - A;
		const FVec3 AC = C - A;
		FVec3 Normal = FVec3::CrossProduct(AB, AC);
		const FReal NormalLength = Normal.SafeNormalize();
		if (!CHAOS_ENSURE(NormalLength > Epsilon))
		{
			//hitting degenerate triangle - should be fixed before we get to this stage
			continue;
		}

		const TPlane<FReal, 3> TriPlane{A, Normal};
		const FVec3 ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Point);
		const FReal Distance2 = (ClosestPointOnTri - Point).SizeSquared();
		if (Distance2 <= Thickness * Thickness) //This really only has a hope in working if thickness is > 0
		{
			return true;
		}
	}
	return false;
}

bool FTriangleMeshImplicitObject::Overlap(const FVec3& Point, const FReal Thickness) const
{
	if (MElements.RequiresLargeIndices())
	{
		return OverlapImp(MElements.GetLargeIndexBuffer(), Point, Thickness);
	}
	else
	{
		return OverlapImp(MElements.GetSmallIndexBuffer(), Point, Thickness);
	}
}

void FTriangleMeshImplicitObject::VisitTriangles(const FAABB3& QueryBounds, const TFunction<void(const FTriangle& Triangle)>& Visitor) const
{
	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);

	auto TriangleProducer = [&](const auto& Elements)
	{
		for (int32 TriIdx : PotentialIntersections)
		{
			TVec3<FReal> A, B, C;
			TransformVertsHelper(FVec3(1), TriIdx, MParticles, Elements, A, B, C);

			Visitor(FTriangle(A, B, C));
		}
	};

	if (MElements.RequiresLargeIndices())
	{
		return TriangleProducer(MElements.GetLargeIndexBuffer());
	}
	else
	{
		return TriangleProducer(MElements.GetSmallIndexBuffer());
	}
}


template <typename QueryGeomType>
bool FTriangleMeshImplicitObject::OverlapGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD, FVec3 TriMeshScale) const
{
	bool bResult = false;
	FAABB3 QueryBounds = QueryGeom.BoundingBox();
	QueryBounds.Thicken(Thickness);
	QueryBounds = QueryBounds.TransformedAABB(QueryTM);
	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);

	if (OutMTD)
	{
		OutMTD->Normal = FVec3(0.0);
		OutMTD->Penetration = TNumericLimits<FReal>::Lowest();
	}

	const auto& WorldScaleQueryGeom = ScaleGeomIntoWorldHelper(QueryGeom, TriMeshScale);

	TRigidTransform<FReal, 3> WorldScaleQueryTM;
	ScaleTransformHelper(TriMeshScale, QueryTM, WorldScaleQueryTM);

	auto LambdaHelper = [&](const auto& Elements, FMTDInfo* InnerMTD)
	{
		if (InnerMTD)
		{
			bool bOverlap = false;
			for (int32 TriIdx : PotentialIntersections)
			{
				FVec3 A, B, C;
				TransformVertsHelper(TriMeshScale, TriIdx, MParticles, Elements, A, B, C);

				FVec3 TriangleNormal(0.0);
				FReal Penetration = 0.0;
				FVec3 ClosestA(0.0);
				FVec3 ClosestB(0.0);
				int32 ClosestVertexIndexA, ClosestVertexIndexB;
				if (GJKPenetration(FTriangle(A, B, C), WorldScaleQueryGeom, WorldScaleQueryTM, Penetration, ClosestA, ClosestB, TriangleNormal, ClosestVertexIndexA, ClosestVertexIndexB, Thickness))
				{
					bOverlap = true;

					// Use Deepest MTD.
					if (Penetration > InnerMTD->Penetration)
					{
						InnerMTD->Penetration = Penetration;
						InnerMTD->Normal = TriangleNormal;
					}
				}
			}

			return bOverlap;
		}
		else
		{
			for (int32 TriIdx : PotentialIntersections)
			{
				FVec3 A, B, C;
				TransformVertsHelper(TriMeshScale, TriIdx, MParticles, Elements, A, B, C);

				const FVec3 AB = B - A;
				const FVec3 AC = C - A;

				//It's most likely that the query object is in front of the triangle since queries tend to be on the outside.
				//However, maybe we should check if it's behind the triangle plane. Also, we should enforce this winding in some way
				const FVec3 Offset = FVec3::CrossProduct(AB, AC);

				if (GJKIntersection(FTriangle(A, B, C), WorldScaleQueryGeom, WorldScaleQueryTM, Thickness, Offset))
				{
					return true;
				}
			}

			return false;
		}
	};

	if(MElements.RequiresLargeIndices())
	{
		return LambdaHelper(MElements.GetLargeIndexBuffer(), OutMTD);
	}
	else
	{
		return LambdaHelper(MElements.GetSmallIndexBuffer(), OutMTD);
	}
}

bool FTriangleMeshImplicitObject::OverlapGeom(const TSphere<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD) const
{
	return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
}

bool FTriangleMeshImplicitObject::OverlapGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD) const
{
	return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
}

bool FTriangleMeshImplicitObject::OverlapGeom(const FCapsule& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD) const
{
	return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
}

bool FTriangleMeshImplicitObject::OverlapGeom(const FConvex& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD) const
{
	return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD);
}

bool FTriangleMeshImplicitObject::OverlapGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD, FVec3 TriMeshScale) const
{
	return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD, TriMeshScale);
}

bool FTriangleMeshImplicitObject::OverlapGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD, FVec3 TriMeshScale) const
{
	return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD, TriMeshScale);
}

bool FTriangleMeshImplicitObject::OverlapGeom(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD, FVec3 TriMeshScale) const
{
	return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD, TriMeshScale);
}

bool FTriangleMeshImplicitObject::OverlapGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& QueryTM, const FReal Thickness, FMTDInfo* OutMTD, FVec3 TriMeshScale) const
{
	return OverlapGeomImp(QueryGeom, QueryTM, Thickness, OutMTD, TriMeshScale);
}

template <typename QueryGeomType, typename IdxType>
struct FTriangleMeshSweepVisitor
{
	FTriangleMeshSweepVisitor(const FTriangleMeshImplicitObject& InTriMesh, const TArray<TVec3<IdxType>>& InElements, const QueryGeomType& InQueryGeom, const TRigidTransform<FReal,3>& InStartTM, const FVec3& InDir,
		const FVec3& InScaledDirNormalized, const FReal InLengthScale, const FRigidTransform3& InScaledStartTM, const FReal InThickness, const bool InComputeMTD, FVec3 InTriMeshScale)
	: TriMesh(InTriMesh)
	, Elements(InElements)
	, StartTM(InStartTM)
	, QueryGeom(InQueryGeom)
	, Dir(InDir)
	, Thickness(InThickness)
	, bComputeMTD(InComputeMTD)
	, ScaledDirNormalized(InScaledDirNormalized)
	, LengthScale(InLengthScale)
	, ScaledStartTM(InScaledStartTM)
	, OutTime(TNumericLimits<FReal>::Max())
	, TriMeshScale(InTriMeshScale)
	{
	}

	const void* GetQueryData() const { return nullptr; }

	bool VisitOverlap(const TSpatialVisitorData<int32>& VisitData)
	{
		check(false);
		return true;
	}

	bool VisitRaycast(const TSpatialVisitorData<int32>& VisitData, FQueryFastData& CurData)
	{
		check(false);
		return true;
	}

	bool VisitSweep(const TSpatialVisitorData<int32>& VisitData, FQueryFastData& CurData)
	{
		const int32 TriIdx = VisitData.Payload;

		FReal Time;
		FVec3 HitPosition;
		FVec3 HitNormal;

		FVec3 A, B, C;
		TransformVertsHelper(TriMeshScale,TriIdx,TriMesh.MParticles,Elements,A,B,C);
		FTriangle Tri(A, B, C);

		const auto& WorldScaleQueryGeom = ScaleGeomIntoWorldHelper(QueryGeom, TriMeshScale);

		if(GJKRaycast2<FReal>(Tri, WorldScaleQueryGeom, ScaledStartTM, ScaledDirNormalized, LengthScale * CurData.CurrentLength, Time, HitPosition, HitNormal, Thickness, bComputeMTD))
		{
			// Time is world scale, OutTime is local scale.
			if(Time < LengthScale * OutTime)
			{
				TransformSweepOutputsHelper(TriMeshScale, HitNormal, HitPosition, LengthScale, Time, OutNormal, OutPosition, OutTime);

				OutFaceIndex = TriIdx;

				if(Time <= 0)	//MTD or initial overlap
				{
					CurData.SetLength(0);

					//initial overlap, no one will beat this
					return false;
				}

				CurData.SetLength(OutTime);
			}
		}

		return true;
	}

	const FTriangleMeshImplicitObject& TriMesh;
	const TArray<TVec3<IdxType>>& Elements;
	const FRigidTransform3 StartTM;
	const QueryGeomType& QueryGeom;
	const FVec3& Dir;
	const FReal Thickness;
	const bool bComputeMTD;

	// Cache these values for Scaled Triangle Mesh, as they are needed for transformation when sweeping against triangles.
	FVec3 ScaledDirNormalized;
	FReal LengthScale;
	FRigidTransform3 ScaledStartTM;

	FReal OutTime;
	FVec3 OutPosition;
	FVec3 OutNormal;
	int32 OutFaceIndex;

	FVec3 TriMeshScale;
};

void ComputeScaledSweepInputs(FVec3 TriMeshScale, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length,
	FVec3& OutScaledDirNormalized, FReal& OutLengthScale, FRigidTransform3& OutScaledStartTM)
{
	const FVec3 UnscaledDirDenorm = TriMeshScale * Dir;
	const FReal LengthScale = UnscaledDirDenorm.Size();
	if (CHAOS_ENSURE(LengthScale > TNumericLimits<FReal>::Min()))
	{
		const FReal LengthScaleInv = 1.f / LengthScale;
		OutScaledDirNormalized = UnscaledDirDenorm * LengthScaleInv;
	}


	OutLengthScale = LengthScale;
	OutScaledStartTM = FRigidTransform3(StartTM.GetLocation() * TriMeshScale, StartTM.GetRotation());
}

template <typename QueryGeomType>
bool FTriangleMeshImplicitObject::SweepGeomImp(const QueryGeomType& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length,
	FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD, FVec3 TriMeshScale) const
{
	//QUICK_SCOPE_CYCLE_COUNTER(TrimeshSweep);
	// Compute scaled sweep inputs to cache in visitor.
	FVec3 ScaledDirNormalized;
	FReal LengthScale;
	FRigidTransform3 ScaledStartTM;
	ComputeScaledSweepInputs(TriMeshScale, StartTM, Dir, Length, ScaledDirNormalized, LengthScale, ScaledStartTM);

	bool bHit = false;
	auto LambdaHelper = [&](const auto& Elements)
	{
		using VisitorType = FTriangleMeshSweepVisitor<QueryGeomType,decltype(Elements[0][0])>;
		VisitorType SQVisitor(*this,Elements, QueryGeom,StartTM,Dir,ScaledDirNormalized,LengthScale,ScaledStartTM,Thickness,bComputeMTD, TriMeshScale);


		const FAABB3 QueryBounds = QueryGeom.BoundingBox().TransformedAABB(FRigidTransform3(FVec3::ZeroVector,StartTM.GetRotation()));
		const FVec3 StartPoint = StartTM.TransformPositionNoScale(QueryBounds.Center());
		const FVec3 Inflation = QueryBounds.Extents() * 0.5 + FVec3(Thickness);
		BVH.template Sweep<VisitorType>(StartPoint,Dir,Length,Inflation,SQVisitor);

		if(SQVisitor.OutTime <= Length)
		{
			OutTime = SQVisitor.OutTime;
			OutPosition = SQVisitor.OutPosition;
			OutNormal = SQVisitor.OutNormal;
			OutFaceIndex = SQVisitor.OutFaceIndex;
			bHit = true;
		}
	};

	if(MElements.RequiresLargeIndices())
	{
		LambdaHelper(MElements.GetLargeIndexBuffer());
	}
	else
	{
		LambdaHelper(MElements.GetSmallIndexBuffer());
	}
	return bHit;
}

bool FTriangleMeshImplicitObject::SweepGeom(const TSphere<FReal,3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD) const
{
	return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
}

bool FTriangleMeshImplicitObject::SweepGeom(const TBox<FReal, 3>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD) const
{
	return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
}

bool FTriangleMeshImplicitObject::SweepGeom(const FCapsule& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD) const
{
	return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
}

bool FTriangleMeshImplicitObject::SweepGeom(const FConvex& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD) const
{
	return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
}

bool FTriangleMeshImplicitObject::SweepGeom(const TImplicitObjectScaled<TSphere<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD, FVec3 TriMeshScale) const
{
	return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD, TriMeshScale);
}

bool FTriangleMeshImplicitObject::SweepGeom(const TImplicitObjectScaled<TBox<FReal, 3>>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD, FVec3 TriMeshScale) const
{
	return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD, TriMeshScale);
}

bool FTriangleMeshImplicitObject::SweepGeom(const TImplicitObjectScaled<FCapsule>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD, FVec3 TriMeshScale) const
{
	return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD, TriMeshScale);
}

bool FTriangleMeshImplicitObject::SweepGeom(const TImplicitObjectScaled<FConvex>& QueryGeom, const FRigidTransform3& StartTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD, FVec3 TriMeshScale) const
{
	return SweepGeomImp(QueryGeom, StartTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD, TriMeshScale);
}

template <typename IdxType>
int32 FTriangleMeshImplicitObject::FindMostOpposingFace(const TArray<TVec3<IdxType>>& Elements, const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const
{
	//todo: this is horribly slow, need adjacency information
	const FReal SearchDist2 = SearchDist * SearchDist;

	FAABB3 QueryBounds(Position - FVec3(SearchDist), Position + FVec3(SearchDist));

	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);
	const FReal Epsilon = 1e-4;

	FReal MostOpposingDot = TNumericLimits<FReal>::Max();
	int32 MostOpposingFace = HintFaceIndex;

	for (int32 TriIdx : PotentialIntersections)
	{
		const FVec3& A = MParticles.X(Elements[TriIdx][0]);
		const FVec3& B = MParticles.X(Elements[TriIdx][1]);
		const FVec3& C = MParticles.X(Elements[TriIdx][2]);

		const FVec3 AB = B - A;
		const FVec3 AC = C - A;
		FVec3 Normal = FVec3::CrossProduct(AB, AC);
		const FReal NormalLength = Normal.SafeNormalize();
		if (!CHAOS_ENSURE(NormalLength > Epsilon))
		{
			//hitting degenerate triangle - should be fixed before we get to this stage
			continue;
		}

		const TPlane<FReal, 3> TriPlane{A, Normal};
		const FVec3 ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Position);
		const FReal Distance2 = (ClosestPointOnTri - Position).SizeSquared();
		if (Distance2 < SearchDist2)
		{
			const FReal Dot = FVec3::DotProduct(Normal, UnitDir);
			if (Dot < MostOpposingDot)
			{
				MostOpposingDot = Dot;
				MostOpposingFace = TriIdx;
			}
		}
	}

	return MostOpposingFace;
}

int32 FTriangleMeshImplicitObject::FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const
{
	if (MElements.RequiresLargeIndices())
	{
		return FindMostOpposingFace(MElements.GetLargeIndexBuffer(), Position, UnitDir, HintFaceIndex, SearchDist);
	}
	else
	{
		return FindMostOpposingFace(MElements.GetSmallIndexBuffer(), Position, UnitDir, HintFaceIndex, SearchDist);
	}
}

FVec3 FTriangleMeshImplicitObject::FindGeometryOpposingNormal(const FVec3& DenormDir, int32 FaceIndex, const FVec3& OriginalNormal) const
{
	return GetFaceNormal(FaceIndex);
}

template <typename IdxType>
TUniquePtr<FTriangleMeshImplicitObject> FTriangleMeshImplicitObject::CopySlowImpl(const TArray<TVector<IdxType, 3>>& InElements) const
{
	using namespace Chaos;

	TArray<Chaos::FVec3> XArray = MParticles.AllX();
	FParticles ParticlesCopy(MoveTemp(XArray));
	TArray<TVector<IdxType, 3>> ElementsCopy(InElements);
	TArray<uint16> MaterialIndicesCopy = MaterialIndices;
	TUniquePtr<TArray<int32>> ExternalFaceIndexMapCopy = nullptr;
	if (ExternalFaceIndexMap)
	{
		ExternalFaceIndexMapCopy = MakeUnique<TArray<int32>>(*ExternalFaceIndexMap.Get());
	}

	TUniquePtr<TArray<int32>> ExternalVertexIndexMapCopy = nullptr;
	if (ExternalVertexIndexMap && TriMeshPerPolySupport)
	{
		ExternalVertexIndexMapCopy = MakeUnique<TArray<int32>>(*ExternalVertexIndexMap.Get());
	}

	return MakeUnique<FTriangleMeshImplicitObject>(MoveTemp(ParticlesCopy), MoveTemp(ElementsCopy), MoveTemp(MaterialIndicesCopy), MoveTemp(ExternalFaceIndexMapCopy), MoveTemp(ExternalVertexIndexMapCopy), bCullsBackFaceRaycast);
}

TUniquePtr<FTriangleMeshImplicitObject> FTriangleMeshImplicitObject::CopySlow() const
{
	if (MElements.RequiresLargeIndices())
	{
		return CopySlowImpl(MElements.GetLargeIndexBuffer());
	}
	else
	{
		return CopySlowImpl(MElements.GetSmallIndexBuffer());
	}
}


void FTriangleMeshImplicitObject::Serialize(FChaosArchive& Ar)
{
	FChaosArchiveScopedMemory ScopedMemory(Ar, GetTypeName());
	SerializeImp(Ar);
}

uint32 FTriangleMeshImplicitObject::GetTypeHash() const
{
	uint32 Result = MParticles.GetTypeHash();
	Result = HashCombine(Result, MLocalBoundingBox.GetTypeHash());

	auto LambdaHelper = [&](const auto& Elements)
	{
		for (TVector<int32, 3> Tri : Elements)
		{
			uint32 TriHash = HashCombine(::GetTypeHash(Tri[0]), HashCombine(::GetTypeHash(Tri[1]), ::GetTypeHash(Tri[2])));
			Result = HashCombine(Result, TriHash);
		}
	};

	if (MElements.RequiresLargeIndices())
	{
		LambdaHelper(MElements.GetLargeIndexBuffer());
	}
	else
	{
		LambdaHelper(MElements.GetSmallIndexBuffer());
	}

	return Result;
}

FVec3 FTriangleMeshImplicitObject::GetFaceNormal(const int32 FaceIdx) const
{
	if (CHAOS_ENSURE(FaceIdx != INDEX_NONE))
	{
		auto LambdaHelper = [&](const auto& Elements)
		{
			const FVec3& A = MParticles.X(Elements[FaceIdx][0]);
			const FVec3& B = MParticles.X(Elements[FaceIdx][1]);
			const FVec3& C = MParticles.X(Elements[FaceIdx][2]);

			const FVec3 AB = B - A;
			const FVec3 AC = C - A;
			FVec3 Normal = FVec3::CrossProduct(AB, AC);
			if (Utilities::NormalizeSafe(Normal))
			{
				return Normal;
			}
	
			UE_LOG(LogChaos, Warning, TEXT("Degenerate triangle %d: (%f %f %f) (%f %f %f) (%f %f %f)"), FaceIdx, A.X, A.Y, A.Z, B.X, B.Y, B.Z, C.X, C.Y, C.Z);
			ensure(false);
			return FVec3(0, 0, 1);
		};
		
		if (MElements.RequiresLargeIndices())
		{
			return LambdaHelper(MElements.GetLargeIndexBuffer());
		}
		else
		{
			return LambdaHelper(MElements.GetSmallIndexBuffer());
		}
	}

	return FVec3(0, 0, 1);
}

uint16 FTriangleMeshImplicitObject::GetMaterialIndex(uint32 HintIndex) const
{
	if (MaterialIndices.IsValidIndex(HintIndex))
	{
		return MaterialIndices[HintIndex];
	}

	// 0 should always be the default material for a shape
	return 0;
}

const FParticles& FTriangleMeshImplicitObject::Particles() const
{
	return MParticles;
}

const FTrimeshIndexBuffer& FTriangleMeshImplicitObject::Elements() const
{
	return MElements;
}

template <typename IdxType>
void FTriangleMeshImplicitObject::RebuildBVImp(const TArray<TVec3<IdxType>>& Elements)
{
	const int32 NumTris = Elements.Num();
	TArray<FBvEntry<sizeof(IdxType) == sizeof(FTrimeshIndexBuffer::LargeIdxType)>> BVEntries;
	BVEntries.Reset(NumTris);

	for (int Tri = 0; Tri < NumTris; Tri++)
	{
		BVEntries.Add({this, Tri});
	}
	BVH.Reinitialize(BVEntries);
}

FTriangleMeshImplicitObject::~FTriangleMeshImplicitObject() = default;

void Chaos::FTriangleMeshImplicitObject::RebuildBV()
{
	if (MElements.RequiresLargeIndices())
	{
		RebuildBVImp(MElements.GetLargeIndexBuffer());
	}
	else
	{
		RebuildBVImp(MElements.GetSmallIndexBuffer());
	}
}

void FTriangleMeshImplicitObject::UpdateVertices(const TArray<FVector>& NewPositions)
{
	if(TriMeshPerPolySupport == false)
	{
		// We don't have vertex map, this will not be correct.
		ensure(false);
		return;
	}

	const bool bRemapIndices = ExternalVertexIndexMap != nullptr;

	for (int32 i = 0; i < NewPositions.Num(); ++i)
	{
		int32 InternalIdx = bRemapIndices ? (*ExternalVertexIndexMap.Get())[i] : i;
		if (InternalIdx < (int32)MParticles.Size())
		{
			MParticles.X(InternalIdx) = Chaos::FVec3(NewPositions[i]);
		}
	}

	RebuildBV();
}


}
