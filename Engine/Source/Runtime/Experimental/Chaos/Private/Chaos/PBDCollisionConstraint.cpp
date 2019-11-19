// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionConstraintImp.h"

#include "Chaos/Capsule.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/CollisionResolutionAlgo.h"
#include "Chaos/CollisionResolutionConvexConvex.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ISpatialAccelerationCollection.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/PBDCollisionConstraintSingleContactUtil.h"

#if INTEL_ISPC
#include "PBDCollisionConstraint.ispc.generated.h"
#endif

//#pragma optimize("", off)

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


extern int32 UseLevelsetCollision;

#if !UE_BUILD_SHIPPING
namespace Chaos
{
	int32 CHAOS_API PendingHierarchyDump = 0;
}
#endif

namespace Chaos
{
	CHAOS_API int32 EnableCollisions = 1;
	FAutoConsoleVariableRef CVarEnableCollisions(TEXT("p.EnableCollisions"), EnableCollisions, TEXT("Enable/Disable collisions on the Chaos solver."));



	//
	// Collision Constraint Container
	//

	template<typename T, int d>
	TPBDCollisionConstraint<T, d>::TPBDCollisionConstraint(
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

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::Reset"), STAT_CollisionConstraintsReset, STATGROUP_Chaos);

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::Reset()
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
	void TPBDCollisionConstraint<T, d>::RemoveConstraint(FConstraintContainerHandle* Handle)
	{
		int32 Idx = Handle->GetConstraintIndex();
		typename TCollisionConstraintBase<T, d>::FType ConstraintType = Handle->GetType();

		Handles.RemoveAtSwap(Idx);
		Constraints.RemoveAtSwap(Idx);
		if (Idx < Constraints.Num())
		{
			Handles[Idx]->SetConstraintIndex(Idx, ConstraintType);
		}

		ensure(Handles.Num() == Constraints.Num());
		HandleAllocator.FreeHandle(Handle);
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>&  InHandleSet)
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
	void TPBDCollisionConstraint<T, d>::ApplyCollisionModifier(const TFunction<ECollisionModifierResult(const FConstraintContainerHandle* Handle)>& CollisionModifier)
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
	void TPBDCollisionConstraint<T, d>::SetPostComputeCallback(const TRigidBodyContactConstraintsPostComputeCallback<T, d>& Callback)
	{
		PostComputeCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ClearPostComputeCallback()
	{
		PostComputeCallback = nullptr;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::SetPostApplyCallback(const TRigidBodyContactConstraintsPostApplyCallback<T, d>& Callback)
	{
		PostApplyCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ClearPostApplyCallback()
	{
		PostApplyCallback = nullptr;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::SetPostApplyPushOutCallback(const TRigidBodyContactConstraintsPostApplyPushOutCallback<T, d>& Callback)
	{
		PostApplyPushOutCallback = Callback;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ClearPostApplyPushOutCallback()
	{
		PostApplyPushOutCallback = nullptr;
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::UpdatePositionBasedState(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ const T Dt)
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

	DEFINE_STAT(STAT_ComputeConstraints);
	DEFINE_STAT(STAT_ComputeConstraintsNP);
	DEFINE_STAT(STAT_ComputeConstraintsBP);
	DEFINE_STAT(STAT_ComputeConstraintsSU);

	template<typename T, int d>
	template <bool bGatherStats>
	void TPBDCollisionConstraint<T, d>::ComputeConstraints(const FAccelerationStructure& AccelerationStructure, T Dt)
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
	void TPBDCollisionConstraint<T, d>::UpdateConstraints(T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles)
	{
		// Clustering uses update constraints to force a re-evaluation. 
	}


	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::Apply(const T Dt, FRigidBodyContactConstraint& Constraint, const int32 It, const int32 NumIts)
	{

		{
			// 
			// @todo(ccaulfield,brice): I think we should never get this? Revisit after particle handle refactor
			// 

			TGenericParticleHandle<T, d> Particle0 = TGenericParticleHandle<T, d>(Constraint.Particle[0]);
			TGenericParticleHandle<T, d> Particle1 = TGenericParticleHandle<T, d>(Constraint.Particle[1]);
			TPBDRigidParticleHandle<T, d>* PBDRigid0 = Particle0->AsDynamic();
			TPBDRigidParticleHandle<T, d>* PBDRigid1 = Particle1->AsDynamic();

			if (Particle0->Sleeping())
			{
				ensure(!PBDRigid1 || PBDRigid1->Sleeping());
				return;
			}
			if (Particle1->Sleeping())
			{
				ensure(!PBDRigid0 || PBDRigid0->Sleeping());
				return;
			}
		}

		Collisions::TSingleContParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
		Collisions::TSingleContactIterationParameters<T> IterationParameters = { Dt, It, NumIts, MApplyPairIterations, nullptr };
		Collisions::Apply(Constraint,MThickness, IterationParameters,  ParticleParameters);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::Apply"), STAT_Apply, STATGROUP_Chaos);
	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Apply);
		if (MApplyPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);
				Apply(Dt, Constraints[ConstraintHandle->GetConstraintIndex()], It, NumIts);
			}, bDisableCollisionParallelFor);
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, InConstraintHandles);
		}
	}


	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ApplyPushOut(const T Dt, FRigidBodyContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations, bool &NeedsAnotherIteration)
	{
		{
			// 
			// @todo(ccaulfield,brice): I think we should never get this? Revisit after particle handle refactor
			// 

			TGenericParticleHandle<T, d> Particle0 = TGenericParticleHandle<T, d>(Constraint.Particle[0]);
			TGenericParticleHandle<T, d> Particle1 = TGenericParticleHandle<T, d>(Constraint.Particle[1]);
			TPBDRigidParticleHandle<T, d>* PBDRigid0 = Particle0->AsDynamic();
			TPBDRigidParticleHandle<T, d>* PBDRigid1 = Particle1->AsDynamic();

			if (Particle0->Sleeping())
			{
				ensure(!PBDRigid1 || PBDRigid1->Sleeping());
				return;
			}
			if (Particle1->Sleeping())
			{
				ensure(!PBDRigid0 || PBDRigid0->Sleeping());
				return;
			}
		}

		Collisions::TSingleContParticleParameters<T> ParticleParameters = { &MCollided, &MPhysicsMaterials, CollisionFrictionOverride, MAngularFriction };
		Collisions::TSingleContactIterationParameters<T> IterationParameters = { Dt, Iteration, NumIterations, MApplyPairIterations, &NeedsAnotherIteration };
		Collisions::ApplyPushOut(Constraint, MThickness, IsTemporarilyStatic, IterationParameters, ParticleParameters);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::ApplyPushOut"), STAT_ApplyPushOut, STATGROUP_Chaos);
	template<typename T, int d>
	bool TPBDCollisionConstraint<T, d>::ApplyPushOut(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const TSet< const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
	{
		SCOPE_CYCLE_COUNTER(STAT_ApplyPushOut);

		bool NeedsAnotherIteration = false;
		if (MApplyPushOutPairIterations > 0)
		{
			PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
				FConstraintContainerHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
				check(ConstraintHandle != nullptr);
				ApplyPushOut(Dt, Constraints[ConstraintHandle->GetConstraintIndex()], IsTemporarilyStatic, Iteration, NumIterations, NeedsAnotherIteration);
			}, bDisableCollisionParallelFor);
		}

		if (PostApplyPushOutCallback != nullptr)
		{
			PostApplyPushOutCallback(Dt, InConstraintHandles, NeedsAnotherIteration);
		}
		return NeedsAnotherIteration;
	}


	template<typename T, int d>
	TRigidTransform<T, d> GetTransform(const TGeometryParticleHandle<T, d>* Particle)
	{
		TGenericParticleHandle<T, d> Generic = const_cast<TGeometryParticleHandle<T, d>*>(Particle);	//TODO: give a const version of the generic API
		return TRigidTransform<T, d>(Generic->P(), Generic->Q());
	}

	template<typename T, int d>
	void TPBDCollisionConstraint<T, d>::ConstructConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness, TRigidBodySingleContactConstraint<T, d> & Constraint)
	{
		if (ensure(Particle0 && Particle1))
		{
			Collisions::ConstructConstraintsImpl<T, d>(Particle0, Particle1, Particle0->Geometry().Get(), Particle1->Geometry().Get(), Thickness, Constraint);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateConstraint"), STAT_UpdateConstraint, STATGROUP_ChaosWide);

	template<typename T, int d>
	template<ECollisionUpdateType UpdateType>
	void TPBDCollisionConstraint<T, d>::UpdateConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConstraint);
		Collisions::UpdateConstraint<UpdateType>(Thickness, Constraint);
	}

	template class TPBDCollisionConstraintHandle<float, 3>;
	template class TAccelerationStructureHandle<float, 3>;
	template class CHAOS_API TPBDCollisionConstraint<float, 3>;
	template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
	template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
	template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<false>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);
	template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<true>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);
}
