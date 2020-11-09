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
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Collision Rule"), STAT_ChaosPBDCollisionRule, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Collider Friction"), STAT_ChaosPBDCollisionRuleFriction, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Collider Kinematic Update"), STAT_CollisionKinematicUpdate, STATGROUP_Chaos);

using namespace Chaos;

template<class T, int d>
void TPBDEvolution<T, d>::AddGroups(int32 NumGroups)
{
	// Add elements
	const uint32 Offset = TArrayCollection::Size();
	TArrayCollection::AddElementsHelper(NumGroups);

	// Set defaults
	for (uint32 GroupId = Offset; GroupId < TArrayCollection::Size(); ++GroupId)
	{
		MGroupGravityForces[GroupId].SetAcceleration(MGravity);
		MGroupCollisionThicknesses[GroupId] = MCollisionThickness;
		MGroupSelfCollisionThicknesses[GroupId] = MSelfCollisionThickness;
		MGroupCoefficientOfFrictions[GroupId] = MCoefficientOfFriction;
		MGroupDampings[GroupId] = MDamping;
		MGroupCenterOfMass[GroupId] = TVector<T,d>(0.);
		MGroupVelocity[GroupId] = TVector<T,d>(0.);
		MGroupAngularVelocity[GroupId] = TVector<T,d>(0.);
	}
}

template<class T, int d>
void TPBDEvolution<T, d>::ResetGroups()
{
	TArrayCollection::ResizeHelper(0);
	AddGroups(1);  // Add default group
}

template<class T, int d>
TPBDEvolution<T, d>::TPBDEvolution(TPBDParticles<T, d>&& InParticles, TKinematicGeometryClothParticles<T, d>&& InGeometryParticles, TArray<TVector<int32, 3>>&& CollisionTriangles,
    int32 NumIterations, T CollisionThickness, T SelfCollisionThickness, T CoefficientOfFriction, T Damping)
    : MParticles(MoveTemp(InParticles))
	, MParticlesActiveView(MParticles)
	, MCollisionParticles(MoveTemp(InGeometryParticles))
	, MCollisionParticlesActiveView(MCollisionParticles)
	, MCollisionTriangles(MoveTemp(CollisionTriangles))
	, MConstraintInitsActiveView(MConstraintInits)
	, MConstraintRulesActiveView(MConstraintRules)
	, MNumIterations(NumIterations)
	, MGravity(TVector<T, d>((T)0., (T)0., (T)-980.665))
	, MCollisionThickness(CollisionThickness)
	, MSelfCollisionThickness(SelfCollisionThickness)
	, MCoefficientOfFriction(CoefficientOfFriction)
	, MDamping(Damping)
	, MTime(0)
{
	// Add group arrays
	TArrayCollection::AddArray(&MGroupGravityForces);
	TArrayCollection::AddArray(&MGroupVelocityFields);
	TArrayCollection::AddArray(&MGroupForceRules);
	TArrayCollection::AddArray(&MGroupCollisionThicknesses);
	TArrayCollection::AddArray(&MGroupSelfCollisionThicknesses);
	TArrayCollection::AddArray(&MGroupCoefficientOfFrictions);
	TArrayCollection::AddArray(&MGroupDampings);
	TArrayCollection::AddArray(&MGroupCenterOfMass);
	TArrayCollection::AddArray(&MGroupVelocity);
	TArrayCollection::AddArray(&MGroupAngularVelocity);
	AddGroups(1);  // Add default group

	// Add particle arrays
	MParticles.AddArray(&MParticleGroupIds);
	MCollisionParticles.AddArray(&MCollided);
	MCollisionParticles.AddArray(&MCollisionParticleGroupIds);

	MParticleUpdate =  // TODO(Kriss.Gossart): this callable seems redundant, might be worth taking it off
		[PBDUpdateRule = 
			TPerParticlePBDUpdateFromDeltaPosition<T, d>()](TPBDActiveView<TPBDParticles<T, d>>& ParticlesView, const T Dt) 
			{
				// Don't bother with threaded execution if we don't have enough work to make it worth while.
				const int32 MinParallelBatchSize = 1000; // TODO: 1000 is a guess, tune this!

				ParticlesView.ParallelFor(
					[PBDUpdateRule, Dt](TPBDParticles<T, d>& Particles, int32 Index)
					{
						PBDUpdateRule.Apply(Particles, Dt, Index);
					}, MinParallelBatchSize);
			};
}

template<class T, int d>
void TPBDEvolution<T, d>::ResetParticles()
{
	// Reset particles
	MParticles.Resize(0);
	MParticlesActiveView.Reset();

	// Reset particle groups
	ResetGroups();
}

template<class T, int d>
int32 TPBDEvolution<T, d>::AddParticleRange(int32 NumParticles, uint32 GroupId, bool bActivate)
{
	if (NumParticles)
	{
		const int32 Offset = (int32)MParticles.Size();

		MParticles.AddParticles(NumParticles);

		// Initialize the new particles' group ids
		for (int32 i = Offset; i < (int32)MParticles.Size(); ++i)
		{
			MParticleGroupIds[i] = GroupId;
		}

		// Resize the group parameter arrays
		const uint32 GroupSize = TArrayCollection::Size();
		if (GroupId >= GroupSize)
		{
			AddGroups(GroupId + 1 - GroupSize);
		}

		// Add range
		MParticlesActiveView.AddRange(NumParticles, bActivate);

		return Offset;
	}
	return INDEX_NONE;
}

template<class T, int d>
void TPBDEvolution<T, d>::ResetCollisionParticles(int32 NumParticles)
{
	MCollisionParticles.Resize(NumParticles);
	MCollisionParticlesActiveView.Reset(NumParticles);
}

template<class T, int d>
int32 TPBDEvolution<T, d>::AddCollisionParticleRange(int32 NumParticles, uint32 GroupId, bool bActivate)
{
	if (NumParticles)
	{
		const int32 RangeOffset = (int32)MCollisionParticles.Size();

		MCollisionParticles.AddParticles(NumParticles);

		// Initialize the new particles' group ids
		for (int32 i = RangeOffset; i < (int32)MCollisionParticles.Size(); ++i)
		{
			MCollisionParticleGroupIds[i] = GroupId;
		}

		// Add range
		MCollisionParticlesActiveView.AddRange(NumParticles, bActivate);
	
		return RangeOffset;
	}
	return INDEX_NONE;
}

template<class T, int d>
int32 TPBDEvolution<T, d>::AddConstraintInitRange(int32 NumConstraints, bool bActivate)
{
	// Add new constraint init functions
	MConstraintInits.AddDefaulted(NumConstraints);

	// Add range
	return MConstraintInitsActiveView.AddRange(NumConstraints, bActivate);
}

template<class T, int d>
int32 TPBDEvolution<T, d>::AddConstraintRuleRange(int32 NumConstraints, bool bActivate)
{
	// Add new constraint rule functions
	MConstraintRules.AddDefaulted(NumConstraints);

	// Add range
	return MConstraintRulesActiveView.AddRange(NumConstraints, bActivate);
}

template<class T, int d>
void TPBDEvolution<T, d>::AdvanceOneTimeStep(const T Dt)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosPBDVAdvanceTime);
	TPerParticleInitForce<T, d> InitForceRule;
	TPerParticleEulerStepVelocity<T, d> EulerStepVelocityRule;
	TPerGroupDampVelocity<T, d> DampVelocityRule(
		MParticleGroupIds,
		MGroupDampings,
		MGroupCenterOfMass,
		MGroupVelocity,
		MGroupAngularVelocity);

	TPerParticlePBDEulerStep<T, d> EulerStepRule;
	TPerParticlePBDCollisionConstraint<T, d, EGeometryParticlesSimType::Other> CollisionRule(MCollisionParticlesActiveView, MCollided, MParticleGroupIds, MCollisionParticleGroupIds, MGroupCollisionThicknesses, MGroupCoefficientOfFrictions);

	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDVelocityDampUpdateState);
		DampVelocityRule.UpdateGroupPositionBasedState(MParticlesActiveView);
	}	

	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDVelocityFieldUpdateForces);
		for (const FVelocityField& VelocityField : MGroupVelocityFields)
		{
			VelocityField.UpdateForces(MParticles, Dt);  // Update force per surface element
		}
	}	

	memset(MCollided.GetData(), 0, MCollided.Num() * sizeof(bool));

	// Don't bother with threaded execution if we don't have enough work to make it worth while.
	const int32 MinParallelBatchSize = 1000; // TODO: 1000 is a guess, tune this!

	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDPreIterationUpdates);

		MParticlesActiveView.ParallelFor(
			[this, Dt, &InitForceRule, &EulerStepVelocityRule, &DampVelocityRule, &EulerStepRule](TPBDParticles<T, d>& Particles, int32 Index)
			{
				const uint32 ParticleGroupId = MParticleGroupIds[Index];

				InitForceRule.Apply(Particles, Dt, Index); // F = TV(0)
				MGroupGravityForces[ParticleGroupId].Apply(Particles, Dt, Index); // F += M * G

				if (MGroupForceRules[ParticleGroupId])
				{
					MGroupForceRules[ParticleGroupId](Particles, Dt, Index); // F += M * A
				}

				MGroupVelocityFields[ParticleGroupId].Apply(Particles, Dt, Index);

				if (MKinematicUpdate)
				{
					MKinematicUpdate(Particles, Dt, MTime + Dt, Index); // X = ...
				}
				EulerStepVelocityRule.Apply(Particles, Dt, Index);
				DampVelocityRule.Apply(Particles, Dt, Index);
				EulerStepRule.Apply(Particles, Dt, Index);
			}, MinParallelBatchSize);
	}

	if (MCollisionKinematicUpdate)
	{
		SCOPE_CYCLE_COUNTER(STAT_CollisionKinematicUpdate);

		MCollisionParticlesActiveView.SequentialFor(
			[this, Dt](TKinematicGeometryClothParticles<T, d>& CollisionParticles, int32 Index)
			{
				MCollisionKinematicUpdate(CollisionParticles, Dt, MTime + Dt, Index);
			});
	}
#if !COMPILE_WITHOUT_UNREAL_SUPPORT
	TPBDCollisionSpringConstraints<T, d> SelfCollisionRule(MParticlesActiveView, MCollisionTriangles, MDisabledCollisionElements, MParticleGroupIds, MGroupSelfCollisionThicknesses, Dt);
#endif

	MConstraintInitsActiveView.SequentialFor(
		[Dt](TArray<TFunction<void()>>& ConstraintInits, int32 Index)
		{
			ConstraintInits[Index]();  // Clear XPBD's Lambdas
		});

	// Do one extra collision pass at the start to decrease likelihood of cloth penetrating- TODO: Add option for more collision passed interleaved between constraints
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDCollisionRule);

		MParticlesActiveView.RangeFor(
			[&CollisionRule, Dt](TPBDParticles<T, d>& Particles, int32 Offset, int32 Range)
			{
				CollisionRule.ApplyRange(Particles, Dt, Offset, Range);
			});
	}

	for (int i = 0; i < MNumIterations; ++i)
	{
		MConstraintRulesActiveView.SequentialFor(
			[this, Dt](TArray<TFunction<void(TPBDParticles<T, d>&, const T)>>& ConstraintRules, int32 Index)
			{
				SCOPE_CYCLE_COUNTER(STAT_ChaosPBDConstraintRule);
				ConstraintRules[Index](MParticles, Dt); // P +/-= ...
			});
#if !COMPILE_WITHOUT_UNREAL_SUPPORT
		{
			SCOPE_CYCLE_COUNTER(STAT_ChaosPBDSelfCollisionRule);
			SelfCollisionRule.Apply(MParticles, Dt);
		}
#endif
		{
			SCOPE_CYCLE_COUNTER(STAT_ChaosPBDCollisionRule);
			MParticlesActiveView.RangeFor(
				[&CollisionRule, Dt](TPBDParticles<T, d>& Particles, int32 Offset, int32 Range)
				{
					CollisionRule.ApplyRange(Particles, Dt, Offset, Range);
				});
		}
	}
	check(MParticleUpdate);
	MParticleUpdate(MParticlesActiveView, Dt); // V = (P - X) / Dt; X = P;

	if (MCoefficientOfFriction > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosPBDCollisionRuleFriction);
		MParticlesActiveView.ParallelFor(
			[&CollisionRule, Dt](TPBDParticles<T, d>& Particles, int32 Index)
			{
				CollisionRule.ApplyFriction(Particles, Dt, Index);
			}, MinParallelBatchSize);
	}

	MTime += Dt;
}

template class Chaos::TPBDEvolution<float, 3>;
