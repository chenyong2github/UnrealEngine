// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDEvolution.h"

#include "Chaos/Framework/Parallel.h"
#include "Chaos/PBDCollisionSphereConstraints.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PerGroupDampVelocity.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDCollisionConstraint.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"


DECLARE_CYCLE_STAT(TEXT("Chaos PBD Advance Time"), STAT_ChaosPBDVAdvanceTime, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Velocity Damping State Update"), STAT_ChaosPBDVelocityDampUpdateState, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Velocity Field Update Forces"), STAT_ChaosPBDVelocityFieldUpdateForces, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Velocity Damping"), STAT_ChaosPBDVelocityDampUpdate, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Pre Iteration Updates"), STAT_ChaosPBDPreIterationUpdates, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Constraint Rule"), STAT_ChaosPBDConstraintRule, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Self Collision"), STAT_ChaosPBDSelfCollisionRule, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Collision"), STAT_ChaosPBDCollisionRule, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Collider Friction"), STAT_ChaosPBDCollisionRuleFriction, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Collider Kinematic Update"), STAT_CollisionKinematicUpdate, STATGROUP_Chaos);

using namespace Chaos;

template<class T, int d>
TPBDEvolution<T, d>::TPBDEvolution(TPBDParticles<T, d>&& InParticles, TKinematicGeometryClothParticles<T, d>&& InGeometryParticles, TArray<TVector<int32, 3>>&& CollisionTriangles,
    int32 NumIterations, T CollisionThickness, T SelfCollisionThickness, T CoefficientOfFriction, T Damping)
    : MParticles(MoveTemp(InParticles))
	, MCollisionParticles(MoveTemp(InGeometryParticles))
	, MCollisionTriangles(MoveTemp(CollisionTriangles))
	, MNumIterations(NumIterations)
	, MCollisionThickness(CollisionThickness)
	, MSelfCollisionThickness(SelfCollisionThickness)
	, MCoefficientOfFriction(CoefficientOfFriction)
	, MDamping(Damping)
	, MTime(0)
{
	MCollisionParticles.AddArray(&MCollided);
	MCollisionParticles.AddArray(&MCollisionParticleGroupIds);
	MParticles.AddArray(&MParticleGroupIds);
	MPerGroupDamping.Add(Damping);
	MPerGroupCollisionThickness.Add(CollisionThickness);
	MPerGroupCoefficientOfFriction.Add(CoefficientOfFriction);

	SetParticleUpdateFunction(
		[PBDUpdateRule = 
			TPerParticlePBDUpdateFromDeltaPosition<float, 3>()](TPBDParticles<T, d>& MParticlesInput, const T Dt) 
			{
				// Don't bother with threaded execution if we don't have enough work to make it worth while.
				const bool NonParallelUpdate = MParticlesInput.Size() > 1000; // TODO: 1000 is a guess, tune this!
				PhysicsParallelFor(MParticlesInput.Size(), [&](int32 Index) {
					PBDUpdateRule.Apply(MParticlesInput, Dt, Index);
				}, NonParallelUpdate);
			});
}

template<class T, int d>
uint32 TPBDEvolution<T, d>::AddParticles(uint32 Num, uint32 GroupId)
{
	// Add new particles
	const uint32 Offset = MParticles.Size();
	MParticles.AddParticles(Num);

	// Initialize the new particles' group ids
	for (uint32 i = Offset; i < MParticles.Size(); ++i)
	{
		MParticleGroupIds[i] = GroupId;
	}

	// Resize group parameter arrays
	const uint32 GroupNum = (uint32)MPerGroupDamping.Num();
	if (GroupId >= GroupNum)
	{
		MPerGroupDamping.SetNum((int32)GroupId + 1);
		MPerGroupCollisionThickness.SetNum((int32)GroupId + 1);
		MPerGroupCoefficientOfFriction.SetNum((int32)GroupId + 1);
		for (uint32 i = GroupNum; i <= GroupId; ++i)
		{
			MPerGroupDamping[i] = MDamping;
			MPerGroupCollisionThickness[i] = MCollisionThickness;
			MPerGroupCoefficientOfFriction[i] = MCoefficientOfFriction;
		}
	}
	return Offset;
}

template<class T, int d>
uint32 TPBDEvolution<T, d>::AddCollisionParticles(uint32 Num, uint32 GroupId)
{
	// Add new particles
	const uint32 Offset = MCollisionParticles.Size();
	MCollisionParticles.AddParticles(Num);

	// Initialize the new particles' group ids
	for (uint32 i = Offset; i < MCollisionParticles.Size(); ++i)
	{
		MCollisionParticleGroupIds[i] = GroupId;
	}
	return Offset;
}

template<class T, int d>
void TPBDEvolution<T, d>::AdvanceOneTimeStep(const T Dt)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosPBDVAdvanceTime);
	TPerParticleInitForce<T, d> InitForceRule;
	TPerParticleEulerStepVelocity<T, d> EulerStepVelocityRule;
	TPerGroupDampVelocity<T, d> DampVelocityRule(MParticleGroupIds, MPerGroupDamping);
	TPerParticlePBDEulerStep<T, d> EulerStepRule;
	TPerParticlePBDCollisionConstraint<T, d, EGeometryParticlesSimType::Other> CollisionRule(MCollisionParticles, MCollided, MParticleGroupIds, MCollisionParticleGroupIds, MPerGroupCollisionThickness, MPerGroupCoefficientOfFriction);

	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDVelocityDampUpdateState);
		DampVelocityRule.UpdatePositionBasedState(MParticles);
	}	

	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDVelocityFieldUpdateForces);
		for (FVelocityField& VelocityField : VelocityFields)
		{
			VelocityField.UpdateForces(MParticles, Dt);
		}
	}	

	// Don't bother with threaded execution if we don't have enough work to make it worth while.
	const bool NonParallelUpdate = MParticles.Size() < 1000; // TODO: 1000 is a guess, tune this!

	//PhysicsParallelFor(MCollisionParticles.Size(), [&](int32 Index) 
	//{ MCollided[Index] = false; }, NonParallelUpdate);
	//for(int32 i=0; i < MCollided.Num(); i++)
	//	MCollided[i] = false;
	memset(MCollided.GetData(), 0, MCollided.Num()*sizeof(bool));
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDPreIterationUpdates);
		PhysicsParallelFor(MParticles.Size(), [&](int32 Index)
		{
			InitForceRule.Apply(MParticles, Dt, Index); // F = TV(0)
			GravityForces.Apply(MParticles, Dt, Index);
			for (TFunction<void(TPBDParticles<T, d>&, const T, const int32)>& ForceRule : MForceRules)
			{
				ForceRule(MParticles, Dt, Index); // F += M * A
			}
			for (FVelocityField& VelocityField : VelocityFields)
			{
				VelocityField.Apply(MParticles, Dt, Index);
			}
			if (MKinematicUpdate)
			{
				MKinematicUpdate(MParticles, Dt, MTime + Dt, Index); // X = ...
			}
			EulerStepVelocityRule.Apply(MParticles, Dt, Index);
			DampVelocityRule.Apply(MParticles, Dt, Index);
			EulerStepRule.Apply(MParticles, Dt, Index);
		}, NonParallelUpdate);
	}

	if (MCollisionKinematicUpdate)
	{
		SCOPE_CYCLE_COUNTER(STAT_CollisionKinematicUpdate);
		PhysicsParallelFor(MCollisionParticles.Size(), [&](int32 Index) {
			MCollisionKinematicUpdate(MCollisionParticles, Dt, MTime + Dt, Index);
		}, NonParallelUpdate);
	}
#if !COMPILE_WITHOUT_UNREAL_SUPPORT
	TPBDCollisionSpringConstraints<T, d> SelfCollisionRule(MParticles, MCollisionTriangles, MDisabledCollisionElements, Dt, MSelfCollisionThickness, 1.5f);
#endif

	for (TFunction<void()>& InitConstraintRule : MInitConstraintRules)
	{
		InitConstraintRule();  // Clear XPBD's Lambdas
	}


	// Do one extra collision pass at the start to decrease likelyhood of cloth penetrating- TODO: Add option for more collision passed interleaved between constraints
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDCollisionRule);
		CollisionRule.ApplyPerParticle(MParticles, Dt);
	}

	for (int i = 0; i < MNumIterations; ++i)
	{
		for (TFunction<void(TPBDParticles<T, d>&, const T)>& ConstraintRule : MConstraintRules)
		{
			SCOPE_CYCLE_COUNTER(STAT_ChaosPBDConstraintRule);
			ConstraintRule(MParticles, Dt); // P +/-= ...
		}
#if !COMPILE_WITHOUT_UNREAL_SUPPORT
		{
			SCOPE_CYCLE_COUNTER(STAT_ChaosPBDSelfCollisionRule);
			SelfCollisionRule.Apply(MParticles, Dt);
		}
#endif
		{
			SCOPE_CYCLE_COUNTER(STAT_ChaosPBDCollisionRule);
			CollisionRule.ApplyPerParticle(MParticles, Dt);
		}
	}
	check(MParticleUpdate);
	MParticleUpdate(MParticles, Dt); // V = (P - X) / Dt; X = P;
	if (MCoefficientOfFriction > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDCollisionRuleFriction);
		PhysicsParallelFor(MParticles.Size(), [&](int32 Index) {
			CollisionRule.ApplyFriction(MParticles, Dt, Index);
		}, NonParallelUpdate);
	}

	MTime += Dt;
}

template class Chaos::TPBDEvolution<float, 3>;
