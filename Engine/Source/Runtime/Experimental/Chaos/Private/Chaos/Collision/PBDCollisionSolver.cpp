// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionSolver.h"

#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/Utilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

bool bChaos_PBD_Collision_UseCorrectApplyPosition = true;	// Temp for testing
FAutoConsoleVariableRef CVarChaos_PBD_Collision_UseCorrectApplyPosition(TEXT("p.Chaos.PBD.Collision.UseCorrectApplyPosition"), bChaos_PBD_Collision_UseCorrectApplyPosition, TEXT(""));

bool bChaos_PBD_Collision_Position_SolveEnabled = true;
float Chaos_PBD_Collision_Position_MinStiffness = 1.0f;
float Chaos_PBD_Collision_Position_MaxStiffness = 1.0f;
float Chaos_PBD_Collision_Position_MinInvMassScale = 0.0f;
float Chaos_PBD_Collision_Position_StaticFrictionIterations = 0.5f;
float Chaos_PBD_Collision_Position_Tolerance = 0.1f;

FAutoConsoleVariableRef CVarChaos_PBD_Collision_Position_SolveEnabled(TEXT("p.Chaos.PBD.Collision.Position.SolveEnabled"), bChaos_PBD_Collision_Position_SolveEnabled, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Position_MinStiffness(TEXT("p.Chaos.PBD.Collision.Position.MinStiffness"), Chaos_PBD_Collision_Position_MinStiffness, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Position_MaxStiffness(TEXT("p.Chaos.PBD.Collision.Position.MaxStiffness"), Chaos_PBD_Collision_Position_MaxStiffness, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Position_MinInvMassScale(TEXT("p.Chaos.PBD.Collision.Position.MinInvMassScale"), Chaos_PBD_Collision_Position_MinInvMassScale, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Position_StaticFrictionIterations(TEXT("p.Chaos.PBD.Collision.Position.StaticFrictionIterations"), Chaos_PBD_Collision_Position_StaticFrictionIterations, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Position_Tolerance(TEXT("p.Chaos.PBD.Collision.Position.Tolerance"), Chaos_PBD_Collision_Position_Tolerance, TEXT(""));

bool bChaos_PBD_Collision_Velocity_SolveEnabled = true;
float Chaos_PBD_Collision_Velocity_MinStiffness = 1.0f;
float Chaos_PBD_Collision_Velocity_MaxStiffness = 1.0f;
bool bChaos_PBD_Collision_Velocity_DynamicFrictionEnabled = true;
bool bChaos_PBD_Collision_Velocity_ImpulseClampEnabled = false;


FAutoConsoleVariableRef CVarChaos_PBD_Collision_Velocity_SolveEnabled(TEXT("p.Chaos.PBD.Collision.Velocity.SolveEnabled"), bChaos_PBD_Collision_Velocity_SolveEnabled, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Velocity_MinStiffness(TEXT("p.Chaos.PBD.Collision.Velocity.MinStiffness"), Chaos_PBD_Collision_Velocity_MinStiffness, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Velocity_MaxStiffness(TEXT("p.Chaos.PBD.Collision.Velocity.MaxStiffness"), Chaos_PBD_Collision_Velocity_MaxStiffness, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Velocity_DynamicFrictionEnabled(TEXT("p.Chaos.PBD.Collision.Velocity.DynamicFrictionEnabled"), bChaos_PBD_Collision_Velocity_DynamicFrictionEnabled, TEXT(""));
FAutoConsoleVariableRef CVarChaos_PBD_Collision_Velocity_ImpulseClampEnabled(TEXT("p.Chaos.PBD.Collision.Velocity.ImpulseClampEnabled"), bChaos_PBD_Collision_Velocity_ImpulseClampEnabled, TEXT(""));


namespace Chaos
{
	inline FReal GetPositionSolveStiffness(int32 It, int32 NumIts)
	{
		if (NumIts > 1)
		{
			const FReal Interpolant = (FReal)It / (FReal)(NumIts - 1);
			return FMath::Lerp((FReal)Chaos_PBD_Collision_Position_MinStiffness, (FReal)Chaos_PBD_Collision_Position_MaxStiffness, Interpolant);
		}
		return Chaos_PBD_Collision_Position_MaxStiffness;
	}

	inline FReal GetPositionInvMassScale(int32 It, int32 NumIts)
	{
		if (NumIts > 1)
		{
			const FReal Interpolant = (FReal)It / (FReal)(NumIts - 1);
			return FMath::Lerp((FReal)1.0f, (FReal)Chaos_PBD_Collision_Position_MinInvMassScale, Interpolant);
		}
		return 1.0f;
	}

	inline FReal GetVelocitySolveStiffness(int32 It, int32 NumIts)
	{
		if (NumIts > 1)
		{
			const FReal Interpolant = (FReal)It / (FReal)(NumIts - 1);
			return FMath::Lerp((FReal)Chaos_PBD_Collision_Velocity_MinStiffness, (FReal)Chaos_PBD_Collision_Velocity_MaxStiffness, Interpolant);
		}
		return Chaos_PBD_Collision_Velocity_MaxStiffness;
	}

	inline FReal GetVelocityInvMassScale(int32 It, int32 NumIts)
	{
		return (FReal)1.0f;// GetPositionInvMassScale(It, NumIts);
	}


	void ApplyPositionCorrection(
		const FReal Stiffness,
		const Collisions::FContactIterationParameters& IterationParameters,
		const Collisions::FContactParticleParameters& ParticleParameters,
		const FVec3& ApplyContactPoint0,
		const FVec3& ApplyContactPoint1,
		const FVec3& ContactError,
		const FVec3& ContactNormal,
		const FMatrix33& ContactMass,
		const FReal ContactMassNormal,
		const FReal StaticFriction,
		const FReal InvM0,
		const FMatrix33& InvI0,
		const FReal InvM1,
		const FMatrix33& InvI1,
		FVec3& P0, // Centre of Mass Positions and Rotations
		FRotation3& Q0,
		FVec3& P1,
		FRotation3& Q1,
		FManifoldPoint& ManifoldPoint)
	{
		// Calculate the pushout required to correct the error for this iteration
		FVec3 PushOut = FVec3(0);
		if ((StaticFriction > 0) && ManifoldPoint.bInsideStaticFrictionCone)
		{
			PushOut = Stiffness * ContactMass * ContactError;
		}
		else
		{
			const FReal ContactErrorNormal = FVec3::DotProduct(ContactError, ContactNormal);
			PushOut = (Stiffness * ContactErrorNormal * ContactMassNormal) * ContactNormal;
		}

		// The total pushout so far this sub-step
		FVec3 NetPushOut = ManifoldPoint.NetPushOut + PushOut;
		FReal NetPushOutNormal = FVec3::DotProduct(NetPushOut, ContactNormal);

		// If we ended up with a negative normal pushout, stop
		if (NetPushOutNormal < 0)
		{
			PushOut = -ManifoldPoint.NetPushOut;
			NetPushOut = FVec3(0);
			NetPushOutNormal = 0.0f;
		}

		// If we exceed the friction cone, remove all tangential correction and rely on the velocity 
		// solve to add dynamic friction. Also disable static friction from now on.
		// Note: this will not result in the correct normal impulse, but it should be corrected
		// on a subsequent iteration (we could probably do better here)
		if ((StaticFriction > 0.0f) && (NetPushOutNormal > 0.0f) && ManifoldPoint.bInsideStaticFrictionCone)
		{
			const FReal MaxPushOutTangential = StaticFriction * NetPushOutNormal;

			// Current tangential correction
			const FVec3 NetPushOutTangential = NetPushOut - NetPushOutNormal * ContactNormal;
			const FReal NetPushOutTangentialLen = NetPushOutTangential.Size();

			// Check friction cone
			if (NetPushOutTangentialLen > MaxPushOutTangential)
			{
				ManifoldPoint.bInsideStaticFrictionCone = false;
				PushOut = PushOut - NetPushOutTangential;
				NetPushOut = ManifoldPoint.NetPushOut + PushOut;
			}
		}

		// Update the particle state based on the pushout
		if (InvM0 > SMALL_NUMBER)
		{
			const FVec3 AngularPushOut = FVec3::CrossProduct(ApplyContactPoint0, PushOut);
			const FVec3 DX0 = InvM0 * PushOut;
			const FVec3 DR0 = InvI0 * AngularPushOut;

			P0 += DX0;
			Q0 += FRotation3::FromElements(DR0, 0.f) * Q0 * FReal(0.5);
			Q0.Normalize();
		}
		if (InvM1 > SMALL_NUMBER)
		{
			const FVec3 AngularPushOut = FVec3::CrossProduct(ApplyContactPoint1, PushOut);
			const FVec3 DX1 = -(InvM1 * PushOut);
			const FVec3 DR1 = -(InvI1 * AngularPushOut);

			P1 += DX1;
			Q1 += FRotation3::FromElements(DR1, 0.f) * Q1 * FReal(0.5);
			Q1.Normalize();
		}

		ManifoldPoint.NetPushOut = NetPushOut;
		ManifoldPoint.bActive = (NetPushOutNormal > 0);
	}

	void ApplyVelocityCorrection(
		const FReal Stiffness,
		const Collisions::FContactIterationParameters& IterationParameters,
		const Collisions::FContactParticleParameters& ParticleParameters,
		const FVec3& ApplyContactPoint0,
		const FVec3& ApplyContactPoint1,
		const FVec3& ContactNormal,
		const FMatrix33& ContactMass,
		const FReal ContactMassNormal,
		const FReal DynamicFriction,
		const FReal Restitution,
		const FReal InvM0,
		const FMatrix33& InvI0,
		const FReal InvM1,
		const FMatrix33& InvI1,
		FVec3& V0,
		FVec3& W0,
		FVec3& V1,
		FVec3& W1,
		FManifoldPoint& ManifoldPoint)
	{
		const FVec3 ContactVelocity0 = V0 + FVec3::CrossProduct(W0, ApplyContactPoint0);
		const FVec3 ContactVelocity1 = V1 + FVec3::CrossProduct(W1, ApplyContactPoint1);
		const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
		const FReal ContactVelocityNormal = FVec3::DotProduct(ContactVelocity, ContactNormal);

		// Add up the errors in the velocity (current velocity - desired velocity)
		FVec3 ContactVelocityError = FVec3(0);
		FVec3 Impulse = FVec3(0);

		// Target normal velocity, including restitution
		FReal ContactVelocityTargetNormal = 0.0f;
		const bool bApplyRestitution = (Restitution > 0.0f) && (ManifoldPoint.InitialContactVelocity < -ParticleParameters.RestitutionVelocityThreshold);
		if (bApplyRestitution)
		{
			ContactVelocityTargetNormal = FMath::Max(0.0f, -Restitution * ManifoldPoint.InitialContactVelocity);
		}
		ContactVelocityError += (ContactVelocityNormal - ContactVelocityTargetNormal) * ContactNormal;

		// Tangential velocity (dynamic friction)
		const bool bApplyFriction = (DynamicFriction > 0) && (IterationParameters.Dt > 0);
		if (bApplyFriction)
		{
			const FVec3 ContactVelocityTangential = ContactVelocity - ContactVelocityNormal * ContactNormal;
			const FReal ContactVelocityTangentialLen = ContactVelocityTangential.Size();
			if (ContactVelocityTangentialLen > SMALL_NUMBER)
			{
				// PushOut = ContactMass * DP, where DP is the contact positional correction
				// Friction force is proportional to the normal force, so friction velocity correction
				// is proprtional to normal velocity correction, or DVn = DPn/dt = PushOut.N / (ContactMass * dt);
				const FReal PushOutNormal = FVec3::DotProduct(ManifoldPoint.NetPushOut, ContactNormal);
				const FReal DynamicFrictionVelocityError = PushOutNormal / (ContactMassNormal * IterationParameters.Dt);
				if (DynamicFrictionVelocityError > SMALL_NUMBER)
				{
					const FReal ContactVelocityErrorTangential = FMath::Min(DynamicFriction * DynamicFrictionVelocityError, ContactVelocityTangentialLen);
					ContactVelocityError += ContactVelocityTangential * (ContactVelocityErrorTangential / ContactVelocityTangentialLen);
				}
			}

			Impulse = -Stiffness * (ContactMass * ContactVelocityError);
		}
		else
		{
			Impulse = -(Stiffness * ContactMassNormal) * ContactVelocityError;
		}

		FVec3 NetImpulse = ManifoldPoint.NetImpulse + Impulse;

		// Clamp the total impulse to be positive along the normal
		// We can apply negative velocity correction, but only to correct the velocity that
		// was added by pushout, or in this velocity solve step.
		if (bChaos_PBD_Collision_Velocity_ImpulseClampEnabled)
		{
			const FReal PushOutVelocityNormal = FMath::Max(0.0f, FVec3::DotProduct(ManifoldPoint.NetPushOut, ContactNormal) / IterationParameters.Dt);
			if (FVec3::DotProduct(NetImpulse, ContactNormal) < -PushOutVelocityNormal)
			{
				Impulse = -ManifoldPoint.NetImpulse;
				NetImpulse = FVec3(0);
			}
		}

		// Calculate the velocity deltas from the impulse
		if (InvM0 > SMALL_NUMBER)
		{
			const FVec3 AngularImpulse = FVec3::CrossProduct(ApplyContactPoint0, Impulse);
			V0 += InvM0 * Impulse;
			W0 += InvI0 * AngularImpulse;
		}
		if (InvM1 > SMALL_NUMBER)
		{
			const FVec3 AngularImpulse = FVec3::CrossProduct(ApplyContactPoint1, Impulse);
			V1 -= InvM1 * Impulse;
			W1 -= InvI1 * AngularImpulse;
		}

		ManifoldPoint.NetImpulse = NetImpulse;
	}


	void FPBDCollisionSolver::SolvePosition(
		FRigidBodyPointContactConstraint& Constraint,
		const Collisions::FContactIterationParameters& IterationParameters,
		const Collisions::FContactParticleParameters& ParticleParameters)
	{
		if (!bChaos_PBD_Collision_Position_SolveEnabled)
		{
			return;
		}

		FGenericParticleHandle Particle0 = FGenericParticleHandle(Constraint.Particle[0]);
		FGenericParticleHandle Particle1 = FGenericParticleHandle(Constraint.Particle[1]);
		TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();

		// Lock bodies for shock propagation?
		bool IsTemporarilyStatic0 = false;
		bool IsTemporarilyStatic1 = false;
		//if (Chaos_Collision_UseShockPropagation)
		//{
		//	IsTemporarilyStatic0 = IsTemporarilyStatic.Contains(Particle0->GeometryParticleHandle());
		//	IsTemporarilyStatic1 = IsTemporarilyStatic.Contains(Particle1->GeometryParticleHandle());

		//	// In the case where two objects at the same level in shock propagation end
		//	// up in contact with each other, try to decide which one has higher priority.
		//	// For now, we use the one which is "lower" (in direction of gravity) as the frozen one.
		//	if (IsTemporarilyStatic0 && IsTemporarilyStatic1)
		//	{
		//		if (ManifoldPoints.Num() > 0)
		//		{
		//			const FReal NormalThreshold = 0.2f;	// @chaos(todo): what value here?
		//			const FReal NormalGrav = FVec3::DotProduct(ManifoldPoints[0].ContactPoint.Normal, GravityDir);
		//			const bool bNormalIsUp = NormalGrav < -NormalThreshold;
		//			const bool bNormalIsDown = NormalGrav > NormalThreshold;
		//			IsTemporarilyStatic0 = bNormalIsDown;
		//			IsTemporarilyStatic1 = bNormalIsUp;
		//		}
		//		else
		//		{
		//			IsTemporarilyStatic0 = false;
		//			IsTemporarilyStatic1 = false;
		//		}
		//	}
		//}

		TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
		TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
		const bool bIsRigidDynamic0 = PBDRigid0 && (PBDRigid0->ObjectState() == EObjectStateType::Dynamic) && (PBDRigid0->InvM() > SMALL_NUMBER) && !IsTemporarilyStatic0;
		const bool bIsRigidDynamic1 = PBDRigid1 && (PBDRigid1->ObjectState() == EObjectStateType::Dynamic) && (PBDRigid1->InvM() > SMALL_NUMBER) && !IsTemporarilyStatic1;

		if (!bIsRigidDynamic0 && !bIsRigidDynamic1)
		{
			return;
		}

		// Gradually increase position correction through iterations (optional based on cvars)
		const FReal Stiffness = GetPositionSolveStiffness(IterationParameters.Iteration, IterationParameters.NumIterations);

		FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
		FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
		FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
		FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

		// Apply the position correction so that all contacts have zero separation
		for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
		{
			FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

			// @todo(chaos): this should be set at initialization, but the old path requires it to be false by default and we want it to be true
			const int32 NumStaticFrictionIterations = FMath::CeilToInt((FReal)Chaos_PBD_Collision_Position_StaticFrictionIterations * (FReal)IterationParameters.NumIterations);
			if (IterationParameters.Iteration == 0)
			{
				ManifoldPoint.bInsideStaticFrictionCone = (NumStaticFrictionIterations > 0);
			}

			const bool bProcessManifoldPoint = (ManifoldPoint.ContactPoint.Phi < Chaos_PBD_Collision_Position_Tolerance) || !ManifoldPoint.NetPushOut.IsNearlyZero();
			if (bProcessManifoldPoint)
			{
				const FReal InvM0 = bIsRigidDynamic0 ? PBDRigid0->InvM() : 0.0f;
				const FReal InvM1 = bIsRigidDynamic1 ? PBDRigid1->InvM() : 0.0f;
				const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Constraint.Manifold.InvInertiaScale0 : FMatrix33(0);
				const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Constraint.Manifold.InvInertiaScale1 : FMatrix33(0);

				// Calculate the position error we need to correct, including static friction and restitution
				// Position correction uses the deepest point on each body (see velocity correction which uses average contact)
				const FRotation3& PlaneQ = (ManifoldPoint.ContactPoint.ContactNormalOwnerIndex == 0) ? Q0 : Q1;
				const FVec3 ContactNormal = PlaneQ * ManifoldPoint.CoMContactNormal;

				const int32 IterationsRemaining = IterationParameters.NumIterations - IterationParameters.Iteration;	// Including this one
				const bool bApplyStaticFriction = ManifoldPoint.bPotentialRestingContact && (IterationsRemaining <= NumStaticFrictionIterations);
				const FReal StaticFriction = bApplyStaticFriction ? FMath::Max(Constraint.Manifold.Friction, Constraint.Manifold.AngularFriction) : 0.0f;

				// Relative contact points on each body in world-space
				const FVec3 RelativeContactPoint0 = Q0 * ManifoldPoint.CoMContactPoints[0];
				const FVec3 RelativeContactPoint1 = Q1 * ManifoldPoint.CoMContactPoints[1];
				const FVec3 ContactPoint = FReal(0.5) * ((P1 + RelativeContactPoint1) + (P0 + RelativeContactPoint0));
				const FVec3 ApplyContactPoint0 = (bChaos_PBD_Collision_UseCorrectApplyPosition) ? ContactPoint - P0 : RelativeContactPoint0;
				const FVec3 ApplyContactPoint1 = (bChaos_PBD_Collision_UseCorrectApplyPosition) ? ContactPoint - P1 : RelativeContactPoint1;
				FVec3 ContactError = (P1 + RelativeContactPoint1) - (P0 + RelativeContactPoint0);
				
				// If we have static friction, we attempt to move the bodies so that the old contact positions are aligned
				if (bApplyStaticFriction)
				{
					FVec3 StaticFrictionLocalContactPoint0 = ManifoldPoint.CoMContactPoints[0];
					FVec3 StaticFrictionLocalContactPoint1 = ManifoldPoint.CoMContactPoints[1];
					Constraint.CalculatePrevCoMContactPoints(Particle0, Particle1, ManifoldPoint, IterationParameters.Dt, StaticFrictionLocalContactPoint0, StaticFrictionLocalContactPoint1);
					const FVec3 StaticFrictionRelativeContactPoint0 = Q0 * StaticFrictionLocalContactPoint0;
					const FVec3 StaticFrictionRelativeContactPoint1 = Q1 * StaticFrictionLocalContactPoint1;
					ContactError = (P1 + StaticFrictionRelativeContactPoint1) - (P0 + StaticFrictionRelativeContactPoint0);
				}

				// Temporary shock propagation implementation...
				const bool bUseMassScale = bIsRigidDynamic0 && bIsRigidDynamic1;
				const bool bOnTop0 = FVec3::DotProduct(ApplyContactPoint0, ContactNormal) < 0.0f;
				const FReal InvMassScale0 = (!bUseMassScale || bOnTop0) ? 1.0f : GetPositionInvMassScale(IterationParameters.Iteration, IterationParameters.NumIterations);
				const FReal InvMassScale1 = (bUseMassScale && bOnTop0) ? GetPositionInvMassScale(IterationParameters.Iteration, IterationParameters.NumIterations) : 1.0f;

				const FMatrix33 ContactMassInv =
					(bIsRigidDynamic0 ? Collisions::ComputeFactorMatrix3(ApplyContactPoint0, InvI0, InvM0) : FMatrix33(0)) +
					(bIsRigidDynamic1 ? Collisions::ComputeFactorMatrix3(ApplyContactPoint1, InvI1, InvM1) : FMatrix33(0));
				const FMatrix33 ContactMass = ContactMassInv.Inverse();
				const FReal ContactMassInvNormal = FVec3::DotProduct(ContactNormal, Utilities::Multiply(ContactMassInv, ContactNormal));
				const FReal ContactMassNormal = (ContactMassInvNormal > (FReal)SMALL_NUMBER) ? (FReal)1.0f / ContactMassInvNormal : (FReal)0.0f;

				ApplyPositionCorrection(
					Stiffness,
					IterationParameters,
					ParticleParameters,
					ApplyContactPoint0,
					ApplyContactPoint1,
					ContactError,
					ContactNormal,
					ContactMass,
					ContactMassNormal,
					StaticFriction,
					InvMassScale0 * InvM0, InvMassScale0 * InvI0, InvMassScale1 * InvM1, InvMassScale1 * InvI1,
					P0, Q0, P1, Q1,
					ManifoldPoint);
			}

			if (bIsRigidDynamic0)
			{
				FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
			}

			if (bIsRigidDynamic1)
			{
				FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
			}
		}

		// @todo(chaos): support early-out
		*IterationParameters.NeedsAnotherIteration = true;
	}

	void FPBDCollisionSolver::SolveVelocity(
		FRigidBodyPointContactConstraint& Constraint,
		const Collisions::FContactIterationParameters& IterationParameters,
		const Collisions::FContactParticleParameters& ParticleParameters)
	{
		FGenericParticleHandle Particle0 = FGenericParticleHandle(Constraint.Particle[0]);
		FGenericParticleHandle Particle1 = FGenericParticleHandle(Constraint.Particle[1]);

		TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
		TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
		const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
		const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

		const FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
		const FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
		const FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
		const FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
		FVec3 V0 = Particle0->V();
		FVec3 W0 = Particle0->W();
		FVec3 V1 = Particle1->V();
		FVec3 W1 = Particle1->W();

		const FReal InvM0 = bIsRigidDynamic0 ? PBDRigid0->InvM() : 0.0f;
		const FReal InvM1 = bIsRigidDynamic1 ? PBDRigid1->InvM() : 0.0f;
		const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Constraint.Manifold.InvInertiaScale0 : FMatrix33(0);
		const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Constraint.Manifold.InvInertiaScale1 : FMatrix33(0);

		//Constraint.AccumulatedImpulse = FVec3(0);

		if (bChaos_PBD_Collision_Velocity_SolveEnabled)
		{
			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			const FReal Stiffness = GetVelocitySolveStiffness(IterationParameters.Iteration, IterationParameters.NumIterations);

			// Only apply friction for 1 iteration - the last one
			const bool bApplyDynamicFriction = (IterationParameters.Iteration == IterationParameters.NumIterations - 1) && bChaos_PBD_Collision_Velocity_DynamicFrictionEnabled;

			for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
			{
				FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

				if (ManifoldPoint.bActive)
				{
					const FVec3 RelativeContactPoint0 = Q0 * ManifoldPoint.CoMContactPoints[0];
					const FVec3 RelativeContactPoint1 = Q1 * ManifoldPoint.CoMContactPoints[1];
					const FVec3 ContactPoint = FReal(0.5) * ((P1 + RelativeContactPoint1) + (P0 + RelativeContactPoint0));
					const FVec3 ApplyContactPoint0 = (bChaos_PBD_Collision_UseCorrectApplyPosition) ? ContactPoint - P0 : RelativeContactPoint0;
					const FVec3 ApplyContactPoint1 = (bChaos_PBD_Collision_UseCorrectApplyPosition) ? ContactPoint - P1 : RelativeContactPoint1;

					const FRotation3& PlaneQ = (ManifoldPoint.ContactPoint.ContactNormalOwnerIndex == 0) ? Q0 : Q1;
					const FVec3 ContactNormal = PlaneQ * ManifoldPoint.CoMContactNormal;

					// Temporary shock propagation implementation
					const bool bUseMassScale = bIsRigidDynamic0 && bIsRigidDynamic1;
					const bool bOnTop0 = FVec3::DotProduct(ApplyContactPoint0, ContactNormal) < 0.0f;
					const FReal InvMassScale0 = (!bUseMassScale || bOnTop0) ? 1.0f : GetVelocityInvMassScale(IterationParameters.Iteration, IterationParameters.NumIterations);
					const FReal InvMassScale1 = (bUseMassScale && bOnTop0) ? GetVelocityInvMassScale(IterationParameters.Iteration, IterationParameters.NumIterations) : 1.0f;

					const FMatrix33 ContactMassInv =
						(bIsRigidDynamic0 ? Collisions::ComputeFactorMatrix3(ApplyContactPoint0, InvI0, InvM0) : FMatrix33(0)) +
						(bIsRigidDynamic1 ? Collisions::ComputeFactorMatrix3(ApplyContactPoint1, InvI1, InvM1) : FMatrix33(0));
					const FMatrix33 ContactMass = ContactMassInv.Inverse();
					const FReal ContactMassInvNormal = FVec3::DotProduct(ContactNormal, Utilities::Multiply(ContactMassInv, ContactNormal));
					const FReal ContactMassNormal = (ContactMassInvNormal > (FReal)SMALL_NUMBER) ? (FReal)1.0f / ContactMassInvNormal : (FReal)0.0f;

					const FReal DynamicFriction = (bApplyDynamicFriction) ? Constraint.Manifold.Friction : (FReal)0.0f;

					const FReal Resitution = Constraint.Manifold.Restitution;

					ApplyVelocityCorrection(
						Stiffness,
						IterationParameters,
						ParticleParameters,
						ApplyContactPoint0,
						ApplyContactPoint1,
						ContactNormal,
						ContactMass,
						ContactMassNormal,
						DynamicFriction,
						Resitution,
						InvM0, InvI0, InvM1, InvI1,
						V0, W0, V1, W1,
						ManifoldPoint);

					//Constraint.AccumulatedImpulse += ManifoldPoint.NetImpulse;
				}
			}

			// @hack - disable dynamic friction after the first iteration so that pair iteration count doesn't impact the result - the formula is iteration-count sensitive (swith to XPBD)
			if (bApplyDynamicFriction)
			{
				Constraint.Manifold.Friction = 0;
			}

		}

		if (bIsRigidDynamic0)
		{
			PBDRigid0->V() = V0;
			PBDRigid0->W() = W0;
		}

		if (bIsRigidDynamic1)
		{
			PBDRigid1->V() = V1;
			PBDRigid1->W() = W1;
		}

		// @todo(chaos): support early-out
		*IterationParameters.NeedsAnotherIteration = true;
	}
}