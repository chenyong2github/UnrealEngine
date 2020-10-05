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
		extern int32 Chaos_Collision_EnergyClampEnabled;
		extern int32 Chaos_Collision_UseShockPropagation;
		extern float Chaos_Collision_CollisionClipTolerance;

		bool Chaos_Manifold_PushOut_StaticFriction = true;
		bool Chaos_Manifold_PushOut_Restitution = false;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_StaticFriction(TEXT("p.Chaos.Collision.Manifold.PushOutStaticFriction"), Chaos_Manifold_PushOut_StaticFriction, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_Restitution(TEXT("p.Chaos.Collision.Manifold.PushOutRestitution"), Chaos_Manifold_PushOut_Restitution, TEXT(""));

		bool Chaos_Manifold_PushOut_PositionCorrection = true;
		bool Chaos_Manifold_PushOut_VelocityCorrection = true;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_PositionCorrection(TEXT("p.Chaos.Collision.Manifold.PushOutPositionCorrection"), Chaos_Manifold_PushOut_PositionCorrection, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_VelocityCorrection(TEXT("p.Chaos.Collision.Manifold.PushOutVelocityCorrection"), Chaos_Manifold_PushOut_VelocityCorrection, TEXT(""));


		void CalculateManifoldVelocityCorrection(
			const FCollisionContact& Contact,
			FManifoldPoint& ManifoldPoint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			bool bInApplyRestitution,
			bool bInApplyFriction,
			FVec3& DV0, // Out
			FVec3& DW0, // Out
			FVec3& DV1, // Out
			FVec3& DW1) // Out
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			const FReal ContactPhi = ManifoldPoint.ContactPoint.Phi;

			bool bProcessContact = (ContactPhi < Chaos_Collision_CollisionClipTolerance) || !ManifoldPoint.NetImpulse.IsNearlyZero();
			if (!bProcessContact)
			{
				return;
			}

			// Do not early out on negative normal velocity since there can still be an accumulated impulse
			*IterationParameters.NeedsAnotherIteration = true;

			const FVec3 ContactVelocity0 = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, RelativeContactPoint0);
			const FVec3 ContactVelocity1 = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, RelativeContactPoint1);
			const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;  
			
			// Target normal velocity, including restitution
			const bool bApplyRestitution = bInApplyRestitution && (Contact.Restitution > 0.0f) && (ManifoldPoint.InitialContactVelocity < -ParticleParameters.RestitutionVelocityThreshold);
			FReal ContactVelocityTargetNormal = 0.0f;
			if (bApplyRestitution)
			{
				ContactVelocityTargetNormal = FMath::Max(0.0f, -Contact.Restitution * ManifoldPoint.InitialContactVelocity);
			}

			// Friction settings
			const bool bApplyFriction = bInApplyFriction;
			const FReal DynamicFriction = bApplyFriction ? Contact.Friction : (FReal)0;
			const FReal StaticFriction = bApplyFriction ? FMath::Max(DynamicFriction, Contact.AngularFriction) : (FReal)0;

			// PushOut needs to know if we applied restitution and static friction
			ManifoldPoint.bInsideStaticFrictionCone = bApplyFriction;										// Reset and updated below based on latest friction calculation
			ManifoldPoint.bRestitutionEnabled = bApplyRestitution || ManifoldPoint.bRestitutionEnabled;		// Latches to on state
			ManifoldPoint.bActive = true;																	// Reset and updated below based on net impulse

			// Calculate constraint-space mass
			const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			const FMatrix33 ConstraintMassInv =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, PBDRigid1->InvM()) : FMatrix33(0));

			// Calculate the impulse required to drive contact velocity to zero, including lateral movement, as if we have infinite friction
			// Impulse = ContactVelocityError / (J.M.Jt)
			const FVec3 ContactVelocityTarget = ContactVelocityTargetNormal * ContactNormal;
			const FVec3 ContactVelocityChange = ContactVelocityTarget - ContactVelocity;
			const FMatrix33 ConstraintMass = ConstraintMassInv.Inverse();
			FVec3 Impulse = ConstraintMass * ContactVelocityChange;

			// Clip the impulse so that the accumulated impulse is not in the wrong direction and is in the friction cone
			// Clipping the accumulated impulse instead of the incremental iteration impulse is very important for jitter
			FVec3 AccumulatedImpulse = ManifoldPoint.NetImpulse + Impulse;

			// Normal impulse
			const FReal AccumulatedImpulseNormalLen = FVec3::DotProduct(AccumulatedImpulse, ContactNormal);

			// Tangential impulse
			const FVec3 AccumulatedImpulseTangential = AccumulatedImpulse - AccumulatedImpulseNormalLen * ContactNormal;
			const FReal AccumulatedImpulseTangentialLen = AccumulatedImpulseTangential.Size();

			// Check total accumulated impulse against static friction cone
			// If within static friction cone use the already calculated impulse
			const FReal MaximumAccumulatedImpulseTangential = StaticFriction * AccumulatedImpulseNormalLen;
			if (AccumulatedImpulseTangentialLen > FMath::Max(MaximumAccumulatedImpulseTangential, KINDA_SMALL_NUMBER))
			{
				// Outside static friction cone, solve for normal relative velocity and keep tangent at cone edge
				// Note: assuming the current accumulated impulse is within the cone, then adding any vector 
				// also within the cone is guaranteed to still be in the cone. So we don't need to clip the 
				// accumulated impulse here, only the incremental impulse.
				if (bApplyFriction)
				{
					ManifoldPoint.bInsideStaticFrictionCone = false;
				}

				// Projecting the impulse, like in the following commented out line, is a simplification that fails with fast sliding contacts.
				// ClippedAccumulatedImpulse = (MaximumImpulseTangential / ImpulseTangentialSize) * ImpulseTangential + NewAccImpNormalSize * ContactNormal;
				// I.e., reducing the tangential impulse will affect the post-impulse normal velocity, requiring a change in normal impulse, which changes the frcition cone, and so on
				FVec3 Tangent = AccumulatedImpulseTangential / AccumulatedImpulseTangentialLen;
				FReal DirectionalConstraintMassInv = FVec3::DotProduct(ContactNormal, ConstraintMassInv * (ContactNormal + DynamicFriction * Tangent));
				if (FMath::Abs(DirectionalConstraintMassInv) > SMALL_NUMBER)
				{
					FReal RelativeNormalVelocity = FVec3::DotProduct(ContactVelocityChange, ContactNormal);
					const FReal ImpulseMag = RelativeNormalVelocity / DirectionalConstraintMassInv;
					Impulse = ImpulseMag * (ContactNormal + DynamicFriction * Tangent);
				}
				else
				{
					Impulse = FVec3(0);
				}

				AccumulatedImpulse = ManifoldPoint.NetImpulse + Impulse;
			}

			// Clamp the total impulse to be positive along the normal
			if (FVec3::DotProduct(AccumulatedImpulse, ContactNormal) <= (FReal)0)
			{
				Impulse = -ManifoldPoint.NetImpulse;
				AccumulatedImpulse = FVec3(0);
				ManifoldPoint.bActive = false;
			}
			ManifoldPoint.NetImpulse = AccumulatedImpulse;
			ManifoldPoint.NetAngularImpulses[0] = FVec3::CrossProduct(RelativeContactPoint0, AccumulatedImpulse);
			ManifoldPoint.NetAngularImpulses[1] = FVec3::CrossProduct(RelativeContactPoint1, AccumulatedImpulse);

			// @todo(chaos): should this clamp the accumulated impulse?
			if (Chaos_Collision_EnergyClampEnabled != 0)
			{
				// Clamp the delta impulse to make sure we don't gain kinetic energy (ignore potential energy)
				// This should not modify the output impulses very often
				Impulse = GetEnergyClampedImpulse(Particle0->CastToRigidParticle(), Particle1->CastToRigidParticle(), Impulse, RelativeContactPoint0, RelativeContactPoint1, ContactVelocity0, ContactVelocity1);
			}

			// Calculate the velocity deltas from the impulse
			if (bIsRigidDynamic0)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				DV0 = PBDRigid0->InvM() * Impulse;
				DW0 = InvI0 * AngularImpulse;
			}
			if (bIsRigidDynamic1)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, -Impulse);
				DV1 = -PBDRigid1->InvM() * Impulse;
				DW1 = InvI1 * AngularImpulse;
			}
		}


		void FixTangentialImpulses(
			FRigidBodyPointContactConstraint& Constraint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1)
		{
			// We only need to do this for dynamic-kinematic pairs (well really any time we have a large mass difference, but this will do for now)
			if (bIsRigidDynamic0 && bIsRigidDynamic1)
			{
				return;
			}

			// @todo(chaos): Remove tangential impulses that are counteracting each other...

			// Calculate tangetial axes
			// Calculate Net tangential angular impulse about each axis from all manifold points
			// Calculate Sum of magnitude of angular impulses about each axis from all manifold points
			// Scale tangential angular impulses for all manifold points about each axis so that net and sum match
		}



		void ApplyManifoldPushOutCorrection(
			const FCollisionContact& Contact,		// @todo(chaos): clean up - split out contact settings from contact state
			FManifoldPoint& ManifoldPoint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			FVec3& P0, // Centre of Mass Positions and Rotations
			FRotation3& Q0,
			FVec3& P1,
			FRotation3& Q1)
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			// If we used restitution in the Apply step, we need to make sure that PushOut enforces the same distance that
			// the velocity solve step should have applied if it fully resolved
			FReal ContactResitutionPadding = 0.0f;
			if (Chaos_Manifold_PushOut_Restitution)
			{
				// For zero restitution we put back at original distance
				// @todo(chaos): pushout restitution
			}
			ContactResitutionPadding = FMath::Max(0.0f, ContactResitutionPadding);

			// Calculate the position error we need to correct, including static friction and restitution
			// Position correction uses the deepest point on each body (see velocity correction which uses average contact)
			const bool bApplyStaticFriction = (ManifoldPoint.bInsideStaticFrictionCone && Chaos_Manifold_PushOut_StaticFriction);
			const FVec3 LocalContactPoint1 = bApplyStaticFriction ? ManifoldPoint.PrevCoMContactPoint1 : ManifoldPoint.CoMContactPoints[1];
			const FVec3 RelativeContactPoint0 = Q0 * ManifoldPoint.CoMContactPoints[0];
			const FVec3 RelativeContactPoint1 = Q1 * LocalContactPoint1;
			const FVec3 ContactNormal = Q1 * ManifoldPoint.CoMContactNormal;
			const FVec3 ContactError = (P1 + RelativeContactPoint1) - (P0 + RelativeContactPoint0) + ContactResitutionPadding * ContactNormal;

			// We never pull the contact together here unless we have already applied some positive correction, in which case we may undo some of that
			const FReal ContactErrorNormal = FVec3::DotProduct(ContactError, ContactNormal);
			if ((ContactErrorNormal < 0.0f) && ManifoldPoint.NetPushOut.IsNearlyZero())
			{
				return;
			}

			// Do not early out on negative normal velocity since there can still be an accumulated impulse
			*IterationParameters.NeedsAnotherIteration = true;

			// Calculate joint-space mass matrix (J.M.Jt)
			const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
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
				PushOut = ContactMass * ContactError;
			}
			else
			{
				const FReal PushOutDenominator = FVec3::DotProduct(ContactNormal, ContactMassInv * ContactNormal);
				if (PushOutDenominator > SMALL_NUMBER)
				{
					PushOut = (ContactErrorNormal / PushOutDenominator) * ContactNormal;
				}
			}

			// Ensure that the accumulated pushout on this contact point is positive.
			// We are allowed to have negative pushout in an iteration if it partially undoes
			// some previous positive pushout. If we have gone negative, we undo all pushout
			// from this contact.
			FVec3 NetPushOut = ManifoldPoint.NetPushOut + PushOut;
			const FReal NetPushOutNormalLen = FVec3::DotProduct(NetPushOut, ContactNormal);
			if (NetPushOutNormalLen <= (FReal)0)
			{
				NetPushOut = FVec3(0);
				PushOut = -ManifoldPoint.NetPushOut;
			}
			ManifoldPoint.NetPushOut = NetPushOut;

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
		}


		void ApplyManifoldPushOutVelocityCorrection(
			const FCollisionContact& Contact,
			FManifoldPoint& ManifoldPoint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1)
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			const FReal ContactPhi = ManifoldPoint.ContactPoint.Phi;

			const FVec3 ContactVelocity0 = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, RelativeContactPoint0);
			const FVec3 ContactVelocity1 = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, RelativeContactPoint1);
			const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
			const FReal ContactVelocityNormal = FVec3::DotProduct(ContactVelocity, ContactNormal);
			if (ContactVelocityNormal > 0.0f)
			{
				return;
			}

			const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			const FMatrix33 ContactMassInv =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, PBDRigid1->InvM()) : FMatrix33(0));

			FVec3 Impulse = FVec3(0);
			FReal ImpulseDenominator = FVec3::DotProduct(ContactNormal, ContactMassInv * ContactNormal);
			if (FMath::Abs(ImpulseDenominator) > SMALL_NUMBER)
			{
				Impulse = (-ContactVelocityNormal / ImpulseDenominator) * ContactNormal;
			}

			if (bIsRigidDynamic0)
			{
				// Velocity update for next step
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				const FVec3 DV0 = PBDRigid0->InvM() * Impulse;
				const FVec3 DW0 = InvI0 * AngularImpulse;

				PBDRigid0->V() += DV0;
				PBDRigid0->W() += DW0;
			}

			if (bIsRigidDynamic1)
			{
				// Velocity update for next step
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, -Impulse);
				const FVec3 DV1 = -PBDRigid1->InvM() * Impulse;
				const FVec3 DW1 = InvI1 * AngularImpulse;

				PBDRigid1->V() += DV1;
				PBDRigid1->W() += DW1;
			}
		}


		void ApplyContactManifold(
			FRigidBodyPointContactConstraint& Constraint,
			TGenericParticleHandle<FReal, 3> Particle0,
			TGenericParticleHandle<FReal, 3> Particle1,
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

			Constraint.AccumulatedImpulse = FVec3(0);

			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			for (int32 PointIndex = ManifoldPoints.Num() - 1; PointIndex >= 0; --PointIndex)
			{
				FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

				FVec3 DV0 = FVec3(0);
				FVec3 DW0 = FVec3(0);
				FVec3 DV1 = FVec3(0);
				FVec3 DW1 = FVec3(0);

				CalculateManifoldVelocityCorrection(
					Constraint.Manifold,
					ManifoldPoint,
					Particle0,
					Particle1,
					IterationParameters,
					ParticleParameters,
					bIsRigidDynamic0,
					bIsRigidDynamic1,
					P0, Q0, P1, Q1,
					true, true,
					DV0, DW0, DV1, DW1);

				if (bIsRigidDynamic0)
				{
					PBDRigid0->V() += DV0;
					PBDRigid0->W() += DW0;
					P0 += (DV0 * IterationParameters.Dt);
					Q0 += FRotation3::FromElements(DW0, 0.f) * Q0 * IterationParameters.Dt * FReal(0.5);
					Q0.Normalize();
					FParticleUtilities::SetCoMWorldTransform(PBDRigid0, P0, Q0);
				}

				if (bIsRigidDynamic1)
				{
					PBDRigid1->V() += DV1;
					PBDRigid1->W() += DW1;
					P1 += (DV1 * IterationParameters.Dt);
					Q1 += FRotation3::FromElements(DW1, 0.f) * Q1 * IterationParameters.Dt * FReal(0.5);
					Q1.Normalize();
					FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
				}

				Constraint.AccumulatedImpulse += ManifoldPoint.NetImpulse;
			}

			// Eliminate any tangential impulses that are opposing each other. This helps with static friction
			FixTangentialImpulses(
				Constraint,
				Particle0,
				Particle1,
				IterationParameters,
				ParticleParameters,
				bIsRigidDynamic0,
				bIsRigidDynamic1,
				P0, Q0, P1, Q1);
		}


		void ApplyPushOutManifold(
			FRigidBodyPointContactConstraint& Constraint,
			const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
			TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[0]);
			TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[1]);

			bool IsTemporarilyStatic0 = IsTemporarilyStatic.Contains(Particle0->GeometryParticleHandle());
			bool IsTemporarilyStatic1 = IsTemporarilyStatic.Contains(Particle1->GeometryParticleHandle());
			// In the case of two objects which are at the same level in shock propagation which end
			// up in contact with each other, treat each object as not temporarily static. This can
			// happen, for example, at the center of an arch, or between objects which are sliding into
			// each other on a static surface.
			if ((IsTemporarilyStatic0 && IsTemporarilyStatic1) || !Chaos_Collision_UseShockPropagation)
			{
				IsTemporarilyStatic0 = false;
				IsTemporarilyStatic1 = false;
			}

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && (PBDRigid0->ObjectState() == EObjectStateType::Dynamic) && !IsTemporarilyStatic0;
			const bool bIsRigidDynamic1 = PBDRigid1 && (PBDRigid1->ObjectState() == EObjectStateType::Dynamic) && !IsTemporarilyStatic1;

			if (!bIsRigidDynamic0 && !bIsRigidDynamic1)
			{
				return;
			}

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			
			if (Chaos_Manifold_PushOut_PositionCorrection)
			{
				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

					//if ((ManifoldPoint.ContactPoint.Phi >= 0.0f) && ManifoldPoint.NetPushOut.IsNearlyZero())
					//{
					//	continue;
					//}

					ApplyManifoldPushOutCorrection(
						Constraint.Manifold,
						ManifoldPoint,
						Particle0,
						Particle1,
						IterationParameters,
						ParticleParameters,
						bIsRigidDynamic0,
						bIsRigidDynamic1,
						P0, Q0, P1, Q1);
				}
			}

			if (Chaos_Manifold_PushOut_VelocityCorrection)
			{
				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

					if (!ManifoldPoint.NetPushOut.IsNearlyZero())
					{
						ApplyManifoldPushOutVelocityCorrection(
							Constraint.Manifold,
							ManifoldPoint,
							Particle0,
							Particle1,
							IterationParameters,
							ParticleParameters,
							bIsRigidDynamic0,
							bIsRigidDynamic1,
							P0, Q0, P1, Q1);
					}
				}
			}
		}

	}
}