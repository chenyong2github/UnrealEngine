// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Framework/BufferedData.h"

#include <memory>
#include <queue>
#include <sstream>
#include "BoundingVolume.h"
#include "AABBTree.h"

// @todo(chaos): optimize and re-enable persistent constraints if we want it
#define CHAOS_COLLISION_PERSISTENCE_ENABLED 0

namespace Chaos
{
template<typename T, int d>
class TPBDCollisionConstraints;

template <typename T, int d>
class TPBDCollisionConstraintHandle;

template <typename T, int d>
class TCollisionConstraintBase;

template <typename T, int d>
class TRigidBodyPointContactConstraint;

template <typename T, int d>
class TRigidBodyMultiPointContactConstraint;

template <typename T, int d>
class TRigidTransform;

class FImplicitObject;

template <typename T, int d>
class TBVHParticles;

template <typename T, int d>
class TAABB;

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

template <typename T, int d>
using TCollisionModifierCallback = TFunction<ECollisionModifierResult(TPBDCollisionConstraintHandle<T, d>*)>;

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
 	using FSweptPointContactConstraint = TRigidBodySweptPointContactConstraint<T, d>;
	using FMultiPointContactConstraint = TRigidBodyMultiPointContactConstraint<T, d>;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDCollisionConstraints<T, d>>;
	using FConstraintContainerHandleKey = typename TPBDCollisionConstraintHandle<T, d>::FHandleKey;
	using FCollisionModifier = TCollisionModifierCallback<T, d>;

	TPBDCollisionConstraints(const TPBDRigidsSOAs<T,d>& InParticles, 
		TArrayCollectionArray<bool>& Collided, 
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterials, 
		const int32 ApplyPairIterations = 1, const int32 ApplyPushOutPairIterations = 1, const T CullDistance = (T)0, const T ShapePadding = (T)0);

	virtual ~TPBDCollisionConstraints() {}

	/**
	 * Whether this container provides constraint handles (simple solvers do not need them)
	 */
	bool GetHandlesEnabled() const { return bHandlesEnabled; }

	/**
	 * Put the container in "no handles" mode for use with simple solver. Must be called when empty of constraints (ideally right after creation).
	 */
	void DisableHandles();

	/**
	 * Set the solver method to use in the Apply step
	 */
	void SetApplyType(ECollisionApplyType InApplyType)
	{
		ApplyType = InApplyType;
	}

	/**
	*  Add the constraint to the container. 
	*
	*  @todo(chaos) : Collision Constraints 
	*  Update to use a custom allocator. 
	*  The InConstraint should be a point to unmanaged, raw memory. 
	*  This function will make a deep copy of the constraint and 
	*  then delete the InConstraint. 
	*/
	void AddConstraint(const TRigidBodyPointContactConstraint<FReal, 3>& InConstraint);
	void AddConstraint(const TRigidBodySweptPointContactConstraint<FReal, 3>& InConstraint);
	void AddConstraint(const TRigidBodyMultiPointContactConstraint<FReal, 3>& InConstraint);

	/**
	*  Reset the constraint frame. 
	*/
	void Reset();

	/**
	 * Apply a modifier to the constraints and specify which constraints should be disabled.
	 * You would probably call this in the PostComputeCallback. Prefer this to calling RemoveConstraints in a loop,
	 * so you don't have to worry about constraint iterator/indices changing.
	 */
	void ApplyCollisionModifier(const FCollisionModifier& CollisionModifier);


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

	/**
	* Update all contact manifolds
	*/
	void UpdateManifolds(T Dt);


	//
	// General Rule API
	//

	void PrepareConstraints(FReal Dt) {}

	void UnprepareConstraints(FReal Dt) {}

	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(const T Dt);

	//
	// Simple Rule API
	//

	bool Apply(const T Dt, const int32 It, const int32 NumIts);
	bool ApplyPushOut(const T Dt, const int32 It, const int32 NumIts);

	//
	// Island Rule API
	//
	// @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule

	bool Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);
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
#if CHAOS_COLLISION_PERSISTENCE_ENABLED
		return Manifolds.Contains(FConstraintContainerHandle::MakeKey(Base));
#else
		return false;
#endif
	}

	// @todo(chaos): remove
	//void SetThickness(T InThickness)
	//{
	//	MCullDistance = InThickness;
	//}

	void SetCullDistance(T InCullDistance)
	{
		MCullDistance = InCullDistance;
	}

	FReal GetCullDistance() const
	{
		return MCullDistance;
	}

	void SetShapePadding(T InShapePadding)
	{
		MShapePadding = InShapePadding;
	}

	FReal GetShapePadding() const
	{
		return MShapePadding;
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
		return Constraints.SinglePointConstraints.Num() + Constraints.SinglePointSweptConstraints.Num() + Constraints.MultiPointConstraints.Num();
	}

	FHandles& GetConstraintHandles()
	{
		return Handles;
	}

	const FHandles& GetConstConstraintHandles() const
	{
		return Handles;
	}

	const FConstraintBase& GetConstraint(int32 Index) const;

	FCollisionConstraintsArray& GetConstraintsArray() { return Constraints; }

protected:
	using Base::GetConstraintIndex;
	using Base::SetConstraintIndex;

	void UpdateConstraintMaterialProperties(FConstraintBase& Contact);

private:

	const TPBDRigidsSOAs<T,d>& Particles;

	FCollisionConstraintsArray Constraints;
	int32 NumActivePointConstraints;
	int32 NumActiveSweptPointConstraints;
	int32 NumActiveIterativeConstraints;

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
	TMap< FConstraintContainerHandleKey, FConstraintContainerHandle* > Manifolds;
#endif
	TArray<FConstraintContainerHandle*> Handles;
	FConstraintHandleAllocator HandleAllocator;

	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& MPhysicsMaterials;
	int32 MApplyPairIterations;
	int32 MApplyPushOutPairIterations;
	T MCullDistance;
	T MShapePadding;
	T MAngularFriction;
	bool bUseCCD;
	bool bEnableCollisions;
	bool bHandlesEnabled;
	ECollisionApplyType ApplyType;

	int32 LifespanCounter;

	TRigidBodyContactConstraintsPostApplyCallback<T, d> PostApplyCallback;
	TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d> PostApplyPushOutCallback;
};
}
