// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/ParticleHandleFwd.h"


namespace Chaos
{
	class FNarrowPhase;
	class FParticlePairBroadPhase;
	class FSimpleConstraintRule;
	class FSyncCollisionReceiver;

	template <typename T, int d>
	class TPBDCollisionConstraints;

	template<typename T_BROADPHASE, typename T_NARROWPHASE, typename T_RECEIVER, typename T_CONTAINER>
	class TCollisionDetector;

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
		using FCollisionConstraints = TPBDCollisionConstraints<FReal, 3>;
		using FCollisionDetector = TCollisionDetector<FParticlePairBroadPhase, FNarrowPhase, FSyncCollisionReceiver, FCollisionConstraints>;
		using FEvolutionCallback = TFunction<void()>;
		using FRigidParticleSOAs = TPBDRigidsSOAs<FReal, 3>;

		FPBDMinEvolution(FRigidParticleSOAs& InParticles, FCollisionDetector& InCollisionDetector);

		void AddConstraintRule(FSimpleConstraintRule* Rule);

		void Advance(const FReal Dt, const FReal MaxStepDt, const int32 MaxSteps);
		void AdvanceOneTimeStep(const FReal dt, const FReal StepFraction);

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

	private:
		void Integrate(FReal Dt);
		void ApplyKinematicTargets(FReal Dt, FReal StepFraction);
		void DetectCollisions(FReal Dt);
		void ApplyConstraints(FReal Dt);
		void UpdateVelocities(FReal Dt);
		void ApplyPushOutConstraints(FReal Dt);
		void UpdatePositions(FReal Dt);

		FRigidParticleSOAs& Particles;
		FCollisionDetector& CollisionDetector;

		TArray<FSimpleConstraintRule*> ConstraintRules;
		TArray<FSimpleConstraintRule*> PrioritizedConstraintRules;

		int32 NumApplyIterations;
		int32 NumApplyPushOutIterations;
		FVec3 Gravity;

		FEvolutionCallback PostIntegrateCallback;
		FEvolutionCallback PostDetectCollisionsCallback;
		FEvolutionCallback PostApplyCallback;
		FEvolutionCallback PostApplyPushOutCallback;
	};
}
