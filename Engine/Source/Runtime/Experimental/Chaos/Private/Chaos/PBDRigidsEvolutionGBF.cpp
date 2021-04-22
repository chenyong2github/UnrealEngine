// Copyright Epic Games, Inc. All Rights Reserved.
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
#include "Chaos/EvolutionResimCache.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/DebugDrawQueue.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
#if !UE_BUILD_SHIPPING
	CHAOS_API bool bPendingHierarchyDump = false;
#else
	const bool bPendingHierarchyDump = false;
#endif

FRealSingle HackMaxAngularVelocity = 1000.f;
FAutoConsoleVariableRef CVarHackMaxAngularVelocity(TEXT("p.HackMaxAngularVelocity"), HackMaxAngularVelocity, TEXT("Max cap on angular velocity: rad/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));

FRealSingle HackMaxVelocity = -1.f;
FAutoConsoleVariableRef CVarHackMaxVelocity(TEXT("p.HackMaxVelocity2"), HackMaxVelocity, TEXT("Max cap on velocity: cm/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));


FRealSingle HackLinearDrag = 0.f;
FAutoConsoleVariableRef CVarHackLinearDrag(TEXT("p.HackLinearDrag2"), HackLinearDrag, TEXT("Linear drag used to slow down objects. This is a hack and should not be relied on as a feature."));

FRealSingle HackAngularDrag = 0.f;
FAutoConsoleVariableRef CVarHackAngularDrag(TEXT("p.HackAngularDrag2"), HackAngularDrag, TEXT("Angular drag used to slow down objects. This is a hack and should not be relied on as a feature."));

int DisableThreshold = 5;
FAutoConsoleVariableRef CVarDisableThreshold(TEXT("p.DisableThreshold2"), DisableThreshold, TEXT("Disable threshold frames to transition to sleeping"));

int CollisionDisableCulledContacts = 0;
FAutoConsoleVariableRef CVarDisableCulledContacts(TEXT("p.CollisionDisableCulledContacts"), CollisionDisableCulledContacts, TEXT("Allow the PBDRigidsEvolutionGBF collision constraints to throw out contacts mid solve if they are culled."));

FRealSingle BoundsThicknessVelocityMultiplier = 2.0f;	// @todo(chaos): more to FChaosSolverConfiguration
FAutoConsoleVariableRef CVarBoundsThicknessVelocityMultiplier(TEXT("p.CollisionBoundsVelocityInflation"), BoundsThicknessVelocityMultiplier, TEXT("Collision velocity inflation for speculatibe contact generation.[def:2.0]"));

FRealSingle SmoothedPositionLerpRate = 0.1f;
FAutoConsoleVariableRef CVarSmoothedPositionLerpRate(TEXT("p.Chaos.SmoothedPositionLerpRate"), SmoothedPositionLerpRate, TEXT("The interpolation rate for the smoothed position calculation. Used for sleeping."));


DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::AdvanceOneTimeStep"), STAT_Evolution_AdvanceOneTimeStep, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::UnclusterUnions"), STAT_Evolution_UnclusterUnions, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::Integrate"), STAT_Evolution_Integrate, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::KinematicTargets"), STAT_Evolution_KinematicTargets, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::PostIntegrateCallback"), STAT_Evolution_PostIntegrateCallback, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::PrepareConstraints"), STAT_Evolution_PrepareConstraints, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::CollisionModifierCallback"), STAT_Evolution_CollisionModifierCallback, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::UnprepareConstraints"), STAT_Evolution_UnprepareConstraints, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ApplyConstraints"), STAT_Evolution_ApplyConstraints, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::UpdateVelocities"), STAT_Evolution_UpdateVelocites, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ApplyPushOut"), STAT_Evolution_ApplyPushOut, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::DetectCollisions"), STAT_Evolution_DetectCollisions, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::PostDetectCollisionsCallback"), STAT_Evolution_PostDetectCollisionsCallback, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::UpdateConstraintPositionBasedState"), STAT_Evolution_UpdateConstraintPositionBasedState, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ComputeIntermediateSpatialAcceleration"), STAT_Evolution_ComputeIntermediateSpatialAcceleration, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::CreateConstraintGraph"), STAT_Evolution_CreateConstraintGraph, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::CreateIslands"), STAT_Evolution_CreateIslands, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::PreApplyCallback"), STAT_Evolution_PreApplyCallback, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ParallelSolve"), STAT_Evolution_ParallelSolve, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::DeactivateSleep"), STAT_Evolution_DeactivateSleep, STATGROUP_Chaos);

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

void FPBDRigidsEvolutionGBF::Advance(const FReal Dt,const FReal MaxStepDt,const int32 MaxSteps)
{
	// Determine how many steps we would like to take
	int32 NumSteps = FMath::CeilToInt(Dt / MaxStepDt);
	if (NumSteps > 0)
	{
		PrepareTick();

		// Determine the step time
		const FReal StepDt = Dt / (FReal)NumSteps;

		// Limit the number of steps
		// NOTE: This is after step time calculation so simulation will appear to slow down for large Dt
		// but that is preferable to blowing up from a large timestep.
		NumSteps = FMath::Clamp(NumSteps, 1, MaxSteps);

		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			// StepFraction: how much of the remaining time this step represents, used to interpolate kinematic targets
			// E.g., for 4 steps this will be: 1/4, 1/2, 3/4, 1
			const FReal StepFraction = (FReal)(Step + 1) / (NumSteps);
		
			UE_LOG(LogChaos, Verbose, TEXT("Advance dt = %f [%d/%d]"), StepDt, Step + 1, NumSteps);

			AdvanceOneTimeStepImpl(StepDt, FSubStepInfo{ StepFraction, Step, MaxSteps });
		}

		UnprepareTick();
	}
}


void FPBDRigidsEvolutionGBF::AdvanceOneTimeStep(const FReal Dt,const FSubStepInfo& SubStepInfo)
{
	PrepareTick();

	AdvanceOneTimeStepImpl(Dt, SubStepInfo);

	UnprepareTick();
}

int32 DrawAwake = 0;
FAutoConsoleVariableRef CVarDrawAwake(TEXT("p.chaos.DebugDrawAwake"),DrawAwake,TEXT("Draw particles that are awake"));

void FPBDRigidsEvolutionGBF::AdvanceOneTimeStepImpl(const FReal Dt,const FSubStepInfo& SubStepInfo)
{
	SCOPE_CYCLE_COUNTER(STAT_Evolution_AdvanceOneTimeStep);

	//for now we never allow solver to schedule more than two tasks back to back
	//this means we only need to keep indices alive for one additional frame
	//the code that pushes indices to pending happens after this check which ensures we won't delete until next frame
	//if sub-stepping is used, the index free will only happen on the first sub-step. However, since we are sub-stepping we would end up releasing half way through interval
	//by checking the step and only releasing on step 0, we ensure the entire interval will see the indices
	if(SubStepInfo.Step == 0)
	{
		Base::ReleasePendingIndices();
	}

#if !UE_BUILD_SHIPPING
	if (SerializeEvolution)
	{
		SerializeToDisk(*this);
	}
#endif

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_UnclusterUnions);
		Clustering.UnionClusterGroups();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_Integrate);
		Integrate(Particles.GetActiveParticlesView(), Dt);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_KinematicTargets);
		ApplyKinematicTargets(Dt, SubStepInfo.PseudoFraction);
	}

	if (PostIntegrateCallback != nullptr)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_PostIntegrateCallback);
		PostIntegrateCallback();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_UpdateConstraintPositionBasedState);
		UpdateConstraintPositionBasedState(Dt);
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_ComputeIntermediateSpatialAcceleration);
		Base::ComputeIntermediateSpatialAcceleration();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_DetectCollisions);
		CollisionDetector.GetBroadPhase().SetSpatialAcceleration(InternalAcceleration);

		CollisionStats::FStatData StatData(bPendingHierarchyDump);

		CollisionDetector.DetectCollisionsWithStats(Dt, StatData, GetCurrentStepResimCache());

		CHAOS_COLLISION_STAT(StatData.Print());
	}

	if (PostDetectCollisionsCallback != nullptr)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_PostDetectCollisionsCallback);
		PostDetectCollisionsCallback();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_PrepareConstraints);
		PrepareIteration(Dt);
	}

	if(CollisionModifiers)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CollisionModifierCallback);
		CollisionConstraints.ApplyCollisionModifier(*CollisionModifiers);
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
		SCOPE_CYCLE_COUNTER(STAT_Evolution_PreApplyCallback);
		PreApplyCallback();
	}

	CollisionConstraints.SetGravity(GetGravityForces().GetAcceleration());

	TArray<bool> SleepedIslands;
	SleepedIslands.SetNum(GetConstraintGraph().NumIslands());
	TArray<TArray<FPBDRigidParticleHandle*>> DisabledParticles;
	DisabledParticles.SetNum(GetConstraintGraph().NumIslands());
	SleepedIslands.SetNum(GetConstraintGraph().NumIslands());
	if(Dt > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_ParallelSolve);
		PhysicsParallelFor(GetConstraintGraph().NumIslands(), [&](int32 Island) {
			
			if(auto* ResimCache = GetCurrentStepResimCache())
			{
				if(ResimCache->IsResimming() && GetConstraintGraph().IslandNeedsResim(Island) == false)
				{
					return;
				}
			}
			
			const TArray<FGeometryParticleHandle*>& IslandParticles = GetConstraintGraph().GetIslandParticles(Island);

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
			SleepedIslands[Island] = GetConstraintGraph().SleepInactive(Island, PhysicsMaterials, SolverPhysicsMaterials);
		});
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_UnprepareConstraints);
		UnprepareIteration(Dt);
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
				DisableParticle(Particle);
			}
		}
	}

	Clustering.AdvanceClustering(Dt, GetCollisionConstraints());

	if(CaptureRewindData)
	{
		CaptureRewindData(Particles.GetDirtyParticlesView());
	}

	ParticleUpdatePosition(Particles.GetDirtyParticlesView(), Dt);

#if !UE_BUILD_SHIPPING
	if(SerializeEvolution)
	{
		SerializeToDisk(*this);
	}

#if CHAOS_DEBUG_DRAW
	if(FDebugDrawQueue::IsDebugDrawingEnabled())
	{
		if(!!DrawAwake)
		{
			static const FColor IslandColors[] = {FColor::Green,FColor::Red,FColor::Yellow,
				FColor::Blue,FColor::Orange,FColor::Black,FColor::Cyan,
				FColor::Magenta,FColor::Purple,FColor::Turquoise};

			static const int32 NumColors = sizeof(IslandColors) / sizeof(IslandColors[0]);
			
			for(const auto& Active : Particles.GetActiveParticlesView())
			{
				if(const auto* Geom = Active.Geometry().Get())
				{
					if(Geom->HasBoundingBox())
					{
						const int32 Island = Active.Island();
						ensure(Island >= 0);
						const int32 ColorIdx = Island % NumColors;
						const FAABB3 LocalBounds = Geom->BoundingBox();
						FDebugDrawQueue::GetInstance().DrawDebugBox(Active.X(),LocalBounds.Extents()*0.5f,Active.R(),IslandColors[ColorIdx],false,-1.f,0,0.f);
					}
				}
			}
		}
	}
#endif
#endif
}

FPBDRigidsEvolutionGBF::FPBDRigidsEvolutionGBF(FPBDRigidsSOAs& InParticles,THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials, const TArray<ISimCallbackObject*>* InCollisionModifiers, bool InIsSingleThreaded)
	: Base(InParticles, SolverPhysicsMaterials, DefaultNumIterations, DefaultNumPushOutIterations, InIsSingleThreaded)
	, Clustering(*this, Particles.GetClusteredParticles())
	, CollisionConstraints(InParticles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, DefaultNumCollisionPairIterations, DefaultNumCollisionPushOutPairIterations, DefaultRestitutionThreshold)
	, CollisionRule(CollisionConstraints)
	, BroadPhase(InParticles, DefaultCollisionCullDistance, BoundsThicknessVelocityMultiplier, DefaultCollisionCullDistance)
	, NarrowPhase()
	, CollisionDetector(BroadPhase, NarrowPhase, CollisionConstraints)
	, PostIntegrateCallback(nullptr)
	, PreApplyCallback(nullptr)
	, PostApplyCallback(nullptr)
	, PostApplyPushOutCallback(nullptr)
	, CurrentStepResimCacheImp(nullptr)
	, CollisionModifiers(InCollisionModifiers)
{
	CollisionConstraints.SetCanDisableContacts(!!CollisionDisableCulledContacts);

	SetParticleUpdateVelocityFunction([PBDUpdateRule = FPerParticlePBDUpdateFromDeltaPosition(), this](const TArray<FGeometryParticleHandle*>& ParticlesInput, const FReal Dt) {
		ParticlesParallelFor(ParticlesInput, [&](auto& Particle, int32 Index) {
			if (Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic)
			{
				PBDUpdateRule.Apply(Particle->CastToRigidParticle(), Dt);
			}
		});
	});

	SetParticleUpdatePositionFunction([this](const TParticleView<FPBDRigidParticles>& ParticlesInput, const FReal Dt)
	{
		ParticlesInput.ParallelFor([&](auto& Particle, int32 Index)
		{
			if (Dt > SMALL_NUMBER)
			{
				const FReal SmoothRate = FMath::Clamp(SmoothedPositionLerpRate, 0.0f, 1.0f);
				const FVec3 VImp = FVec3::CalculateVelocity(Particle.X(), Particle.P(), Dt);
				const FVec3 WImp = FRotation3::CalculateAngularVelocity(Particle.R(), Particle.Q(), Dt);
				Particle.VSmooth() = FMath::Lerp(Particle.VSmooth(), VImp, SmoothRate);
				Particle.WSmooth() = FMath::Lerp(Particle.WSmooth(), WImp, SmoothRate);
			}

			Particle.X() = Particle.P();
			Particle.R() = Particle.Q();

			//TODO: rename this function since it's not just updating position
			Particle.SetPreObjectStateLowLevel(Particle.ObjectState());
		});
	});

	AddForceFunction([this](TTransientPBDRigidParticleHandle<FReal, 3>& HandleIn, const FReal Dt)
	{
		GravityForces.Apply(HandleIn, Dt);
	});

	AddConstraintRule(&CollisionRule);

	SetInternalParticleInitilizationFunction([](const FGeometryParticleHandle*, const FGeometryParticleHandle*) {});
	NarrowPhase.GetContext().bFilteringEnabled = true;
	NarrowPhase.GetContext().bDeferUpdate = true;
	NarrowPhase.GetContext().bAllowManifolds = false;
}

void FPBDRigidsEvolutionGBF::Serialize(FChaosArchive& Ar)
{
	Base::Serialize(Ar);
}

TUniquePtr<IResimCacheBase> FPBDRigidsEvolutionGBF::CreateExternalResimCache() const
{
	return TUniquePtr<IResimCacheBase>(new FEvolutionResimCache());
}

void FPBDRigidsEvolutionGBF::SetCurrentStepResimCache(IResimCacheBase* InCurrentStepResimCache)
{
	CurrentStepResimCacheImp = static_cast<FEvolutionResimCache*>(InCurrentStepResimCache);
}

}

