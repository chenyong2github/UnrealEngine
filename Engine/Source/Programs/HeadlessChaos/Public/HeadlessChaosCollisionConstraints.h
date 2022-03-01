// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"
#include "Chaos/Box.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/Collision/SpatialAccelerationCollisionDetector.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Plane.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"
#include "Chaos/Vector.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/Evolution/SolverDatas.h"

namespace Chaos
{	

/**
 * Test collision constraints.
 */
class FPBDCollisionConstraintAccessor
{
public:
	using FCollisionConstraints = FPBDCollisionConstraints;
	using FConstraintContainerHandle = FPBDCollisionConstraintHandle;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDCollisionConstraint>;
	using FConstraintHandleID = TPair<const FGeometryParticleHandle*, const FGeometryParticleHandle*>;
	using FCollisionDetector = FSpatialAccelerationCollisionDetector;
	using FAccelerationStructure = TBoundingVolume<FAccelerationStructureHandle>;

	FPBDCollisionConstraintAccessor()
		: EmptyParticles(UniqueIndices)
		, SpatialAcceleration(EmptyParticles.GetNonDisabledView())
		, CollisionConstraints(EmptyParticles, EmptyCollided, EmptyPhysicsMaterials, EmptyUniquePhysicsMaterials, nullptr, 1, 1)
		, NarrowPhase((FReal)1, (FReal)0, CollisionConstraints.GetConstraintAllocator())
		, BroadPhase(EmptyParticles)
		, CollisionDetector(BroadPhase, NarrowPhase, CollisionConstraints)
	{
		CollisionConstraints.SetSolverType(EConstraintSolverType::QuasiPbd);
		CollisionConstraints.SetContainerId(0);
		SolverData.AddConstraintDatas<FCollisionConstraints>(CollisionConstraints.GetContainerId());
	}

	FPBDCollisionConstraintAccessor(const FPBDRigidsSOAs& InParticles, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterials, const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& PerParticleUniqueMaterials,
		const int32 PushOutIterations, const int32 PushOutPairIterations) 
		: EmptyParticles(UniqueIndices)
		, SpatialAcceleration(InParticles.GetNonDisabledView())
		, CollisionConstraints(InParticles, Collided, PerParticleMaterials, PerParticleUniqueMaterials, nullptr, 1, 1)
		, NarrowPhase((FReal)1, (FReal)0, CollisionConstraints.GetConstraintAllocator())
		, BroadPhase(InParticles)
		, CollisionDetector(BroadPhase, NarrowPhase, CollisionConstraints)
	{
		CollisionConstraints.SetSolverType(EConstraintSolverType::QuasiPbd);
		CollisionConstraints.SetContainerId(0);
		SolverData.AddConstraintDatas<FCollisionConstraints>(CollisionConstraints.GetContainerId());
	}

	virtual ~FPBDCollisionConstraintAccessor() {}
	
	void ComputeConstraints(FReal Dt)
	{
		CollisionDetector.GetBroadPhase().SetSpatialAcceleration(&SpatialAcceleration);
		CollisionDetector.GetNarrowPhase().GetContext().bFilteringEnabled = true;
		CollisionDetector.GetNarrowPhase().GetContext().bDeferUpdate = false;
		CollisionDetector.GetNarrowPhase().GetContext().bAllowManifolds = true;
		CollisionDetector.DetectCollisions(Dt, nullptr);
		CollisionDetector.GetCollisionContainer().GetConstraintAllocator().SortConstraintsHandles();
	}

	void Update(FPBDCollisionConstraint& Constraint)
	{
		if (Constraint.GetCCDType() == ECollisionCCDType::Disabled)
		{
			// Dt is not important for the tests that use this function
			const FReal Dt = FReal(1) / FReal(30);

			Constraint.ResetPhi(TNumericLimits<FReal>::Max());
			Collisions::UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest>(
				Constraint, 
				FParticleUtilities::GetActorWorldTransform(FGenericParticleHandle(Constraint.GetParticle0())), 
				FParticleUtilities::GetActorWorldTransform(FGenericParticleHandle(Constraint.GetParticle1())), 
				Dt);
		}
	}


	void UpdateLevelsetConstraint(FPBDCollisionConstraint& Constraint)
	{
		// Dt is not important for the tests that use this function
		const FReal Dt = FReal(1) / FReal(30);

		FRigidTransform3 WorldTransform0 = Constraint.GetShapeRelativeTransform0() * Collisions::GetTransform(Constraint.GetParticle0());
		FRigidTransform3 WorldTransform1 = Constraint.GetShapeRelativeTransform1() * Collisions::GetTransform(Constraint.GetParticle1());

		Constraint.ResetManifold();
		Collisions::UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest>(WorldTransform0, WorldTransform1, FReal(1 / 30.0f), Constraint);
	}

	int32 NumConstraints() const
	{
		return CollisionConstraints.NumConstraints();
	}

	FPBDCollisionConstraint& GetConstraint(int32 Index)
	{
		if (Index < CollisionConstraints.NumConstraints())
		{
			return GetConstraintHandle(Index)->GetContact();
		}
		return EmptyConstraint;
	}

	const FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex) const
	{
		return CollisionConstraints.GetConstraintHandles()[ConstraintIndex];
	}

	FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex)
	{
		return CollisionConstraints.GetConstraintHandles()[ConstraintIndex];
	}

	void Apply(const FReal Dt, const int32 It, const int32 NumIts)
	{
		CollisionConstraints.ApplyPhase1(Dt, It, NumIts, SolverData);
	}

	bool ApplyPushOut(const FReal Dt, int32 Iteration, int32 NumIterations)
	{
		return CollisionConstraints.ApplyPhase2(Dt, Iteration, NumIterations, SolverData);
	}

	void GatherInput(FReal Dt)
	{
		SolverData.GetBodyContainer().Reset(1000);
		CollisionConstraints.SetNumIslandConstraints(CollisionConstraints.NumConstraints(), SolverData);

		for (auto Handle : CollisionConstraints.GetConstraintHandles())
		{
			Handle->PreGatherInput(Dt, SolverData);
			Handle->GatherInput(Dt, INDEX_NONE, INDEX_NONE, SolverData);
		}
	}

	void ScatterOutput(FReal Dt)
	{
		CollisionConstraints.ScatterOutput(Dt, SolverData);
		SolverData.GetBodyContainer().ScatterOutput();
		SolverData.GetBodyContainer().Reset(0);;
	}

	void SetImplicitVelocities(FReal Dt)
	{
		SolverData.GetBodyContainer().SetImplicitVelocities(Dt);
	}

	FPBDCollisionConstraint EmptyConstraint;
	FParticleUniqueIndicesMultithreaded UniqueIndices;
	FPBDRigidsSOAs EmptyParticles;
	TArrayCollectionArray<bool> EmptyCollided;
	TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> EmptyPhysicsMaterials;
	TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> EmptyUniquePhysicsMaterials;

	FAccelerationStructure SpatialAcceleration;
	FCollisionConstraints CollisionConstraints;
	FNarrowPhase NarrowPhase;
	FSpatialAccelerationBroadPhase BroadPhase;
	FCollisionDetector CollisionDetector;
	FPBDIslandSolverData SolverData;
};
}
