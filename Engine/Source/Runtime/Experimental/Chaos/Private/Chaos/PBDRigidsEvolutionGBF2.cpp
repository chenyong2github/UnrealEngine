// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolutionGBF2.h"
#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/PBDCollisionConstraint.h"
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
#include "Chaos/Levelset.h"
#include "Chaos/ChaosPerfTest.h"

namespace Chaos
{
float HackMaxAngularVelocity2 = 1000.f;
FAutoConsoleVariableRef CVarHackMaxAngularVelocity2(TEXT("p.HackMaxAngularVelocity2"), HackMaxAngularVelocity2, TEXT("Max cap on angular velocity: rad/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));

float HackMaxVelocity2 = -1.f;
FAutoConsoleVariableRef CVarHackMaxVelocity2(TEXT("p.HackMaxVelocity2"), HackMaxVelocity2, TEXT("Max cap on velocity: cm/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));


float HackLinearDrag2 = 0.f;
FAutoConsoleVariableRef CVarHackLinearDrag2(TEXT("p.HackLinearDrag2"), HackLinearDrag2, TEXT("Linear drag used to slow down objects. This is a hack and should not be relied on as a feature."));

float HackAngularDrag2 = 0.f;
FAutoConsoleVariableRef CVarHackAngularDrag2(TEXT("p.HackAngularDrag2"), HackAngularDrag2, TEXT("Angular drag used to slow down objects. This is a hack and should not be relied on as a feature."));

int DisableThreshold2 = 5;
FAutoConsoleVariableRef CVarDisableThreshold2(TEXT("p.DisableThreshold2"), DisableThreshold2, TEXT("Disable threshold frames to transition to sleeping"));

template <typename T, int d>
void TPBDRigidsEvolutionGBF2<T, d>::Integrate(T Dt)
{
	//SCOPE_CYCLE_COUNTER(STAT_Integrate);
	CHAOS_SCOPED_TIMER(Integrate);
	TPerParticleInitForce<T, d> InitForceRule;
	TPerParticleEulerStepVelocity<T, d> EulerStepVelocityRule;
	TPerParticleEtherDrag<T, d> EtherDragRule(HackLinearDrag2, HackAngularDrag2);
	TPerParticlePBDEulerStep<T, d> EulerStepRule;

	const T MaxAngularSpeedSq = HackMaxAngularVelocity2 * HackMaxAngularVelocity2;
	const T MaxSpeedSq = HackMaxVelocity2 * HackMaxVelocity2;
	PhysicsParallelFor(DynamicAwakeParticles.Size(), [&](int32 Index)
	{
		//save off previous velocities
		DynamicAwakeParticles.PreV(Index) = DynamicAwakeParticles.V(Index);
		DynamicAwakeParticles.PreW(Index) = DynamicAwakeParticles.W(Index);

		InitForceRule.Apply(DynamicAwakeParticles, Dt, Index);
		for (FForceRule ForceRule : ForceRules)
		{
			ForceRule(DynamicAwakeParticles, Dt, Index);
		}
		EulerStepVelocityRule.Apply(DynamicAwakeParticles, Dt, Index);
		EtherDragRule.Apply(DynamicAwakeParticles, Dt, Index);

		if (HackMaxAngularVelocity2 >= 0.f)
		{
			const T AngularSpeedSq = DynamicAwakeParticles.W(Index).SizeSquared();
			if (AngularSpeedSq > MaxAngularSpeedSq)
			{
				DynamicAwakeParticles.W(Index) = DynamicAwakeParticles.W(Index) * (HackMaxAngularVelocity2 / FMath::Sqrt(AngularSpeedSq));
			}
		}

		if (HackMaxVelocity2 >= 0.f)
		{
			const T SpeedSq = DynamicAwakeParticles.V(Index).SizeSquared();
			if (SpeedSq > MaxSpeedSq)
			{
				DynamicAwakeParticles.V(Index) = DynamicAwakeParticles.V(Index) * (HackMaxVelocity2 / FMath::Sqrt(SpeedSq));
			}
		}


		EulerStepRule.Apply(DynamicAwakeParticles, Dt, Index);
	});

	{
		//SCOPE_CYCLE_COUNTER(STAT_ParticleUpdatePosition);
		ParticleUpdatePosition(DynamicAwakeParticles, Dt);
	}
}

template <typename T, int d>
void TPBDRigidsEvolutionGBF2<T, d>::AdvanceOneTimeStep(T Dt)
{
	Integrate(Dt);

	UpdateConstraintPositionBasedState(Dt);
	CreateConstraintGraph();
	CreateIslands();
	
	TArray<bool> SleepedIslands;
	SleepedIslands.SetNum(ConstraintGraph.NumIslands());
	{
		PhysicsParallelFor(ConstraintGraph.NumIslands(), [&](int32 Island) {
			const TArray<TGeometryParticleHandle<T, d>*>& IslandParticles = ConstraintGraph.GetIslandParticles(Island);

			ApplyConstraints(Dt, Island);

			UpdateVelocities(Dt, Island);

			ApplyPushOut(Dt, Island);
		});
	}

	ParticleUpdatePosition(DynamicAwakeParticles, Dt);
}

template <typename T, int d>
TPBDRigidsEvolutionGBF2<T, d>::TPBDRigidsEvolutionGBF2(int32 InNumIterations)
	: Base(InNumIterations)
{
	SetParticleUpdateVelocityFunction([PBDUpdateRule = TPerParticlePBDUpdateFromDeltaPosition<float, 3>(), this](TPBDRigidParticles<T, d>& ParticlesInput, const T Dt) {
		PhysicsParallelFor(ParticlesInput.Size(), [&](int32 Index) {
			PBDUpdateRule.Apply(ParticlesInput, Dt, Index);
		});
	});

	SetParticleUpdatePositionFunction([this](TPBDRigidParticles<T, d>& ParticlesInput, const T Dt)
	{
		PhysicsParallelFor(ParticlesInput.Size(), [&](int32 Index)
		{
			ParticlesInput.X(Index) = ParticlesInput.P(Index);
			ParticlesInput.R(Index) = ParticlesInput.Q(Index);
		});
	});

	//AddConstraintRule(&CollisionRule);
}

}

template class Chaos::TPBDRigidsEvolutionGBF2<float, 3>;