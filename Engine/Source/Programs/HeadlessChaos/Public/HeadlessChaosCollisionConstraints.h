// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"
#include "Chaos/Box.h"
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
	using FMultiPointContactConstraint = FRigidBodyMultiPointContactConstraint;
	using FPointContactConstraint = FRigidBodyPointContactConstraint;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDCollisionConstraints>;
	using FConstraintHandleID = TPair<const TGeometryParticleHandle<FReal, 3>*, const TGeometryParticleHandle<FReal, 3>*>;
	using FCollisionDetector = FSpatialAccelerationCollisionDetector;
	using FAccelerationStructure = TBoundingVolume<TAccelerationStructureHandle<FReal, 3>, FReal, 3>;

	FPBDCollisionConstraintAccessor()
		: SpatialAcceleration(EmptyParticles.GetNonDisabledView())
		, BroadPhase(EmptyParticles, (FReal)1, (FReal)0)
		, CollisionConstraints(EmptyParticles, EmptyCollided, EmptyPhysicsMaterials, 1, 1)
		, CollisionDetector(BroadPhase, NarrowPhase, CollisionConstraints)
	{}

	FPBDCollisionConstraintAccessor(const TPBDRigidsSOAs<FReal, 3>& InParticles, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterials,
	const int32 PushOutIterations, const int32 PushOutPairIterations, const FReal Thickness) 
		: SpatialAcceleration(InParticles.GetNonDisabledView())
		, BroadPhase(InParticles, (FReal)1, (FReal)0)
		, CollisionConstraints(InParticles, Collided, PerParticleMaterials, 1, 1, Thickness)
		, CollisionDetector(BroadPhase, NarrowPhase, CollisionConstraints)
	{}

	virtual ~FPBDCollisionConstraintAccessor() {}
	
	void ComputeConstraints(FReal Dt)
	{
		CollisionDetector.GetBroadPhase().SetSpatialAcceleration(&SpatialAcceleration);
		CollisionDetector.DetectCollisions(Dt);
	}

	void Update(FCollisionConstraintBase& Constraint, FReal CullDistance = FReal(0) )
	{
		if (Constraint.GetType() == FPointContactConstraint::StaticType())
		{
			Collisions::Update(*Constraint.As<FPointContactConstraint>(), CullDistance);
		}
		else if(Constraint.GetType() == FMultiPointContactConstraint::StaticType())
		{
			Collisions::Update(*Constraint.As<FMultiPointContactConstraint>(), CullDistance);
		}
	}

	void UpdateManifold(FCollisionConstraintBase& Constraint, FReal CullDistance = FReal(0))
	{
		if (Constraint.GetType() == FCollisionConstraintBase::FType::MultiPoint)
		{
			FCollisionContext Context;
			Collisions::UpdateManifold(*Constraint.As<FRigidBodyMultiPointContactConstraint>(), CullDistance, Context);
		}
	}


	void UpdateLevelsetConstraint(FPointContactConstraint& Constraint) 
	{
		Collisions::UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest>(FReal(0), Constraint);
	}

	int32 NumConstraints() const
	{
		return CollisionConstraints.NumConstraints();
	}

	FCollisionConstraintBase& GetConstraint(int32 Index)
	{
		if (Index < CollisionConstraints.NumConstraints())
		{
			return GetConstraintHandle(Index)->GetContact();
		}
		return EmptyConstraint;
	}

	const FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex) const
	{
		return CollisionConstraints.GetAllConstraintHandles()[ConstraintIndex];
	}

	FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex)
	{
		return CollisionConstraints.GetAllConstraintHandles()[ConstraintIndex];
	}

	void Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		CollisionConstraints.Apply(Dt, InConstraintHandles, It, NumIts);
	}

	bool ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
	{
		return CollisionConstraints.ApplyPushOut(Dt, InConstraintHandles, IsTemporarilyStatic, Iteration, NumIterations);
	}

	FPointContactConstraint EmptyConstraint;
	TPBDRigidsSOAs<FReal, 3> EmptyParticles;
	TArrayCollectionArray<bool> EmptyCollided;
	TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> EmptyPhysicsMaterials;

	FAccelerationStructure SpatialAcceleration;
	FSpatialAccelerationBroadPhase BroadPhase;
	FNarrowPhase NarrowPhase;
	FCollisionConstraints CollisionConstraints;
	FCollisionDetector CollisionDetector;
};
}
