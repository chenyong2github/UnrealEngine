// Copyright Epic Games, Inc. All Rights Reserved.

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

	DECLARE_CYCLE_STAT(TEXT("Collisions::Reset"), STAT_Collisions_Reset, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::UpdatePointConstraints"), STAT_Collisions_UpdatePointConstraints, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateManifoldConstraints"), STAT_Collisions_UpdateManifoldConstraints, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::Apply"), STAT_Collisions_Apply, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::ApplyPushOut"), STAT_Collisions_ApplyPushOut, STATGROUP_ChaosCollision);

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
		const T CullDistance /*= (T)0*/,
		const T ShapePadding /*= (T)0*/)
		: Particles(InParticles)
		, MCollided(Collided)
		, MPhysicsMaterials(InPerParticleMaterials)
		, MApplyPairIterations(InApplyPairIterations)
		, MApplyPushOutPairIterations(InApplyPushOutPairIterations)
		, MCullDistance(CullDistance)
		, MShapePadding(ShapePadding)
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
	void TPBDCollisionConstraints<T, d>::UpdateConstraintMaterialProperties(FConstraintBase& Constraint)
	{
		TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial0 = Constraint.Particle[0]->AuxilaryValue(MPhysicsMaterials);
		TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial1 = Constraint.Particle[1]->AuxilaryValue(MPhysicsMaterials);

		TCollisionContact<T, d>& Contact = Constraint.Manifold;
		if (PhysicsMaterial0 && PhysicsMaterial1)
		{
			// @todo(ccaulfield): support different friction/restitution combining algorithms
			Contact.Restitution = FMath::Min(PhysicsMaterial0->Restitution, PhysicsMaterial1->Restitution);
			Contact.Friction = FMath::Max(PhysicsMaterial0->Friction, PhysicsMaterial1->Friction);
		}
		else if (PhysicsMaterial0)
		{
			Contact.Restitution = PhysicsMaterial0->Restitution;
			Contact.Friction = PhysicsMaterial0->Friction;
		}
		else if (PhysicsMaterial1)
		{
			Contact.Restitution = PhysicsMaterial1->Restitution;
			Contact.Friction = PhysicsMaterial1->Friction;
		}
		else
		{
			Contact.Friction = 0;
			Contact.Restitution = 0;
		}
		Contact.AngularFriction = MAngularFriction;

		// Overrides for testing
		if (CollisionFrictionOverride >= 0)
		{
			Contact.Friction = CollisionFrictionOverride;
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::AddConstraint(const TRigidBodyPointContactConstraint<FReal, 3>& InConstraint)
	{
		int32 Idx = PointConstraints.Add(InConstraint);
		FConstraintContainerHandle* Handle = HandleAllocator.template AllocHandle< TRigidBodyPointContactConstraint<T, d> >(this, Idx);
		Handle->GetContact().Timestamp = -INT_MAX; // force point constraints to be deleted.

		check(Handle != nullptr);
		Handles.Add(Handle);
		Manifolds.Add(Handle->GetKey(), Handle);

		UpdateConstraintMaterialProperties(PointConstraints[Idx]);
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::AddConstraint(const TRigidBodyMultiPointContactConstraint<FReal, 3>& InConstraint)
	{
		int32 Idx = IterativeConstraints.Add(InConstraint);
		FConstraintContainerHandle* Handle = HandleAllocator.template AllocHandle< TRigidBodyMultiPointContactConstraint<T, d> >(this, Idx);
		Handle->GetContact().Timestamp = LifespanCounter;

		check(Handle != nullptr);
		Handles.Add(Handle);
		Manifolds.Add(Handle->GetKey(), Handle);

		UpdateConstraintMaterialProperties(IterativeConstraints[Idx]);
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
			if (Idx < IterativeConstraints.Num() - 1)
			{
				// update the handle
				FConstraintContainerHandleKey Key = FPBDCollisionConstraintHandle::MakeKey(&IterativeConstraints.Last());
				Manifolds[Key]->SetConstraintIndex(Idx, ConstraintType);
			}
			IterativeConstraints.RemoveAtSwap(Idx);
		}
		else 
		{
			check(false);
		}

		// @todo(chaos): Collision Manifold
		//   Add an index to the handle in the Manifold.Value 
		//   to prevent the search in Handles when removed.
		Manifolds.Remove(KeyToRemove);  
		Handles.Remove(Handle);

		ensure(Handles.Num() == PointConstraints.Num() + IterativeConstraints.Num());

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
		SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdatePointConstraints);

		//PhysicsParallelFor(Handles.Num(), [&](int32 ConstraintHandleIndex)
		//{
		//	FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintHandleIndex];
		//	check(ConstraintHandle != nullptr);
		//	Collisions::Update<ECollisionUpdateType::Deepest, float, 3>(MCullDistance, MShapePadding, ConstraintHandle->GetContact());

		//	if (ConstraintHandle->GetContact().GetPhi() < MCullDistance) 
		//	{
		//		ConstraintHandle->GetContact().Timestamp = LifespanCounter;
		//	}
		//}, bDisableCollisionParallelFor);

		for (FPointContactConstraint& Contact : PointConstraints)
		{
			Collisions::Update<ECollisionUpdateType::Deepest, float, 3>(MCullDistance, Contact);
			if (Contact.GetPhi() < MCullDistance)
			{
				Contact.Timestamp = LifespanCounter;
			}
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::UpdateManifolds(T Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateManifoldConstraints);

		//PhysicsParallelFor(Handles.Num(), [&](int32 ConstraintHandleIndex)
		//{
		//	FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintHandleIndex];
		//	check(ConstraintHandle != nullptr);
		//	Collisions::UpdateManifold<float, 3>(MCullDistance, MShapePadding, ConstraintHandle->GetContact());
		//}, bDisableCollisionParallelFor);

		for (FMultiPointContactConstraint& Contact : IterativeConstraints)
		{
			Collisions::UpdateManifold<float, 3>(MCullDistance, Contact);
			if (Contact.GetPhi() < MCullDistance)
			{
				Contact.Timestamp = LifespanCounter;
			}
		}
	}

	template<typename T, int d>
	void TPBDCollisionConstraints<T, d>::Apply(const T Dt, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		if (MApplyPairIterations > 0)
		{
			const Collisions::TContactParticleParameters<T> ParticleParameters = { MCullDistance, MShapePadding, &MCollided };
			const Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, nullptr };

			for (FPointContactConstraint& Contact : PointConstraints)
			{
				Collisions::Apply(Contact, IterationParameters, ParticleParameters);
			}

			for (FMultiPointContactConstraint& Contact : IterativeConstraints)
			{
				Collisions::Apply(Contact, IterationParameters, ParticleParameters);
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
			const Collisions::TContactParticleParameters<T> ParticleParameters = { MCullDistance, MShapePadding, &MCollided };
			const Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPushOutPairIterations, &bNeedsAnotherIteration };

			for (FPointContactConstraint& Contact : PointConstraints)
			{
				Collisions::ApplyPushOut(Contact, TempStatic, IterationParameters, ParticleParameters);
			}

			for (FMultiPointContactConstraint& Contact : IterativeConstraints)
			{
				Collisions::ApplyPushOut(Contact, TempStatic, IterationParameters, ParticleParameters);
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

				Collisions::TContactParticleParameters<T> ParticleParameters = { MCullDistance, MShapePadding, &MCollided };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, nullptr };
				Collisions::Apply(ConstraintHandle->GetContact(), IterationParameters, ParticleParameters);

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

				Collisions::TContactParticleParameters<T> ParticleParameters = { MCullDistance, MShapePadding, &MCollided };
				Collisions::TContactIterationParameters<T> IterationParameters = { Dt, Iteration, NumIterations, MApplyPushOutPairIterations, &bNeedsAnotherIteration };
				Collisions::ApplyPushOut(ConstraintHandle->GetContact(), IsTemporarilyStatic, IterationParameters, ParticleParameters);

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
