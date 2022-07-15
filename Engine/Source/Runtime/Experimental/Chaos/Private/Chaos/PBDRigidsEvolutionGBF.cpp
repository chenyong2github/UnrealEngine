// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Defines.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/MassConditioning.h"
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

	namespace CVars
	{
		FRealSingle HackMaxAngularVelocity = 1000.f;
		FAutoConsoleVariableRef CVarHackMaxAngularVelocity(TEXT("p.HackMaxAngularVelocity"), HackMaxAngularVelocity, TEXT("Max cap on angular velocity: rad/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));

		FRealSingle HackMaxVelocity = -1.f;
		FAutoConsoleVariableRef CVarHackMaxVelocity(TEXT("p.HackMaxVelocity2"), HackMaxVelocity, TEXT("Max cap on velocity: cm/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));


		int DisableThreshold = 5;
		FAutoConsoleVariableRef CVarDisableThreshold(TEXT("p.DisableThreshold2"), DisableThreshold, TEXT("Disable threshold frames to transition to sleeping"));

		int CollisionDisableCulledContacts = 0;
		FAutoConsoleVariableRef CVarDisableCulledContacts(TEXT("p.CollisionDisableCulledContacts"), CollisionDisableCulledContacts, TEXT("Allow the PBDRigidsEvolutionGBF collision constraints to throw out contacts mid solve if they are culled."));

		FRealSingle BoundsThicknessVelocityMultiplier = 0.0f;
		FAutoConsoleVariableRef CVarBoundsThicknessVelocityMultiplier(TEXT("p.CollisionBoundsVelocityInflation"), BoundsThicknessVelocityMultiplier, TEXT("Collision velocity inflation for speculative contact generation.[def:0.0]"));

		FRealSingle SmoothedPositionLerpRate = 0.1f;
		FAutoConsoleVariableRef CVarSmoothedPositionLerpRate(TEXT("p.Chaos.SmoothedPositionLerpRate"), SmoothedPositionLerpRate, TEXT("The interpolation rate for the smoothed position calculation. Used for sleeping."));

		int DisableParticleUpdateVelocityParallelFor = 0;
		FAutoConsoleVariableRef CVarDisableParticleUpdateVelocityParallelFor(TEXT("p.DisableParticleUpdateVelocityParallelFor"), DisableParticleUpdateVelocityParallelFor, TEXT("Disable Particle Update Velocity ParallelFor and run the update on a single thread"));

		// NOTE: If we have mrope than 1 CCD iteration (ChaosCollisionCCDConstraintMaxProcessCount), the tight bounding box will cause us to miss secondary CCD collisions if the first one(s) result in a change in direction
		bool bChaosCollisionCCDUseTightBoundingBox = true;
		FAutoConsoleVariableRef  CVarChaosCollisionCCDUseTightBoundingBox(TEXT("p.Chaos.Collision.CCD.UseTightBoundingBox"), bChaosCollisionCCDUseTightBoundingBox , TEXT(""));

		int32 ChaosSolverCollisionPriority = 0;
		FAutoConsoleVariableRef CVarChaosSolverCollisionPriority(TEXT("p.Chaos.Solver.Collision.Priority"), ChaosSolverCollisionPriority, TEXT("Set constraint priority. Larger values are evaluated later [def:0]"));

		int32 ChaosSolverJointPriority = 0;
		FAutoConsoleVariableRef CVarChaosSolverJointPriority(TEXT("p.Chaos.Solver.Joint.Priority"), ChaosSolverJointPriority, TEXT("Set constraint priority. Larger values are evaluated later [def:0]"));

		int32 ChaosSolverSuspensionPriority = 0;
		FAutoConsoleVariableRef CVarChaosSolverSuspensionPriority(TEXT("p.Chaos.Solver.Suspension.Priority"), ChaosSolverSuspensionPriority, TEXT("Set constraint priority. Larger values are evaluated later [def:0]"));

		bool DoTransferJointConstraintCollisions = true;
		FAutoConsoleVariableRef CVarDoTransferJointConstraintCollisions(TEXT("p.Chaos.Solver.Joint.TransferCollisions"), DoTransferJointConstraintCollisions, TEXT("Allows joints to apply collisions to the parent from the child when the Joints TransferCollisionScale is not 0 [def:true]"));

		int32 TransferCollisionsLimit = INT_MAX;
		FAutoConsoleVariableRef CVarTransferCollisionsMultiply(TEXT("p.Chaos.Solver.Joint.TransferCollisionsLimit"), TransferCollisionsLimit, TEXT("Maximum number of constraints that are allowed to transfer to the parent. Lowering this will improve performance but reduce accuracy. [def:INT_MAX]"));

		FRealSingle TransferCollisionsKinematicScale = 1.0f;
		FAutoConsoleVariableRef CVarTransferCollisionsKinematicScale(TEXT("p.Chaos.Solver.Joint.TransferCollisionsKinematicScale"), TransferCollisionsKinematicScale, TEXT("Scale to apply to collision transfers between kinematic bodies [def:1.0]"));

		FRealSingle TransferCollisionsStiffnessClamp = 1.0f;
		FAutoConsoleVariableRef CVarTransferCollisionsStiffnessClamp(TEXT("p.Chaos.Solver.Joint.TransferCollisionsStiffnessClamp"), TransferCollisionsStiffnessClamp, TEXT("Clamp of maximum value of the stiffness clamp[def:1.0]"));

		bool TransferCollisionsDebugTestAgainstMaxClamp = false;
		FAutoConsoleVariableRef CVarTransferCollisionsDebugTestAgainstMaxClamp(TEXT("p.Chaos.Solver.Joint.TransferCollisionsDebugTestAgainstMaxClamp"), TransferCollisionsDebugTestAgainstMaxClamp, TEXT("Force all joint collision constraint settings to max clamp value to validate stability [def:false]"));

		bool DoFinalProbeNarrowPhase = true;
		FAutoConsoleVariableRef CVarDoFinalProbeNarrowPhase(TEXT("p.Chaos.Solver.DoFinalProbeNarrowPhase"), DoFinalProbeNarrowPhase, TEXT(""));

		// Enable inertia conditioning for joint and collisions stabilization for small and thin objects
		bool bChaosSolverInertiaConditioningEnabled = true;
		FAutoConsoleVariableRef  CVarChaosSolverInertiaConditioningEnabled(TEXT("p.Chaos.Solver.InertiaConditioning.Enabled"), bChaosSolverInertiaConditioningEnabled, TEXT("Enable/Disable constraint stabilization through inertia conditioning"));

		// The largest joint error we expect to resolve in a moderately stable way
		FRealSingle ChaosSolverInertiaConditioningDistance = 20;
		FAutoConsoleVariableRef  CVarChaosSolverInertiaConditioningDistance(TEXT("p.Chaos.Solver.InertiaConditioning.Distance"), ChaosSolverInertiaConditioningDistance, TEXT("An input to inertia conditioning system. The joint distance error which needs to be stable (generate a low rotation)."));

		// The ratio of joint error correction that comes from particle rotation versus translation
		FRealSingle ChaosSolverInertiaConditioningRotationRatio = 2;
		FAutoConsoleVariableRef  CVarChaosSolverInertiaConditioningRotationRatio(TEXT("p.Chaos.Solver.InertiaConditioning.RotationRatio"), ChaosSolverInertiaConditioningRotationRatio, TEXT("An input to inertia conditioning system. The maximum ratio of joint correction from rotation versus translation"));

		// If > 1, limits the inverse inertia components to be mo more than this multiple of the smallest component. Makes the objects more round. Set to 0 to disable.
		FRealSingle ChaosSolverMaxInvInertiaComponentRatio = 0;
		FAutoConsoleVariableRef  CVarChaosSolverInertiaConditioningMaxInvInertiaComponentRatio(TEXT("p.Chaos.Solver.InertiaConditioning.MaxInvInertiaComponentRatio"), ChaosSolverMaxInvInertiaComponentRatio, TEXT("An input to inertia conditioning system. The largest inertia component must be at least least multiple of the smallest component"));

		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::AdvanceOneTimeStep"), STAT_Evolution_AdvanceOneTimeStep, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::UnclusterUnions"), STAT_Evolution_UnclusterUnions, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::Integrate"), STAT_Evolution_Integrate, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::KinematicTargets"), STAT_Evolution_KinematicTargets, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::PostIntegrateCallback"), STAT_Evolution_PostIntegrateCallback, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::CollisionModifierCallback"), STAT_Evolution_CollisionModifierCallback, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::CCD"), STAT_Evolution_CCD, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::GraphColor"), STAT_Evolution_GraphColor, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::Gather"), STAT_Evolution_Gather, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::Scatter"), STAT_Evolution_Scatter, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ApplyConstraintsPhase1"), STAT_Evolution_ApplyConstraintsPhase1, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::UpdateVelocities"), STAT_Evolution_UpdateVelocites, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ApplyConstraintsPhase2"), STAT_Evolution_ApplyConstraintsPhase2, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ApplyConstraintsPhase3"), STAT_Evolution_ApplyConstraintsPhase3, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::DetectCollisions"), STAT_Evolution_DetectCollisions, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::TransferJointCollisions"), STAT_Evolution_TransferJointCollisions, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::PostDetectCollisionsCallback"), STAT_Evolution_PostDetectCollisionsCallback, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::UpdateConstraintPositionBasedState"), STAT_Evolution_UpdateConstraintPositionBasedState, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ComputeIntermediateSpatialAcceleration"), STAT_Evolution_ComputeIntermediateSpatialAcceleration, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::CreateConstraintGraph"), STAT_Evolution_CreateConstraintGraph, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::CreateIslands"), STAT_Evolution_CreateIslands, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::PruneCollisions"), STAT_Evolution_PruneCollisions, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::UpdateAccelerationStructures"), STAT_Evolution_UpdateAccelerationStructures, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::AddSleepingContacts"), STAT_Evolution_AddSleepingContacts, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::PreApplyCallback"), STAT_Evolution_PreApplyCallback, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::ParallelSolve"), STAT_Evolution_ParallelSolve, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::SaveParticlePostSolve"), STAT_Evolution_SavePostSolve, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::DeactivateSleep"), STAT_Evolution_DeactivateSleep, STATGROUP_Chaos);
		DECLARE_CYCLE_STAT(TEXT("FPBDRigidsEvolutionGBF::InertiaConditioning"), STAT_Evolution_InertiaConditioning, STATGROUP_Chaos);

		int32 SerializeEvolution = 0;
		FAutoConsoleVariableRef CVarSerializeEvolution(TEXT("p.SerializeEvolution"), SerializeEvolution, TEXT(""));

		bool bChaos_CollisionStore_Enabled = true;
		FAutoConsoleVariableRef CVarCollisionStoreEnabled(TEXT("p.Chaos.CollisionStore.Enabled"), bChaos_CollisionStore_Enabled, TEXT(""));

		// Put the solver into a mode where it reset particles to their initial positions each frame.
		// This is used to test collision detection and - it will be removed
		// @chaos(todo): remove this when no longer needed
		bool bChaos_Solver_TestMode  = false;
		FAutoConsoleVariableRef CVarChaosSolverTestMode(TEXT("p.Chaos.Solver.TestMode"), bChaos_Solver_TestMode, TEXT(""));
	}

	using namespace CVars;

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
	int32 NumSteps = FMath::CeilToInt32(Dt / MaxStepDt);
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
			const FReal StepFraction = (FReal)(Step + 1) / (FReal)(NumSteps);
		
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

void FPBDRigidsEvolutionGBF::ReloadParticlesCache(const int32 Island)
{
	if(!GetConstraintGraph().GetSolverIsland(Island)->IsSleeping())
	{
		FEvolutionResimCache* ResimCache = GetCurrentStepResimCache();
		if (ResimCache && ResimCache->IsResimming() && GetConstraintGraph().IslandNeedsResim(Island) == false)
		{
			for (auto Particle : GetConstraintGraph().GetIslandParticles(Island))
			{
				if (auto Rigid = Particle->CastToRigidParticle())
				{
					ResimCache->ReloadParticlePostSolve(*Rigid);
				}
			}
			GetConstraintGraph().GetSolverIsland(Island)->SetIsUsingCache(true);
		}
		GetConstraintGraph().GetSolverIsland(Island)->SetIsUsingCache(false);
	}
}

void FPBDRigidsEvolutionGBF::BuildDisabledParticles(const int32 Island, TArray<TArray<FPBDRigidParticleHandle*>>& DisabledParticles, TArray<bool>& SleepedIslands)
{
	for (auto Particle : GetConstraintGraph().GetIslandParticles(Island))
	{
		// If a dynamic particle is moving slowly enough for long enough, disable it.
		// @todo(mlentine): Find a good way of not doing this when we aren't using this functionality

		// increment the disable count for the particle
		auto PBDRigid = Particle->CastToRigidParticle();
		if (PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Dynamic)
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
				DisabledParticles[Island].Add(PBDRigid);
			}

			if (!(ensure(!FMath::IsNaN(PBDRigid->P()[0])) && ensure(!FMath::IsNaN(PBDRigid->P()[1])) && ensure(!FMath::IsNaN(PBDRigid->P()[2]))))
			{
				DisabledParticles[Island].Add(PBDRigid);
			}
		}
	}

	// Turn off if not moving
	SleepedIslands[Island] = GetConstraintGraph().SleepInactive(Island, PhysicsMaterials, SolverPhysicsMaterials);
}

int32 DrawAwake = 0;
FAutoConsoleVariableRef CVarDrawAwake(TEXT("p.chaos.DebugDrawAwake"),DrawAwake,TEXT("Draw particles that are awake"));

#define CSV_CUSTOM_STAT_PER_ISLAND_HELPER(Stat) CSV_CUSTOM_STAT(PhysicsVerbose, Stat, Stats[Stat] * 1000.0, ECsvCustomStatOp::Set);

//Using aligned to avoid false sharing during perf capture
struct alignas(64) FPerIslandStats
{
#if CSV_PROFILER

	enum EPerIslandStat
	{
		/** Any stat added to this enum must be added to ReportStats. Can use inl if it's a problem, but seems fine to just copy paste here*/
		PerIslandSolve_ReloadCacheTotalSerialized = 0,
		PerIslandSolve_GatherTotalSerialized,
		PerIslandSolve_ApplyTotalSerialized,
		PerIslandSolve_UpdateVelocitiesTotalSerialized,
		PerIslandSolve_ApplyPushOutTotalSerialized,
		PerIslandSolve_ApplyProjectionTotalSerialized,
		PerIslandSolve_ScatterTotalSerialized,
		NumStats
	};

	void ReportStats() const
	{
		CSV_CUSTOM_STAT_PER_ISLAND_HELPER(PerIslandSolve_ReloadCacheTotalSerialized);
		CSV_CUSTOM_STAT_PER_ISLAND_HELPER(PerIslandSolve_GatherTotalSerialized);
		CSV_CUSTOM_STAT_PER_ISLAND_HELPER(PerIslandSolve_ApplyTotalSerialized);
		CSV_CUSTOM_STAT_PER_ISLAND_HELPER(PerIslandSolve_UpdateVelocitiesTotalSerialized);
		CSV_CUSTOM_STAT_PER_ISLAND_HELPER(PerIslandSolve_ApplyPushOutTotalSerialized);
		CSV_CUSTOM_STAT_PER_ISLAND_HELPER(PerIslandSolve_ApplyProjectionTotalSerialized);
		CSV_CUSTOM_STAT_PER_ISLAND_HELPER(PerIslandSolve_ScatterTotalSerialized);
	}

	FPerIslandStats()
	{
		for (double& Stat : Stats) { Stat = 0.0; }
	}

	static FPerIslandStats Flatten(const TArray<FPerIslandStats>& PerIslandStats)
	{
		FPerIslandStats Flat;
		for(const FPerIslandStats& IslandStats : PerIslandStats)
		{
			for(int32 Stat = 0; Stat < NumStats; ++Stat)
			{
				Flat.Stats[Stat] += IslandStats.Stats[Stat];
			}
		}

		return Flat;
	}

	double Stats[EPerIslandStat::NumStats];
#else
	void ReportStats() const{}
#endif

};

#if CSV_PROFILER
#define CSV_SCOPED_PER_ISLAND_TIMING_STAT(Stat) FScopedDurationTimer Timer_##Stat(PerIslandStats[GroupIndex].Stats[FPerIslandStats::Stat])
#else
#define CSV_SCOPED_PER_ISLAND_TIMING_STAT(Stat)
#endif
void FPBDRigidsEvolutionGBF::AdvanceOneTimeStepImpl(const FReal Dt, const FSubStepInfo& SubStepInfo)
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
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_Integrate);
		Integrate(Particles.GetActiveParticlesView(), Dt);
	}

	if (bChaos_Solver_TestMode)
	{
		TestModeResetParticles();
		TestModeResetCollisions();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_KinematicTargets);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_KinematicTargets);
		ApplyKinematicTargets(Dt, SubStepInfo.PseudoFraction);
	}

	if (PostIntegrateCallback != nullptr)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_PostIntegrateCallback);
		PostIntegrateCallback();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_UpdateConstraintPositionBasedState);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_UpdateConstraintPositionBasedState);
		UpdateConstraintPositionBasedState(Dt);
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_ComputeIntermediateSpatialAcceleration);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_ComputeIntermediateSpatialAcceleration);
		Base::ComputeIntermediateSpatialAcceleration();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_DetectCollisions);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_DetectCollisions);
		CollisionDetector.GetBroadPhase().SetSpatialAcceleration(InternalAcceleration);

		CollisionDetector.DetectCollisions(Dt, GetCurrentStepResimCache());
	}

	if (PostDetectCollisionsCallback != nullptr)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_PostDetectCollisionsCallback);
		PostDetectCollisionsCallback();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_TransferJointCollisions);
		TransferJointConstraintCollisions();
	}

	if(CollisionModifiers)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CollisionModifierCallback);
		CollisionConstraints.ApplyCollisionModifier(*CollisionModifiers, Dt);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_InertiaConditioning);
		UpdateInertiaConditioning();
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CCD);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, CCD);
		CCDManager.ApplyConstraintsPhaseCCD(Dt, &CollisionConstraints.GetConstraintAllocator(), Particles.GetActiveParticlesView().Num());
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CreateConstraintGraph);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_CreateConstraintGraph);
		CreateConstraintGraph();
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_CreateIslands);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_CreateIslands);
		CreateIslands();
	}
	{
		// We keep the graph color stat name for now to compare with previous implementation
		// @todo : change the name to sort constraints
		SCOPE_CYCLE_COUNTER(STAT_Evolution_GraphColor);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_GraphColor);
		SortConstraints();
	}
	{
		// Once the graph is built (constraints are removed/added) we can destroy any collision that are not required
		SCOPE_CYCLE_COUNTER(STAT_Evolution_PruneCollisions);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_PruneCollisions);
		CollisionConstraints.GetConstraintAllocator().PruneExpiredItems();
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
	if(Dt > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_ParallelSolve);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_PerIslandSolve);
		const int32 NumGroups = GetConstraintGraph().NumGroups();
		CSV_CUSTOM_STAT(PhysicsCounters, NumIslandGroups, NumGroups, ECsvCustomStatOp::Set);
		TArray<FPerIslandStats> PerIslandStats;
#if CSV_PROFILER
		PerIslandStats.AddDefaulted(NumGroups);
#endif

		PhysicsParallelFor(NumGroups, [&](int32 GroupIndex) 
		{
			const bool bValidIsland = GetConstraintGraph().GetIslandGroups().IsValidIndex(GroupIndex) && GetConstraintGraph().GetIslandGroup(GroupIndex)->IsValid();
		  	if (!bValidIsland)
			{
				return;
			}
			
			// Reload cache if necessary
			{
				CSV_SCOPED_PER_ISLAND_TIMING_STAT(PerIslandSolve_ReloadCacheTotalSerialized);
				for(auto& IslandSolver : GetConstraintGraph().GetGroupIslands(GroupIndex))
				{
					ReloadParticlesCache(IslandSolver->GetIslandIndex());
				}
			}

			// Collect all the data that the constraint solvers operate on
			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_Gather);
				CSV_SCOPED_PER_ISLAND_TIMING_STAT(PerIslandSolve_GatherTotalSerialized);
				GatherSolverInput(Dt, GroupIndex);
			}

			// Run the first phase of the constraint solvers
			// For GBF this is the hybrid velocity solving step (which also moves the bodies to make the implicit velocity be what it should be)
			// For PBD/QPBD this is the position solve step
			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_ApplyConstraintsPhase1);
				CSV_SCOPED_TIMING_STAT(Chaos, ApplyConstraints);
				CSV_SCOPED_PER_ISLAND_TIMING_STAT(PerIslandSolve_ApplyTotalSerialized);
				ApplyConstraintsPhase1(Dt, GroupIndex);
			}

			for(auto& IslandSolver : GetConstraintGraph().GetGroupIslands(GroupIndex))
			{
				if(!IslandSolver->IsSleeping() && !IslandSolver->IsUsingCache())
				{
					if (PostApplyCallback != nullptr)
					{
						PostApplyCallback(IslandSolver->GetIslandIndex());
					}
				}
			}

			// Update implicit velocities from results of constraint solver phase 1
			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_UpdateVelocites);
				CSV_SCOPED_PER_ISLAND_TIMING_STAT(PerIslandSolve_UpdateVelocitiesTotalSerialized);
				SetImplicitVelocities(Dt, GroupIndex);
			}

			// Run the second phase of the constraint solvers
			// For GBF this is the pushout step
			// For PBD this does nothing
			// For QPBD this is the velocity solve step
			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_ApplyConstraintsPhase2);
				CSV_SCOPED_PER_ISLAND_TIMING_STAT(PerIslandSolve_ApplyPushOutTotalSerialized);
				ApplyConstraintsPhase2(Dt, GroupIndex);
			}

			// Apply projections. This is a phase only implemented by joints that applies a semi-physical
			// correction and/or teleport to fix any errors remaining after the position and velocity solve steps
			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_ApplyConstraintsPhase3);
				CSV_SCOPED_PER_ISLAND_TIMING_STAT(PerIslandSolve_ApplyProjectionTotalSerialized);
				ApplyConstraintsPhase3(Dt, GroupIndex);
			}

			// Update the particles with the results of the constraint solvers, and also update constraint data
			// that is accessed externally (net impusles, break info, etc)
			{
				SCOPE_CYCLE_COUNTER(STAT_Evolution_Scatter);
				CSV_SCOPED_PER_ISLAND_TIMING_STAT(PerIslandSolve_ScatterTotalSerialized);
				ScatterSolverOutput(Dt, GroupIndex);
			}

			for(auto& IslandSolver : GetConstraintGraph().GetGroupIslands(GroupIndex))
			{
				if(!IslandSolver->IsSleeping() && !IslandSolver->IsUsingCache())
				{
					const int32 Island = IslandSolver->GetIslandIndex();
					if (PostApplyPushOutCallback != nullptr)
					{
						PostApplyPushOutCallback(Island);
					}
					BuildDisabledParticles(Island, DisabledParticles, SleepedIslands);
				}
			}
		});

#if CSV_PROFILER
		FPerIslandStats FlattenedStats = FPerIslandStats::Flatten(PerIslandStats);
		FlattenedStats.ReportStats();
#endif
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_SavePostSolve);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_SavePostSolve);
		FEvolutionResimCache* ResimCache = GetCurrentStepResimCache();
		if (ResimCache)
		{
			for (const auto& Particle : Particles.GetActiveKinematicParticlesView())
			{
				if (const auto* Rigid = Particle.CastToRigidParticle())
				{
					//NOTE: this assumes the cached values have not changed after the solve (V, W, P, Q should be untouched, otherwise we'll use the wrong values when resim happens)
					ResimCache->SaveParticlePostSolve(*Rigid->Handle());
				}
			}
			for (const auto& Particle : Particles.GetNonDisabledDynamicView())
			{
				//NOTE: this assumes the cached values have not changed after the solve (V, W, P, Q should be untouched, otherwise we'll use the wrong values when resim happens)
				ResimCache->SaveParticlePostSolve(*Particle.Handle());
			}
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_Evolution_DeactivateSleep);
		CSV_SCOPED_TIMING_STAT(PhysicsVerbose, StepSolver_DeactivateSleep);
		for (int32 Island = 0; Island < GetConstraintGraph().NumIslands(); ++Island)
		{
			if (SleepedIslands[Island])
			{
				GetConstraintGraph().SleepIsland(Particles, Island);
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

	// Clean up the transient data from the constraint graph (e.g., IslandSolvers get cleared here)
	GetConstraintGraph().EndTick();

	if (bChaos_Solver_TestMode)
	{
		TestModeResetParticles();
	}

	if (CVars::DoFinalProbeNarrowPhase)
	{
		// Run contact updates on probe constraints
		GetCollisionConstraints().DetectProbeCollisions(Dt);
	}

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
						const int32 Island = Active.IslandIndex();
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

void FPBDRigidsEvolutionGBF::GatherSolverInput(FReal Dt, int32 GroupIndex)
{
	// We must initialize the solver body container to be large enough to hold all particles in the
	// island so that the pointers remain valid (the array should not grow and relocate)
	if(FPBDIslandGroup* IslandGroup = ConstraintGraph.GetIslandGroups()[GroupIndex].Get())
	{
		IslandGroup->GetBodyContainer().Reset(IslandGroup->GetNumParticles());

		for(auto& SolverIsland : IslandGroup->GetIslands())
		{
			// We pre-add all the dynamic particles solver bodies involved in any constraint to speed up the gather 
			const TArray<FGeometryParticleHandle*>& IslandParticles = SolverIsland->GetParticles();

			if(!SolverIsland->IsSleeping() && !SolverIsland->IsUsingCache())
			{
				for(auto& Particle : IslandParticles)
				{
					FGenericParticleHandle GenericHandle = Particle->Handle();
					GenericHandle->SetSolverBodyIndex(INDEX_NONE);
					
					if(GenericHandle->ObjectState() == EObjectStateType::Dynamic)
					{
						if(FPBDRigidParticleHandle* PBDRigid = Particle->Handle()->CastToRigidParticle())
						{
							if(ConstraintGraph.GetGraphNodes().IsValidIndex(PBDRigid->ConstraintGraphIndex()))
							{
								if(ConstraintGraph.GetGraphNodes()[PBDRigid->ConstraintGraphIndex()].NodeEdges.Num() > 0)
								{
									// Only the non sleeping dynamic rigid particles involved in any constraints will be added
									// by default to the solver bodies
									const int32 BodyIndex = IslandGroup->GetBodyContainer().GetBodies().Emplace(GenericHandle, Dt);
									GenericHandle->SetSolverBodyIndex(BodyIndex);
								}
							}
						}
					}
				}
			}
		}
	}

	// NOTE: SolverBodies are gathered as part of the constraint gather, in the order that they are first seen 
	for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
	{
		ConstraintRule->GatherSolverInput(Dt, GroupIndex);
	}
}

void FPBDRigidsEvolutionGBF::ScatterSolverOutput(FReal Dt, int32 GroupIndex)
{
	// Scatter solver results for constraints (impulses, break events, etc)
	for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
	{
		ConstraintRule->ScatterSolverOutput(Dt, GroupIndex);
	}

	// Scatter body results back to particles (position, rotation, etc)
	if(FPBDIslandGroup* IslandGroup = ConstraintGraph.GetIslandGroups()[GroupIndex].Get())
	{
		IslandGroup->GetBodyContainer().ScatterOutput();
	}
}

void FPBDRigidsEvolutionGBF::ApplyConstraintsPhase1(const FReal Dt, int32 GroupIndex)
{
	// @todo(ccaulfield): track whether we are sufficiently solved and can early-out
	for (int i = 0; i < NumPositionIterations; ++i)
	{
		bool bNeedsAnotherIteration = false;
		for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
		{
			bNeedsAnotherIteration |= ConstraintRule->ApplyConstraints(Dt, GroupIndex, i, NumPositionIterations);
		}

		if (ChaosRigidsEvolutionApplyAllowEarlyOutCVar && !bNeedsAnotherIteration)
		{
			break;
		}
	}
}

void FPBDRigidsEvolutionGBF::SetImplicitVelocities(const FReal Dt, int32 GroupIndex)
{
	if(FPBDIslandGroup* IslandGroup = GetConstraintGraph().GetIslandGroups()[GroupIndex].Get())
	{
		IslandGroup->GetBodyContainer().SetImplicitVelocities(Dt);
	}
}

void FPBDRigidsEvolutionGBF::ApplyConstraintsPhase2(const FReal Dt, int32 GroupIndex)
{
	bool bNeedsAnotherIteration = true;
	for (int32 It = 0; It < NumVelocityIterations; ++It)
	{
		bNeedsAnotherIteration = false;
		for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
		{
			bNeedsAnotherIteration |= ConstraintRule->ApplyPushOut(Dt, GroupIndex, It, NumVelocityIterations);
		}

		if (ChaosRigidsEvolutionApplyPushoutAllowEarlyOutCVar && !bNeedsAnotherIteration)
		{
			break;
		}
	}
}

void FPBDRigidsEvolutionGBF::ApplyConstraintsPhase3(const FReal Dt, int32 GroupIndex)
{
	// Update the body transforms from the deltas calculated in the constraint solve phases 1 and 2
	// NOTE: deliberately not updating the world-space inertia as it is not currently needed by joint projection
	// and no other constraints implement projection
	if (FPBDIslandGroup* IslandGroup = ConstraintGraph.GetIslandGroups()[GroupIndex].Get())
	{
		IslandGroup->GetBodyContainer().ApplyCorrections();
	}

	for (int32 It = 0; It < NumProjectionIterations; ++It)
	{
		for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
		{
			ConstraintRule->ApplyProjection(Dt, GroupIndex, It, NumProjectionIterations);
		}
	}
}

void FPBDRigidsEvolutionGBF::SetIsDeterministic(const bool bInIsDeterministic)
{
	// We detect collisions in parallel, so order is non-deterministic without additional processing
	CollisionConstraints.SetIsDeterministic(bInIsDeterministic);
}


void FPBDRigidsEvolutionGBF::TestModeResetParticles()
{
	for (auto& Rigid : Particles.GetNonDisabledDynamicView())
	{
		FTestModeParticleData* Data = TestModeData.Find(Rigid.Handle());
		if (Data != nullptr)
		{
			Rigid.X() = Data->X;
			Rigid.P() = Data->P;
			Rigid.R() = Data->R;
			Rigid.Q() = Data->Q;
			Rigid.V() = Data->V;
			Rigid.W() = Data->W;
		}
		if (Data == nullptr)
		{
			Data = &TestModeData.Add(Rigid.Handle());
			Data->X = Rigid.X();
			Data->P = Rigid.P();
			Data->R = Rigid.R();
			Data->Q = Rigid.Q();
			Data->V = Rigid.V();
			Data->W = Rigid.W();
		}
	}
}

void FPBDRigidsEvolutionGBF::TestModeResetCollisions()
{
	for (FPBDCollisionConstraintHandle* Collision : CollisionConstraints.GetConstraintHandles())
	{
		Collision->GetContact().ResetManifold();
		Collision->GetContact().ResetModifications();
		Collision->GetContact().GetGJKWarmStartData().Reset();
	}
}

FPBDRigidsEvolutionGBF::FPBDRigidsEvolutionGBF(FPBDRigidsSOAs& InParticles,THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials, const TArray<ISimCallbackObject*>* InCollisionModifiers, bool InIsSingleThreaded)
	: Base(InParticles, SolverPhysicsMaterials, InIsSingleThreaded)
	, Clustering(*this, Particles.GetClusteredParticles())
	, JointConstraintRule(JointConstraints, ChaosSolverJointPriority)
	, SuspensionConstraintRule(SuspensionConstraints, ChaosSolverSuspensionPriority)
	, CollisionConstraints(InParticles, Collided, PhysicsMaterials, PerParticlePhysicsMaterials, &SolverPhysicsMaterials, 0, 0, DefaultRestitutionThreshold)
	, CollisionRule(CollisionConstraints, ChaosSolverCollisionPriority)
	, BroadPhase(InParticles)
	, NarrowPhase(DefaultCollisionCullDistance, BoundsThicknessVelocityMultiplier, CollisionConstraints.GetConstraintAllocator())
	, CollisionDetector(BroadPhase, NarrowPhase, CollisionConstraints)
	, PostIntegrateCallback(nullptr)
	, PreApplyCallback(nullptr)
	, PostApplyCallback(nullptr)
	, PostApplyPushOutCallback(nullptr)
	, CurrentStepResimCacheImp(nullptr)
	, CollisionModifiers(InCollisionModifiers)
	, CCDManager()
	, bIsDeterministic(false)
{
	SetNumPositionIterations(DefaultNumPositionIterations);
	SetNumVelocityIterations(DefaultNumVelocityIterations);
	SetNumProjectionIterations(DefaultNumProjectionIterations);

	CollisionConstraints.SetCanDisableContacts(!!CollisionDisableCulledContacts);

	SetParticleUpdatePositionFunction([this](const TParticleView<FPBDRigidParticles>& ParticlesInput, const FReal Dt)
	{
		ParticlesInput.ParallelFor([&](auto& Particle, int32 Index)
		{
			if (Dt > UE_SMALL_NUMBER)
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

	AddConstraintRule(&SuspensionConstraintRule);
	AddConstraintRule(&CollisionRule);
	AddConstraintRule(&JointConstraintRule);

	SetInternalParticleInitilizationFunction([](const FGeometryParticleHandle*, const FGeometryParticleHandle*) {});
}

FPBDRigidsEvolutionGBF::~FPBDRigidsEvolutionGBF()
{
	// This is really only needed to ensure proper cleanup (we verify that constraints have been removed from 
	// the graph in the destructor). This can be optimized if it's a problem but it shouldn't be
	GetConstraintGraph().Reset();
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

void FPBDRigidsEvolutionGBF::TransferJointConstraintCollisions()
{
	//
	// Stubbed out implementation of joint constraint collision transfer. 
	// Currently disabled because its not required, and the implementation
	// requires proper unit testing before it should be released. 
	//
#if 0
	// Transfer collisions from the child of a joint to the parent.
	// E.g., if body A and B are connected by a joint, with A the parent and B the child...
	// then a third body C collides with B...
	// we create a new collision between A and C at the same world position.
	// E.g., This can be used to forward collision impulses from a vehicle bumper to its
	// chassis without having to worry about making the joint connecting them very stiff
	// which is quite difficult for large mass ratios and would require many iterations.
	if (DoTransferJointConstraintCollisions)
	{
		FCollisionConstraintAllocator& CollisionAllocator = CollisionConstraints.GetConstraintAllocator();

		// @todo(chaos): we should only visit the joints that have ContactTransferScale > 0 
		for (int32 JointConstraintIndex = 0; JointConstraintIndex < GetJointConstraints().NumConstraints(); ++JointConstraintIndex)
		{
			FPBDJointConstraintHandle* JointConstraint = GetJointConstraints().GetConstraintHandle(JointConstraintIndex);
			const FPBDJointSettings& JointSettings = JointConstraint->GetSettings();
			if (JointSettings.ContactTransferScale > FReal(0))
			{
				FGenericParticleHandle ParentParticle = JointConstraint->GetConstrainedParticles()[1];
				FGenericParticleHandle ChildParticle = JointConstraint->GetConstrainedParticles()[0];

				const FRigidTransform3 ParentTransform = FParticleUtilities::GetActorWorldTransform(ParentParticle);
				const FRigidTransform3 ChildTransform = FParticleUtilities::GetActorWorldTransform(ChildParticle);
				const FRigidTransform3 ChildToParentTransform = ChildTransform.GetRelativeTransform(ParentTransform);

				ChildParticle->Handle()->ParticleCollisions().VisitCollisions(
					[&](const FPBDCollisionConstraint* VisitedConstraint)
					{
						if (VisitedConstraint->GetCCDType() != ECollisionCCDType::Disabled)
						{
							return;
						}

						// @todo(chaos): implemeent this
						// Note: the defined out version has a couple issues we will need to address in the new version
						//	-	it passes Implicit pointers from one body to a constraint whose lifetime is not controlled by that body
						//		which could cause problems if the first body is destroyed.
						//	-	we need to properly support collisions constraints without one (or both) Implicit Objects. Collisions are 
						//		managed per shape pair, and found by a key that depends on them, so we'd need to rethink that a bit. 
						//		Here it's useful to be able to use the child implicit to generate the unique key, but we don't want the 
						//		constraint to hold the pointer (see previous issue).
						//	-	we should check to see if there is already an active constraint between the bodies because we don't want
						//		to replace a legit collision with our fake one...probably
						//const FGeometryParticleHandle* NewParentParticleConst = (CurrConstraint->GetParticle0() == ChildParticle->Handle()) ? CurrConstraint->GetParticle1() : ChildCollisionConstraint->GetParticle0();
						//FGeometryParticleHandle* NewParticleA = const_cast<FGeometryParticleHandle*>(NewParentParticleConst);
						//FGeometryParticleHandle* NewParticleB = ParentParticle->Handle();

						FPBDCollisionConstraint& CollisionConstraint = *const_cast<FPBDCollisionConstraint*>(VisitedConstraint);
						// If the collision constraint is colliding with the child of the joint, then 
						// we want to map the collision between the parent of the joint and the external
						// collision body.Otherwise, we map the collision between the childand 
						// the external collision body.The new collision constraint is constructed with
						// the implicit object of the colliding body on the joint, to the particle the
						// collision was transferred to.

						auto MakeCollisionAtIndex0 = [&ChildToParentTransform, &CollisionConstraint](FGenericParticleHandle& TransferToParticle)
						{
							return FPBDCollisionConstraint::Make(
								TransferToParticle->Handle(),
								CollisionConstraint.GetImplicit0(),
								CollisionConstraint.GetCollisionParticles0(),
								ChildToParentTransform.GetRelativeTransform(CollisionConstraint.GetShapeRelativeTransform0()),
								CollisionConstraint.GetParticle1(),
								CollisionConstraint.GetImplicit1(),
								CollisionConstraint.GetCollisionParticles1(),
								CollisionConstraint.GetShapeRelativeTransform1(),
								CollisionConstraint.GetCullDistance(),
								CollisionConstraint.GetUseManifold(),
								CollisionConstraint.GetShapesType()
							);
						};

						auto MakeCollisionAtIndex1 = [&ChildToParentTransform, &CollisionConstraint](FGenericParticleHandle& TransferToParticle)
						{
							return FPBDCollisionConstraint::Make(
								CollisionConstraint.GetParticle1(),
								CollisionConstraint.GetImplicit1(),
								CollisionConstraint.GetCollisionParticles1(),
								CollisionConstraint.GetShapeRelativeTransform1(),
								TransferToParticle->Handle(),
								CollisionConstraint.GetImplicit0(),
								CollisionConstraint.GetCollisionParticles0(),
								ChildToParentTransform.GetRelativeTransform(CollisionConstraint.GetShapeRelativeTransform1()),
								CollisionConstraint.GetCullDistance(),
								CollisionConstraint.GetUseManifold(),
								CollisionConstraint.GetShapesType()
							);
						};

						FGenericParticleHandle CollisionParticleA, CollisionParticleB;
						TUniquePtr<FPBDCollisionConstraint> TransferedConstraint;
						if (CollisionConstraint.GetParticle0() == ChildParticle->CastToRigidParticle() ||
							CollisionConstraint.GetParticle1() == ChildParticle->CastToRigidParticle())
						{
							if (CollisionConstraint.GetParticle0() == ChildParticle->CastToRigidParticle())
							{
								CollisionParticleA = ParentParticle;
								CollisionParticleB = CollisionConstraint.GetParticle1();
								TransferedConstraint = MakeCollisionAtIndex0(ParentParticle);
							}
							else
							{
								CollisionParticleA = CollisionConstraint.GetParticle0();
								CollisionParticleB = ParentParticle;
								TransferedConstraint = MakeCollisionAtIndex1(ParentParticle);
							}
						}
						else if (CollisionConstraint.GetParticle0() == ParentParticle->CastToRigidParticle() ||
							CollisionConstraint.GetParticle1() == ParentParticle->CastToRigidParticle())
						{
							if (CollisionConstraint.GetParticle0() == ParentParticle->CastToRigidParticle())
							{
								CollisionParticleA = ChildParticle;
								CollisionParticleB = CollisionConstraint.GetParticle0();
								TransferedConstraint = MakeCollisionAtIndex0(ChildParticle);
							}
							else
							{
								CollisionParticleA = CollisionConstraint.GetParticle1();
								CollisionParticleB = ChildParticle;
								TransferedConstraint = MakeCollisionAtIndex1(ChildParticle);
							}
						}

						TArray<FManifoldPoint> TransferManifolds;
						for (FManifoldPoint& Manifold : CollisionConstraint.GetManifoldPoints())
						{
							TransferManifolds.Add(Manifold);
						}
						//TransferedConstraint->SetManifoldPoints(TransferManifolds);
						TransferedConstraint->ResetActiveManifoldContacts();
						for (FManifoldPoint& Manifold : CollisionConstraint.GetManifoldPoints())
						{
							TransferedConstraint->AddOneshotManifoldContact(Manifold.ContactPoint);
						}

						FReal CollisionConstraintStiffness = JointConstraint->GetSettings().ContactTransferScale;
						if (TransferCollisionsDebugTestAgainstMaxClamp)
						{
							CollisionConstraintStiffness = TransferCollisionsStiffnessClamp;
						}

						if (CollisionParticleA.Get() && CollisionParticleA->ObjectState() != EObjectStateType::Dynamic)
						{
							if (ensureMsgf(TransferCollisionsKinematicScale > 0, TEXT("Zero or Negative TransferCollisionsKinematicScale")))
							{
								CollisionConstraintStiffness *= TransferCollisionsKinematicScale;
							}
						}
						TransferedConstraint->SetStiffness(FMath::Clamp(CollisionConstraintStiffness, (FReal)0.f, TransferCollisionsStiffnessClamp));

						FParticlePairMidPhase* MidPhase = CollisionAllocator.GetParticlePairMidPhase(CollisionParticleA->Handle(), CollisionParticleB->Handle());
						MidPhase->InjectCollision(*TransferedConstraint);

						return ECollisionVisitorResult::Continue;
					});
			}
		}

		CollisionAllocator.ProcessInjectedConstraints();
	}
#endif
}

void FPBDRigidsEvolutionGBF::DestroyParticleCollisionsInAllocator(FGeometryParticleHandle* Particle)
{
	if (Particle != nullptr)
	{
		CollisionConstraints.GetConstraintAllocator().RemoveParticle(Particle);
		Particle->ParticleCollisions().Reset();
	}
}

void FPBDRigidsEvolutionGBF::DestroyTransientConstraints(FGeometryParticleHandle* Particle)
{
	if (Particle != nullptr)
	{
		// Remove all the particle's collisions from the graph
		GetConstraintGraph().RemoveParticleConstraints(Particle, CollisionConstraints.GetContainerId());

		// Mark all collision constraints for destruction
		DestroyParticleCollisionsInAllocator(Particle);
	}
}

void FPBDRigidsEvolutionGBF::ParticleMaterialChanged(FGeometryParticleHandle* Particle)
{
	Particle->ParticleCollisions().VisitCollisions([this](FPBDCollisionConstraint& Collision)
	{
		CollisionConstraints.UpdateConstraintMaterialProperties(Collision);
		return ECollisionVisitorResult::Continue;
	});
}


void FPBDRigidsEvolutionGBF::UpdateInertiaConditioning()
{
	// The maximum contribution to error correction from rotation
	const FRealSingle MaxRotationRatio = ChaosSolverInertiaConditioningRotationRatio;

	// The error distance that the constraint correction must be stable for
	// @todo(chaos): should probably be tied to constraint teleport threshold?
	const FRealSingle MaxDistance = ChaosSolverInertiaConditioningDistance;

	// A limit on the relative sizes of the inertia components (inverse)
	const FRealSingle MaxInvInertiaComponentRatio = ChaosSolverMaxInvInertiaComponentRatio;

	// @todo(chaos): we should only visit particles that are dirty
	for (auto& Rigid : Particles.GetNonDisabledDynamicView())
	{
		if (Rigid.InertiaConditioningDirty())
		{
			const bool bWantInertiaConditioning = Rigid.IsDynamic() && Rigid.InertiaConditioningEnabled() && bChaosSolverInertiaConditioningEnabled;
			if (bWantInertiaConditioning)
			{
				const FVec3f InvInertiaScale = CalculateParticleInertiaConditioning(Rigid.Handle(), MaxDistance, MaxRotationRatio, MaxInvInertiaComponentRatio);
				Rigid.SetInvIConditioning(InvInertiaScale);
			}
			else
			{
				Rigid.SetInvIConditioning(FVec3f(1));
			}
			Rigid.ClearInertiaConditioningDirty();
		}
	}
}

}

