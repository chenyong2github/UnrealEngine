// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/Evolution/PBDMinEvolution.h"
#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"

namespace Chaos
{
	DECLARE_CYCLE_STAT(TEXT("FPBDMinEvolution::AdvanceOneTimeStep"), STAT_MinEvolution_AdvanceOneTimeStep, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("FPBDMinEvolution::Integrate"), STAT_MinEvolution_Integrate, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("FPBDMinEvolution::KinematicTargets"), STAT_MinEvolution_KinematicTargets, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("FPBDMinEvolution::ApplyConstraints"), STAT_MinEvolution_ApplyConstraints, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("FPBDMinEvolution::UpdateVelocities"), STAT_MinEvolution_UpdateVelocites, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("FPBDMinEvolution::ApplyPushOut"), STAT_MinEvolution_ApplyPushOut, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("FPBDMinEvolution::DetectCollisions"), STAT_MinEvolution_DetectCollisions, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("FPBDMinEvolution::UpdatePositions"), STAT_MinEvolution_UpdatePositions, STATGROUP_Chaos);

	FPBDMinEvolution::FPBDMinEvolution(FRigidParticleSOAs& InParticles, FCollisionDetector& InCollisionDetector)
		: Particles(InParticles)
		, CollisionDetector(InCollisionDetector)
		, NumApplyIterations(0)
		, NumApplyPushOutIterations(0)
		, Gravity(FVec3(0))
	{
	}

	void FPBDMinEvolution::AddConstraintRule(FSimpleConstraintRule* Rule)
	{
		ConstraintRules.Add(Rule);
	}

	// @todo(ccaulfield): dedupe (PBDRigidsEvolutionGBF)
	void FPBDMinEvolution::Advance(const FReal Dt, const FReal MaxStepDt, const int32 MaxSteps)
	{
		// Determine how many steps we would like to take
		int32 NumSteps = FMath::CeilToInt(Dt / MaxStepDt);
		if (NumSteps > 0)
		{
			// Determine the step time
			const FReal StepDt = Dt / (FReal)NumSteps;

			// Limit the number of steps
			// NOTE: This is after step time calculation so sim will appear to slow down for large Dt
			// but that is preferable to blowing up from a large timestep.
			NumSteps = FMath::Clamp(NumSteps, 1, MaxSteps);

			for (int32 Step = 0; Step < NumSteps; ++Step)
			{
				// StepFraction: how much of the remaining time this step represents, used to interpolate kinematic targets
				// E.g., for 4 steps this will be: 1/4, 1/3, 1/2, 1
				const float StepFraction = (FReal)1 / (FReal)(NumSteps - Step);

				UE_LOG(LogChaos, Verbose, TEXT("Advance dt = %f [%d/%d]"), StepDt, Step + 1, NumSteps);

				AdvanceOneTimeStep(StepDt, StepFraction);
			}
		}
	}

	void FPBDMinEvolution::AdvanceOneTimeStep(const FReal Dt, const FReal StepFraction)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_AdvanceOneTimeStep);

		Integrate(Dt);

		ApplyKinematicTargets(Dt, StepFraction);

		if (PostIntegrateCallback != nullptr)
		{
			PostIntegrateCallback();
		}

		DetectCollisions(Dt);

		if (PostDetectCollisionsCallback != nullptr)
		{
			PostDetectCollisionsCallback();
		}

		if (Dt > 0)
		{
			ApplyConstraints(Dt);

			if (PostApplyCallback != nullptr)
			{
				PostApplyCallback();
			}

			UpdateVelocities(Dt);

			ApplyPushOutConstraints(Dt);

			if (PostApplyPushOutCallback != nullptr)
			{
				PostApplyPushOutCallback();
			}

			UpdatePositions(Dt);
		}
	}

	// @todo(ccaulfield): dedupe (PBDRigidsEvolutionGBF)
	void FPBDMinEvolution::Integrate(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_Integrate);

		TPerParticleEulerStepVelocity<FReal, 3> EulerStepVelocityRule;
		TPerParticleEtherDrag<FReal, 3> EtherDragRule;
		TPerParticlePBDEulerStep<FReal, 3> EulerStepPositionRule;

		for (TTransientPBDRigidParticleHandle<FReal, 3>& Particle : Particles.GetActiveParticlesView())
		{
			if (Particle.ObjectState() == EObjectStateType::Dynamic)
			{
				Particle.PreV() = Particle.V();
				Particle.PreW() = Particle.W();

				Particle.F() += Particle.M() * Gravity;

				EulerStepVelocityRule.Apply(Particle, Dt);
				EtherDragRule.Apply(Particle, Dt);

				EulerStepPositionRule.Apply(Particle, Dt);

				Particle.F() = FVec3(0);
				Particle.Torque() = FVec3(0);

				if (Particle.HasBounds())
				{
					const FReal BoundsThickness = 1.0f;
					const FReal BoundsThicknessVelocityInflation = 2.0f;
					const TAABB<FReal, 3>& LocalBounds = Particle.LocalBounds();
					TAABB<FReal, 3> WorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(Particle.P(), Particle.Q()));
					WorldSpaceBounds.ThickenSymmetrically(FVec3(BoundsThickness) + BoundsThicknessVelocityInflation * Particle.V().GetAbs() * Dt);
					Particle.SetWorldSpaceInflatedBounds(WorldSpaceBounds);
				}
			}
		}

		for (TTransientGeometryParticleHandle<FReal, 3>& Particle : Particles.GetActiveKinematicParticlesView())
		{
			if (Particle.HasBounds())
			{
				const FReal BoundsThickness = 1.0f;
				const FReal BoundsThicknessVelocityInflation = 2.0f;
				const TAABB<FReal, 3>& LocalBounds = Particle.LocalBounds();
				TAABB<FReal, 3> WorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(Particle.X(), Particle.R()));
				const FVec3 VAbs = (Particle.CastToKinematicParticle()) ? Particle.CastToKinematicParticle()->V().GetAbs() : FVec3(0);
				WorldSpaceBounds.ThickenSymmetrically(FVec3(BoundsThickness) + BoundsThicknessVelocityInflation * VAbs.GetAbs() * Dt);
				Particle.SetWorldSpaceInflatedBounds(WorldSpaceBounds);
			}
		}
	}

	// @todo(ccaulfield): dedupe (PBDRigidsEvolutionGBF)
	void FPBDMinEvolution::ApplyKinematicTargets(FReal Dt, FReal StepFraction)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_KinematicTargets);

		check(StepFraction > (FReal)0);
		check(StepFraction <= (FReal)1);

		// @todo(ccaulfield): optimize. Depending on the number of kinematics relative to the number that have 
		// targets set, it may be faster to process a command list rather than iterate over them all each frame. 
		const FReal MinDt = 1e-6f;
		for (auto& Particle : Particles.GetActiveKinematicParticlesView())
		{
			TKinematicTarget<FReal, 3>& KinematicTarget = Particle.KinematicTarget();
			switch (KinematicTarget.GetMode())
			{
			case EKinematicTargetMode::None:
				// Nothing to do
				break;

			case EKinematicTargetMode::Zero:
			{
				// Reset velocity and then switch to do-nothing mode
				Particle.V() = FVec3(0);
				Particle.W() = FVec3(0);
				KinematicTarget.SetMode(EKinematicTargetMode::None);
				break;
			}

			case EKinematicTargetMode::Position:
			{
				// Move to kinematic target and update velocities to match
				// Target positions only need to be processed once, and we reset the velocity next frame (if no new target is set)
				FVec3 TargetPos;
				FRotation3 TargetRot;
				if (FMath::IsNearlyEqual(StepFraction, (FReal)1, KINDA_SMALL_NUMBER))
				{
					TargetPos = KinematicTarget.GetTarget().GetLocation();
					TargetRot = KinematicTarget.GetTarget().GetRotation();
					KinematicTarget.SetMode(EKinematicTargetMode::Zero);
				}
				else
				{
					TargetPos = FVec3::Lerp(Particle.X(), KinematicTarget.GetTarget().GetLocation(), StepFraction);
					TargetRot = FRotation3::Slerp(Particle.R(), KinematicTarget.GetTarget().GetRotation(), StepFraction);
				}
				if (Dt > MinDt)
				{
					Particle.V() = FVec3::CalculateVelocity(Particle.X(), TargetPos, Dt);
					Particle.W() = FRotation3::CalculateAngularVelocity(Particle.R(), TargetRot, Dt);
				}
				Particle.X() = TargetPos;
				Particle.R() = TargetRot;
				break;
			}

			case EKinematicTargetMode::Velocity:
			{
				// Move based on velocity
				Particle.X() = Particle.X() + Particle.V() * Dt;
				FRotation3::IntegrateRotationWithAngularVelocity(Particle.R(), Particle.W(), Dt);
				break;
			}
			}
		}
	}

	void FPBDMinEvolution::DetectCollisions(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_DetectCollisions);

		// @todo(ccaulfield): doesn't need to be every frame
		PrioritizedConstraintRules = ConstraintRules;
		PrioritizedConstraintRules.StableSort();

		for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
		{
			ConstraintRule->UpdatePositionBasedState(Dt);
		}

		CollisionDetector.DetectCollisions(Dt);
	}

	void FPBDMinEvolution::ApplyConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_ApplyConstraints);

		// @todo(ccaulfield): track whether we are sufficiently solved and can early-out
		for (int32 i = 0; i < NumApplyIterations; ++i)
		{
			for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
			{
				ConstraintRule->ApplyConstraints(Dt, i, NumApplyIterations);
			}
		}
	}

	void FPBDMinEvolution::UpdateVelocities(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_UpdateVelocites);

		TPerParticlePBDUpdateFromDeltaPosition<FReal, 3> UpdateVelocityRule;
		for (auto& Particle : Particles.GetActiveParticlesView())
		{
			UpdateVelocityRule.Apply(Particle, Dt);
		}
	}

	void FPBDMinEvolution::ApplyPushOutConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_ApplyPushOut);

		bool bNeedsAnotherIteration = true;
		for (int32 It = 0; bNeedsAnotherIteration && (It < NumApplyPushOutIterations); ++It)
		{
			bNeedsAnotherIteration = false;
			for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
			{
				if (ConstraintRule->ApplyPushOut(Dt, It, NumApplyPushOutIterations))
				{
					bNeedsAnotherIteration = true;
				}
			}
		}
	}

	void FPBDMinEvolution::UpdatePositions(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_UpdatePositions);
		for (auto& Particle : Particles.GetActiveParticlesView())
		{
			Particle.X() = Particle.P();
			Particle.R() = Particle.Q();
		}
	}

}
