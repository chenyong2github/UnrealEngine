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
	class IResimCacheBase;
	class FEvolutionResimCache;

	CHAOS_API extern FRealSingle HackMaxAngularVelocity;
	CHAOS_API extern FRealSingle HackMaxVelocity;

	CHAOS_API extern FRealSingle HackLinearDrag;
	CHAOS_API extern FRealSingle HackAngularDrag;

	using FPBDRigidsEvolutionCallback = TFunction<void()>;

	using FPBDRigidsEvolutionIslandCallback = TFunction<void(int32 Island)>;

	using FPBDRigidsEvolutionInternalHandleCallback = TFunction<void(
		const FGeometryParticleHandle* OldParticle,
		const FGeometryParticleHandle* NewParticle)>;

	class FPBDRigidsEvolutionGBF : public FPBDRigidsEvolutionBase
	{
	public:
		using Base = FPBDRigidsEvolutionBase;
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
		using Base::PerParticlePhysicsMaterials;
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

		using FGravityForces = FPerParticleGravity;
		using FCollisionConstraints = FPBDCollisionConstraints;
		using FCollisionConstraintRule = TPBDConstraintColorRule<FCollisionConstraints>;
		using FCollisionDetector = FSpatialAccelerationCollisionDetector;
		using FExternalForces = FPerParticleExternalForces;
		using FRigidClustering = TPBDRigidClustering<FPBDRigidsEvolutionGBF, FPBDCollisionConstraints>;

		// Default iteration counts
		static constexpr int32 DefaultNumIterations = 8;
		static constexpr int32 DefaultNumCollisionPairIterations = 1;
		static constexpr int32 DefaultNumPushOutIterations = 1;
		static constexpr int32 DefaultNumCollisionPushOutPairIterations = 3;
		static constexpr FRealSingle DefaultCollisionMarginFraction = 0.1f;
		static constexpr FRealSingle DefaultCollisionMarginMax = 100.0f;
		static constexpr FRealSingle DefaultCollisionCullDistance = 5.0f;
		static constexpr int32 DefaultNumJointPairIterations = 3;
		static constexpr int32 DefaultNumJointPushOutPairIterations = 0;
		static constexpr int32 DefaultRestitutionThreshold = 1000;

		// @todo(chaos): Required by clustering - clean up
		using Base::ApplyPushOut;

		CHAOS_API FPBDRigidsEvolutionGBF(FPBDRigidsSOAs& InParticles, THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials, const TArray<ISimCallbackObject*>* InCollisionModifiers = nullptr, bool InIsSingleThreaded = false);
		CHAOS_API ~FPBDRigidsEvolutionGBF() {}

		FORCEINLINE void SetPostIntegrateCallback(const FPBDRigidsEvolutionCallback& Cb)
		{
			PostIntegrateCallback = Cb;
		}

		FORCEINLINE void SetPostDetectCollisionsCallback(const FPBDRigidsEvolutionCallback& Cb)
		{
			PostDetectCollisionsCallback = Cb;
		}

		FORCEINLINE void SetPreApplyCallback(const FPBDRigidsEvolutionCallback& Cb)
		{
			PreApplyCallback = Cb;
		}

		FORCEINLINE void SetPostApplyCallback(const FPBDRigidsEvolutionIslandCallback& Cb)
		{
			PostApplyCallback = Cb;
		}

		FORCEINLINE void SetPostApplyPushOutCallback(const FPBDRigidsEvolutionIslandCallback& Cb)
		{
			PostApplyPushOutCallback = Cb;
		}

		FORCEINLINE void SetInternalParticleInitilizationFunction(const FPBDRigidsEvolutionInternalHandleCallback& Cb)
		{ 
			InternalParticleInitilization = Cb;
		}

		FORCEINLINE void DoInternalParticleInitilization(const FGeometryParticleHandle* OldParticle, const FGeometryParticleHandle* NewParticle) 
		{ 
			if(InternalParticleInitilization)
			{
				InternalParticleInitilization(OldParticle, NewParticle);
			}
		}

		CHAOS_API void Advance(const FReal Dt, const FReal MaxStepDt, const int32 MaxSteps);
		CHAOS_API void AdvanceOneTimeStep(const FReal dt, const FSubStepInfo& SubStepInfo = FSubStepInfo());

		FORCEINLINE FCollisionConstraints& GetCollisionConstraints() { return CollisionConstraints; }
		FORCEINLINE const FCollisionConstraints& GetCollisionConstraints() const { return CollisionConstraints; }

		FORCEINLINE FCollisionConstraintRule& GetCollisionConstraintsRule() { return CollisionRule; }
		FORCEINLINE const FCollisionConstraintRule& GetCollisionConstraintsRule() const { return CollisionRule; }

		FORCEINLINE FCollisionDetector& GetCollisionDetector() { return CollisionDetector; }
		FORCEINLINE const FCollisionDetector& GetCollisionDetector() const { return CollisionDetector; }

		FORCEINLINE FGravityForces& GetGravityForces() { return GravityForces; }
		FORCEINLINE const FGravityForces& GetGravityForces() const { return GravityForces; }

		FORCEINLINE const TPBDRigidClustering<FPBDRigidsEvolutionGBF, FPBDCollisionConstraints>& GetRigidClustering() const { return Clustering; }
		FORCEINLINE TPBDRigidClustering<FPBDRigidsEvolutionGBF, FPBDCollisionConstraints>& GetRigidClustering() { return Clustering; }

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
			FPerParticleEulerStepVelocity EulerStepVelocityRule;
			FPerParticleAddImpulses AddImpulsesRule;
			FPerParticleEtherDrag EtherDragRule;
			FPerParticlePBDEulerStep EulerStepRule;

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
						if (Particle.CCDEnabled())
						{
							WorldSpaceBounds.ThickenSymmetrically(Particle.V() * Dt);
						}
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

		CHAOS_API TUniquePtr<IResimCacheBase> CreateExternalResimCache() const;
		CHAOS_API void SetCurrentStepResimCache(IResimCacheBase* InCurrentStepResimCache);

		CHAOS_API FSpatialAccelerationBroadPhase& GetBroadPhase() { return BroadPhase; }

	protected:

		CHAOS_API void AdvanceOneTimeStepImpl(const FReal dt, const FSubStepInfo& SubStepInfo);
		
		FEvolutionResimCache* GetCurrentStepResimCache()
		{
			return CurrentStepResimCacheImp;
		}

		TPBDRigidClustering<FPBDRigidsEvolutionGBF, FPBDCollisionConstraints> Clustering;

		FGravityForces GravityForces;
		FCollisionConstraints CollisionConstraints;
		FCollisionConstraintRule CollisionRule;
		FSpatialAccelerationBroadPhase BroadPhase;
		FNarrowPhase NarrowPhase;
		FSpatialAccelerationCollisionDetector CollisionDetector;

		FPBDRigidsEvolutionCallback PostIntegrateCallback;
		FPBDRigidsEvolutionCallback PostDetectCollisionsCallback;
		FPBDRigidsEvolutionCallback PreApplyCallback;
		FPBDRigidsEvolutionIslandCallback PostApplyCallback;
		FPBDRigidsEvolutionIslandCallback PostApplyPushOutCallback;
		FPBDRigidsEvolutionInternalHandleCallback InternalParticleInitilization;
		FEvolutionResimCache* CurrentStepResimCacheImp;
		const TArray<ISimCallbackObject*>* CollisionModifiers;
	};

}
