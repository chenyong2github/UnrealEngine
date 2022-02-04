// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/CollisionSolver.h"

#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Defines.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

#if INTEL_ISPC
#include "PBDCollisionConstraints.ispc.generated.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

CHAOS_API DECLARE_LOG_CATEGORY_EXTERN(LogChaosCollisionSolver, Log, All);
DEFINE_LOG_CATEGORY(LogChaosCollisionSolver);

namespace Chaos
{
	namespace CVars
	{
		extern int32 Chaos_Collision_UseShockPropagation;
		extern FRealSingle Chaos_Collision_CollisionClipTolerance;

		bool bChaos_Manifold_Apply_AllowNegativeIncrementalImpulse = true;
		FAutoConsoleVariableRef CVarChaos_Manifold_NegativeApply(TEXT("p.Chaos.Collision.Manifold.Apply.NegativeIncrementalImpulse"), bChaos_Manifold_Apply_AllowNegativeIncrementalImpulse, TEXT(""));

		bool Chaos_Manifold_PushOut_NegativePushOut = false;
		bool Chaos_Manifold_PushOut_StaticFriction = true;
		bool Chaos_Manifold_PushOut_Restitution = false;
		bool Chaos_Manifold_PushOut_PositionCorrection = true;
		int32 Chaos_Manifold_PushOut_VelocityCorrection = 2;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_NegativePushOut(TEXT("p.Chaos.Collision.Manifold.PushOut.NegativePushOut"), Chaos_Manifold_PushOut_NegativePushOut, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_StaticFriction(TEXT("p.Chaos.Collision.Manifold.PushOut.StaticFriction"), Chaos_Manifold_PushOut_StaticFriction, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_Restitution(TEXT("p.Chaos.Collision.Manifold.PushOut.Restitution"), Chaos_Manifold_PushOut_Restitution, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_PositionCorrection(TEXT("p.Chaos.Collision.Manifold.PushOut.PositionCorrection"), Chaos_Manifold_PushOut_PositionCorrection, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_VelocityCorrection(TEXT("p.Chaos.Collision.Manifold.PushOut.VelocityCorrectionMode"), Chaos_Manifold_PushOut_VelocityCorrection, TEXT("0 = No Velocity Correction; 1 = Normal Velocity Correction; 2 = Normal + Tangential Velocity Correction"));

		// Note: Velocity solve requires full stiffness for restitution to work correctly so the max stiffness should be 1
		FRealSingle Chaos_Manifold_Apply_MinStiffness = 0.25f;
		FRealSingle Chaos_Manifold_Apply_MaxStiffness = 1.0f;
		FAutoConsoleVariableRef CVarChaos_Manifold_Apply_MinStiffness(TEXT("p.Chaos.Collision.Manifold.Apply.MinStiffness"), Chaos_Manifold_Apply_MinStiffness, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_Apply_MaxStiffness(TEXT("p.Chaos.Collision.Manifold.Apply.MaxStiffness"), Chaos_Manifold_Apply_MaxStiffness, TEXT(""));

		FRealSingle Chaos_Manifold_PushOut_MinStiffness = 0.25f;
		FRealSingle Chaos_Manifold_PushOut_MaxStiffness = 0.5f;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_MinStiffness(TEXT("p.Chaos.Collision.Manifold.PushOut.MinStiffness"), Chaos_Manifold_PushOut_MinStiffness, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_MaxStiffness(TEXT("p.Chaos.Collision.Manifold.PushOut.MaxStiffness"), Chaos_Manifold_PushOut_MaxStiffness, TEXT(""));

		float Chaos_ConstraintStiffness_Modifier = 5.0;
		FAutoConsoleVariableRef CVarChaos_ConstraintStiffness_Modifier(TEXT("p.Chaos.Collision.ConstraintStiffnessModifier"), Chaos_ConstraintStiffness_Modifier, TEXT(""));

		FRealSingle Chaos_Manifold_Apply_ImpulseTolerance = 1.e-4f;
		FAutoConsoleVariableRef CVarChaos_Manifold_Apply_ImpulseTolerance(TEXT("p.Chaos.Collision.Manifold.Apply.ImpulseTolerance"), Chaos_Manifold_Apply_ImpulseTolerance, TEXT(""));

		float Chaos_Manifold_PushOut_PositionTolerance = 1.e-4f;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_PositionTolerance(TEXT("p.Chaos.Collision.Manifold.PushOut.PositionTolerance"), Chaos_Manifold_PushOut_PositionTolerance, TEXT(""));
	}
	using namespace CVars;

	namespace Collisions
	{
		inline FReal GetConstraintStiffness(const FReal InStiffness, const FContactIterationParameters& IterationParameters)
		{
			FReal Stiffness = FMath::Clamp(InStiffness, (FReal)0.0, (FReal)1.0);
			if (Stiffness < 1.0)
			{
				FReal Factor = (FReal)Chaos_ConstraintStiffness_Modifier * (FReal)IterationParameters.NumIterations * (FReal)IterationParameters.NumPairIterations;
				if (!FMath::IsNearlyZero(Factor))
				{
					return Stiffness / Factor;
				}
			}
			return (FReal)1;
		}

		inline FReal GetApplyStiffness(int32 It, int32 NumIts)
		{
			const FReal Interpolant = (FReal)(It + 1) / (FReal)NumIts;
			return FMath::Lerp(Chaos_Manifold_Apply_MinStiffness, Chaos_Manifold_Apply_MaxStiffness, Interpolant);
		}

		inline FReal GetPushOutStiffness(int32 It, int32 NumIts)
		{
			const FReal Interpolant = (FReal)(It + 1) / (FReal)NumIts;
			return FMath::Lerp(Chaos_Manifold_PushOut_MinStiffness, Chaos_Manifold_PushOut_MaxStiffness, Interpolant);
		}


		// Calculate the impulse to drive normal contact velocity to zero (or positive for restitution), ignoring friction
		// Some state on the in/out Manifold point is also modified for use in the push out phase.
		void CalculateManifoldNormalVelocityCorrection(
			const FReal Stiffness,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const FReal Restitution,
			FSolverBody& Body0,
			FSolverBody& Body1,
			FManifoldPoint& ManifoldPoint)
		{
		#if 0
			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - Body0.P();
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - Body1.P();
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			const FReal ContactPhi = ManifoldPoint.ContactPoint.Phi;

			// Reject non-contact points unless the point has previously been processed - we may want to undo some of the previous work
			// The tolerance is so that we catch collisions that were resolved last frame, but resulted in slightly positive separations. However,
			// it also means we are effectively padding objects by this amount.
			if ((ContactPhi > Chaos_Collision_CollisionClipTolerance) && (ManifoldPoint.InitialPhi > Chaos_Collision_CollisionClipTolerance) && !ManifoldPoint.bActive)
			{
				return;
			}

			const FVec3 ContactVelocity0 = Body0.V() + FVec3::CrossProduct(Body0.W(), RelativeContactPoint0);
			const FVec3 ContactVelocity1 = Body1.V() + FVec3::CrossProduct(Body1.W(), RelativeContactPoint1);
			const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
			const FReal ContactVelocityNormalLen = FVec3::DotProduct(ContactVelocity, ContactNormal);

			// Reject contacts moving apart unless the point has previously been processed, in which case we may want to undo some of the previous work
			const bool bAllowNegativeImpulse = ManifoldPoint.bActive && bChaos_Manifold_Apply_AllowNegativeIncrementalImpulse;
			if ((ContactVelocityNormalLen > 0.0f) && !bAllowNegativeImpulse)
			{
				return;
			}

			// Target normal velocity, including restitution
			const bool bApplyRestitution = (Restitution > 0.0f) && (ManifoldPoint.InitialContactVelocity < -ParticleParameters.RestitutionVelocityThreshold);
			FReal ContactVelocityTargetNormal = 0.0f;
			if (bApplyRestitution)
			{
				ContactVelocityTargetNormal = FMath::Max((FReal)0., -Restitution * ManifoldPoint.InitialContactVelocity);
			}

			// Calculate constraint-space mass
			const FMatrix33 ConstraintMassInv =
				(Body0.IsDynamic() ? ComputeFactorMatrix3(RelativeContactPoint0, Body0.InvI(), Body0.InvM()) : FMatrix33(0)) +
				(Body1.IsDynamic() ? ComputeFactorMatrix3(RelativeContactPoint1, Body1.InvI(), Body1.InvM()) : FMatrix33(0));

			// Calculate the impulse required to drive normal contact velocity to zero
			const FReal ContactVelocityNormalChange = (ContactVelocityTargetNormal - ContactVelocityNormalLen);
			const FReal ConstraintMassNormalInv = FVec3::DotProduct(ContactNormal, ConstraintMassInv * ContactNormal);
			if (ConstraintMassNormalInv < SMALL_NUMBER)
			{
				return;
			}
			FVec3 Impulse = (Stiffness * ContactVelocityNormalChange / ConstraintMassNormalInv) * ContactNormal;

			// Clip the impulse so that the accumulated impulse is not in the wrong direction
			FVec3 NetImpulse = ManifoldPoint.NetImpulse + Impulse;

			// Clamp the total impulse to be positive along the normal
			// Note: if we go inactive we also undo all friction
			const bool bActive = FVec3::DotProduct(NetImpulse, ContactNormal) > 0.0f;
			if (!bActive)
			{
				Impulse = -ManifoldPoint.NetImpulse;
				NetImpulse = FVec3(0);
			}

			// Calculate the velocity deltas from the impulse
			if (Body0.IsDynamic())
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				const FVec3 DV0 = Body0.InvM() * Impulse;
				const FVec3 DW0 = Body0.InvI() * AngularImpulse;
				Body0.ApplyVelocityDelta(DV0, DW0);
			}
			if (Body1.IsDynamic())
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, Impulse);
				const FVec3 DV1 = Body1.InvM() * -Impulse;
				const FVec3 DW1 = Body1.InvI() * -AngularImpulse;
				Body1.ApplyVelocityDelta(DV1, DW1);
			}

			// PushOut needs to know if we applied restitution and static friction
			ManifoldPoint.bActive = bActive;
			ManifoldPoint.bInsideStaticFrictionCone = bActive && ManifoldPoint.bInsideStaticFrictionCone;					// Reset when not active
			ManifoldPoint.bRestitutionEnabled = bActive && (bApplyRestitution || ManifoldPoint.bRestitutionEnabled);		// Latches to on-state when active
			ManifoldPoint.NetImpulse = NetImpulse;

			// If we applied any additional impulse, we need to go again next iteration
			const FReal ImpulseTolerance = Chaos_Manifold_Apply_ImpulseTolerance * Chaos_Manifold_Apply_ImpulseTolerance;
			*IterationParameters.NeedsAnotherIteration |= (Impulse.SizeSquared() > ImpulseTolerance);
#endif
		}

		void CalculateManifoldFrictionVelocityCorrection(
			const FReal Stiffness,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const FReal StaticFriction,
			const FReal DynamicFriction,
			FSolverBody& Body0,
			FSolverBody& Body1,
			FManifoldPoint& ManifoldPoint)
		{
#if 0
			// Reject inactive contacts (no normal impulse)
			if (!ManifoldPoint.bActive || (StaticFriction == 0.0f))
			{
				return;
			}

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - Body0.P();
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - Body1.P();
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;

			const FVec3 ContactVelocity0 = Body0.V() + FVec3::CrossProduct(Body0.W(), RelativeContactPoint0);
			const FVec3 ContactVelocity1 = Body1.V() + FVec3::CrossProduct(Body1.W(), RelativeContactPoint1);
			const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
			const FReal ContactVelocityNormalLen = FVec3::DotProduct(ContactVelocity, ContactNormal);
			const FVec3 ContactVelocityTangent = ContactVelocity - ContactVelocityNormalLen * ContactNormal;
			const FReal ContactVelocityTangentLen = ContactVelocityTangent.Size();
				
			// Note: We cannot early-out at low lateral contact velocity because we may have reduced the normal
			// impulse in a later iteration, which means we may need to reduce the friction impulse to get
			// it back into the friction cone. However, we can skip the incremental impulse calculation
			// and go straight to the friction cone clamping (on net impulse).
			FVec3 Impulse = FVec3(0);
			const FReal FrictionVelocityThreshold = 1.e-5f;
			if (ContactVelocityTangentLen > FrictionVelocityThreshold)
			{
				const FVec3 ContactTangent = ContactVelocityTangent / ContactVelocityTangentLen;

				// Calculate constraint-space mass
				const FMatrix33 ConstraintMassInv =
					(Body0.IsDynamic() ? ComputeFactorMatrix3(RelativeContactPoint0, Body0.InvI(), Body0.InvM()) : FMatrix33(0)) +
					(Body1.IsDynamic() ? ComputeFactorMatrix3(RelativeContactPoint1, Body1.InvI(), Body1.InvM()) : FMatrix33(0));

				// Calculate the impulse required to drive contact velocity to zero along the tangent
				const FReal ContactVelocityTangentChange = -ContactVelocityTangentLen;
				const FReal ConstraintMassTangentInv = FVec3::DotProduct(ContactTangent, ConstraintMassInv * ContactTangent);
				const FReal ImpulseTangentLen = (Stiffness * ContactVelocityTangentChange / ConstraintMassTangentInv);
				Impulse = ImpulseTangentLen * ContactTangent;
			}

			// Clamp the tangential impulse to the friction cone (looking at net impulse over all iterations)
			bool bInsideStaticFrictionCone = true;
			const FReal NetImpulseNormalLen = FVec3::DotProduct(ManifoldPoint.NetImpulse, ContactNormal);
			const FVec3 NetImpulseTangent = ManifoldPoint.NetImpulse - NetImpulseNormalLen * ContactNormal;
			const FVec3 NewNetImpulseTangent = NetImpulseTangent + Impulse;
			const FReal NewNetImpulseTangentLen = NewNetImpulseTangent.Size();
			const FReal MaxStaticImpulseTangentLen = StaticFriction * NetImpulseNormalLen;
			if (NewNetImpulseTangentLen > MaxStaticImpulseTangentLen)
			{
				const FReal MaxDynamicImpulseTangentLen = DynamicFriction * NetImpulseNormalLen;
				const FVec3 ClampedNewNetImpulseTangent = (MaxDynamicImpulseTangentLen / NewNetImpulseTangentLen) * NewNetImpulseTangent;

				Impulse = ClampedNewNetImpulseTangent - NetImpulseTangent;
				bInsideStaticFrictionCone = false;
			}
			const FVec3 NetImpulse = ManifoldPoint.NetImpulse + Impulse;

			// Calculate the velocity deltas from the impulse
			if (Body0.IsDynamic())
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				const FVec3 DV0 = Body0.InvM() * Impulse;
				const FVec3 DW0 = Body0.InvI() * AngularImpulse;
				Body0.ApplyVelocityDelta(DV0, DW0);
			}
			if (Body1.IsDynamic())
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, Impulse);
				const FVec3 DV1 = Body1.InvM() * -Impulse;
				const FVec3 DW1 = Body1.InvI() * -AngularImpulse;
				Body1.ApplyVelocityDelta(DV1, DW1);
			}

			// PushOut needs to know if we applied restitution and static friction
			ManifoldPoint.bInsideStaticFrictionCone = bInsideStaticFrictionCone;
			ManifoldPoint.NetImpulse = NetImpulse;

			// If we applied any additional impulse, we need to go again next iteration
			const FReal ImpulseTolerance = Chaos_Manifold_Apply_ImpulseTolerance * Chaos_Manifold_Apply_ImpulseTolerance;
			*IterationParameters.NeedsAnotherIteration |= (Impulse.SizeSquared() > ImpulseTolerance);
#endif
		}


		// Apply a position correction to the bodies so that the manifold points is not penetrating.
		// This modifies the in/out CoM position and rotations.
		void ApplyManifoldPushOutCorrection(
			const FReal Stiffness,
			const FPBDCollisionConstraint& Constraint,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			FConstraintSolverBody& Body0,
			FConstraintSolverBody& Body1,
			FManifoldPoint& ManifoldPoint)
		{
#if 0
			const FReal Margin0 = ManifoldPoint.ContactPoint.ShapeMargins[0];
			const FReal Margin1 = ManifoldPoint.ContactPoint.ShapeMargins[1];

			// Calculate the position error we need to correct, including static friction and restitution
			// Position correction uses the deepest point on each body (see velocity correction which uses average contact)

			const FVec3 ContactNormal = ManifoldPoint.ManifoldContactNormal;

			const bool bApplyStaticFriction = (ManifoldPoint.bActive && ManifoldPoint.bInsideStaticFrictionCone && ManifoldPoint.bPotentialRestingContact && Chaos_Manifold_PushOut_StaticFriction);
			FVec3 LocalContactPoint0 = ManifoldPoint.CoMContactPoints[0];
			FVec3 LocalContactPoint1 = ManifoldPoint.CoMContactPoints[1];

			// If we are applying static friction, we are attempting to move the contact points at their intitial positions back together
			if (bApplyStaticFriction)
			{
				Constraint.CalculatePrevCoMContactPoints(Body0.SolverBody(), Body1.SolverBody(), ManifoldPoint, IterationParameters.Dt, LocalContactPoint0, LocalContactPoint1);
			}

			// We could push out to the PBD distance that would give an implicit velocity equal to -(1+e).Vin
			// but the values involved are not very stable from frame to frame, so instead we actually pull
			// objects together so that Phi = 0 after pushout if the contact applied a contact impulse
			const FReal PhiPadding = 0;

			// Contact points on each body adjusted so that the points end up separated by TargetPhi
			const FVec3 RelativeContactPoint0 = Body0.Q() * LocalContactPoint0 - (PhiPadding + Margin0) * ContactNormal;
			const FVec3 RelativeContactPoint1 = Body1.Q() * LocalContactPoint1 + (PhiPadding + Margin1) * ContactNormal;

			// Net error we need to correct, including lateral movement to correct for friction
			FVec3 ContactError = (Body1.P() + RelativeContactPoint1) - (Body0.P() + RelativeContactPoint0);

			FReal ContactErrorNormal = FVec3::DotProduct(ContactError, ContactNormal);

			// Don't allow objects to be pulled together, but we may still have to correct static friction drift
			// @todo(chaos): allow undoing of previously added normal pushout
			if ((ContactErrorNormal < 0.0f) && !Chaos_Manifold_PushOut_NegativePushOut)
			{
				// If we have no static friction and no normal separation to enforce, we have nothing to do
				if (!bApplyStaticFriction)
				{
					return;
				}

				ContactError = ContactError - ContactErrorNormal * ContactNormal;
				ContactErrorNormal = 0.0f;
			}

			if (ContactError.SizeSquared() < Chaos_Manifold_PushOut_PositionTolerance * Chaos_Manifold_PushOut_PositionTolerance)
			{
				return;
			}

			// Calculate joint-space mass matrix (J.M.Jt)
			const FMatrix33 ContactMassInv =
				(Body0.IsDynamic() ? ComputeFactorMatrix3(RelativeContactPoint0, Body0.InvI(), Body0.InvM()) : FMatrix33(0)) +
				(Body1.IsDynamic() ? ComputeFactorMatrix3(RelativeContactPoint1, Body1.InvI(), Body1.InvM()) : FMatrix33(0));

			// Calculate pushout
			// - If we were inside the static friction cone during the apply step, correct positions so that
			// the relative contact points at the start of the frame are coincident.
			// - If we were outside the static friction cone, just push out along the normal 
			// i.e., ignore dynamic friction during the pushout step
			FVec3 PushOut = FVec3(0);
			if (bApplyStaticFriction)
			{
				const FMatrix33 ContactMass = ContactMassInv.Inverse();
				PushOut = Stiffness * ContactMass * ContactError;
			}
			else
			{
				const FReal PushOutDenominator = FVec3::DotProduct(ContactNormal, ContactMassInv * ContactNormal);
				if (PushOutDenominator > SMALL_NUMBER)
				{
					PushOut = (Stiffness * ContactErrorNormal / PushOutDenominator) * ContactNormal;
				}
			}

			FVec3 NetPushOut = ManifoldPoint.NetPushOut + PushOut;

			if (Body0.IsDynamic())
			{
				const FVec3 AngularPushOut = FVec3::CrossProduct(RelativeContactPoint0, PushOut);
				const FVec3 DX0 = Body0.InvM() * PushOut;
				const FVec3 DR0 = Body0.InvI() * AngularPushOut;
				Body0.ApplyTransformDelta(DX0, DR0);
				Body0.UpdateRotationDependentState();
			}

			if (Body1.IsDynamic())
			{
				const FVec3 AngularPushOut = FVec3::CrossProduct(RelativeContactPoint1, PushOut);
				const FVec3 DX1 = Body1.InvM() * -PushOut;
				const FVec3 DR1 = Body1.InvI() * -AngularPushOut;
				Body1.ApplyTransformDelta(DX1, DR1);
				Body1.UpdateRotationDependentState();
			}

			ManifoldPoint.NetPushOut = NetPushOut;
			*IterationParameters.NeedsAnotherIteration = true;
#endif
		}


		// Apply a velocity correction to the Manifold point for use in the PushOut phase.
		void CalculateManifoldPushOutVelocityCorrection(
			const FReal Stiffness,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const FReal Restitution,
			const FReal StaticFriction,
			const FReal DynamicFriction,
			FConstraintSolverBody& Body0,
			FConstraintSolverBody& Body1,
			FManifoldPoint& ManifoldPoint)
		{
#if 0
			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - Body0.P();
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - Body1.P();
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			FVec3 ContactTangent = FVec3(0);

			if (Chaos_Manifold_PushOut_VelocityCorrection == 2)
			{
				ContactTangent = (ManifoldPoint.NetPushOut - FVec3::DotProduct(ManifoldPoint.NetPushOut, ContactNormal) * ContactNormal).GetSafeNormal();
			}

			const FVec3 ContactVelocity0 = Body0.V() + FVec3::CrossProduct(Body0.W(), RelativeContactPoint0);
			const FVec3 ContactVelocity1 = Body1.V() + FVec3::CrossProduct(Body1.W(), RelativeContactPoint1);
			const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
			const FReal ContactVelocityNormal = FVec3::DotProduct(ContactVelocity, ContactNormal);
			const FReal ContactVelocityTangent = FVec3::DotProduct(ContactVelocity, ContactTangent);

			// If we are already moving away we are probably done. However, we may want to undo some
			// of the previously applied velocity correct if there is some
			if ((ContactVelocityNormal > 0.0f) && FMath::IsNearlyZero(ManifoldPoint.NetPushOutImpulseNormal))
			{
				return;
			}

			// Calculate constraint-space inverse mass
			const FMatrix33 ContactMassInv =
				(Body0.IsDynamic() ? ComputeFactorMatrix3(RelativeContactPoint0, Body0.InvI(), Body0.InvM()) : FMatrix33(0)) +
				(Body1.IsDynamic() ? ComputeFactorMatrix3(RelativeContactPoint1, Body1.InvI(), Body1.InvM()) : FMatrix33(0));

			// Calculate the impulse to get the desired target normal velocity
			// We are ignoring both static and dynamic friction here
			FReal ImpulseNormal = 0.0f;
			FReal NetImpulseNormal = 0.0f;
			{
				// If we applied restitution in the velocity solve step, we also apply it here
				FReal TargetVelocityNormal = 0.0f;
				if (ManifoldPoint.bRestitutionEnabled && Chaos_Manifold_PushOut_Restitution)
				{
					TargetVelocityNormal = FMath::Max((FReal)0., -Restitution * ManifoldPoint.InitialContactVelocity);
				}

				const FReal ImpulseNormalDenominator = FVec3::DotProduct(ContactNormal, ContactMassInv * ContactNormal);
				if (FMath::Abs(ImpulseNormalDenominator) > SMALL_NUMBER)
				{
					const FReal ContactVelocityError = TargetVelocityNormal - ContactVelocityNormal;
					ImpulseNormal = (Stiffness * ContactVelocityError / ImpulseNormalDenominator);
				}

				// If we applied a negative impulse this iteration, make sure the total impulse is not negative
				NetImpulseNormal = ManifoldPoint.NetPushOutImpulseNormal + ImpulseNormal;
				if (NetImpulseNormal < 0.0f)
				{
					ImpulseNormal = -ManifoldPoint.NetPushOutImpulseNormal;
					NetImpulseNormal = 0.0f;
				}
			}

			FReal ImpulseTangent = 0.0f;
			FReal NetImpulseTangent = 0.0f;
			const bool bApplyStaticFriction = ManifoldPoint.bInsideStaticFrictionCone && ManifoldPoint.bPotentialRestingContact && Chaos_Manifold_PushOut_StaticFriction && (Chaos_Manifold_PushOut_VelocityCorrection == 2);
			if (bApplyStaticFriction)
			{
				FReal TargetVelocityTangent = 0.0f;

				const FReal ImpulseTangentDenominator = FVec3::DotProduct(ContactTangent, ContactMassInv * ContactTangent);
				if (FMath::Abs(ImpulseTangentDenominator) > SMALL_NUMBER)
				{
					const FReal ContactVelocityError = TargetVelocityTangent - ContactVelocityTangent;
					ImpulseTangent = (Stiffness * ContactVelocityError / ImpulseTangentDenominator);
				}

				// If we applied a negative impulse this iteration, make sure the total impulse is not negative
				NetImpulseTangent = ManifoldPoint.NetPushOutImpulseTangent + ImpulseTangent;
				if (NetImpulseTangent < 0.0f)
				{
					ImpulseTangent = -ManifoldPoint.NetPushOutImpulseTangent;
					NetImpulseTangent = 0.0f;
				}
			}


			const FVec3 Impulse = ImpulseNormal * ContactNormal + ImpulseTangent * ContactTangent;
			if (Body0.IsDynamic())
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				const FVec3 DV0 = Body0.InvM() * Impulse;
				const FVec3 DW0 = Body0.InvI() * AngularImpulse;
				Body0.ApplyVelocityDelta(DV0, DW0);
			}

			if (Body1.IsDynamic())
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, Impulse);
				const FVec3 DV1 = Body1.InvM() * -Impulse;
				const FVec3 DW1 = Body1.InvI() * -AngularImpulse;
				Body1.ApplyVelocityDelta(DV1, DW1);
			}

			ManifoldPoint.NetPushOutImpulseNormal = NetImpulseNormal;
			ManifoldPoint.NetPushOutImpulseTangent = NetImpulseTangent;
#endif
		}


		// Velocity solver loop for a single contact manifold.
		void ApplyContactManifold(
			FPBDCollisionConstraint& Constraint,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
#if 0
			FSolverBody& Body0 = *Constraint.GetSolverBody0();
			FSolverBody& Body1 = *Constraint.GetSolverBody1();

			Constraint.AccumulatedImpulse = FVec3(0);

			const FReal ConstraintStiffness = GetConstraintStiffness(Constraint.GetStiffness(), IterationParameters);
			const FReal Stiffness = ConstraintStiffness * GetApplyStiffness(IterationParameters.Iteration, IterationParameters.NumIterations);

			const FVec3 InitialV0 = Body0.V();
			const FVec3 InitialW0 = Body0.W();
			const FVec3 InitialV1 = Body1.V();
			const FVec3 InitialW1 = Body1.W();

			// Iterate over the manifold and accumulate velocity corrections
			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
			{
				FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, Body0.P(), Body0.Q(), Body1.P(), Body1.Q());

				CalculateManifoldNormalVelocityCorrection(
					Stiffness,
					IterationParameters,
					ParticleParameters,
					Constraint.Manifold.Restitution,
					Body0, 
					Body1,
					ManifoldPoint);

				Constraint.AccumulatedImpulse += ManifoldPoint.NetImpulse;
			}

			const FReal StaticFriction = FMath::Max(Constraint.Manifold.Friction, Constraint.Manifold.AngularFriction);
			const FReal DynamicFriction = Constraint.Manifold.Friction;
			if (StaticFriction > 0.0f)
			{
				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, Body0.P(), Body0.Q(), Body1.P(), Body1.Q());

					CalculateManifoldFrictionVelocityCorrection(
						Stiffness,
						IterationParameters,
						ParticleParameters,
						StaticFriction,
						DynamicFriction,
						Body0, 
						Body1,
						ManifoldPoint);

					Constraint.AccumulatedImpulse += ManifoldPoint.NetImpulse;
				}
			}

			// Apply a position correction so that we get the desired velocity in the implicit velocity step
			if (Body0.IsDynamic())
			{
				const FVec3 DV0 = Body0.V() - InitialV0;
				const FVec3 DW0 = Body0.W() - InitialW0;
				const FVec3 DX0 = DV0 * IterationParameters.Dt;
				const FVec3 DR0 = DW0 * IterationParameters.Dt;
				Body0.ApplyTransformDelta(DX0, DR0);
				Body0.UpdateRotationDependentState();
			}
			if (Body1.IsDynamic())
			{
				const FVec3 DV1 = Body1.V() - InitialV1;
				const FVec3 DW1 = Body1.W() - InitialW1;
				const FVec3 DX1 = DV1 * IterationParameters.Dt;
				const FVec3 DR1 = DW1 * IterationParameters.Dt;
				Body1.ApplyTransformDelta(DX1, DR1);
				Body1.UpdateRotationDependentState();
			}
#endif
		}


		// Pushout solver loop for a single contafct manifold.
		void ApplyPushOutManifold(
			FPBDCollisionConstraint& Constraint,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
#if 0
			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();

			FConstraintSolverBody Body0 = *Constraint.GetSolverBody0();
			FConstraintSolverBody Body1 = *Constraint.GetSolverBody1();

			// How much shock propagation should we apply - currently 100% on final iteration, otherwise none
			FReal ShockPropagationInvMassScale = (IterationParameters.Iteration < (IterationParameters.NumIterations - 1)) ? 1.0f : 0.0f;
			if ((ShockPropagationInvMassScale < 1.0f) && Chaos_Collision_UseShockPropagation)
			{
				// Lock one of the bodies if want shock propagation and they are at different shock levels
				if (Body0.Level() < Body1.Level())
				{
					// Body0 is lower level and should be frozen
					Body0.SetInvMassScale(ShockPropagationInvMassScale);
				}
				else if (Body0.Level() > Body1.Level())
				{
					// Body1 is lower level and should be frozen
					Body1.SetInvMassScale(ShockPropagationInvMassScale);
				}
				else
				{
					// Both bodies are at the same level so neither is frozen
				}
			}

			// If both bodies are frozen or kinematic just skip
			// @todo(chaos): this should not happen - if one body is kinematic, the other should have no mass scaling. Check and remove
			if (!Body0.IsDynamic() && !Body1.IsDynamic())
			{
				return;
			}

			// Gradually increase position correction through iterations (optional based on cvars)
			const FReal Stiffness = GetPushOutStiffness(IterationParameters.Iteration, IterationParameters.NumIterations);

			// Apply the position correction so that all contacts have zero separation
			if (Chaos_Manifold_PushOut_PositionCorrection)
			{
				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, Body0.P(), Body0.Q(), Body1.P(), Body1.Q());

					// The distance we should pushout to
					FReal PushOutSeparation = 0.0f;

					if (ManifoldPoint.bActive || (ManifoldPoint.ContactPoint.Phi < PushOutSeparation))
					{
						ApplyManifoldPushOutCorrection(
							Stiffness,
							Constraint,
							IterationParameters,
							ParticleParameters,
							Body0,
							Body1,
							ManifoldPoint);
					}
				}
			}

			// Apply a velocity correction so that all contacts have non-negative contact velocity
			if (Chaos_Manifold_PushOut_VelocityCorrection > 0)
			{
				const FReal StaticFriction = FMath::Max(Constraint.Manifold.Friction, Constraint.Manifold.AngularFriction);
				const FReal DynamicFriction = Constraint.Manifold.Friction;

				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, Body0.P(), Body0.Q(), Body1.P(), Body1.Q());

					if (ManifoldPoint.bActive || !ManifoldPoint.NetPushOut.IsNearlyZero(KINDA_SMALL_NUMBER))
					{
						CalculateManifoldPushOutVelocityCorrection(
							Stiffness,
							IterationParameters,
							ParticleParameters,
							Constraint.Manifold.Restitution,
							StaticFriction,
							DynamicFriction,
							Body0,
							Body1,
							ManifoldPoint);
					}

					//Constraint.AccumulatedImpulse += ManifoldPoint.NetPushOutImpulseNormal * ManifoldPoint.ContactPoint.Normal;
				}
			}
#endif
		}

	}
}