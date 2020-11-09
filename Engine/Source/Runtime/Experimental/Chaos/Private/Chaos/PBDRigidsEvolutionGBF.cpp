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

int CollisionDisableCulledContacts = 0;
FAutoConsoleVariableRef CVarDisableCulledContacts(TEXT("p.CollisionDisableCulledContacts"), CollisionDisableCulledContacts, TEXT("Allow the PBDRigidsEvolutionGBF collision constraints to throw out contacts mid solve if they are culled."));

float BoundsThicknessVelocityMultiplier = 2.0f;	// @todo(chaos): more to FChaosSolverConfiguration
FAutoConsoleVariableRef CVarBoundsThicknessVelocityMultiplier(TEXT("p.CollisionBoundsVelocityInflation"), BoundsThicknessVelocityMultiplier, TEXT("Collision velocity inflation for speculatibe contact generation.[def:2.0]"));

float HackCCD_EnableThreshold = -1.f;
FAutoConsoleVariableRef CVarHackCCDVelThreshold(TEXT("p.Chaos.CCD.EnableThreshold"), HackCCD_EnableThreshold, TEXT("If distance moved is greater than this times the minimum object dimension, use CCD"));

float HackCCD_DepthThreshold = 0.05f;
FAutoConsoleVariableRef CVarHackCCDDepthThreshold(TEXT("p.Chaos.CCD.DepthThreshold"), HackCCD_DepthThreshold, TEXT("When returning to TOI, leave this much contact depth (as a fraction of MinBounds)"));

float SmoothedPositionLerpRate = 0.1f;
FAutoConsoleVariableRef CVarSmoothedPositionLerpRate(TEXT("p.Chaos.SmoothedPositionLerpRate"), SmoothedPositionLerpRate, TEXT("The interpolation rate for the smoothed position calculation. Used for sleeping."));


DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::AdvanceOneTimeStep"), STAT_Evolution_AdvanceOneTimeStep, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::UnclusterUnions"), STAT_Evolution_UnclusterUnions, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::Integrate"), STAT_Evolution_Integrate, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::KinematicTargets"), STAT_Evolution_KinematicTargets, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::PostIntegrateCallback"), STAT_Evolution_PostIntegrateCallback, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::PrepareConstraints"), STAT_Evolution_PrepareConstraints, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::CollisionModifierCallback"), STAT_Evolution_CollisionModifierCallback, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::UnprepareConstraints"), STAT_Evolution_UnprepareConstraints, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::ApplyConstraints"), STAT_Evolution_ApplyConstraints, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::UpdateVelocities"), STAT_Evolution_UpdateVelocites, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::ApplyPushOut"), STAT_Evolution_ApplyPushOut, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::DetectCollisions"), STAT_Evolution_DetectCollisions, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::PostDetectCollisionsCallback"), STAT_Evolution_PostDetectCollisionsCallback, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::UpdateConstraintPositionBasedState"), STAT_Evolution_UpdateConstraintPositionBasedState, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::ComputeIntermediateSpatialAcceleration"), STAT_Evolution_ComputeIntermediateSpatialAcceleration, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::CCDHack"), STAT_Evolution_CCDHack, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::CreateConstraintGraph"), STAT_Evolution_CreateConstraintGraph, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::CreateIslands"), STAT_Evolution_CreateIslands, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("TPBDRigidsEvolutionGBF::PreApplyCallback"), STAT_Evolution_PreApplyCallback, STATGROUP_Chaos);
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

template <typename Traits>
void TPBDRigidsEvolutionGBF<Traits>::Advance(const FReal Dt,const FReal MaxStepDt,const int32 MaxSteps)
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
			// E.g., for 4 steps this will be: 1/4, 1/3, 1/2, 1
			const float StepFraction = (FReal)1 / (FReal)(NumSteps - Step);
		
			UE_LOG(LogChaos, Verbose, TEXT("Advance dt = %f [%d/%d]"), StepDt, Step + 1, NumSteps);

			AdvanceOneTimeStepImpl(StepDt, StepFraction);
		}

		UnprepareTick();
	}
}

//
//
//
// BEGIN TOI
//
//
//

namespace Collisions
{
	extern bool GetTOIHack(const TPBDRigidParticleHandle<FReal, 3>* Particle0, const TGeometryParticleHandle<FReal, 3>* Particle1, FReal& OutTOI, FVec3& OutNormal, FReal& OutPhi);
}

void PostMoveToTOIHack(FReal Dt, TPBDRigidParticleHandle<FReal, 3>* Particle)
{
	// Update bounds, velocity etc
	TPerParticlePBDUpdateFromDeltaPosition<FReal, 3> VelUpdateRule;
	VelUpdateRule.Apply(Particle, Dt);
}

void MoveToTOIPairHack(FReal Dt, TPBDRigidParticleHandle<FReal, 3>* Particle1, const TGeometryParticleHandle<FReal, 3>* Particle2)
{
	FReal TOI = 0.0f;
	FReal Phi = 0.0f;
	FVec3 Normal = FVec3(0);
	if (Collisions::GetTOIHack(Particle1, Particle2, TOI, Normal, Phi))
	{
		FReal Depth = -Phi;
		FReal MinBounds = Particle1->Geometry()->BoundingBox().Extents().Min();
		FReal MaxDepth = MinBounds * HackCCD_DepthThreshold;

		//UE_LOG(LogChaos, Warning, TEXT("MoveTOIHAck: TOI %f; Depth %f"), TOI, Depth);

		if ((Depth > MaxDepth) && (TOI > KINDA_SMALL_NUMBER) && (TOI < 1.0f))
		{
			// Move the particle to just after the TOI so we still have a collision to resolve but won't get tunneling
			// The time after TOI we want is given by the ratio of max acceptable depth to depth at T = 1
			FReal ExtraT = (1.0f - TOI) * MaxDepth / Depth;
			FReal FinalT = TOI + ExtraT;
			FVec3 FinalP = FVec3::Lerp(Particle1->X(), Particle1->P(), FinalT);

			//UE_LOG(LogChaos, Warning, TEXT("MoveTOIHack: FinalTOI %f; Correction %f"), FinalT, (FinalP - Particle1->P()).Size());
		
			Particle1->SetP(FinalP);
			PostMoveToTOIHack(Dt, Particle1);
		}
	}
}


void MoveToTOIHack(FReal Dt, TTransientPBDRigidParticleHandle<FReal, 3>& Particle, const ISpatialAcceleration<TAccelerationStructureHandle<FReal, 3>, FReal, 3>* SpatialAcceleration)
{
	if (const auto AABBTree = SpatialAcceleration->template As<TAABBTree<TAccelerationStructureHandle<FReal, 3>, TAABBTreeLeafArray<TAccelerationStructureHandle<FReal, 3>, FReal>, FReal>>())
	{
		MoveToTOIHackImpl(Dt, Particle, AABBTree);
	}
	else if (const auto BV = SpatialAcceleration->template As<TBoundingVolume<TAccelerationStructureHandle<FReal, 3>, FReal, 3>>())
	{
		MoveToTOIHackImpl(Dt, Particle, BV);
	}
	else if (const auto AABBTreeBV = SpatialAcceleration->template As<TAABBTree<TAccelerationStructureHandle<FReal, 3>, TBoundingVolume<TAccelerationStructureHandle<FReal, 3>, FReal, 3>, FReal>>())
	{
		MoveToTOIHackImpl(Dt, Particle, AABBTreeBV);
	}
	else if (const auto Collection = SpatialAcceleration->template As<ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal, 3>, FReal, 3>>())
	{
		Collection->CallMoveToTOIHack(Dt, Particle);
	}
}

bool RequiresCCDHack(FReal Dt, const TTransientPBDRigidParticleHandle<FReal, 3>& Particle)
{
	if (Particle.HasBounds() && (Particle.ObjectState() == EObjectStateType::Dynamic))
	{
		FReal Dist2 = Particle.V().SizeSquared() * Dt * Dt;
		FReal MinBounds = Particle.Geometry()->BoundingBox().Extents().Min();
		FReal MaxDepth = MinBounds * HackCCD_EnableThreshold;
		if (Dist2 > MaxDepth * MaxDepth)
		{
			//UE_LOG(LogChaos, Warning, TEXT("MoveTOIHack: Enabled at DR = %f / %f"), FMath::Sqrt(Dist2), MaxDepth);

			return true;
		}
	}
	return false;
}

void CCDHack(const FReal Dt, TParticleView<TPBDRigidParticles<FReal, 3>>& ParticlesView, const ISpatialAcceleration<TAccelerationStructureHandle<FReal, 3>, FReal, 3>* SpatialAcceleration)
{
	if (HackCCD_EnableThreshold > 0)
	{
		for (auto& Particle : ParticlesView)
		{
			if (RequiresCCDHack(Dt, Particle))
			{
				MoveToTOIHack(Dt, Particle, SpatialAcceleration);
			}
		}
	}
}

//
//
//
// END TOI
//
//
//

template <typename Traits>
void TPBDRigidsEvolutionGBF<Traits>::AdvanceOneTimeStep(const FReal Dt,const FReal StepFraction)
{
	PrepareTick();

	AdvanceOneTimeStepImpl(Dt, StepFraction);

	UnprepareTick();
}

int32 DrawAwake = 0;
FAutoConsoleVariableRef CVarDrawAwake(TEXT("p.chaos.DebugDrawAwake"),DrawAwake,TEXT("Draw particles that are awake"));

template <typename Traits>
void TPBDRigidsEvolutionGBF<Traits>::AdvanceOneTimeStepImpl(const FReal Dt,const FReal StepFraction)
{
	SCOPE_CYCLE_COUNTER(STAT_Evolution_AdvanceOneTimeStep);

	Particles.ClearTransientDirty();

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
		ApplyKinematicTargets(Dt, StepFraction);
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
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CCDHack);
		CCDHack(Dt, Particles.GetActiveParticlesView(), InternalAcceleration);
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

	if (CollisionModifierCallback)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CollisionModifierCallback);
		CollisionConstraints.ApplyCollisionModifier(CollisionModifierCallback);
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

	TArray<bool> SleepedIslands;
	SleepedIslands.SetNum(GetConstraintGraph().NumIslands());
	TArray<TArray<TPBDRigidParticleHandle<FReal, 3>*>> DisabledParticles;
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
			
			const TArray<TGeometryParticleHandle<FReal, 3>*>& IslandParticles = GetConstraintGraph().GetIslandParticles(Island);

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
						const TAABB<FReal,3> LocalBounds = Geom->BoundingBox();
						FDebugDrawQueue::GetInstance().DrawDebugBox(Active.X(),LocalBounds.Extents()*0.5f,Active.R(),IslandColors[ColorIdx],false,-1.f,0,0.f);
					}
				}
			}
		}
	}
#endif
#endif
}

template <typename Traits>
TPBDRigidsEvolutionGBF<Traits>::TPBDRigidsEvolutionGBF(TPBDRigidsSOAs<FReal,3>& InParticles,THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials,bool InIsSingleThreaded)
	: Base(InParticles, SolverPhysicsMaterials, DefaultNumIterations, DefaultNumPushOutIterations, InIsSingleThreaded)
	, Clustering(*this, Particles.GetClusteredParticles())
	, CollisionConstraints(InParticles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, DefaultNumCollisionPairIterations, DefaultNumCollisionPushOutPairIterations, DefaultCollisionCullDistance)
	, CollisionRule(CollisionConstraints)
	, BroadPhase(InParticles, DefaultCollisionCullDistance, BoundsThicknessVelocityMultiplier, DefaultCollisionCullDistance)
	, NarrowPhase()
	, CollisionDetector(BroadPhase, NarrowPhase, CollisionConstraints)
	, PostIntegrateCallback(nullptr)
	, PreApplyCallback(nullptr)
	, PostApplyCallback(nullptr)
	, PostApplyPushOutCallback(nullptr)
	, CurrentStepResimCacheImp(nullptr)
{
	CollisionConstraints.SetCanDisableContacts(!!CollisionDisableCulledContacts);

	SetParticleUpdateVelocityFunction([PBDUpdateRule = TPerParticlePBDUpdateFromDeltaPosition<float, 3>(), this](const TArray<TGeometryParticleHandle<FReal, 3>*>& ParticlesInput, const FReal Dt) {
		ParticlesParallelFor(ParticlesInput, [&](auto& Particle, int32 Index) {
			if (Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic)
			{
				PBDUpdateRule.Apply(Particle->CastToRigidParticle(), Dt);
			}
		});
	});

	SetParticleUpdatePositionFunction([this](const TParticleView<TPBDRigidParticles<FReal, 3>>& ParticlesInput, const FReal Dt)
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
		});
	});

	AddForceFunction([this](TTransientPBDRigidParticleHandle<FReal, 3>& HandleIn, const FReal Dt)
	{
		GravityForces.Apply(HandleIn, Dt);
	});

	AddConstraintRule(&CollisionRule);

	SetInternalParticleInitilizationFunction([](const TGeometryParticleHandle<float, 3>*, const TGeometryParticleHandle<float, 3>*) {});
	NarrowPhase.GetContext().bFilteringEnabled = true;
	NarrowPhase.GetContext().bDeferUpdate = true;
	NarrowPhase.GetContext().bAllowManifolds = false;
	NarrowPhase.GetContext().bUseIncrementalManifold = false;
	NarrowPhase.GetContext().bUseOneShotManifolds = false;
}

template <typename Traits>
void TPBDRigidsEvolutionGBF<Traits>::Serialize(FChaosArchive& Ar)
{
	Base::Serialize(Ar);
}

template <typename Traits>
TUniquePtr<IResimCacheBase> TPBDRigidsEvolutionGBF<Traits>::CreateExternalResimCache() const
{
	check(Traits::IsRewindable());
	return TUniquePtr<IResimCacheBase>(new FEvolutionResimCache());
}

template <typename Traits>
void TPBDRigidsEvolutionGBF<Traits>::SetCurrentStepResimCache(IResimCacheBase* InCurrentStepResimCache)
{
	check(Traits::IsRewindable());
	CurrentStepResimCacheImp = static_cast<FEvolutionResimCache*>(InCurrentStepResimCache);
}

#define EVOLUTION_TRAIT(Trait) template class TPBDRigidsEvolutionGBF<Trait>;
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
}

