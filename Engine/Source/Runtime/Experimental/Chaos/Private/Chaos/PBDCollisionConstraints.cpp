// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsImp.h"

#include "Chaos/Capsule.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ISpatialAccelerationCollection.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDCollisionConstraintsPointContactUtil.h"
#include "Chaos/PBDCollisionConstraintsPlaneContactUtil.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/ScopedTimers.h"

#if INTEL_ISPC
#include "PBDCollisionConstraints.ispc.generated.h"
#endif

//#pragma optimize("", off)

namespace Chaos
{
	extern int32 UseLevelsetCollision;

	int32 CollisionParticlesBVHDepth = 4;
	FAutoConsoleVariableRef CVarCollisionParticlesBVHDepth(TEXT("p.CollisionParticlesBVHDepth"), CollisionParticlesBVHDepth, TEXT("The maximum depth for collision particles bvh"));

	int32 ConstraintBPBVHDepth = 2;
	FAutoConsoleVariableRef CVarConstraintBPBVHDepth(TEXT("p.ConstraintBPBVHDepth"), ConstraintBPBVHDepth, TEXT("The maximum depth for constraint bvh"));

	int32 BPTreeOfGrids = 1;
	FAutoConsoleVariableRef CVarBPTreeOfGrids(TEXT("p.BPTreeOfGrids"), BPTreeOfGrids, TEXT("Whether to use a seperate tree of grids for bp"));

	float CollisionVelocityInflationCVar = 2.0f;
	FAutoConsoleVariableRef CVarCollisionVelocityInflation(TEXT("p.CollisionVelocityInflation"), CollisionVelocityInflationCVar, TEXT("Collision velocity inflation.[def:2.0]"));

	float CollisionFrictionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionFrictionOverride(TEXT("p.CollisionFriction"), CollisionFrictionOverride, TEXT("Collision friction for all contacts if >= 0"));

	CHAOS_API int32 EnableCollisions = 1;
	FAutoConsoleVariableRef CVarEnableCollisions(TEXT("p.EnableCollisions"), EnableCollisions, TEXT("Enable/Disable collisions on the Chaos solver."));

#if !UE_BUILD_SHIPPING
	int32 CHAOS_API PendingHierarchyDump = 0;
#endif

	DEFINE_STAT(STAT_ComputeConstraints);
	DEFINE_STAT(STAT_ComputeConstraintsSU);

	//
	// Collision Constraint Container
	//

	template<typename T, int d>
	TPBDCollisionConstraints<T, d>::TPBDCollisionConstraints(
		const TPBDRigidsSOAs<T, d>& InParticles,
		TArrayCollectionArray<bool>& Collided,
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& InPerParticleMaterials,
		const int32 InApplyPairIterations /*= 1*/,
		const int32 InApplyPushOutPairIterations /*= 1*/,
		const T Thickness /*= (T)0*/)
		: Particles(InParticles)
		, MCollided(Collided)
		, MPhysicsMaterials(InPerParticleMaterials)
		, MApplyPairIterations(InApplyPairIterations)
		, MApplyPushOutPairIterations(InApplyPushOutPairIterations)
		, MThickness(Thickness)
		, MAngularFriction(0)
		, bUseCCD(false)
		, bEnableCollisions(true)
		, LifespanCounter(0)
		, CollisionVelocityInflation(CollisionVelocityInflationCVar)
		, PostComputeCallback(nullptr)
		, PostApplyCallback(nullptr)
		, PostApplyPushOutCallback(nullptr)
		, SpatialAcceleration(nullptr)
	{
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::SetPostComputeCallback(const TRigidBodyContactConstraintsPostComputeCallback<T, d>& Callback)
	{
		PostComputeCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::ClearPostComputeCallback()
	{
		PostComputeCallback = nullptr;
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::SetPostApplyCallback(const TRigidBodyContactConstraintsPostApplyCallback<T, d>& Callback)
	{
		PostApplyCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::ClearPostApplyCallback()
	{
		PostApplyCallback = nullptr;
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::SetPostApplyPushOutCallback(const TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d>& Callback)
	{
		PostApplyPushOutCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::ClearPostApplyPushOutCallback()
	{
		PostApplyPushOutCallback = nullptr;
	}


	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::UpdatePositionBasedState(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ const T Dt)
	{
		Reset();

		if (bEnableCollisions)
		{
#if !UE_BUILD_SHIPPING
			if (PendingHierarchyDump)
			{
				ComputeConstraints<true>(*SpatialAcceleration, Dt);
			}
			else
#endif
			{
				ComputeConstraints(*SpatialAcceleration, Dt);
			}
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::ConstructConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
	{
		if (ensure(Particle0 && Particle1))
		{
			Collisions::ConstructConstraintsImpl<T, d>(Particle0, Particle1, Particle0->Geometry().Get(), Particle1->Geometry().Get(), Collisions::GetTransform(Particle0), Collisions::GetTransform(Particle1), Thickness, NewConstraints);
		}

	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::Reset"), STAT_CollisionConstraintsReset, STATGROUP_Chaos);
	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::Reset()
	{
		SCOPE_CYCLE_COUNTER(STAT_CollisionConstraintsReset);

		TArray<FConstraintContainerHandle*> CopyOfHandles = Handles;

		for (FConstraintContainerHandle* ContactHandle : CopyOfHandles)
		{
			//if (!bEnableCollisions)
			{
				RemoveConstraint(ContactHandle);
			}
		}


		MAngularFriction = 0;
		bUseCCD = false;
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::ApplyCollisionModifier(const TFunction<ECollisionModifierResult(const FConstraintContainerHandle* Handle)>& CollisionModifier)
	{
		TArray<FConstraintContainerHandle*> CopyOfHandles = Handles;

		for (FConstraintContainerHandle* ContactHandle : CopyOfHandles)
		{
			ECollisionModifierResult Result = CollisionModifier(ContactHandle);
			if (Result == ECollisionModifierResult::Disabled)
			{
				RemoveConstraint(ContactHandle);
			}
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>&  InHandleSet)
	{
		const TArray<TGeometryParticleHandle<T, d>*> HandleArray = InHandleSet.Array();
		for (auto ParticleHandle : HandleArray)
		{
			TArray<FConstraintContainerHandle*> CopyOfHandles = Handles;

			for (FConstraintContainerHandle* ContactHandle : CopyOfHandles)
			{
				TVector<TGeometryParticleHandle<T, d>*, 2> ConstraintParticles = ContactHandle->GetConstrainedParticles();
				if (ConstraintParticles[1] == ParticleHandle || ConstraintParticles[0] == ParticleHandle)
				{
					RemoveConstraint(ContactHandle);
				}
			}
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::RemoveConstraint(FConstraintContainerHandle* Handle)
	{
		int32 Idx = Handle->GetConstraintIndex();
		typename TCollisionConstraintBase<T, d>::FType ConstraintType = Handle->GetType();

		Handles.RemoveAtSwap(Idx);
		PointConstraints.RemoveAtSwap(Idx);
		if (Idx < PointConstraints.Num())
		{
			Handles[Idx]->SetConstraintIndex(Idx, ConstraintType);
		}

		ensure(Handles.Num() == PointConstraints.Num());
		HandleAllocator.FreeHandle(Handle);
	}



	template<typename T, int d>
	template <bool bGatherStats>
	void TPBDCollisionConstraints<T, d>::ComputeConstraints(const FAccelerationStructure& AccelerationStructure, T Dt)
	{
		if (const auto AABBTree = AccelerationStructure.template As<TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>>())
		{
			ComputeConstraintsHelperLowLevel<bGatherStats>(*AABBTree, Dt);
		}
		else if (const auto BV = AccelerationStructure.template As<TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>>())
		{
			ComputeConstraintsHelperLowLevel<bGatherStats>(*BV, Dt);
		}
		else if (const auto AABBTreeBV = AccelerationStructure.template As<TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>>())
		{
			ComputeConstraintsHelperLowLevel<bGatherStats>(*AABBTreeBV, Dt);
		}
		else if (const auto Collection = AccelerationStructure.template As<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>())
		{
			if (bGatherStats)
			{
				Collection->PBDComputeConstraintsLowLevel_GatherStats(*this, Dt);
			}
			else
			{
				Collection->PBDComputeConstraintsLowLevel(*this, Dt);
			}
		}
		else
		{
			check(false);  //question: do we want to support a dynamic dispatch version?
		}

		if (PostComputeCallback != nullptr)
		{
			PostComputeCallback();
		}
	}


	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::UpdateConstraints(T Dt, const TSet<TGeometryParticleHandle<T, d>*>& ParticlesSet)
	{
		// Clustering uses update constraints to force a re-evaluation. 
	}


	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::Apply"), STAT_Apply, STATGROUP_Chaos);
	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Apply);
		if (MApplyPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);

				if (ConstraintHandle->GetType() == FPointContactConstraint::StaticType())
				{
					Collisions::TPointContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
					Collisions::TPointContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, nullptr };
					Collisions::Apply(ConstraintHandle->GetPointContact(), MThickness, IterationParameters, ParticleParameters);
				}
				else if (ConstraintHandle->GetType() == FPlaneContactConstraint::StaticType())
				{
					Collisions::TPlaneContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
					Collisions::TPlaneContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, nullptr };
					Collisions::Apply(ConstraintHandle->GetPlaneContact(), MThickness, IterationParameters, ParticleParameters);
				}
				else
				{
					ensureMsgf(false, TEXT("Invalid constraint type"));
				}

			}, bDisableCollisionParallelFor);
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, InConstraintHandles);
		}
	}


	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::ApplyPushOut"), STAT_ApplyPushOut, STATGROUP_Chaos);
	template<typename T, int d>
	bool TPBDCollisionConstraints<T, d>::ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const TSet< const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_ApplyPushOut);

		bool NeedsAnotherIteration = false;
		if (MApplyPushOutPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex)
			{
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);

				if (ConstraintHandle->GetType() == FPointContactConstraint::StaticType())
				{
					Collisions::TPointContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
					Collisions::TPointContactIterationParameters<T> IterationParameters = { Dt, Iteration, NumIterations, MApplyPushOutPairIterations, &NeedsAnotherIteration };
					Collisions::ApplyPushOut(ConstraintHandle->GetPointContact(), MThickness, IsTemporarilyStatic, IterationParameters, ParticleParameters);
				}
				else if (ConstraintHandle->GetType() == FPlaneContactConstraint::StaticType())
				{
					Collisions::TPlaneContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
					Collisions::TPlaneContactIterationParameters<T> IterationParameters = { Dt, Iteration, NumIterations, MApplyPushOutPairIterations, &NeedsAnotherIteration };
					Collisions::ApplyPushOut(ConstraintHandle->GetPlaneContact(), MThickness, IsTemporarilyStatic, IterationParameters, ParticleParameters);
				}
				else
				{
					ensureMsgf(false, TEXT("Invalid constraint type"));
				}


			}, bDisableCollisionParallelFor);
		}

		if (PostApplyPushOutCallback != nullptr)
		{
			PostApplyPushOutCallback(Dt, InConstraintHandles, NeedsAnotherIteration);
		}
		return NeedsAnotherIteration;
	}



	template class TAccelerationStructureHandle<float, 3>;
	template class CHAOS_API TPBDCollisionConstraints<float, 3>;
	template void TPBDCollisionConstraints<float, 3>::ComputeConstraints<false>(const TPBDCollisionConstraints<float, 3>::FAccelerationStructure&, float Dt);
	template void TPBDCollisionConstraints<float, 3>::ComputeConstraints<true>(const TPBDCollisionConstraints<float, 3>::FAccelerationStructure&, float Dt);
}
