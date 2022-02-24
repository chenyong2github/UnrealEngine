// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDCollisionSpringConstraintsBase.h"
#include "Chaos/Plane.h"
#include "Chaos/TriangleCollisionPoint.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/PBDSoftsSolverParticles.h"

#if !COMPILE_WITHOUT_UNREAL_SUPPORT
#include "Chaos/Framework/Parallel.h"
#include <atomic>

namespace Chaos::Softs {

FPBDCollisionSpringConstraintsBase::FPBDCollisionSpringConstraintsBase(
	const int32 InOffset,
	const int32 InNumParticles,
	const FTriangleMesh& InTriangleMesh,
	const TArray<FSolverVec3>* InReferencePositions,
	TSet<TVec2<int32>>&& InDisabledCollisionElements,
	const FSolverReal InThickness,
	const FSolverReal InStiffness)
	: TriangleMesh(InTriangleMesh)
	, Elements(InTriangleMesh.GetSurfaceElements())
	, ReferencePositions(InReferencePositions)
	, DisabledCollisionElements(InDisabledCollisionElements)
	, Offset(InOffset)
	, NumParticles(InNumParticles)
	, Thickness(InThickness)
	, Stiffness(InStiffness)
{
}

void FPBDCollisionSpringConstraintsBase::Init(const FSolverParticles& Particles)
{
	if (!Elements.Num())
	{
		return;
	}

	FTriangleMesh::TBVHType<FSolverReal> BVH;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ChaosPBDCollisionSpring_BuildBVH);
		TriangleMesh.BuildBVH(static_cast<const TArrayView<const FSolverVec3>&>(Particles.XArray()), BVH);
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ChaosPBDCollisionSpring_ProximityQuery);

		// Preallocate enough space for all possible connections.
		constexpr int32 MaxConnectionsPerPoint = 3;
		Constraints.SetNum(NumParticles * MaxConnectionsPerPoint);
		Barys.SetNum(NumParticles * MaxConnectionsPerPoint);

		std::atomic<int32> ConstraintIndex(0);

		const FSolverReal HeightSq = FMath::Square(Thickness + Thickness);
		PhysicsParallelFor(NumParticles,
			[this, &BVH, &Particles, &ConstraintIndex, HeightSq, MaxConnectionsPerPoint](int32 i)
			{
				const int32 Index = i + Offset;

				TArray< TTriangleCollisionPoint<FSolverReal> > Result;
				if (TriangleMesh.PointProximityQuery(BVH, static_cast<const TArrayView<const FSolverVec3>&>(Particles.XArray()), Index, Particles.X(Index), Thickness, Thickness,
					[this](const int32 PointIndex, const int32 TriangleIndex)->bool
					{
						const TVector<int32, 3>& Elem = Elements[TriangleIndex];
						if (DisabledCollisionElements.Contains({ PointIndex, Elem[0] }) ||
							DisabledCollisionElements.Contains({ PointIndex, Elem[1] }) ||
							DisabledCollisionElements.Contains({ PointIndex, Elem[2] }))
						{
							return false;
						}
						return true;
					},
					Result))
				{

					if (Result.Num() > MaxConnectionsPerPoint)
					{
						// TODO: once we have a PartialSort, use that instead here.
						Result.Sort(
							[](const TTriangleCollisionPoint<FSolverReal>& First, const TTriangleCollisionPoint<FSolverReal>& Second)->bool
							{
								return First.Phi < Second.Phi;
							}
						);
						Result.SetNum(MaxConnectionsPerPoint, false /*bAllowShrinking*/);
					}

					for (const TTriangleCollisionPoint<FSolverReal>& CollisionPoint : Result)
					{
						const TVector<int32, 3>& Elem = Elements[CollisionPoint.Indices[1]];
						if (ReferencePositions)
						{
							const FSolverVec3& RefP = (*ReferencePositions)[Index];
							const FSolverVec3& RefP0 = (*ReferencePositions)[Elem[0]];
							const FSolverVec3& RefP1 = (*ReferencePositions)[Elem[1]];
							const FSolverVec3& RefP2 = (*ReferencePositions)[Elem[2]];
							const FSolverVec3 RefDiff = RefP - CollisionPoint.Bary[1] * RefP0 - CollisionPoint.Bary[2] * RefP1 - CollisionPoint.Bary[3] * RefP2;
							if (RefDiff.SizeSquared() < HeightSq)
							{
								continue;
							}
						}
						const int32 IndexToWrite = ConstraintIndex.fetch_add(1);

						Constraints[IndexToWrite] = { Index, Elem[0], Elem[1], Elem[2] };
						Barys[IndexToWrite] = { CollisionPoint.Bary[1], CollisionPoint.Bary[2], CollisionPoint.Bary[3] };
					}
				}
			}
		);

		// Shrink the arrays to the actual number of found constraints.
		const int32 ConstraintNum = ConstraintIndex.load();
		Constraints.SetNum(ConstraintNum, /*bAllowShrinking*/ true);
		Barys.SetNum(ConstraintNum, /*bAllowShrinking*/ true);
	}
}

FSolverVec3 FPBDCollisionSpringConstraintsBase::GetDelta(const FSolverParticles& Particles, const int32 i) const
{
	const TVec4<int32>& Constraint = Constraints[i];
	const int32 i1 = Constraint[0];
	const int32 i2 = Constraint[1];
	const int32 i3 = Constraint[2];
	const int32 i4 = Constraint[3];

	const FSolverReal CombinedMass = Particles.InvM(i1) +
		Particles.InvM(i2) * Barys[i][0] +
		Particles.InvM(i3) * Barys[i][1] +
		Particles.InvM(i4) * Barys[i][2];
	if (CombinedMass <= (FSolverReal)1e-7)
	{
		return FSolverVec3(0);
	}

	const FSolverVec3& P1 = Particles.P(i1);
	const FSolverVec3& P2 = Particles.P(i2);
	const FSolverVec3& P3 = Particles.P(i3);
	const FSolverVec3& P4 = Particles.P(i4);

	const FSolverReal Height = Thickness + Thickness;
	const FSolverVec3 P = Barys[i][0] * P2 + Barys[i][1] * P3 + Barys[i][2] * P4;
	const FSolverVec3 Difference = P1 - P;
	const FSolverReal DistSq = Difference.SizeSquared();
	if (DistSq > Height * Height)
	{
		return FSolverVec3(0);
	}
	const FSolverVec3 Delta = Difference * Height * FMath::InvSqrt(DistSq) - Difference;
	return Stiffness * Delta / CombinedMass;
}

}  // End namespace Chaos::Softs

#endif
