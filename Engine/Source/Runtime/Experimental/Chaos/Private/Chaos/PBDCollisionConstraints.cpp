// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraints.h"

#include "Chaos/Capsule.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
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

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::Reset"), STAT_Collisions_Reset, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::Apply"), STAT_Collisions_Apply, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::ApplyPushOut"), STAT_Collisions_ApplyPushOut, STATGROUP_Chaos);

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
		// WARNING : ConstraintBase is about to be deleted!

		FConstraintContainerHandle* Handle = nullptr;

		if (ConstraintBase->GetType() == TRigidBodyPointContactConstraint<T, 3>::StaticType())
		{
			TRigidBodyPointContactConstraint<T, d>* PointConstraint = ConstraintBase->template As< TRigidBodyPointContactConstraint<T, d> >();

			int32 Idx = PointConstraints.Add(*PointConstraint);
			Handle = HandleAllocator.template AllocHandle< TRigidBodyPointContactConstraint<T, d> >(this, Idx);
			Handle->GetContact().Timestamp = -INT_MAX; // force point constraints to be deleted.

			delete PointConstraint;
		}
		else if (ConstraintBase->GetType() == TRigidBodyIterativeContactConstraint<T, 3>::StaticType())
		{
			TRigidBodyIterativeContactConstraint<T, d>* PlaneConstraint = ConstraintBase->template As< TRigidBodyIterativeContactConstraint<T, d> >();

			int32 Idx = PlaneConstraints.Add(*PlaneConstraint);
			Handle = HandleAllocator.template AllocHandle< TRigidBodyIterativeContactConstraint<T, d> >(this, Idx);
			Handle->GetContact().Timestamp = LifespanCounter;

			delete PlaneConstraint;
		}

		check(Handle != nullptr);
		Handles.Add(Handle);
		Manifolds.Add(Handle->GetKey(), Handle);
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::UpdatePositionBasedState(const T Dt)
	{
		Reset();
	
		LifespanCounter++;
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::Reset()
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Reset);

		TArray<FConstraintContainerHandle*> CopyOfHandles = Handles;

		int32 LifespanWindow = LifespanCounter - 1;
		for (FConstraintContainerHandle* ContactHandle : CopyOfHandles)
		{
			if (!bEnableCollisions || ContactHandle->GetContact().Timestamp< LifespanWindow)
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
		FConstraintContainerHandleKey KeyToRemove = Handle->GetKey();
		int32 Idx = Handle->GetConstraintIndex(); // index into specific array
		typename FCollisionConstraintBase::FType ConstraintType = Handle->GetType();

		if (ConstraintType == FCollisionConstraintBase::FType::SinglePoint)
		{
			if (Idx < PointConstraints.Num() - 1)
			{
				// update the handle
				FConstraintContainerHandleKey Key = FPBDCollisionConstraintHandle::MakeKey(&PointConstraints.Last());
				Manifolds[Key]->SetConstraintIndex(Idx, ConstraintType);
			}
			PointConstraints.RemoveAtSwap(Idx);

		}
		else if (ConstraintType == FCollisionConstraintBase::FType::MultiPoint)
		{
			if (Idx < PlaneConstraints.Num() - 1)
			{
				// update the handle
				FConstraintContainerHandleKey Key = FPBDCollisionConstraintHandle::MakeKey(&PlaneConstraints.Last());
				Manifolds[Key]->SetConstraintIndex(Idx, ConstraintType);
			}
			PlaneConstraints.RemoveAtSwap(Idx);
		}
		else 
		{
			check(false);
		}

		Manifolds.Remove(KeyToRemove); // todo(brice): Add index to the handle to prevent the search. 
		Handles.Remove(Handle);

		ensure(Handles.Num() == PointConstraints.Num() + PlaneConstraints.Num());

		HandleAllocator.FreeHandle(Handle);
	}


	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::UpdateConstraints(T Dt, const TSet<TGeometryParticleHandle<T, d>*>& ParticlesSet)
	{
		// Clustering uses update constraints to force a re-evaluation. 
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::UpdateConstraints(T Dt)
	{
		PhysicsParallelFor(Handles.Num(), [&](int32 ConstraintHandleIndex)
		{
			FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintHandleIndex];
			check(ConstraintHandle != nullptr);
			Collisions::Update<ECollisionUpdateType::Deepest, float, 3>(MThickness, ConstraintHandle->GetContact());

			if (ConstraintHandle->GetContact().GetPhi() < MThickness) 
			{
				ConstraintHandle->GetContact().Timestamp = LifespanCounter;
			}
		}, bDisableCollisionParallelFor);
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::UpdateManifolds(T Dt)
	{
		PhysicsParallelFor(PlaneConstraints.Num(), [&](int32 ConstraintIndex)
		{
			FConstraintBase& ConstraintBase = PlaneConstraints[ConstraintIndex];
			if (ConstraintBase.GetType() == FCollisionConstraintBase::FType::MultiPoint)
			{
				Collisions::UpdateManifold<float, 3>(MThickness, ConstraintBase);
			}
		}, bDisableCollisionParallelFor);
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::Apply(const T Dt, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		if (MApplyPairIterations > 0)
		{
			for (FPointContactConstraint& Contact : PointConstraints)
			{
				Collisions::TContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, nullptr };
				Collisions::Apply(Contact, MThickness, IterationParameters, ParticleParameters);
			}

			for (FPlaneContactConstraint& Contact : PlaneConstraints)
			{
				Collisions::TContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, nullptr };
				Collisions::Apply(Contact, MThickness, IterationParameters, ParticleParameters);
			}
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, Handles);
		}
	}

	template<typename T, int d>
	bool TPBDCollisionConstraints<T, d>::ApplyPushOut(const T Dt, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		TSet<const TGeometryParticleHandle<T, d>*> TempStatic;
		bool bNeedsAnotherIteration = false;
		if (MApplyPushOutPairIterations > 0)
		{
			for (FPointContactConstraint& Contact : PointConstraints)
			{
				Collisions::TContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPushOutPairIterations, &bNeedsAnotherIteration };
				Collisions::ApplyPushOut(Contact, MThickness, TempStatic, IterationParameters, ParticleParameters);
			}

			for (FPlaneContactConstraint& Contact : PlaneConstraints)
			{
				Collisions::TContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPushOutPairIterations, &bNeedsAnotherIteration };
				Collisions::ApplyPushOut(Contact, MThickness, TempStatic, IterationParameters, ParticleParameters);
			}
		}

		if (PostApplyPushOutCallback != nullptr)
		{
			PostApplyPushOutCallback(Dt, Handles, bNeedsAnotherIteration);
		}

		return bNeedsAnotherIteration;
	}


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


	template<typename T, int d>
	bool TPBDCollisionConstraints<T, d>::ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const TSet< const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		bool bNeedsAnotherIteration = false;
		if (MApplyPushOutPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex)
			{
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);

				Collisions::TContactParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iteration, NumIterations, MApplyPushOutPairIterations, &bNeedsAnotherIteration };
				Collisions::ApplyPushOut(ConstraintHandle->GetContact(), MThickness, IsTemporarilyStatic, IterationParameters, ParticleParameters);

			}, bDisableCollisionParallelFor);
		}

		if (PostApplyPushOutCallback != nullptr)
		{
			PostApplyPushOutCallback(Dt, InConstraintHandles, bNeedsAnotherIteration);
		}

		return bNeedsAnotherIteration;
	}



	template class TAccelerationStructureHandle<float, 3>;
	template class CHAOS_API TPBDCollisionConstraints<float, 3>;
}
