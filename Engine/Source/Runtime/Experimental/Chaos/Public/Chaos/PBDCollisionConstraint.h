// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/BoundingVolume.h"
#include "Chaos/PBDCollisionTypes.h"
#include "Framework/BufferedData.h"

#include <memory>
#include <queue>
#include <sstream>

namespace Chaos
{
template<typename T, int d>
class TPBDCollisionConstraintAccessor;

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

/** Specifies the type of work we should do*/
enum class ECollisionUpdateType
{
	Any,	//stop if we have at least one deep penetration. Does not compute location or normal
	Deepest	//find the deepest penetration. Compute location and normal
};

/**
 * Manages a set of contact constraints:
 *	- Performs collision detection to generate constraints.
 *	- Responsible for applying corrections to particles affected by the constraints.
 * @todo(ccaulfield): rename to TPBDCollisionConstraints
 */
template<typename T, int d>
class CHAOS_API TPBDCollisionConstraint
{
  public:
	friend class TPBDCollisionConstraintAccessor<T, d>;

	typedef TRigidBodyContactConstraint<T, d> FRigidBodyContactConstraint;

	TPBDCollisionConstraint(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& PerParticleMaterials, const int32 PairIterations = 1, const T Thickness = (T)0);
	virtual ~TPBDCollisionConstraint() {}

	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const T Dt);

	/**
	 * Apply corrections for the specified list of constraints.
	 */
	// @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices);

	/** 
	 * Apply push out for the specified list of constraints.
	 * Return true if we need another iteration 
	 */
	 // @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	bool ApplyPushOut(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices, const TSet<int32>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations);

	void RemoveConstraints(const TSet<uint32>& RemovedParticles);
	void UpdateConstraints(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, T Dt, const TSet<uint32>& AddedParticles, const TArray<uint32>& ActiveParticles);

	static bool NearestPoint(TArray<Pair<TVector<T, d>, TVector<T, d>>>& Points, TVector<T, d>& Direction, TVector<T, d>& ClosestPoint);

	const TArray<FRigidBodyContactConstraint>& GetAllConstraints() const { return Constraints; }
	const ISpatialAcceleration<T, d>& GetSpatialAcceleration() const;// { return SpatialAccelerationResource.GetRead(); }
	void ReleaseSpatialAcceleration() const;
	void SwapSpatialAcceleration();

	// @todo(ccaulfield) We should probably wrap the LevelsetConstraint functions in a utility class and remove them from here.
	// They are currently public for the Linux build, and member functions for Headless Chaos (TPBDCollisionConstraintAccessor). 
	template<ECollisionUpdateType, class T_PARTICLES>
	static void UpdateLevelsetConstraint(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint);
	template<ECollisionUpdateType, class T_PARTICLES>
	static void UpdateLevelsetConstraintGJK(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint);


	int32 NumConstraints() const { return Constraints.Num(); }

	/**
	 * Get the particles that are affected by the specified constraint.
	 */
	TVector<int32, 2> ConstraintParticleIndices(int32 ContactIndex) const { return { Constraints[ContactIndex].ParticleIndex, Constraints[ContactIndex].LevelsetIndex }; }

	void SetPushOutPairIterations(int32 InPushOutPairIterations) { MPairIterations = InPushOutPairIterations; }

private:
	void Reset(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices);

	template <bool bGatherStats = false>
	void ComputeConstraints(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, T Dt);

	template<ECollisionUpdateType, class T_PARTICLES>
	void UpdateConstraint(const T_PARTICLES& InParticles, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint);

	FRigidBodyContactConstraint ComputeConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 Body1Index, int32 Body2Index, const T Thickness);

	template <typename SPATIAL_ACCELERATION, bool bGatherStats>
	void ComputeConstraintsHelper(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, T Dt, const SPATIAL_ACCELERATION& SpatialAcceleration);

	template<typename SPATIAL_ACCELERATION>
	void UpdateConstraintsHelper(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, T Dt, const TSet<uint32>& AddedParticles, const TArray<uint32>& ActiveParticles, SPATIAL_ACCELERATION& SpatialAcceleration);

	// @todo(ccaulfield): move spatial acceleration out of constraint container and make shareable
	TChaosReadWriteResource<TBoundingVolume<TPBDRigidParticles<T, d>, T, d>> SpatialAccelerationResource;
	TChaosReadWriteResource<TBoundingVolumeHierarchy<TPBDRigidParticles<T, d>, TBoundingVolume<TPBDRigidParticles<T, d>, T, d>, T, d>> SpatialAccelerationResource2;

	TArray<FRigidBodyContactConstraint> Constraints;
	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& MPhysicsMaterials;
	int32 MPairIterations;
	T MThickness;
	T MAngularFriction;
	bool bUseCCD;
};
}
