// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/Collision/SpatialAccelerationCollisionDetector.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PerParticleAddImpulses.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleExternalForces.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"

namespace Chaos
{
	class FChaosArchive;

	CHAOS_API extern float HackMaxAngularVelocity;
	CHAOS_API extern float HackMaxVelocity;

	CHAOS_API extern float HackLinearDrag;
	CHAOS_API extern float HackAngularDrag;

	using FPBDRigidsEvolutionCallback = TFunction<void()>;

	using FPBDRigidsEvolutionIslandCallback = TFunction<void(int32 Island)>;

	using FPBDRigidsEvolutionInternalHandleCallback = TFunction<void(
		const TGeometryParticleHandle<float, 3> * OldParticle,
		const TGeometryParticleHandle<float, 3> * NewParticle)>;

	template <typename Traits>
	class TPBDRigidsEvolutionGBF : public TPBDRigidsEvolutionBase<Traits>
	{
	public:
		using Base = TPBDRigidsEvolutionBase<Traits>;
		using Base::Particles;
		using typename Base::FForceRule;
		using Base::ForceRules;
		using Base::PrepareTick;
		using Base::UnprepareTick;
		using Base::ApplyKinematicTargets;
		using Base::UpdateConstraintPositionBasedState;
		using Base::InternalAcceleration;
		using Base::CreateConstraintGraph;
		using Base::CreateIslands;
		using Base::GetParticles;
		using Base::DirtyParticle;
		using Base::SetPhysicsMaterial;
		using Base::SetPerParticlePhysicsMaterial;
		using Base::GetPerParticlePhysicsMaterial;
		using Base::CreateParticle;
		using Base::GenerateUniqueIdx;
		using Base::DestroyParticle;
		using Base::CreateClusteredParticles;
		using Base::EnableParticle;
		using Base::DisableParticles;
		using Base::GetActiveClusteredArray;
		using Base::NumIslands;
		using Base::GetNonDisabledClusteredArray;
		using Base::DisableParticle;
		using Base::PrepareIteration;
		using Base::GetConstraintGraph;
		using Base::ApplyConstraints;
		using Base::UpdateVelocities;
		using Base::PhysicsMaterials;
		using Base::ParticleDisableCount;
		using Base::SolverPhysicsMaterials;
		using Base::UnprepareIteration;
		using Base::CaptureRewindData;
		using Base::Collided;
		using Base::SetParticleUpdateVelocityFunction;
		using Base::SetParticleUpdatePositionFunction;
		using Base::AddForceFunction;
		using Base::AddConstraintRule;
		using Base::ParticleUpdatePosition;

		using FGravityForces = TPerParticleGravity<FReal, 3>;
		using FCollisionConstraints = FPBDCollisionConstraints;
		using FCollisionConstraintRule = TPBDConstraintColorRule<FCollisionConstraints>;
		using FCollisionDetector = FSpatialAccelerationCollisionDetector;
		using FExternalForces = TPerParticleExternalForces<FReal, 3>;

		static constexpr int32 DefaultNumIterations = 1;
		static constexpr int32 DefaultNumPairIterations = 1;
		static constexpr int32 DefaultNumPushOutIterations = 3;
		static constexpr int32 DefaultNumPushOutPairIterations = 2;

		// @todo(chaos): Required by clustering - clean up
		using Base::ApplyPushOut;

		CHAOS_API TPBDRigidsEvolutionGBF(TPBDRigidsSOAs<FReal, 3>& InParticles, THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials, int32 InNumIterations = DefaultNumIterations, int32 InNumPushoutIterations = DefaultNumPushOutIterations, bool InIsSingleThreaded = false);
		CHAOS_API ~TPBDRigidsEvolutionGBF() {}

		void SetPostIntegrateCallback(const FPBDRigidsEvolutionCallback& Cb)
		{
			PostIntegrateCallback = Cb;
		}

		void SetPostDetectCollisionsCallback(const FPBDRigidsEvolutionCallback& Cb)
		{
			PostDetectCollisionsCallback = Cb;
		}

		void SetCollisionModifierCallback(const FCollisionModifierCallback& Cb)
		{
			CollisionModifierCallback = Cb;
		}

		void SetPreApplyCallback(const FPBDRigidsEvolutionCallback& Cb)
		{
			PreApplyCallback = Cb;
		}

		void SetPostApplyCallback(const FPBDRigidsEvolutionIslandCallback& Cb)
		{
			PostApplyCallback = Cb;
		}

		void SetPostApplyPushOutCallback(const FPBDRigidsEvolutionIslandCallback& Cb)
		{
			PostApplyPushOutCallback = Cb;
		}

		void SetInternalParticleInitilizationFunction(const FPBDRigidsEvolutionInternalHandleCallback& Cb)
		{ 
			InternalParticleInitilization = Cb;
		}

		void DoInternalParticleInitilization(const TGeometryParticleHandle<float, 3>* OldParticle, const TGeometryParticleHandle<float, 3>* NewParticle) 
		{ 
			if (InternalParticleInitilization) InternalParticleInitilization(OldParticle, NewParticle); 
		}


		CHAOS_API void Advance(const FReal Dt, const FReal MaxStepDt, const int32 MaxSteps);
		CHAOS_API void AdvanceOneTimeStep(const FReal dt, const FReal StepFraction = (FReal)1.0);

		FCollisionConstraints& GetCollisionConstraints() { return CollisionConstraints; }
		const FCollisionConstraints& GetCollisionConstraints() const { return CollisionConstraints; }

		FCollisionConstraintRule& GetCollisionConstraintsRule() { return CollisionRule; }
		const FCollisionConstraintRule& GetCollisionConstraintsRule() const { return CollisionRule; }

		FCollisionDetector& GetCollisionDetector() { return CollisionDetector; }
		const FCollisionDetector& GetCollisionDetector() const { return CollisionDetector; }

		FGravityForces& GetGravityForces() { return GravityForces; }
		const FGravityForces& GetGravityForces() const { return GravityForces; }

		const auto& GetRigidClustering() const { return Clustering; }
		auto& GetRigidClustering() { return Clustering; }

		CHAOS_API inline void EndFrame(FReal Dt)
		{
			Particles.GetNonDisabledDynamicView().ParallelFor([&](auto& Particle, int32 Index) {
				Particle.F() = FVec3(0);
				Particle.Torque() = FVec3(0);
			});
		}

		template<typename TParticleView>
		void Integrate(const TParticleView& InParticles, FReal Dt)
		{
			//SCOPE_CYCLE_COUNTER(STAT_Integrate);
			CHAOS_SCOPED_TIMER(Integrate);
			TPerParticleEulerStepVelocity<FReal, 3> EulerStepVelocityRule;
			TPerParticleAddImpulses<FReal, 3> AddImpulsesRule;
			TPerParticleEtherDrag<FReal, 3> EtherDragRule;
			TPerParticlePBDEulerStep<FReal, 3> EulerStepRule;

			const FReal MaxAngularSpeedSq = HackMaxAngularVelocity * HackMaxAngularVelocity;
			const FReal MaxSpeedSq = HackMaxVelocity * HackMaxVelocity;
			InParticles.ParallelFor([&](auto& GeomParticle, int32 Index) {
				//question: can we enforce this at the API layer? Right now islands contain non dynamic which makes this hard
				auto PBDParticle = GeomParticle.CastToRigidParticle();
				if (PBDParticle && PBDParticle->ObjectState() == EObjectStateType::Dynamic)
				{
					auto& Particle = *PBDParticle;

					//save off previous velocities
					Particle.PreV() = Particle.V();
					Particle.PreW() = Particle.W();

					for (FForceRule ForceRule : ForceRules)
					{
						ForceRule(Particle, Dt);
					}
					EulerStepVelocityRule.Apply(Particle, Dt);
					AddImpulsesRule.Apply(Particle, Dt);
					EtherDragRule.Apply(Particle, Dt);

					if (HackMaxAngularVelocity >= 0.f)
					{
						const FReal AngularSpeedSq = Particle.W().SizeSquared();
						if (AngularSpeedSq > MaxAngularSpeedSq)
						{
							Particle.W() = Particle.W() * (HackMaxAngularVelocity / FMath::Sqrt(AngularSpeedSq));
						}
					}

					if (HackMaxVelocity >= 0.f)
					{
						const FReal SpeedSq = Particle.V().SizeSquared();
						if (SpeedSq > MaxSpeedSq)
						{
							Particle.V() = Particle.V() * (HackMaxVelocity / FMath::Sqrt(SpeedSq));
						}
					}

					EulerStepRule.Apply(Particle, Dt);

					if (Particle.HasBounds())
					{
						const FAABB3& LocalBounds = Particle.LocalBounds();
						FAABB3 WorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(Particle.P(), Particle.Q()));
						WorldSpaceBounds.ThickenSymmetrically(Particle.V() * Dt);
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

		CHAOS_API void AdvanceOneTimeStepImpl(const FReal dt, const FReal StepFraction);

		TPBDRigidClustering<TPBDRigidsEvolutionGBF<Traits>, FPBDCollisionConstraints, FReal, 3> Clustering;

		FGravityForces GravityForces;
		FCollisionConstraints CollisionConstraints;
		FCollisionConstraintRule CollisionRule;
		FSpatialAccelerationBroadPhase BroadPhase;
		FNarrowPhase NarrowPhase;
		FSpatialAccelerationCollisionDetector CollisionDetector;

		FPBDRigidsEvolutionCallback PostIntegrateCallback;
		FPBDRigidsEvolutionCallback PostDetectCollisionsCallback;
		FCollisionModifierCallback CollisionModifierCallback;
		FPBDRigidsEvolutionCallback PreApplyCallback;
		FPBDRigidsEvolutionIslandCallback PostApplyCallback;
		FPBDRigidsEvolutionIslandCallback PostApplyPushOutCallback;
		FPBDRigidsEvolutionInternalHandleCallback InternalParticleInitilization;
	};

#if PLATFORM_MAC
#define EVOLUTION_TRAIT(Trait) extern template class CHAOS_API TPBDRigidsEvolutionGBF<Trait>;
#else
#define EVOLUTION_TRAIT(Trait) extern template class TPBDRigidsEvolutionGBF<Trait>;
#endif
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
}
