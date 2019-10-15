// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolutionGBF.h"
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
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace Chaos
{
float HackMaxAngularVelocity = 1000.f;
FAutoConsoleVariableRef CVarHackMaxAngularVelocity(TEXT("p.HackMaxAngularVelocity"), HackMaxAngularVelocity, TEXT("Max cap on angular velocity: rad/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));

float HackMaxVelocity = -1.f;
FAutoConsoleVariableRef CVarHackMaxVelocity(TEXT("p.HackMaxVelocity2"), HackMaxVelocity, TEXT("Max cap on velocity: cm/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));


float HackLinearDrag = 0.f;
FAutoConsoleVariableRef CVarHackLinearDrag(TEXT("p.HackLinearDrag2"), HackLinearDrag, TEXT("Linear drag used to slow down objects. This is a hack and should not be relied on as a feature."));

float HackAngularDrag = 0.f;
FAutoConsoleVariableRef CVarHackAngularDrag(TEXT("p.HackAngularDrag2"), HackAngularDrag, TEXT("Angular drag used to slow down objects. This is a hack and should not be relied on as a feature."));

int DisableThreshold = 5;
FAutoConsoleVariableRef CVarDisableThreshold(TEXT("p.DisableThreshold2"), DisableThreshold, TEXT("Disable threshold frames to transition to sleeping"));


DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::AdvanceOneTimeStep"), STAT_AdvanceOneTimeStep, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::Integrate"), STAT_Integrate, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::KinematicTargets"), STAT_KinematicTargets, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::UpdateConstraintPositionBasedState"), STAT_UpdateConstraintPositionBasedState, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::CreateConstraintGraph"), STAT_CreateConstraintGraph, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::CreateIslands"), STAT_CreateIslands, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::ParallelSolve"), STAT_ParallelSolve, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::DeactivateSleep"), STAT_DeactivateSleep, STATGROUP_Chaos);

int32 SerializeEvolution = 0;
FAutoConsoleVariableRef CVarSerializeEvolution(TEXT("p.SerializeEvolution"), SerializeEvolution, TEXT(""));

#if !UE_BUILD_SHIPPING
template <typename TEvolution>
void SerializeToDisk(TEvolution& Evolution)
{
	const TCHAR* FilePrefix = TEXT("ChaosEvolution");
	const FString FullPathPrefix = FPaths::ProfilingDir() / FilePrefix;

	static FCriticalSection CS;	//many evolutions could be running in parallel, serialize one at a time to avoid file conflicts
	FScopeLock Lock(&CS);

	int32 Tries = 0;
	FString UseFileName;
	do
	{
		UseFileName = FString::Printf(TEXT("%s_%d.bin"), *FullPathPrefix, Tries++);
	} while (IFileManager::Get().FileExists(*UseFileName));

	//this is not actually file safe but oh well, very unlikely someone else is trying to create this file at the same time
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*UseFileName));
	if (File)
	{
		FChaosArchive Ar(*File);
		UE_LOG(LogChaos, Log, TEXT("SerializeToDisk File: %s"), *UseFileName);
		Evolution.Serialize(Ar);
	}
	else
	{
		UE_LOG(LogChaos, Warning, TEXT("Could not create file(%s)"), *UseFileName);
	}
}
#endif

template <typename T, int d>
void TPBDRigidsEvolutionGBF<T, d>::AdvanceOneTimeStep(T Dt)
{
	SCOPE_CYCLE_COUNTER(STAT_AdvanceOneTimeStep);

#if !UE_BUILD_SHIPPING
	if (SerializeEvolution)
	{
		SerializeToDisk(*this);
	}
#endif

	{
		SCOPE_CYCLE_COUNTER(STAT_Integrate);
		Integrate(Particles.GetNonDisabledDynamicView(), Dt);	//Question: should we use an awake view?
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_KinematicTargets);
		ApplyKinematicTargets(Dt);
	}

	if (PostIntegrateCallback != nullptr)
	{
		PostIntegrateCallback();
	}

	{
		Base::ComputeIntermediateSpatialAcceleration();
		CollisionConstraints.SetSpatialAcceleration(InternalAcceleration.Get());
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateConstraintPositionBasedState);
		UpdateConstraintPositionBasedState(Dt);
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateConstraintGraph);
		CreateConstraintGraph();
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateIslands);
		CreateIslands();
	}

	if (PreApplyCallback != nullptr)
	{
		PreApplyCallback();
	}

	TArray<bool> SleepedIslands;
	SleepedIslands.SetNum(ConstraintGraph.NumIslands());
	TArray<TArray<TPBDRigidParticleHandle<T,d>*>> DisabledParticles;
	DisabledParticles.SetNum(ConstraintGraph.NumIslands());
	SleepedIslands.SetNum(ConstraintGraph.NumIslands());
	if(Dt > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_ParallelSolve);
		PhysicsParallelFor(ConstraintGraph.NumIslands(), [&](int32 Island) {
			const TArray<TGeometryParticleHandle<T, d>*>& IslandParticles = ConstraintGraph.GetIslandParticles(Island);

			ApplyConstraints(Dt, Island);

			if (PostApplyCallback != nullptr)
			{
				PostApplyCallback(Island);
			}

			UpdateVelocities(Dt, Island);

			ApplyPushOut(Dt, Island);

			if (PostApplyPushOutCallback != nullptr)
			{
				PostApplyPushOutCallback(Island);
			}

			for (auto Particle : IslandParticles)
			{
				// If a dynamic particle is moving slowly enough for long enough, disable it.
				// @todo(mlentine): Find a good way of not doing this when we aren't using this functionality

				// increment the disable count for the particle
				if (auto PBDRigid = Particle->AsDynamic())
				{
					if (PBDRigid->AuxilaryValue(PhysicsMaterials) && PBDRigid->V().SizeSquared() < PBDRigid->AuxilaryValue(PhysicsMaterials)->DisabledLinearThreshold &&
						PBDRigid->W().SizeSquared() < PBDRigid->AuxilaryValue(PhysicsMaterials)->DisabledAngularThreshold)
					{
						++PBDRigid->AuxilaryValue(ParticleDisableCount);
					}

					// check if we're over the disable count threshold
					if (PBDRigid->AuxilaryValue(ParticleDisableCount) > DisableThreshold)
					{
						PBDRigid->AuxilaryValue(ParticleDisableCount) = 0;
						//Particles.Disabled(Index) = true;
						DisabledParticles[Island].Add(PBDRigid);
						//Particles.V(Index) = TVector<T, d>(0);
						//Particles.W(Index) = TVector<T, d>(0);
					}

					if (!(ensure(!FMath::IsNaN(PBDRigid->P()[0])) && ensure(!FMath::IsNaN(PBDRigid->P()[1])) && ensure(!FMath::IsNaN(PBDRigid->P()[2]))))
					{
						//Particles.Disabled(Index) = true;
						DisabledParticles[Island].Add(PBDRigid);
					}
				}
			}

			// Turn off if not moving
			SleepedIslands[Island] = ConstraintGraph.SleepInactive(Island, PhysicsMaterials);
		});
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_DeactivateSleep);
		for (int32 Island = 0; Island < ConstraintGraph.NumIslands(); ++Island)
		{
			if (SleepedIslands[Island])
			{
				Particles.DeactivateParticles(ConstraintGraph.GetIslandParticles(Island));
			}
			for (const auto Particle : DisabledParticles[Island])
			{
				Particles.DisableParticle(Particle);
			}
		}
	}

	Clustering.AdvanceClustering(Dt, GetCollisionConstraints());

	ParticleUpdatePosition(Particles.GetActiveParticlesView(), Dt);
}

template <typename T, int d>
TPBDRigidsEvolutionGBF<T, d>::TPBDRigidsEvolutionGBF(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations)
	: Base(InParticles, InNumIterations)
	, CollisionConstraints(InParticles, Collided, PhysicsMaterials, DefaultNumPushOutPairIterations)
	, CollisionRule(CollisionConstraints, DefaultNumPushOutIterations)
	, PostIntegrateCallback(nullptr)
	, PreApplyCallback(nullptr)
	, PostApplyCallback(nullptr)
	, PostApplyPushOutCallback(nullptr)
{
	SetParticleUpdateVelocityFunction([PBDUpdateRule = TPerParticlePBDUpdateFromDeltaPosition<float, 3>(), this](const TParticleView<TPBDRigidParticles<T, d>>& ParticlesInput, const T Dt) {
		ParticlesInput.ParallelFor([&](auto& Particle, int32 Index) {
			PBDUpdateRule.Apply(Particle, Dt);
		});
	});

	SetParticleUpdatePositionFunction([this](const TParticleView<TPBDRigidParticles<T, d>>& ParticlesInput, const T Dt)
	{
		ParticlesInput.ParallelFor([&](auto& Particle, int32 Index)
		{
			Particle.X() = Particle.P();
			Particle.R() = Particle.Q();
		});
	});

	AddForceFunction([this](TTransientPBDRigidParticleHandle<T, d>& HandleIn, const T Dt) 
	{
		ExternalForces.Apply(HandleIn, Dt);
	});

	AddForceFunction([this](TTransientPBDRigidParticleHandle<T, d>& HandleIn, const T Dt)
	{
		GravityForces.Apply(HandleIn, Dt);
	});

	AddConstraintRule(&CollisionRule);
}

template <typename T, int d>
void TPBDRigidsEvolutionGBF<T,d>::Serialize(FChaosArchive& Ar)
{
	Base::Serialize(Ar);
}

}

template class Chaos::TPBDRigidsEvolutionGBF<float, 3>;