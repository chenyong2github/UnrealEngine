// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"
#include "Chaos/Box.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/Collision/CollisionDetector.h"
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
template <class T, int d>
class TPBDCollisionConstraintAccessor
{
public:
	using FCollisionConstraints = TPBDCollisionConstraints<T, d>;
	using FConstraintContainerHandle = TPBDCollisionConstraintHandle<T, d>;
	using FContactConstraintBase = TCollisionConstraintBase<T, d>;
	using FMultiPointContactConstraint = TRigidBodyMultiPointContactConstraint<T, d>;
	using FPointContactConstraint = TRigidBodyPointContactConstraint<T, d>;
	using FMultiPointContactConstraint = TRigidBodyMultiPointContactConstraint<T, d>;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDCollisionConstraints<T, d>>;
	using FConstraintHandleID = TPair<const TGeometryParticleHandle<T, d>*, const TGeometryParticleHandle<T, d>*>;
	using FCollisionDetector = TCollisionDetector<FSpatialAccelerationBroadPhase, FNarrowPhase, FAsyncCollisionReceiver, FCollisionConstraints>;
	using FAccelerationStructure = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;

	TPBDCollisionConstraintAccessor() 
		: SpatialAcceleration(EmptyParticles.GetNonDisabledView())
		, BroadPhase(EmptyParticles, (T)1, (T)0)
		, CollisionConstraints(EmptyParticles, EmptyCollided, EmptyPhysicsMaterials, 1, 1)
		, CollisionDetector(BroadPhase, CollisionConstraints)
	{}

	TPBDCollisionConstraintAccessor(const TPBDRigidsSOAs<T, d>& InParticles, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterials,
	const int32 PushOutIterations, const int32 PushOutPairIterations, const T Thickness) 
		: SpatialAcceleration(InParticles.GetNonDisabledView())
		, BroadPhase(InParticles, (T)1, (T)0)
		, CollisionConstraints(InParticles, Collided, PerParticleMaterials, 1, 1, Thickness)
		, CollisionDetector(BroadPhase, CollisionConstraints)
	{}

	virtual ~TPBDCollisionConstraintAccessor() {}
	
	void ComputeConstraints(T Dt)
	{
		CollisionStats::FStatData StatData(false);
		CollisionDetector.GetBroadPhase().SetSpatialAcceleration(&SpatialAcceleration);
		CollisionDetector.DetectCollisions(Dt, StatData);
	}

	void Update(FContactConstraintBase& Constraint, T BoundsThickness = T(0) )
	{
		Collisions::Update<ECollisionUpdateType::Deepest,T,d>(BoundsThickness, Constraint);
	}

	void UpdateManifold(FContactConstraintBase& Constraint, T BoundsThickness = T(0))
	{
		Collisions::UpdateManifold<T, d>(BoundsThickness, Constraint);
	}


	void UpdateLevelsetConstraint(FPointContactConstraint& Constraint) 
	{
		Collisions::UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest>(T(0), Constraint);
	}

	int32 NumConstraints() const
	{
		return CollisionConstraints.NumConstraints();
	}

	TCollisionConstraintBase<T,d>& GetConstraint(int32 Index)
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

	void Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		CollisionConstraints.Apply(Dt, InConstraintHandles, It, NumIts);
	}

	bool ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
	{
		return CollisionConstraints.ApplyPushOut(Dt, InConstraintHandles, IsTemporarilyStatic, Iteration, NumIterations);
	}

	FPointContactConstraint EmptyConstraint;
	TPBDRigidsSOAs<T, d> EmptyParticles;
	TArrayCollectionArray<bool> EmptyCollided;
	TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> EmptyPhysicsMaterials;

	FAccelerationStructure SpatialAcceleration;
	FSpatialAccelerationBroadPhase BroadPhase;
	FCollisionConstraints CollisionConstraints;
	FCollisionDetector CollisionDetector;
};
}
