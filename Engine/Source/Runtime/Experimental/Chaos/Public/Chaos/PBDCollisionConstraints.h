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
class FPBDRigidsSOAs;

using FRigidBodyContactConstraintsPostComputeCallback = TFunction<void()>;
using FRigidBodyContactConstraintsPostApplyCallback = TFunction<void(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>&)>;
using FRigidBodyContactConstraintsPostApplyPushOutCallback = TFunction<void(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>&, bool)>;

namespace Collisions
{
	struct FContactParticleParameters;
	struct FContactIterationParameters;
}

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


	FPBDCollisionConstraints(const FPBDRigidsSOAs& InParticles, 
		TArrayCollectionArray<bool>& Collided, 
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PhysicsMaterials, 
		const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& PerParticlePhysicsMaterials, 
		const int32 ApplyPairIterations = 1, 
		const int32 ApplyPushOutPairIterations = 1, 
		const FReal RestitutionThreshold = 2000.0f);

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
	void SetSolverType(EConstraintSolverType InSolverType)
	{
		SolverType = InSolverType;
	}

	/**
	 * Helper object for efficiently appending constraints into the constraint container
	 * in a scope. The encapsulates the separation of appending the constraints into the
	 * owning container and building the handles required for them. Previously this was
	 * done one at a time, this helper lets us batch the operation to make it faster
	 *
	 * It's important not to mutate the owning container while this helper is alive
	 * otherwise it will not be able to append correctly to it.
	 */
	struct FConstraintAppendScope
	{
		FConstraintAppendScope() = delete;
		FConstraintAppendScope(const FConstraintAppendScope&) = delete;
		FConstraintAppendScope& operator=(const FConstraintAppendScope&) = delete;

		FConstraintAppendScope(FConstraintAppendScope&&) = default;
		FConstraintAppendScope& operator=(FConstraintAppendScope&&) = default;

		FConstraintAppendScope(FPBDCollisionConstraints* InOwner);
		~FConstraintAppendScope();

		// Reserves space for NumToAdd constraints in the internal container
		void ReserveSingle(int32 NumToAdd);
		void ReserveSingleSwept(int32 NumToAdd);

		// Append constraint lists to the internal container.
		// note this will move the container, it will no longer be valid after a call to Append.
		void Append(TArray<FRigidBodyPointContactConstraint>&& InConstraints);
		void Append(TArray<FRigidBodySweptPointContactConstraint>&& InConstraints);

	private:
		FPBDCollisionConstraints* Owner = nullptr;
		FCollisionConstraintsArray* Constraints = nullptr;

		// Tracking for how many constraints the container began with and how many
		// the helper added so we can build the new handles on scope exit
		int32 NumBeginSingle = 0;
		int32 NumBeginSingleSwept = 0;
		int32 NumAddedSingle = 0;
		int32 NumAddedSingleSwept = 0;
	};
	
	/** Begin an append operation, recieving a helper object for bulk operations on the constraint container */
	FConstraintAppendScope BeginAppendScope();

private:

	// Set whenever an append scope is constructed, and unset when destructed
	// and used to assert the container isn't mutated during appending.
	bool bInAppendOperation;

public:

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

	/**
	*  Reset the constraint frame. 
	*/
	void Reset();

	/**
	 * Apply modifiers to the constraints and specify which constraints should be disabled.
	 * You would probably call this in the PostComputeCallback. Prefer this to calling RemoveConstraints in a loop,
	 * so you don't have to worry about constraint iterator/indices changing.
	 */
	void ApplyCollisionModifier(const TArray<ISimCallbackObject*>& CollisionModifiers);


	/**
	* Remove the constraints associated with the ParticleHandle.
	*/
	void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>&  ParticleHandle);


	/**
	* Disable the constraints associated with the ParticleHandle.
	*/
	void DisableConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& ParticleHandle) {}


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


	//
	// General Rule API
	//

	void PrepareTick() {}

	void UnprepareTick() {}

	void PrepareIteration(FReal Dt);

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

	void SetCanDisableContacts(bool bInCanDisableContacts)
	{
		bCanDisableContacts = bInCanDisableContacts;
	}

	bool GetCanDisableContacts() const
	{
		return bCanDisableContacts;
	}

	void SetRestitutionThreshold(FReal InRestitutionThreshold)
	{
		RestitutionThreshold = InRestitutionThreshold;
	}

	FReal GetRestitutionThreshold() const
	{
		return RestitutionThreshold;
	}

	void SetPairIterations(int32 InPairIterations)
	{
		MApplyPairIterations = InPairIterations;
	}

	int32 GetPairIterations() const
	{
		return MApplyPairIterations;
	}

	void SetPushOutPairIterations(int32 InPairIterations)
	{
		MApplyPushOutPairIterations = InPairIterations;
	}

	int32 GetPushOutPairIterations() const
	{
		return MApplyPushOutPairIterations;
	}

	void SetCollisionsEnabled(bool bInEnableCollisions)
	{
		bEnableCollisions = bInEnableCollisions;
	}

	bool GetCollisionsEnabled() const
	{
		return bEnableCollisions;
	}

	void SetRestitutionEnabled(bool bInEnableRestitution)
	{
		bEnableRestitution = bInEnableRestitution;
	}

	bool GetRestitutionEnabled() const
	{
		return bEnableRestitution;
	}

	void SetGravity(const FVec3& InGravity)
	{
		GravityDir = InGravity.GetSafeNormal();
	}

	int32 NumConstraints() const
	{
		return Constraints.SinglePointConstraints.Num() + Constraints.SinglePointSweptConstraints.Num();
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

	//Sort constraints based on particle indices so that we have a deterministic solve order
	void SortConstraints();

protected:
	using Base::GetConstraintIndex;
	using Base::SetConstraintIndex;

	void UpdateConstraintMaterialProperties(FCollisionConstraintBase& Contact);

	Collisions::FContactParticleParameters GetContactParticleParameters(const FReal Dt);
	Collisions::FContactIterationParameters GetContactIterationParameters(const FReal Dt, const int32 Iteration, const int32 NumIterations, const int32 NumPairIterations, bool& bNeedsAnotherIteration);

private:

	friend FConstraintAppendScope;
	const FPBDRigidsSOAs& Particles;

	FCollisionConstraintsArray Constraints;
	int32 NumActivePointConstraints;
	int32 NumActiveSweptPointConstraints;

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
	TMap< FConstraintContainerHandleKey, FPBDCollisionConstraintHandle* > Manifolds;
#endif
	TArray<FPBDCollisionConstraintHandle*> Handles;
	FConstraintHandleAllocator HandleAllocator;

	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& MPhysicsMaterials;
	const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& MPerParticlePhysicsMaterials;
	int32 MApplyPairIterations;
	int32 MApplyPushOutPairIterations;
	FReal RestitutionThreshold;
	bool bUseCCD;
	bool bEnableCollisions;
	bool bEnableRestitution;
	bool bHandlesEnabled;

	// This is passed to IterationParameters. If true, then an iteration can cull a contact
	// permanently (ie, for the remaining iterations) if it is ignored due to culldistance.
	// This improves performance, but can decrease stability if contacts are culled prematurely.
	bool bCanDisableContacts;

	// Used by PushOut to decide on priority when two bodies are at same shock propagation level
	FVec3 GravityDir;

	EConstraintSolverType SolverType;

	int32 LifespanCounter;

	FRigidBodyContactConstraintsPostApplyCallback PostApplyCallback;
	FRigidBodyContactConstraintsPostApplyPushOutCallback PostApplyPushOutCallback;
};
}
