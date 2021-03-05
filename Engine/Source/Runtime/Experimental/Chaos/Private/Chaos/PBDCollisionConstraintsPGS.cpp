// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraintsPGS.h"

#include "Chaos/BoundingVolume.h"
#include "Chaos/BoundingVolumeHierarchy.h"
#include "Chaos/Defines.h"
#include "Chaos/Pair.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"

#include "ProfilingDebugging/ScopedTimers.h"

#if CHAOS_PARTICLEHANDLE_TODO

using namespace Chaos;

template<class T_PARTICLES>
FVec3 GetTranslationPGS(const T_PARTICLES& InParticles)
{
	check(false);
	return FRigidTransform3();
}

FVec3 GetTranslationPGS(const TRigidParticles<FReal,3>& InParticles, const int32 Index)
{
	return InParticles.X(Index);
}

FVec3 GetTranslationPGS(const FPBDRigidParticles& InParticles, const int32 Index)
{
	return InParticles.P(Index);
}

template<class T_PARTICLES>
FRotation3 GetRotationPGS(const T_PARTICLES& InParticles)
{
	check(false);
	return FRotation3();
}

FRotation3 GetRotationPGS(const TRigidParticles<FReal, 3>& InParticles, const int32 Index)
{
	return InParticles.R(Index);
}

FRotation3 GetRotationPGS(const FPBDRigidParticles& InParticles, const int32 Index)
{
	return InParticles.Q(Index);
}

FRigidTransform3 GetTransformPGS(const T_PARTICLES& InParticles)
{
	check(false);
	return FRigidTransform3();
}

FRigidTransform3 GetTransformPGS(const TRigidParticles<FReal, 3>& InParticles, const int32 Index)
{
	return FRigidTransform3(InParticles.X(Index), InParticles.R(Index));
}

FRigidTransform3 GetTransformPGS(const FPBDRigidParticles& InParticles, const int32 Index)
{
	return FRigidTransform3(InParticles.P(Index), InParticles.Q(Index));
}

FPBDCollisionConstraintPGS::FPBDCollisionConstraintPGS(FPBDRigidParticles& InParticles, const TArray<int32>& InIndices, TArrayCollectionArray<bool>& Collided, TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& InPhysicsMaterials, const FReal Thickness /*= (FReal)0*/)
    : MCollided(Collided), MPhysicsMaterials(InPhysicsMaterials), MThickness(Thickness), Tolerance(KINDA_SMALL_NUMBER), MaxIterations(10), bUseCCD(false)
{
}

void FPBDCollisionConstraintPGS::ComputeConstraints(const FPBDRigidParticles& InParticles, const TArray<int32>& InIndices, const FReal Dt)
{
	double Time = 0;
	FDurationTimer Timer(Time);
	// Broad phase
	//TBoundingVolumeHierarchy<FPBDRigidParticles, T, d> Hierarchy(InParticles);
	TBoundingVolume<FPBDRigidParticles> Hierarchy(InParticles, true, Dt);
	Timer.Stop();
	UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Construct Hierarchy %f"), Time);
	// Narrow phase
	FCriticalSection CriticalSection;
	Time = 0;
	Timer.Start();
	PhysicsParallelFor(InIndices.Num(), [&](int32 Index) {
		const int32 ParticleIndex = InIndices[Index];

		TArray<int32> PotentialIntersections;
		FAABB3 Box1;
		if (InParticles.Geometry(ParticleIndex)->HasBoundingBox())
		{
			Box1 = Hierarchy.GetWorldSpaceBoundingBox(InParticles, ParticleIndex);
			PotentialIntersections = Hierarchy.FindAllIntersections(Box1);
		}
		else
		{
			PotentialIntersections = Hierarchy.GlobalObjects();
		}

		for (int32 i = 0; i < PotentialIntersections.Num(); ++i)
		{
			int32 Body2Index = PotentialIntersections[i];

			// Collision group culling...
			// CollisionGroup == 0 : Collide_With_Everything
			// CollisionGroup == INDEX_NONE : Disabled collisions
			// CollisionGroup_A != CollisionGroup_B : Skip Check
			if(InParticles.Disabled(Body2Index))
			{
				continue;
			}
				
			if (InParticles.CollisionGroup(ParticleIndex) == INDEX_NONE || InParticles.CollisionGroup(Body2Index) == INDEX_NONE)
			{
				continue;
			}
			if (InParticles.CollisionGroup(ParticleIndex) && InParticles.CollisionGroup(Body2Index) && InParticles.CollisionGroup(ParticleIndex) != InParticles.CollisionGroup(Body2Index))
			{
				continue;
			}

			if (InParticles.InvM(ParticleIndex) < FLT_MIN && InParticles.InvM(Body2Index) < FLT_MIN)
			{
				continue;
			}
			if (ParticleIndex == Body2Index || ((InParticles.Geometry(ParticleIndex)->HasBoundingBox() == InParticles.Geometry(Body2Index)->HasBoundingBox()) && Body2Index > ParticleIndex))
			{
				continue;
			}
			if (InParticles.Geometry(ParticleIndex)->HasBoundingBox() && InParticles.Geometry(Body2Index)->HasBoundingBox())
			{
				const auto& Box2 = Hierarchy.GetWorldSpaceBoundingBox(InParticles, Body2Index);
				if (!Box1.Intersects(Box2))
				{
					continue;
				}
			}
			auto Constraint = ComputeConstraint(InParticles, ParticleIndex, Body2Index, MThickness);
			CriticalSection.Lock();
			Constraints.Add(Constraint);
			CriticalSection.Unlock();
		}
	});

	Timer.Stop();

	UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Construct %d Constraints with Potential Collisions %f"), Constraints.Num(), Time);
}

void FPBDCollisionConstraintPGS::RemoveConstraints(const TSet<uint32>& RemovedParticles)
{
	for (int32 i = 0; i < Constraints.Num(); ++i)
	{
		const auto& Constraint = Constraints[i];
		if (RemovedParticles.Contains(Constraint.ParticleIndex) || RemovedParticles.Contains(Constraint.LevelsetIndex))
		{
			Constraints.RemoveAtSwap(i);
			i--;
		}
	}
}

void FPBDCollisionConstraintPGS::UpdateConstraints(const FPBDRigidParticles& InParticles, T Dt, const TSet<uint32>& AddedParticles, const TArray<uint32>& ActiveParticles)
{
	double Time = 0;
	FDurationTimer Timer(Time);

	//
	// Broad phase
	//

	// @todo(mlentine): We only need to construct the hierarchy for the islands we care about
	TBoundingVolume<FPBDRigidParticles> Hierarchy(InParticles, ActiveParticles, true, Dt);
	Timer.Stop();
	UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Construct Hierarchy %f"), Time);

	//
	// Narrow phase
	//

	FCriticalSection CriticalSection;
	Time = 0;
	Timer.Start();
	TArray<uint32> AddedParticlesArray = AddedParticles.Array();
	PhysicsParallelFor(AddedParticlesArray.Num(), [&](int32 Index) {
		int32 Body1Index = AddedParticlesArray[Index];
		if (InParticles.Disabled(Body1Index))
			return;
		TArray<int32> PotentialIntersections;
		FAABB3 Box1;
		if (InParticles.Geometry(Body1Index)->HasBoundingBox())
		{
			Box1 = Hierarchy.GetWorldSpaceBoundingBox(InParticles, Body1Index);
			PotentialIntersections = Hierarchy.FindAllIntersections(Box1);
		}
		else
		{
			PotentialIntersections = Hierarchy.GlobalObjects();
		}
		for (int32 i = 0; i < PotentialIntersections.Num(); ++i)
		{
			int32 Body2Index = PotentialIntersections[i];

			if (Body1Index == Body2Index || ((InParticles.Geometry(Body1Index)->HasBoundingBox() == InParticles.Geometry(Body2Index)->HasBoundingBox()) && AddedParticles.Contains(Body2Index) && AddedParticles.Contains(Body1Index) && Body2Index > Body1Index))
			{
				continue;
			}
			if (InParticles.Geometry(Body1Index)->HasBoundingBox() && InParticles.Geometry(Body2Index)->HasBoundingBox())
			{
				const auto& Box2 = Hierarchy.GetWorldSpaceBoundingBox(InParticles, Body2Index);
				if (!Box1.Intersects(Box2))
				{
					continue;
				}
			}
			
			if (InParticles.InvM(Body1Index) && InParticles.InvM(Body2Index) && (InParticles.Island(Body1Index) != InParticles.Island(Body2Index)))	//todo(ocohen): this is a hack - we should not even consider dynamics from other islands
			{
				continue;
			}
			auto Constraint = ComputeConstraint(InParticles, Body1Index, Body2Index, MThickness);
			CriticalSection.Lock();
			Constraints.Add(Constraint);
			CriticalSection.Unlock();
		}
	});
	Timer.Stop();
	UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Update %d Constraints with Potential Collisions %f"), Constraints.Num(), Time);
}

template<class T_PARTICLES>
void ComputePGSPropeties(const T_PARTICLES& InParticles, const FRigidBodyContactConstraintPGS& Constraint, const int32 PointIndex, const int32 FlattenedIndex,
	const FMatrix33& WorldSpaceInvI1, const FMatrix33& WorldSpaceInvI2, const FVec3 Direction,
	TArray<TVec2<FVec3>>& Angulars, TArray<TVec2<FVec3>>& MassWeightedAngulars, TArray<FReal>& Multipliers)
{
	FVec3 VectorToPoint1 = Constraint.Location[PointIndex] - GetTranslationPGS(InParticles, Constraint.ParticleIndex);
	FVec3 VectorToPoint2 = Constraint.Location[PointIndex] - GetTranslationPGS(InParticles, Constraint.LevelsetIndex);
	Angulars[FlattenedIndex][0] = -FVec3::CrossProduct(VectorToPoint1, Direction);
	Angulars[FlattenedIndex][1] = FVec3::CrossProduct(VectorToPoint2, Direction);
	MassWeightedAngulars[FlattenedIndex][0] = WorldSpaceInvI1 * Angulars[FlattenedIndex][0];
	MassWeightedAngulars[FlattenedIndex][1] = WorldSpaceInvI2 * Angulars[FlattenedIndex][1];
	if (InParticles.InvM(Constraint.ParticleIndex))
	{
		Multipliers[FlattenedIndex] += InParticles.InvM(Constraint.ParticleIndex) + FVec3::DotProduct(Angulars[FlattenedIndex][0], MassWeightedAngulars[FlattenedIndex][0]);
	}
	if (InParticles.InvM(Constraint.LevelsetIndex))
	{
		Multipliers[FlattenedIndex] += InParticles.InvM(Constraint.LevelsetIndex) + FVec3::DotProduct(Angulars[FlattenedIndex][1], MassWeightedAngulars[FlattenedIndex][1]);
	}
}

template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::Solve(T_PARTICLES& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices)
{
	TArray<FReal> Normals;
	TArray<FVec2> Tangents;
	TArray<FReal> Multipliers;
	TArray<TVec2<FVec3>> Angulars, MassWeightedAngulars;
	TArray<TVec2<FVec3>> ConstraintTangents;
	TVec2<TArray<FReal>> TangentMultipliers;
	TVec2<TArray<TVec2<FVec3>>> TangentAngulars, TangentMassWeightedAngulars;

	int32 NumConstraints = 0;
	for (int32 ConstraintIndex : InConstraintIndices)
	{
		const FRigidBodyContactConstraint& Constraint = Constraints[ConstraintIndex];
		NumConstraints += Constraint.Phi.Num();
	}

	Normals.SetNumZeroed(NumConstraints);
	Tangents.SetNumZeroed(NumConstraints);
	Multipliers.SetNumZeroed(NumConstraints);
	Angulars.SetNum(NumConstraints);
	MassWeightedAngulars.SetNum(NumConstraints);
	ConstraintTangents.SetNum(NumConstraints);
	for (int32 Dimension = 0; Dimension < (d - 1); ++Dimension)
	{
		TangentMultipliers[Dimension].SetNumZeroed(NumConstraints);
		TangentAngulars[Dimension].SetNum(NumConstraints);
		TangentMassWeightedAngulars[Dimension].SetNum(NumConstraints);
	}

	int32 FlattenedIndex = 0;
	for (int32 ConstraintIndex : InConstraintIndices)
	{
		FRigidBodyContactConstraint& Constraint = Constraints[ConstraintIndex];
		FMatrix33 WorldSpaceInvI1 = (GetRotationPGS(InParticles, Constraint.ParticleIndex) * FMatrix::Identity).GetTransposed() * InParticles.InvI(Constraint.ParticleIndex) * (GetRotationPGS(InParticles, Constraint.ParticleIndex) * FMatrix::Identity);
		FMatrix33 WorldSpaceInvI2 = (GetRotationPGS(InParticles, Constraint.LevelsetIndex) * FMatrix::Identity).GetTransposed() * InParticles.InvI(Constraint.LevelsetIndex) * (GetRotationPGS(InParticles, Constraint.LevelsetIndex) * FMatrix::Identity);
		for (int32 PointIndex = 0; PointIndex < Constraint.Phi.Num(); ++PointIndex)
		{
			ComputePGSPropeties(InParticles, Constraint, PointIndex, FlattenedIndex, WorldSpaceInvI1, WorldSpaceInvI2, -Constraint.Normal[PointIndex], Angulars, MassWeightedAngulars, Multipliers);
			// Tangents
			{
				T AbsX = FMath::Abs(Constraint.Normal[PointIndex][0]);
				T AbsY = FMath::Abs(Constraint.Normal[PointIndex][1]);
				T AbsZ = FMath::Abs(Constraint.Normal[PointIndex][2]);
				if (AbsX < AbsY)
				{
					if (AbsX < AbsZ)
					{
						ConstraintTangents[FlattenedIndex][0] = FVec3(0, Constraint.Normal[PointIndex][2], -Constraint.Normal[PointIndex][1]);
					}
					else
					{
						ConstraintTangents[FlattenedIndex][0] = FVec3(Constraint.Normal[PointIndex][1], -Constraint.Normal[PointIndex][0], 0);
					}
				}
				else
				{
					if (AbsY < AbsZ)
					{
						ConstraintTangents[FlattenedIndex][0] = FVec3(-Constraint.Normal[PointIndex][2], 0, Constraint.Normal[PointIndex][0]);
					}
					else
					{
						ConstraintTangents[FlattenedIndex][0] = FVec3(Constraint.Normal[PointIndex][1], -Constraint.Normal[PointIndex][0], 0);
					}
				}
			}
			ConstraintTangents[FlattenedIndex][0] = ConstraintTangents[FlattenedIndex][0].GetSafeNormal();
			ConstraintTangents[FlattenedIndex][1] = FVec3::CrossProduct(-ConstraintTangents[FlattenedIndex][0], Constraint.Normal[PointIndex]);
			ComputePGSPropeties(InParticles, Constraint, PointIndex, FlattenedIndex, WorldSpaceInvI1, WorldSpaceInvI2, -ConstraintTangents[FlattenedIndex][0], TangentAngulars[0], TangentMassWeightedAngulars[0], TangentMultipliers[0]);
			ComputePGSPropeties(InParticles, Constraint, PointIndex, FlattenedIndex, WorldSpaceInvI1, WorldSpaceInvI2, -ConstraintTangents[FlattenedIndex][1], TangentAngulars[1], TangentMassWeightedAngulars[1], TangentMultipliers[1]);
			FlattenedIndex++;
		}
	}

	T Residual = 0;
	for (int32 Iteration = 0; Iteration < MaxIterations; ++Iteration)
	{
		Residual = 0;
		FlattenedIndex = 0;
		for (int32 ConstraintIndex : InConstraintIndices)
		{
			FRigidBodyContactConstraint& Constraint = Constraints[ConstraintIndex];
			for (int32 PointIndex = 0; PointIndex < Constraint.Phi.Num(); ++PointIndex)
			{
				T Body1NormalVelocity = FVec3::DotProduct(InParticles.V(Constraint.ParticleIndex), Constraint.Normal[PointIndex]) +
					FVec3::DotProduct(InParticles.W(Constraint.ParticleIndex), Angulars[FlattenedIndex][0]);
				T Body2NormalVelocity = FVec3::DotProduct(InParticles.V(Constraint.LevelsetIndex), -Constraint.Normal[PointIndex]) +
					FVec3::DotProduct(InParticles.W(Constraint.LevelsetIndex), Angulars[FlattenedIndex][1]);
				T RelativeNormalVelocity = Body1NormalVelocity + Body2NormalVelocity + Constraint.Phi[PointIndex] / Dt;
				T NewResidual = FMath::Max(-RelativeNormalVelocity, RelativeNormalVelocity * Normals[FlattenedIndex]);
				if (NewResidual > Residual)
				{
					Residual = NewResidual;
				}
				T NormalDelta = -RelativeNormalVelocity / Multipliers[FlattenedIndex];
				// Update Normals
				T NewNormal = Normals[FlattenedIndex] + NormalDelta;
				if (NewNormal < 0)
				{
					NewNormal = 0;
					NormalDelta = -Normals[FlattenedIndex];
				}
				check(RelativeNormalVelocity < 0 || NormalDelta == 0 || Iteration > 0);
				// Velocity update
				InParticles.V(Constraint.ParticleIndex) += NormalDelta * InParticles.InvM(Constraint.ParticleIndex) * Constraint.Normal[PointIndex];
				InParticles.V(Constraint.LevelsetIndex) += NormalDelta * InParticles.InvM(Constraint.LevelsetIndex) * -Constraint.Normal[PointIndex];
				InParticles.W(Constraint.ParticleIndex) += NormalDelta * MassWeightedAngulars[FlattenedIndex][0];
				InParticles.W(Constraint.LevelsetIndex) += NormalDelta * MassWeightedAngulars[FlattenedIndex][1];
				// Normal update
				Normals[FlattenedIndex] = NewNormal;
				T Friction = MPhysicsMaterials[Constraint.LevelsetIndex]
					? FMath::Max(MPhysicsMaterials[Constraint.ParticleIndex]->Friction, MPhysicsMaterials[Constraint.LevelsetIndex]->Friction)
					: MPhysicsMaterials[Constraint.ParticleIndex]->Friction;
				if (Friction)
				{
					for (int32 Dimension = 0; Dimension < (d - 1); Dimension++)
					{
						T Body1TangentVelocity = FVec3::DotProduct(InParticles.V(Constraint.ParticleIndex), ConstraintTangents[PointIndex][Dimension]) +
							FVec3::DotProduct(InParticles.W(Constraint.ParticleIndex), TangentAngulars[Dimension][FlattenedIndex][0]);
						T Body2TangentVelocity = FVec3::DotProduct(InParticles.V(Constraint.LevelsetIndex), -ConstraintTangents[PointIndex][Dimension]) +
							FVec3::DotProduct(InParticles.W(Constraint.LevelsetIndex), TangentAngulars[Dimension][FlattenedIndex][1]);
						T RelativeTangentVelocity = Body1TangentVelocity + Body2TangentVelocity;
						T TangentDelta = -RelativeTangentVelocity / TangentMultipliers[Dimension][FlattenedIndex];
						T NewTangent = Tangents[FlattenedIndex][Dimension] + TangentDelta;
						if (FMath::Abs(NewTangent) > Friction * NewNormal)
						{
							NewTangent = Friction * NewNormal;
							if (NewTangent < 0)
							{
								NewTangent *= -1;
							}
						}
						// Velocity update
						InParticles.V(Constraint.ParticleIndex) += TangentDelta * InParticles.InvM(Constraint.ParticleIndex) * ConstraintTangents[PointIndex][Dimension];
						InParticles.V(Constraint.LevelsetIndex) += TangentDelta * InParticles.InvM(Constraint.LevelsetIndex) * -ConstraintTangents[PointIndex][Dimension];
						InParticles.W(Constraint.ParticleIndex) += TangentDelta * TangentMassWeightedAngulars[Dimension][FlattenedIndex][0];
						InParticles.W(Constraint.LevelsetIndex) += TangentDelta * TangentMassWeightedAngulars[Dimension][FlattenedIndex][1];
						Tangents[FlattenedIndex][Dimension] = NewTangent;
					}
				}
				FlattenedIndex++;
			}
		}
		UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Solve with Residual %f"), Residual);
		if (Residual < Tolerance)
		{
			break;
		}
	}
}

void FPBDCollisionConstraintPGS::PrintParticles(const FPBDRigidParticles& InParticles, const TArray<int32>& InConstraintIndices)
{
	TSet<int32> ConstraintParticles;
	for (int32 ConstraintIndex : InConstraintIndices)
	{
		const FRigidBodyContactConstraint& Constraint = Constraints[ConstraintIndex];
		if (!ConstraintParticles.Contains(Constraint.ParticleIndex))
		{
			ConstraintParticles.Add(Constraint.ParticleIndex);
		}
		if (!ConstraintParticles.Contains(Constraint.LevelsetIndex))
		{
			ConstraintParticles.Add(Constraint.LevelsetIndex);
		}
	}
	for (const auto& i : ConstraintParticles)
	{
		UE_LOG(LogChaos, Verbose, TEXT("Particle %d has X=(%f, %f, %f) and V=(%f, %f, %f)"), i, InParticles.X(i)[0], InParticles.X(i)[1], InParticles.X(i)[2], InParticles.V(i)[0], InParticles.V(i)[1], InParticles.V(i)[2]);
	}
}

void FPBDCollisionConstraintPGS::PrintConstraints(const FPBDRigidParticles& InParticles, const TArray<int32>& InConstraintIndices)
{
	for (int32 ConstraintIndex : InConstraintIndices)
	{
		const FRigidBodyContactConstraint& Constraint = Constraints[ConstraintIndex];
		UE_LOG(LogChaos, Verbose, TEXT("Constraint between %d and %d has %d contacts"), Constraint.ParticleIndex, Constraint.LevelsetIndex, Constraint.Phi.Num());
		for (int32 j = 0; j < Constraint.Phi.Num(); ++j)
		{
			UE_LOG(LogChaos, Verbose, TEXT("Constraint has loction (%f, %f, %f) and phi %f"), Constraint.Location[j][0], Constraint.Location[j][1], Constraint.Location[j][2], Constraint.Phi[j]);
		}
	}
}

void FindPointsOnHull(FRigidBodyContactConstraintPGS& Constraint,
	const FVec3& X0, const FVec3& X1, const FVec3& X2,
	const TPlane<FReal, 3>& SplitPlane, const TArray<int32>& Indices, TSet<int32>& HullPoints)
{
	int32 MaxD = 0; //This doesn't need to be initialized but we need to avoid the compiler warning
	FReal MaxDistance = 0;
	for (int32 i = 0; i < Indices.Num(); ++i)
	{
		FReal Distance = SplitPlane.SignedDistance(Constraint.Location[Indices[i]]);
		check(Distance >= 0);
		if (Distance > MaxDistance)
		{
			MaxDistance = Distance;
			MaxD = Indices[i];
		}
	}
	if (MaxDistance > 0)
	{
		if (!HullPoints.Contains(MaxD))
		{
			HullPoints.Add(MaxD);
		}
		const FVec3& NewX = Constraint.Location[MaxD];
		const FVec3 V1 = (NewX - X0).GetSafeNormal();
		const FVec3 V2 = (NewX - X1).GetSafeNormal();
		const FVec3 V3 = (NewX - X2).GetSafeNormal();
		FVec3 Normal1 = FVec3::CrossProduct(V1, V2).GetSafeNormal();
		if (FVec3::DotProduct(Normal1, X2 - X0) > 0)
		{
			Normal1 *= -1;
		}
		FVec3 Normal2 = FVec3::CrossProduct(V1, V3).GetSafeNormal();
		if (FVec3::DotProduct(Normal2, X1 - X0) > 0)
		{
			Normal2 *= -1;
		}
		FVec3 Normal3 = FVec3::CrossProduct(V2, V3).GetSafeNormal();
		if (FVec3::DotProduct(Normal3, X0 - X1) > 0)
		{
			Normal3 *= -1;
		}
		TPlane<FReal, 3> NewPlane1(NewX, Normal1);
		TPlane<FReal, 3> NewPlane2(NewX, Normal2);
		TPlane<FReal, 3> NewPlane3(NewX, Normal3);
		TArray<int32> NewIndices1;
		TArray<int32> NewIndices2;
		TArray<int32> NewIndices3;
		for (int32 i = 0; i < Indices.Num(); ++i)
		{
			if (MaxD == Indices[i])
			{
				continue;
			}
			if (NewPlane1.SignedDistance(Constraint.Location[Indices[i]]) > 0)
			{
				NewIndices1.Add(Indices[i]);
			}
			if (NewPlane2.SignedDistance(Constraint.Location[Indices[i]]) > 0)
			{
				NewIndices2.Add(Indices[i]);
			}
			if (NewPlane3.SignedDistance(Constraint.Location[Indices[i]]) > 0)
			{
				NewIndices3.Add(Indices[i]);
			}
		}
		FindPointsOnHull(Constraint, X0, X1, NewX, NewPlane1, NewIndices1, HullPoints);
		FindPointsOnHull(Constraint, X0, X2, NewX, NewPlane2, NewIndices2, HullPoints);
		FindPointsOnHull(Constraint, X1, X2, NewX, NewPlane3, NewIndices3, HullPoints);
	}
}

void RemovePointsInsideHull(FRigidBodyContactConstraintPGS& Constraint)
{
	if (Constraint.Location.Num() <= 2)
	{
		return;
	}
	// Find max and min x points
	int32 MinX = 0;
	int32 MaxX = 0;
	int32 MinY = 0;
	int32 MaxY = 0;
	int32 Index1 = 0;
	int32 Index2 = 0;
	for (int32 i = 1; i < Constraint.Location.Num(); ++i)
	{
		if (Constraint.Location[i][0] > Constraint.Location[MaxX][0])
		{
			MaxX = i;
		}
		if (Constraint.Location[i][0] < Constraint.Location[MinX][0])
		{
			MinX = i;
		}
		if (Constraint.Location[i][1] > Constraint.Location[MaxY][1])
		{
			MaxY = i;
		}
		if (Constraint.Location[i][1] < Constraint.Location[MinY][1])
		{
			MinY = i;
		}
	}
	if (MaxX == MinX && MinY == MaxY && MinX == MinY)
	{
		// Points are colinear so need to sort but for now do nothing
		return;
	}
	// Find max distance
	FReal DistanceY = (Constraint.Location[MaxY] - Constraint.Location[MinY]).Size();
	FReal DistanceX = (Constraint.Location[MaxX] - Constraint.Location[MinX]).Size();
	if (DistanceX > DistanceY)
	{
		Index1 = MaxX;
		Index2 = MinX;
	}
	else
	{
		Index1 = MaxY;
		Index2 = MinY;
	}
	TSet<int32> HullPoints;
	HullPoints.Add(Index1);
	HullPoints.Add(Index2);
	const FVec3& X1 = Constraint.Location[Index1];
	const FVec3& X2 = Constraint.Location[Index2];
	FReal MaxDist = 0;
	int32 MaxD = -1;
	for (int32 i = 0; i < Constraint.Location.Num(); ++i)
	{
		if (i == Index1 || i == Index2)
		{
			continue;
		}
		const FVec3& X0 = Constraint.Location[i];
		FReal Distance = FVec3::CrossProduct(X0 - X1, X0 - X2).Size() / (X2 - X1).Size();
		if (Distance > MaxDist)
		{
			MaxDist = Distance;
			MaxD = i;
		}
	}
	if (MaxD != -1)
	{
		HullPoints.Add(MaxD);
		const FVec3& X0 = Constraint.Location[MaxD];
		FVec3 Normal = FVec3::CrossProduct((X0 - X1).GetSafeNormal(), (X0 - X2).GetSafeNormal());
		TPlane<FReal, 3> SplitPlane(X0, Normal);
		TPlane<FReal, 3> SplitPlaneNeg(X0, -Normal);
		TArray<int32> Left;
		TArray<int32> Right;
		for (int32 i = 0; i < Constraint.Location.Num(); ++i)
		{
			if (i == Index1 || i == Index2 || i == MaxD)
			{
				continue;
			}
			if (SplitPlane.SignedDistance(Constraint.Location[i]) >= 0)
			{
				Left.Add(i);
			}
			else
			{
				Right.Add(i);
			}
		}
		FindPointsOnHull(Constraint, X0, X1, X2, SplitPlane, Left, HullPoints);
		FindPointsOnHull(Constraint, X0, X1, X2, SplitPlaneNeg, Right, HullPoints);
	}
	TArray<FVec3> Locations;
	TArray<FVec3> Normals;
	TArray<FReal> Distances;
	for (const auto& Index : HullPoints)
	{
		Locations.Add(Constraint.Location[Index]);
		Normals.Add(Constraint.Normal[Index]);
		Distances.Add(Constraint.Phi[Index]);
	}
	Constraint.Location = Locations;
	Constraint.Normal = Normals;
	Constraint.Phi = Distances;
}

void FPBDCollisionConstraintPGS::Apply(FPBDRigidParticles& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices)
{
	PhysicsParallelFor(InConstraintIndices.Num(), [&](int32 ConstraintIndex) {
		FRigidBodyContactConstraint& Constraint = Constraints[ConstraintIndex];
		if (InParticles.Sleeping(Constraint.ParticleIndex))
		{
			check(InParticles.Sleeping(Constraint.LevelsetIndex) || InParticles.InvM(Constraint.LevelsetIndex) == 0);
			return;
		}
		// @todo(mlentine): This is a really coarse approximation. Prune points that we know are not relevant.
		const FReal Threshold = (InParticles.V(Constraint.ParticleIndex).Size() - InParticles.V(Constraint.LevelsetIndex).Size()) * Dt;
		// Guessing Max is a decent approximation as with restitution 0 the difference is X between predicted and actual is Vdt
		const FReal Thickness = MThickness + FMath::Max(InParticles.V(Constraint.ParticleIndex).Size(), InParticles.V(Constraint.LevelsetIndex).Size()) * Dt;
		const_cast<FPBDCollisionConstraintPGS*>(this)->UpdateConstraint(static_cast<TRigidParticles<FReal, 3>&>(InParticles), Thickness + Threshold, Constraint);
		// @todo(mlentine): Prune contact points based on convex hull
		RemovePointsInsideHull(Constraint);
	});
	PrintParticles(InParticles, InConstraintIndices);
	PrintConstraints(InParticles, InConstraintIndices);
	Solve(static_cast<TRigidParticles<FReal, 3>&>(InParticles), Dt, InConstraintIndices);
	PrintParticles(InParticles, InConstraintIndices);
}

void FPBDCollisionConstraintPGS::ApplyPushOut(FPBDRigidParticles& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices)
{
	PhysicsParallelFor(InConstraintIndices.Num(), [&](int32 ConstraintIndex) {
		FRigidBodyContactConstraint& Constraint = Constraints[ConstraintIndex];
		if (InParticles.Sleeping(Constraint.ParticleIndex))
		{
			check(InParticles.Sleeping(Constraint.LevelsetIndex) || InParticles.InvM(Constraint.LevelsetIndex) == 0);
			return;
		}
		const_cast<FPBDCollisionConstraintPGS*>(this)->UpdateConstraint(InParticles, MThickness, Constraint);
		// @todo(mlentine): Prune contact points based on convex hull
	});

	TArray<bool> Saved;
	TArray<FVec3> SavedV, SavedW;
	Saved.SetNum(InParticles.Size());
	SavedV.SetNum(InParticles.Size());
	SavedW.SetNum(InParticles.Size());
	auto SaveParticle = [&](int32 ParticleIndex)
	{
		if (!Saved[ParticleIndex])
		{
			SavedV[ParticleIndex] = InParticles.V(ParticleIndex);
			SavedW[ParticleIndex] = InParticles.W(ParticleIndex);
			InParticles.V(ParticleIndex) = FVec3(0);
			InParticles.W(ParticleIndex) = FVec3(0);
			Saved[ParticleIndex] = true;
		}
	};
	auto RestoreParticle = [&](int32 ParticleIndex)
	{
		if (Saved[ParticleIndex])
		{
			if (InParticles.InvM(ParticleIndex))
			{
				InParticles.P(ParticleIndex) += InParticles.V(ParticleIndex) * Dt;
				InParticles.Q(ParticleIndex) += FRotation3(InParticles.W(ParticleIndex), 0.f) * InParticles.Q(ParticleIndex) * Dt * FReal(0.5);
				InParticles.Q(ParticleIndex).Normalize();
			}
			InParticles.V(ParticleIndex) = SavedV[ParticleIndex];
			InParticles.W(ParticleIndex) = SavedW[ParticleIndex];
			Saved[ParticleIndex] = false;
		}
	};

	PhysicsParallelFor(InConstraintIndices.Num(), [&](int32 ConstraintIndex) {
		SaveParticle(Constraints[ConstraintIndex].ParticleIndex);
		SaveParticle(Constraints[ConstraintIndex].LevelsetIndex);
	});
	PrintParticles(InParticles, InConstraintIndices);
	PrintConstraints(InParticles, InConstraintIndices);

	Solve(InParticles, Dt, InConstraintIndices);
	
	PrintParticles(InParticles, InConstraintIndices);
	PhysicsParallelFor(InConstraintIndices.Num(), [&](int32 ConstraintIndex) {
		RestoreParticle(Constraints[ConstraintIndex].ParticleIndex);
		RestoreParticle(Constraints[ConstraintIndex].LevelsetIndex);
	});
}

bool FPBDCollisionConstraintPGS::NearestPoint(TArray<Pair<FVec3, FVec3>>& Points, FVec3& Direction)
{
	check(Points.Num() > 1 && Points.Num() <= 4);
	if (Points.Num() == 2)
	{
		TPlane<FReal, 3> LocalPlane(Points[1].First, Points[0].First - Points[1].First);
		FVec3 Normal;
		const auto& Phi = LocalPlane.PhiWithNormal(FVec3(0), Normal);
		if ((FVec3::DotProduct(-Points[1].First, Normal.GetSafeNormal()) - Points[1].First.Size()) < SMALL_NUMBER)
		{
			FReal Alpha = Points[0].First.Size() / (Points[1].First - Points[0].First).Size();
			return true;
		}
		if (Phi > 0)
		{
			check(Points.Num() == 2);
			Direction = FVec3::CrossProduct(FVec3::CrossProduct(Normal, -Points[1].First), Normal);
		}
		else
		{
			Direction = -Points[1].First;
			Points.RemoveAtSwap(0);
			check(Points.Num() == 1);
		}
		check(Points.Num() > 1 && Points.Num() < 4);
		return false;
	}
	if (Points.Num() == 3)
	{
		FVec3 TriangleNormal = FVec3::CrossProduct(Points[0].First - Points[2].First, Points[0].First - Points[1].First);
		TPlane<FReal, 3> LocalPlane1(Points[2].First, FVec3::CrossProduct(Points[0].First - Points[2].First, TriangleNormal));
		TPlane<FReal, 3> LocalPlane2(Points[2].First, FVec3::CrossProduct(Points[1].First - Points[2].First, TriangleNormal));
		FVec3 Normal;
		FReal Phi = LocalPlane1.PhiWithNormal(FVec3(0), Normal);
		if (Phi > 0)
		{
			FVec3 Delta = Points[0].First - Points[2].First;
			if (FVec3::DotProduct(-Points[2].First, Delta) > 0)
			{
				Direction = FVec3::CrossProduct(FVec3::CrossProduct(Delta, -Points[2].First), Delta);
				Points.RemoveAtSwap(1);
				check(Points.Num() == 2);
			}
			else
			{
				Delta = Points[1].First - Points[2].First;
				if (FVec3::DotProduct(-Points[2].First, Delta) > 0)
				{
					Direction = FVec3::CrossProduct(FVec3::CrossProduct(Delta, -Points[2].First), Delta);
					Points.RemoveAtSwap(0);
					check(Points.Num() == 2);
				}
				else
				{
					Direction = -Points[2].First;
					Points = {Points[2]};
					check(Points.Num() == 1);
				}
			}
		}
		else
		{
			Phi = LocalPlane2.PhiWithNormal(FVec3(0), Normal);
			if (Phi > 0)
			{
				FVec3 Delta = Points[1].First - Points[2].First;
				if (FVec3::DotProduct(-Points[2].First, Delta) > 0)
				{
					Direction = FVec3::CrossProduct(FVec3::CrossProduct(Delta, -Points[2].First), Delta);
					Points.RemoveAtSwap(0);
					check(Points.Num() == 2);
				}
				else
				{
					Direction = -Points[2].First;
					Points = {Points[2]};
					check(Points.Num() == 1);
				}
			}
			else
			{
				const auto DotResult = FVec3::DotProduct(TriangleNormal, -Points[2].First);
				// We are inside the triangle
				if (DotResult < SMALL_NUMBER)
				{
					FVec3 Bary;
					FVec3 P10 = Points[1].First - Points[0].First;
					FVec3 P20 = Points[2].First - Points[0].First;
					FVec3 PP0 = -Points[0].First;
					FReal Size10 = P10.SizeSquared();
					FReal Size20 = P20.SizeSquared();
					FReal ProjSides = FVec3::DotProduct(P10, P20);
					FReal ProjP1 = FVec3::DotProduct(PP0, P10);
					FReal ProjP2 = FVec3::DotProduct(PP0, P20);
					FReal Denom = Size10 * Size20 - ProjSides * ProjSides;
					Bary.Y = (Size20 * ProjP1 - ProjSides * ProjP2) / Denom;
					Bary.Z = (Size10 * ProjP2 - ProjSides * ProjP1) / Denom;
					Bary.X = 1.0f - Bary.Z - Bary.Y;
					return true;
				}
				if (DotResult > 0)
				{
					Direction = TriangleNormal;
				}
				else
				{
					Direction = -TriangleNormal;
					Points.Swap(0, 1);
					check(Points.Num() == 3);
				}
			}
		}
		check(Points.Num() > 0 && Points.Num() < 4);
		return false;
	}
	if (Points.Num() == 4)
	{
		FVec3 TriangleNormal = FVec3::CrossProduct(Points[1].First - Points[3].First, Points[1].First - Points[2].First);
		if (FVec3::DotProduct(TriangleNormal, Points[0].First - Points[3].First) > 0)
		{
			TriangleNormal *= -1;
		}
		FReal DotResult = FVec3::DotProduct(TriangleNormal, -Points[3].First);
		if (DotResult > 0)
		{
			Points = {Points[1], Points[2], Points[3]};
			check(Points.Num() == 3);
			return NearestPoint(Points, Direction);
		}
		TriangleNormal = FVec3::CrossProduct(Points[2].First - Points[0].First, Points[2].First - Points[3].First);
		if (FVec3::DotProduct(TriangleNormal, Points[1].First - Points[3].First) > 0)
		{
			TriangleNormal *= -1;
		}
		DotResult = FVec3::DotProduct(TriangleNormal, -Points[3].First);
		if (DotResult > 0)
		{
			Points = {Points[0], Points[2], Points[3]};
			check(Points.Num() == 3);
			return NearestPoint(Points, Direction);
		}
		TriangleNormal = FVec3::CrossProduct(Points[3].First - Points[1].First, Points[3].First - Points[0].First);
		if (FVec3::DotProduct(TriangleNormal, Points[2].First - Points[3].First) > 0)
		{
			TriangleNormal *= -1;
		}
		DotResult = FVec3::DotProduct(TriangleNormal, -Points[3].First);
		if (DotResult > 0)
		{
			Points = {Points[0], Points[1], Points[3]};
			check(Points.Num() == 3);
			return NearestPoint(Points, Direction);
		}
		TVector<FReal, 4> Bary;
		FVec3 PP0 = -Points[0].First;
		FVec3 PP1 = -Points[1].First;
		FVec3 P10 = Points[1].First - Points[0].First;
		FVec3 P20 = Points[2].First - Points[0].First;
		FVec3 P30 = Points[3].First - Points[0].First;
		FVec3 P21 = Points[2].First - Points[1].First;
		FVec3 P31 = Points[3].First - Points[1].First;
		Bary[0] = FVec3::DotProduct(PP1, FVec3::CrossProduct(P31, P21));
		Bary[1] = FVec3::DotProduct(PP0, FVec3::CrossProduct(P20, P30));
		Bary[2] = FVec3::DotProduct(PP0, FVec3::CrossProduct(P30, P10));
		Bary[3] = FVec3::DotProduct(PP0, FVec3::CrossProduct(P10, P20));
		FReal Denom = FVec3::DotProduct(P10, FVec3::CrossProduct(P20, P30));
		return true;
	}
	check(Points.Num() > 1 && Points.Num() < 4);
	return false;
}

void UpdateLevelsetConstraintHelperCCD(const TRigidParticles<FReal, 3>& InParticles, const int32 j, const FRigidTransform3& LocalToWorld1, const FRigidTransform3& LocalToWorld2, const T Thickness, FRigidBodyContactConstraintPGS& Constraint)
{
	if(InParticles.CollisionParticles(Constraint.ParticleIndex))
	{
		const FRigidTransform3 PreviousLocalToWorld1 = GetTransformPGS(InParticles, Constraint.ParticleIndex);
		FVec3 WorldSpacePointStart = PreviousLocalToWorld1.TransformPosition(InParticles.CollisionParticles(Constraint.ParticleIndex)->X(j));
		FVec3 WorldSpacePointEnd = LocalToWorld1.TransformPosition(InParticles.CollisionParticles(Constraint.ParticleIndex)->X(j));
		FVec3 Body2SpacePointStart = LocalToWorld2.InverseTransformPosition(WorldSpacePointStart);
		FVec3 Body2SpacePointEnd = LocalToWorld2.InverseTransformPosition(WorldSpacePointEnd);
		Pair<FVec3, bool> PointPair = InParticles.Geometry(Constraint.LevelsetIndex)->FindClosestIntersection(Body2SpacePointStart, Body2SpacePointEnd, Thickness);
		if (PointPair.Second)
		{
			const FVec3 WorldSpaceDelta = WorldSpacePointEnd - FVec3(LocalToWorld2.TransformPosition(PointPair.First));
			Constraint.Phi.Add(-WorldSpaceDelta.Size());
			Constraint.Normal.Add(LocalToWorld2.TransformVector(InParticles.Geometry(Constraint.LevelsetIndex)->Normal(PointPair.First)));
			// @todo(mlentine): Should we be using the actual collision point or that point evolved to the current time step?
			Constraint.Location.Add(WorldSpacePointEnd);
		}
	}
}

void UpdateLevelsetConstraintHelper(const TRigidParticles<FReal, 3>& InParticles, const int32 j, const FRigidTransform3& LocalToWorld1, const FRigidTransform3& LocalToWorld2, const T Thickness, FRigidBodyContactConstraintPGS& Constraint)
{
	if(InParticles.CollisionParticles(Constraint.ParticleIndex))
	{
		FVec3 WorldSpacePoint = LocalToWorld1.TransformPosition(InParticles.CollisionParticles(Constraint.ParticleIndex)->X(j));
		FVec3 Body2SpacePoint = LocalToWorld2.InverseTransformPosition(WorldSpacePoint);
		FVec3 LocalNormal;
		FReal LocalPhi = InParticles.Geometry(Constraint.LevelsetIndex)->PhiWithNormal(Body2SpacePoint, LocalNormal);
		if (LocalPhi < Thickness)
		{
			Constraint.Phi.Add(LocalPhi);
			Constraint.Normal.Add(LocalToWorld2.TransformVector(LocalNormal));
			Constraint.Location.Add(WorldSpacePoint);
		}
	}
}

template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::UpdateLevelsetConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint)
{
	Constraint.Normal.Reset();
	Constraint.Phi.Reset();
	Constraint.Location.Reset();
	const FRigidTransform3 LocalToWorld1 = GetTransformPGS(InParticles, Constraint.ParticleIndex);
	const FRigidTransform3 LocalToWorld2 = GetTransformPGS(InParticles, Constraint.LevelsetIndex);
	if (InParticles.Geometry(Constraint.LevelsetIndex)->HasBoundingBox())
	{
		FAABB3 ImplicitBox = InParticles.Geometry(Constraint.LevelsetIndex)->BoundingBox().TransformedBox(LocalToWorld2 * LocalToWorld1.Inverse());
		if (InParticles.CollisionParticles(Constraint.ParticleIndex))
		{
			TArray<int32> PotentialParticles = InParticles.CollisionParticles(Constraint.ParticleIndex)->FindAllIntersections(ImplicitBox);
			for (int32 j = 0; j < PotentialParticles.Num(); ++j)
			{
				if (bUseCCD)
				{
					UpdateLevelsetConstraintHelperCCD(InParticles, PotentialParticles[j], LocalToWorld1, LocalToWorld2, Thickness, Constraint);
				}
				else
				{
					UpdateLevelsetConstraintHelper(InParticles, PotentialParticles[j], LocalToWorld1, LocalToWorld2, Thickness, Constraint);
				}
			}
		}
	}
	else
	{
		if (InParticles.CollisionParticles(Constraint.ParticleIndex))
		{
			for (uint32 j = 0; j < InParticles.CollisionParticles(Constraint.ParticleIndex)->Size(); ++j)
			{
				UpdateLevelsetConstraintHelper(InParticles, j, LocalToWorld1, LocalToWorld2, Thickness, Constraint);
			}
		}
	}
}

template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::UpdateLevelsetConstraintGJK(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint)
{
	static int32 MaxIterationsGJK = 100;
	Constraint.Normal.Reset();
	Constraint.Phi.Reset();
	Constraint.Location.Reset();
	const FRigidTransform3 LocalToWorld1 = GetTransformPGS(InParticles, Constraint.ParticleIndex);
	const FRigidTransform3 LocalToWorld2 = GetTransformPGS(InParticles, Constraint.LevelsetIndex);
	FVec3 Direction = LocalToWorld1.GetTranslation() - LocalToWorld2.GetTranslation();
	FVec3 SupportA = LocalToWorld1.TransformPosition(InParticles.Geometry(Constraint.ParticleIndex)->Support(LocalToWorld1.InverseTransformVector(-Direction), Thickness));
	FVec3 SupportB = LocalToWorld2.TransformPosition(InParticles.Geometry(Constraint.LevelsetIndex)->Support(LocalToWorld2.InverseTransformVector(Direction), Thickness));
	FVec3 Point = SupportB - SupportA;
	TArray<Pair<FVec3, FVec3>> Points = {MakePair(Point, SupportA)};
	Direction = -Point;
	for (int32 i = 0; i < MaxIterationsGJK; ++i)
	{
		SupportA = LocalToWorld1.TransformPosition(InParticles.Geometry(Constraint.ParticleIndex)->Support(LocalToWorld1.InverseTransformVector(-Direction), Thickness));
		SupportB = LocalToWorld2.TransformPosition(InParticles.Geometry(Constraint.LevelsetIndex)->Support(LocalToWorld2.InverseTransformVector(Direction), Thickness));
		Point = SupportB - SupportA;
		if (FVec3::DotProduct(Point, Direction) < 0)
		{
			break;
		}
		Points.Add(MakePair(Point, SupportA));
		FVec3 ClosestPoint;
		if (NearestPoint(Points, Direction))
		{
			for (const auto& SinglePoint : Points)
			{
				FVec3 Body1Location = LocalToWorld1.InverseTransformPosition(SinglePoint.Second);
				FVec3 Normal;
				FReal Phi = InParticles.Geometry(Constraint.ParticleIndex)->PhiWithNormal(Body1Location, Normal);
				Normal = LocalToWorld1.TransformVector(Normal);
				FVec3 SurfacePoint = SinglePoint.Second - Phi * Normal;
				Constraint.Location.Add(SurfacePoint);
				FVec3 Body2Location = LocalToWorld2.InverseTransformPosition(SurfacePoint);
				Constraint.Phi.Add(InParticles.Geometry(Constraint.LevelsetIndex)->PhiWithNormal(Body2Location, Normal));
				Constraint.Normal.Add(LocalToWorld2.TransformVector(Normal));
			}
			break;
		}
	}
}

template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::UpdateBoxConstraint(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint)
{
	Constraint.Normal.Reset();
	Constraint.Phi.Reset();
	Constraint.Location.Reset();
	const FRigidTransform3 Box1Transform = GetTransformPGS(InParticles, Constraint.ParticleIndex);
	const FRigidTransform3 Box2Transform = GetTransformPGS(InParticles, Constraint.LevelsetIndex);
	const auto& Box1 = *InParticles.Geometry(Constraint.ParticleIndex)->template GetObject<TBox<T, d>>();
	const auto& Box2 = *InParticles.Geometry(Constraint.LevelsetIndex)->template GetObject<TBox<T, d>>();
	auto Box2SpaceBox1 = Box1.TransformedBox(Box1Transform * Box2Transform.Inverse());
	auto Box1SpaceBox2 = Box2.TransformedBox(Box2Transform * Box1Transform.Inverse());
	Box2SpaceBox1.Thicken(Thickness);
	Box1SpaceBox2.Thicken(Thickness);
	if (Box1SpaceBox2.Intersects(Box1) && Box2SpaceBox1.Intersects(Box2))
	{
		const FVec3 Box1Center = (Box1Transform * Box2Transform.Inverse()).TransformPosition(Box1.Center());
		if (Box2.SignedDistance(Box1Center) < 0)
		{
			TSphere<FReal, 3> Sphere1(Box1Transform.TransformPosition(Box1.Center()), Box1.Extents().Min() / 2);
			TSphere<FReal, 3> Sphere2(Box2Transform.TransformPosition(Box2.Center()), Box2.Extents().Min() / 2);
			const FVec3 Direction = Sphere1.GetCenter() - Sphere2.GetCenter();
			FReal Size = Direction.Size();
			if (Size < (Sphere1.GetRadius() + Sphere2.GetRadius()))
			{
				FVec3 Normal = Size > SMALL_NUMBER ? Direction / Size : FVec3(0, 0, 1);
				Constraint.Normal.Add(Normal);
				Constraint.Phi.Add(Size - (Sphere1.GetRadius() + Sphere2.GetRadius()));
				Constraint.Location.Add(Sphere1.GetCenter() - Sphere1.GetRadius() * Normal);
			}
		}
		if (!Constraint.Phi.Num())
		{
			//UpdateLevelsetConstraintGJK(InParticles, Thickness, Constraint);
			//check(Constraint.Phi < MThickness);
			// For now revert to doing all points vs lsv check until we can figure out a good way to get the deepest point without needing this
			UpdateLevelsetConstraint(InParticles, Thickness, Constraint);
		}
	}
}

template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::UpdateBoxPlaneConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint)
{
	Constraint.Normal.Reset();
	Constraint.Phi.Reset();
	Constraint.Location.Reset();
	const FRigidTransform3 BoxTransform = GetTransformPGS(InParticles, Constraint.ParticleIndex);
	const FRigidTransform3 PlaneTransform = GetTransformPGS(InParticles, Constraint.LevelsetIndex);
	const auto& ObjectBox = *InParticles.Geometry(Constraint.ParticleIndex)->template GetObject<TBox<T, d>>();
	const auto& ObjectPlane = *InParticles.Geometry(Constraint.LevelsetIndex)->template GetObject<TPlane<FReal, 3>>();
	const FRigidTransform3 BoxToPlaneTransform(BoxTransform * PlaneTransform.Inverse());
	const FVec3 Extents = ObjectBox.Extents();
	TArray<FVec3> Corners;
	Corners.Add(BoxToPlaneTransform.TransformPosition(ObjectBox.Max()));
	Corners.Add(BoxToPlaneTransform.TransformPosition(ObjectBox.Min()));
	for (int32 j = 0; j < d; ++j)
	{
		Corners.Add(BoxToPlaneTransform.TransformPosition(ObjectBox.Min() + FVec3::AxisVector(j) * Extents));
		Corners.Add(BoxToPlaneTransform.TransformPosition(ObjectBox.Max() - FVec3::AxisVector(j) * Extents));
	}
	TArray<FVec3> PotentialConstraints;
	for (int32 i = 0; i < Corners.Num(); ++i)
	{
		FVec3 Normal;
		const FReal NewPhi = ObjectPlane.PhiWithNormal(Corners[i], Normal);
		if (NewPhi < Thickness)
		{
			Constraint.Phi.Add(NewPhi);
			Constraint.Normal.Add(PlaneTransform.TransformVector(Normal));
			Constraint.Location.Add(PlaneTransform.TransformPosition(Corners[i]));
		}
	}
}

template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::UpdateSphereConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint)
{
	Constraint.Normal.Reset();
	Constraint.Phi.Reset();
	Constraint.Location.Reset();
	const FRigidTransform3 Sphere1Transform = GetTransformPGS(InParticles, Constraint.ParticleIndex);
	const FRigidTransform3 Sphere2Transform = GetTransformPGS(InParticles, Constraint.LevelsetIndex);
	const auto& Sphere1 = *InParticles.Geometry(Constraint.ParticleIndex)->template GetObject<TSphere<FReal, 3>>();
	const auto& Sphere2 = *InParticles.Geometry(Constraint.LevelsetIndex)->template GetObject<TSphere<FReal, 3>>();
	const FVec3 Center1 = Sphere1Transform.TransformPosition(Sphere1.GetCenter());
	const FVec3 Center2 = Sphere2Transform.TransformPosition(Sphere2.GetCenter());
	const FVec3 Direction = Center1 - Center2;
	const FReal Size = Direction.Size();
	if (Size < (Sphere1.GetRadius() + Sphere2.GetRadius() + Thickness))
	{
		FVec3 Normal = Size > SMALL_NUMBER ? Direction / Size : FVec3(0, 0, 1);
		Constraint.Normal.Add(Normal);
		Constraint.Phi.Add(Size - (Sphere1.GetRadius() + Sphere2.GetRadius()));
		Constraint.Location.Add(Center1 - Sphere1.GetRadius() * Normal);
	}
}

template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::UpdateSpherePlaneConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint)
{
	Constraint.Normal.Reset();
	Constraint.Phi.Reset();
	Constraint.Location.Reset();
	const FRigidTransform3 SphereTransform = GetTransformPGS(InParticles, Constraint.ParticleIndex);
	const FRigidTransform3 PlaneTransform = GetTransformPGS(InParticles, Constraint.LevelsetIndex);
	const auto& ObjectSphere = *InParticles.Geometry(Constraint.ParticleIndex)->template GetObject<TSphere<FReal, 3>>();
	const auto& ObjectPlane = *InParticles.Geometry(Constraint.LevelsetIndex)->template GetObject<TPlane<FReal, 3>>();
	const FRigidTransform3 SphereToPlaneTransform(PlaneTransform.Inverse() * SphereTransform);
	const FVec3 SphereCenter = SphereToPlaneTransform.TransformPosition(ObjectSphere.GetCenter());
	Constraint.Normal.SetNum(1);
	Constraint.Phi.Add(ObjectPlane.PhiWithNormal(SphereCenter, Constraint.Normal[0]));
	Constraint.Phi[0] -= ObjectSphere.GetRadius();
	Constraint.Location.Add(SphereCenter - Constraint.Normal[0] * ObjectSphere.GetRadius());
}

template<class T, int d>
template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::UpdateSphereBoxConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint)
{
	Constraint.Normal.Reset();
	Constraint.Phi.Reset();
	Constraint.Location.Reset();
	const FRigidTransform3 SphereTransform = GetTransformPGS(InParticles, Constraint.ParticleIndex);
	const FRigidTransform3 BoxTransform = GetTransformPGS(InParticles, Constraint.LevelsetIndex);
	const auto& ObjectSphere = *InParticles.Geometry(Constraint.ParticleIndex)->template GetObject<TSphere<FReal, 3>>();
	const auto& ObjectBox = *InParticles.Geometry(Constraint.LevelsetIndex)->template GetObject<TBox<T, d>>();
	const FRigidTransform3 SphereToBoxTransform(SphereTransform * BoxTransform.Inverse());
	const FVec3 SphereCenter = SphereToBoxTransform.TransformPosition(ObjectSphere.GetCenter());
	Constraint.Normal.SetNum(1);
	Constraint.Phi.Add(ObjectBox.PhiWithNormal(SphereCenter, Constraint.Normal[0]));
	Constraint.Phi[0] -= ObjectSphere.GetRadius();
	Constraint.Location.Add(SphereCenter - Constraint.Normal[0] * ObjectSphere.GetRadius());
}

typename FPBDCollisionConstraintPGS::FRigidBodyContactConstraint FPBDCollisionConstraintPGS::ComputeLevelsetConstraint(const FPBDRigidParticles& InParticles, int32 ParticleIndex, int32 LevelsetIndex, const FReal Thickness)
{
	if (!InParticles.CollisionParticlesSize(ParticleIndex))
	{
		int32 TmpIndex = ParticleIndex;
		ParticleIndex = LevelsetIndex;
		LevelsetIndex = TmpIndex;
	}
	// Find Deepest Point
	FRigidBodyContactConstraint Constraint;
	Constraint.ParticleIndex = ParticleIndex;
	Constraint.LevelsetIndex = LevelsetIndex;
	return Constraint;
}

typename FPBDCollisionConstraintPGS::FRigidBodyContactConstraint FPBDCollisionConstraintPGS::ComputeLevelsetConstraintGJK(const FPBDRigidParticles& InParticles, int32 ParticleIndex, int32 LevelsetIndex, const FReal Thickness)
{
	FRigidBodyContactConstraint Constraint;
	Constraint.ParticleIndex = ParticleIndex;
	Constraint.LevelsetIndex = LevelsetIndex;
	return Constraint;
}

typename FPBDCollisionConstraintPGS::FRigidBodyContactConstraint FPBDCollisionConstraintPGS::ComputeBoxConstraint(const FPBDRigidParticles& InParticles, int32 Box1Index, int32 Box2Index, const FReal Thickness)
{
	FRigidBodyContactConstraint Constraint;
	Constraint.ParticleIndex = Box1Index;
	Constraint.LevelsetIndex = Box2Index;
	return Constraint;
}

typename FPBDCollisionConstraintPGS::FRigidBodyContactConstraint FPBDCollisionConstraintPGS::ComputeBoxPlaneConstraint(const FPBDRigidParticles& InParticles, int32 BoxIndex, int32 PlaneIndex, const FReal Thickness)
{
	FRigidBodyContactConstraint Constraint;
	Constraint.ParticleIndex = BoxIndex;
	Constraint.LevelsetIndex = PlaneIndex;
	return Constraint;
}

typename FPBDCollisionConstraintPGS::FRigidBodyContactConstraint FPBDCollisionConstraintPGS::ComputeSphereConstraint(const FPBDRigidParticles& InParticles, int32 Sphere1Index, int32 Sphere2Index, const FReal Thickness)
{
	FRigidBodyContactConstraint Constraint;
	Constraint.ParticleIndex = Sphere1Index;
	Constraint.LevelsetIndex = Sphere2Index;
	return Constraint;
}

typename FPBDCollisionConstraintPGS::FRigidBodyContactConstraint FPBDCollisionConstraintPGS::ComputeSpherePlaneConstraint(const FPBDRigidParticles& InParticles, int32 SphereIndex, int32 PlaneIndex, const FReal Thickness)
{
	FRigidBodyContactConstraint Constraint;
	Constraint.ParticleIndex = SphereIndex;
	Constraint.LevelsetIndex = PlaneIndex;
	return Constraint;
}

typename FPBDCollisionConstraintPGS::FRigidBodyContactConstraint FPBDCollisionConstraintPGS::ComputeSphereBoxConstraint(const FPBDRigidParticles& InParticles, int32 SphereIndex, int32 BoxIndex, const FReal Thickness)
{
	FRigidBodyContactConstraint Constraint;
	Constraint.ParticleIndex = SphereIndex;
	Constraint.LevelsetIndex = BoxIndex;
	return Constraint;
}

typename FPBDCollisionConstraintPGS::FRigidBodyContactConstraint FPBDCollisionConstraintPGS::ComputeConstraint(const FPBDRigidParticles& InParticles, int32 Body1Index, int32 Body2Index, const FReal Thickness)
{
	if (InParticles.Geometry(Body1Index)->GetType() == FAABB3::GetType() && InParticles.Geometry(Body2Index)->GetType() == FAABB3::GetType())
	{
		return ComputeBoxConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == TSphere<FReal, 3>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TSphere<FReal, 3>::GetType())
	{
		return ComputeSphereConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == FAABB3::GetType() && InParticles.Geometry(Body2Index)->GetType() == TPlane<FReal, 3>::GetType())
	{
		return ComputeBoxPlaneConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body2Index)->GetType() == TPlane<FReal, 3>::GetType() && InParticles.Geometry(Body1Index)->GetType() == FAABB3::GetType())
	{
		return ComputeBoxPlaneConstraint(InParticles, Body2Index, Body1Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == TSphere<FReal, 3>::GetType() && InParticles.Geometry(Body2Index)->GetType() == TPlane<FReal, 3>::GetType())
	{
		return ComputeSpherePlaneConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body2Index)->GetType() == TPlane<FReal, 3>::GetType() && InParticles.Geometry(Body1Index)->GetType() == TSphere<FReal, 3>::GetType())
	{
		return ComputeSpherePlaneConstraint(InParticles, Body2Index, Body1Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->GetType() == TSphere<FReal, 3>::GetType() && InParticles.Geometry(Body2Index)->GetType() == FAABB3::GetType())
	{
		return ComputeSphereBoxConstraint(InParticles, Body1Index, Body2Index, Thickness);
	}
	else if (InParticles.Geometry(Body2Index)->GetType() == FAABB3::GetType() && InParticles.Geometry(Body1Index)->GetType() == TSphere<FReal, 3>::GetType())
	{
		return ComputeSphereBoxConstraint(InParticles, Body2Index, Body1Index, Thickness);
	}
	else if (InParticles.Geometry(Body1Index)->IsConvex() && InParticles.Geometry(Body2Index)->IsConvex())
	{
		return ComputeLevelsetConstraintGJK(InParticles, Body1Index, Body2Index, Thickness);
	}
	return ComputeLevelsetConstraint(InParticles, Body1Index, Body2Index, Thickness);
}

template<class T_PARTICLES>
void FPBDCollisionConstraintPGS::UpdateConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint)
{
	if (InParticles.Geometry(Constraint.ParticleIndex)->GetType() == FAABB3::GetType() && InParticles.Geometry(Constraint.LevelsetIndex)->GetType() == FAABB3::GetType())
	{
		UpdateBoxConstraint(InParticles, Thickness, Constraint);
	}
	else if (InParticles.Geometry(Constraint.ParticleIndex)->GetType() == TSphere<FReal, 3>::GetType() && InParticles.Geometry(Constraint.LevelsetIndex)->GetType() == TSphere<FReal, 3>::GetType())
	{
		UpdateSphereConstraint(InParticles, Thickness, Constraint);
	}
	else if (InParticles.Geometry(Constraint.ParticleIndex)->GetType() == FAABB3::GetType() && InParticles.Geometry(Constraint.LevelsetIndex)->GetType() == TPlane<FReal, 3>::GetType())
	{
		UpdateBoxPlaneConstraint(InParticles, Thickness, Constraint);
	}
	else if (InParticles.Geometry(Constraint.ParticleIndex)->GetType() == TSphere<FReal, 3>::GetType() && InParticles.Geometry(Constraint.LevelsetIndex)->GetType() == TPlane<FReal, 3>::GetType())
	{
		UpdateSpherePlaneConstraint(InParticles, Thickness, Constraint);
	}
	else if (InParticles.Geometry(Constraint.ParticleIndex)->GetType() == TSphere<FReal, 3>::GetType() && InParticles.Geometry(Constraint.LevelsetIndex)->GetType() == FAABB3::GetType())
	{
		UpdateSphereBoxConstraint(InParticles, Thickness, Constraint);
	}
	else if (InParticles.Geometry(Constraint.ParticleIndex)->GetType() == TPlane<FReal, 3>::GetType() && InParticles.Geometry(Constraint.LevelsetIndex)->GetType() == FAABB3::GetType())
	{
		int32 Tmp = Constraint.ParticleIndex;
		Constraint.ParticleIndex = Constraint.LevelsetIndex;
		Constraint.LevelsetIndex = Tmp;
		UpdateBoxPlaneConstraint(InParticles, Thickness, Constraint);
	}
	else if (InParticles.Geometry(Constraint.ParticleIndex)->GetType() == TPlane<FReal, 3>::GetType() && InParticles.Geometry(Constraint.LevelsetIndex)->GetType() == TSphere<FReal, 3>::GetType())
	{
		int32 Tmp = Constraint.ParticleIndex;
		Constraint.ParticleIndex = Constraint.LevelsetIndex;
		Constraint.LevelsetIndex = Tmp;
		UpdateSpherePlaneConstraint(InParticles, Thickness, Constraint);
	}
	else if (InParticles.Geometry(Constraint.ParticleIndex)->GetType() == FAABB3::GetType() && InParticles.Geometry(Constraint.LevelsetIndex)->GetType() == TSphere<FReal, 3>::GetType())
	{
		int32 Tmp = Constraint.ParticleIndex;
		Constraint.ParticleIndex = Constraint.LevelsetIndex;
		Constraint.LevelsetIndex = Tmp;
		UpdateSphereBoxConstraint(InParticles, Thickness, Constraint);
	}
	else if (InParticles.Geometry(Constraint.ParticleIndex)->IsConvex() && InParticles.Geometry(Constraint.LevelsetIndex)->IsConvex())
	{
		UpdateLevelsetConstraintGJK(InParticles, Thickness, Constraint);
	}
	else
	{
		UpdateLevelsetConstraint(InParticles, Thickness, Constraint);
	}
}

#endif