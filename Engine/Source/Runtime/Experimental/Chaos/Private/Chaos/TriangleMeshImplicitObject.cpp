// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/GJK.h"
#include "Chaos/ImplicitObjectScaled.h"

namespace Chaos
{
template <typename T>
TTriangleMeshImplicitObject<T>::TTriangleMeshImplicitObject(TParticles<T, 3>&& Particles, TArray<TVector<int32, 3>>&& Elements)
	: TImplicitObject<T, 3>(EImplicitObject::HasBoundingBox, ImplicitObjectType::TriangleMesh)
	, MParticles(MoveTemp(Particles))
	, MElements(MoveTemp(Elements))
	, MLocalBoundingBox(MParticles.X(0), MParticles.X(0))
	, BVH(MakeParticleView(&ConvexHulls))
{
	for (uint32 Idx = 1; Idx < MParticles.Size(); ++Idx)
	{
		MLocalBoundingBox.GrowToInclude(MParticles.X(Idx));
	}

	RebuildConvexHulls();
	RebuildBV();
}

template <typename T>
struct TTriangleMeshRaycastVisitor
{
	TTriangleMeshRaycastVisitor(const TVector<T,3>& InStart, const TVector<T,3>& InDir, const T InThickness, const TParticles<T,3>& InParticles, const TArray<TVector<int32, 3>>& InElements)
	: Particles(InParticles)
	, Elements(InElements)
	, StartPoint(InStart)
	, Dir(InDir)
	, Thickness(InThickness)
	, OutTime(TNumericLimits<T>::Max())
	{
	}

	enum class ERaycastType
	{
		Raycast,
		Sweep
	};

	template <ERaycastType SQType>
	bool Visit(int32 TriIdx, T& CurLength)
	{
		constexpr T Epsilon = 1e-4;
		constexpr T Epsilon2 = Epsilon * Epsilon;
		const T Thickness2 = SQType == ERaycastType::Sweep ? Thickness * Thickness : 0;
		T MinTime = 0;	//no need to initialize, but fixes warning

		const T R = Thickness + Epsilon;
		const T R2 = R * R;

		const TVector<T, 3>& A = Particles.X(Elements[TriIdx][0]);
		const TVector<T, 3>& B = Particles.X(Elements[TriIdx][1]);
		const TVector<T, 3>& C = Particles.X(Elements[TriIdx][2]);

		const TVector<T, 3> AB = B - A;
		const TVector<T, 3> AC = C - A;
		TVector<T, 3> TriNormal = TVector<T, 3>::CrossProduct(AB, AC);
		const T NormalLength = TriNormal.SafeNormalize();
		if (!ensure(NormalLength > Epsilon))
		{
			//hitting degenerate triangle so keep searching - should be fixed before we get to this stage
			return true;
		}

		const TPlane<T, 3> TriPlane{ A, TriNormal };
		TVector<T, 3> RaycastPosition;
		TVector<T, 3> RaycastNormal;
		T Time;

		//Check if we even intersect with triangle plane
		int32 DummyFaceIndex;
		if (TriPlane.Raycast(StartPoint, Dir, CurLength, Thickness, Time, RaycastPosition, RaycastNormal, DummyFaceIndex))
		{
			TVector<T, 3> IntersectionPosition = RaycastPosition;
			TVector<T, 3> IntersectionNormal = RaycastNormal;
			bool bTriangleIntersects = false;
			if (Time == 0)
			{
				//Initial overlap so no point of intersection, do an explicit sphere triangle test.
				const TVector<T, 3> ClosestPtOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, StartPoint);
				const T DistToTriangle2 = (StartPoint - ClosestPtOnTri).SizeSquared();
				if (DistToTriangle2 <= R2)
				{
					OutTime = 0;
					OutFaceIndex = TriIdx;
					return false; //no one will beat Time == 0
				}
			}
			else
			{
				const TVector<T, 3> ClosestPtOnTri = FindClosestPointOnTriangle(RaycastPosition, A, B, C, RaycastPosition);	//We know Position is on the triangle plane
				const T DistToTriangle2 = (RaycastPosition - ClosestPtOnTri).SizeSquared();
				bTriangleIntersects = DistToTriangle2 <= Epsilon2;	//raycast gave us the intersection point so sphere radius is already accounted for
			}

			if (SQType == ERaycastType::Sweep && !bTriangleIntersects)
			{
				//sphere is not immediately touching the triangle, but it could start intersecting the perimeter as it sweeps by
				TVector<T, 3> BorderPositions[3];
				TVector<T, 3> BorderNormals[3];
				T BorderTimes[3];
				bool bBorderIntersections[3];

				const TCapsule<T> ABCapsule(A, B, Thickness);
				bBorderIntersections[0] = ABCapsule.Raycast(StartPoint, Dir, CurLength, 0, BorderTimes[0], BorderPositions[0], BorderNormals[0], DummyFaceIndex);

				const TCapsule<T> BCCapsule(B, C, Thickness);
				bBorderIntersections[1] = BCCapsule.Raycast(StartPoint, Dir, CurLength, 0, BorderTimes[1], BorderPositions[1], BorderNormals[1], DummyFaceIndex);

				const TCapsule<T> ACCapsule(A, C, Thickness);
				bBorderIntersections[2] = ACCapsule.Raycast(StartPoint, Dir, CurLength, 0, BorderTimes[2], BorderPositions[2], BorderNormals[2], DummyFaceIndex);

				int32 MinBorderIdx = INDEX_NONE;
				T MinBorderTime = 0;	//initialization not needed, but fixes warning

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
						TVector<T, 3> TmpNormal;
						const T SignedDistance = TriPlane.PhiWithNormal(StartPoint, TmpNormal);
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
					CurLength = Time;	//prevent future rays from going any farther
					OutFaceIndex = TriIdx;
				}
			}
		}

		return true;
	}

	bool VisitRaycast(int32 TriIdx, T& CurLength)
	{
		return Visit<ERaycastType::Raycast>(TriIdx, CurLength);
	}

	bool VisitSweep(int32 TriIdx, T& CurLength)
	{
		return Visit<ERaycastType::Sweep>(TriIdx, CurLength);
	}

	const TParticles<T, 3>& Particles;
	const TArray<TVector<int32, 3>>& Elements;
	const TVector<T, 3>& StartPoint;
	const TVector<T, 3>& Dir;
	const T Thickness;
	T OutTime;
	TVector<T, 3> OutPosition;
	TVector<T, 3> OutNormal;
	int32 OutFaceIndex;
};

template <typename T>
bool TTriangleMeshImplicitObject<T>::Raycast(const TVector<T, 3>& StartPoint, const TVector<T, 3>& Dir, const T Length, const T Thickness, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex) const
{
	TTriangleMeshRaycastVisitor<T> SQVisitor(StartPoint, Dir, Thickness, MParticles, MElements);

	if (Thickness > 0)
	{
		BVH.Sweep(StartPoint, Dir, Length, TVector<T, 3>(Thickness), SQVisitor);
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

template <typename T>
bool TTriangleMeshImplicitObject<T>::Overlap(const TVector<T, 3>& Point, const T Thickness) const
{
	TBox<T, 3> QueryBounds(Point, Point);
	QueryBounds.Thicken(Thickness);
	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);

	const T Epsilon = 1e-4;
	//ensure(Thickness > Epsilon);	//There's no hope for this to work unless thickness is large (really a sphere overlap test)
	//todo: turn ensure back on, off until some other bug is fixed

	for (int32 TriIdx : PotentialIntersections)
	{
		const TVector<T, 3>& A = MParticles.X(MElements[TriIdx][0]);
		const TVector<T, 3>& B = MParticles.X(MElements[TriIdx][1]);
		const TVector<T, 3>& C = MParticles.X(MElements[TriIdx][2]);

		const TVector<T, 3> AB = B - A;
		const TVector<T, 3> AC = C - A;
		TVector<T, 3> Normal = TVector<T, 3>::CrossProduct(AB, AC);
		const T NormalLength = Normal.SafeNormalize();
		if (!ensure(NormalLength > Epsilon))
		{
			//hitting degenerate triangle - should be fixed before we get to this stage
			continue;
		}

		const TPlane<T, 3> TriPlane{ A, Normal };
		const TVector<T,3> ClosestPointOnTri = FindClosestPointOnTriangle(TriPlane, A, B, C, Point);
		const T Distance2 = (ClosestPointOnTri - Point).SizeSquared();
		if (Distance2 <= Thickness * Thickness)	//This really only has a hope in working if thickness is > 0
		{
			return true;
		}
	}
	return false;
}

template <typename T>
bool TTriangleMeshImplicitObject<T>::OverlapGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& QueryTM, const T Thickness, const TVector<T,3> Scale) const
{
	bool bResult = false;
	TBox<T, 3> QueryBounds = QueryGeom.BoundingBox();
	QueryBounds.Thicken(Thickness);
	QueryBounds = QueryBounds.TransformedBox(QueryTM);
	const TArray<int32> PotentialIntersections = BVH.FindAllIntersections(QueryBounds);

	for (int32 TriIdx : PotentialIntersections)
	{
		const TVector<T, 3>& A = MParticles.X(MElements[TriIdx][0]);
		const TVector<T, 3>& B = MParticles.X(MElements[TriIdx][1]);
		const TVector<T, 3>& C = MParticles.X(MElements[TriIdx][2]);
		const TVector<T, 3> AB = B - A;
		const TVector<T, 3> AC = C - A;
		
		//It's most likely that the query object is in front of the triangle since queries tend to be on the outside.
		//However, maybe we should check if it's behind the triangle plane. Also, we should enforce this winding in some way
		const TVector<T, 3> Offset = TVector<T, 3>::CrossProduct(AB, AC);

		if (GJKIntersection(*ConvexHulls.Geometry(TriIdx), QueryGeom, QueryTM, Thickness, Offset))
		{
			bResult = true;
			break;
		}
	}
	return bResult;
}

template <typename T>
struct TTriangleMeshSweepVisitor
{
	TTriangleMeshSweepVisitor(const TTriangleMeshImplicitObject<T>& InTriMesh, const TImplicitObject<T,3>& InQueryGeom, const TRigidTransform<T,3>& InStartTM, const TVector<T,3>& InDir, const T InThickness, const TVector<T,3>& InScale)
	: TriMesh(InTriMesh)
	, StartTM(InStartTM)
	, QueryGeom(InQueryGeom)
	, Dir(InDir)
	, Scale(InScale)
	, Thickness(InThickness)
	, OutTime(TNumericLimits<T>::Max())
	{
	}

	bool VisitSweep(int32 TriIdx, T& CurLength)
	{
		TImplicitObjectScaled<T, 3> TriConvex(TriMesh.ConvexHulls.Geometry(TriIdx), Scale);
		T Time;
		TVector<T, 3> HitPosition;
		TVector<T, 3> HitNormal;
		if (GJKRaycast<T>(TriConvex, QueryGeom, StartTM, Dir, CurLength, Time, HitPosition, HitNormal, Thickness))
		{
			if (Time < OutTime)
			{
				OutNormal = HitNormal;
				OutPosition = HitPosition;
				OutTime = Time;
				CurLength = Time;
				OutFaceIndex = TriIdx;

				if (Time == 0)
				{
					//initial overlap, no one will beat this
					return false;
				}
			}
		}

		return true;
	}

	const TTriangleMeshImplicitObject<T>& TriMesh;
	const TRigidTransform<T, 3> StartTM;
	const TImplicitObject<T, 3>& QueryGeom;
	const TVector<T, 3>& Dir;
	const TVector<T, 3>& Scale;
	const T Thickness;

	T OutTime;
	TVector<T, 3> OutPosition;
	TVector<T, 3> OutNormal;
	int32 OutFaceIndex;
};

template <typename T>
bool TTriangleMeshImplicitObject<T>::SweepGeom(const TImplicitObject<T, 3>& QueryGeom, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& Dir, const T Length, T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, int32& OutFaceIndex, const T Thickness, const TVector<T,3> Scale) const
{
	bool bHit = false;
	TTriangleMeshSweepVisitor<T> SQVisitor(*this, QueryGeom, StartTM, Dir, Thickness, Scale);
	const TBox<T, 3> QueryBounds = QueryGeom.BoundingBox();
	const TVector<T, 3> StartPoint = StartTM.TransformPositionNoScale(QueryBounds.Center());
	const TVector<T, 3> Inflation = QueryBounds.Extents() * 0.5 + TVector<T, 3>(Thickness);
	BVH.template Sweep<TTriangleMeshSweepVisitor<T>, false>(StartPoint, Dir, Length, Inflation, SQVisitor, Scale);

	if (SQVisitor.OutTime <= Length)
	{
		OutTime = SQVisitor.OutTime;
		OutPosition = SQVisitor.OutPosition;
		OutNormal = SQVisitor.OutNormal;
		OutFaceIndex = SQVisitor.OutFaceIndex;
		bHit = true;
	}
	return bHit;
}

template <typename T>
int32 TTriangleMeshImplicitObject<T>::FindMostOpposingFace(const TVector<T, 3>& Position, const TVector<T, 3>& UnitDir, int32 HintFaceIndex) const
{
	ensure(HintFaceIndex != INDEX_NONE);
	return HintFaceIndex;	//for now we just return what they gave us. todo: use adjacency information to compute this
}


template<typename T>
void Chaos::TTriangleMeshImplicitObject<T>::RebuildBV()
{
	BVH = BVHType(MakeParticleView(&ConvexHulls), false, 0, 64);
}

template <typename T>
void Chaos::TTriangleMeshImplicitObject<T>::RebuildConvexHulls()
{
	ConvexHulls.Resize(MElements.Num());

	for (int32 TriIdx = 0; TriIdx < MElements.Num(); ++TriIdx)
	{
		const TVector<T, 3>& A = MParticles.X(MElements[TriIdx][0]);
		const TVector<T, 3>& B = MParticles.X(MElements[TriIdx][1]);
		const TVector<T, 3>& C = MParticles.X(MElements[TriIdx][2]);

		TParticles<T, 3> Pts;
		Pts.AddParticles(3);
		Pts.X(0) = A;
		Pts.X(1) = B;
		Pts.X(2) = C;

		TUniquePtr<TConvex<T, 3>> TriConvex = MakeUnique<TConvex<T, 3>>(Pts);
		ConvexHulls.X(TriIdx) = TVector<T, 3>(0);
		ConvexHulls.R(TriIdx) = TRotation<T, 3>::Identity;
		ConvexHulls.SetDynamicGeometry(TriIdx, MoveTemp(TriConvex));
	}
}

}

template class Chaos::TTriangleMeshImplicitObject<float>;