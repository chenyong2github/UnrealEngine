// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ConstraintHandle.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Framework/BufferedData.h"

#include <memory>
#include <queue>
#include <sstream>
#include "BoundingVolume.h"
#include "AABBTree.h"

namespace Chaos
{
template<typename T, int d>
class TPBDCollisionConstraintAccessor;

template<typename T, int d>
class TPBDCollisionConstraint;

template <typename T, int d>
class TRigidTransform;

class FImplicitObject;

template <typename T, int d>
class TBVHParticles;

template <typename T, int d>
class TBox;

template<class T>
class TChaosPhysicsMaterial;

template <typename T, int d>
class TPBDRigidsSOAs;

template<typename T, int d>
using TRigidBodyContactConstraintsPostComputeCallback = TFunction<void()>;

template<typename T, int d>
using TRigidBodyContactConstraintsPostApplyCallback = TFunction<void(const T Dt, const TArray<TPBDCollisionConstraintHandle<T, d>*>& InConstraintHandles)>;

template<typename T, int d>
using TRigidBodyContactConstraintsPostApplyPushOutCallback = TFunction<void(const T Dt, const TArray<TPBDCollisionConstraintHandle<T, d>*>& InConstraintHandles, bool bRequiresAnotherIteration)>;

/**
 * Manages a set of contact constraints:
 *	- Performs collision detection to generate constraints.
 *	- Responsible for applying corrections to particles affected by the constraints.
 * @todo(ccaulfield): rename to TPBDCollisionConstraints
 * @todo(ccaulfield): remove TPBDCollisionConstraintAccessor
 * @todo(ccaulfield): separate collision detection from constraint container
 */
template<typename T, int d>
class CHAOS_API TPBDCollisionConstraint : public FPBDConstraintContainer
{
public:
	friend class TPBDCollisionConstraintAccessor<T, d>;
	friend class TPBDCollisionConstraintHandle<T, d>;

	using Base = FPBDConstraintContainer;
	using FConstraintHandleID = TPair<const TGeometryParticleHandle<T, d>*, const TGeometryParticleHandle<T, d>*>;
	using FConstraintContainerHandle = TPBDCollisionConstraintHandle<T, d>; // @todo(brice) rename this to FCollisionConstraintHandle
	using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDCollisionConstraint<T, d>>;
	using FRigidBodyContactConstraint = TRigidBodySingleContactConstraint<T, d>;
	using FHandles = TArray<FConstraintContainerHandle*>;

	TPBDCollisionConstraint(const TPBDRigidsSOAs<T,d>& InParticles, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterials, const int32 ApplyPairIterations = 1, const int32 ApplyPushOutPairIterations = 1, const T Thickness = (T)0);
	virtual ~TPBDCollisionConstraint() {}

	//
	// Constraint Container API
	//

	void SetThickness(T InThickness)
	{
		MThickness = InThickness;
	}

	void SetVelocityInflation(float ScaleFactor)
	{
		CollisionVelocityInflation = ScaleFactor;
	}

	void SetPairIterations(int32 InPairIterations)
	{
		MApplyPairIterations = InPairIterations;
	}

	void SetPushOutPairIterations(int32 InPairIterations)
	{
		MApplyPushOutPairIterations = InPairIterations;
	}

	void SetCollisionsEnabled(bool bInEnableCollisions)
	{
		bEnableCollisions = bInEnableCollisions;
	}

	int32 NumConstraints() const
	{
		return Constraints.Num();
	}

	FConstraintHandleID GetConstraintHandleID(const FRigidBodyContactConstraint & Constraint) const
	{
		return  FConstraintHandleID( Constraint.Particle[0], Constraint.Particle[1] );
	}

	FHandles& GetConstraintHandles()
	{
		return Handles;
	}
	const FHandles& GetConstConstraintHandles() const
	{
		return Handles;
	}

	void RemoveConstraint(FConstraintContainerHandle*);

	/**
	* Remove the constraints associated with the ParticleHandle.
	*/
	void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>&  ParticleHandle);

	/**
	 * Apply a modifier to the constraints and specify which constraints should be disabled.
	 * You would probably call this in the PostComputeCallback. Prefer this to calling RemoveConstraints in a loop, 
	 * so you don't have to worry about constraint iterator/indices changing.
	 */
	void ApplyCollisionModifier(const TFunction<ECollisionModifierResult(const FConstraintContainerHandle* Handle)>& CollisionModifier);

	/**
	 * Set the callback used just after contacts are generated at the start of a frame tick.
	 * This can be used to modify or disable constraints (via ApplyCollisionModifier).
	 */
	void SetPostComputeCallback(const TRigidBodyContactConstraintsPostComputeCallback<T, d>& Callback);
	void ClearPostComputeCallback();

	void SetPostApplyCallback(const TRigidBodyContactConstraintsPostApplyCallback<T, d>& Callback);
	void ClearPostApplyCallback();

	void SetPostApplyPushOutCallback(const TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d>& Callback);
	void ClearPostApplyPushOutCallback();

	//
	// Island Rule API
	//

	/**
	 * Apply corrections for the specified list of constraints. (Runs Wide!)
	 *
	 * @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	 *
	 */
	void Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);

	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(const T Dt);

	/**
	 * Update all constraint values
	 */
	void UpdateConstraints(T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles);

	/**
	 * Apply push out for the specified list of constraints.
	 *  Return true if we need another iteration 
	 *
	 * @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	 *
	 */
	bool ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const TSet<const TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations);


	const TArray<FConstraintContainerHandle*>& GetAllConstraintHandles() const { return Handles; }

	//using FAccelerationStructure = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;
	//using FAccelerationStructure = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;
	using FAccelerationStructure = ISpatialAcceleration< TAccelerationStructureHandle<T, d>, T, d>;
	void SetSpatialAcceleration(const FAccelerationStructure* InSpatialAcceleration) { SpatialAcceleration = InSpatialAcceleration; }

	//NOTE: this should not be called by anyone other than ISpatialAccelerationCollection and CollisionConstraints - todo: make private with friends?
	template <bool bGatherStats, typename SPATIAL_ACCELERATION>
	void ComputeConstraintsHelperLowLevel(const SPATIAL_ACCELERATION& SpatialAcceleration, T Dt);
	

protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

private:

	/*
	* Reset the constraints. 
	*/
	void Reset( );

	void Apply(const T Dt, FRigidBodyContactConstraint& Constraint, const int32 It, const int32 NumIts);


	void ApplyPushOut(const T Dt, FRigidBodyContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations, bool &NeedsAnotherIteration);

	template <bool bGatherStats = false>
	void ComputeConstraints(const FAccelerationStructure& AccelerationStructure, T Dt);

	template<ECollisionUpdateType>
	void UpdateConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint);

	void ConstructConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness, TRigidBodySingleContactConstraint<T, d> & Constraint);


	const TPBDRigidsSOAs<T,d>& Particles;


	TArray<FConstraintContainerHandle*> Handles;
	FConstraintHandleAllocator HandleAllocator;
	TMap< FConstraintHandleID, FConstraintContainerHandle* > Manifolds;
	TArray<FRigidBodyContactConstraint> Constraints;

	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& MPhysicsMaterials;
	int32 MApplyPairIterations;
	int32 MApplyPushOutPairIterations;
	T MThickness;
	T MAngularFriction;
	bool bUseCCD;
	bool bEnableCollisions;

	int32 LifespanCounter;
	float CollisionVelocityInflation;

	TRigidBodyContactConstraintsPostComputeCallback<T, d> PostComputeCallback;
	TRigidBodyContactConstraintsPostApplyCallback<T, d> PostApplyCallback;
	TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d> PostApplyPushOutCallback;

	// @todo(ccaulfield): move spatial acceleration out of constraint container and make shareable
	const FAccelerationStructure* SpatialAcceleration;
};

extern template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<false>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);
extern template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<true>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);
}
