// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/BoundingVolume.h"
#include "Chaos/PBDCollisionTypes.h"
#include "Framework/BufferedData.h"
#include "Chaos/PBDCollisionConstraint.h"

#include <memory>
#include <queue>
#include <sstream>
#include "BoundingVolume.h"

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

template <typename T, int d>
class TPBDRigidsSOAs;

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

	TPBDCollisionConstraint(const TPBDRigidsSOAs<T,d>& InParticles, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& PerParticleMaterials, const int32 PairIterations = 1, const T Thickness = (T)0);
	virtual ~TPBDCollisionConstraint() {}

	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ const T Dt);

	/**
	 * Apply corrections for the specified list of constraints.
	 */
	// @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	void Apply(const T Dt, const TArray<int32>& InConstraintIndices);

	/** 
	 * Apply push out for the specified list of constraints.
	 * Return true if we need another iteration 
	 */
	 // @todo(ccaulfield): this runs wide. The serial/parallel decision should be in the ConstraintRule
	bool ApplyPushOut(const T Dt, const TArray<int32>& InConstraintIndices, const TSet<TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations);

	void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles);
	void UpdateConstraints(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles);

	static bool NearestPoint(TArray<Pair<TVector<T, d>, TVector<T, d>>>& Points, TVector<T, d>& Direction, TVector<T, d>& ClosestPoint);

	const TArray<FRigidBodyContactConstraint>& GetAllConstraints() const { return Constraints; }
	const ISpatialAcceleration<TGeometryParticleHandle<T, d>*, T, d>& GetSpatialAcceleration() const { return SpatialAccelerationResource.GetRead(); }
	void ReleaseSpatialAcceleration() const;
	void SwapSpatialAcceleration();

	// @todo(ccaulfield) We should probably wrap the LevelsetConstraint functions in a utility class and remove them from here.
	// They are currently public for the Linux build, and member functions for Headless Chaos (TPBDCollisionConstraintAccessor). 
	template<ECollisionUpdateType>
	static void UpdateLevelsetConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<ECollisionUpdateType>
	static void UpdateLevelsetConstraintGJK(const T Thickness, FRigidBodyContactConstraint& Constraint);


	int32 NumConstraints() const { return Constraints.Num(); }

	/**
	 * Get the particles that are affected by the specified constraint.
	 */
	TVector<TGeometryParticleHandle<T, d>*, 2> ConstraintParticles(int32 ContactIndex) const { return { Constraints[ContactIndex].Particle, Constraints[ContactIndex].Levelset }; }

	void SetPushOutPairIterations(int32 InPushOutPairIterations) { MPairIterations = InPushOutPairIterations; }

private:
	void Reset(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices*/);

	void Apply(const T Dt, FRigidBodyContactConstraint& Constraint);
	void ApplyPushOut(const T Dt, FRigidBodyContactConstraint& Constraint, const TSet<TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations, bool& NeedsAnotherIteration);

	template <bool bGatherStats = false>
	void ComputeConstraints(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ T Dt);

	template<ECollisionUpdateType>
	void UpdateConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint);

	FRigidBodyContactConstraint ComputeConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness);

	template <typename SPATIAL_ACCELERATION, bool bGatherStats>
	void ComputeConstraintsHelper(T Dt, const SPATIAL_ACCELERATION& SpatialAcceleration);

	template<typename SPATIAL_ACCELERATION>
	void UpdateConstraintsHelper(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles, SPATIAL_ACCELERATION& SpatialAcceleration);

	// @todo(ccaulfield): move spatial acceleration out of constraint container and make shareable
#if CHAOS_PARTICLEHANDLE_TODO
	TChaosReadWriteResource<TBoundingVolume<TPBDRigidParticles<T, d>, T, d>> SpatialAccelerationResource;
	TChaosReadWriteResource<TBoundingVolumeHierarchy<TPBDRigidParticles<T, d>, TBoundingVolume<TPBDRigidParticles<T, d>, T, d>, T, d>> SpatialAccelerationResource2;
#endif

	const TPBDRigidsSOAs<T,d>& Particles;
	TChaosReadWriteResource<TBoundingVolume<TGeometryParticles<T, d>, TGeometryParticleHandle<T,d>*, T, d>> SpatialAccelerationResource;

	TArray<FRigidBodyContactConstraint> Constraints;
	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& MPhysicsMaterials;
	int32 MPairIterations;
	T MThickness;
	T MAngularFriction;
	bool bUseCCD;
};

extern template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraintGJK<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraintGJK<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
extern template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<false>(float Dt);
extern template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<true>(float Dt);
}
