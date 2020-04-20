// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraints.h"

#include "Chaos/Capsule.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/Collision/CollisionContext.h"
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

//PRAGMA_DISABLE_OPTIMIZATION

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

	float CollisionRestitutionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionRestitutionOverride(TEXT("p.CollisionRestitution"), CollisionRestitutionOverride, TEXT("Collision restitution for all contacts if >= 0"));
	
	float CollisionAngularFrictionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionAngularFrictionOverride(TEXT("p.CollisionAngularFriction"), CollisionAngularFrictionOverride, TEXT("Collision angular friction for all contacts if >= 0"));

	CHAOS_API int32 EnableCollisions = 1;
	FAutoConsoleVariableRef CVarEnableCollisions(TEXT("p.EnableCollisions"), EnableCollisions, TEXT("Enable/Disable collisions on the Chaos solver."));
	
	float DefaultCollisionFriction = 0;
	FAutoConsoleVariableRef CVarDefaultCollisionFriction(TEXT("p.DefaultCollisionFriction"), DefaultCollisionFriction, TEXT("Collision friction default value if no materials are found."));

	float DefaultCollisionRestitution = 0;
	FAutoConsoleVariableRef CVarDefaultCollisionRestitution(TEXT("p.DefaultCollisionRestitution"), DefaultCollisionRestitution, TEXT("Collision restitution default value if no materials are found."));

	int32 Chaos_Collision_UseAccumulatedImpulseClipSolve = 0; // Experimental: This requires multiple contact points per iteration per pair, and making sure the contact points don't move too much in body space
	FAutoConsoleVariableRef CVarChaosCollisionOriginalSolve(TEXT("p.Chaos.Collision.UseAccumulatedImpulseClipSolve"), Chaos_Collision_UseAccumulatedImpulseClipSolve, TEXT("Use experimental Accumulated impulse clipped contact solve"));

#if INTEL_ISPC
	bool bChaos_Collision_ISPC_Enabled = false;
	FAutoConsoleVariableRef CVarChaosCollisionISPCEnabled(TEXT("p.Chaos.Collision.ISPC"), bChaos_Collision_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in the Collision Solver"));
#endif


	DECLARE_CYCLE_STAT(TEXT("Collisions::Reset"), STAT_Collisions_Reset, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::UpdatePointConstraints"), STAT_Collisions_UpdatePointConstraints, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateManifoldConstraints"), STAT_Collisions_UpdateManifoldConstraints, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::Apply"), STAT_Collisions_Apply, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::ApplyPushOut"), STAT_Collisions_ApplyPushOut, STATGROUP_ChaosCollision);

	//
	// Collision Constraint Container
	//

	FPBDCollisionConstraints::FPBDCollisionConstraints(
		const TPBDRigidsSOAs<FReal, 3>& InParticles,
		TArrayCollectionArray<bool>& Collided,
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& InPerParticleMaterials,
		const int32 InApplyPairIterations /*= 1*/,
		const int32 InApplyPushOutPairIterations /*= 1*/,
		const FReal CullDistance /*= (FReal)0*/,
		const FReal ShapePadding /*= (FReal)0*/)
		: Particles(InParticles)
		, NumActivePointConstraints(0)
		, NumActiveSweptPointConstraints(0)
		, NumActiveIterativeConstraints(0)
		, MCollided(Collided)
		, MPhysicsMaterials(InPerParticleMaterials)
		, MApplyPairIterations(InApplyPairIterations)
		, MApplyPushOutPairIterations(InApplyPushOutPairIterations)
		, MCullDistance(CullDistance)
		, MShapePadding(ShapePadding)
		, bUseCCD(false)
		, bEnableCollisions(true)
		, bHandlesEnabled(true)
		, ApplyType(ECollisionApplyType::Velocity)
		, LifespanCounter(0)
		, PostApplyCallback(nullptr)
		, PostApplyPushOutCallback(nullptr)
	{
#if INTEL_ISPC
		if (bChaos_Collision_ISPC_Enabled)
		{
			check(sizeof(FCollisionContact) == ispc::SizeofFCollisionContact());
		}
#endif
	}

	void FPBDCollisionConstraints::DisableHandles()
	{
		check(NumConstraints() == 0);
		bHandlesEnabled = false;
	}


	void FPBDCollisionConstraints::SetPostApplyCallback(const FRigidBodyContactConstraintsPostApplyCallback& Callback)
	{
		PostApplyCallback = Callback;
	}

	void FPBDCollisionConstraints::ClearPostApplyCallback()
	{
		PostApplyCallback = nullptr;
	}

	void FPBDCollisionConstraints::SetPostApplyPushOutCallback(const FRigidBodyContactConstraintsPostApplyPushOutCallback& Callback)
	{
		PostApplyPushOutCallback = Callback;
	}
	
	void FPBDCollisionConstraints::ClearPostApplyPushOutCallback()
	{
		PostApplyPushOutCallback = nullptr;
	}

	const FChaosPhysicsMaterial* GetPhysicsMaterial(const TGeometryParticleHandle<FReal, 3>* Particle, const FImplicitObject* Geom, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PhysicsMaterials)
	{
		// Use the per-particle material if it exists
		const FChaosPhysicsMaterial* PhysicsMaterial = Particle->AuxilaryValue(PhysicsMaterials).Get();

		// If no particle material, see if the shape has one
		if (PhysicsMaterial == nullptr)
		{
			// @todo(chaos): handle materials for meshes etc
		
			for (const TUniquePtr<FPerShapeData>& ShapeData : Particle->ShapesArray())
			{
				if (Geom == ShapeData->GetGeometry().Get())
				{
					if (ShapeData->GetMaterials().Num() > 0)
					{
						PhysicsMaterial = ShapeData->GetMaterials()[0].Get();
					}
					break;
				}
			}
		}

		return PhysicsMaterial;
	}

	void FPBDCollisionConstraints::UpdateConstraintMaterialProperties(FCollisionConstraintBase& Constraint)
	{
		const FChaosPhysicsMaterial* PhysicsMaterial0 = GetPhysicsMaterial(Constraint.Particle[0], Constraint.Manifold.Implicit[0], MPhysicsMaterials);
		const FChaosPhysicsMaterial* PhysicsMaterial1 = GetPhysicsMaterial(Constraint.Particle[1], Constraint.Manifold.Implicit[1], MPhysicsMaterials);

		FCollisionContact& Contact = Constraint.Manifold;
		if (PhysicsMaterial0 && PhysicsMaterial1)
		{
			const FChaosPhysicsMaterial::ECombineMode RestitutionCombineMode = FChaosPhysicsMaterial::ChooseCombineMode(PhysicsMaterial0->RestitutionCombineMode,PhysicsMaterial1->RestitutionCombineMode);
			Contact.Restitution = FChaosPhysicsMaterial::CombineHelper(PhysicsMaterial0->Restitution, PhysicsMaterial1->Restitution, RestitutionCombineMode);

			const FChaosPhysicsMaterial::ECombineMode FrictionCombineMode = FChaosPhysicsMaterial::ChooseCombineMode(PhysicsMaterial0->FrictionCombineMode,PhysicsMaterial1->FrictionCombineMode);
			Contact.Friction = FChaosPhysicsMaterial::CombineHelper(PhysicsMaterial0->Friction,PhysicsMaterial1->Friction, FrictionCombineMode);
			Contact.AngularFriction = FChaosPhysicsMaterial::CombineHelper(PhysicsMaterial0->StaticFriction, PhysicsMaterial1->StaticFriction, FrictionCombineMode);
		}
		else if (PhysicsMaterial0)
		{
			Contact.Restitution = PhysicsMaterial0->Restitution;
			Contact.Friction = PhysicsMaterial0->Friction;
			Contact.AngularFriction = PhysicsMaterial0->StaticFriction;
		}
		else if (PhysicsMaterial1)
		{
			Contact.Restitution = PhysicsMaterial1->Restitution;
			Contact.Friction = PhysicsMaterial1->Friction;
			Contact.AngularFriction = PhysicsMaterial1->StaticFriction;
		}
		else
		{
			Contact.Friction = DefaultCollisionFriction;
			Contact.AngularFriction = 0;
			Contact.Restitution = DefaultCollisionRestitution;
		}

		// Overrides for testing
		if (CollisionFrictionOverride >= 0)
		{
			Contact.Friction = CollisionFrictionOverride;
		}
		if (CollisionRestitutionOverride >= 0)
		{
			Contact.Restitution = CollisionRestitutionOverride;
		}
		if (CollisionAngularFrictionOverride >= 0)
		{
			Contact.AngularFriction = CollisionAngularFrictionOverride;
		}
	}

	void FPBDCollisionConstraints::AddConstraint(const FRigidBodyPointContactConstraint& InConstraint)
	{
		int32 Idx = Constraints.SinglePointConstraints.Add(InConstraint);

		if (bHandlesEnabled)
		{
			FPBDCollisionConstraintHandle* Handle = HandleAllocator.template AllocHandle<FRigidBodyPointContactConstraint>(this, Idx);
			Handle->GetContact().Timestamp = -INT_MAX; // force point constraints to be deleted.

			Constraints.SinglePointConstraints[Idx].ConstraintHandle = Handle;

			check(Handle != nullptr);
			Handles.Add(Handle);

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
			check(!Manifolds.Contains(Handle->GetKey()));
			Manifolds.Add(Handle->GetKey(), Handle);
#endif
		}

		UpdateConstraintMaterialProperties(Constraints.SinglePointConstraints[Idx]);
	}

	void FPBDCollisionConstraints::AddConstraint(const FRigidBodySweptPointContactConstraint& InConstraint)
	{
		int32 Idx = Constraints.SinglePointSweptConstraints.Add(InConstraint);

		if (bHandlesEnabled)
		{
			FPBDCollisionConstraintHandle* Handle = HandleAllocator.template AllocHandle<FRigidBodySweptPointContactConstraint>(this, Idx);
			Handle->GetContact().Timestamp = -INT_MAX; // force point constraints to be deleted.

			Constraints.SinglePointSweptConstraints[Idx].ConstraintHandle = Handle;

			if(ensure(Handle != nullptr))
			{			
				Handles.Add(Handle);

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
				check(!Manifolds.Contains(Handle->GetKey()));
				Manifolds.Add(Handle->GetKey(), Handle);
#endif
			}
		}

		UpdateConstraintMaterialProperties(Constraints.SinglePointSweptConstraints[Idx]);
	}


	void FPBDCollisionConstraints::AddConstraint(const FRigidBodyMultiPointContactConstraint& InConstraint)
	{
		int32 Idx = Constraints.MultiPointConstraints.Add(InConstraint);

		if (bHandlesEnabled)
		{
			FPBDCollisionConstraintHandle* Handle = HandleAllocator.template AllocHandle<FRigidBodyMultiPointContactConstraint>(this, Idx);
			Handle->GetContact().Timestamp = LifespanCounter;

			Constraints.MultiPointConstraints[Idx].ConstraintHandle = Handle;

			check(Handle != nullptr);
			Handles.Add(Handle);

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
			check(!Manifolds.Contains(Handle->GetKey()));
			Manifolds.Add(Handle->GetKey(), Handle);
#endif
		}

		UpdateConstraintMaterialProperties(Constraints.MultiPointConstraints[Idx]);
	}

	void FPBDCollisionConstraints::UpdatePositionBasedState(const FReal Dt)
	{
		Reset();
	
		LifespanCounter++;
	}

	void FPBDCollisionConstraints::Reset()
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Reset);

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
		check(bHandlesEnabled);	// This will need fixing for handle-free mode
		TArray<FPBDCollisionConstraintHandle*> CopyOfHandles = Handles;
		int32 LifespanWindow = LifespanCounter - 1;
		for (FPBDCollisionConstraintHandle* ContactHandle : CopyOfHandles)
		{
			if (!bEnableCollisions || ContactHandle->GetContact().Timestamp< LifespanWindow)
			{
				RemoveConstraint(ContactHandle);
			}
		}
#else
		for (FPBDCollisionConstraintHandle* Handle : Handles)
		{
			HandleAllocator.FreeHandle(Handle);
		}
		Constraints.Reset();
		Handles.Reset();
#endif

		bUseCCD = false;
	}

	void FPBDCollisionConstraints::ApplyCollisionModifier(const TFunction<ECollisionModifierResult(FPBDCollisionConstraintHandle* Handle)>& CollisionModifier)
	{
		if (CollisionModifier)
		{
			TArray<FPBDCollisionConstraintHandle*> CopyOfHandles = Handles;

			for (FPBDCollisionConstraintHandle* ContactHandle : CopyOfHandles)
			{
				ECollisionModifierResult Result = CollisionModifier(ContactHandle);
				if (Result == ECollisionModifierResult::Disabled)
				{
					RemoveConstraint(ContactHandle);
				}
			}
		}
	}

	void FPBDCollisionConstraints::RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>&  InHandleSet)
	{
		const TArray<TGeometryParticleHandle<FReal, 3>*> HandleArray = InHandleSet.Array();
		for (auto ParticleHandle : HandleArray)
		{
			TArray<FPBDCollisionConstraintHandle*> CopyOfHandles = Handles;

			for (FPBDCollisionConstraintHandle* ContactHandle : CopyOfHandles)
			{
				TVector<TGeometryParticleHandle<FReal, 3>*, 2> ConstraintParticles = ContactHandle->GetConstrainedParticles();
				if (ConstraintParticles[1] == ParticleHandle || ConstraintParticles[0] == ParticleHandle)
				{
					RemoveConstraint(ContactHandle);
				}
			}
		}
	}

	void FPBDCollisionConstraints::RemoveConstraint(FPBDCollisionConstraintHandle* Handle)
	{
		FConstraintContainerHandleKey KeyToRemove = Handle->GetKey();
		int32 Idx = Handle->GetConstraintIndex(); // index into specific array
		typename FCollisionConstraintBase::FType ConstraintType = Handle->GetType();

		if (ConstraintType == FCollisionConstraintBase::FType::SinglePoint)
		{
#if CHAOS_COLLISION_PERSISTENCE_ENABLED
			if (Idx < Constraints.SinglePointConstraints.Num() - 1)
			{
				// update the handle
				FConstraintContainerHandleKey Key = FPBDCollisionConstraintHandle::MakeKey(&Constraints.SinglePointConstraints.Last());
				Manifolds[Key]->SetConstraintIndex(Idx, ConstraintType);
			}
#endif
			Constraints.SinglePointConstraints.RemoveAtSwap(Idx);
			if (bHandlesEnabled && (Idx < Constraints.SinglePointConstraints.Num()))
			{
				Constraints.SinglePointConstraints[Idx].ConstraintHandle->SetConstraintIndex(Idx, FCollisionConstraintBase::FType::SinglePoint);
			}

		}
		else if (ConstraintType == FCollisionConstraintBase::FType::SinglePointSwept)
		{
#if CHAOS_COLLISION_PERSISTENCE_ENABLED
			if (Idx < Constraints.SinglePointSweptConstraints.Num() - 1)
			{
				// update the handle
				FConstraintContainerHandleKey Key = FPBDCollisionConstraintHandle::MakeKey(&Constraints.SinglePointSweptConstraints.Last());
				Manifolds[Key]->SetConstraintIndex(Idx, ConstraintType);
			}
#endif
			Constraints.SinglePointSweptConstraints.RemoveAtSwap(Idx);
			if (bHandlesEnabled && (Idx < Constraints.SinglePointSweptConstraints.Num()))
			{
				Constraints.SinglePointSweptConstraints[Idx].ConstraintHandle->SetConstraintIndex(Idx, FCollisionConstraintBase::FType::SinglePointSwept);
			}
		}
		else if (ConstraintType == FCollisionConstraintBase::FType::MultiPoint)
		{
#if CHAOS_COLLISION_PERSISTENCE_ENABLED
			if (Idx < Constraints.MultiPointConstraints.Num() - 1)
			{
				// update the handle
				FConstraintContainerHandleKey Key = FPBDCollisionConstraintHandle::MakeKey(&Constraints.MultiPointConstraints.Last());
				Manifolds[Key]->SetConstraintIndex(Idx, ConstraintType);
			}
#endif
			Constraints.MultiPointConstraints.RemoveAtSwap(Idx);
			if (bHandlesEnabled && (Idx < Constraints.MultiPointConstraints.Num()))
			{
				Constraints.MultiPointConstraints[Idx].ConstraintHandle->SetConstraintIndex(Idx, FCollisionConstraintBase::FType::MultiPoint);
			}
		}
		else 
		{
			check(false);
		}

		if (bHandlesEnabled)
		{
			// @todo(chaos): Collision Manifold
			//   Add an index to the handle in the Manifold.Value 
			//   to prevent the search in Handles when removed.
#if CHAOS_COLLISION_PERSISTENCE_ENABLED
			Manifolds.Remove(KeyToRemove);
#endif
			Handles.Remove(Handle);
			check(Handles.Num() == Constraints.SinglePointConstraints.Num() + Constraints.SinglePointSweptConstraints.Num() + Constraints.MultiPointConstraints.Num());

			HandleAllocator.FreeHandle(Handle);
		}
	}


	void FPBDCollisionConstraints::UpdateConstraints(FReal Dt, const TSet<TGeometryParticleHandle<FReal, 3>*>& ParticlesSet)
	{
		// Clustering uses update constraints to force a re-evaluation. 
	}

	// Called once per frame to update persistent constraints (reruns collision detection, or selects the best manifold point)
	void FPBDCollisionConstraints::UpdateConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdatePointConstraints);

		// Make sure the cull distance is enough if we switched to Accumulated Impulse clipping		
		const int MinCullDistanceForImpulseClipping = 5;
		if (Chaos_Collision_UseAccumulatedImpulseClipSolve && MCullDistance < MinCullDistanceForImpulseClipping)
		{
			MCullDistance = MinCullDistanceForImpulseClipping;
		}

		// @todo(chaos): parallelism needs to be optional

		//PhysicsParallelFor(Handles.Num(), [&](int32 ConstraintHandleIndex)
		//{
		//	FPBDCollisionConstraintHandle* ConstraintHandle = Handles[ConstraintHandleIndex];
		//	check(ConstraintHandle != nullptr);
		//	Collisions::Update(MCullDistance, MShapePadding, ConstraintHandle->GetContact());

		//	if (ConstraintHandle->GetContact().GetPhi() < MCullDistance) 
		//	{
		//		ConstraintHandle->GetContact().Timestamp = LifespanCounter;
		//	}
		//}, bDisableCollisionParallelFor);

		for (FRigidBodyPointContactConstraint& Contact : Constraints.SinglePointConstraints)
		{
			Collisions::Update(Contact, MCullDistance);
			if (Contact.GetPhi() < MCullDistance)
			{
				Contact.Timestamp = LifespanCounter;
			}
		}
	}

	// Called once per tick to update/regenerate persistent manifold planes and points
	void FPBDCollisionConstraints::UpdateManifolds(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateManifoldConstraints);

		// @todo(chaos): parallelism needs to be optional

		//PhysicsParallelFor(Handles.Num(), [&](int32 ConstraintHandleIndex)
		//{
		//	FPBDCollisionConstraintHandle* ConstraintHandle = Handles[ConstraintHandleIndex];
		//	check(ConstraintHandle != nullptr);
		//	Collisions::Update(MCullDistance, MShapePadding, ConstraintHandle->GetContact());
		//}, bDisableCollisionParallelFor);

		FCollisionContext Context;

		for (FRigidBodyMultiPointContactConstraint& Contact : Constraints.MultiPointConstraints)
		{
			Collisions::UpdateManifold(Contact, MCullDistance, Context);
			if (Contact.GetPhi() < MCullDistance)
			{
				Contact.Timestamp = LifespanCounter;
			}
		}
	}

	bool FPBDCollisionConstraints::Apply(const FReal Dt, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		bool bNeedsAnotherIteration = false;
		if (MApplyPairIterations > 0)
		{
			const Collisions::FContactParticleParameters ParticleParameters = { MCullDistance, MShapePadding, &MCollided };
			const Collisions::FContactIterationParameters IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, ApplyType, &bNeedsAnotherIteration };

			NumActivePointConstraints = 0;
			for (FRigidBodyPointContactConstraint& Contact : Constraints.SinglePointConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::ApplySinglePoint(Contact, IterationParameters, ParticleParameters);
					++NumActivePointConstraints;
				}
			}

			NumActiveIterativeConstraints = 0;
			for (FRigidBodyMultiPointContactConstraint& Contact : Constraints.MultiPointConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::ApplyMultiPoint(Contact, IterationParameters, ParticleParameters);
					++NumActiveIterativeConstraints;
				}
			}

			// Swept apply may significantly change particle position, invalidating other constraint's manifolds.
			// We don't update manifolds on first apply iteration, so make sure we apply swept constraints last.
			NumActiveSweptPointConstraints = 0;
			for (FRigidBodySweptPointContactConstraint& Contact : Constraints.SinglePointSweptConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::Apply(Contact, IterationParameters, ParticleParameters);
					++NumActiveSweptPointConstraints;
				}
			}
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, Handles);
		}

		return bNeedsAnotherIteration;
	}

	bool FPBDCollisionConstraints::ApplyPushOut(const FReal Dt, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		TSet<const TGeometryParticleHandle<FReal, 3>*> TempStatic;
		bool bNeedsAnotherIteration = false;
		if (MApplyPushOutPairIterations > 0)
		{
			const Collisions::FContactParticleParameters ParticleParameters = { MCullDistance, MShapePadding, &MCollided };
			const Collisions::FContactIterationParameters IterationParameters = { Dt, Iterations, NumIterations, MApplyPushOutPairIterations, ECollisionApplyType::None, &bNeedsAnotherIteration };

			for (FRigidBodyPointContactConstraint& Contact : Constraints.SinglePointConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::ApplyPushOutSinglePoint(Contact, TempStatic, IterationParameters, ParticleParameters);
				}
			}

			for (FRigidBodySweptPointContactConstraint& Contact : Constraints.SinglePointSweptConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::ApplyPushOut(Contact, TempStatic, IterationParameters, ParticleParameters);
				}
			}

			for (FRigidBodyMultiPointContactConstraint& Contact : Constraints.MultiPointConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::ApplyPushOutMultiPoint(Contact, TempStatic, IterationParameters, ParticleParameters);
				}
			}
		}

		if (PostApplyPushOutCallback != nullptr)
		{
			PostApplyPushOutCallback(Dt, Handles, bNeedsAnotherIteration);
		}

		return bNeedsAnotherIteration;
	}


	bool FPBDCollisionConstraints::Apply(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		TAtomic<bool> bNeedsAnotherIterationAtomic;
		bNeedsAnotherIterationAtomic.Store(false);
		if (MApplyPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
				FPBDCollisionConstraintHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);

				TVector<const TGeometryParticleHandle<FReal, 3>*, 2> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
				bool bNeedsAnotherIteration = false;

				if (!ConstraintHandle->GetContact().GetDisabled())
				{
					Collisions::FContactParticleParameters ParticleParameters = { MCullDistance, MShapePadding, &MCollided };
					Collisions::FContactIterationParameters IterationParameters = { Dt, Iterations, NumIterations, MApplyPairIterations, ApplyType, &bNeedsAnotherIteration };
					Collisions::Apply(ConstraintHandle->GetContact(), IterationParameters, ParticleParameters);

					if (bNeedsAnotherIteration)
					{
						bNeedsAnotherIterationAtomic.Store(true);
					}
				}

			}, bDisableCollisionParallelFor);
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, InConstraintHandles);
		}

		return bNeedsAnotherIterationAtomic.Load();
	}


	bool FPBDCollisionConstraints::ApplyPushOut(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles, const TSet< const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_ApplyPushOut);

		bool bNeedsAnotherIteration = false;
		if (MApplyPushOutPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex)
			{
				FPBDCollisionConstraintHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);

				if (!ConstraintHandle->GetContact().GetDisabled())
				{
					Collisions::FContactParticleParameters ParticleParameters = { MCullDistance, MShapePadding, &MCollided };
					Collisions::FContactIterationParameters IterationParameters = { Dt, Iteration, NumIterations, MApplyPushOutPairIterations, ECollisionApplyType::None, &bNeedsAnotherIteration };
					Collisions::ApplyPushOut(ConstraintHandle->GetContact(), IsTemporarilyStatic, IterationParameters, ParticleParameters);
				}

			}, bDisableCollisionParallelFor);
		}

		if (PostApplyPushOutCallback != nullptr)
		{
			PostApplyPushOutCallback(Dt, InConstraintHandles, bNeedsAnotherIteration);
		}

		return bNeedsAnotherIteration;
	}

	const FCollisionConstraintBase& FPBDCollisionConstraints::GetConstraint(int32 Index) const
	{
		check(Index < NumConstraints());
		
		if (Index < Constraints.SinglePointConstraints.Num())
		{
			return Constraints.SinglePointConstraints[Index];
		}
		Index -= Constraints.SinglePointConstraints.Num();

		if (Index < Constraints.SinglePointSweptConstraints.Num())
		{
			return Constraints.SinglePointSweptConstraints[Index];
		}
		Index -= Constraints.SinglePointSweptConstraints.Num();

		{
			return Constraints.MultiPointConstraints[Index];
		}
	}


	template class TAccelerationStructureHandle<float, 3>;
}
