// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraints.h"

#include "Chaos/Capsule.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/SpatialAccelerationCollection.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
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

	float CollisionFrictionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionFrictionOverride(TEXT("p.CollisionFriction"), CollisionFrictionOverride, TEXT("Collision friction for all contacts if >= 0"));

	CHAOS_API int32 EnableCollisions = 1;
	FAutoConsoleVariableRef CVarEnableCollisions(TEXT("p.EnableCollisions"), EnableCollisions, TEXT("Enable/Disable collisions on the Chaos solver."));

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
		, PostApplyCallback(nullptr)
		, PostApplyPushOutCallback(nullptr)
	{
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
	void TPBDCollisionConstraints<T, d>::AddConstraint(FConstraintBase* ConstraintBase)
	{
		if (ConstraintBase->GetType() == TRigidBodyPointContactConstraint<T, 3>::StaticType())
		{
			TRigidBodyPointContactConstraint<T, d>* PointConstraint = ConstraintBase->template As< TRigidBodyPointContactConstraint<T, d> >();

			int32 Idx = PointConstraints.AddUninitialized(1);
			PointConstraints[Idx] = *PointConstraint;
			Handles.Add(HandleAllocator.template AllocHandle< TRigidBodyPointContactConstraint<T, d> >(this, Idx));

			delete PointConstraint;
		}
		else if (ConstraintBase->GetType() == TRigidBodyPlaneContactConstraint<T, 3>::StaticType())
		{
			TRigidBodyPlaneContactConstraint<T, d>* PlaneConstraint = ConstraintBase->template As< TRigidBodyPlaneContactConstraint<T, d> >();

			int32 Idx = PlaneConstraints.AddUninitialized(1);
			PlaneConstraints[Idx] = *PlaneConstraint;
			Handles.Add(HandleAllocator.template AllocHandle< TRigidBodyPlaneContactConstraint<T, d> >(this, Idx));

			delete PlaneConstraint;
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::UpdatePositionBasedState(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ const T Dt)
	{
		Reset();
	
		LifespanCounter++;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::Reset"), STAT_Collisions_Reset, STATGROUP_Chaos);
	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::Reset()
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Reset);

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
	void TPBDCollisionConstraints<T, d>::UpdateConstraints(T Dt, const TSet<TGeometryParticleHandle<T, d>*>& ParticlesSet)
	{
		// Clustering uses update constraints to force a re-evaluation. 
	}


	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::Apply"), STAT_Collisions_Apply, STATGROUP_Chaos);
	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);
		if (MApplyPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);

				Collisions::TContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, nullptr };
				Collisions::Apply(ConstraintHandle->GetContact(), MThickness, IterationParameters, ParticleParameters);

			}, bDisableCollisionParallelFor);
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, InConstraintHandles);
		}
	}


	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::ApplyPushOut"), STAT_Collisions_ApplyPushOut, STATGROUP_Chaos);
	template<typename T, int d>
	bool TPBDCollisionConstraints<T, d>::ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const TSet< const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		bool NeedsAnotherIteration = false;
		if (MApplyPushOutPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex)
			{
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);

				Collisions::TContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iteration, NumIterations, MApplyPushOutPairIterations, &NeedsAnotherIteration };
				Collisions::ApplyPushOut(ConstraintHandle->GetContact(), MThickness, IsTemporarilyStatic, IterationParameters, ParticleParameters);

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
}
