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
class TPBDCollisionConstraints;

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
using TRigidBodyContactConstraintsPostApplyCallback = TFunction<void(const T Dt, const TArray<TPBDCollisionConstraintHandle<T, d>*>&)>;

template<typename T, int d>
using TRigidBodyContactConstraintsPostApplyPushOutCallback = TFunction<void(const T Dt, const TArray<TPBDCollisionConstraintHandle<T, d>*>&, bool)>;

/**
 * Manages a set of contact constraints:
 *	- Performs collision detection to generate constraints.
 *	- Responsible for applying corrections to particles affected by the constraints.
 * @todo(ccaulfield): remove TPBDCollisionConstraintAccessor
 */
template<typename T, int d>
class CHAOS_API TPBDCollisionConstraints : public FPBDConstraintContainer
{
public:
	friend class TPBDCollisionConstraintAccessor<T, d>;
	friend class TPBDCollisionConstraintHandle<T, d>;

	using Base = FPBDConstraintContainer;
	using FHandles = TArray<TPBDCollisionConstraintHandle<T, d>*>;
	using FConstraintContainerHandle = TPBDCollisionConstraintHandle<T, d>;
 	using FPointContactConstraint = TRigidBodyPointContactConstraint<T, d>;
	using FPlaneContactConstraint = TRigidBodyPlaneContactConstraint<T, d>;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDCollisionConstraints<T, d>>;
	using FConstraintHandleID = TPair<const TGeometryParticleHandle<T, d>*, const TGeometryParticleHandle<T, d>*>;


	TPBDCollisionConstraints(const TPBDRigidsSOAs<T,d>& InParticles, 
		TArrayCollectionArray<bool>& Collided, 
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterials, 
		const int32 ApplyPairIterations = 1, const int32 ApplyPushOutPairIterations = 1, const T Thickness = (T)0);

	virtual ~TPBDCollisionConstraints() {}


	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(const T Dt);


	/**
	*  Reset the constraint frame. 
	*/
	void Reset();

	/**
	 * Apply a modifier to the constraints and specify which constraints should be disabled.
	 * You would probably call this in the PostComputeCallback. Prefer this to calling RemoveConstraints in a loop,
	 * so you don't have to worry about constraint iterator/indices changing.
	 */
	void ApplyCollisionModifier(const TFunction<ECollisionModifierResult(const FConstraintContainerHandle* Handle)>& CollisionModifier);


	/**
	* Remove the constraints associated with the ParticleHandle.
	*/
	void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>&  ParticleHandle);


	/**
	* Remove the constraint, update the handle, and any maps. 
	*/
	void RemoveConstraint(FConstraintContainerHandle*);


	/**
	*  Process constraints acceleration structures. 
	*/
	//using FAccelerationStructure = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;
	//using FAccelerationStructure = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;
	using FAccelerationStructure = ISpatialAcceleration< TAccelerationStructureHandle<T, d>, T, d>;
	void SetSpatialAcceleration(const FAccelerationStructure* InSpatialAcceleration) { SpatialAcceleration = InSpatialAcceleration; }

	template <bool bGatherStats = false>
	void ComputeConstraints(const FAccelerationStructure& AccelerationStructure, T Dt);

	//NOTE: this should not be called by anyone other than ISpatialAccelerationCollection and CollisionConstraints - todo: make private with friends?
	template <bool bGatherStats, typename SPATIAL_ACCELERATION>
	void ComputeConstraintsHelperLowLevel(const SPATIAL_ACCELERATION& SpatialAcceleration, T Dt);


	/**
	 * Update all constraint values
	 */
	void UpdateConstraints(T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles);



	/**
	 * Apply corrections for the specified list of constraints. (Runs Wide!)
	 *
	 * @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	 *
	 */
	void Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);


	/**
	 * Apply push out for the specified list of constraints.
	 *  Return true if we need another iteration 
	 *
	 * @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	 *
	 */
	bool ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, 
		const TSet<const TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations);


	/**
	*
	*  Callbacks
	*
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
	// Member Access
	//

	const TArray<FConstraintContainerHandle*>& GetAllConstraintHandles() const 
	{ 
		return Handles; 
	}

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
		return Handles.Num();
	}

	FConstraintHandleID GetConstraintHandleID(const FPointContactConstraint & Constraint) const
	{
		return  FConstraintHandleID(Constraint.Particle[0], Constraint.Particle[1]);
	}

	FHandles& GetConstraintHandles()
	{
		return Handles;
	}
	const FHandles& GetConstConstraintHandles() const
	{
		return Handles;
	}


protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

private:

	/**
	* Update the individual constraints.
	*/
	template<ECollisionUpdateType>
	void UpdateConstraint(const T Thickness, TRigidBodyPointContactConstraint<T, d> & Constraint);

	/**
	*  Build a constraint based on the two particle handles.
	*/
	void ConstructConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, 
		const T Thickness, TRigidBodyPointContactConstraint<T, d> & Constraint);



	const TPBDRigidsSOAs<T,d>& Particles;

	TArray<FPointContactConstraint> PointConstraints;
	TArray<FPlaneContactConstraint> PlaneConstraints;

	TMap< FConstraintHandleID, FConstraintContainerHandle* > Manifolds;
	TArray<FConstraintContainerHandle*> Handles;
	FConstraintHandleAllocator HandleAllocator;

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

extern template void TPBDCollisionConstraints<float, 3>::UpdateConstraint<ECollisionUpdateType::Any>(const float Thickness, TRigidBodyPointContactConstraint<float,3> & Constraint);
extern template void TPBDCollisionConstraints<float, 3>::UpdateConstraint<ECollisionUpdateType::Deepest>(const float Thickness, TRigidBodyPointContactConstraint<float, 3> & Constraint);
extern template void TPBDCollisionConstraints<float, 3>::ComputeConstraints<false>(const TPBDCollisionConstraints<float, 3>::FAccelerationStructure&, float Dt);
extern template void TPBDCollisionConstraints<float, 3>::ComputeConstraints<true>(const TPBDCollisionConstraints<float, 3>::FAccelerationStructure&, float Dt);
}
