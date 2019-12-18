// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleVelocityDamping.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticleExternalForces.h"

namespace Chaos
{
class FChaosArchive;

CHAOS_API extern float HackMaxAngularVelocity;
CHAOS_API extern float HackMaxVelocity;

CHAOS_API extern float HackLinearDrag;
CHAOS_API extern float HackAngularDrag;

template<typename T, int d>
class TPBDRigidsEvolutionGBF;

template<typename T, int d>
using TPBDRigidsEvolutionCallback = TFunction<void()>;

template<typename T, int d>
using TPBDRigidsEvolutionIslandCallback = TFunction<void(int32 Island)>;


template<typename T, int d>
class TPBDRigidsEvolutionGBF : public TPBDRigidsEvolutionBase<TPBDRigidsEvolutionGBF<T, d>, TPBDCollisionConstraints<T,d>, T, d>
{
public:
	using Base = TPBDRigidsEvolutionBase<TPBDRigidsEvolutionGBF<T, d>, TPBDCollisionConstraints<T, d>, T, d>;
	using Base::Particles;
	using Base::ForceRules;
	using Base::ParticleUpdatePosition;
	using Base::ApplyKinematicTargets;
	using Base::SetParticleUpdateVelocityFunction;
	using Base::SetParticleUpdatePositionFunction;
	using Base::AddConstraintRule;
	using Base::AddForceFunction;
	using Base::Clustering;
	using typename Base::FForceRule;
	using FGravityForces = TPerParticleGravity<T, d>;
	using FCollisionConstraints = TPBDCollisionConstraints<T, d>;
	using FCollisionConstraintRule = TPBDConstraintColorRule<FCollisionConstraints>;
	using FCollisionDetector = TCollisionDetector<FSpatialAccelerationBroadPhase, FNarrowPhase, FAsyncCollisionReceiver, FCollisionConstraints>;
	using FExternalForces = TPerParticleExternalForces<T, d>;

	static constexpr int32 DefaultNumIterations = 1;
	static constexpr int32 DefaultNumPairIterations = 1;
	static constexpr int32 DefaultNumPushOutIterations = 5;
	static constexpr int32 DefaultNumPushOutPairIterations = 2;

	CHAOS_API TPBDRigidsEvolutionGBF(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations = DefaultNumIterations, bool InIsSingleThreaded = false);
	CHAOS_API ~TPBDRigidsEvolutionGBF() {}

	void SetPostIntegrateCallback(const TPBDRigidsEvolutionCallback<T, d>& Cb)
	{
		PostIntegrateCallback = Cb;
	}

	void SetPostDetectCollisionsCallback(const TPBDRigidsEvolutionCallback<T, d>& Cb)
	{
		PostDetectCollisionsCallback = Cb;
	}

	void SetPreApplyCallback(const TPBDRigidsEvolutionCallback<T, d>& Cb)
	{
		PreApplyCallback = Cb;
	}

	void SetPostApplyCallback(const TPBDRigidsEvolutionIslandCallback<T, d>& Cb)
	{
		PostApplyCallback = Cb;
	}

	void SetPostApplyPushOutCallback(const TPBDRigidsEvolutionIslandCallback<T, d>& Cb)
	{
		PostApplyPushOutCallback = Cb;
	}

	CHAOS_API void Advance(const T Dt, const T MaxStepDt, const int32 MaxSteps);
	CHAOS_API void AdvanceOneTimeStep(const T dt, const T StepFraction = (T)1.0);

	using Base::ApplyConstraints;
	using Base::ApplyPushOut;

	FCollisionConstraints& GetCollisionConstraints() { return CollisionConstraints; }
	const FCollisionConstraints& GetCollisionConstraints() const { return CollisionConstraints; }

	FCollisionConstraintRule& GetCollisionConstraintsRule() { return CollisionRule; }
	const FCollisionConstraintRule& GetCollisionConstraintsRule() const { return CollisionRule; }

	FCollisionDetector& GetCollisionDetector() { return CollisionDetector; }
	const FCollisionDetector& GetCollisionDetector() const { return CollisionDetector; }

	FExternalForces& GetExternalForces() { return ExternalForces; }
	const FExternalForces& GetExternalForces() const { return ExternalForces; }

	FGravityForces& GetGravityForces() { return GravityForces; }
	const FGravityForces& GetGravityForces() const { return GravityForces; }

	CHAOS_API inline void EndFrame(T Dt)
	{
		Particles.GetNonDisabledDynamicView().ParallelFor([&](auto& Particle, int32 Index)
		{
			Particle.ExternalForce() = TVector<T, 3>(0);
			Particle.ExternalTorque() = TVector<T, 3>(0);
		});
	}


	template <typename TParticleView>
	void Integrate(const TParticleView& InParticles, T Dt)
	{
		//SCOPE_CYCLE_COUNTER(STAT_Integrate);
		CHAOS_SCOPED_TIMER(Integrate);
		TPerParticleInitForce<T, d> InitForceRule;
		TPerParticleEulerStepVelocity<T, d> EulerStepVelocityRule;
		TPerParticleVelocityDamping<T, d> VelocityDampingRule;
		TPerParticleEtherDrag<T, d> EtherDragRule(HackLinearDrag, HackAngularDrag);
		TPerParticlePBDEulerStep<T, d> EulerStepRule;

		const T MaxAngularSpeedSq = HackMaxAngularVelocity * HackMaxAngularVelocity;
		const T MaxSpeedSq = HackMaxVelocity * HackMaxVelocity;
		InParticles.ParallelFor([&](auto& GeomParticle, int32 Index)
		{
			//question: can we enforce this at the API layer? Right now islands contain non dynamic which makes this hard
			auto PBDParticle = GeomParticle.CastToRigidParticle();
			if(PBDParticle && PBDParticle->ObjectState() == EObjectStateType::Dynamic)
			{
				auto& Particle = *PBDParticle;

				//save off previous velocities
				Particle.PreV() = Particle.V();
				Particle.PreW() = Particle.W();

				InitForceRule.Apply(Particle, Dt);
				for (FForceRule ForceRule : ForceRules)
				{
					ForceRule(Particle, Dt);
				}
				EulerStepVelocityRule.Apply(Particle, Dt);
				VelocityDampingRule.Apply(Particle, Dt);
				EtherDragRule.Apply(Particle, Dt);

				if (HackMaxAngularVelocity >= 0.f)
				{
					const T AngularSpeedSq = Particle.W().SizeSquared();
					if (AngularSpeedSq > MaxAngularSpeedSq)
					{
						Particle.W() = Particle.W() * (HackMaxAngularVelocity / FMath::Sqrt(AngularSpeedSq));
					}
				}

				if (HackMaxVelocity >= 0.f)
				{
					const T SpeedSq = Particle.V().SizeSquared();
					if (SpeedSq > MaxSpeedSq)
					{
						Particle.V() = Particle.V() * (HackMaxVelocity / FMath::Sqrt(SpeedSq));
					}
				}

				EulerStepRule.Apply(Particle, Dt);

				if (Particle.HasBounds())
				{
					const TBox<T, d>& LocalBounds = Particle.LocalBounds();
					TBox<T, d> WorldSpaceBounds = LocalBounds.TransformedBox(TRigidTransform<T, d>(Particle.P(), Particle.Q()));
					WorldSpaceBounds.ThickenSymmetrically(Particle.V());
					Particle.SetWorldSpaceInflatedBounds(WorldSpaceBounds);
				}
			}
		});

		for (auto& Particle : InParticles)
		{
			Base::DirtyParticle(Particle);
		}
	}

	CHAOS_API void Serialize(FChaosArchive& Ar);

protected:
	using Base::UpdateConstraintPositionBasedState;
	using Base::CreateConstraintGraph;
	using Base::CreateIslands;
	using Base::ConstraintRules;
	using Base::GetConstraintGraph;
	using Base::UpdateVelocities;
	using Base::PhysicsMaterials;
	using Base::ParticleDisableCount;
	using Base::Collided;
	using Base::InternalAcceleration;

	FGravityForces GravityForces;
	FExternalForces ExternalForces;
	FCollisionConstraints CollisionConstraints;
	FCollisionConstraintRule CollisionRule;
	FSpatialAccelerationBroadPhase BroadPhase;
	FCollisionDetector CollisionDetector;

	TPBDRigidsEvolutionCallback<T, d> PostIntegrateCallback;
	TPBDRigidsEvolutionCallback<T, d> PostDetectCollisionsCallback;
	TPBDRigidsEvolutionCallback<T, d> PreApplyCallback;
	TPBDRigidsEvolutionIslandCallback<T, d> PostApplyCallback;
	TPBDRigidsEvolutionIslandCallback<T, d> PostApplyPushOutCallback;
};
}
