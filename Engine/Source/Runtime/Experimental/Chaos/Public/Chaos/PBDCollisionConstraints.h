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
 * A container and solver for collision constraints.
 */
template<typename T, int d>
class CHAOS_API TPBDCollisionConstraints : public FPBDConstraintContainer
{
public:
	friend class TPBDCollisionConstraintHandle<T, d>;

	using Base = FPBDConstraintContainer;
	using FHandles = TArray<TPBDCollisionConstraintHandle<T, d>*>;
	using FConstraintContainerHandle = TPBDCollisionConstraintHandle<T, d>;
	using FConstraintBase = TCollisionConstraintBase<T, d>;
 	using FPointContactConstraint = TRigidBodyPointContactConstraint<T, d>;
	using FPlaneContactConstraint = TRigidBodyPlaneContactConstraint<T, d>;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDCollisionConstraints<T, d>>;
	using FConstraintContainerHandleKey = typename TPBDCollisionConstraintHandle<T, d>::FHandleKey;

	TPBDCollisionConstraints(const TPBDRigidsSOAs<T,d>& InParticles, 
		TArrayCollectionArray<bool>& Collided, 
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterials, 
		const int32 ApplyPairIterations = 1, const int32 ApplyPushOutPairIterations = 1, const T Thickness = (T)0);

	virtual ~TPBDCollisionConstraints() {}


	void AddConstraint(FConstraintBase* InConstraint);

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
	 * Update all constraint values within the set
	 */
	void UpdateConstraints(T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles);

	/**
	 * Update all constraint values
	 */

	/**
	* Update all constraint values
	*/
	void UpdateConstraints(T Dt);

	//
	// General Rule API
	//

	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(const T Dt);

	//
	// Simple Rule API
	//

	void Apply(const T Dt, const int32 It, const int32 NumIts);
	bool ApplyPushOut(const T Dt, const int32 It, const int32 NumIts);

	//
	// Island Rule API
	//
	// @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule

	void Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);
	bool ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, 
		const TSet<const TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations);


	/**
	 *  Callbacks
	 */
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

	bool Contains(const FConstraintBase* Base) const 
	{
		return Manifolds.Contains(FConstraintContainerHandle::MakeKey(Base));
	}

	void SetThickness(T InThickness)
	{
		MThickness = InThickness;
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

	bool GetCollisionsEnabled() const
	{
		return bEnableCollisions;
	}

	int32 NumConstraints() const
	{
		return Handles.Num();
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

	const TPBDRigidsSOAs<T,d>& Particles;

	TArray<FPointContactConstraint> PointConstraints;
	TArray<FPlaneContactConstraint> PlaneConstraints;

	TMap< FConstraintContainerHandleKey, FConstraintContainerHandle* > Manifolds;
	TArray<FConstraintContainerHandle*> Handles;
	FConstraintHandleAllocator HandleAllocator;

	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& MPhysicsMaterials;
	int32 MApplyPairIterations;
	int32 MApplyPushOutPairIterations;
	T MThickness;	// @todo(ccaulfield) - COLLISION thickness - this should be used as shape padding (as opposed to broad/narrowphase thickness which is for speculative creation of constraints)
	T MAngularFriction;
	bool bUseCCD;
	bool bEnableCollisions;

	int32 LifespanCounter;

	TRigidBodyContactConstraintsPostApplyCallback<T, d> PostApplyCallback;
	TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d> PostApplyPushOutCallback;
};
}
