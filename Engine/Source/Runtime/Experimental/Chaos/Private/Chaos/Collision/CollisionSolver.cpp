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
	namespace Collisions
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
		float Chaos_Manifold_PushOut_DistanceFactor = 0.0f;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_NegativePushOut(TEXT("p.Chaos.Collision.Manifold.PushOut.NegativePushOut"), Chaos_Manifold_PushOut_NegativePushOut, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_StaticFriction(TEXT("p.Chaos.Collision.Manifold.PushOut.StaticFriction"), Chaos_Manifold_PushOut_StaticFriction, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_Restitution(TEXT("p.Chaos.Collision.Manifold.PushOut.Restitution"), Chaos_Manifold_PushOut_Restitution, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_PositionCorrection(TEXT("p.Chaos.Collision.Manifold.PushOut.PositionCorrection"), Chaos_Manifold_PushOut_PositionCorrection, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_VelocityCorrection(TEXT("p.Chaos.Collision.Manifold.PushOut.VelocityCorrectionMode"), Chaos_Manifold_PushOut_VelocityCorrection, TEXT("0 = No Velocity Correction; 1 = Normal Velocity Correction; 2 = Normal + Tangential Velocity Correction"));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_DistanceFactor(TEXT("p.Chaos.Collision.Manifold.PushOut.DistanceFactor"), Chaos_Manifold_PushOut_DistanceFactor, TEXT(""));

		// Note: Velocity solve requires full stiffness for restitution to work correctly so the max stiffness should be 1
		FRealSingle Chaos_Manifold_Apply_MinStiffness = 0.25f;
		FRealSingle Chaos_Manifold_Apply_MaxStiffness = 1.0f;
		FAutoConsoleVariableRef CVarChaos_Manifold_Apply_MinStiffness(TEXT("p.Chaos.Collision.Manifold.Apply.MinStiffness"), Chaos_Manifold_Apply_MinStiffness, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_Apply_MaxStiffness(TEXT("p.Chaos.Collision.Manifold.Apply.MaxStiffness"), Chaos_Manifold_Apply_MaxStiffness, TEXT(""));

		FRealSingle Chaos_Manifold_PushOut_MinStiffness = 0.25f;
		FRealSingle Chaos_Manifold_PushOut_MaxStiffness = 0.5f;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_MinStiffness(TEXT("p.Chaos.Collision.Manifold.PushOut.MinStiffness"), Chaos_Manifold_PushOut_MinStiffness, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_MaxStiffness(TEXT("p.Chaos.Collision.Manifold.PushOut.MaxStiffness"), Chaos_Manifold_PushOut_MaxStiffness, TEXT(""));

		FRealSingle Chaos_Manifold_Apply_ImpulseTolerance = 1.e-4f;
		FAutoConsoleVariableRef CVarChaos_Manifold_Apply_ImpulseTolerance(TEXT("p.Chaos.Collision.Manifold.Apply.ImpulseTolerance"), Chaos_Manifold_Apply_ImpulseTolerance, TEXT(""));

		float Chaos_Manifold_PushOut_PositionTolerance = 1.e-4f;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_PositionTolerance(TEXT("p.Chaos.Collision.Manifold.PushOut.PositionTolerance"), Chaos_Manifold_PushOut_PositionTolerance, TEXT(""));


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
			const FReal InvM0,
			const FMatrix33& InvI0,
			const FReal InvM1,
			const FMatrix33& InvI1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			FVec3& V0,
			FVec3& W0,
			FVec3& V1,
			FVec3& W1,
			FManifoldPoint& ManifoldPoint)
		{
			const bool bIsRigidDynamic0 = (InvM0 > 0.0f);
			const bool bIsRigidDynamic1 = (InvM1 > 0.0f);

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			const FReal ContactPhi = ManifoldPoint.ContactPoint.Phi;

			// Reject non-contact points unless the point has previously been processed - we may want to undo some of the previous work
			// The tolerance is so that we catch collisions that were resolved last frame, but resulted in slightly positive separations. However,
			// it also means we are effectively padding objects by this amount.
			if ((ContactPhi > Chaos_Collision_CollisionClipTolerance) && (ManifoldPoint.InitialPhi > Chaos_Collision_CollisionClipTolerance) && !ManifoldPoint.bActive)
			{
				return;
			}

			const FVec3 ContactVelocity0 = V0 + FVec3::CrossProduct(W0, RelativeContactPoint0);
			const FVec3 ContactVelocity1 = V1 + FVec3::CrossProduct(W1, RelativeContactPoint1);
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
				ContactVelocityTargetNormal = FMath::Max(0.0f, -Restitution * ManifoldPoint.InitialContactVelocity);
			}

			// Calculate constraint-space mass
			const FMatrix33 ConstraintMassInv =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, InvM0) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, InvM1) : FMatrix33(0));

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
			if (bIsRigidDynamic0)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				V0 += InvM0 * Impulse;
				W0 += InvI0 * AngularImpulse;
			}
			if (bIsRigidDynamic1)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, Impulse);
				V1 -= InvM1 * Impulse;
				W1 -= InvI1 * AngularImpulse;
			}

			// PushOut needs to know if we applied restitution and static friction
			ManifoldPoint.bActive = bActive;
			ManifoldPoint.bInsideStaticFrictionCone = bActive && ManifoldPoint.bInsideStaticFrictionCone;					// Reset when not active
			ManifoldPoint.bRestitutionEnabled = bActive && (bApplyRestitution || ManifoldPoint.bRestitutionEnabled);		// Latches to on-state when active
			ManifoldPoint.NetImpulse = NetImpulse;

			// If we applied any additional impulse, we need to go again next iteration
			const FReal ImpulseTolerance = Chaos_Manifold_Apply_ImpulseTolerance * Chaos_Manifold_Apply_ImpulseTolerance;
			*IterationParameters.NeedsAnotherIteration |= (Impulse.SizeSquared() > ImpulseTolerance);
		}

		void CalculateManifoldFrictionVelocityCorrection(
			const FReal Stiffness,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const FReal StaticFriction,
			const FReal DynamicFriction,
			const FReal InvM0,
			const FMatrix33& InvI0,
			const FReal InvM1,
			const FMatrix33& InvI1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			FVec3& V0,
			FVec3& W0,
			FVec3& V1,
			FVec3& W1,
			FManifoldPoint& ManifoldPoint)
		{
			// Reject inactive contacts (no normal impulse)
			if (!ManifoldPoint.bActive || (StaticFriction == 0.0f))
			{
				return;
			}

			const bool bIsRigidDynamic0 = (InvM0 > 0.0f);
			const bool bIsRigidDynamic1 = (InvM1 > 0.0f);

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;

			const FVec3 ContactVelocity0 = V0 + FVec3::CrossProduct(W0, RelativeContactPoint0);
			const FVec3 ContactVelocity1 = V1 + FVec3::CrossProduct(W1, RelativeContactPoint1);
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
					(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, InvM0) : FMatrix33(0)) +
					(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, InvM1) : FMatrix33(0));

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
			if (bIsRigidDynamic0)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				V0 += InvM0 * Impulse;
				W0 += InvI0 * AngularImpulse;
			}
			if (bIsRigidDynamic1)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, Impulse);
				V1 -= InvM1 * Impulse;
				W1 -= InvI1 * AngularImpulse;
			}

			// PushOut needs to know if we applied restitution and static friction
			ManifoldPoint.bInsideStaticFrictionCone = bInsideStaticFrictionCone;
			ManifoldPoint.NetImpulse = NetImpulse;

			// If we applied any additional impulse, we need to go again next iteration
			const FReal ImpulseTolerance = Chaos_Manifold_Apply_ImpulseTolerance * Chaos_Manifold_Apply_ImpulseTolerance;
			*IterationParameters.NeedsAnotherIteration |= (Impulse.SizeSquared() > ImpulseTolerance);
		}


		// Apply a position correction to the bodies so that the manifold points is not penetrating.
		// This modifies the in/out CoM position and rotations.
		void ApplyManifoldPushOutCorrection(
			const FReal Stiffness,
			const FRigidBodyPointContactConstraint& Constraint,
			const FGenericParticleHandle Particle0,
			const FGenericParticleHandle Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			FVec3& P0, // Centre of Mass Positions and Rotations
			FRotation3& Q0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V0,
			FVec3& W0,
			FVec3& V1,
			FVec3& W1,
			FManifoldPoint& ManifoldPoint)
		{
			const TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			const TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			const FReal Margin0 = ManifoldPoint.ContactPoint.ShapeMargins[0];
			const FReal Margin1 = ManifoldPoint.ContactPoint.ShapeMargins[1];

			// Calculate the position error we need to correct, including static friction and restitution
			// Position correction uses the deepest point on each body (see velocity correction which uses average contact)
			const FRotation3& PlaneQ = (ManifoldPoint.ContactPoint.ContactNormalOwnerIndex == 0) ? Q0 : Q1;
			const FVec3 ContactNormal = PlaneQ * ManifoldPoint.CoMContactNormal;

			const bool bApplyStaticFriction = (ManifoldPoint.bActive && ManifoldPoint.bInsideStaticFrictionCone && ManifoldPoint.bPotentialRestingContact && Chaos_Manifold_PushOut_StaticFriction);
			FVec3 LocalContactPoint0 = ManifoldPoint.CoMContactPoints[0];
			FVec3 LocalContactPoint1 = ManifoldPoint.CoMContactPoints[1];

			// If we are applying static friction, we are attempting to move the contact points at their intitial positions back together
			if (bApplyStaticFriction)
			{
				Constraint.CalculatePrevCoMContactPoints(Particle0, Particle1, ManifoldPoint, IterationParameters.Dt, LocalContactPoint0, LocalContactPoint1);
			}

			// We could push out to the PBD distance that would give an implicit velocity equal to -(1+e).Vin
			// but the values involved are not very stable from frame to frame, so instead we actually pull
			// objects together so that Phi = 0 after pushout if the contact applied a contact impulse
			const FReal PhiPadding = 0;

			// Contact points on each body adjusted so that the points end up separated by TargetPhi
			const FVec3 RelativeContactPoint0 = Q0 * LocalContactPoint0 - (PhiPadding + Margin0) * ContactNormal;
			const FVec3 RelativeContactPoint1 = Q1 * LocalContactPoint1 + (PhiPadding + Margin1) * ContactNormal;

			// Net error we need to correct, including lateral movement to correct for friction
			FVec3 ContactError = (P1 + RelativeContactPoint1) - (P0 + RelativeContactPoint0);

			// The max rest distance for PBD object is the distance it can move in one frame from rest along the contact normal (i.e., position delta from gravity)
			if ((Chaos_Manifold_PushOut_DistanceFactor > 0.0f) && (ContactNormal.Z > 0.0f))
			{
				// @todo(chaos): pass gravity into the solver if we use this feature. Otherwise remove it.
				const FReal GravityNegZ = 980.0f;
				const FReal MaxRestDistance = GravityNegZ * IterationParameters.Dt * IterationParameters.Dt;
				ContactError += (Chaos_Manifold_PushOut_DistanceFactor * MaxRestDistance * ContactNormal.Z) * ContactNormal;
			}

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
			const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Constraint.Manifold.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Constraint.Manifold.InvInertiaScale1 : FMatrix33(0);
			const FMatrix33 ContactMassInv =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, PBDRigid1->InvM()) : FMatrix33(0));

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

			if (bIsRigidDynamic0)
			{
				const FVec3 AngularPushOut = FVec3::CrossProduct(RelativeContactPoint0, PushOut);
				const FVec3 DX0 = PBDRigid0->InvM() * PushOut;
				const FVec3 DR0 = InvI0 * AngularPushOut;

				P0 += DX0;
				Q0 += FRotation3::FromElements(DR0, 0.f) * Q0 * FReal(0.5);
				Q0.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
			}

			if (bIsRigidDynamic1)
			{
				const FVec3 AngularPushOut = FVec3::CrossProduct(RelativeContactPoint1, PushOut);
				const FVec3 DX1 = -(PBDRigid1->InvM() * PushOut);
				const FVec3 DR1 = -(InvI1 * AngularPushOut);

				P1 += DX1;
				Q1 += FRotation3::FromElements(DR1, 0.f) * Q1 * FReal(0.5);
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
			}

			ManifoldPoint.NetPushOut = NetPushOut;
			*IterationParameters.NeedsAnotherIteration = true;
		}


		// Apply a velocity correction to the Manifold point for use in the PushOut phase.
		void CalculateManifoldPushOutVelocityCorrection(
			const FReal Stiffness,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const FReal Restitution,
			const FReal StaticFriction,
			const FReal DynamicFriction,
			const FReal InvM0,
			const FMatrix33& InvI0,
			const FReal InvM1,
			const FMatrix33& InvI1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			FVec3& V0,
			FVec3& W0,
			FVec3& V1,
			FVec3& W1,
			FManifoldPoint& ManifoldPoint)
		{
			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			FVec3 ContactTangent = FVec3(0);

			if (Chaos_Manifold_PushOut_VelocityCorrection == 2)
			{
				ContactTangent = (ManifoldPoint.NetPushOut - FVec3::DotProduct(ManifoldPoint.NetPushOut, ContactNormal) * ContactNormal).GetSafeNormal();
			}

			const FVec3 ContactVelocity0 = V0 + FVec3::CrossProduct(W0, RelativeContactPoint0);
			const FVec3 ContactVelocity1 = V1 + FVec3::CrossProduct(W1, RelativeContactPoint1);
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
				((InvM0 > 0.0f) ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, InvM0) : FMatrix33(0)) +
				((InvM1 > 0.0f) ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, InvM1) : FMatrix33(0));

			// Calculate the impulse to get the desired target normal velocity
			// We are ignoring both static and dynamic friction here
			FReal ImpulseNormal = 0.0f;
			FReal NetImpulseNormal = 0.0f;
			{
				// If we applied restitution in the velocity solve step, we also apply it here
				FReal TargetVelocityNormal = 0.0f;
				if (ManifoldPoint.bRestitutionEnabled && Chaos_Manifold_PushOut_Restitution)
				{
					TargetVelocityNormal = FMath::Max(0.0f, -Restitution * ManifoldPoint.InitialContactVelocity);
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
			if (InvM0 > 0.0f)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				const FVec3 DV0 = InvM0 * Impulse;
				const FVec3 DW0 = InvI0 * AngularImpulse;

				V0 += DV0;
				W0 += DW0;
			}

			if (InvM1 > 0.0f)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, -Impulse);
				const FVec3 DV1 = -InvM1 * Impulse;
				const FVec3 DW1 = InvI1 * AngularImpulse;

				V1 += DV1;
				W1 += DW1;
			}

			ManifoldPoint.NetPushOutImpulseNormal = NetImpulseNormal;
			ManifoldPoint.NetPushOutImpulseTangent = NetImpulseTangent;
		}


		// Velocity solver loop for a single contact manifold.
		void ApplyContactManifold(
			FRigidBodyPointContactConstraint& Constraint,
			FGenericParticleHandle Particle0,
			FGenericParticleHandle Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
			FVec3 V0 = Particle0->V();
			FVec3 W0 = Particle0->W();
			FVec3 V1 = Particle1->V();
			FVec3 W1 = Particle1->W();

			const FReal InvM0 = bIsRigidDynamic0 ? PBDRigid0->InvM() : 0.0f;
			const FReal InvM1 = bIsRigidDynamic1 ? PBDRigid1->InvM() : 0.0f;
			const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Constraint.Manifold.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Constraint.Manifold.InvInertiaScale1 : FMatrix33(0);

			Constraint.AccumulatedImpulse = FVec3(0);

			const FReal Stiffness = GetApplyStiffness(IterationParameters.Iteration, IterationParameters.NumIterations);

			// Iterate over the manifold and accumulate velocity corrections - we will apply them after the loop
			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
			{
				FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

				CalculateManifoldNormalVelocityCorrection(
					Stiffness,
					IterationParameters,
					ParticleParameters,
					Constraint.Manifold.Restitution,
					InvM0, InvI0, InvM1, InvI1,
					P0, Q0, P1, Q1,
					V0, W0, V1, W1,
					ManifoldPoint);

				Constraint.AccumulatedImpulse += ManifoldPoint.NetImpulse;
			}

			const FReal StaticFriction = FMath::Max(Constraint.Manifold.Friction, Constraint.Manifold.AngularFriction);
			const FReal DynamicFriction = Constraint.Manifold.Friction;
			if (StaticFriction > 0.0f)
			{
				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

					CalculateManifoldFrictionVelocityCorrection(
						Stiffness,
						IterationParameters,
						ParticleParameters,
						StaticFriction,
						DynamicFriction,
						InvM0, InvI0, InvM1, InvI1,
						P0, Q0, P1, Q1,
						V0, W0, V1, W1,
						ManifoldPoint);

					Constraint.AccumulatedImpulse += ManifoldPoint.NetImpulse;
				}
			}

			if (bIsRigidDynamic0)
			{
				const FVec3 DV0 = V0 - PBDRigid0->V();
				const FVec3 DW0 = W0 - PBDRigid0->W();
				const FVec3 DX0 = DV0 * IterationParameters.Dt;
				const FVec3 DR0 = DW0 * IterationParameters.Dt;

				PBDRigid0->V() = V0;
				PBDRigid0->W() = W0;
				P0 += DX0;
				Q0 += FRotation3::FromElements(DR0, 0.f) * Q0 * FReal(0.5);
				Q0.Normalize();
				FParticleUtilities::SetCoMWorldTransform(PBDRigid0, P0, Q0);
			}

			if (bIsRigidDynamic1)
			{
				const FVec3 DV1 = V1 - PBDRigid1->V();
				const FVec3 DW1 = W1 - PBDRigid1->W();
				const FVec3 DX1 = DV1 * IterationParameters.Dt;
				const FVec3 DR1 = DW1 * IterationParameters.Dt;

				PBDRigid1->V() = V1;
				PBDRigid1->W() = W1;
				P1 += DX1;
				Q1 += FRotation3::FromElements(DR1, 0.f) * Q1 * FReal(0.5);
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
			}
		}


		// Pushout solver loop for a single contafct manifold.
		void ApplyPushOutManifold(
			FRigidBodyPointContactConstraint& Constraint,
			const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const FVec3& GravityDir)
		{
			FGenericParticleHandle Particle0 = FGenericParticleHandle(Constraint.Particle[0]);
			FGenericParticleHandle Particle1 = FGenericParticleHandle(Constraint.Particle[1]);
			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();

			// Lock bodies for shock propagation?
			bool IsTemporarilyStatic0 = false;
			bool IsTemporarilyStatic1 = false;
			if (Chaos_Collision_UseShockPropagation)
			{
				IsTemporarilyStatic0 = IsTemporarilyStatic.Contains(Particle0->GeometryParticleHandle());
				IsTemporarilyStatic1 = IsTemporarilyStatic.Contains(Particle1->GeometryParticleHandle());

				// In the case where two objects at the same level in shock propagation end
				// up in contact with each other, try to decide which one has higher priority.
				// For now, we use the one which is "lower" (in direction of gravity) as the frozen one.
				if (IsTemporarilyStatic0 && IsTemporarilyStatic1)
				{
					if (ManifoldPoints.Num() > 0)
					{
						const FReal NormalThreshold = 0.2f;	// @chaos(todo): what value here?
						const FReal NormalGrav = FVec3::DotProduct(ManifoldPoints[0].ContactPoint.Normal, GravityDir);
						const bool bNormalIsUp = NormalGrav < -NormalThreshold;
						const bool bNormalIsDown = NormalGrav > NormalThreshold;
						IsTemporarilyStatic0 = bNormalIsDown;
						IsTemporarilyStatic1 = bNormalIsUp;
					}
					else
					{
						IsTemporarilyStatic0 = false;
						IsTemporarilyStatic1 = false;
					}
				}
			}

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && (PBDRigid0->ObjectState() == EObjectStateType::Dynamic) && !IsTemporarilyStatic0;
			const bool bIsRigidDynamic1 = PBDRigid1 && (PBDRigid1->ObjectState() == EObjectStateType::Dynamic) && !IsTemporarilyStatic1;

			if (!bIsRigidDynamic0 && !bIsRigidDynamic1)
			{
				return;
			}

			// Gradually increase position correction through iterations (optional based on cvars)
			const FReal Stiffness = GetPushOutStiffness(IterationParameters.Iteration, IterationParameters.NumIterations);

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
			FVec3 V0 = Particle0->V();
			FVec3 W0 = Particle0->W();
			FVec3 V1 = Particle1->V();
			FVec3 W1 = Particle1->W();

			// Apply the position correction so that all contacts have zero separation
			if (Chaos_Manifold_PushOut_PositionCorrection)
			{
				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

					// The distance we should pushout to
					FReal PushOutSeparation = 0.0f;
					if ((Chaos_Manifold_PushOut_DistanceFactor > 0.0f) && (ManifoldPoint.ContactPoint.Normal.Z > 0.0f))
					{
						// @todo(chaos): pass gravity into the solver if we use this feature. Otherwise remove it.
						const FReal GravityNegZ = 980.0f;
						const FReal MaxRestDistance = GravityNegZ * IterationParameters.Dt * IterationParameters.Dt;
						PushOutSeparation = (Chaos_Manifold_PushOut_DistanceFactor * MaxRestDistance * ManifoldPoint.ContactPoint.Normal.Z);
					}

					if (ManifoldPoint.bActive || (ManifoldPoint.ContactPoint.Phi < PushOutSeparation))
					{
						ApplyManifoldPushOutCorrection(
							Stiffness,
							Constraint,
							Particle0,
							Particle1,
							IterationParameters,
							ParticleParameters,
							bIsRigidDynamic0,
							bIsRigidDynamic1,
							P0, Q0, P1, Q1,
							V0, V1, W0, W1,
							ManifoldPoint);
					}
				}
			}

			// Apply a velocity correction so that all contacts have non-negative contact velocity
			if (Chaos_Manifold_PushOut_VelocityCorrection > 0)
			{
				const FReal InvM0 = bIsRigidDynamic0 ? PBDRigid0->InvM() : 0.0f;
				const FReal InvM1 = bIsRigidDynamic1 ? PBDRigid1->InvM() : 0.0f;
				const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Constraint.Manifold.InvInertiaScale0 : FMatrix33(0);
				const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Constraint.Manifold.InvInertiaScale1 : FMatrix33(0);
				const FReal StaticFriction = FMath::Max(Constraint.Manifold.Friction, Constraint.Manifold.AngularFriction);
				const FReal DynamicFriction = Constraint.Manifold.Friction;

				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

					if (ManifoldPoint.bActive || !ManifoldPoint.NetPushOut.IsNearlyZero(KINDA_SMALL_NUMBER))
					{
						CalculateManifoldPushOutVelocityCorrection(
							Stiffness,
							IterationParameters,
							ParticleParameters,
							Constraint.Manifold.Restitution,
							StaticFriction,
							DynamicFriction,
							InvM0, InvI0,
							InvM1, InvI1,
							P0, Q0, 
							P1, Q1,
							V0, W0,
							V1, W1,
							ManifoldPoint);
					}

					//Constraint.AccumulatedImpulse += ManifoldPoint.NetPushOutImpulseNormal * ManifoldPoint.ContactPoint.Normal;
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
			}
		}

	}
}