// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionConstraintImp.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/Capsule.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/ISpatialAccelerationCollection.h"

#if INTEL_ISPC
#include "PBDCollisionConstraint.ispc.generated.h"
#endif

int32 CollisionParticlesBVHDepth = 4;
FAutoConsoleVariableRef CVarCollisionParticlesBVHDepth(TEXT("p.CollisionParticlesBVHDepth"), CollisionParticlesBVHDepth, TEXT("The maximum depth for collision particles bvh"));


int32 ConstraintBPBVHDepth = 2;
FAutoConsoleVariableRef CVarConstraintBPBVHDepth(TEXT("p.ConstraintBPBVHDepth"), ConstraintBPBVHDepth, TEXT("The maximum depth for constraint bvh"));

int32 BPTreeOfGrids = 1;
FAutoConsoleVariableRef CVarBPTreeOfGrids(TEXT("p.BPTreeOfGrids"), BPTreeOfGrids, TEXT("Whether to use a seperate tree of grids for bp"));


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

template<ECollisionUpdateType, typename T, int d>
void UpdateConstraintImp2(const TRigidTransform<T, d>& ParticleTM, const TImplicitObject<T, d>& LevelsetObject, const TRigidTransform<T, d>& LevelsetTM, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint);

//
// Collision Constraint Handle
//

template<typename T, int d>
TPBDCollisionConstraintHandle<T, d>::TPBDCollisionConstraintHandle()
{
}

template<typename T, int d>
TPBDCollisionConstraintHandle<T, d>::TPBDCollisionConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex)
	: TContainerConstraintHandle<TPBDCollisionConstraint<T, d>>(InConstraintContainer, InConstraintIndex)
{
}

template<typename T, int d>
const TRigidBodyContactConstraint<T, d>& TPBDCollisionConstraintHandle<T, d>::GetContact() const
{
	return ConstraintContainer->Constraints[ConstraintIndex];
}


//
// Collision Constraint Container
//

template<typename T, int d>
TPBDCollisionConstraint<T, d>::TPBDCollisionConstraint(const TPBDRigidsSOAs<T,d>& InParticles, TArrayCollectionArray<bool>& Collided, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& InPerParticleMaterials, const int32 PairIterations /*= 1*/, const T Thickness /*= (T)0*/)
	: Particles(InParticles)
	, SpatialAcceleration(nullptr)
	, MCollided(Collided)
	, MPhysicsMaterials(InPerParticleMaterials)
	, bEnableVelocitySolve(true)
	, MPairIterations(PairIterations)
	, MThickness(Thickness)
	, MAngularFriction(0)
	, bUseCCD(false)
	, bEnableCollisions(true)
	, LifespanCounter(0)
	, PostComputeCallback(nullptr)
	, PostApplyCallback(nullptr)
	, PostApplyPushOutCallback(nullptr)
{
}

DECLARE_CYCLE_STAT(TEXT("CollisionConstraint::Reset"), STAT_CollisionConstraintsReset, STATGROUP_Chaos);

template<typename T, int d>
void TPBDCollisionConstraint<T, d>::Reset(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices*/)
{
	SCOPE_CYCLE_COUNTER(STAT_CollisionConstraintsReset);

	int32 Threshold = LifespanCounter - 1; // Maybe this should be solver time?
	for (int32 Idx = Constraints.Num() - 1; Idx >= 0; Idx--)
	{
		if (Constraints[Idx].Lifespan < Threshold)
		{
			RemoveConstraint(Idx);
		}
	}

	MAngularFriction = 0;
	bUseCCD = false;
}

template<typename T, int d>
void TPBDCollisionConstraint<T, d>::RemoveConstraint(int32 Idx)
{
	HandleAllocator.FreeHandle(Handles.FindAndRemoveChecked(GetConstraintHandleID(Idx)));
	Constraints.RemoveAtSwap(Idx);
	if (Idx < Constraints.Num())
	{
		Handles[GetConstraintHandleID(Idx)]->SetConstraintIndex(Idx);
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

CHAOS_API int32 CollisionConstraintsForceSingleThreaded = 0;
FAutoConsoleVariableRef CVarCollisionConstraintsForceSingleThreaded(TEXT("p.Chaos.Collision.ForceSingleThreaded"), CollisionConstraintsForceSingleThreaded, TEXT("CollisionConstraintsForceSingleThreaded"));

template<typename T, int d>
template <bool bGatherStats>
void TPBDCollisionConstraint<T, d>::ComputeConstraints(const FAccelerationStructure& AccelerationStructure, T Dt)
{
	if (const auto AABBTree = AccelerationStructure.template As<TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>>())
	{
		ComputeConstraintsHelperLowLevel<bGatherStats>(*AABBTree, Dt);
	}
	else if(const auto BV = AccelerationStructure.template As<TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>>())
	{
		ComputeConstraintsHelperLowLevel<bGatherStats>(*BV, Dt);
	}
	else if (const auto AABBTreeBV = AccelerationStructure.template As<TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>>())
	{
		ComputeConstraintsHelperLowLevel<bGatherStats>(*AABBTreeBV, Dt);
	}
	else if(const auto Collection = AccelerationStructure.template As<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>())
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
		PostComputeCallback(Constraints);
	}
}


DECLARE_CYCLE_STAT(TEXT("UpdateConstraints"), STAT_UpdateConstraints, STATGROUP_Chaos);

template <typename T, int d>
template<typename SPATIAL_ACCELERATION>
void TPBDCollisionConstraint<T, d>::UpdateConstraintsHelper(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles, SPATIAL_ACCELERATION& InSpatialAcceleration)
{
#if CHAOS_PARTICLEHANDLE_TODO
	SCOPE_CYCLE_COUNTER(STAT_UpdateConstraints);
	double Time = 0;
	FDurationTimer Timer(Time);

	TArray<uint32> AddedParticlesArray = AddedParticles.Array();
	TArray<uint32> NewActiveIndices = ActiveParticles;
	NewActiveIndices.Append(AddedParticlesArray);

	//
	// Broad phase
	//

	{
		//QUICK_SCOPE_CYCLE_COUNTER(Reinitialize);
		// @todo(mlentine): We only need to construct the hierarchy for the islands we care about
		//TBoundingVolumeHierarchy<TPBDRigidParticles<T, d>, TArray<int32>, T, d> LocalHierarchy(InParticles, ActiveParticles, true, Dt * BoundsThicknessMultiplier); 	//todo(ocohen): should we pass MThickness into this structure?
		SpatialAcceleration.Reinitialize((const TArray<uint32>&)NewActiveIndices, true, Dt * BoundsThicknessMultiplier); //todo: faster path when adding just a few
		Timer.Stop();
		UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Construct Hierarchy %f"), Time);
	}

	//
	// Narrow phase
	//

	FCriticalSection CriticalSection;
	Time = 0;
	Timer.Start();
	
	TQueue<TRigidBodyContactConstraint<T, d>, EQueueMode::Mpsc> Queue;	//todo(ocohen): use per thread buffer instead, need better support than ParallelFor for this
	//SpatialAcceleration.AddElements(AddedParticlesArray);	not supported by bvh so just reinitializing. Should probably improve this later
	PhysicsParallelFor(AddedParticlesArray.Num(), [&](int32 Index) {
		int32 Body1Index = AddedParticlesArray[Index];
		if (InParticles.Disabled(Body1Index))
		{
			return;
		}
		if (InParticles.InvM(Body1Index) == 0)
		{
			return;
		}
		TArray<int32> PotentialIntersections;
		TBox<T, d> Box1;
		T Box1Thickness = (T)0;

		const bool bBody1Bounded = HasBoundingBox(InParticles, Body1Index);
		if (bBody1Bounded)
		{
			Box1 = SpatialAcceleration.GetWorldSpaceBoundingBox(InParticles, Body1Index);
			Box1Thickness = ComputeThickness(InParticles, Dt, Body1Index).Size();
			PotentialIntersections = SpatialAcceleration.FindAllIntersections(Box1);
		}
		else
		{
			PotentialIntersections = SpatialAcceleration.GlobalObjects();
		}
		for (int32 i = 0; i < PotentialIntersections.Num(); ++i)
		{
			int32 Body2Index = PotentialIntersections[i];
			const bool bBody2Bounded = HasBoundingBox(InParticles, Body2Index);

			if(InParticles.Disabled(Body2Index))
			{
				// Can't collide with disabled objects
				continue;
			}

			if (Body1Index == Body2Index || ((bBody1Bounded == bBody2Bounded) && AddedParticles.Contains(Body2Index) && AddedParticles.Contains(Body1Index) && Body2Index > Body1Index))
			{
				continue;
			}

			if (InParticles.InvM(Body1Index) && InParticles.InvM(Body2Index) && (InParticles.Island(Body1Index) != InParticles.Island(Body2Index)))	//todo(ocohen): this is a hack - we should not even consider dynamics from other islands
			{
				continue;
			}

			if (!InParticles.Geometry(Body1Index) && !InParticles.Geometry(Body2Index))
			{
				continue;
			}

			if (bBody1Bounded && bBody2Bounded)
			{	
				const TBox<T, d>& Box2 = SpatialAcceleration.GetWorldSpaceBoundingBox(InParticles, Body2Index);
				if (!Box1.Intersects(Box2))
				{
					continue;
				}
			}

			//todo(ocohen): this should not be needed in theory, but in practice we accidentally merge islands. We should be doing this test within an island for clusters
			if (InParticles.Island(Body1Index) >= 0 && InParticles.Island(Body2Index) >= 0 && InParticles.Island(Body1Index) != InParticles.Island(Body2Index))
			{
				continue;
			}

			const TVector<T, d> Box2Thickness = ComputeThickness(InParticles, Dt, Body2Index);
			const T UseThickness = FMath::Max(Box1Thickness, Box2Thickness.Size());// + MThickness

			auto Constraint = ComputeConstraint(InParticles, Body1Index, Body2Index, UseThickness);

			//if (true || !InParticles.Geometry(Body1Index)->HasBoundingBox() || !InParticles.Geometry(Body2Index)->HasBoundingBox())
			{
				//use narrow phase to determine if constraint is needed. Without this we can't do shock propagation
				if (ComputeConstraintsUseAny)
				{
					UpdateConstraint<ECollisionUpdateType::Any>(InParticles, UseThickness, Constraint);
				}
				else
				{
					UpdateConstraint<ECollisionUpdateType::Deepest>(InParticles, UseThickness, Constraint);
				}
				if (Constraint.Phi < UseThickness)
				{
					Queue.Enqueue(Constraint);
				}
			}
			/*else
			{
			CriticalSection.Lock();
			Constraints.Add(Constraint);
			CriticalSection.Unlock();
			}*/
		}
	});
	while (!Queue.IsEmpty())
	{
		const TRigidBodyContactConstraint<T, d> * Constraint = Queue.Peek();
		FConstraintHandleID HandleID = GetConstraintHandleID(*Constraint);
		if (Handles.Contains(HandleID))
		{
			FConstraintHandle* Handle = Handles[HandleID];
			int32 Idx = Handle->GetConstraintIndex();
			Queue.Dequeue(Constraints[Idx]);
			Constraints[Idx].Lifespan = LifespanCounter;
		}
		else
		{
			int32 Idx = Constraints.AddUninitialized(1);
			Queue.Dequeue(Constraints[Idx]);
			Handles.Add(GetConstraintHandleID(Idx), HandleAllocator.AllocHandle(this, Idx));
			Constraints[Idx].Lifespan = LifespanCounter;
		}
	}
	LifespanCounter++;

	Timer.Stop();
	UE_LOG(LogChaos, Verbose, TEXT("\tPBDCollisionConstraint Update %d Constraints with Potential Collisions %f"), Constraints.Num(), Time);
#endif
}

DECLARE_CYCLE_STAT(TEXT("Reconcile Updated Constraints"), STAT_ReconcileConstraints2, STATGROUP_Chaos);
template<typename T, int d>
void TPBDCollisionConstraint<T, d>::UpdateConstraints(/*const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices,*/ T Dt, const TSet<TGeometryParticleHandle<T, d>*>& AddedParticles)
{
#if CHAOS_PARTICLEHANDLE_TODO
	{
		SCOPE_CYCLE_COUNTER(STAT_ReconcileConstraints2);

		// Updating post-clustering, we will have invalid constraints
		int32 NumRemovedConstraints = 0;
		for(int32 i = 0; i < Constraints.Num(); ++i)
		{
			const FRigidBodyContactConstraint& Constraint = Constraints[i];
			if(InParticles.Disabled(Constraint.ParticleIndex) || InParticles.Disabled(Constraint.LevelsetIndex))
			{
				Constraints.RemoveAtSwap(i);
				HandleAllocator.FreeHandle(Handles[i]);
				Handles.RemoveAtSwap(i);
				if (i < Handles.Num())
				{
					SetConstraintIndex(Handles[i], i);
				}
				++NumRemovedConstraints;
				i--;
			}
		}

		if(NumRemovedConstraints > 0)
		{
			UE_LOG(LogChaos, Verbose, TEXT("TPBDCollisionConstraint::UpdateConstraints - Needed to remove %d constraints because they contained disabled particles."), NumRemovedConstraints);
		}
	}

	if (BPTreeOfGrids)
	{
		UpdateConstraintsHelper(InParticles, InIndices, Dt, AddedParticles, ActiveParticles, SpatialAccelerationResource2);
	}
	else
	{
		UpdateConstraintsHelper(InParticles, InIndices, Dt, AddedParticles, ActiveParticles, SpatialAccelerationResource);
	}
#endif
}

// @todo(ccaulfield): This is duplicated in JointConstraints - move to a utility file
template<typename T>
PMatrix<T, 3, 3> ComputeFactorMatrix3(const TVector<T, 3>& V, const PMatrix<T, 3, 3>& M, const T& Im)
{
	// Rigid objects rotational contribution to the impulse.
	// Vx*M*VxT+Im
	check(Im > FLT_MIN)
	return PMatrix<T, 3, 3>(
		-V[2] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]) + V[1] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]) + Im,
		V[2] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) - V[0] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]),
		-V[1] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) + V[0] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]),
		V[2] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) - V[0] * (V[2] * M.M[2][0] - V[0] * M.M[2][2]) + Im,
		-V[1] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) + V[0] * (V[2] * M.M[1][0] - V[0] * M.M[2][1]),
		-V[1] * (-V[1] * M.M[0][0] + V[0] * M.M[1][0]) + V[0] * (-V[1] * M.M[1][0] + V[0] * M.M[1][1]) + Im);
}

template<typename T, int d>
TVector<T, d> GetEnergyClampedImpulse(const TRigidBodyContactConstraint<T, d>& Constraint, const TVector<T, d>& Impulse, const TVector<T, d>& VectorToPoint1, const TVector<T, d>& VectorToPoint2, const TVector<T, d>& Velocity1, const TVector<T, d>& Velocity2)
{
	TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint.Particle->AsDynamic();
	TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint.Levelset->AsDynamic();

	TVector<T, d> Jr0, Jr1, IInvJr0, IInvJr1;
	T ImpulseRatioNumerator0 = 0, ImpulseRatioNumerator1 = 0, ImpulseRatioDenom0 = 0, ImpulseRatioDenom1 = 0;
	T ImpulseSize = Impulse.SizeSquared();
	TVector<T, d> KinematicVelocity = !PBDRigid0 ? Velocity1 : !PBDRigid1 ? Velocity2 : TVector<T, d>(0);
	if (PBDRigid0)
	{
		Jr0 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse);
		IInvJr0 = PBDRigid0->Q().RotateVector(PBDRigid0->InvI() * PBDRigid0->Q().UnrotateVector(Jr0));
		ImpulseRatioNumerator0 = TVector<T, d>::DotProduct(Impulse, PBDRigid0->V() - KinematicVelocity) + TVector<T, d>::DotProduct(IInvJr0, PBDRigid0->W());
		ImpulseRatioDenom0 = ImpulseSize / PBDRigid0->M() + TVector<T, d>::DotProduct(Jr0, IInvJr0);
	}
	if (PBDRigid1)
	{
		Jr1 = TVector<T, d>::CrossProduct(VectorToPoint2, Impulse);
		IInvJr1 = PBDRigid1->Q().RotateVector(PBDRigid1->InvI() * PBDRigid1->Q().UnrotateVector(Jr1));
		ImpulseRatioNumerator1 = TVector<T, d>::DotProduct(Impulse, PBDRigid1->V() - KinematicVelocity) + TVector<T, d>::DotProduct(IInvJr1, PBDRigid1->W());
		ImpulseRatioDenom1 = ImpulseSize / PBDRigid1->M() + TVector<T, d>::DotProduct(Jr1, IInvJr1);
	}
	T Numerator = -2 * (ImpulseRatioNumerator0 - ImpulseRatioNumerator1);
	if (Numerator < 0)
	{
		return TVector<T, d>(0);
	}
	check(Numerator >= 0);
	T Denominator = ImpulseRatioDenom0 + ImpulseRatioDenom1;
	return Numerator < Denominator ? (Impulse * Numerator / Denominator) : Impulse;
}

template<typename T, int d>
void TPBDCollisionConstraint<T, d>::Apply(const T Dt, FRigidBodyContactConstraint& Constraint)
{
	TGeometryParticleHandle<T, d>* Particle0 = Constraint.Particle;
	TGeometryParticleHandle<T, d>* Particle1 = Constraint.Levelset;
	TPBDRigidParticleHandle<T, d>* PBDRigid0 = Particle0->AsDynamic();
	TPBDRigidParticleHandle<T, d>* PBDRigid1 = Particle1->AsDynamic();

	// @todo(ccaulfield): I think we should never get this? Revisit after particle handle refactor
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
	UpdateConstraint<ECollisionUpdateType::Deepest>(MThickness, Constraint);
	if (Constraint.Phi >= MThickness)
	{
		return;
	}

	// @todo(ccaulfield): CHAOS_PARTICLEHANDLE_TODO what's the best way to manage external per-particle data?
	Particle0->AuxilaryValue(MCollided) = true;
	Particle1->AuxilaryValue(MCollided) = true;

	// @todo(ccaulfield): CHAOS_PARTICLEHANDLE_TODO split function to avoid ifs
	const TVector<T, d> ZeroVector = TVector<T, d>(0);
	const TRotation<T, d>& Q0 = PBDRigid0? PBDRigid0->Q() : Particle0->R();
	const TRotation<T, d>& Q1 = PBDRigid1? PBDRigid1->Q() : Particle1->R();
	const TVector<T, d>& P0 = PBDRigid0 ? PBDRigid0->P() : Particle0->X();
	const TVector<T, d>& P1 = PBDRigid1 ? PBDRigid1->P() : Particle1->X();
	const TVector<T, d>& V0 = PBDRigid0 ? PBDRigid0->V() : ZeroVector;
	const TVector<T, d>& V1 = PBDRigid1 ? PBDRigid1->V() : ZeroVector;
	const TVector<T, d>& W0 = PBDRigid0 ? PBDRigid0->W() : ZeroVector;
	const TVector<T, d>& W1 = PBDRigid1 ? PBDRigid1->W() : ZeroVector;
	TSerializablePtr<TChaosPhysicsMaterial<T>> PhysicsMaterial0 = Particle0->AuxilaryValue(MPhysicsMaterials);
	TSerializablePtr<TChaosPhysicsMaterial<T>> PhysicsMaterial1 = Particle1->AuxilaryValue(MPhysicsMaterials);

	TVector<T, d> VectorToPoint1 = Constraint.Location - P0;
	TVector<T, d> VectorToPoint2 = Constraint.Location - P1;
	TVector<T, d> Body1Velocity = V0 + TVector<T, d>::CrossProduct(W0, VectorToPoint1);
	TVector<T, d> Body2Velocity = V1 + TVector<T, d>::CrossProduct(W1, VectorToPoint2);
	TVector<T, d> RelativeVelocity = Body1Velocity - Body2Velocity;
	if (TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal) < 0) // ignore separating constraints
	{
		PMatrix<T, d, d> WorldSpaceInvI1 = PBDRigid0 ? (Q0 * FMatrix::Identity).GetTransposed() * PBDRigid0->InvI() * (Q0 * FMatrix::Identity) : PMatrix<T, d, d>(0);
		PMatrix<T, d, d> WorldSpaceInvI2 = PBDRigid1 ? (Q1 * FMatrix::Identity).GetTransposed() * PBDRigid1->InvI() * (Q1 * FMatrix::Identity) : PMatrix<T, d, d>(0);
		PMatrix<T, d, d> Factor =
			(PBDRigid0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : PMatrix<T, d, d>(0)) +
			(PBDRigid1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : PMatrix<T, d, d>(0));
		TVector<T, d> Impulse;
		TVector<T, d> AngularImpulse(0);

		// Resting contact if very close to the surface
		T Restitution = (T)0;
		T Friction = (T)0;
		bool bApplyRestitution = (RelativeVelocity.Size() > (2 * 980 * Dt));
		if (PhysicsMaterial0 && PhysicsMaterial1)
		{
			if (bApplyRestitution)
			{
				Restitution = FMath::Min(PhysicsMaterial0->Restitution, PhysicsMaterial1->Restitution);
			}
			Friction = FMath::Max(PhysicsMaterial0->Friction, PhysicsMaterial1->Friction);
		}
		else if (PhysicsMaterial0)
		{
			if (bApplyRestitution)
			{
				Restitution = PhysicsMaterial0->Restitution;
			}
			Friction = PhysicsMaterial0->Friction;
		}
		else if (PhysicsMaterial1)
		{
			if (bApplyRestitution)
			{
				Restitution = PhysicsMaterial1->Restitution;
			}
			Friction = PhysicsMaterial1->Friction;
		}

		if (Friction)
		{
			T RelativeNormalVelocity = TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal);
			if (RelativeNormalVelocity > 0)
			{
				RelativeNormalVelocity = 0;
			}
			TVector<T, d> VelocityChange = -(Restitution * RelativeNormalVelocity * Constraint.Normal + RelativeVelocity);
			T NormalVelocityChange = TVector<T, d>::DotProduct(VelocityChange, Constraint.Normal);
			PMatrix<T, d, d> FactorInverse = Factor.Inverse();
			TVector<T, d> MinimalImpulse = FactorInverse * VelocityChange;
			const T MinimalImpulseDotNormal = TVector<T, d>::DotProduct(MinimalImpulse, Constraint.Normal);
			const T TangentialSize = (MinimalImpulse - MinimalImpulseDotNormal * Constraint.Normal).Size();
			if (TangentialSize <= Friction * MinimalImpulseDotNormal)
			{
				//within friction cone so just solve for static friction stopping the object
				Impulse = MinimalImpulse;
				if (MAngularFriction)
				{
					TVector<T, d> RelativeAngularVelocity = W0 - W1;
					T AngularNormal = TVector<T, d>::DotProduct(RelativeAngularVelocity, Constraint.Normal);
					TVector<T, d> AngularTangent = RelativeAngularVelocity - AngularNormal * Constraint.Normal;
					TVector<T, d> FinalAngularVelocity = FMath::Sign(AngularNormal) * FMath::Max((T)0, FMath::Abs(AngularNormal) - MAngularFriction * NormalVelocityChange) * Constraint.Normal + FMath::Max((T)0, AngularTangent.Size() - MAngularFriction * NormalVelocityChange) * AngularTangent.GetSafeNormal();
					TVector<T, d> Delta = FinalAngularVelocity - RelativeAngularVelocity;
					if (!PBDRigid0 && PBDRigid1)
					{
						PMatrix<T, d, d> WorldSpaceI2 = (Q1 * FMatrix::Identity) * PBDRigid1->I() * (Q1 * FMatrix::Identity).GetTransposed();
						TVector<T, d> ImpulseDelta = PBDRigid1->M() * TVector<T, d>::CrossProduct(VectorToPoint2, Delta);
						Impulse += ImpulseDelta;
						AngularImpulse += WorldSpaceI2 * Delta - TVector<T, d>::CrossProduct(VectorToPoint2, ImpulseDelta);
					}
					else if (PBDRigid0 && !PBDRigid1)
					{
						PMatrix<T, d, d> WorldSpaceI1 = (Q0 * FMatrix::Identity) * PBDRigid0->I() * (Q0 * FMatrix::Identity).GetTransposed();
						TVector<T, d> ImpulseDelta = PBDRigid0->M() * TVector<T, d>::CrossProduct(VectorToPoint1, Delta);
						Impulse += ImpulseDelta;
						AngularImpulse += WorldSpaceI1 * Delta - TVector<T, d>::CrossProduct(VectorToPoint1, ImpulseDelta);
					}
					else if (PBDRigid0 && PBDRigid1)
					{
						PMatrix<T, d, d> Cross1(0, VectorToPoint1.Z, -VectorToPoint1.Y, -VectorToPoint1.Z, 0, VectorToPoint1.X, VectorToPoint1.Y, -VectorToPoint1.X, 0);
						PMatrix<T, d, d> Cross2(0, VectorToPoint2.Z, -VectorToPoint2.Y, -VectorToPoint2.Z, 0, VectorToPoint2.X, VectorToPoint2.Y, -VectorToPoint2.X, 0);
						PMatrix<T, d, d> CrossI1 = Cross1 * WorldSpaceInvI1;
						PMatrix<T, d, d> CrossI2 = Cross2 * WorldSpaceInvI2;
						PMatrix<T, d, d> Diag1 = CrossI1 * Cross1.GetTransposed() + CrossI2 * Cross2.GetTransposed();
						Diag1.M[0][0] += PBDRigid0->InvM() + PBDRigid1->InvM();
						Diag1.M[1][1] += PBDRigid0->InvM() + PBDRigid1->InvM();
						Diag1.M[2][2] += PBDRigid0->InvM() + PBDRigid1->InvM();
						PMatrix<T, d, d> OffDiag1 = (CrossI1 + CrossI2) * -1;
						PMatrix<T, d, d> Diag2 = (WorldSpaceInvI1 + WorldSpaceInvI2).Inverse();
						PMatrix<T, d, d> OffDiag1Diag2 = OffDiag1 * Diag2;
						TVector<T, d> ImpulseDelta = PMatrix<T, d, d>((Diag1 - OffDiag1Diag2 * OffDiag1.GetTransposed()).Inverse())* ((OffDiag1Diag2 * -1) * Delta);
						Impulse += ImpulseDelta;
						AngularImpulse += Diag2 * (Delta - PMatrix<T, d, d>(OffDiag1.GetTransposed()) * ImpulseDelta);
					}
				}
			}
			else
			{
				//outside friction cone, solve for normal relative velocity and keep tangent at cone edge
				TVector<T, d> Tangent = (RelativeVelocity - TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal) * Constraint.Normal).GetSafeNormal();
				TVector<T, d> DirectionalFactor = Factor * (Constraint.Normal - Friction * Tangent);
				T ImpulseDenominator = TVector<T, d>::DotProduct(Constraint.Normal, DirectionalFactor);
				if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nDirectionalFactor:%s, ImpulseDenominator:%f"),
					*Constraint.ToString(),
					*Particle0->ToString(),
					*Particle1->ToString(),
					*DirectionalFactor.ToString(), ImpulseDenominator))
				{
					ImpulseDenominator = (T)1;
				}

				const T ImpulseMag = -(1 + Restitution) * RelativeNormalVelocity / ImpulseDenominator;
				Impulse = ImpulseMag * (Constraint.Normal - Friction * Tangent);
			}
		}
		else
		{
			T ImpulseDenominator = TVector<T, d>::DotProduct(Constraint.Normal, Factor * Constraint.Normal);
			TVector<T, d> ImpulseNumerator = -(1 + Restitution) * TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal)* Constraint.Normal;
			if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Constraint.Normal:%s, ImpulseDenominator:%f"),
				*Constraint.ToString(),
				*Particle0->ToString(),
				*Particle1->ToString(),
				*(Factor * Constraint.Normal).ToString(), ImpulseDenominator))
			{
				ImpulseDenominator = (T)1;
			}
			Impulse = ImpulseNumerator / ImpulseDenominator;
		}
		Impulse = GetEnergyClampedImpulse(Constraint, Impulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
		Constraint.AccumulatedImpulse += Impulse;
		TVector<T, d> AngularImpulse1 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse) + AngularImpulse;
		TVector<T, d> AngularImpulse2 = TVector<T, d>::CrossProduct(VectorToPoint2, -Impulse) - AngularImpulse;
		if (PBDRigid0)
		{
			// Velocity update for next step
			PBDRigid0->V() += PBDRigid0->InvM() * Impulse;
			PBDRigid0->W() += WorldSpaceInvI1 * AngularImpulse1;
			// Position update as part of pbd
			PBDRigid0->P() += (PBDRigid0->InvM() * Impulse) * Dt;
			PBDRigid0->Q() += TRotation<T, d>::FromElements(WorldSpaceInvI1 * AngularImpulse1, 0.f) * Q0 * Dt * T(0.5);
			PBDRigid0->Q().Normalize();
		}
		if (PBDRigid1)
		{
			// Velocity update for next step
			PBDRigid1->V() -= PBDRigid1->InvM() * Impulse;
			PBDRigid1->W() += WorldSpaceInvI2 * AngularImpulse2;
			// Position update as part of pbd
			PBDRigid1->P() -= (PBDRigid1->InvM() * Impulse) * Dt;
			PBDRigid1->Q() += TRotation<T, d>::FromElements(WorldSpaceInvI2 * AngularImpulse2, 0.f) * Q0 * Dt * T(0.5);
			PBDRigid1->Q().Normalize();
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("Apply"), STAT_Apply2, STATGROUP_ChaosWide);
template<typename T, int d>
void TPBDCollisionConstraint<T, d>::Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
{
	if (bEnableVelocitySolve)
	{
		PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
			FConstraintHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
			check(ConstraintHandle != nullptr);
			Apply(Dt, Constraints[ConstraintHandle->GetConstraintIndex()]);
			});
	}

	if (PostApplyCallback != nullptr)
	{
		PostApplyCallback(Dt, InConstraintHandles);
	}
}

DECLARE_CYCLE_STAT(TEXT("ApplyPushOut"), STAT_ApplyPushOut2, STATGROUP_ChaosWide);
template<typename T, int d>
void TPBDCollisionConstraint<T, d>::ApplyPushOut(const T Dt, FRigidBodyContactConstraint& Constraint, const TSet<TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations, bool &NeedsAnotherIteration)
{
	SCOPE_CYCLE_COUNTER(STAT_ApplyPushOut2);
	TGeometryParticleHandle<T, d>* Particle0 = Constraint.Particle;
	TGeometryParticleHandle<T, d>* Particle1 = Constraint.Levelset;
	TPBDRigidParticleHandle<T, d>* PBDRigid0 = Particle0->AsDynamic();
	TPBDRigidParticleHandle<T, d>* PBDRigid1 = Particle1->AsDynamic();

	// @todo(ccaulfield): I think we should never get this? Revisit after particle handle refactor
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

	const TVector<T, d> ZeroVector = TVector<T, d>(0);
	const TRotation<T, d>& Q0 = PBDRigid0 ? PBDRigid0->Q() : Particle0->R();
	const TRotation<T, d>& Q1 = PBDRigid1 ? PBDRigid1->Q() : Particle1->R();
	const TVector<T, d>& P0 = PBDRigid0 ? PBDRigid0->P() : Particle0->X();
	const TVector<T, d>& P1 = PBDRigid1 ? PBDRigid1->P() : Particle1->X();
	const TVector<T, d>& V0 = PBDRigid0 ? PBDRigid0->V() : ZeroVector;
	const TVector<T, d>& V1 = PBDRigid1 ? PBDRigid1->V() : ZeroVector;
	const TVector<T, d>& W0 = PBDRigid0 ? PBDRigid0->W() : ZeroVector;
	const TVector<T, d>& W1 = PBDRigid1 ? PBDRigid1->W() : ZeroVector;
	TSerializablePtr<TChaosPhysicsMaterial<T>> PhysicsMaterial0 = Particle0->AuxilaryValue(MPhysicsMaterials);
	TSerializablePtr<TChaosPhysicsMaterial<T>> PhysicsMaterial1 = Particle1->AuxilaryValue(MPhysicsMaterials);
	const bool IsTemporarilyStatic0 = IsTemporarilyStatic.Contains(Particle0);
	const bool IsTemporarilyStatic1 = IsTemporarilyStatic.Contains(Particle1);

	for (int32 PairIteration = 0; PairIteration < MPairIterations; ++PairIteration)
	{
		UpdateConstraint<ECollisionUpdateType::Deepest>(MThickness, Constraint);
		if (Constraint.Phi >= MThickness)
		{
			break;
		}

		if ((!PBDRigid0 || IsTemporarilyStatic0) && (!PBDRigid1 || IsTemporarilyStatic1))
		{
			break;
		}

		NeedsAnotherIteration = true;
		PMatrix<T, d, d> WorldSpaceInvI1 = PBDRigid0? (Q0 * FMatrix::Identity).GetTransposed() * PBDRigid0->InvI() * (Q0 * FMatrix::Identity) : PMatrix<T, d, d>(0);
		PMatrix<T, d, d> WorldSpaceInvI2 = PBDRigid1 ? (Q1 * FMatrix::Identity).GetTransposed() * PBDRigid1->InvI() * (Q1 * FMatrix::Identity) : PMatrix<T, d, d>(0);
		TVector<T, d> VectorToPoint1 = Constraint.Location - P0;
		TVector<T, d> VectorToPoint2 = Constraint.Location - P1;
		PMatrix<T, d, d> Factor =
			(PBDRigid0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : PMatrix<T, d, d>(0)) +
			(PBDRigid1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : PMatrix<T, d, d>(0));
		T Numerator = FMath::Min((T)(Iteration + 2), (T)NumIterations);
		T ScalingFactor = Numerator / (T)NumIterations;

		//if pushout is needed we better fix relative velocity along normal. Treat it as if 0 restitution
		TVector<T, d> Body1Velocity = V0 + TVector<T, d>::CrossProduct(W0, VectorToPoint1);
		TVector<T, d> Body2Velocity = V1 + TVector<T, d>::CrossProduct(W1, VectorToPoint2);
		TVector<T, d> RelativeVelocity = Body1Velocity - Body2Velocity;
		const T RelativeVelocityDotNormal = TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal);
		if (RelativeVelocityDotNormal < 0)
		{
			T ImpulseDenominator = TVector<T, d>::DotProduct(Constraint.Normal, Factor * Constraint.Normal);
			TVector<T, d> ImpulseNumerator = -TVector<T, d>::DotProduct(RelativeVelocity, Constraint.Normal) * Constraint.Normal * ScalingFactor;
			if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("ApplyPushout Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Constraint.Normal:%s, ImpulseDenominator:%f"),
				*Constraint.ToString(),
				*Particle0->ToString(),
				*Particle1->ToString(),
				*(Factor*Constraint.Normal).ToString(), ImpulseDenominator))
			{
				ImpulseDenominator = (T)1;
			}

			TVector<T, d> VelocityFixImpulse = ImpulseNumerator / ImpulseDenominator;
			VelocityFixImpulse = GetEnergyClampedImpulse(Constraint, VelocityFixImpulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
			Constraint.AccumulatedImpulse += VelocityFixImpulse;	//question: should we track this?
			if (!IsTemporarilyStatic0 && PBDRigid0)
			{
				TVector<T, d> AngularImpulse = TVector<T, d>::CrossProduct(VectorToPoint1, VelocityFixImpulse);
				PBDRigid0->V() += PBDRigid0->InvM() * VelocityFixImpulse;
				PBDRigid0->W() += WorldSpaceInvI1 * AngularImpulse;

			}

			if (!IsTemporarilyStatic1 && PBDRigid1)
			{
				TVector<T, d> AngularImpulse = TVector<T, d>::CrossProduct(VectorToPoint2, -VelocityFixImpulse);
				PBDRigid1->V() -= PBDRigid1->InvM() * VelocityFixImpulse;
				PBDRigid1->W() += WorldSpaceInvI2 * AngularImpulse;
			}

		}


		TVector<T, d> Impulse = PMatrix<T, d, d>(Factor.Inverse()) * ((-Constraint.Phi + MThickness) * ScalingFactor * Constraint.Normal);
		TVector<T, d> AngularImpulse1 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse);
		TVector<T, d> AngularImpulse2 = TVector<T, d>::CrossProduct(VectorToPoint2, -Impulse);
		if (!IsTemporarilyStatic0 && PBDRigid0)
		{
			PBDRigid0->P() += PBDRigid0->InvM() * Impulse;
			PBDRigid0->Q() = TRotation<T, d>::FromVector(WorldSpaceInvI1 * AngularImpulse1) * Q0;
			PBDRigid0->Q().Normalize();
		}
		if (!IsTemporarilyStatic1 && PBDRigid1)
		{
			PBDRigid1->P() -= PBDRigid1->InvM() * Impulse;
			PBDRigid1->Q() = TRotation<T, d>::FromVector(WorldSpaceInvI2 * AngularImpulse2) * Q1;
			PBDRigid1->Q().Normalize();
		}
	}
}

//DECLARE_CYCLE_STAT(TEXT("ApplyPushOut"), STAT_ApplyPushOut, STATGROUP_ChaosWide);
template<typename T, int d>
bool TPBDCollisionConstraint<T, d>::ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const TSet<TGeometryParticleHandle<T,d>*>& IsTemporarilyStatic, int32 Iteration, int32 NumIterations)
{
	SCOPE_CYCLE_COUNTER(STAT_ApplyPushOut2);

	bool NeedsAnotherIteration = false;

	if (MPairIterations > 0)
	{
		PhysicsParallelFor(InConstraintHandles.Num(), [&](int32 ConstraintHandleIndex) {
			FConstraintHandle* ConstraintHandle = InConstraintHandles[ConstraintHandleIndex];
			check(ConstraintHandle != nullptr);
			ApplyPushOut(Dt, Constraints[ConstraintHandle->GetConstraintIndex()], IsTemporarilyStatic, Iteration, NumIterations, NeedsAnotherIteration);
			});
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
	//#if CHAOS_PARTICLEHANDLE_TODO
	
	TGenericParticleHandle<T, d> Generic = const_cast<TGeometryParticleHandle<T, d>*>(Particle);	//TODO: give a const version of the generic API
	return TRigidTransform<T, d>(Generic->P(), Generic->Q());
}

#if 0
template<typename T, int d>
void UpdateLevelsetConstraintHelperCCD(const TRigidParticles<T, d>& InParticles, const int32 j, const TRigidTransform<T, d>& LocalToWorld1, const TRigidTransform<T, d>& LocalToWorld2, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	if (InParticles.CollisionParticles(Constraint.ParticleIndex))
	{
		const TRigidTransform<T, d> PreviousLocalToWorld1 = GetTransform(InParticles, Constraint.ParticleIndex);
		TVector<T, d> WorldSpacePointStart = PreviousLocalToWorld1.TransformPosition(InParticles.CollisionParticles(Constraint.ParticleIndex)->X(j));
		TVector<T, d> WorldSpacePointEnd = LocalToWorld1.TransformPosition(InParticles.CollisionParticles(Constraint.ParticleIndex)->X(j));
		TVector<T, d> Body2SpacePointStart = LocalToWorld2.InverseTransformPosition(WorldSpacePointStart);
		TVector<T, d> Body2SpacePointEnd = LocalToWorld2.InverseTransformPosition(WorldSpacePointEnd);
		Pair<TVector<T, d>, bool> PointPair = InParticles.Geometry(Constraint.LevelsetIndex)->FindClosestIntersection(Body2SpacePointStart, Body2SpacePointEnd, Thickness);
		if (PointPair.Second)
		{
			const TVector<T, d> WorldSpaceDelta = WorldSpacePointEnd - TVector<T, d>(LocalToWorld2.TransformPosition(PointPair.First));
			Constraint.Phi = -WorldSpaceDelta.Size();
			Constraint.Normal = LocalToWorld2.TransformVector(InParticles.Geometry(Constraint.LevelsetIndex)->Normal(PointPair.First));
			// @todo(mlentine): Should we be using the actual collision point or that point evolved to the current time step?
			Constraint.Location = WorldSpacePointEnd;
		}
	}
}
#endif

template <typename T, int d>
bool SampleObjectHelper2(const TImplicitObject<T, d>& Object, const TRigidTransform<T,d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
	TVector<T, d> LocalNormal;
	T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
	if (LocalPhi < Constraint.Phi)
	{
		Constraint.Phi = LocalPhi;
		Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
		Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
		return true;
	}
	return false;
}

template <typename T, int d>
bool SampleObjectNoNormal2(const TImplicitObject<T, d>& Object, const TRigidTransform<T,d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
	TVector<T, d> LocalNormal;
	T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
	if (LocalPhi < Constraint.Phi)
	{
		Constraint.Phi = LocalPhi;
		return true;
	}
	return false;
}

template <typename T, int d>
bool SampleObjectNormalAverageHelper2(const TImplicitObject<T, d>& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, T& TotalThickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
	TVector<T, d> LocalNormal;
	T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
	T LocalThickness = LocalPhi - Thickness;
	if (LocalThickness < -KINDA_SMALL_NUMBER)
	{
		Constraint.Location += LocalPoint * LocalThickness;
		TotalThickness += LocalThickness;
		return true;
	}
	return false;
}

DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetPartial"), STAT_UpdateLevelsetPartial2, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetFindParticles"), STAT_UpdateLevelsetFindParticles2, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetBVHTraversal"), STAT_UpdateLevelsetBVHTraversal2, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetSignedDistance"), STAT_UpdateLevelsetSignedDistance2, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetAll"), STAT_UpdateLevelsetAll2, STATGROUP_ChaosWide);
DECLARE_CYCLE_STAT(TEXT("SampleObject"), STAT_SampleObject2, STATGROUP_ChaosWide);

int32 NormalAveraging2 = 1;
FAutoConsoleVariableRef CVarNormalAveraging2(TEXT("p.NormalAveraging2"), NormalAveraging2, TEXT(""));

int32 SampleMinParticlesForAcceleration2 = 2048;
FAutoConsoleVariableRef CVarSampleMinParticlesForAcceleration2(TEXT("p.SampleMinParticlesForAcceleration2"), SampleMinParticlesForAcceleration2, TEXT("The minimum number of particles needed before using an acceleration structure when sampling"));

template <ECollisionUpdateType UpdateType, typename T, int d>
void SampleObject2(const TImplicitObject<T, d>& Object, const TRigidTransform<T, d>& ObjectTransform, const TBVHParticles<T, d>& SampleParticles, const TRigidTransform<T, d>& SampleParticlesTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_SampleObject2);
	TRigidBodyContactConstraint<T, d> AvgConstraint;
	AvgConstraint.Particle = Constraint.Particle;
	AvgConstraint.Levelset = Constraint.Levelset;
	AvgConstraint.Location = TVector<T, d>::ZeroVector;
	AvgConstraint.Normal = TVector<T, d>::ZeroVector;
	AvgConstraint.Phi = Thickness;
	T TotalThickness = T(0);

	int32 DeepestParticle = -1;
	const int32 NumParticles = SampleParticles.Size();

	const TRigidTransform<T, d> & SampleToObjectTM = SampleParticlesTransform.GetRelativeTransform(ObjectTransform);
	if (NumParticles > SampleMinParticlesForAcceleration2 && Object.HasBoundingBox())
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetPartial2);
		TBox<T, d> ImplicitBox = Object.BoundingBox().TransformedBox(ObjectTransform.GetRelativeTransform(SampleParticlesTransform));
		ImplicitBox.Thicken(Thickness);
		TArray<int32> PotentialParticles;
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetFindParticles2);
			PotentialParticles = SampleParticles.FindAllIntersections(ImplicitBox);
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetSignedDistance2);
			for (int32 i : PotentialParticles)
			{
				if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)	//if we just want one don't bother with normal
				{
					SampleObjectNormalAverageHelper2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
				}
				else
				{
					if (SampleObjectNoNormal2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
					{
						DeepestParticle = i;
						if (UpdateType == ECollisionUpdateType::Any)
						{
							Constraint.Phi = AvgConstraint.Phi;
							return;
						}
					}
				}
			}
		}
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetAll2);
		for (int32 i = 0; i < NumParticles; ++i)
		{
			if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)	//if we just want one don't bother with normal
			{
				const bool bInside = SampleObjectNormalAverageHelper2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
			}
			else
			{
				if (SampleObjectNoNormal2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
				{
					DeepestParticle = i;
					if (UpdateType == ECollisionUpdateType::Any)
					{
						Constraint.Phi = AvgConstraint.Phi;
						return;
					}
				}
			}
		}
	}

	if (NormalAveraging2)
	{
		if (TotalThickness < -KINDA_SMALL_NUMBER)
		{
			TVector<T, d> LocalPoint = AvgConstraint.Location / TotalThickness;
			TVector<T, d> LocalNormal;
			const T NewPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
			if (NewPhi < Constraint.Phi)
			{
				Constraint.Phi = NewPhi;
				Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
				Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
			}
		}
		else
		{
			check(AvgConstraint.Phi >= Thickness);
		}
	}
	else if(AvgConstraint.Phi < Constraint.Phi)
	{
		check(DeepestParticle >= 0);
		TVector<T,d> LocalPoint = SampleToObjectTM.TransformPositionNoScale(SampleParticles.X(DeepestParticle));
		TVector<T, d> LocalNormal;
		Constraint.Phi = Object.PhiWithNormal(LocalPoint, LocalNormal);
		Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
		Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
			
	}
}

#if INTEL_ISPC
template<ECollisionUpdateType UpdateType>
void SampleObject2(const TImplicitObject<float, 3>& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_SampleObject2);
	TRigidBodyContactConstraint<float, 3> AvgConstraint;
	AvgConstraint.Particle = Constraint.Particle;
	AvgConstraint.Levelset = Constraint.Levelset;
	AvgConstraint.Location = TVector<float, 3>::ZeroVector;
	AvgConstraint.Normal = TVector<float, 3>::ZeroVector;
	AvgConstraint.Phi = Thickness;
	float TotalThickness = float(0);

	int32 DeepestParticle = -1;

	const TRigidTransform<float, 3>& SampleToObjectTM = SampleParticlesTransform.GetRelativeTransform(ObjectTransform);
	int32 NumParticles = SampleParticles.Size();

	if (NumParticles > SampleMinParticlesForAcceleration2 && Object.HasBoundingBox())
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetPartial2);
		TBox<float, 3> ImplicitBox = Object.BoundingBox().TransformedBox(ObjectTransform.GetRelativeTransform(SampleParticlesTransform));
		ImplicitBox.Thicken(Thickness);
		TArray<int32> PotentialParticles;
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetFindParticles2);
			PotentialParticles = SampleParticles.FindAllIntersections(ImplicitBox);
		}
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetSignedDistance2);

			if (Object.GetType(true) == ImplicitObjectType::LevelSet && PotentialParticles.Num() > 0)
			{
				//QUICK_SCOPE_CYCLE_COUNTER(STAT_LevelSet);
				const TLevelSet<float, 3>* LevelSet = Object.GetObject<Chaos::TLevelSet<float, 3>>();
				const TUniformGrid<float, 3>& Grid = LevelSet->GetGrid();

				if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
				{
					ispc::SampleLevelSetNormalAverage(
						(ispc::FVector&)Grid.MinCorner(),
						(ispc::FVector&)Grid.MaxCorner(),
						(ispc::FVector&)Grid.Dx(),
						(ispc::FIntVector&)Grid.Counts(),
						(ispc::TArrayND*)&LevelSet->GetPhiArray(),
						(ispc::FTransform&)SampleToObjectTM,
						(ispc::FVector*)&SampleParticles.XArray()[0],
						&PotentialParticles[0],
						Thickness,
						TotalThickness,
						(ispc::FVector&)AvgConstraint.Location,
						PotentialParticles.Num());
				}
				else
				{
					ispc::SampleLevelSetNoNormal(
						(ispc::FVector&)Grid.MinCorner(),
						(ispc::FVector&)Grid.MaxCorner(),
						(ispc::FVector&)Grid.Dx(),
						(ispc::FIntVector&)Grid.Counts(),
						(ispc::TArrayND*)&LevelSet->GetPhiArray(),
						(ispc::FTransform&)SampleToObjectTM,
						(ispc::FVector*)&SampleParticles.XArray()[0],
						&PotentialParticles[0],
						DeepestParticle,
						AvgConstraint.Phi,
						PotentialParticles.Num());

					if (UpdateType == ECollisionUpdateType::Any)
					{
						Constraint.Phi = AvgConstraint.Phi;
						return;
					}
				}
			}
			else if (Object.GetType(true) == ImplicitObjectType::Box && PotentialParticles.Num() > 0)
			{
				//QUICK_SCOPE_CYCLE_COUNTER(STAT_Box);
				const TBox<float, 3>* Box = Object.GetObject<Chaos::TBox<float, 3>>();

				if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
				{
					ispc::SampleBoxNormalAverage(
						(ispc::FVector&)Box->Min(),
						(ispc::FVector&)Box->Max(),
						(ispc::FTransform&)SampleToObjectTM,
						(ispc::FVector*)&SampleParticles.XArray()[0],
						&PotentialParticles[0],
						Thickness,
						TotalThickness,
						(ispc::FVector&)AvgConstraint.Location,
						PotentialParticles.Num());
				}
				else
				{
					ispc::SampleBoxNoNormal(
						(ispc::FVector&)Box->Min(),
						(ispc::FVector&)Box->Max(),
						(ispc::FTransform&)SampleToObjectTM,
						(ispc::FVector*)&SampleParticles.XArray()[0],
						&PotentialParticles[0],
						DeepestParticle,
						AvgConstraint.Phi,
						PotentialParticles.Num());

					if (UpdateType == ECollisionUpdateType::Any)
					{
						Constraint.Phi = AvgConstraint.Phi;
						return;
					}
				}
			}
			else
			{
				//QUICK_SCOPE_CYCLE_COUNTER(STAT_Other);
				for (int32 i : PotentialParticles)
				{
					if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
					{
						SampleObjectNormalAverageHelper2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
					}
					else
					{
						if (SampleObjectNoNormal2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
						{
							DeepestParticle = i;
							if (UpdateType == ECollisionUpdateType::Any)
							{
								Constraint.Phi = AvgConstraint.Phi;
								return;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetAll2);
		if (Object.GetType(true) == ImplicitObjectType::LevelSet && NumParticles > 0)
		{
			const TLevelSet<float, 3>* LevelSet = Object.GetObject<Chaos::TLevelSet<float, 3>>();
			const TUniformGrid<float, 3>& Grid = LevelSet->GetGrid();

			if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
			{
				ispc::SampleLevelSetNormalAverageAll(
					(ispc::FVector&)Grid.MinCorner(),
					(ispc::FVector&)Grid.MaxCorner(),
					(ispc::FVector&)Grid.Dx(),
					(ispc::FIntVector&)Grid.Counts(),
					(ispc::TArrayND*)&LevelSet->GetPhiArray(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XArray()[0],
					Thickness,
					TotalThickness,
					(ispc::FVector&)AvgConstraint.Location,
					NumParticles);
			}
			else
			{
				ispc::SampleLevelSetNoNormalAll(
					(ispc::FVector&)Grid.MinCorner(),
					(ispc::FVector&)Grid.MaxCorner(),
					(ispc::FVector&)Grid.Dx(),
					(ispc::FIntVector&)Grid.Counts(),
					(ispc::TArrayND*)&LevelSet->GetPhiArray(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XArray()[0],
					DeepestParticle,
					AvgConstraint.Phi,
					NumParticles);

				if (UpdateType == ECollisionUpdateType::Any)
				{
					Constraint.Phi = AvgConstraint.Phi;
					return;
				}
			}
		}
		else if (Object.GetType(true) == ImplicitObjectType::Plane && NumParticles > 0)
		{
			const TPlane<float, 3>* Plane = Object.GetObject<Chaos::TPlane<float, 3>>();

			if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
			{
				ispc::SamplePlaneNormalAverageAll(
					(ispc::FVector&)Plane->Normal(),
					(ispc::FVector&)Plane->X(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XArray()[0],
					Thickness,
					TotalThickness,
					(ispc::FVector&)AvgConstraint.Location,
					NumParticles);
			}
			else
			{
				ispc::SamplePlaneNoNormalAll(
					(ispc::FVector&)Plane->Normal(),
					(ispc::FVector&)Plane->X(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XArray()[0],
					DeepestParticle,
					AvgConstraint.Phi,
					NumParticles);

				if (UpdateType == ECollisionUpdateType::Any)
				{
					Constraint.Phi = AvgConstraint.Phi;
					return;
				}
			}
		}
		else if (Object.GetType(true) == ImplicitObjectType::Box && NumParticles > 0)
		{
			const TBox<float, 3>* Box = Object.GetObject<Chaos::TBox<float, 3>>();

			if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
			{
				ispc::SampleBoxNormalAverageAll(
					(ispc::FVector&)Box->Min(),
					(ispc::FVector&)Box->Max(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XArray()[0],
					Thickness,
					TotalThickness,
					(ispc::FVector&)AvgConstraint.Location,
					NumParticles);
			}
			else
			{
				ispc::SampleBoxNoNormalAll(
					(ispc::FVector&)Box->Min(),
					(ispc::FVector&)Box->Max(),
					(ispc::FTransform&)SampleToObjectTM,
					(ispc::FVector*)&SampleParticles.XArray()[0],
					DeepestParticle,
					AvgConstraint.Phi,
					NumParticles);

				if (UpdateType == ECollisionUpdateType::Any)
				{
					Constraint.Phi = AvgConstraint.Phi;
					return;
				}
			}
		}
		else
		{
			//QUICK_SCOPE_CYCLE_COUNTER(STAT_Other);
			for (int32 i = 0; i < NumParticles; ++i)
			{
				if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
				{
					SampleObjectNormalAverageHelper2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
				}
				else
				{
					if (SampleObjectNoNormal2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
					{
						DeepestParticle = i;
						if (UpdateType == ECollisionUpdateType::Any)
						{
							Constraint.Phi = AvgConstraint.Phi;
							return;
						}
					}
				}
			}
		}
	}

	if (NormalAveraging2)
	{
		if (TotalThickness < -KINDA_SMALL_NUMBER)
		{
			TVector<float, 3> LocalPoint = AvgConstraint.Location / TotalThickness;
			TVector<float, 3> LocalNormal;
			const float NewPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
			if (NewPhi < Constraint.Phi)
			{
				Constraint.Phi = NewPhi;
				Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
				Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
			}
		}
		else
		{
			check(AvgConstraint.Phi >= Thickness);
		}
	}
	else if (AvgConstraint.Phi < Constraint.Phi)
	{
		check(DeepestParticle >= 0);
		TVector<float, 3> LocalPoint = SampleToObjectTM.TransformPositionNoScale(SampleParticles.X(DeepestParticle));
		TVector<float, 3> LocalNormal;
		Constraint.Phi = Object.PhiWithNormal(LocalPoint, LocalNormal);
		Constraint.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
		Constraint.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
	}
}
#endif

template <typename T, int d>
bool UpdateBoxPlaneConstraint(const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	
#if USING_CODE_ANALYSIS
	MSVC_PRAGMA( warning( push ) )
	MSVC_PRAGMA( warning( disable : ALL_CODE_ANALYSIS_WARNINGS ) )
#endif	// USING_CODE_ANALYSIS

	bool bApplied = false;
	const TRigidTransform<T, d> BoxToPlaneTransform(BoxTransform.GetRelativeTransform(PlaneTransform));
	const TVector<T, d> Extents = Box.Extents();
	constexpr int32 NumCorners = 2 + 2 * d;
	constexpr T Epsilon = KINDA_SMALL_NUMBER;

	TVector<T, d> Corners[NumCorners];
	int32 CornerIdx = 0;
	Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max());
	Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min());
	for (int32 j = 0; j < d; ++j)
	{
		Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min() + TVector<T, d>::AxisVector(j) * Extents);
		Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max() - TVector<T, d>::AxisVector(j) * Extents);
	}

#if USING_CODE_ANALYSIS
	MSVC_PRAGMA( warning( pop ) )
#endif	// USING_CODE_ANALYSIS

	TVector<T, d> PotentialConstraints[NumCorners];
	int32 NumConstraints = 0;
	for (int32 i = 0; i < NumCorners; ++i)
	{
		TVector<T, d> Normal;
		const T NewPhi = Plane.PhiWithNormal(Corners[i], Normal);
		if (NewPhi < Constraint.Phi + Epsilon)
		{
			if (NewPhi <= Constraint.Phi - Epsilon)
			{
				NumConstraints = 0;
			}
			Constraint.Phi = NewPhi;
			Constraint.Normal = PlaneTransform.TransformVector(Normal);
			Constraint.Location = PlaneTransform.TransformPosition(Corners[i]);
			PotentialConstraints[NumConstraints++] = Constraint.Location;
			bApplied = true;
		}
	}
	if (NumConstraints > 1)
	{
		TVector<T, d> AverageLocation(0);
		for (int32 ConstraintIdx = 0; ConstraintIdx < NumConstraints; ++ConstraintIdx)
		{
			AverageLocation += PotentialConstraints[ConstraintIdx];
		}
		Constraint.Location = AverageLocation / NumConstraints;
	}

	return bApplied;
}

template <typename T, int d>
void UpdateSphereConstraint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	const TVector<T, d> Center1 = Sphere1Transform.TransformPosition(Sphere1.GetCenter());
	const TVector<T, d> Center2 = Sphere2Transform.TransformPosition(Sphere2.GetCenter());
	const TVector<T, d> Direction = Center1 - Center2;
	const T Size = Direction.Size();
	const T NewPhi = Size - (Sphere1.GetRadius() + Sphere2.GetRadius());
	if (NewPhi < Constraint.Phi)
	{
		Constraint.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
		Constraint.Phi = NewPhi;
		Constraint.Location = Center1 - Sphere1.GetRadius() * Constraint.Normal;
	}
}

template <typename T, int d>
void UpdateSpherePlaneConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	const TRigidTransform<T, d> SphereToPlaneTransform(PlaneTransform.Inverse() * SphereTransform);
	const TVector<T, d> SphereCenter = SphereToPlaneTransform.TransformPosition(Sphere.GetCenter());

	TVector<T, d> NewNormal;
	T NewPhi = Plane.PhiWithNormal(SphereCenter, NewNormal);
	NewPhi -= Sphere.GetRadius();

	if (NewPhi < Constraint.Phi)
	{
		Constraint.Phi = NewPhi;
		Constraint.Normal = PlaneTransform.TransformVectorNoScale(NewNormal);
		Constraint.Location = SphereCenter - Constraint.Normal * Sphere.GetRadius();
	}
}

template <typename T, int d>
void UpdateSphereBoxConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	const TRigidTransform<T, d> SphereToBoxTransform(SphereTransform * BoxTransform.Inverse());
	const TVector<T, d> SphereCenterInBox = SphereToBoxTransform.TransformPosition(Sphere.GetCenter());

	TVector<T, d> NewNormal;
	T NewPhi = Box.PhiWithNormal(SphereCenterInBox, NewNormal);
	NewPhi -= Sphere.GetRadius();

	if (NewPhi < Constraint.Phi)
	{
		Constraint.Phi = NewPhi;
		Constraint.Normal = BoxTransform.TransformVectorNoScale(NewNormal);
		Constraint.Location = SphereTransform.TransformPosition(Sphere.GetCenter()) - Constraint.Normal * Sphere.GetRadius();
	}
}

template <typename T, int d>
void UpdateCapsuleCapsuleConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	FVector A1 = ATransform.TransformPosition(A.GetX1());
	FVector A2 = ATransform.TransformPosition(A.GetX2());
	FVector B1 = BTransform.TransformPosition(B.GetX1());
	FVector B2 = BTransform.TransformPosition(B.GetX2());
	FVector P1, P2;
	FMath::SegmentDistToSegmentSafe(A1, A2, B1, B2, P1, P2);

	TVector<T, d> Delta = P2 - P1;
	T DeltaLen = Delta.Size();
	if (DeltaLen > KINDA_SMALL_NUMBER)
	{
		TVector<T, d> Dir = Delta / DeltaLen;
		Constraint.Phi = DeltaLen - (A.GetRadius() + B.GetRadius());
		Constraint.Normal = -Dir;
		Constraint.Location = P1 + Dir * A.GetRadius();
	}
}

template <typename T, int d>
void UpdateCapsuleBoxConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TBox<T, d>& B, const TRigidTransform<T, d>& BTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	// @todo(ccaulfield): Add custom capsule-box collision

	TRigidTransform<T, d> BToATransform = BTransform.GetRelativeTransform(ATransform);

	// Use GJK to track closest points (not strictly necessary yet)
	TVector<T, d> NearPointALocal, NearPointBLocal;
	T NearPointDistance;
	if (GJKDistance<T>(A, B, BToATransform, NearPointDistance, NearPointALocal, NearPointBLocal))
	{
		TVector<T, d> NearPointAWorld = ATransform.TransformPosition(NearPointALocal);
		TVector<T, d> NearPointBWorld = BTransform.TransformPosition(NearPointBLocal);
		TVector<T, d> NearPointBtoAWorld = NearPointAWorld - NearPointBWorld;
		Constraint.Phi = NearPointDistance;
		Constraint.Normal = NearPointBtoAWorld.GetSafeNormal();
		Constraint.Location = NearPointAWorld;
	}
	else
	{
		// Use box particle samples against the implicit capsule
		const TArray<TVector<T, d>> BParticles = B.ComputeSamplePoints();
		const int32 NumParticles = BParticles.Num();
		for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
		{
			if (SampleObjectHelper2(A, ATransform, BToATransform, BParticles[ParticleIndex], Thickness, Constraint))
			{
				// SampleObjectHelper2 expects A to be the box, so reverse the results
				Constraint.Normal = -Constraint.Normal;
			}
		}
	}

}

DECLARE_CYCLE_STAT(TEXT("FindRelevantShapes2"), STAT_FindRelevantShapes2, STATGROUP_ChaosWide);
template <typename T, int d>
TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> FindRelevantShapes2(const TImplicitObject<T,d>* ParticleObj, const TRigidTransform<T,d>& ParticlesTM, const TImplicitObject<T,d>& LevelsetObj, const TRigidTransform<T,d>& LevelsetTM, const T Thickness)
{
	SCOPE_CYCLE_COUNTER(STAT_FindRelevantShapes2);
	TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> RelevantShapes;
	//find all levelset inner objects
	if (ParticleObj)
	{
		if (ParticleObj->HasBoundingBox())
		{
			const TRigidTransform<T, d> ParticlesToLevelsetTM = ParticlesTM.GetRelativeTransform(LevelsetTM);
			TBox<T, d> ParticleBoundsInLevelset = ParticleObj->BoundingBox().TransformedBox(ParticlesToLevelsetTM);
			ParticleBoundsInLevelset.Thicken(Thickness);
			{
				LevelsetObj.FindAllIntersectingObjects(RelevantShapes, ParticleBoundsInLevelset);
			}
		}
		else
		{
			LevelsetObj.AccumulateAllImplicitObjects(RelevantShapes, TRigidTransform<T, d>::Identity);
		}
	}
	else
	{
		//todo:compute bounds
		LevelsetObj.AccumulateAllImplicitObjects(RelevantShapes, TRigidTransform<T, d>::Identity);
	}

	return RelevantShapes;
}

DECLARE_CYCLE_STAT(TEXT("UpdateUnionUnionConstraint"), STAT_UpdateUnionUnionConstraint2, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, typename T, int d>
void UpdateUnionUnionConstraint(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateUnionUnionConstraint2);

	TGenericParticleHandle<T, d> Particle0 = Constraint.Particle;
	TGenericParticleHandle<T, d> Particle1 = Constraint.Levelset;

	TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
	TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

	const TImplicitObject<T, d>* ParticleObj = Particle0->Geometry().Get();
	const TImplicitObject<T,d>* LevelsetObj = Particle1->Geometry().Get();
	const TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes2(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

	for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
	{
		const TImplicitObject<T, d>& LevelsetInnerObj = *LevelsetObjPair.First;
		const TRigidTransform<T, d>& LevelsetInnerObjTM = LevelsetObjPair.Second * LevelsetTM;

		//now find all particle inner objects
		const TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes2(&LevelsetInnerObj, LevelsetInnerObjTM, *ParticleObj, ParticlesTM, Thickness);

		//for each inner obj pair, update constraint
		for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& ParticlePair : ParticleShapes)
		{
			const TImplicitObject<T, d>& ParticleInnerObj = *ParticlePair.First;
			const TRigidTransform<T, d> ParticleInnerObjTM = ParticlePair.Second * ParticlesTM;
			UpdateConstraintImp2<UpdateType>(ParticleInnerObj, ParticleInnerObjTM, LevelsetInnerObj, LevelsetInnerObjTM, Thickness, Constraint);
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateSingleUnionConstraint"), STAT_UpdateSingleUnionConstraint2, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, typename T, int d>
void UpdateSingleUnionConstraint(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateSingleUnionConstraint2);

	TGenericParticleHandle<T, d> Particle0 = Constraint.Particle;
	TGenericParticleHandle<T, d> Particle1 = Constraint.Levelset;

	TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
	TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

	const TImplicitObject<T, d>* ParticleObj = Particle0->Geometry().Get();
	const TImplicitObject<T, d>* LevelsetObj = Particle1->Geometry().Get();
	const TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes2(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);
	
	for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
	{
		const TImplicitObject<T, d>& LevelsetInnerObj = *LevelsetObjPair.First;
		const TRigidTransform<T, d> LevelsetInnerObjTM = LevelsetObjPair.Second * LevelsetTM;
		UpdateConstraintImp2<UpdateType>(*ParticleObj, ParticlesTM, LevelsetInnerObj, LevelsetInnerObjTM, Thickness, Constraint);
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetConstraint"), STAT_UpdateLevelsetConstraint2, STATGROUP_ChaosWide);
template<typename T, int d>
template<ECollisionUpdateType UpdateType>
void TPBDCollisionConstraint<T, d>::UpdateLevelsetConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetConstraint2);
	
	TGenericParticleHandle<T, d> Particle0 = Constraint.Particle;
	TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
	if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
	{
		return;
	}

	TGenericParticleHandle<T, d> Particle1 = Constraint.Levelset;
	TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());
	if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
	{
		return;
	}

	const TBVHParticles<T, d>* SampleParticles = nullptr;
	SampleParticles = Particle0->CollisionParticles().Get();

	if(SampleParticles)
	{
		SampleObject2<UpdateType>(*Particle1->Geometry(), LevelsetTM, *SampleParticles, ParticlesTM, Thickness, Constraint);
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateConvexConstraintsUsingCoreShapes"), UpdateConvexConstraintsUsingCoreShapes, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, typename T, int d>
void UpdateConvexConstraintsUsingCoreShapes(const TImplicitObject<T, d> & AObj, const TRigidTransform<T, d>& ATM, const TImplicitObject<T, d> & BObj, const TRigidTransform<T, d>& BTM, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(UpdateConvexConstraintsUsingCoreShapes);

	GJKCoreShapeIntersection<T, 3>(AObj, ATM, BObj, BTM, Constraint.Location, Constraint.Phi, Constraint.Normal, Thickness);
}

DECLARE_CYCLE_STAT(TEXT("UpdateUnionLevelsetConstraint"), STAT_UpdateUnionLevelsetConstraint2, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, typename T, int d>
void UpdateUnionLevelsetConstraint(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateUnionLevelsetConstraint2);

	TGenericParticleHandle<T, d> Particle0 = Constraint.Particle;
	TGenericParticleHandle<T, d> Particle1 = Constraint.Levelset;

	TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
	TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

	if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
	{
		return;
	}

	if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
	{
		return;
	}

	const TImplicitObject<T, d>* ParticleObj = Particle0->Geometry().Get();
	const TImplicitObject<T, d>* LevelsetObj = Particle1->Geometry().Get();
	TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes2(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

	if (LevelsetShapes.Num() && Particle0->CollisionParticles().Get())
	{
		const TBVHParticles<T, d>& SampleParticles = *Particle0->CollisionParticles().Get();
		if (SampleParticles.Size())
		{
			for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
			{
				const TImplicitObject<T, d>* Object = LevelsetObjPair.First;
				const TRigidTransform<T, d> ObjectTM = LevelsetObjPair.Second * LevelsetTM;
				SampleObject2<UpdateType>(*Object, ObjectTM, SampleParticles, ParticlesTM, Thickness, Constraint);
				if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
				{
					return;
				}
			}
		}
#if CHAOS_PARTICLEHANDLE_TODO
		else if (ParticleObj && ParticleObj->IsUnderlyingUnion())
		{
			const TImplicitObjectUnion<T, d>* UnionObj = static_cast<const TImplicitObjectUnion<T, d>*>(ParticleObj);
			//need to traverse shapes to get their collision particles
			for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
			{
				const TImplicitObject<T, d>* LevelsetInnerObject = LevelsetObjPair.First;
				const TRigidTransform<T, d> LevelsetInnerObjectTM = LevelsetObjPair.Second * LevelsetTM;

				TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes2(LevelsetInnerObject, LevelsetInnerObjectTM, *ParticleObj, ParticlesTM, Thickness);
				for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& ParticleObjPair : ParticleShapes)
				{
					const TImplicitObject<T, d>* ParticleInnerObject = ParticleObjPair.First;
					const TRigidTransform<T, d> ParticleInnerObjectTM = ParticleObjPair.Second * ParticlesTM;

					if (const int32* OriginalIdx = UnionObj->MCollisionParticleLookupHack.Find(ParticleInnerObject))
					{
						const TBVHParticles<T, d>& InnerSampleParticles = *InParticles.CollisionParticles(*OriginalIdx).Get();
						SampleObject2<UpdateType>(*LevelsetInnerObject, LevelsetInnerObjectTM, InnerSampleParticles, ParticleInnerObjectTM, Thickness, Constraint);
						if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
						{
							return;
						}
					}
				}

			}
		}
#endif
	}
}


DECLARE_CYCLE_STAT(TEXT("UpdateLevelsetUnionConstraint"), STAT_UpdateLevelsetUnionConstraint2, STATGROUP_ChaosWide);
template<ECollisionUpdateType UpdateType, typename T, int d>
void UpdateLevelsetUnionConstraint(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetUnionConstraint2);

	TGenericParticleHandle<T, d> Particle0 = Constraint.Particle;
	TGenericParticleHandle<T, d> Particle1 = Constraint.Levelset;

	TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
	TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

	const TImplicitObject<T, d>* ParticleObj = Particle0->Geometry().Get();
	const TImplicitObject<T, d>* LevelsetObj = Particle1->Geometry().Get();

	if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
	{
		return;
	}

	if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
	{
		return;
	}

#if CHAOS_PARTICLEHANDLE_TODO
	TArray<Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes2(LevelsetObj, LevelsetTM, *ParticleObj, ParticlesTM, Thickness);
	check(ParticleObj->IsUnderlyingUnion());
	const TImplicitObjectUnion<T, d>* UnionObj = static_cast<const TImplicitObjectUnion<T, d>*>(ParticleObj);
	for (const Pair<const TImplicitObject<T, d>*, TRigidTransform<T, d>>& ParticleObjPair : ParticleShapes)
	{
		const TImplicitObject<T, d>* Object = ParticleObjPair.First;

		if (const int32* OriginalIdx = UnionObj->MCollisionParticleLookupHack.Find(Object))
		{
			const TBVHParticles<T, d>& SampleParticles = *InParticles.CollisionParticles(*OriginalIdx).Get();
			const TRigidTransform<T, d> ObjectTM = ParticleObjPair.Second * ParticlesTM;

			SampleObject2<UpdateType>(*LevelsetObj, LevelsetTM, SampleParticles, ObjectTM, Thickness, Constraint);
			if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
			{
				return;
			}
		}
	}
#endif
}


template <typename T, int d>
void UpdateBoxConstraint(const TBox<T, d>& Box1, const TRigidTransform<T, d>& Box1Transform, const TBox<T, d>& Box2, const TRigidTransform<T, d>& Box2Transform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TBox<T,d> Box2SpaceBox1 = Box1.TransformedBox(Box1Transform * Box2Transform.Inverse());
	TBox<T,d> Box1SpaceBox2 = Box2.TransformedBox(Box2Transform * Box1Transform.Inverse());
	Box2SpaceBox1.Thicken(Thickness);
	Box1SpaceBox2.Thicken(Thickness);
	if (Box1SpaceBox2.Intersects(Box1) && Box2SpaceBox1.Intersects(Box2))
	{
		const TVector<T, d> Box1Center = (Box1Transform * Box2Transform.Inverse()).TransformPosition(Box1.Center());
		bool bDeepOverlap = false;
		if (Box2.SignedDistance(Box1Center) < 0)
		{
			//If Box1 is overlapping Box2 by this much the signed distance approach will fail (box1 gets sucked into box2). In this case just use two spheres
			TSphere<T, d> Sphere1(Box1Transform.TransformPosition(Box1.Center()), Box1.Extents().Min() / 2);
			TSphere<T, d> Sphere2(Box2Transform.TransformPosition(Box2.Center()), Box2.Extents().Min() / 2);
			const TVector<T, d> Direction = Sphere1.GetCenter() - Sphere2.GetCenter();
			T Size = Direction.Size();
			if(Size < (Sphere1.GetRadius() + Sphere2.GetRadius()))
			{
				const T NewPhi = Size - (Sphere1.GetRadius() + Sphere2.GetRadius());;
				if (NewPhi < Constraint.Phi)
				{
					bDeepOverlap = true;
					Constraint.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
					Constraint.Phi = NewPhi;
					Constraint.Location = Sphere1.GetCenter() - Sphere1.GetRadius() * Constraint.Normal;
				}
			}
		}
		if (!bDeepOverlap || Constraint.Phi >= 0)
		{
			//if we didn't have deep penetration use signed distance per particle. If we did have deep penetration but the spheres did not overlap use signed distance per particle

			//UpdateLevelsetConstraintGJK(InParticles, Thickness, Constraint);
			//check(Constraint.Phi < MThickness);
			// For now revert to doing all points vs lsv check until we can figure out a good way to get the deepest point without needing this
			{
				const TArray<TVector<T, d>> SampleParticles = Box1.ComputeSamplePoints();
				const TRigidTransform<T, d> Box1ToBox2Transform = Box1Transform.GetRelativeTransform(Box2Transform);
				int32 NumParticles = SampleParticles.Num();
				for (int32 i = 0; i < NumParticles; ++i)
				{
					SampleObjectHelper2(Box2, Box2Transform, Box1ToBox2Transform, SampleParticles[i], Thickness, Constraint);
				}
			}
		}
	}
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeLevelsetConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	//todo(ocohen):if both have collision particles, use the one with fewer?
	if (!Particle1->Geometry() || (Particle0->AsDynamic() && !Particle0->AsDynamic()->CollisionParticlesSize() && Particle0->Geometry() && !Particle0->Geometry()->IsUnderlyingUnion()))
	{
		TRigidBodyContactConstraint<T, d> Constraint;
		Constraint.Particle = Particle1;
		Constraint.Levelset = Particle0;
		return Constraint;
	}
	else
	{
		TRigidBodyContactConstraint<T, d> Constraint;
		Constraint.Particle = Particle0;
		Constraint.Levelset = Particle1;
		return Constraint;
	}
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeLevelsetConstraintGJK(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeBoxConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeBoxPlaneConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeSphereConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeSpherePlaneConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeSphereBoxConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeCapsuleCapsuleConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template<typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeCapsuleBoxConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template <typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeSingleUnionConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	return Constraint;
}

template <typename T, int d>
TRigidBodyContactConstraint<T, d> ComputeUnionUnionConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	TRigidBodyContactConstraint<T, d> Constraint;
	Constraint.Particle = Particle0;
	Constraint.Levelset = Particle1;
	//todo(ocohen): some heuristic for determining the order?
	return Constraint;
}

template<typename T, int d>
typename TPBDCollisionConstraint<T, d>::FRigidBodyContactConstraint TPBDCollisionConstraint<T, d>::ComputeConstraint(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const T Thickness)
{
	if (!Particle0->Geometry() || !Particle1->Geometry())
	{
		return ComputeLevelsetConstraint(Particle0, Particle1, Thickness);
	}
	if (Particle0->Geometry()->GetType() == TBox<T, d>::GetType() && Particle1->Geometry()->GetType() == TBox<T, d>::GetType())
	{
		return ComputeBoxConstraint(Particle0, Particle1, Thickness);
	}
	else if (Particle0->Geometry()->GetType() == TSphere<T, d>::GetType() && Particle1->Geometry()->GetType() == TSphere<T, d>::GetType())
	{
		return ComputeSphereConstraint(Particle0, Particle1, Thickness);
	}
	else if (Particle0->Geometry()->GetType() == TBox<T, d>::GetType() && Particle1->Geometry()->GetType() == TPlane<T, d>::GetType())
	{
		return ComputeBoxPlaneConstraint(Particle0, Particle1, Thickness);
	}
	else if (Particle1->Geometry()->GetType() == TBox<T, d>::GetType() && Particle0->Geometry()->GetType() == TPlane<T, d>::GetType())
	{
		return ComputeBoxPlaneConstraint(Particle1, Particle0, Thickness);
	}
	else if (Particle0->Geometry()->GetType() == TSphere<T, d>::GetType() && Particle1->Geometry()->GetType() == TPlane<T, d>::GetType())
	{
		return ComputeSpherePlaneConstraint(Particle0, Particle1, Thickness);
	}
	else if (Particle1->Geometry()->GetType() == TSphere<T, d>::GetType() && Particle0->Geometry()->GetType() == TPlane<T, d>::GetType())
	{
		return ComputeSpherePlaneConstraint(Particle1, Particle0, Thickness);
	}
	else if (Particle0->Geometry()->GetType() == TSphere<T, d>::GetType() && Particle1->Geometry()->GetType() == TBox<T, d>::GetType())
	{
		return ComputeSphereBoxConstraint(Particle0, Particle1, Thickness);
	}
	else if (Particle1->Geometry()->GetType() == TSphere<T, d>::GetType() && Particle0->Geometry()->GetType() == TBox<T, d>::GetType())
	{
		return ComputeSphereBoxConstraint(Particle1, Particle0, Thickness);
	}
	else if (Particle0->Geometry()->GetType() == TCapsule<T>::GetType() && Particle1->Geometry()->GetType() == TCapsule<T>::GetType())
	{
		return ComputeCapsuleCapsuleConstraint(Particle0, Particle1, Thickness);
	}
	else if (Particle0->Geometry()->GetType() == TCapsule<T>::GetType() && Particle1->Geometry()->GetType() == TBox<T, d>::GetType())
	{
		return ComputeCapsuleBoxConstraint(Particle0, Particle1, Thickness);
	}
	else if (Particle1->Geometry()->GetType() == TCapsule<T>::GetType() && Particle0->Geometry()->GetType() == TBox<T, d>::GetType())
	{
		return ComputeCapsuleBoxConstraint(Particle1, Particle0, Thickness);
	}
	else if (Particle0->Geometry()->GetType() < TImplicitObjectUnion<T, d>::GetType() && Particle1->Geometry()->GetType() == TImplicitObjectUnion<T, d>::GetType())
	{
		return ComputeSingleUnionConstraint(Particle0, Particle1, Thickness);
	}
	else if (Particle0->Geometry()->GetType() == TImplicitObjectUnion<T, d>::GetType() && Particle1->Geometry()->GetType() < TImplicitObjectUnion<T, d>::GetType())
	{
		return ComputeSingleUnionConstraint(Particle1, Particle0, Thickness);
	}
	else if(Particle0->Geometry()->GetType() == TImplicitObjectUnion<T, d>::GetType() && Particle1->Geometry()->GetType() == TImplicitObjectUnion<T, d>::GetType())
	{
		return ComputeUnionUnionConstraint(Particle0, Particle1, Thickness);
	}
#if 0
	else if (Particle0->Geometry()->IsConvex() && Particle1->Geometry()->IsConvex())
	{
		return ComputeLevelsetConstraintGJK(Particle0, Particle1, Thickness);
	}
#endif
	return ComputeLevelsetConstraint(Particle0, Particle1, Thickness);
}

// NOTE: UpdateLevelsetConstraintImp and its <float,3> specialization are here for the Linux build. It looks like
// GCC does not resolve TPBDCollisionConstraint<T, d> correctly and it won't compile without them.
template<ECollisionUpdateType UpdateType, typename T, int d>
void UpdateLevelsetConstraintImp(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	TPBDCollisionConstraint<T, d>::UpdateLevelsetConstraint<UpdateType>(Thickness, Constraint);
}

template<ECollisionUpdateType UpdateType>
void UpdateLevelsetConstraintImp(const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint)
{
	TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<UpdateType>(Thickness, Constraint);
}

template<ECollisionUpdateType UpdateType, typename T, int d>
void UpdateConstraintImp2(const TImplicitObject<T, d>& ParticleObject, const TRigidTransform<T, d>& ParticleTM, const TImplicitObject<T, d>& LevelsetObject, const TRigidTransform<T, d>& LevelsetTM, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
{
	if (ParticleObject.GetType() == TBox<T, d>::GetType() && LevelsetObject.GetType() == TBox<T, d>::GetType())
	{
		UpdateBoxConstraint(*ParticleObject.template GetObject<TBox<T,d>>(), ParticleTM, *LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TSphere<T, d>::GetType() && LevelsetObject.GetType() == TSphere<T, d>::GetType())
	{
		UpdateSphereConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TBox<T, d>::GetType() && LevelsetObject.GetType() == TPlane<T, d>::GetType())
	{
		UpdateBoxPlaneConstraint(*ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TPlane<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TSphere<T, d>::GetType() && LevelsetObject.GetType() == TPlane<T, d>::GetType())
	{
		UpdateSpherePlaneConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TPlane<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TSphere<T, d>::GetType() && LevelsetObject.GetType() == TBox<T, d>::GetType())
	{
		UpdateSphereBoxConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TCapsule<T>::GetType() && LevelsetObject.GetType() == TCapsule<T>::GetType())
	{
		UpdateCapsuleCapsuleConstraint(*ParticleObject.template GetObject<TCapsule<T>>(), ParticleTM, *LevelsetObject.template GetObject<TCapsule<T>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TCapsule<T>::GetType() && LevelsetObject.GetType() == TBox<T, d>::GetType())
	{
		UpdateCapsuleBoxConstraint(*ParticleObject.template GetObject<TCapsule<T>>(), ParticleTM, *LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TPlane<T, d>::GetType() && LevelsetObject.GetType() == TBox<T, d>::GetType())
	{
		TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
		UpdateBoxPlaneConstraint(*LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TPlane<T, d>>(), ParticleTM, Thickness, TmpConstraint);
		if (TmpConstraint.Phi < Constraint.Phi)
		{
			Constraint = TmpConstraint;
			Constraint.Normal = -Constraint.Normal;
		}
	}
	else if (ParticleObject.GetType() == TPlane<T, d>::GetType() && LevelsetObject.GetType() == TSphere<T, d>::GetType())
	{
		TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
		UpdateSpherePlaneConstraint(*LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TPlane<T, d>>(), ParticleTM, Thickness, TmpConstraint);
		if (TmpConstraint.Phi < Constraint.Phi)
		{
			Constraint = TmpConstraint;
			Constraint.Normal = -Constraint.Normal;
		}
	}
	else if (ParticleObject.GetType() == TBox<T, d>::GetType() && LevelsetObject.GetType() == TSphere<T, d>::GetType())
	{
		TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
		UpdateSphereBoxConstraint(*LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, Thickness, TmpConstraint);
		if (TmpConstraint.Phi < Constraint.Phi)
		{
			Constraint = TmpConstraint;
			Constraint.Normal = -Constraint.Normal;
		}
	}
	else if (ParticleObject.GetType() == TBox<T, d>::GetType() && LevelsetObject.GetType() == TCapsule<T>::GetType())
	{
		TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
		UpdateCapsuleBoxConstraint(*LevelsetObject.template GetObject<TCapsule<T>>(), LevelsetTM, *ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, Thickness, TmpConstraint);
		if (TmpConstraint.Phi < Constraint.Phi)
		{
			Constraint = TmpConstraint;
			Constraint.Normal = -Constraint.Normal;
		}
	}
	else if (ParticleObject.GetType() < TImplicitObjectUnion<T, d>::GetType() && LevelsetObject.GetType() == TImplicitObjectUnion<T, d>::GetType())
	{
		return UpdateSingleUnionConstraint<UpdateType>(Thickness, Constraint);
	}
	else if (ParticleObject.GetType() == TImplicitObjectUnion<T, d>::GetType() && LevelsetObject.GetType() < TImplicitObjectUnion<T, d>::GetType())
	{
		check(false);	//should not be possible to get this ordering (see ComputeConstraint)
	}
	else if (ParticleObject.GetType() == TImplicitObjectUnion<T, d>::GetType() && LevelsetObject.GetType() == TImplicitObjectUnion<T, d>::GetType())
	{
		return UpdateUnionUnionConstraint<UpdateType>(Thickness, Constraint);
	}
	else if (ParticleObject.IsConvex() && LevelsetObject.IsConvex())
	{
		UpdateConvexConstraintsUsingCoreShapes<UpdateType>(ParticleObject, ParticleTM, LevelsetObject, LevelsetTM, Thickness, Constraint);
	}
	else if (LevelsetObject.IsUnderlyingUnion())
	{
		UpdateUnionLevelsetConstraint<UpdateType>(Thickness, Constraint);
	}
	else if (ParticleObject.IsUnderlyingUnion())
	{
		UpdateLevelsetUnionConstraint<UpdateType>(Thickness, Constraint);
	}
	else
	{
		UpdateLevelsetConstraintImp<UpdateType>(Thickness, Constraint);
	}
}

DECLARE_CYCLE_STAT(TEXT("UpdateConstraint"), STAT_UpdateConstraint2, STATGROUP_ChaosWide);

template<typename T, int d>
template<ECollisionUpdateType UpdateType>
void TPBDCollisionConstraint<T, d>::UpdateConstraint(const T Thickness, FRigidBodyContactConstraint& Constraint)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateConstraint2);

	Constraint.Phi = Thickness;
	
	const TRigidTransform<T, d> ParticleTM = GetTransform(Constraint.Particle);
	const TRigidTransform<T, d> LevelsetTM = GetTransform(Constraint.Levelset);

	if (!Constraint.Particle->Geometry())
	{
		if (Constraint.Levelset->Geometry())
		{
			if (!Constraint.Levelset->Geometry()->IsUnderlyingUnion())
			{
				TPBDCollisionConstraint<T, d>::UpdateLevelsetConstraint<UpdateType>(Thickness, Constraint);
			}
			else
			{
				UpdateUnionLevelsetConstraint<UpdateType>(Thickness, Constraint);
			}
		}
	}
	else
	{
		UpdateConstraintImp2<UpdateType>(*Constraint.Particle->Geometry(), ParticleTM, *Constraint.Levelset->Geometry(), LevelsetTM, Thickness, Constraint);
	}
}

template class TPBDCollisionConstraintHandle<float, 3>;
template class TAccelerationStructureHandle<float, 3>;
template class CHAOS_API TPBDCollisionConstraint<float, 3>;
template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::UpdateConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<ECollisionUpdateType::Any>(const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::UpdateLevelsetConstraint<ECollisionUpdateType::Deepest>(const float Thickness, FRigidBodyContactConstraint& Constraint);
template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<false>(const TPBDCollisionConstraint<float,3>::FAccelerationStructure&, float Dt);
template void TPBDCollisionConstraint<float, 3>::ComputeConstraints<true>(const TPBDCollisionConstraint<float, 3>::FAccelerationStructure&, float Dt);

template void UpdateConstraintImp2<ECollisionUpdateType::Any, float, 3>(const TImplicitObject<float, 3>& ParticleObject, const TRigidTransform<float, 3>& ParticleTM, const TImplicitObject<float, 3>& LevelsetObject, const TRigidTransform<float, 3>& LevelsetTM, const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);
template void UpdateConstraintImp2<ECollisionUpdateType::Deepest, float, 3>(const TImplicitObject<float, 3>& ParticleObject, const TRigidTransform<float, 3>& ParticleTM, const TImplicitObject<float, 3>& LevelsetObject, const TRigidTransform<float, 3>& LevelsetTM, const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);
}
