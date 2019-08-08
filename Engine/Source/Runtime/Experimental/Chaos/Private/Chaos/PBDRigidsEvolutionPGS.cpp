// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolutionPGS.h"

#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionConstraintPGS.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/DebugDrawQueue.h"

#define LOCTEXT_NAMESPACE "Chaos"

using namespace Chaos;

#if CHAOS_PARTICLEHANDLE_TODO
template<class T, int d>
TPBDRigidsEvolutionPGS<T, d>::TPBDRigidsEvolutionPGS(TPBDRigidParticles<T, d>&& InParticles, int32 NumIterations)
    : Base(MoveTemp(InParticles), NumIterations)
	, CollisionConstraints(Particles, NonDisabledIndices, Collided, PhysicsMaterials)
	, CollisionRule(CollisionConstraints)
{
	SetParticleUpdateVelocityFunction([PBDUpdateRule = TPerParticlePBDUpdateFromDeltaPosition<float, 3>(), this](TPBDRigidParticles<T, d>& ParticlesInput, const T Dt, const TArray<int32>& InActiveIndices) {
		PhysicsParallelFor(InActiveIndices.Num(), [&](int32 ActiveIndex) {
			int32 Index = InActiveIndices[ActiveIndex];
			PBDUpdateRule.Apply(ParticlesInput, Dt, Index);
		});
	});

	SetParticleUpdatePositionFunction([this](TPBDRigidParticles<T, d>& ParticlesInput, const T Dt)
	{
		const TArray<int32>& ActiveIndicesArray = GetActiveIndicesArray();
		PhysicsParallelFor(ActiveIndicesArray.Num(), [&](int32 ActiveIndex)
		{
			int32 Index = ActiveIndicesArray[ActiveIndex];
			ParticlesInput.X(Index) = ParticlesInput.P(Index);
			ParticlesInput.R(Index) = ParticlesInput.Q(Index);
		});
	});

	CollisionRule.BindToGraph(ConstraintGraph, INDEX_NONE);
}

template<class T, int d>
void TPBDRigidsEvolutionPGS<T, d>::IntegrateV(const TArray<int32>& InActiveIndices, const T Dt)
{	
	TPerParticleInitForce<T, d> InitForceRule;
	TPerParticleEulerStepVelocity<T, d> EulerStepVelocityRule;

	PhysicsParallelFor(InActiveIndices.Num(), [&](int32 ActiveIndex) {
		int32 Index = InActiveIndices[ActiveIndex];
		check(!Particles.Disabled(Index) && !Particles.Sleeping(Index));

		//save off previous velocities
		Particles.PreV(Index) = Particles.V(Index);
		Particles.PreW(Index) = Particles.W(Index);

		InitForceRule.Apply(Particles, Dt, Index);
		for (const FForceRule& ForceRule : ForceRules)
		{
			ForceRule(Particles, Dt, Index);
		}
		EulerStepVelocityRule.Apply(Particles, Dt, Index);
	});
}

template<class T, int d>
void TPBDRigidsEvolutionPGS<T, d>::IntegrateX(const TArray<int32>& InActiveIndices, const T Dt)
{	
	TPerParticleEtherDrag<T, d> EtherDragRule(0.0, 0.0);
	TPerParticlePBDEulerStep<T, d> EulerStepRule;
	PhysicsParallelFor(InActiveIndices.Num(), [&](int32 ActiveIndex) {
		int32 Index = InActiveIndices[ActiveIndex];
		EtherDragRule.Apply(Particles, Dt, Index);
		EulerStepRule.Apply(Particles, Dt, Index);
	});
}

template<class T, int d>
void TPBDRigidsEvolutionPGS<T, d>::AdvanceOneTimeStep(const T Dt)
{
	UE_LOG(LogChaos, Verbose, TEXT("START FRAME with Dt %f"), Dt);

	IntegrateV(GetActiveIndicesArray(), Dt);

	UpdateConstraintPositionBasedState(Dt);
	CreateConstraintGraph();
	CollisionRule.UpdatePositionBasedState(Particles, NonDisabledIndices, Dt);
	CollisionRule.AddToGraph();
	CreateIslands();
	CollisionRule.InitializeAccelerationStructures();

	TArray<bool> SleepedIslands;
	SleepedIslands.SetNum(ConstraintGraph.NumIslands());
	TArray<TArray<int32>> DisabledParticles;
	DisabledParticles.SetNum(ConstraintGraph.NumIslands());
	PhysicsParallelFor(ConstraintGraph.NumIslands(), [&](int32 Island) {
		const TArray<int32>& IslandParticleIndices = ConstraintGraph.GetIslandParticles(Island);

		CollisionRule.UpdateAccelerationStructures(Particles, NonDisabledIndices, Island);
		CollisionRule.ApplyConstraints(Particles, Dt, Island);

		IntegrateX(IslandParticleIndices, Dt);

		ApplyConstraints(Dt, Island);

		ApplyPushOut(Dt, Island);

		UpdateVelocities(Dt, Island);

		// Turn off if not moving
		SleepedIslands[Island] = ConstraintGraph.SleepInactive(Particles, Island, PhysicsMaterials);
		// @todo(mlentine): Find a good way of not doing this when we aren't using this functionality
		for (const int32 Index : IslandParticleIndices)
		{
			if (Particles.ObjectState(Index) != EObjectStateType::Kinematic && Particles.V(Index).SizeSquared() < PhysicsMaterials[Index]->DisabledLinearThreshold && Particles.W(Index).SizeSquared() < PhysicsMaterials[Index]->DisabledAngularThreshold)
			{
				Particles.SetDisabledLowLevel(Index, true);
				DisabledParticles[Island].Add(Index);
			}
			if (!(ensure(!FMath::IsNaN(Particles.P(Index)[0])) && ensure(!FMath::IsNaN(Particles.P(Index)[1])) && ensure(!FMath::IsNaN(Particles.P(Index)[2]))))
			{
				Particles.SetDisabledLowLevel(Index, true);
				DisabledParticles[Island].Add(Index);
			}
		}
	});

	for (int32 i = 0; i < ConstraintGraph.NumIslands(); ++i)
	{
		if (SleepedIslands[i])
		{
			for (const int32 Index : ConstraintGraph.GetIslandParticles(i))
			{
				ActiveIndices.Remove(Index);
				NonDisabledIndices.Add(Index);
			}
		}
		for (const int32 Index : DisabledParticles[i])
		{
			ActiveIndices.Remove(Index);
			NonDisabledIndices.Add(Index);
		}
	}

	ParticleUpdatePosition(Particles, Dt);

#if CHAOS_DEBUG_DRAW
	if (FDebugDrawQueue::IsDebugDrawingEnabled())
	{
		for (uint32 Idx = 0; Idx < Particles.Size(); ++Idx)
		{
			if (Particles.Disabled(Idx)) { continue; }
			if (Particles.CollisionParticles(Idx))
			{
				for (uint32 CollisionIdx = 0; CollisionIdx < Particles.CollisionParticles(Idx)->Size(); ++CollisionIdx)
				{
					const TVector<T, d>& X = Particles.CollisionParticles(Idx)->X(CollisionIdx);
					const TVector<T, d> WorldX = TRigidTransform<T, d>(Particles.X(Idx), Particles.R(Idx)).TransformPosition(X);
					FDebugDrawQueue::GetInstance().DrawDebugPoint(WorldX, FColor::Purple, false, 1e-4, 0, 10.f);
				}
			}
			
		}
	}
#endif

	Time += Dt;
}

template class Chaos::TPBDRigidsEvolutionPGS<float, 3>;
#endif

#undef LOCTEXT_NAMESPACE
