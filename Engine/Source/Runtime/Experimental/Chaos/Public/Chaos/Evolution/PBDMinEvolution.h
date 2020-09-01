// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Evolution/SimulationSpace.h"
#include "Chaos/ParticleHandleFwd.h"


namespace Chaos
{
	class FParticlePairCollisionDetector;
	class FPBDCollisionConstraints;
	class FSimpleConstraintRule;

	template <typename T, int d>
	class TPBDRigidsSOAs;


	/**
	 * A minimal optimized evolution with support for
	 *	- PBD Rigids
	 *	- Joints
	 *	- Collisions
	 *
	 * It is single-threaded and does not use a constraint graph or partition the particles into islands.
	 */
	class CHAOS_API FPBDMinEvolution
	{
	public:
		// @todo(ccaulfield): make it so that CollisionDetection is plugged in with a constraint rule...

		using FCollisionDetector = FParticlePairCollisionDetector;
		using FEvolutionCallback = TFunction<void()>;
		using FRigidParticleSOAs = TPBDRigidsSOAs<FReal, 3>;

		FPBDMinEvolution(FRigidParticleSOAs& InParticles, TArrayCollectionArray<FVec3>& InPrevX, TArrayCollectionArray<FRotation3>& InPrevR, FCollisionDetector& InCollisionDetector, const FReal InBoundsExtension);

		void AddConstraintRule(FSimpleConstraintRule* Rule);

		void Advance(const FReal StepDt, const int32 NumSteps, const FReal RewindDt);
		void AdvanceOneTimeStep(const FReal Dt, const FReal StepFraction);

		void SetNumIterations(const int32 NumIts)
		{
			NumApplyIterations = NumIts;
		}

		void SetNumPushOutIterations(const int32 NumIts)
		{
			NumApplyPushOutIterations = NumIts;
		}

		void SetGravity(const FVec3& G)
		{
			Gravity = G;
		}

		void SetBoundsExtension(const FReal InBoundsExtension)
		{
			BoundsExtension = InBoundsExtension;
		}

		void SetPostIntegrateCallback(const FEvolutionCallback& Cb)
		{
			PostIntegrateCallback = Cb;
		}

		void SetPostDetectCollisionsCallback(const FEvolutionCallback& Cb)
		{
			PostDetectCollisionsCallback = Cb;
		}

		void SetPostApplyCallback(const FEvolutionCallback& Cb)
		{
			PostApplyCallback = Cb;
		}

		void SetPostApplyPushOutCallback(const FEvolutionCallback& Cb)
		{
			PostApplyPushOutCallback = Cb;
		}

		void SetSimulationSpace(const FSimulationSpace& InSimulationSpace)
		{
			SimulationSpace = InSimulationSpace;
		}

		FSimulationSpaceSettings& GetSimulationSpaceSettings()
		{
			return SimulationSpaceSettings;
		}

		const FSimulationSpaceSettings& GetSimulationSpaceSettings() const
		{
			return SimulationSpaceSettings;
		}

		void SetSimulationSpaceSettings(const FSimulationSpaceSettings& InSimulationSpaceSettings)
		{
			SimulationSpaceSettings = InSimulationSpaceSettings;
		}

	private:
		void PrepareTick();
		void UnprepareTick();
		void Rewind(FReal Dt, FReal RewindDt);
		void Integrate(FReal Dt);
		void IntegrateImpl(FReal Dt);
		void IntegrateImpl2(FReal Dt);
		void IntegrateImplISPC(FReal Dt);
		void ApplyKinematicTargets(FReal Dt, FReal StepFraction);
		void DetectCollisions(FReal Dt);
		void PrepareIteration(FReal Dt);
		void UnprepareIteration(FReal Dt);
		void ApplyConstraints(FReal Dt);
		void UpdateVelocities(FReal Dt);
		void ApplyPushOutConstraints(FReal Dt);
		void UpdatePositions(FReal Dt);

		FRigidParticleSOAs& Particles;
		FCollisionDetector& CollisionDetector;

		TArrayCollectionArray<FVec3>& ParticlePrevXs;
		TArrayCollectionArray<FRotation3>& ParticlePrevRs;

		TArray<FSimpleConstraintRule*> ConstraintRules;
		TArray<FSimpleConstraintRule*> PrioritizedConstraintRules;

		int32 NumApplyIterations;
		int32 NumApplyPushOutIterations;
		FReal BoundsExtension;
		FVec3 Gravity;
		FSimulationSpaceSettings SimulationSpaceSettings;
		FSimulationSpace SimulationSpace;

		FEvolutionCallback PostIntegrateCallback;
		FEvolutionCallback PostDetectCollisionsCallback;
		FEvolutionCallback PostApplyCallback;
		FEvolutionCallback PostApplyPushOutCallback;
	};
}
