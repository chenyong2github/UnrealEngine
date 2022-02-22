// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/Collision/SpatialAccelerationCollisionDetector.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PerParticleAddImpulses.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleExternalForces.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/CCDUtilities.h"
#include "Chaos/PBDSuspensionConstraints.h"

namespace Chaos
{
	class FCollisionConstraintAllocator;
	class FChaosArchive;
	class IResimCacheBase;
	class FEvolutionResimCache;

	namespace CVars
	{
		CHAOS_API extern FRealSingle HackMaxAngularVelocity;
		CHAOS_API extern FRealSingle HackMaxVelocity;
		CHAOS_API extern FRealSingle CCDEnableThresholdBoundsScale;
		CHAOS_API extern bool bChaosCollisionCCDUseTightBoundingBox;
	}

	using FPBDRigidsEvolutionCallback = TFunction<void()>;

	using FPBDRigidsEvolutionIslandCallback = TFunction<void(int32 Island)>;

	using FPBDRigidsEvolutionInternalHandleCallback = TFunction<void(
		const FGeometryParticleHandle* OldParticle,
		FGeometryParticleHandle* NewParticle)>;

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
		using Base::NumIslands;
		using Base::GetNonDisabledClusteredView;
		using Base::DisableParticle;
		using Base::GetConstraintGraph;
		using Base::PhysicsMaterials;
		using Base::PerParticlePhysicsMaterials;
		using Base::ParticleDisableCount;
		using Base::SolverPhysicsMaterials;
		using Base::CaptureRewindData;
		using Base::Collided;
		using Base::SetParticleUpdatePositionFunction;
		using Base::AddForceFunction;
		using Base::AddConstraintRule;
		using Base::ParticleUpdatePosition;
		using Base::GetAllRemovals;

		using FGravityForces = FPerParticleGravity;
		using FCollisionConstraints = FPBDCollisionConstraints;
		using FCollisionConstraintRule = TPBDConstraintColorRule<FCollisionConstraints>;
		using FCollisionDetector = FSpatialAccelerationCollisionDetector;
		using FExternalForces = FPerParticleExternalForces;
		using FJointConstraintsRule = TPBDConstraintIslandRule<FPBDJointConstraints>;
		using FSuspensionConstraintsRule = TPBDConstraintIslandRule<FPBDSuspensionConstraints>;
		using FJointConstraints = FPBDJointConstraints;
		using FJointConstraintRule = TPBDConstraintIslandRule<FJointConstraints>;

		// Default iteration counts
		static constexpr int32 DefaultNumIterations = 8;
		static constexpr int32 DefaultNumCollisionPairIterations = 1;
		static constexpr int32 DefaultNumPushOutIterations = 1;
		static constexpr int32 DefaultNumCollisionPushOutPairIterations = 1;
		static constexpr FRealSingle DefaultCollisionMarginFraction = 0.05f;
		static constexpr FRealSingle DefaultCollisionMarginMax = 10.0f;
		static constexpr FRealSingle DefaultCollisionCullDistance = 3.0f;
		static constexpr FRealSingle DefaultCollisionMaxPushOutVelocity = 1000.0f;
		static constexpr int32 DefaultNumJointPairIterations = 1;
		static constexpr int32 DefaultNumJointPushOutPairIterations = 1;
		static constexpr int32 DefaultRestitutionThreshold = 1000;

		CHAOS_API FPBDRigidsEvolutionGBF(FPBDRigidsSOAs& InParticles, THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials, const TArray<ISimCallbackObject*>* InCollisionModifiers = nullptr, bool InIsSingleThreaded = false);
		CHAOS_API ~FPBDRigidsEvolutionGBF();

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

		FORCEINLINE void DoInternalParticleInitilization(const FGeometryParticleHandle* OldParticle, FGeometryParticleHandle* NewParticle) 
		{ 
			if(InternalParticleInitilization)
			{
				InternalParticleInitilization(OldParticle, NewParticle);
			}
		}

		void SetIsDeterministic(const bool bInIsDeterministic);

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

		FORCEINLINE const FRigidClustering& GetRigidClustering() const { return Clustering; }
		FORCEINLINE FRigidClustering& GetRigidClustering() { return Clustering; }

		FORCEINLINE FJointConstraints& GetJointConstraints() { return JointConstraints; }
		FORCEINLINE const FJointConstraints& GetJointConstraints() const { return JointConstraints; }

		FORCEINLINE FPBDSuspensionConstraints& GetSuspensionConstraints() { return SuspensionConstraints; }
		FORCEINLINE const FPBDSuspensionConstraints& GetSuspensionConstraints() const { return SuspensionConstraints; }

		/**
		 * Reload the particles cache within an island
		 * @param Island Index of the island in which the cache will be used
		 */
		void ReloadParticlesCache(const int32 Island);

		/**
		 * Build the list of disables particles and update the sleeping flag on the island
		 * @param Island Index of the island in which the cache will be used
		 * @param DisabledParticles List of islands disabled particles
		 * @param SleepedIslands List of islands sleeping state 
		 */
		void BuildDisabledParticles(const int32 Island, TArray<TArray<FPBDRigidParticleHandle*>>& DisabledParticles, TArray<bool>& SleepedIslands);

		void DestroyConstraint(FConstraintHandle* Constraint);

		void DestroyParticleCollisionsInAllocator(FGeometryParticleHandle* Particle);

		CHAOS_API inline void EndFrame(FReal Dt)
		{
			Particles.GetNonDisabledDynamicView().ParallelFor([&](auto& Particle, int32 Index) {
				Particle.Acceleration() = FVec3(0);
				Particle.AngularAcceleration() = FVec3(0);
			});
		}

		template<typename TParticleView>
		void Integrate(const TParticleView& InParticles, FReal Dt)
		{
			//SCOPE_CYCLE_COUNTER(STAT_Integrate);
			CHAOS_SCOPED_TIMER(Integrate);

			const FReal BoundsThickness = GetNarrowPhase().GetBoundsExpansion();
			const FReal MaxAngularSpeedSq = CVars::HackMaxAngularVelocity * CVars::HackMaxAngularVelocity;
			const FReal MaxSpeedSq = CVars::HackMaxVelocity * CVars::HackMaxVelocity;
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

					//EulerStepVelocityRule.Apply(Particle, Dt);
					Particle.V() += Particle.Acceleration() * Dt;
					Particle.W() += Particle.AngularAcceleration() * Dt;


					//AddImpulsesRule.Apply(Particle, Dt);
					Particle.V() += Particle.LinearImpulseVelocity();
					Particle.W() += Particle.AngularImpulseVelocity();
					Particle.LinearImpulseVelocity() = FVec3(0);
					Particle.AngularImpulseVelocity() = FVec3(0);
					

					//EtherDragRule.Apply(Particle, Dt);
					{
						FVec3& V = Particle.V();
						FVec3& W = Particle.W();

						const FReal LinearDrag = LinearEtherDragOverride >= 0 ? LinearEtherDragOverride : Particle.LinearEtherDrag() * Dt;
						const FReal LinearMultiplier = FMath::Max(FReal(0), FReal(1) - LinearDrag);
						V *= LinearMultiplier;

						const FReal AngularDrag = AngularEtherDragOverride >= 0 ? AngularEtherDragOverride : Particle.AngularEtherDrag() * Dt;
						const FReal AngularMultiplier = FMath::Max(FReal(0), FReal(1) - AngularDrag);
						W *= AngularMultiplier;

						const FReal LinearSpeedSq = V.SizeSquared();
						const FReal AngularSpeedSq = W.SizeSquared();

						if (LinearSpeedSq > Particle.MaxLinearSpeedSq())
						{
							V *= FMath::Sqrt(Particle.MaxLinearSpeedSq() / LinearSpeedSq);
						}

						if (AngularSpeedSq > Particle.MaxAngularSpeedSq())
						{
							W *= FMath::Sqrt(Particle.MaxAngularSpeedSq() / AngularSpeedSq);
						}
					}

					if (CVars::HackMaxAngularVelocity >= 0.f)
					{
						const FReal AngularSpeedSq = Particle.W().SizeSquared();
						if (AngularSpeedSq > MaxAngularSpeedSq)
						{
							Particle.W() = Particle.W() * (CVars::HackMaxAngularVelocity / FMath::Sqrt(AngularSpeedSq));
						}
					}

					if (CVars::HackMaxVelocity >= 0.f)
					{
						const FReal SpeedSq = Particle.V().SizeSquared();
						if (SpeedSq > MaxSpeedSq)
						{
							Particle.V() = Particle.V() * (CVars::HackMaxVelocity / FMath::Sqrt(SpeedSq));
						}
					}

					//EulerStepRule.Apply(Particle, Dt);
					FVec3 PCoM = FParticleUtilitiesXR::GetCoMWorldPosition(&Particle);
					FRotation3 QCoM = FParticleUtilitiesXR::GetCoMWorldRotation(&Particle);

					PCoM = PCoM + Particle.V() * Dt;
					QCoM = FRotation3::IntegrateRotationWithAngularVelocity(QCoM, Particle.W(), Dt);

					FParticleUtilitiesPQ::SetCoMWorldTransform(&Particle, PCoM, QCoM);

					if (!Particle.CCDEnabled())
					{
						Particle.UpdateWorldSpaceState(FRigidTransform3(Particle.P(), Particle.Q()), FVec3(BoundsThickness));
					}
					else
					{
						const FReal MinBoundsAxis = Particle.LocalBounds().Extents().Min();
						const FReal LengthCCDThreshold = MinBoundsAxis *  CVars::CCDEnableThresholdBoundsScale;
						const FReal PXSizeSquared = (Particle.P() - Particle.X()).SizeSquared();
						if (PXSizeSquared > LengthCCDThreshold * LengthCCDThreshold)
						{
							if (CVars::bChaosCollisionCCDUseTightBoundingBox)
							{
								Particle.UpdateWorldSpaceStateSwept(FRigidTransform3(Particle.P(), Particle.Q()), FVec3(BoundsThickness), Particle.X() - Particle.P());
							}
							else
							{
								Particle.UpdateWorldSpaceState(FRigidTransform3(Particle.P(), Particle.Q()), FVec3(BoundsThickness) + Particle.V() * Dt);
							}
						}
						else
						{
							Particle.UpdateWorldSpaceState(FRigidTransform3(Particle.P(), Particle.Q()), FVec3(BoundsThickness));
						}
					}
				}
			});

			for (auto& Particle : InParticles)
			{
				Base::DirtyParticle(Particle);
			}
		}

		// First phase of constraint solver
		// For GBF this is the velocity solve phase
		// For PBD/QuasiPBD this is the position solve phase
		void ApplyConstraintsPhase1(const FReal Dt, int32 GroupIndex);

		// Calculate the implicit velocites based on the change in position from ApplyConstraintsPhase1
		void SetImplicitVelocities(const FReal Dt, int32 GroupIndex);
		
		// Second phase of constraint solver (after implicit velocity calculation following results of phase 1)
		// For GBF this is the pushout phase
		// For QuasiPBD this is the velocity solve phase
		void ApplyConstraintsPhase2(const FReal Dt, int32 GroupIndex);


		CHAOS_API void Serialize(FChaosArchive& Ar);

		CHAOS_API TUniquePtr<IResimCacheBase> CreateExternalResimCache() const;
		CHAOS_API void SetCurrentStepResimCache(IResimCacheBase* InCurrentStepResimCache);

		CHAOS_API FSpatialAccelerationBroadPhase& GetBroadPhase() { return BroadPhase; }
		CHAOS_API FNarrowPhase& GetNarrowPhase() { return NarrowPhase; }

		CHAOS_API void TransferJointConstraintCollisions();

	protected:

		CHAOS_API void AdvanceOneTimeStepImpl(const FReal dt, const FSubStepInfo& SubStepInfo);

		void GatherSolverInput(FReal Dt, int32 GroupIndex);
		void ScatterSolverOutput(FReal Dt, int32 GroupIndex);

		FEvolutionResimCache* GetCurrentStepResimCache()
		{
			return CurrentStepResimCacheImp;
		}

		FRigidClustering Clustering;

		FPBDJointConstraints JointConstraints;
		TPBDConstraintIslandRule<FPBDJointConstraints> JointConstraintRule;
		FPBDSuspensionConstraints SuspensionConstraints;
		TPBDConstraintIslandRule<FPBDSuspensionConstraints> SuspensionConstraintRule;

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

		FCCDManager CCDManager;

		bool bIsDeterministic;
	};

}
