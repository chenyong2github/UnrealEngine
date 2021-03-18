// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Evolution/PBDMinEvolution.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/ParticlePairCollisionDetector.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/PerParticleAddImpulses.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"

#if INTEL_ISPC
#include "PBDMinEvolution.ispc.generated.h"
#endif


//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	CHAOS_API DECLARE_LOG_CATEGORY_EXTERN(LogChaosMinEvolution, Log, Warning);
#else
	CHAOS_API DECLARE_LOG_CATEGORY_EXTERN(LogChaosMinEvolution, Log, All);
#endif
	DEFINE_LOG_CATEGORY(LogChaosMinEvolution);

	DECLARE_CYCLE_STAT(TEXT("MinEvolution::Advance"), STAT_MinEvolution_Advance, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::PrepareTick"), STAT_MinEvolution_PrepareTick, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::UnprepareTick"), STAT_MinEvolution_UnprepareTick, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::Rewind"), STAT_MinEvolution_Rewind, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::AdvanceOneTimeStep"), STAT_MinEvolution_AdvanceOneTimeStep, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::Integrate"), STAT_MinEvolution_Integrate, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::KinematicTargets"), STAT_MinEvolution_KinematicTargets, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::PrepareIteration"), STAT_MinEvolution_PrepareIteration, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::UnprepareIteration"), STAT_MinEvolution_UnprepareIteration, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::ApplyConstraints"), STAT_MinEvolution_ApplyConstraints, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::UpdateVelocities"), STAT_MinEvolution_UpdateVelocites, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::ApplyPushOut"), STAT_MinEvolution_ApplyPushOut, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::DetectCollisions"), STAT_MinEvolution_DetectCollisions, STATGROUP_ChaosMinEvolution);
	DECLARE_CYCLE_STAT(TEXT("MinEvolution::UpdatePositions"), STAT_MinEvolution_UpdatePositions, STATGROUP_ChaosMinEvolution);

	//
	//
	//

	bool bChaos_MinEvolution_RewindLerp = true;
	FAutoConsoleVariableRef CVarChaosMinEvolutionRewindLerp(TEXT("p.Chaos.MinEvolution.RewindLerp"), bChaos_MinEvolution_RewindLerp, TEXT("If rewinding (fixed dt mode) use Backwards-Lerp as opposed to Backwards Velocity"));

#if INTEL_ISPC
	int Chaos_MinEvolution_IntegrateMode = 0;
	FAutoConsoleVariableRef CVarChaosMinEvolutionIntegrateMode(TEXT("p.Chaos.MinEvolution.IntegrateMode"), Chaos_MinEvolution_IntegrateMode, TEXT(""));
#else
	const int Chaos_MinEvolution_IntegrateMode = 0;
#endif

	//
	//
	//

	struct FPBDRigidArrays
	{
		FPBDRigidArrays()
			: NumParticles(0)
		{
		}

		FPBDRigidArrays(TPBDRigidParticles<FReal, 3>& Dynamics)
		{
			NumParticles = Dynamics.Size();
			ObjectState = Dynamics.AllObjectState().GetData();
			X = Dynamics.AllX().GetData();
			P = Dynamics.AllP().GetData();
			R = Dynamics.AllR().GetData();
			Q = Dynamics.AllQ().GetData();
			V = Dynamics.AllV().GetData();
			PreV = Dynamics.AllPreV().GetData();
			W = Dynamics.AllW().GetData();
			PreW = Dynamics.AllPreW().GetData();
			CenterOfMass = Dynamics.AllCenterOfMass().GetData();
			RotationOfMass = Dynamics.AllRotationOfMass().GetData();
			InvM = Dynamics.AllInvM().GetData();
			InvI = Dynamics.AllInvI().GetData();
			F = Dynamics.AllF().GetData();
			T = Dynamics.AllT().GetData();
			LinearImpulse = Dynamics.AllLinearImpulse().GetData();
			AngularImpulse = Dynamics.AllAngularImpulse().GetData();
			Disabled = Dynamics.AllDisabled().GetData();
			GravityEnabled = Dynamics.AllGravityEnabled().GetData();
			LinearEtherDrag = Dynamics.AllLinearEtherDrag().GetData();
			AngularEtherDrag = Dynamics.AllAngularEtherDrag().GetData();
			HasBounds = Dynamics.AllHasBounds().GetData();
			LocalBounds = Dynamics.AllLocalBounds().GetData();
			WorldBounds = Dynamics.AllWorldSpaceInflatedBounds().GetData();
		}

		int32 NumParticles;
		EObjectStateType* ObjectState;
		FVec3* X;
		FVec3* P;
		FRotation3* R;
		FRotation3* Q;
		FVec3* V;
		FVec3* PreV;
		FVec3* W;
		FVec3* PreW;
		FVec3* CenterOfMass;
		FRotation3* RotationOfMass;
		FReal* InvM;
		FMatrix33* InvI;
		FVec3* F;
		FVec3* T;
		FVec3* LinearImpulse;
		FVec3* AngularImpulse;
		bool* Disabled;
		bool* GravityEnabled;
		FReal* LinearEtherDrag;
		FReal* AngularEtherDrag;
		bool* HasBounds;
		FAABB3* LocalBounds;
		FAABB3* WorldBounds;
	};


	//
	//
	//

	FPBDMinEvolution::FPBDMinEvolution(FRigidParticleSOAs& InParticles, TArrayCollectionArray<FVec3>& InPrevX, TArrayCollectionArray<FRotation3>& InPrevR, FCollisionDetector& InCollisionDetector, const FReal InBoundsExtension)
		: Particles(InParticles)
		, CollisionDetector(InCollisionDetector)
		, ParticlePrevXs(InPrevX)
		, ParticlePrevRs(InPrevR)
		, NumApplyIterations(0)
		, NumApplyPushOutIterations(0)
		, BoundsExtension(InBoundsExtension)
		, Gravity(FVec3(0))
		, SimulationSpaceSettings()
	{
#if INTEL_ISPC
		if (Chaos_MinEvolution_IntegrateMode == 2)
		{
			check((int32)EObjectStateType::Dynamic == (int32)ispc::ValueOfEObjectStateTypeDynamic())
			check(sizeof(FRigidTransform3) == ispc::SizeofFTransform());
			check(sizeof(FAABB3) == ispc::SizeofFAABB());
			check(sizeof(FPBDRigidArrays) == ispc::SizeofFPBDRigidArrays());
			check(sizeof(FSimulationSpace) == ispc::SizeofFSimulationSpace());
			check(sizeof(FSimulationSpaceSettings) == ispc::SizeofFSimulationSpaceSettings());
		}
#endif
	}

	void FPBDMinEvolution::AddConstraintRule(FSimpleConstraintRule* Rule)
	{
		ConstraintRules.Add(Rule);
	}

	void FPBDMinEvolution::Advance(const FReal StepDt, const int32 NumSteps, const FReal RewindDt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_Advance);

		PrepareTick();

		if (RewindDt > SMALL_NUMBER)
		{
			Rewind(StepDt, RewindDt);
		}

		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			// StepFraction: how much of the remaining time this step represents, used to interpolate kinematic targets
			// E.g., for 4 steps this will be: 1/4, 1/2, 3/4, 1
			const FReal StepFraction = (FReal)(Step + 1) / (NumSteps);

			UE_LOG(LogChaosMinEvolution, Verbose, TEXT("Advance dt = %f [%d/%d]"), StepDt, Step + 1, NumSteps);

			AdvanceOneTimeStep(StepDt, StepFraction);
		}

		for (TTransientPBDRigidParticleHandle<FReal, 3>& Particle : Particles.GetActiveParticlesView())
		{
			if (Particle.ObjectState() == EObjectStateType::Dynamic)
			{
				Particle.F() = FVec3(0);
				Particle.Torque() = FVec3(0);
			}
		}

		UnprepareTick();
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
			PrepareIteration(Dt);

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

			UnprepareIteration(Dt);

			UpdatePositions(Dt);
		}
	}

	// A opportunity for systems to allocate buffers for the duration of the tick, if they have enough info to do so
	void FPBDMinEvolution::PrepareTick()
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_PrepareTick);

		for (FSimpleConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->PrepareTick();
		}
	}

	void FPBDMinEvolution::UnprepareTick()
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_UnprepareTick);

		for (FSimpleConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UnprepareTick();
		}
	}

	// Update X/R as if we started the next tick 'RewindDt' seconds ago.
	void FPBDMinEvolution::Rewind(FReal Dt, FReal RewindDt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_Rewind);

		if (bChaos_MinEvolution_RewindLerp)
		{
			const FReal T = (Dt - RewindDt) / Dt;
			UE_LOG(LogChaosMinEvolution, Verbose, TEXT("Rewind dt = %f; rt = %f; T = %f"), Dt, RewindDt, T);
			for (TTransientPBDRigidParticleHandle<FReal, 3>& Particle : Particles.GetActiveParticlesView())
			{
				if (Particle.ObjectState() == EObjectStateType::Dynamic)
				{
					Particle.X() = FVec3::Lerp(Particle.Handle()->AuxilaryValue(ParticlePrevXs), Particle.X(), T);
					Particle.R() = FRotation3::Slerp(Particle.Handle()->AuxilaryValue(ParticlePrevRs), Particle.R(), T);
				}
			}
		}
		else
		{
			for (TTransientPBDRigidParticleHandle<FReal, 3>& Particle : Particles.GetActiveParticlesView())
			{
				if (Particle.ObjectState() == EObjectStateType::Dynamic)
				{
					const FVec3 XCoM = FParticleUtilitiesXR::GetCoMWorldPosition(&Particle);
					const FRotation3 RCoM = FParticleUtilitiesXR::GetCoMWorldRotation(&Particle);

					const FVec3 XCoM2 = XCoM - Particle.V() * RewindDt;
					const FRotation3 RCoM2 = FRotation3::IntegrateRotationWithAngularVelocity(RCoM, -Particle.W(), RewindDt);

					FParticleUtilitiesXR::SetCoMWorldTransform(&Particle, XCoM2, RCoM2);
				}
			}
		}

		for (auto& Particle : Particles.GetActiveKinematicParticlesView())
		{
			Particle.X() = Particle.X() - Particle.V() * RewindDt;
			Particle.R() = FRotation3::IntegrateRotationWithAngularVelocity(Particle.R(), -Particle.W(), RewindDt);
		}
	}

	// @todo(ccaulfield): dedupe (PBDRigidsEvolutionGBF)
	void FPBDMinEvolution::Integrate(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_Integrate);
		if (Chaos_MinEvolution_IntegrateMode == 0)
		{
			IntegrateImpl(Dt);
		}
		else if (Chaos_MinEvolution_IntegrateMode == 1)
		{
			IntegrateImpl2(Dt);
		}
		else if (Chaos_MinEvolution_IntegrateMode == 2)
		{
			IntegrateImplISPC(Dt);
		}
	}

	void FPBDMinEvolution::IntegrateImpl(FReal Dt)
	{
		// Simulation space velocity and acceleration
		FVec3 SpaceV = FVec3(0);	// Velocity
		FVec3 SpaceW = FVec3(0);	// Angular Velocity
		FVec3 SpaceA = FVec3(0);	// Acceleration
		FVec3 SpaceB = FVec3(0);	// Angular Acceleration
		if (SimulationSpaceSettings.MasterAlpha > 0.0f)
		{
			SpaceV = SimulationSpace.Transform.InverseTransformVector(SimulationSpace.LinearVelocity);
			SpaceW = SimulationSpace.Transform.InverseTransformVector(SimulationSpace.AngularVelocity);
			SpaceA = SimulationSpace.Transform.InverseTransformVector(SimulationSpace.LinearAcceleration);
			SpaceB = SimulationSpace.Transform.InverseTransformVector(SimulationSpace.AngularAcceleration);
		}

		for (TTransientPBDRigidParticleHandle<FReal, 3>& Particle : Particles.GetActiveParticlesView())
		{
			if (Particle.ObjectState() == EObjectStateType::Dynamic)
			{
				Particle.PreV() = Particle.V();
				Particle.PreW() = Particle.W();

				const FVec3 XCoM = FParticleUtilitiesXR::GetCoMWorldPosition(&Particle);
				const FRotation3 RCoM = FParticleUtilitiesXR::GetCoMWorldRotation(&Particle);
				
				// Forces and torques
				const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(RCoM, Particle.InvI());
				FVec3 DV = Particle.InvM() * (Particle.F() * Dt + Particle.LinearImpulse());
				FVec3 DW = Utilities::Multiply(WorldInvI, (Particle.Torque() * Dt + Particle.AngularImpulse()));
				FVec3 TargetV = FVec3(0);
				FVec3 TargetW = FVec3(0);

				// Gravity
				if (Particle.GravityEnabled())
				{
					DV += Gravity * Dt;
				}

				// Moving and accelerating simulation frame
				// https://en.wikipedia.org/wiki/Rotating_reference_frame
				if (SimulationSpaceSettings.MasterAlpha > 0.0f)
				{
					const FVec3 CoriolisAcc = SimulationSpaceSettings.CoriolisAlpha * 2.0f * FVec3::CrossProduct(SpaceW, Particle.V());
					const FVec3 CentrifugalAcc = SimulationSpaceSettings.CentrifugalAlpha * FVec3::CrossProduct(SpaceW, FVec3::CrossProduct(SpaceW, XCoM));
					const FVec3 EulerAcc = SimulationSpaceSettings.EulerAlpha * FVec3::CrossProduct(SpaceB, XCoM);
					const FVec3 LinearAcc = SimulationSpaceSettings.LinearAccelerationAlpha * SpaceA;
					const FVec3 AngularAcc = SimulationSpaceSettings.AngularAccelerationAlpha * SpaceB;
					const FVec3 LinearDragAcc = SimulationSpaceSettings.ExternalLinearEtherDrag * SpaceV;
					DV -= SimulationSpaceSettings.MasterAlpha * (LinearAcc + LinearDragAcc + CoriolisAcc + CentrifugalAcc + EulerAcc) * Dt;
					DW -= SimulationSpaceSettings.MasterAlpha * AngularAcc * Dt;
					TargetV = -SimulationSpaceSettings.MasterAlpha * SimulationSpaceSettings.LinearVelocityAlpha * SpaceV;
					TargetW = -SimulationSpaceSettings.MasterAlpha * SimulationSpaceSettings.AngularVelocityAlpha * SpaceW;
				}

				// New velocity
				const FReal LinearDrag = FMath::Min(FReal(1), Particle.LinearEtherDrag() * Dt);
				const FReal AngularDrag = FMath::Min(FReal(1), Particle.AngularEtherDrag() * Dt);
				const FVec3 V = FMath::Lerp(Particle.V() + DV, TargetV, LinearDrag);
				const FVec3 W = FMath::Lerp(Particle.W() + DW, TargetW, AngularDrag);

				// New position
				const FVec3 PCoM = XCoM + V * Dt;
				const FRotation3 QCoM = FRotation3::IntegrateRotationWithAngularVelocity(RCoM, W, Dt);

				// Update particle state (forces are not zeroed until the end of the frame)
				FParticleUtilitiesPQ::SetCoMWorldTransform(&Particle, PCoM, QCoM);
				Particle.V() = V;
				Particle.W() = W;
				Particle.LinearImpulse() = FVec3(0);
				Particle.AngularImpulse() = FVec3(0);

				// Update world-space bounds
				if (Particle.HasBounds())
				{
					const FAABB3& LocalBounds = Particle.LocalBounds();
					
					FAABB3 WorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(Particle.P(), Particle.Q()));
					WorldSpaceBounds.ThickenSymmetrically(WorldSpaceBounds.Extents() * BoundsExtension);

					// Dynamic bodies may get pulled back into their old positions by joints - make sure we find collisions that may prevent this
					// We could add the AABB at X/R here, but I'm avoiding another call to TransformedAABB. Hopefully this is good enough.
					WorldSpaceBounds.GrowByVector(Particle.X() - Particle.P());

					WorldSpaceBounds.ThickenSymmetrically(FVec3(CollisionDetector.GetBroadPhase().GetCullDistance()));

					Particle.SetWorldSpaceInflatedBounds(WorldSpaceBounds);
				}
			}
		}
	}

	void FPBDMinEvolution::IntegrateImpl2(FReal Dt)
	{
		FPBDRigidArrays Rigids = FPBDRigidArrays(Particles.GetDynamicParticles());

		// Simulation space velocity and acceleration
		FVec3 SpaceV = FVec3(0);	// Velocity
		FVec3 SpaceW = FVec3(0);	// Angular Velocity
		FVec3 SpaceA = FVec3(0);	// Acceleration
		FVec3 SpaceB = FVec3(0);	// Angular Acceleration
		if (SimulationSpaceSettings.MasterAlpha > 0.0f)
		{
			SpaceV = SimulationSpace.Transform.InverseTransformVector(SimulationSpace.LinearVelocity);
			SpaceW = SimulationSpace.Transform.InverseTransformVector(SimulationSpace.AngularVelocity);
			SpaceA = SimulationSpace.Transform.InverseTransformVector(SimulationSpace.LinearAcceleration);
			SpaceB = SimulationSpace.Transform.InverseTransformVector(SimulationSpace.AngularAcceleration);
		}

		for (int32 ParticleIndex = 0; ParticleIndex < Rigids.NumParticles; ++ParticleIndex)
		{
			if (!Rigids.Disabled[ParticleIndex] && (Rigids.ObjectState[ParticleIndex] == EObjectStateType::Dynamic))
			{
				Rigids.PreV[ParticleIndex] = Rigids.V[ParticleIndex];
				Rigids.PreW[ParticleIndex] = Rigids.W[ParticleIndex];

				const FVec3 XCoM = Rigids.X[ParticleIndex] + Rigids.R[ParticleIndex].RotateVector(Rigids.CenterOfMass[ParticleIndex]);
				const FRotation3 RCoM = Rigids.R[ParticleIndex] * Rigids.RotationOfMass[ParticleIndex];

				// Forces and torques
				const FMatrix33 WorldInvI = Utilities::ComputeWorldSpaceInertia(RCoM, Rigids.InvI[ParticleIndex]);
				FVec3 DV = Rigids.InvM[ParticleIndex] * (Rigids.F[ParticleIndex] * Dt + Rigids.LinearImpulse[ParticleIndex]);
				FVec3 DW = WorldInvI * (Rigids.T[ParticleIndex] * Dt + Rigids.AngularImpulse[ParticleIndex]);
				FVec3 TargetV = FVec3(0);
				FVec3 TargetW = FVec3(0);

				// Gravity
				if (Rigids.GravityEnabled[ParticleIndex])
				{
					DV += Gravity * Dt;
				}

				// Moving and accelerating simulation frame
				// https://en.wikipedia.org/wiki/Rotating_reference_frame
				if (SimulationSpaceSettings.MasterAlpha > 0.0f)
				{
					const FVec3 CoriolisAcc = SimulationSpaceSettings.CoriolisAlpha * 2.0f * FVec3::CrossProduct(SpaceW, Rigids.V[ParticleIndex]);
					const FVec3 CentrifugalAcc = SimulationSpaceSettings.CentrifugalAlpha * FVec3::CrossProduct(SpaceW, FVec3::CrossProduct(SpaceW, XCoM));
					const FVec3 EulerAcc = SimulationSpaceSettings.EulerAlpha * FVec3::CrossProduct(SpaceB, XCoM);
					const FVec3 LinearAcc = SimulationSpaceSettings.LinearAccelerationAlpha * SpaceA;
					const FVec3 AngularAcc = SimulationSpaceSettings.AngularAccelerationAlpha * SpaceB;
					const FVec3 LinearDragAcc = SimulationSpaceSettings.ExternalLinearEtherDrag * SpaceV;
					DV -= SimulationSpaceSettings.MasterAlpha * (LinearAcc + LinearDragAcc + CoriolisAcc + CentrifugalAcc + EulerAcc) * Dt;
					DW -= SimulationSpaceSettings.MasterAlpha * AngularAcc * Dt;
					TargetV = -SimulationSpaceSettings.MasterAlpha * SimulationSpaceSettings.LinearVelocityAlpha * SpaceV;
					TargetW = -SimulationSpaceSettings.MasterAlpha * SimulationSpaceSettings.AngularVelocityAlpha * SpaceW;
				}

				// New velocity
				const FReal LinearDrag = FMath::Min(FReal(1), Rigids.LinearEtherDrag[ParticleIndex] * Dt);
				const FReal AngularDrag = FMath::Min(FReal(1), Rigids.AngularEtherDrag[ParticleIndex] * Dt);
				const FVec3 VCoM = FMath::Lerp(Rigids.V[ParticleIndex] + DV, TargetV, LinearDrag);
				const FVec3 WCoM = FMath::Lerp(Rigids.W[ParticleIndex] + DW, TargetW, AngularDrag);

				// New position
				const FVec3 PCoM = XCoM + VCoM * Dt;
				const FRotation3 QCoM = FRotation3::IntegrateRotationWithAngularVelocity(RCoM, WCoM, Dt);

				// Update particle state (forces are not zeroed until the end of the frame)
				const FRotation3 QActor = QCoM * Rigids.RotationOfMass[ParticleIndex].Inverse();
				const FVec3 PActor = PCoM - QActor.RotateVector(Rigids.CenterOfMass[ParticleIndex]);
				Rigids.P[ParticleIndex] = PActor;
				Rigids.Q[ParticleIndex] = QActor;

				Rigids.V[ParticleIndex] = VCoM;
				Rigids.W[ParticleIndex] = WCoM;
				Rigids.LinearImpulse[ParticleIndex] = FVec3(0);
				Rigids.AngularImpulse[ParticleIndex] = FVec3(0);

				// Update world-space bounds
				if (Rigids.HasBounds[ParticleIndex])
				{
					FAABB3 WorldSpaceBounds = Rigids.LocalBounds[ParticleIndex].TransformedAABB(FRigidTransform3(Rigids.P[ParticleIndex], Rigids.Q[ParticleIndex]));
					WorldSpaceBounds.ThickenSymmetrically(WorldSpaceBounds.Extents() * BoundsExtension);

					// Dynamic bodies may get pulled back into their old positions by joints - make sure we find collisions that may prevent this
					// We could add the AABB at X/R here, but I'm avoiding another call to TransformedAABB. Hopefully this is good enough.
					WorldSpaceBounds.GrowByVector(Rigids.X[ParticleIndex] - Rigids.P[ParticleIndex]);

					WorldSpaceBounds.ThickenSymmetrically(FVec3(CollisionDetector.GetBroadPhase().GetCullDistance()));

					Rigids.WorldBounds[ParticleIndex] = WorldSpaceBounds;
				}
			}
		}

		// @todo(ccaulfield): See SetWorldSpaceInflatedBounds - it does some extra stuff that seems suspect
		for (int32 ParticleIndex = 0; ParticleIndex < Rigids.NumParticles; ++ParticleIndex)
		{
			if (!Rigids.Disabled[ParticleIndex] && (Rigids.ObjectState[ParticleIndex] == EObjectStateType::Dynamic))
			{
				if (Rigids.HasBounds[ParticleIndex])
				{
					Particles.GetDynamicParticles().Handle(ParticleIndex)->SetWorldSpaceInflatedBounds(Rigids.WorldBounds[ParticleIndex]);
				}
			}
		}
	}


	void FPBDMinEvolution::IntegrateImplISPC(FReal Dt)
	{
		check(bRealTypeCompatibleWithISPC);
#if INTEL_ISPC
		FPBDRigidArrays Rigids = FPBDRigidArrays(Particles.GetDynamicParticles());
		ispc::MinEvolutionIntegrate(Dt, (ispc::FPBDRigidArrays&)Rigids, (ispc::FSimulationSpace&)SimulationSpace, (ispc::FSimulationSpaceSettings&)SimulationSpaceSettings, (ispc::FVector&)Gravity, BoundsExtension, CollisionDetector.GetBroadPhase().GetCullDistance());

		// @todo(ccaulfield): move to ispc
		for (int32 ParticleIndex = 0; ParticleIndex < Rigids.NumParticles; ++ParticleIndex)
		{
			if (!Rigids.Disabled[ParticleIndex] && (Rigids.ObjectState[ParticleIndex] == EObjectStateType::Dynamic))
			{
				if (Rigids.HasBounds[ParticleIndex])
				{
					// @todo(ccaulfield): See SetWorldSpaceInflatedBounds - it does some extra stuff that seems suspect
					Particles.GetDynamicParticles().Handle(ParticleIndex)->SetWorldSpaceInflatedBounds(Rigids.WorldBounds[ParticleIndex]);
				}
			}
		}
#else
		IntegrateImpl(Dt);
#endif
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

			const TRigidTransform<FReal, 3>& Previous = KinematicTarget.GetPrevious();
			const FVec3 PrevX = Previous.GetTranslation();
			const FRotation3 PrevR = Previous.GetRotation();


			switch (KinematicTarget.GetMode())
			{
			case EKinematicTargetMode::None:
				// Nothing to do
				break;

			case EKinematicTargetMode::Reset:
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
					KinematicTarget.SetMode(EKinematicTargetMode::Reset);
				}
				else
				{
					TargetPos = FVec3::Lerp(PrevX, KinematicTarget.GetTarget().GetLocation(), StepFraction);
					TargetRot = FRotation3::Slerp(PrevR, KinematicTarget.GetTarget().GetRotation(), StepFraction);
				}
				if (Dt > MinDt)
				{
					Particle.V() = FVec3::CalculateVelocity(PrevX, TargetPos, Dt);
					Particle.W() = FRotation3::CalculateAngularVelocity(PrevR, TargetRot, Dt);
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

			// Update world space bouunds
			if (Particle.HasBounds())
			{
				const FAABB3& LocalBounds = Particle.LocalBounds();
				
				FAABB3 WorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(Particle.X(), Particle.R()));
				WorldSpaceBounds.ThickenSymmetrically(WorldSpaceBounds.Extents() * BoundsExtension);

				//FAABB3 PrevWorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(PrevX, PrevR));
				//WorldSpaceBounds.GrowToInclude(PrevWorldSpaceBounds);

				Particle.SetWorldSpaceInflatedBounds(WorldSpaceBounds);
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

	void FPBDMinEvolution::PrepareIteration(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_PrepareIteration);

		for (FSimpleConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->PrepareIteration(Dt);
		}
	}

	void FPBDMinEvolution::UnprepareIteration(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_UnprepareIteration);

		for (FSimpleConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UnprepareIteration(Dt);
		}
	}

	void FPBDMinEvolution::ApplyConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_ApplyConstraints);

		for (int32 i = 0; i < NumApplyIterations; ++i)
		{
			bool bNeedsAnotherIteration = false;
			for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
			{
				bNeedsAnotherIteration |= ConstraintRule->ApplyConstraints(Dt, i, NumApplyIterations);
			}

			if (!bNeedsAnotherIteration)
			{
				break;
			}
		}
	}

	void FPBDMinEvolution::UpdateVelocities(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_UpdateVelocites);

		FPerParticlePBDUpdateFromDeltaPosition UpdateVelocityRule;
		for (auto& Particle : Particles.GetActiveParticlesView())
		{
			UpdateVelocityRule.Apply(Particle, Dt);
		}
	}

	void FPBDMinEvolution::ApplyPushOutConstraints(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_ApplyPushOut);

		for (int32 It = 0; It < NumApplyPushOutIterations; ++It)
		{
			bool bNeedsAnotherIteration = false;
			for (FSimpleConstraintRule* ConstraintRule : PrioritizedConstraintRules)
			{
				bNeedsAnotherIteration |= ConstraintRule->ApplyPushOut(Dt, It, NumApplyPushOutIterations);
			}

			if (!bNeedsAnotherIteration)
			{
				break;
			}
		}
	}

	void FPBDMinEvolution::UpdatePositions(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_MinEvolution_UpdatePositions);
		for (auto& Particle : Particles.GetActiveParticlesView())
		{
			Particle.Handle()->AuxilaryValue(ParticlePrevXs) = Particle.X();
			Particle.Handle()->AuxilaryValue(ParticlePrevRs) = Particle.R();
			Particle.X() = Particle.P();
			Particle.R() = Particle.Q();
		}
	}

}
