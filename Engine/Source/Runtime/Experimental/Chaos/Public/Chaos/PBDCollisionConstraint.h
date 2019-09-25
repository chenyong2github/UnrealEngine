// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/BoundingVolume.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Framework/BufferedData.h"

#include <memory>
#include <queue>
#include <sstream>
#include "BoundingVolume.h"

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


template<class T, int d>
class CHAOS_API TPBDCollisionConstraintHandle : public TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>
{
public:
	using Base = TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>;
	using FConstraintContainer = TPBDCollisionConstraint<T, d>;

	TPBDCollisionConstraintHandle() {}
	TPBDCollisionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>(InConstraintContainer, InConstraintIndex) {}

protected:
	using Base::ConstraintIndex;
	using Base::ConstraintContainer;
};

/** Wrapper that holds both physics thread data and GT data. It's possible that the physics handle is null if we're doing operations entirely on external threads*/
template <typename T, int d>
class CHAOS_API TAccelerationStructureHandle
{
public:
	TAccelerationStructureHandle(TGeometryParticleHandle<T, d>* InHandle)
		: ExternalGeometryParticle(InHandle->GTGeometryParticle())
		, GeometryParticleHandle(InHandle)
	{
	}

	TAccelerationStructureHandle(TGeometryParticle<T, d>* InGeometryParticle)
		: ExternalGeometryParticle(InGeometryParticle)
		, GeometryParticleHandle(nullptr)
	{
	}

	//Should only be used by GT and scene query threads where an appropriate lock has been acquired
	TGeometryParticle<T, d>* GetExternalGeometryParticle_ExternalThread() const { return ExternalGeometryParticle; }

	//Should only be used by PT
	TGeometryParticleHandle<T, d>* GetGeometryParticleHandle_PhysicsThread() const { return GeometryParticleHandle; }

	bool operator==(const TAccelerationStructureHandle<T, d>& Rhs) const
	{
		return ExternalGeometryParticle == Rhs.ExternalGeometryParticle;
	}

	bool operator<(const TAccelerationStructureHandle<T, d>& Rhs) const
	{
		return  ExternalGeometryParticle < Rhs.ExternalGeometryParticle;
	}

	uint32 GetTypeHash() const
	{
		return ::GetTypeHash((void*)ExternalGeometryParticle);
	}

private:
	TGeometryParticle<T, d>* ExternalGeometryParticle;
	TGeometryParticleHandle<T, d>* GeometryParticleHandle;
};

template <typename T, int d>
uint32 GetTypeHash(const TAccelerationStructureHandle<T,d>& A)
{
	return A.GetTypeHash();
}

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
	using FReal = T;
	static const int Dimensions = d;
	using FConstraintHandle = TPBDCollisionConstraintHandle<T, d>;
	using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBDCollisionConstraint<T, d>>;
	using FRigidBodyContactConstraint = TRigidBodyContactConstraint<T, d>;

	TPBDCollisionConstraint(const TPBDRigidsSOAs<T,d>& InParticles, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& PerParticleMaterials, const int32 PairIterations = 1, const T Thickness = (T)0);
	virtual ~TPBDCollisionConstraint() {}

	//
	// Constraint Container API
	//

	/**
	 * Get the number of constraints.
	 */
	int32 NumConstraints() const
	{
		return Constraints.Num();
	}

	const FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex) const
	{
		return Handles[ConstraintIndex];
	}

	FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex)
	{
		return Handles[ConstraintIndex];
	}

	// @todo(ccaulfield): rename/remove
	void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles);

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
	void Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles);

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

	static bool NearestPoint(TArray<Pair<TVector<T, d>, TVector<T, d>>>& Points, TVector<T, d>& Direction, TVector<T, d>& ClosestPoint);

	const TArray<FRigidBodyContactConstraint>& GetAllConstraints() const { return Constraints; }
	const ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T, d>& GetSpatialAcceleration() const { return SpatialAccelerationResource.GetRead(); }
	void ReleaseSpatialAcceleration() const;
	void SwapSpatialAcceleration();

	// @todo(ccaulfield) We should probably wrap the LevelsetConstraint functions in a utility class and remove them from here.
	// They are currently public for the Linux build, and member functions for Headless Chaos (TPBDCollisionConstraintAccessor). 
	template<ECollisionUpdateType>
	static void UpdateLevelsetConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<ECollisionUpdateType>
	static void UpdateLevelsetConstraintGJK(const T Thickness, FRigidBodyContactConstraint& Constraint);

	/**
	 * Get the particles that are affected by the specified constraint.
	 */
	TVector<TGeometryParticleHandle<T, d>*, 2> ConstraintParticles(int32 ContactIndex) const { return { Constraints[ContactIndex].Particle, Constraints[ContactIndex].Levelset }; }

	void SetPushOutPairIterations(int32 InPushOutPairIterations) { MPairIterations = InPushOutPairIterations; }

	/** Make a copy of the acceleration structure to allow for external modification. This is needed for supporting sync operations on SQ structure from game thread */
	TUniquePtr<ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T, d>> CreateExternalAccelerationStructure();
	
protected:
	using Base::GetConstraintIndex;
	using Base::SetConstraintIndex;

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
	TChaosReadWriteResource<TBoundingVolume<TGeometryParticles<T, d>, TAccelerationStructureHandle<T,d>, T, d>> SpatialAccelerationResource;

	TArray<FRigidBodyContactConstraint> Constraints;
	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& MPhysicsMaterials;
	int32 MPairIterations;
	T MThickness;
	T MAngularFriction;
	bool bUseCCD;

	TArray<FConstraintHandle*> Handles;
	FConstraintHandleAllocator HandleAllocator;
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
