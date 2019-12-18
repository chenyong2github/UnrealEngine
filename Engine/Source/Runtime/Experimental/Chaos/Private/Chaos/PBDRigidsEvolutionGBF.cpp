// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/PBDCollisionConstraints.h"
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
#if !UE_BUILD_SHIPPING
	CHAOS_API bool bPendingHierarchyDump = false;
#else
	const bool bPendingHierarchyDump = false;
#endif

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

float BoundsThickness = 0;
float BoundsThicknessVelocityMultiplier = 2.0f;
FAutoConsoleVariableRef CVarBoundsThickness(TEXT("p.CollisionBoundsThickness"), BoundsThickness, TEXT("Collision inflation for speculative contact generation.[def:0.0]"));
FAutoConsoleVariableRef CVarBoundsThicknessVelocityMultiplier(TEXT("p.CollisionBoundsVelocityInflation"), BoundsThicknessVelocityMultiplier, TEXT("Collision velocity inflation for speculatibe contact generation.[def:2.0]"));


DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::AdvanceOneTimeStep"), STAT_Evolution_AdvanceOneTimeStep, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::Integrate"), STAT_Evolution_Integrate, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::KinematicTargets"), STAT_Evolution_KinematicTargets, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::ApplyConstraints"), STAT_Evolution_ApplyConstraints, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::UpdateVelocities"), STAT_Evolution_UpdateVelocites, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::ApplyPushOut"), STAT_Evolution_ApplyPushOut, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::DetectCollisions"), STAT_Evolution_DetectCollisions, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::UpdateConstraintPositionBasedState"), STAT_Evolution_UpdateConstraintPositionBasedState, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::CreateConstraintGraph"), STAT_Evolution_CreateConstraintGraph, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::CreateIslands"), STAT_Evolution_CreateIslands, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::ParallelSolve"), STAT_Evolution_ParallelSolve, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::DeactivateSleep"), STAT_Evolution_DeactivateSleep, STATGROUP_Chaos);

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
void TPBDRigidsEvolutionGBF<T, d>::Advance(const T Dt, const T MaxStepDt, const int32 MaxSteps)
{
	// Determine how many steps we would like to take
	int32 NumSteps = FMath::CeilToInt(Dt / MaxStepDt);
	if (NumSteps > 0)
	{
		// Determine the step time
		const T StepDt = Dt / (T)NumSteps;

		// Limit the number of steps
		// NOTE: This is after step time calculation so sim will appear to slow down for large Dt
		// but that is preferable to blowing up from a large timestep.
		NumSteps = FMath::Clamp(NumSteps, 1, MaxSteps);

		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			// StepFraction: how much of the remaining time this step represents, used to interpolate kinematic targets
			// E.g., for 4 steps this will be: 1/4, 1/3, 1/2, 1
			const float StepFraction = (T)1 / (T)(NumSteps - Step);
		
			UE_LOG(LogChaos, Verbose, TEXT("Advance dt = %f [%d/%d]"), StepDt, Step + 1, NumSteps);

			AdvanceOneTimeStep(StepDt, StepFraction);
		}
	}
}

template <typename T, int d>
void TPBDRigidsEvolutionGBF<T, d>::AdvanceOneTimeStep(const T Dt, const T StepFraction)
{
	SCOPE_CYCLE_COUNTER(STAT_Evolution_AdvanceOneTimeStep);

#if !UE_BUILD_SHIPPING
	if (SerializeEvolution)
	{
		SerializeToDisk(*this);
	}
#endif

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_Integrate);
		Integrate(Particles.GetActiveParticlesView(), Dt);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_KinematicTargets);
		ApplyKinematicTargets(Dt, StepFraction);
	}

	if (PostIntegrateCallback != nullptr)
	{
		PostIntegrateCallback();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_UpdateConstraintPositionBasedState);
		UpdateConstraintPositionBasedState(Dt);
	}
	{
		Base::ComputeIntermediateSpatialAcceleration();
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_DetectCollisions);
		CollisionDetector.GetBroadPhase().SetSpatialAcceleration(InternalAcceleration.Get());

		CollisionStats::FStatData StatData(bPendingHierarchyDump);

		CollisionDetector.DetectCollisions(Dt, StatData);

		CHAOS_COLLISION_STAT(StatData.Print());
	}

	if (PostDetectCollisionsCallback != nullptr)
	{
		PostDetectCollisionsCallback();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CreateConstraintGraph);
		CreateConstraintGraph();
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CreateIslands);
		CreateIslands();
	}

	if (PreApplyCallback != nullptr)
	{
		PreApplyCallback();
	}

	TArray<bool> SleepedIslands;
	SleepedIslands.SetNum(GetConstraintGraph().NumIslands());
	TArray<TArray<TPBDRigidParticleHandle<T,d>*>> DisabledParticles;
	DisabledParticles.SetNum(GetConstraintGraph().NumIslands());
	SleepedIslands.SetNum(GetConstraintGraph().NumIslands());
	if(Dt > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_ParallelSolve);
		PhysicsParallelFor(GetConstraintGraph().NumIslands(), [&](int32 Island) {
			const TArray<TGeometryParticleHandle<T, d>*>& IslandParticles = GetConstraintGraph().GetIslandParticles(Island);

			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_ApplyConstraints);
				ApplyConstraints(Dt, Island);
			}

			if (PostApplyCallback != nullptr)
			{
				PostApplyCallback(Island);
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_UpdateVelocites);
				UpdateVelocities(Dt, Island);
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_ApplyPushOut);
				ApplyPushOut(Dt, Island);
			}

			if (PostApplyPushOutCallback != nullptr)
			{
				PostApplyPushOutCallback(Island);
			}

			for (auto Particle : IslandParticles)
			{
				// If a dynamic particle is moving slowly enough for long enough, disable it.
				// @todo(mlentine): Find a good way of not doing this when we aren't using this functionality

				// increment the disable count for the particle
				auto PBDRigid = Particle->CastToRigidParticle();
				if(PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Dynamic)
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
			SleepedIslands[Island] = GetConstraintGraph().SleepInactive(Island, PhysicsMaterials);
		});
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_DeactivateSleep);
		for (int32 Island = 0; Island < GetConstraintGraph().NumIslands(); ++Island)
		{
			if (SleepedIslands[Island])
			{
				Particles.DeactivateParticles(GetConstraintGraph().GetIslandParticles(Island));
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
TPBDRigidsEvolutionGBF<T, d>::TPBDRigidsEvolutionGBF(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations, bool InIsSingleThreaded)
	: Base(InParticles, InNumIterations, 1, InIsSingleThreaded)
	, CollisionConstraints(InParticles, Collided, PhysicsMaterials, DefaultNumPairIterations, DefaultNumPushOutPairIterations)
	, CollisionRule(CollisionConstraints, DefaultNumPushOutIterations)
	, BroadPhase(InParticles, BoundsThickness, BoundsThicknessVelocityMultiplier)
	, CollisionDetector(BroadPhase, CollisionConstraints)
	, PostIntegrateCallback(nullptr)
	, PreApplyCallback(nullptr)
	, PostApplyCallback(nullptr)
	, PostApplyPushOutCallback(nullptr)
{
	SetParticleUpdateVelocityFunction([PBDUpdateRule = TPerParticlePBDUpdateFromDeltaPosition<float, 3>(), this](const TArray<TGeometryParticleHandle<T, d>*>& ParticlesInput, const T Dt) {
		ParticlesParallelFor(ParticlesInput, [&](auto& Particle, int32 Index) {
			if (Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic)
			{
				PBDUpdateRule.Apply(Particle->CastToRigidParticle(), Dt);
			}
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

