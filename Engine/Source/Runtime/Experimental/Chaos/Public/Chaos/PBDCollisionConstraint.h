// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ConstraintHandle.h"
#include "Chaos/PBDCollisionTypes.h"
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

template <typename T, int d>
class TImplicitObject;

template <typename T, int d>
class TBVHParticles;

template <typename T, int d>
class TBox;

template<class T>
class TChaosPhysicsMaterial;

template <typename T, int d>
class TPBDRigidsSOAs;

/** Specifies the type of work we should do*/
enum class ECollisionUpdateType
{
	Any,	//stop if we have at least one deep penetration. Does not compute location or normal
	Deepest	//find the deepest penetration. Compute location and normal
};

/** Return value of the collision modification callback */
enum class ECollisionModifierResult
{
	Unchanged,	/** No change to the collision */
	Modified,	/** Modified the collision, but want it to remain enabled */
	Disabled,	/** Collision should be disabled */
};

template<class T, int d>
class CHAOS_API TPBDCollisionConstraintHandle : public TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>
{
public:
	using Base = TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>;
	using FConstraintContainer = TPBDCollisionConstraint<T, d>;

	TPBDCollisionConstraintHandle();
	TPBDCollisionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);

	const TRigidBodyContactConstraint<T, d>& GetContact() const;
	void SetConstraintIndex(int32 IndexIn) { ConstraintIndex = IndexIn; }

protected:
	using Base::ConstraintIndex;
	using Base::ConstraintContainer;
};

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
class CHAOS_API TPBDCollisionConstraint : public TPBDConstraintContainer<T, d>
{
public:
	using Base = TPBDConstraintContainer<T, d>;
	friend class TPBDCollisionConstraintAccessor<T, d>;
	friend class TPBDCollisionConstraintHandle<T, d>;
	using FReal = T;
	static const int Dimensions = d;
	using FConstraintHandle = TPBDCollisionConstraintHandle<T, d>;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDCollisionConstraint<T, d>>;
	using FRigidBodyContactConstraint = TRigidBodyContactConstraint<T, d>;
	typedef TPair<TGeometryParticleHandle<T, d>*, const TGeometryParticleHandle<T, d>*> FConstraintHandleID;

	TPBDCollisionConstraint(const TPBDRigidsSOAs<T,d>& InParticles, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& PerParticleMaterials, const int32 PairIterations = 1, const T Thickness = (T)0);
	virtual ~TPBDCollisionConstraint() {}

	//
	// Constraint Container API
	//

	void SetThickness(T InThickness)
	{
		MThickness = InThickness;
	}

	void SetVelocitySolveEnabled(bool bInEnableVelocitySolve)
	{
		bEnableVelocitySolve = bInEnableVelocitySolve;
	}

	void SetPushOutPairIterations(int32 InPairIterations)
	{ 
		MPairIterations = InPairIterations;
	}

	void SetCollisionsEnabled(bool bInEnableCollisions)
	{
		bEnableCollisions = bInEnableCollisions;
	}

	/**
	 * Get the number of constraints.
	 */
	int32 NumConstraints() const
	{
		return Constraints.Num();
	}

	FConstraintHandleID GetConstraintHandleID(const FRigidBodyContactConstraint & Constraint) const
	{
		return  FConstraintHandleID( Constraint.Particle, Constraint.Levelset );
	}

	FConstraintHandleID GetConstraintHandleID(int32 ConstraintIndex) const
	{
		return  FConstraintHandleID( Constraints[ConstraintIndex].Particle, Constraints[ConstraintIndex].Levelset );
	}
		
	const FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex) const
	{
		return Handles[GetConstraintHandleID(ConstraintIndex)];
	}

	FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex)
	{
		return Handles[ GetConstraintHandleID(ConstraintIndex) ];
	}

	void RemoveConstraint(int32 Idx);

	/**
	* Remove the constraints associated with the ParticleHandle.
	*/
	void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>&  ParticleHandle);

	/**
	 * Apply a modifier to the constraints and specify which constraints should be disabled.
	 * You would probably call this in the PostComputeCallback. Prefer this to calling RemoveConstraints in a loop, 
	 * so you don't have to worry about constraint iterator/indices changing.
	 */
	void ApplyCollisionModifier(const TFunction<ECollisionModifierResult(FRigidBodyContactConstraint& Constraint)>& CollisionModifier);

	/**
	 * Set the callback used just after contacts are generated at the start of a frame tick.
	 * This can be used to modify or disable constraints (via ApplyCollisionModifier).
	 */
	void SetPostComputeCallback(const TRigidBodyContactConstraintsPostComputeCallback<T, d>& Callback);

	/**
	 * Remove the constraint callback.
	 */
	void ClearPostComputeCallback();

	void SetPostApplyCallback(const TRigidBodyContactConstraintsPostApplyCallback<T, d>& Callback);
	void ClearPostApplyCallback();

	void SetPostApplyPushOutCallback(const TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d>& Callback);
	void ClearPostApplyPushOutCallback();

	//
	// Constraint API
	//

	/**
	 * Get the particles that are affected by the specified constraint.
	 */
	TVector<TGeometryParticleHandle<T, d>*, 2> GetConstrainedParticles(int32 ConstraintIndex) const
	{
		return { Constraints[ConstraintIndex].Particle, Constraints[ConstraintIndex].Levelset };
	}


	//
	// Island Rule API
	//

	/**
	 * Apply corrections for the specified list of constraints.
	 */
	 // @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	void Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);

	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ const T Dt);

	/** 
	 * Apply push out for the specified list of constraints.
	 * Return true if we need another iteration 
	 */
	 // @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	bool ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const TSet<TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations);

	void UpdateConstraints(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles);

	const TArray<FRigidBodyContactConstraint>& GetAllConstraints() const { return Constraints; }

	//using FAccelerationStructure = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;
	//using FAccelerationStructure = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;
	using FAccelerationStructure = ISpatialAcceleration< TAccelerationStructureHandle<T, d>, T, d>;
	void SetSpatialAcceleration(const FAccelerationStructure* InSpatialAcceleration) { SpatialAcceleration = InSpatialAcceleration; }

	// @todo(ccaulfield) We should probably wrap the LevelsetConstraint functions in a utility class and remove them from here.
	// They are currently public for the Linux build, and member functions for Headless Chaos (TPBDCollisionConstraintAccessor). 
	template<ECollisionUpdateType>
	static void UpdateLevelsetConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint);

	/**
	 * Get the particles that are affected by the specified constraint.
	 */
	TVector<TGeometryParticleHandle<T, d>*, 2> ConstraintParticles(int32 ContactIndex) const { return { Constraints[ContactIndex].Particle, Constraints[ContactIndex].Levelset }; }

	//NOTE: this should not be called by anyone other than ISpatialAccelerationCollection and CollisionConstraints - todo: make private with friends?
	template <bool bGatherStats, typename SPATIAL_ACCELERATION>
	void ComputeConstraintsHelperLowLevel(const SPATIAL_ACCELERATION& SpatialAcceleration, T Dt);
	
protected:
	using Base::GetConstraintIndex;
	using Base::SetConstraintIndex;

private:
	void Reset(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices*/);

	void Apply(const T Dt, FRigidBodyContactConstraint& Constraint);
	void ApplyPushOut(const T Dt, FRigidBodyContactConstraint& Constraint, const TSet<TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations, bool& NeedsAnotherIteration);

	template <bool bGatherStats = false>
	void ComputeConstraints(const FAccelerationStructure& AccelerationStructure, T Dt);

	template<ECollisionUpdateType>
	void UpdateConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint);

	FRigidBodyContactConstraint ComputeConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness);


	template<typename SPATIAL_ACCELERATION>
	void UpdateConstraintsHelper(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles, SPATIAL_ACCELERATION& SpatialAcceleration);

	const TPBDRigidsSOAs<T,d>& Particles;

	// @todo(ccaulfield): move spatial acceleration out of constraint container and make shareable
	const FAccelerationStructure* SpatialAcceleration;

	TArray<FRigidBodyContactConstraint> Constraints;
	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& MPhysicsMaterials;
	bool bEnableVelocitySolve;
	int32 MPairIterations;
	T MThickness;
	T MAngularFriction;
	bool bUseCCD;
	bool bEnableCollisions;
	int32 LifespanCounter;

	TRigidBodyContactConstraintsPostComputeCallback<T, d> PostComputeCallback;
	TRigidBodyContactConstraintsPostApplyCallback<T, d> PostApplyCallback;
	TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d> PostApplyPushOutCallback;

	TMap<FConstraintHandleID, FConstraintHandle*> Handles;
	FConstraintHandleAllocator HandleAllocator;
};

extern template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<false>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);
extern template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<true>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);
}
