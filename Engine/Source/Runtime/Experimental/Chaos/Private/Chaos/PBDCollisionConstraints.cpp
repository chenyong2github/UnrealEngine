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
#include "Chaos/CastingUtilities.h"
#include "ChaosLog.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Algo/Sort.h"

#if INTEL_ISPC
#include "PBDCollisionConstraints.ispc.generated.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	extern int32 UseLevelsetCollision;

	namespace Collisions
	{
		extern int32 Chaos_Collision_UseAccumulatedImpulseClipSolve;
	}

	int32 CollisionParticlesBVHDepth = 4;
	FAutoConsoleVariableRef CVarCollisionParticlesBVHDepth(TEXT("p.CollisionParticlesBVHDepth"), CollisionParticlesBVHDepth, TEXT("The maximum depth for collision particles bvh"));

	int32 ConstraintBPBVHDepth = 2;
	FAutoConsoleVariableRef CVarConstraintBPBVHDepth(TEXT("p.ConstraintBPBVHDepth"), ConstraintBPBVHDepth, TEXT("The maximum depth for constraint bvh"));

	int32 BPTreeOfGrids = 1;
	FAutoConsoleVariableRef CVarBPTreeOfGrids(TEXT("p.BPTreeOfGrids"), BPTreeOfGrids, TEXT("Whether to use a seperate tree of grids for bp"));

	FRealSingle CollisionFrictionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionFrictionOverride(TEXT("p.CollisionFriction"), CollisionFrictionOverride, TEXT("Collision friction for all contacts if >= 0"));

	FRealSingle CollisionRestitutionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionRestitutionOverride(TEXT("p.CollisionRestitution"), CollisionRestitutionOverride, TEXT("Collision restitution for all contacts if >= 0"));
	
	FRealSingle CollisionAngularFrictionOverride = -1.0f;
	FAutoConsoleVariableRef CVarCollisionAngularFrictionOverride(TEXT("p.CollisionAngularFriction"), CollisionAngularFrictionOverride, TEXT("Collision angular friction for all contacts if >= 0"));

	CHAOS_API int32 EnableCollisions = 1;
	FAutoConsoleVariableRef CVarEnableCollisions(TEXT("p.EnableCollisions"), EnableCollisions, TEXT("Enable/Disable collisions on the Chaos solver."));
	
	FRealSingle DefaultCollisionFriction = 0;
	FAutoConsoleVariableRef CVarDefaultCollisionFriction(TEXT("p.DefaultCollisionFriction"), DefaultCollisionFriction, TEXT("Collision friction default value if no materials are found."));

	FRealSingle DefaultCollisionRestitution = 0;
	FAutoConsoleVariableRef CVarDefaultCollisionRestitution(TEXT("p.DefaultCollisionRestitution"), DefaultCollisionRestitution, TEXT("Collision restitution default value if no materials are found."));

	FRealSingle CollisionRestitutionThresholdOverride = -1.0f;
	FAutoConsoleVariableRef CVarDefaultCollisionRestitutionThreshold(TEXT("p.CollisionRestitutionThreshold"), CollisionRestitutionThresholdOverride, TEXT("Collision restitution threshold override if >= 0 (units of acceleration)"));

	int32 CollisionCanAlwaysDisableContacts = 0;
	FAutoConsoleVariableRef CVarCollisionCanAlwaysDisableContacts(TEXT("p.CollisionCanAlwaysDisableContacts"), CollisionCanAlwaysDisableContacts, TEXT("Collision culling will always be able to permanently disable contacts"));

	int32 CollisionCanNeverDisableContacts = 0;
	FAutoConsoleVariableRef CVarCollisionCanNeverDisableContacts(TEXT("p.CollisionCanNeverDisableContacts"), CollisionCanNeverDisableContacts, TEXT("Collision culling will never be able to permanently disable contacts"));

#if INTEL_ISPC
	bool bChaos_Collision_ISPC_Enabled = false;
	FAutoConsoleVariableRef CVarChaosCollisionISPCEnabled(TEXT("p.Chaos.Collision.ISPC"), bChaos_Collision_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in the Collision Solver"));
#endif


	DECLARE_CYCLE_STAT(TEXT("Collisions::Reset"), STAT_Collisions_Reset, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::UpdatePointConstraints"), STAT_Collisions_UpdatePointConstraints, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::Apply"), STAT_Collisions_Apply, STATGROUP_ChaosCollision);
	DECLARE_CYCLE_STAT(TEXT("Collisions::ApplyPushOut"), STAT_Collisions_ApplyPushOut, STATGROUP_ChaosCollision);

	//
	// Collision Constraint Container
	//

	FPBDCollisionConstraints::FPBDCollisionConstraints(
		const FPBDRigidsSOAs& InParticles,
		TArrayCollectionArray<bool>& Collided,
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& InPhysicsMaterials,
		const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& InPerParticlePhysicsMaterials,
		const int32 InApplyPairIterations /*= 1*/,
		const int32 InApplyPushOutPairIterations /*= 1*/,
		const FReal InRestitutionThreshold /*= (FReal)0*/)
		: bInAppendOperation(false)
		, Particles(InParticles)
		, NumActivePointConstraints(0)
		, NumActiveSweptPointConstraints(0)
		, MCollided(Collided)
		, MPhysicsMaterials(InPhysicsMaterials)
		, MPerParticlePhysicsMaterials(InPerParticlePhysicsMaterials)
		, MApplyPairIterations(InApplyPairIterations)
		, MApplyPushOutPairIterations(InApplyPushOutPairIterations)
		, RestitutionThreshold(InRestitutionThreshold)	// @todo(chaos): expose as property
		, bUseCCD(false)
		, bEnableCollisions(true)
		, bEnableRestitution(true)
		, bHandlesEnabled(true)
		, bCanDisableContacts(true)
		, SolverType(EConstraintSolverType::GbfPbd)
		, LifespanCounter(0)
		, PostApplyCallback(nullptr)
		, PostApplyPushOutCallback(nullptr)
	{
#if INTEL_ISPC
		if (bRealTypeCompatibleWithISPC && bChaos_Collision_ISPC_Enabled)
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

	const FChaosPhysicsMaterial* GetPhysicsMaterial(const TGeometryParticleHandle<FReal, 3>* Particle, const FImplicitObject* Geom, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PhysicsMaterials, const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& PerParticlePhysicsMaterials)
	{
		// Use the per-particle material if it exists
		const FChaosPhysicsMaterial* UniquePhysicsMaterial = Particle->AuxilaryValue(PerParticlePhysicsMaterials).Get();
		if (UniquePhysicsMaterial != nullptr)
		{
			return UniquePhysicsMaterial;
		}
		const FChaosPhysicsMaterial* PhysicsMaterial = Particle->AuxilaryValue(PhysicsMaterials).Get();
		if (PhysicsMaterial != nullptr)
		{
			return PhysicsMaterial;
		}

		// If no particle material, see if the shape has one
		// @todo(chaos): handle materials for meshes etc
		for (const TUniquePtr<FPerShapeData>& ShapeData : Particle->ShapesArray())
		{
			const FImplicitObject* OuterShapeGeom = ShapeData->GetGeometry().Get();
			const FImplicitObject* InnerShapeGeom = Utilities::ImplicitChildHelper(OuterShapeGeom);
			if (Geom == OuterShapeGeom || Geom == InnerShapeGeom)
			{
				if (ShapeData->GetMaterials().Num() > 0)
				{
					return ShapeData->GetMaterials()[0].Get();
				}
				else
				{
					// This shape doesn't have a material assigned
					return nullptr;
				}
			}
		}

		// The geometry used for this particle does not belong to the particle.
		// This can happen in the case of fracture.
		return nullptr;
	}

	void FPBDCollisionConstraints::UpdateConstraintMaterialProperties(FCollisionConstraintBase& Constraint)
	{
		const FChaosPhysicsMaterial* PhysicsMaterial0 = GetPhysicsMaterial(Constraint.Particle[0], Constraint.Manifold.Implicit[0], MPhysicsMaterials, MPerParticlePhysicsMaterials);
		const FChaosPhysicsMaterial* PhysicsMaterial1 = GetPhysicsMaterial(Constraint.Particle[1], Constraint.Manifold.Implicit[1], MPhysicsMaterials, MPerParticlePhysicsMaterials);

		FCollisionContact& Contact = Constraint.Manifold;
		if (PhysicsMaterial0 && PhysicsMaterial1)
		{
			const FChaosPhysicsMaterial::ECombineMode RestitutionCombineMode = FChaosPhysicsMaterial::ChooseCombineMode(PhysicsMaterial0->RestitutionCombineMode,PhysicsMaterial1->RestitutionCombineMode);
			Contact.Restitution = FChaosPhysicsMaterial::CombineHelper(PhysicsMaterial0->Restitution, PhysicsMaterial1->Restitution, RestitutionCombineMode);

			const FChaosPhysicsMaterial::ECombineMode FrictionCombineMode = FChaosPhysicsMaterial::ChooseCombineMode(PhysicsMaterial0->FrictionCombineMode,PhysicsMaterial1->FrictionCombineMode);
			Contact.Friction = FChaosPhysicsMaterial::CombineHelper(PhysicsMaterial0->Friction,PhysicsMaterial1->Friction, FrictionCombineMode);
			const FReal StaticFriction0 = FMath::Max(PhysicsMaterial0->Friction, PhysicsMaterial0->StaticFriction);
			const FReal StaticFriction1 = FMath::Max(PhysicsMaterial1->Friction, PhysicsMaterial1->StaticFriction);
			Contact.AngularFriction = FChaosPhysicsMaterial::CombineHelper(StaticFriction0, StaticFriction1, FrictionCombineMode);
		}
		else if (PhysicsMaterial0)
		{
			const FReal StaticFriction0 = FMath::Max(PhysicsMaterial0->Friction, PhysicsMaterial0->StaticFriction);
			Contact.Restitution = PhysicsMaterial0->Restitution;
			Contact.Friction = PhysicsMaterial0->Friction;
			Contact.AngularFriction = StaticFriction0;
		}
		else if (PhysicsMaterial1)
		{
			const FReal StaticFriction1 = FMath::Max(PhysicsMaterial1->Friction, PhysicsMaterial1->StaticFriction);
			Contact.Restitution = PhysicsMaterial1->Restitution;
			Contact.Friction = PhysicsMaterial1->Friction;
			Contact.AngularFriction = StaticFriction1;
		}
		else
		{
			Contact.Friction = DefaultCollisionFriction;
			Contact.AngularFriction = DefaultCollisionFriction;
			Contact.Restitution = DefaultCollisionRestitution;
		}

		if (!bEnableRestitution)
		{
			Contact.Restitution = 0.0f;
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

	FPBDCollisionConstraints::FConstraintAppendScope FPBDCollisionConstraints::BeginAppendScope()
	{
		check(!bInAppendOperation);
		return FPBDCollisionConstraints::FConstraintAppendScope(this);
	}

	void FPBDCollisionConstraints::AddConstraint(const FRigidBodyPointContactConstraint& InConstraint)
	{
		check(!bInAppendOperation);

		int32 Idx = Constraints.SinglePointConstraints.Add(InConstraint);

		if (bHandlesEnabled)
		{
			FPBDCollisionConstraintHandle* Handle = HandleAllocator.template AllocHandle<FRigidBodyPointContactConstraint>(this, Idx);
			Handle->GetContact().Timestamp = -INT_MAX; // force point constraints to be deleted.

			Constraints.SinglePointConstraints[Idx].SetConstraintHandle(Handle);

			check(Handle != nullptr);
			Handles.Add(Handle);

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
			check(!Manifolds.Contains(Handle->GetKey()));
			Manifolds.Add(Handle->GetKey(), Handle);
#endif
		}
	}

	void FPBDCollisionConstraints::AddConstraint(const FRigidBodySweptPointContactConstraint& InConstraint)
	{
		check(!bInAppendOperation);

		int32 Idx = Constraints.SinglePointSweptConstraints.Add(InConstraint);

		if (bHandlesEnabled)
		{
			FPBDCollisionConstraintHandle* Handle = HandleAllocator.template AllocHandle<FRigidBodySweptPointContactConstraint>(this, Idx);
			Handle->GetContact().Timestamp = -INT_MAX; // force point constraints to be deleted.

			Constraints.SinglePointSweptConstraints[Idx].SetConstraintHandle(Handle);

			if(ensure(Handle != nullptr))
			{			
				Handles.Add(Handle);

#if CHAOS_COLLISION_PERSISTENCE_ENABLED
				check(!Manifolds.Contains(Handle->GetKey()));
				Manifolds.Add(Handle->GetKey(), Handle);
#endif
			}
		}
	}

	void FPBDCollisionConstraints::PrepareIteration(FReal dt)
	{
		// NOTE: We could set material properties as we add constraints, but the ParticlePairBroadphase
		// skips the call to AddConstraint and writes directly to the constraint array, so we
		// need to do it after all constraints are added.

		for (FRigidBodyPointContactConstraint& Contact : Constraints.SinglePointConstraints)
		{
			UpdateConstraintMaterialProperties(Contact);
		}

		for (FRigidBodySweptPointContactConstraint& Contact : Constraints.SinglePointSweptConstraints)
		{
			UpdateConstraintMaterialProperties(Contact);
		}
	}

	void FPBDCollisionConstraints::UpdatePositionBasedState(const FReal Dt)
	{
		check(!bInAppendOperation);

		Reset();
	
		LifespanCounter++;
	}

	void FPBDCollisionConstraints::Reset()
	{
		check(!bInAppendOperation);

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

	void FPBDCollisionConstraints::ApplyCollisionModifier(const TArray<ISimCallbackObject*>& CollisionModifiers)
	{
		check(!bInAppendOperation);
		if (Handles.Num())
		{
			for(ISimCallbackObject* Modifier : CollisionModifiers)
		{
			TArray<FPBDCollisionConstraintHandleModification> ModificationResults;
			ModificationResults.Reserve(Handles.Num());
				for (FPBDCollisionConstraintHandle* Handle : Handles)
			{
				ModificationResults.Emplace(Handle);
			}

				Modifier->ContactModification_Internal(TArrayView<FPBDCollisionConstraintHandleModification>(ModificationResults.GetData(), ModificationResults.Num()));
				
			for (const FPBDCollisionConstraintHandleModification& Modification : ModificationResults)
			{
					if (Modification.GetResult() == ECollisionModifierResult::Disabled)
				{
					RemoveConstraint(Modification.GetHandle());
				}
			}
		}
			
		}
	}

	void FPBDCollisionConstraints::RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>&  InHandleSet)
	{
		check(!bInAppendOperation);

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
		check(!bInAppendOperation);

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
				Constraints.SinglePointConstraints[Idx].GetConstraintHandle()->SetConstraintIndex(Idx, FCollisionConstraintBase::FType::SinglePoint);
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
				Constraints.SinglePointSweptConstraints[Idx].GetConstraintHandle()->SetConstraintIndex(Idx, FCollisionConstraintBase::FType::SinglePointSwept);
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
			check(Handles.Num() == Constraints.SinglePointConstraints.Num() + Constraints.SinglePointSweptConstraints.Num());

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
			Collisions::Update(Contact, Dt);
			if (Contact.GetPhi() < Contact.GetCullDistance())
			{
				Contact.Timestamp = LifespanCounter;
			}
		}
	}

	Collisions::FContactParticleParameters FPBDCollisionConstraints::GetContactParticleParameters(const FReal Dt)
	{
		return { 
			(CollisionRestitutionThresholdOverride >= 0.0f) ? CollisionRestitutionThresholdOverride * Dt : RestitutionThreshold * Dt,
			CollisionCanAlwaysDisableContacts ? true : (CollisionCanNeverDisableContacts ? false : bCanDisableContacts),
			&MCollided,

		};
	}

	Collisions::FContactIterationParameters FPBDCollisionConstraints::GetContactIterationParameters(const FReal Dt, const int32 Iteration, const int32 NumIterations, const int32 NumPairIterations, bool& bNeedsAnotherIteration)
	{
		return {
			Dt, 
			Iteration, 
			NumIterations, 
			NumPairIterations, 
			SolverType, 
			&bNeedsAnotherIteration
		};
	}

	bool FPBDCollisionConstraints::Apply(const FReal Dt, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		bool bNeedsAnotherIteration = false;
		if (MApplyPairIterations > 0)
		{
			const Collisions::FContactParticleParameters ParticleParameters = GetContactParticleParameters(Dt);
			const Collisions::FContactIterationParameters IterationParameters = GetContactIterationParameters(Dt, Iterations, NumIterations, MApplyPairIterations, bNeedsAnotherIteration);

			NumActivePointConstraints = 0;
			for (FRigidBodyPointContactConstraint& Contact : Constraints.SinglePointConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::Apply(Contact, IterationParameters, ParticleParameters);
					++NumActivePointConstraints;
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
			const Collisions::FContactParticleParameters ParticleParameters = GetContactParticleParameters(Dt);
			const Collisions::FContactIterationParameters IterationParameters = GetContactIterationParameters(Dt, Iterations, NumIterations, MApplyPushOutPairIterations, bNeedsAnotherIteration);

			for (FRigidBodyPointContactConstraint& Contact : Constraints.SinglePointConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::ApplyPushOut(Contact, TempStatic, IterationParameters, ParticleParameters);
				}
			}

			for (FRigidBodySweptPointContactConstraint& Contact : Constraints.SinglePointSweptConstraints)
			{
				if (!Contact.GetDisabled())
				{
					Collisions::ApplyPushOut(Contact, TempStatic, IterationParameters, ParticleParameters);
				}
			}
		}

		if (PostApplyPushOutCallback != nullptr)
		{
			PostApplyPushOutCallback(Dt, Handles, bNeedsAnotherIteration);
		}

		return bNeedsAnotherIteration;
	}

	void FPBDCollisionConstraints::SortConstraints()
	{
		check(!bInAppendOperation);

		Algo::Sort(Handles, [](const FPBDCollisionConstraintHandle* A, const FPBDCollisionConstraintHandle* B)
		{
			if(A->GetType() == B->GetType())
			{
				return A->GetContact() < B->GetContact();
			}
			else
			{
				return A->GetType() < B->GetType();
			}
		});
	}

	bool FPBDCollisionConstraints::Apply(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>& InConstraintHandles, const int32 Iterations, const int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_Collisions_Apply);

		TAtomic<bool> bNeedsAnotherIterationAtomic;
		bNeedsAnotherIterationAtomic.Store(false);
		if (MApplyPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) 
			{
				FPBDCollisionConstraintHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);

				TVector<const TGeometryParticleHandle<FReal, 3>*, 2> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
				bool bNeedsAnotherIteration = false;

				if (!ConstraintHandle->GetContact().GetDisabled())
				{
					const Collisions::FContactParticleParameters ParticleParameters = GetContactParticleParameters(Dt);
					const Collisions::FContactIterationParameters IterationParameters = GetContactIterationParameters(Dt, Iterations, NumIterations, MApplyPairIterations, bNeedsAnotherIteration);
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
					const Collisions::FContactParticleParameters ParticleParameters = GetContactParticleParameters(Dt);
					const Collisions::FContactIterationParameters IterationParameters = GetContactIterationParameters(Dt, Iteration, NumIterations, MApplyPushOutPairIterations, bNeedsAnotherIteration);
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

		return Constraints.SinglePointSweptConstraints[Index];
	}

	FPBDCollisionConstraints::FConstraintAppendScope::FConstraintAppendScope(FPBDCollisionConstraints* InOwner)
		: Owner(InOwner)
	{
		check(Owner);
		Owner->bInAppendOperation = true;
		Constraints = &(Owner->Constraints);

		NumBeginSingle = Owner->Constraints.SinglePointConstraints.Num();
		NumBeginSingleSwept = Owner->Constraints.SinglePointSweptConstraints.Num();
	}

	FPBDCollisionConstraints::FConstraintAppendScope::~FConstraintAppendScope()
	{
		FPBDCollisionConstraints::FConstraintHandleAllocator& HandleAlloc = Owner->HandleAllocator;
		int32 HandlesBeginIndex = Owner->Handles.Num();
		const int32 TotalAdded = NumAddedSingle + NumAddedSingleSwept;

		Owner->Handles.AddUninitialized(TotalAdded);
		const int32 NumHandles = Owner->Handles.Num();

		for(int32 HandleIndex = 0; HandleIndex < NumAddedSingle ; ++HandleIndex)
		{
			FPBDCollisionConstraintHandle* NewHandle = HandleAlloc.template AllocHandle<FRigidBodyPointContactConstraint>(Owner, NumBeginSingle + HandleIndex);
			
			const int32 FullHandleIndex = HandlesBeginIndex + HandleIndex;
			Owner->Handles[FullHandleIndex] = NewHandle;

			NewHandle->GetContact().Timestamp = -INT_MAX;
			Constraints->SinglePointConstraints[NumBeginSingle + HandleIndex].SetConstraintHandle(NewHandle);
		}
		HandlesBeginIndex += NumAddedSingle;

		for(int32 HandleIndex = 0; HandleIndex < NumAddedSingleSwept; ++HandleIndex)
		{
			FPBDCollisionConstraintHandle* NewHandle = HandleAlloc.template AllocHandle<FRigidBodySweptPointContactConstraint>(Owner, NumBeginSingleSwept + HandleIndex);

			const int32 FullHandleIndex = HandlesBeginIndex + HandleIndex;
			Owner->Handles[FullHandleIndex] = NewHandle;

			NewHandle->GetContact().Timestamp = -INT_MAX;
			Constraints->SinglePointSweptConstraints[NumBeginSingleSwept + HandleIndex].SetConstraintHandle(NewHandle);
		}
		HandlesBeginIndex += NumAddedSingle;

		Owner->bInAppendOperation = false;
	}

	void FPBDCollisionConstraints::FConstraintAppendScope::ReserveSingle(int32 NumToAdd)
	{
		Constraints->SinglePointConstraints.Reserve(Constraints->SinglePointConstraints.Num() + NumToAdd);
	}

	void FPBDCollisionConstraints::FConstraintAppendScope::ReserveSingleSwept(int32 NumToAdd)
	{
		Constraints->SinglePointSweptConstraints.Reserve(Constraints->SinglePointConstraints.Num() + NumToAdd);
	}

	void FPBDCollisionConstraints::FConstraintAppendScope::Append(TArray<FRigidBodyPointContactConstraint>&& InConstraints)
	{
		if(InConstraints.Num() == 0)
		{
			return;
		}

		NumAddedSingle += InConstraints.Num();
		Constraints->SinglePointConstraints.Append(MoveTemp(InConstraints));
	}

	void FPBDCollisionConstraints::FConstraintAppendScope::Append(TArray<FRigidBodySweptPointContactConstraint>&& InConstraints)
	{
		if(InConstraints.Num() == 0)
		{
			return;
		}

		NumAddedSingleSwept += InConstraints.Num();
		Constraints->SinglePointSweptConstraints.Append(MoveTemp(InConstraints));
	}

}
