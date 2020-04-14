// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Collision/PBDCollisionConstraintHandle.h"
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
class FCollisionConstraintBase;
class FImplicitObject;
class FPBDCollisionConstraints;
class FRigidBodyPointContactConstraint;
class FRigidBodyMultiPointContactConstraint;

template <typename T, int d>
class TPBDRigidsSOAs;

using FRigidBodyContactConstraintsPostComputeCallback = TFunction<void()>;
using FRigidBodyContactConstraintsPostApplyCallback = TFunction<void(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>&)>;
using FRigidBodyContactConstraintsPostApplyPushOutCallback = TFunction<void(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>&, bool)>;

/**
 * A container and solver for collision constraints.
 */
class CHAOS_API FPBDCollisionConstraints : public FPBDConstraintContainer
{
public:
	friend class FPBDCollisionConstraintHandle;

	using Base = FPBDConstraintContainer;
	using FHandles = TArray<FPBDCollisionConstraintHandle*>;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDCollisionConstraints>;
	using FConstraintContainerHandleKey = typename FPBDCollisionConstraintHandle::FHandleKey;

	// For use by dependent types
	using FPointContactConstraint = FRigidBodyPointContactConstraint;
	using FConstraintContainerHandle = FPBDCollisionConstraintHandle;


	FPBDCollisionConstraints(const TPBDRigidsSOAs<FReal, 3>& InParticles, 
		TArrayCollectionArray<bool>& Collided, 
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterials, 
		const int32 ApplyPairIterations = 1, const int32 ApplyPushOutPairIterations = 1, const FReal CullDistance = (FReal)0, const FReal ShapePadding = (FReal)0);

	virtual ~FPBDCollisionConstraints() {}

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
	void AddConstraint(const FRigidBodyPointContactConstraint& InConstraint);
	void AddConstraint(const FRigidBodySweptPointContactConstraint& InConstraint);
	void AddConstraint(const FRigidBodyMultiPointContactConstraint& InConstraint);

	/**
	*  Reset the constraint frame. 
	*/
	void Reset();

	/**
	 * Apply a modifier to the constraints and specify which constraints should be disabled.
	 * You would probably call this in the PostComputeCallback. Prefer this to calling RemoveConstraints in a loop,
	 * so you don't have to worry about constraint iterator/indices changing.
	 */
	void ApplyCollisionModifier(const FCollisionModifierCallback& CollisionModifier);


	/**
	* Remove the constraints associated with the ParticleHandle.
	*/
	void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>&  ParticleHandle);


	/**
	* Remove the constraint, update the handle, and any maps. 
	*/
	void RemoveConstraint(FPBDCollisionConstraintHandle* ConstraintHandle);


	/**
	 * Update all constraint values within the set
	 */
	void UpdateConstraints(FReal Dt, const TSet<TGeometryParticleHandle<FReal, 3>*>& AddedParticles);

	/**
	 * Update all constraint values
	 */

	 /**
	 * Update all constraint values
	 */
	void UpdateConstraints(FReal Dt);

	/**
	* Update all contact manifolds
	*/
	void UpdateManifolds(FReal Dt);


	//
	// General Rule API
	//

	void PrepareTick() {}

	void UnprepareTick() {}

	void PrepareIteration(FReal Dt) {}

	void UnprepareIteration(FReal Dt) {}

	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(const FReal Dt);

	//
	// Simple Rule API
	//

	bool Apply(const FReal Dt, const int32 It, const int32 NumIts);
	bool ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts);

	//
	// Island Rule API
	//
	// @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule

	bool Apply(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);
	bool ApplyPushOut(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles, 
		const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations);


	/**
	 *  Callbacks
	 */
	void SetPostApplyCallback(const FRigidBodyContactConstraintsPostApplyCallback& Callback);
	void ClearPostApplyCallback();

	void SetPostApplyPushOutCallback(const FRigidBodyContactConstraintsPostApplyPushOutCallback& Callback);
	void ClearPostApplyPushOutCallback();


	//
	// Member Access
	//

	const TArray<FPBDCollisionConstraintHandle*>& GetAllConstraintHandles() const 
	{ 
		return Handles; 
	}

	bool Contains(const FCollisionConstraintBase* Base) const
	{
#if CHAOS_COLLISION_PERSISTENCE_ENABLED
		return Manifolds.Contains(FPBDCollisionConstraintHandle::MakeKey(Base));
#else
		return false;
#endif
	}

	// @todo(chaos): remove
	//void SetThickness(FReal InThickness)
	//{
	//	MCullDistance = InThickness;
	//}

	void SetCullDistance(FReal InCullDistance)
	{
		MCullDistance = InCullDistance;
	}

	FReal GetCullDistance() const
	{
		return MCullDistance;
	}

	void SetShapePadding(FReal InShapePadding)
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

	const FCollisionConstraintBase& GetConstraint(int32 Index) const;

	FCollisionConstraintsArray& GetConstraintsArray() { return Constraints; }

protected:
	using Base::GetConstraintIndex;
	using Base::SetConstraintIndex;

	void UpdateConstraintMaterialProperties(FCollisionConstraintBase& Contact);

private:

	const TPBDRigidsSOAs<FReal, 3>& Particles;

	FCollisionConstraintsArray Constraints;
	int32 NumActivePointConstraints;
	int32 NumActiveSweptPointConstraints;
	int32 NumActiveIterativeConstraints;

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
	TMap< FConstraintContainerHandleKey, FPBDCollisionConstraintHandle* > Manifolds;
#endif
	TArray<FPBDCollisionConstraintHandle*> Handles;
	FConstraintHandleAllocator HandleAllocator;

	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& MPhysicsMaterials;
	int32 MApplyPairIterations;
	int32 MApplyPushOutPairIterations;
	FReal MCullDistance;
	FReal MShapePadding;
	FReal MAngularFriction;
	bool bUseCCD;
	bool bEnableCollisions;
	bool bHandlesEnabled;
	ECollisionApplyType ApplyType;

	int32 LifespanCounter;

	FRigidBodyContactConstraintsPostApplyCallback PostApplyCallback;
	FRigidBodyContactConstraintsPostApplyPushOutCallback PostApplyPushOutCallback;
};
}
